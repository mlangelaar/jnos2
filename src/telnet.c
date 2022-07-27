/* Internet Telnet client
 * Copyright 1991 Phil Karn, KA9Q
 */
/* Mods by PA0GRI */
#ifdef  __TURBOC__
#include <io.h>
#include <fcntl.h>
#endif
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "telnet.h"
#include "session.h"
#include "proc.h"
#include "tty.h"
#include "commands.h"
#include "netuser.h"
#include <ctype.h>
#ifdef LZW
#include "lzw.h"
#endif
#ifdef MD5AUTHENTICATE
#include "md5.h"
#include "files.h"	/* need md5sum() */
#ifdef EXPEDITE_BBS_CMD
static char *etel_login __ARGS((char *host));
#endif
#endif
  
static int filemode __ARGS((FILE *fp,int mode));
#define CTLZ    26
  
int Refuse_echo = 0;
int Tn_cr_mode = 0;    /* if true turn <cr> to <cr-nul> */
  
#undef DEBUG
  
#ifdef  DEBUG
char *T_options[] = {
    "Transmit Binary",      /* 0 */
    "Echo",             /* 1 */
    "",             /* 2 */
    "Suppress Go Ahead",        /* 3 */
    "",             /* 4 */
    "Status",           /* 5 */
    "Timing Mark"           /* 6 */
#ifdef  TN_KK6JQ
    ,
    "Remote Controlled Transmission/Echo",
    "Negotiate Line Width",
    "Negotiate Page Size",
    "Negotiate CR Disposition",
    "Negotiate Horz Tab Stops",
    "Negotiate Horz tab Disposition",
    "Negotiate FormFeed Disposition",
    "Negotiate Vert Tab Stops",
    "Negotiate Vert Tab Disposition",
    "Negotiate LineFeed Disposition",
    "Extended ASCII",
    "Force Logout",
    "Byte Macro",
    "Data Entry Terminal",
    "Protocol supdup",
    "supdup",
    "Send Location",
    "Terminal Type",
    "End of Record"
#endif  /* TN_KK6JQ */
};
#endif  /* DEBUG */
  
#if defined BBSSESSION || defined TELNETSESSION || defined TTYLINKSESSION
/* Execute user telnet,ttylink or bbs command */
int
dotelnet(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct session *sp;
    struct sockaddr_in fsocket;
    char *name, firstch;
#if defined (MD5AUTHENTICATE) && defined (EXPEDITE_BBS_CMD)
    char *cp, *cp1;
#endif
    int split = 0;
    int bbs = 0;
    int auth = 0;
#ifdef LZW
    int lzw = 0;
#endif
  
    /*Make sure this comes from console - WG7J*/
    if(Curproc->input != Command->input)
        return 0;
  
    name = argv[1];     /* This is not valid for 'bbs' command ! */
    firstch = tolower(*argv[0]);
#ifdef BBSSESSION
    if(firstch == 'b') {
        name = "Local BBS";
        bbs = 1;
    }
#ifdef TTYLINKSESSION
    else
#endif
#endif /* BBSSESSION */

#ifdef MD5AUTHENTICATE
        {
            if (firstch == 'e') { /* etelnet, ettylink */
                auth++;
                firstch = tolower(argv[0][1]);
            }
#endif /* MD5AUTHENTICATE */
#ifdef TTYLINKSESSION
#ifdef LZW
        if(firstch == 'l') /* lzw-link command */
            lzw = split = 1;
        else
#endif
            if (firstch == 't' && tolower(argv[0][auth+1]) == 't') /* tty-link command */
                split = 1;
#endif /* TTYLINKSESSION */
#ifdef MD5AUTHENTICATE
        }
#endif

    /* Allocate a session descriptor */
    if((sp = newsession(name,TELNET,split)) == NULLSESSION){
        j2tputs(TooManySessions);
        return 1;
    }
    fsocket.sin_family = AF_INET;
    if(argc < (auth ? 5 : 3)) {  /* is the port defaulted? */
#ifdef LZW
        if(lzw)
            fsocket.sin_port = IPPORT_XCONVERS;
        else
#endif
#ifdef TTYLINKSESSION
        if(split)
            fsocket.sin_port = IPPORT_TTYLINK;
        else
#endif
            fsocket.sin_port = IPPORT_TELNET;
    } else {
        fsocket.sin_port = atoip(argv[2]);
    }
#ifdef BBSSESSION
    if(bbs) {
        /* 127.0.0.1 is the loopback interface */
        fsocket.sin_addr.s_addr = 0x7f000001L;

	/*
	 * 14Nov2020, Maiko, thanks Gus - the original TNODE mod had a
	 * mistake, so we'll deal with it this way (commented out). But
	 * warn the user if '#define TNODE' is set, so that the intent
	 * of this TNODE mod is not lost forever ... Bwuhahaha ;->
	 */

//	if you enable "node" you may use the below
//          fsocket.sin_port = IPPORT_NODE;

#ifdef	TNODE
        tprintf ("not using TNODE port, edit telnet.c to change ...\n");
#endif

    } else
#endif
    {
        tprintf("Resolving %s... ",sp->name);
        if((fsocket.sin_addr.s_addr = resolve(sp->name)) == 0){
            tprintf(Badhost,sp->name);
            keywait(NULLCHAR,1);
            freesession(sp);
            return 1;
        }
    }
    if((sp->s = j2socket(AF_INET,SOCK_STREAM,0)) == -1){
        j2tputs(Nosock);
        keywait(NULLCHAR,1);
        freesession(sp);
        return 1;
    }
#if defined LZW
    if (lzw) /* to achieve non-split lzw-telnet, use the session cmd to turn split off */
        lzwinit(sp->s,Lzwbits,Lzwmode);
#endif
#ifdef MD5AUTHENTICATE
    if (auth && argc > 3) {  /* etelnet dest [port] loginid passwd */
        sp->ufile = j2strdup(argv[argc-2]);  /* Reuse the upload filename ptr briefly */
        sp->rfile = j2strdup(argv[argc-1]);  /* Ditto, for record filename ptr */
        /* n5knx: Note 1) we assume the last 2 args are id and passwd, and 2) if we
         * abort henceforth, freesession() will free the ufile/rfile allocations.
         */
    }
#ifdef EXPEDITE_BBS_CMD
    else if (bbs) {  /*  Do bbs autologin if possible. */
        if ((cp = etel_login(Hostname)) != NULLCHAR) {
            cp1 = strchr(cp,' ');
            *cp1++ = '\0';            /* Now Points to Password */
            sp->ufile = j2strdup(cp);   /* Reuse the upload filename ptr briefly */
            sp->rfile = j2strdup(cp1);  /* Ditto, for record filename ptr */
            free(cp);
        }
    }
#endif /* EXPEDITE_BBS_CMD */
#endif /* MD5AUTHENTICATE */
    return tel_connect(sp,(char *)&fsocket,SOCKSIZE);
}

#if defined(MD5AUTHENTICATE) && defined(EXPEDITE_BBS_CMD)
/*
 * Read net.rc file, looking for host's entry.  Then return either  
 * "username password" or NULLCHAR if not found. - KA1NNN/N5KNX 11/96
 * Patterned after the ftpcli_login() routine in ftpcli.c.
 */
static char *
etel_login(host)
char *host;
{
  
    char buf[80],*cp2,*cp3,*cp4;
    FILE *fp;

    extern char *Hostfile;  /* List of user names and permissions */
  
    if((fp = fopen(Hostfile,"r")) == NULLFILE){
        return NULLCHAR;
    }
    while(fgets(buf,sizeof(buf),fp),!feof(fp)){
        buf[strlen(buf)-1] = '\0';      /* Nuke the newline */
        if(buf[0] == '#')
            continue;       /* Comment */
        if((cp2 = strpbrk(buf," \t")) == NULLCHAR)
            /* Bogus entry */
            continue;
        *cp2++ = '\0';           /* Now points to user name */
        if(strcmp(host,buf) == 0)
            break;          /* Found host name */
    }
    if(feof(fp)){
        /* User name not found in file */
        fclose(fp);
        return NULLCHAR;
    }
    fclose(fp);
    /* Skip whitespace before user field in file */
    cp2 = skipwhite(cp2);
    if((cp3 = strpbrk(cp2," \t")) == NULLCHAR)
        /* Not there */
        return NULLCHAR;
    else {
        *cp3++ = ' ';            /* Now points to password */
        cp4 = skipwhite(cp3);
        if (*cp4 == '-') cp4++;  /* Skip leading '-' in password */
        strcpy(cp3,cp4);         /* Copy password so it follows userid " " */
        return j2strdup(cp2);
    }
}
#endif /* MD5AUTHENTICATE && EXPEDITE_BBS_CMD */
#endif /* TELNET || TTYLINK || BBS */
  
/* Generic interactive connect routine, used by Telnet, AX.25, NET/ROM */
int
tel_connect(sp,fsocket,len)
struct session *sp;
char *fsocket;
int len;
{
    unsigned int index;
    struct telnet tn;
  
    index = (unsigned int) (sp - Sessions);
    memset((char *)&tn,0,sizeof(tn));
    tn.eolmode = Tn_cr_mode;
    tn.session = sp;    /* Upward pointer */
    sp->cb.telnet = &tn;    /* Downward pointer */
    sockmode(sp->s,SOCK_ASCII); /* Default to ascii mode */
  
    tprintf("Trying %s...\n",psocket((struct sockaddr *)fsocket));
    if(j2connect(sp->s,fsocket,len) == -1){
        tprintf("%s session %u failed: %s errno %d\n",
        Sestypes[sp->type], index, sockerr(sp->s),errno);
  
        keywait(NULLCHAR,1);
        freesession(sp);
        return 1;
    }
    tprintf("%s session %u connected to %s\n",
    Sestypes[sp->type],index,sp->name);
    tnrecv(&tn);
    return 0;
}
  
/* Telnet input routine, common to both telnet and ttylink */
void
tnrecv(tn)
struct telnet *tn;
{
    int c,s,index;
    int Ignore_refuse_echo=0;
    struct sockaddr_in fsocket;
    struct session *sp;
    char *cp;
#ifdef MD5AUTHENTICATE
    int state=0;
    int32 challenge=0;
    int send_passwd = 0; /* replaces GOTO 'send_passwd:' label */
/*    MD5_CTX md;*/
#endif
  
    sp = tn->session;
    s = sp->s;
  
#ifdef MD5AUTHENTICATE
    if(sp->ufile != NULLCHAR)
        state=1;   /* we wait for the "login: " prompt */
#endif

    /* slight complication: if we set 'echo refuse' then a telnet to the
       localhost will be sub-optimal (Mbox's --more-- prompting, etc).
       So we want to ignore Refuse_echo iff remote IP addr is localhost.
       Yes, this is a Kludge (capital K).  Better ideas? -- n5knx */
    c = sizeof(fsocket);
    if(j2getpeername(s,(char *)&fsocket,&c) == 0 && c != 0 &&
       fsocket.sin_addr.s_addr==0x7f000001L)
        Ignore_refuse_echo = 1;

    index = (unsigned int) (sp - Sessions);
  
    /* Fork off the transmit process */
    sp->proc1 = newproc("tel_out",1024,tel_output,0,tn,NULL,0);
  
    /* Process input on the connection */
    while((c = recvchar(s)) != -1){
        if(c != IAC){
#ifdef notdef
            /* Allow international character sets to pass - WG7J */
            /* Ordinary character */
            if(!tn->remote[TN_TRANSMIT_BINARY])
                c &= 0x7f;
#endif
            tputc((char)c);
#ifdef MD5AUTHENTICATE
            if (state) {
            	send_passwd = 0; /* replaces GOTO 'send_passwd:' label */
                c &= 0x7f;
                if(state<8) {  /* waiting for login: */
                    if( c == *("login: "+state-1))
                        state++;
                    else
                        state=1;
                    if(state == 8) {  /* send login name */
                        usprintf(s,"%s\n",sp->ufile);
                        tprintf("%s\n",sp->ufile); /* also to local screen */
                        free(sp->ufile);
                        sp->ufile=NULLCHAR;
                    }
                }
                else if(state==8) {  /* await start of challenge, ie, Password [cccc] : */
                    if(c == '[')  /* starts...*/
                        state++;
                    else if (c == ':') {  /* no challenge, can't encrypt */
                        cp = sp->rfile;  /* send cleartext */
                        send_passwd = 1;
                    }
                }
                else if(state==9) {  /* store challenge */
                    if(c == ']') {  /* end of challenge */
#ifdef notdef
                        MD5Init(&md);
                        MD5Update(&md,(unsigned char *)&challenge,sizeof(challenge));
                        MD5Update(&md,(unsigned char *)sp->rfile,strlen(sp->rfile));
                        MD5Final(&md);
                        for(i=0; i<16; i++) {
                            usprintf(s,"%02x",md.digest[i]);
                            tputc('.');
                        }
                        usputc(s,'\n');
                        tputc('\n');
#endif
                        cp = md5sum(challenge, sp->rfile);
			send_passwd = 1;
                    }
                    else {
                        challenge <<= 4;
                        challenge += (c>'9') ? (tolower(c)-'a'+10) : c-'0';
                    }
                }
		if (send_passwd)
		{
                    usprintf(s,"%s\n", cp);
                    tprintf("%s\n", cp);

                    free(sp->rfile);  /* best not to send passwd in the clear */
                    sp->rfile=NULLCHAR;
                    state=0;
		}
            }
#endif /* MD5AUTHENTICATE */
            continue;
        }
        /* IAC received, get command sequence */
        c = recvchar(s);
        switch(c){
            case WILL:
                c = recvchar(s);
                willopt(tn,c,Ignore_refuse_echo);
                break;
            case WONT:
                c = recvchar(s);
                wontopt(tn,c);
                break;
            case DO:
                    c = recvchar(s);
                    doopt(tn,c);
                    break;
            case DONT:
                c = recvchar(s);
                dontopt(tn,c);
                break;
#ifdef  TN_KK6JQ
            case SB:
                c = recvchar(s);
                dosb(tn,c);
                break;
#endif  /* TN_KK6JQ */
            case IAC:   /* Escaped IAC */
                tputc(IAC);
                break;
        }
    }
    /* A close was received from the remote host.
     * Notify the user, kill the output task and wait for a response
     * from the user before freeing the session.
     */
    sockmode(sp->output,SOCK_ASCII); /* Restore newline translation */
    cp = sockerr(s);
    tprintf("%s session %u", Sestypes[sp->type],index);
    tprintf(" closed: %s\n", cp != NULLCHAR ? cp : "EOF");
    killproc(sp->proc1);
    sp->proc1 = NULLPROC;
    close_s(sp->s);
    sp->s = -1;
    keywait(NULLCHAR,1);
    freesession(sp);
}
  
/* User telnet output task, started by user telnet command */
void
tel_output(unused,tn1,p)
int unused;
void *tn1;
void *p;
{
    struct session *sp;
    int c;
    struct telnet *tn;
  
    tn = (struct telnet *)tn1;
    sp = tn->session;
  
    /* Send whatever's typed on the terminal */
    while((c = recvchar(sp->input)) != EOF){
        usputc(sp->s,(char)c);
        if(!tn->remote[TN_ECHO] && sp->record != NULLFILE)
            putc(c,sp->record);
  
        /* By default, output is transparent in remote echo mode.
         * If eolmode is set, turn a cr into cr-null.
         * This can only happen when in remote echo (raw) mode, since
         * the tty driver normally maps \r to \n in cooked mode.
         */
        if(c == '\r' && tn->eolmode)
            usputc(sp->s,'\0');
  
        if(tn->remote[TN_ECHO])
            usflush(sp->s);
    }
    /* Make sure our parent doesn't try to kill us after we exit */
    sp->proc1 = NULLPROC;
}
int
doecho(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2){
        if(Refuse_echo)
            j2tputs("Refuse\n");
        else
            j2tputs("Accept\n");
    } else {
        if(argv[1][0] == 'r')
            Refuse_echo = 1;
        else if(argv[1][0] == 'a')
            Refuse_echo = 0;
        else
            return -1;
    }
    return 0;
}
/* set for unix end of line for remote echo mode telnet */
int
doeol(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2){
        if(Tn_cr_mode)
            j2tputs("null\n");
        else
            j2tputs("standard\n");
    } else {
        if(argv[1][0] == 'n')
            Tn_cr_mode = 1;
        else if(argv[1][0] == 's')
            Tn_cr_mode = 0;
        else {
            tprintf("Usage: %s [standard|null]\n",argv[0]);
            return -1;
        }
    }
    return 0;
}
  
/* The guts of the actual Telnet protocol: negotiating options */
void
willopt(tn,opt,ignore_refuse_echo)
struct telnet *tn;
int opt, ignore_refuse_echo;
{
    int ack;
  
#ifdef  DEBUG
    printf("recv: will ");
    if(uchar(opt) <= NOPTIONS)
        printf("%s\n",T_options[opt]);
    else
        printf("%u\n",opt);
#endif
  
    switch(uchar(opt)){
        case TN_TRANSMIT_BINARY:
        case TN_ECHO:
        case TN_SUPPRESS_GA:
            if(tn->remote[uchar(opt)] == 1)
                return;     /* Already set, ignore to prevent loop */
            if(uchar(opt) == TN_ECHO){
                if(Refuse_echo && !ignore_refuse_echo){
                /* User doesn't want to accept */
                    ack = DONT;
                    break;
                } else {
                /* Put tty into raw mode */
                    tn->session->ttystate.edit = 0;
                    tn->session->ttystate.echo = 0;
                    sockmode(tn->session->s,SOCK_BINARY);
                    sockmode(tn->session->input,SOCK_BINARY);
                    sockmode(tn->session->output,SOCK_BINARY);
                    if(tn->session->record != NULLFILE)
                        filemode(tn->session->record,SOCK_BINARY);
  
                }
            }
            tn->remote[uchar(opt)] = 1;
            ack = DO;
                break;
#ifdef TN_KK6JQ
        case TN_TERM_TYPE:  /* We are the client, we don't want this */
#endif /* TN_KK6JQ */
        default:
            ack = DONT; /* We don't know what he's offering; refuse */
    }
    answer(tn,ack,opt);
}
void
wontopt(tn,opt)
struct telnet *tn;
int opt;
{
#ifdef  DEBUG
    printf("recv: wont ");
    if(uchar(opt) <= NOPTIONS)
        printf("%s\n",T_options[uchar(opt)]);
    else
        printf("%u\n",uchar(opt));
#endif
    if(uchar(opt) <= NOPTIONS){
        if(tn->remote[uchar(opt)] == 0)
            return;     /* Already clear, ignore to prevent loop */
            tn->remote[uchar(opt)] = 0;
            if(uchar(opt) == TN_ECHO){
            /* Put tty into cooked mode */
                tn->session->ttystate.edit = 1;
                tn->session->ttystate.echo = 1;
                sockmode(tn->session->s,SOCK_ASCII);
                sockmode(tn->session->input,SOCK_ASCII);
                sockmode(tn->session->output,SOCK_ASCII);
                if(tn->session->record != NULLFILE)
                    filemode(tn->session->record,SOCK_ASCII);
            }
    }
    answer(tn,DONT,opt);    /* Must always accept */
}
  
#ifdef  TN_KK6JQ
void
dosb(tn, opt)
struct telnet *tn;
int opt;
{
    struct session *sp;
    int s, c;
    char    *name, resp_buf[64];
    int resp_len = 0;
  
    sp = tn->session;
    s = sp->s;
  
    switch (uchar(opt)) {
        case TN_TERM_TYPE:
        /* make sure qualifier is SEND */
        /* ignore otherwise */
  
        /* Karn code is cavalier about EOF when in an IAC sequence.. */
            c = recvchar(s);
            if (c == TS_SEND) {
                c = recvchar(s);    /* should be IAC */
                c = recvchar(s);    /* should be SE */
/*
 * Should check to make sure sequence includes IAC, SE and somehow fail
 * if it doesn't.
 */
                name = getenv("TERM");
                if ((name==NULL) || ((resp_len = strlen(name)) > 58))
                    name = "UNKNOWN";
                sprintf(resp_buf, "%c%c%c%c%s%c%c",
                    IAC, SB, TN_TERM_TYPE, TS_IS, name, IAC, SE);
                resp_len = 6+strlen(name);  /* since TS_IS is 0 (tnx Jim 7J1AJH) */
            }
            break;
        default:
            break;
    }
  
    if (resp_len > 0)
        j2send(s, resp_buf, resp_len, 0);
}
#endif  /* TN_KK6JQ */
  
void
doopt(tn,opt)
struct telnet *tn;
int opt;
{
    int ack;
  
#ifdef  DEBUG
    printf("recv: do ");
    if(uchar(opt) <= NOPTIONS)
        printf("%s\n",T_options[uchar(opt)]);
    else
        printf("%u\n",uchar(opt));
#endif
    switch(uchar(opt)){
        case TN_SUPPRESS_GA:
#ifdef  TN_KK6JQ
        case TN_TERM_TYPE:
#endif  /* TN_KK6JQ */
            if(tn->local[uchar(opt)] == 1)
                return;     /* Already set, ignore to prevent loop */
                tn->local[uchar(opt)] = 1;
                ack = WILL;
                break;
        default:
            ack = WONT; /* Don't know what it is */
    }
    answer(tn,ack,opt);
}
void
dontopt(tn,opt)
struct telnet *tn;
int opt;
{
#ifdef  DEBUG
    printf("recv: dont ");
    if(uchar(opt) <= NOPTIONS)
        printf("%s\n",T_options[uchar(opt)]);
    else
        printf("%u\n",uchar(opt));
#endif
    if(uchar(opt) <= NOPTIONS){
        if(tn->local[uchar(opt)] == 0){
            /* Already clear, ignore to prevent loop */
            return;
        }
        tn->local[uchar(opt)] = 0;
    }
    answer(tn,WONT,opt);
}
void
answer(tn,r1,r2)
struct telnet *tn;
int r1,r2;
{
    char s[3];
  
#ifdef  DEBUG
    switch(r1){
        case WILL:
            printf("sent: will ");
            break;
        case WONT:
            printf("sent: wont ");
            break;
        case DO:
            printf("sent: do ");
            break;
        case DONT:
            printf("sent: dont ");
            break;
    }
    if(r2 <= NOPTIONS)
        printf("%s\n",T_options[r2]);
    else
        printf("%u\n",r2);
#endif
  
    s[0] = IAC;
    s[1] = r1;
    s[2] = r2;
    j2send(tn->session->s,s,3,0);
}
#ifdef  __TURBOC__
/* Set end-of-line translation mode on file */
static int
filemode(fp,mode)
FILE *fp;
int mode;
{
    int omode;
  
    if(fp == NULLFILE)
        return -1;
  
    if(fp->flags & _F_BIN)
        omode = SOCK_BINARY;
    else
        omode = SOCK_ASCII;
  
    switch(mode){
        case SOCK_BINARY:
            fp->flags = _F_BIN;
            setmode(fileno(fp),O_BINARY);
            break;
        case SOCK_ASCII:
            fp->flags &= ~_F_BIN;
            setmode(fileno(fp),O_TEXT);
            break;
    }
    return omode;
}
#else
static int
filemode(fp,mode)
FILE *fp;
int mode;
{
    return 0;
}
#endif
