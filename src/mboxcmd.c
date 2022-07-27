/* NOTE: because of size, the previous 'mailbox.c' has been
 * split in 3 parts:
 * mboxcmd.c, containing the 'mbox' subcommands,
 * mailbox.c, containing some user mailbox commands, and
 * mailbox2.c, containing the remaining user commands.
 * 921125 - WG7J
 */
/* There are only two functions in this mailbox code that depend on the
 * underlying protocol, namely mbx_getname() and dochat(). All the other
 * functions can hopefully be used without modification on other stream
 * oriented protocols than AX.25 or NET/ROM.
 *
 * SM0RGV 890506, most work done previously by W9NK
 *
 *** Changed 900114 by KA9Q to use newline mapping features in stream socket
 *      interface code; everything here uses C eol convention (\n)
 *
 *      Numerous new commands and other changes by SM0RGV, 900120
 *
 * Gateway function now support outgoing connects with the user's call
 * with inverted ssid. Users can connect to system alias as well...
 * See also several mods in socket.c,ax25.c and others
 * 11/15/91, WG7J/PA3DIS
 *
 * Userlogging, RM,VM and KM commands, and R:-line interpretation
 * added 920307 and later, Johan. K. Reinalda, WG7J/PA3DIS
 *
 * Inactivity timeout-disconnect added 920325 and later - WG7J
 *
 */
#ifdef MSDOS
#include <io.h>
#endif
#include <time.h>
#include <ctype.h>
#ifdef MSDOS
#include <alloc.h>
#endif
#include <time.h>
#ifdef  UNIX
#include <sys/types.h>
#endif
#include <sys/stat.h>
#ifdef MSDOS
#include <dir.h>
#include <dos.h>
#endif
#include "global.h"
#ifdef MAILBOX
#include "timer.h"
#include "proc.h"
#include "socket.h"
#include "usock.h"
#include "session.h"
#include "smtp.h"
#include "dirutil.h"
#include "telnet.h"
#include "ftp.h"
#include "ftpserv.h"
#include "commands.h"
#include "netuser.h"
#include "files.h"
#include "bm.h"
#include "pktdrvr.h"
#include "ax25.h"
#include "mailbox.h"
#include "ax25mail.h"
#include "nr4mail.h"
#include "cmdparse.h"
#include "mailfor.h"

#ifdef	B2F
#include "b2f.h"	/* 08Mar2009, Maiko, B2F prototypes */
#endif

#include "j2KLMlists.h"	/* 20Jun2020, Maiko, now using calls list for winlink call */

/*
#define MBDEBUG
*/
  
extern int dombmovemail __ARGS((int argc,char *argv[],void *p));
extern char Myalias[];
  
extern struct mbx *Mbox;
extern int BbsUsers;
extern int MbMaxUsers;
extern int Totallogins;
char *Mtmsg;
#ifdef TTYLINKSERVER
int MAttended = 0;
#endif
char Mbpasswd[MAXPWDLEN+1] = "";

#ifdef MBFWD
char *Mbzip = NULLCHAR;
char *Mbqth = NULLCHAR;
char *Mbhaddress = NULLCHAR;
char *Mbfwdinfo = NULLCHAR;
int Mbsmtptoo = 0;
int Mtrace = 0;

/* 29Jun2016, Maiko (VE4KLM), default is on, but they can be a nuisance */
int Meolwarnings = 1;

#if defined(FBBCMP)

/* 10Jun2016, Maiko (VE4KLM), no more of this Mfbb = 3 crap, not necessary */
#ifdef	NO_MORE_MBOX_FBB_3_JUST_A_STUPID_IDEA
#ifdef B2F
int16 Mfbb = 3;
#else
int16 Mfbb = 2;
#endif
#endif

/* 10Jun2016, Maiko (VE4KLM), no more of this Mfbb = 3 crap, not necessary */
int16 Mfbb = 2;

#elif defined(FBBFWD)
int16 Mfbb = 1;
#endif
#endif //MBFWD

/* 29Dec2013, Maiko, Option to force 'm->to' when Hdr[TO] written to tfile */
int MbrewriteToHdr = 0;
  
extern int MbSent;
extern int MbRead;
extern int MbRecvd;
#ifdef MBFWD
extern int MbForwarded;
#endif

#ifdef HTTPVNC
static char VNClink[] =    "Browser  (%s)";	/* New, 03Jul09, VE4KLM */
#endif 
#ifdef HFDD
static char HFlink[] =     "Pactor   (%s)";	/* New, 12Apr07, VE4KLM */
#endif 
#ifdef AX25
static char Uplink[] =     "Uplink   (%s)";
static char Downlink[] =   "Downlink (%s)";
#endif
#ifdef NETROM
static char incircuit[]  = "Circuit  (%s%s%s %s)";
static char outcircuit[] = "Circuit  (%s%s%s)";
#endif
static char Telnet[] =     "Telnet   (%s @ %s)";
static char Telnetdown[] = "Telnet   (%s)";
static char Local[] =      "Local    (%s)";
  
static int doattend __ARGS((int argc,char *argv[],void *p));
int dombusers __ARGS((int argc,char *argv[],void *p));
extern int dombpast __ARGS((int argc,char *argv[],void *p));

#ifdef  J2_SID_CAPTURE
/* 13May2016, Maiko (VE4KLM), SID tracking feature :) */
extern int dosidmbxpast (int, char**, void*);
#endif

#ifdef MAILBOX
#if ((defined AX25) || (defined NETROM))
static int dombnrid __ARGS((int argc,char *argv[],void *p));
#endif
#endif
static int dombpasswd __ARGS((int argc,char *argv[],void *p));
static int dombsecure __ARGS((int argc,char *argv[],void *p));

/* 29Jun2016, Maiko (VE4KLM), warnings can be a nuisance :) */
static int doeolwarnings (int, char**, void*);

static int dombtrace __ARGS((int argc,char *argv[],void *p));
#ifdef FBBFWD
static int dombfbb __ARGS((int argc,char *argv[],void *p));
#endif
static int dombzipcode __ARGS((int argc,char *argv[],void *p));
static int dombfwdinfo __ARGS((int argc,char *argv[],void *p));
static int dombqth __ARGS((int argc,char *argv[],void *p));
static int dombhaddress __ARGS((int argc,char *argv[],void *p));
static int dombsmtptoo __ARGS((int argc,char *argv[],void *p));
extern int dombstatus __ARGS((int argc,char *argv[],void *p));
int dombmailstats __ARGS((int argc,char *argv[],void *p));
static int dombtdisc __ARGS((int argc,char *argv[],void *p));
static int dombmaxusers __ARGS((int argc,char *argv[],void *p));
static int dombtmsg __ARGS((int argc,char *argv[],void *p));
static int dombsendquery __ARGS((int argc,char *argv[],void *p));
static int dombnewmail __ARGS((int argc,char *argv[],void *p));
static int dombheader __ARGS((int argc,char *argv[],void *p));
static int dombnobid __ARGS((int argc,char *argv[],void *p));
#ifdef AX25
static int dombmport __ARGS((int argc,char *argv[],void *p));
static int dombifilter __ARGS((int argc,char *argv[],void *p));
static int dombhideport __ARGS((int argc,char *argv[],void *p));
static int dombnoax25 __ARGS((int argc,char *argv[],void *p));
#endif
static int dombbonly __ARGS((int argc,char *argv[],void *p));
static int dombuonly __ARGS((int argc,char *argv[],void *p));
static int dombsonly __ARGS((int argc,char *argv[],void *p));
static int dombreset __ARGS((int argc,char *argv[],void *p));
static int dombregister __ARGS((int argc,char *argv[],void *p));
static int dombshowalias __ARGS((int argc,char *argv[],void *p));
static struct pu *pu_lookup __ARGS((char *name));
#ifdef USERLOG
static char *getstrg __ARGS((char **sp, char *cp));
#endif
#ifdef CONVERS
static int domballowconvers __ARGS((int argc,char *argv[],void *p));
#endif
static int dombrewriteToHdr (int argc, char **argv, void *p);
  
#ifdef	J2MFA
extern int domfafixed (int argc, char **argv, void *p);
#endif

int Usenrid;
int MBSecure;
  
#ifdef REGISTER
int MbRegister = 1;
#endif

#ifdef MAILCMDS
int Mbsendquery = 1;
int NoBid;
#ifdef USERLOG
int Mbnewmail = 1;
#endif
#endif
  
int32 Mbtdiscinit;

#ifdef	BLACKLIST_BAD_LOGINS
int32 MbBlackList = 0;

static int dombblacklist (int argc, char **argv, void *p)
{
	/* 16Feb2015, Maiko (VE4KLM), used to be boolean, now holds expire time */
    return setint32 (&MbBlackList, "blacklist", argc, argv);
}
#endif

#ifdef B2F

/* 23Apr2008, Maiko (VE4KLM), Manipulate the noB2F callsign list */

static int doaddnoB2F (int argc, char **argv, void *p)
{
	if (argc == 1)
		j2noB2F (NULL);		/* just display current list */
	else if (argc == 2)
		j2AddnoB2F (argv[1]);	/* add a call to the current list */

	return 0;
}

#ifdef	DONT_COMPILE

/* 09Dec2015, Maiko, Need dedicated Winlink call for challenge/response */

char Wl2kcall[AXALEN];

static int dombwl2kcall (int argc, char **argv, void *p)
{
    char tmp[AXBUF];

    if (argc < 2)
    {
        tprintf("%s\n",pax25(tmp,Wl2kcall));
        return 0;
    }

    if(setcall(Wl2kcall,argv[1]) == -1)
        return -1;

    return 0;
}

#endif

#endif	/* end of B2F */

/* mbox subcommand table */
static struct cmds DFAR Mbtab[] = {
    { "alias",        doaliases,      0, 0, NULLCHAR },
#ifdef TTYLINKSERVER
    { "attend",       doattend,       0, 0, NULLCHAR },
#endif
#ifdef	BLACKLIST_BAD_LOGINS
	/* 02Feb2015, Maiko (VE4KLM), Blacklist feature */
    { "blacklist", dombblacklist, 0, 0,NULLCHAR },
#endif
#ifdef CONVERS
    { "convers",  domballowconvers,0, 0, NULLCHAR },
#endif
#ifdef MAILCMDS
#ifdef MBFWD
#ifdef FBBFWD
    { "fbb",      dombfbb,    0, 0, NULLCHAR },
#endif
    { "fwdinfo",  dombfwdinfo,0, 0, NULLCHAR },
    { "haddress", dombhaddress,0,0, NULLCHAR },
    { "header",   dombheader, 0, 0, NULLCHAR },
#endif
#endif /* MAILCMDS */
#ifdef AX25
    { "hideport", dombhideport, 0,0, NULLCHAR },
#endif /* AX25 */
#if defined(MAILCMDS) && defined(HOLD_LOCAL_MSGS)
    { "holdlocal",dombholdlocal,0,0, NULLCHAR },
#endif
#ifdef AX25
    { "ifilter",  dombifilter,0, 0, NULLCHAR },
#endif /* AX25 */
#ifdef MAILCMDS
#ifdef MBFWD
    { "kick",     dombkick,   0, 0, NULLCHAR },
#endif
#endif /* MAILCMDS */
    { "maxusers", dombmaxusers,0,0, NULLCHAR },
#ifdef	J2MFA
 /* 21Nov2020, Maiko, now direct to calls list code */
    { "MFAexclude", docallslist,  0, 0, NULLCHAR },
 /*
  * 23Nov2020, Maiko (VE4KLM), need a fixed code for BBS authentication,
  * for now just have one sysop defined code for all incoming BBS telnet
  * forwarding, just want to get this released, can always add a future
  * function to give each BBS their own MFA code, but not right now.
  */
    { "MFAfixed", domfafixed,  0, 0, NULLCHAR },
#endif
#ifdef MAILCMDS
#ifdef AX25
#ifdef MAILFOR
    { "mailfor",  dombmailfor,0, 0, NULLCHAR },
#endif /* MAILFOR */
    { "mport",    dombmport,  0, 0, NULLCHAR },
#endif /* AX25 */
#ifdef USERLOG
    { "newmail",  dombnewmail,0, 0, NULLCHAR },
#endif
#ifdef B2F
	{ "nob2f",  doaddnoB2F,  0, 0, NULLCHAR },
#endif
    { "nobid",  dombnobid,  0, 0, NULLCHAR },
#ifdef AX25
    { "nrid",     dombnrid,   0, 0, NULLCHAR },
#endif
#endif /* MAILCMDS */
    { "past",     dombpast,   0, 0, NULLCHAR },
    { "password", dombpasswd, 0, 0, NULLCHAR },
#ifdef MAILCMDS
    { "mailstats",dombmailstats,0,0,NULLCHAR },
#ifdef MBFWD
    { "qth",      dombqth,    0, 0, NULLCHAR },
#endif
#ifdef REGISTER
    { "register", dombregister,0, 0,NULLCHAR },
#endif
#endif /* MAILCMDS */
    { "reset",    dombreset,  0, 0, NULLCHAR },

    /* 29Dec2013, Maiko, Option to force 'm->to' when Hdr[TO] written to tfile */
    { "rewriteToHdr", dombrewriteToHdr,  0, 0, NULLCHAR },

    { "secure",   dombsecure, 0, 0, NULLCHAR },
#ifdef MAILCMDS
    { "sendquery",dombsendquery,0,0,NULLCHAR },
#ifdef MBFWD
    { "smtptoo",  dombsmtptoo,0, 0, NULLCHAR },
#endif
#endif /* MAILCMDS */
    { "status",   dombstatus,  0, 0, NULLCHAR },
    { "showalias",dombshowalias,0,0, NULLCHAR },
    /* 29Jun2016, Maiko (VE4KLM), warnings can be a nuisance :) */
    { "eolwarnings", doeolwarnings,  0, 0, NULLCHAR },
#ifdef MAILCMDS
#ifdef MBFWD
    { "timer",        dombtimer,      0, 0, NULLCHAR },
#endif
#endif /* MAILCMDS */
    { "tdisc",    dombtdisc,  0, 0, NULLCHAR },
    { "tmsg",     dombtmsg,   0, 0, NULLCHAR },
#ifdef MAILCMDS
#ifdef MBFWD
    { "trace",    dombtrace,  0, 0, NULLCHAR },
#ifdef B2F
  /* 09Dec2015, Maiko, Need dedicated Winlink call for challenge/response
    { "winlinkcall", dombwl2kcall,  0, 0, NULLCHAR },
 */
 /* 20Jun2020, Maiko, now direct to calls list code */
    { "winlinkcalls", docallslist,  0, 0, NULLCHAR },
#endif
    { "zipcode",  dombzipcode,0, 0, NULLCHAR },
#endif
#endif /* MAILCMDS */
#ifdef AX25
    { "noax25",   dombnoax25, 0, 0, NULLCHAR },
#endif
    { "bbsonly",  dombbonly,  0, 0, NULLCHAR },
    { "usersonly",dombuonly,  0, 0, NULLCHAR },
    { "sysoponly",dombsonly, 0, 0, NULLCHAR },
#ifdef  J2_SID_CAPTURE
	/* 13May2016, Maiko (VE4KLM), SID tracking feature :) */
    { "sidlist", dosidmbxpast, 0, 0, NULLCHAR },
#endif
    { NULLCHAR, NULL, 0, 0, NULLCHAR }
};
  
struct alias *AliasList;
  
// add aliases to the list
int
doaliases(int argc,char *argv[],void *p) {
    struct alias *a;
    int len;
  
    if(argc < 2)    // show the aliases
        return dombalias(0,NULL,NULL);
  
    if(argc == 2) {  // show a single alias, if any
        for(a=AliasList;a;a=a->next)
            if(!stricmp(a->name,argv[1])) {
                tprintf("%s\n",a->cmd);
                break;
            }
        if(!a)
            j2tputs("not set!\n");
        return 0;
    }
  
    // now either delete or add an alias !
    if((len=strlen(argv[2])) == 0) {  // delete an alias
        struct alias *p = NULL;
  
        for(a=AliasList;a;p=a,a=a->next) {
            if(!stricmp(a->name,argv[1])) {
                if(p)
                    p->next = a->next;
                else
                    AliasList = a->next;
                free(a->name);
                free(a->cmd);
                free(a);
                break;
            }
        }
    } else {    // add a new alias or change an existing one
        if(len > MBXLINE) {
            j2tputs("Alias too long!\n");
            return 1;
        }
        for(a=AliasList;a;a=a->next)
            if(!stricmp(a->name,argv[1]))
                break;
        if(!a) {    // add a new one
            a = mallocw(sizeof(struct alias));
            a->next = AliasList;
            AliasList = a;
            a->name = j2strdup(argv[1]);
            strupr(a->name);
        } else
            free(a->cmd);
        a->cmd = j2strdup(argv[2]);
    }
    return 0;
}
  
  
int
dombalias(int argc,char *argv[],void *p) {
    struct alias *a;
  
    for(a=AliasList;a;a=a->next)
        if(tprintf("%s: %s\n",a->name,a->cmd) == EOF)
            return EOF;
    return 0;
}
  
struct alias *findalias(char *cmd) {
    struct alias *a;
  
    // check with the alias list
    for(a=AliasList;a;a=a->next)
        if(!stricmp(a->name,cmd))
            break;
    return a;
}
  
  
char Mbnrid[20];
  
/*set the mailbox netrom id*/
void
setmbnrid() {
    char tmp[AXBUF];
    char tmp2[AXBUF];
#ifndef AX25
    char *cp;
#endif
  
#ifdef NETROM
    if(Nr_iface != NULLIF) { /* Use netrom call, and alias (if exists) */
        if(*Myalias != '\0')
            sprintf(Mbnrid,"%s:%s ",pax25(tmp,Myalias),
            pax25(tmp2,Nr_iface->hwaddr));
        else
            sprintf(Mbnrid,"%s ",pax25(tmp,Nr_iface->hwaddr));
        return;
    }
    /* Use Mycall, and alias (if exists) */
    if(*Myalias != '\0')
        sprintf(Mbnrid,"%s:%s ",pax25(tmp,Myalias),pax25(tmp2,Mycall));
    else
#endif
#ifdef AX25
        sprintf(Mbnrid,"%s ",pax25(tmp,Bbscall));  /* was pax25(tmp,Mycall) */
#else
    strncpy(Mbnrid,Hostname,19);
    if((cp = strchr(Mbnrid,'.')) != NULLCHAR)
        *cp = '\0';
#endif
    return;
}
  
/*This is a dummy called from the main command interpreter,
 *setup a mbx structure so dombusers() works correct - WG7J
 */
int
dombstatus(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx m;
  
    m.privs = SYSOP_CMD;
    m.stype = ' ';
    return dombusers(0,NULL,&m);
}
  
int
dombox(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc == 1)
        return dombstatus(0,NULL,NULL);
    return subcmd(Mbtab,argc,argv,p);
}

#ifdef REGISTER
static int
dombregister(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&MbRegister,"registration",argc,argv);
}
#endif
  
#ifdef AX25

extern int MbAx25Ifilter;

static int
dombifilter(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&MbAx25Ifilter,"Mbox I-frame filter",argc,argv);
}

static int
dombhideport(int argc, char *argv[], void *p)
{
    return setflag(argc,argv[1],HIDE_PORT,argv[2]);
}
  
static int
dombnrid(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Usenrid,"Netrom id prompt",argc,argv);
}
  
#ifdef MAILCMDS
static int
dombmport(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setflag(argc,argv[1],MAIL_BEACON,argv[2]);
}
#endif
#endif /* AX25 */
  
#ifdef MAILCMDS
  
static int
dombnobid(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&NoBid,"Accept Buls without BID",argc,argv);
}
  
#ifdef HOLD_LOCAL_MSGS
int MbHoldLocal = 0;

int
dombholdlocal(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    (void)setbool(&MbHoldLocal,"Hold locally-originated messages",argc,argv);
    if(argc==1 && MbHoldLocal) {  /* holding enabled, and we queried hold flag state */
        int i,idx,held;
        FILE *fp;
    	char buf[LINELEN], path[FILE_PATH_SIZE+LINELEN];	/* 01Oct2019, Maiko, compiler format overflow warning, so added +LINELEN bytes */
        struct indexhdr hdr;
        struct mailindex ind;

        if((fp = fopen(Holdlist,READ_TEXT)) != NULLFILE) {
            memset(&ind,0,sizeof(ind));
            while(fgets(buf,sizeof(buf),fp) != NULLCHAR) {
                firsttoken(buf);  /* allow text after area name? */
                dirformat(buf);
                sprintf(path,"%s/%s.ind",Mailspool,buf);
                if((idx = open(path,READBINARY)) == -1) continue;
                held=0;
                if(read_header(idx,&hdr) == 0)    /* Read the header */
                    for(i=0; i<hdr.msgs; i++) {   /* then read each index ent */
                        pwait(NULL);
                        default_index(buf,&ind);
                        read_index(idx,&ind);
                        if(ind.status&BM_HOLD) held++;
                    }
                close(idx);
                dotformat(buf);
                if(held) tprintf("%s - %d held\n", buf, held);
            }
            default_index("", &ind);
            fclose(fp);
        }
    }
    return 0;
}
#endif /* HOLD_LOCAL_MSGS */

static int
dombsendquery(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Mbsendquery,"query after send",argc,argv);
}
  
#ifdef USERLOG
  
static int
dombnewmail(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Mbnewmail,"show new area mail",argc,argv);
}
#endif
#endif
  
#ifdef CONVERS
int Mbconverse = 1;
  
static int
domballowconvers(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Mbconverse,"Mbox convers",argc,argv);
}
#endif
  
#ifdef TTYLINKSERVER
/* if unattended mode is set, ax25, telnet and maybe other sessions will
 * be restricted.
 */
static int
doattend(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&MAttended,"Mailbox Attended flag",argc,argv);
}
#endif
  
/* Set mailbox redundancy timer */
static int dombtdisc (int argc, char **argv, void *p)
{
#ifdef	JUST_AN_IDEA_RIGHT_NOW
	/*
	 * 14Jun2016, Maiko (VE4KLM), allow the capability to set tdisc for a
	 * specific callsign, added this to allow sysop to set a much lower tdisc
	 * for specific systems (callsigns) encountering the EOL sequence issues.
	 */
	if ((argc > 1) & !is_digit(argv[1]))
	{
	}
	else
#endif

    return setint32 (&Mbtdiscinit, "Mbox redundancy timer (sec)", argc, argv);
}
  
static int
dombmaxusers(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint(&MbMaxUsers,"Mbox max users",argc,argv);
}

#ifdef MAILCMDS
#ifdef MBFWD
int Mbheader;
  
extern int ThirdParty;
  
static int
dombheader(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int retval;
  
    retval = setbool(&Mbheader,"R: header",argc,argv);
    if(!Mbheader)
        ThirdParty = 0;
    return retval;
}
  
static int
dombsmtptoo(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Mbsmtptoo,"Bbs forwards SMTP headers",argc,argv);
}
#endif
#endif
  
static int
dombpasswd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int len;
  
    /*Only allowed from keyboard*/
    if(Curproc->input != Command->input) {
        j2tputs(Noperm);
        return 0;
    }
    if(argc != 2) {
        j2tputs("Usage: mbox password \"<sysop password>\"\n");
        return 0;
    }
    if((len=strlen(argv[1])) == 0)
        return 0;       /* zero length, don't reset */
  
    if(len > MAXPWDLEN) {
        j2tputs("Too long\n");
        return 0;
    }
    strcpy(Mbpasswd,argv[1]);
    return 0;
}
  
static int
dombtmsg(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc > 2) {
        j2tputs("Usage: mbox tmsgs \"<your message>\"\n");
        return 0;
    }
  
    if(argc < 2) {
        if(Mtmsg != NULLCHAR)
            j2tputs(Mtmsg);
    }
    else {
        if(Mtmsg != NULLCHAR){
            free(Mtmsg);
            Mtmsg = NULLCHAR;   /* reset the pointer */
        }
        if(!strlen(argv[1]))
            return 0;               /* clearing the buffer */
        Mtmsg = mallocw(strlen(argv[1])+5);/* allow for the EOL char */
        strcpy(Mtmsg, argv[1]);
        strcat(Mtmsg, "\n");        /* add the EOL char */
    }
    return 0;
}
  
#ifdef MAILCMDS
#ifdef MBFWD
/*Set the ZIP to be used in the R: line when forwarding */
static int
dombzipcode(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int len;
  
    if(argc > 2) {
        j2tputs("Usage: mbox zipcode <your zip>\n");
        return 0;
    }
  
    if(argc < 2) {
        if(Mbzip != NULLCHAR)
            tprintf("%s\n",Mbzip);
    }
    else {
        len = strlen(argv[1]);
        if(Mbzip != NULLCHAR){
            free(Mbzip);
            Mbzip = NULLCHAR;   /* reset the pointer */
        }
        if(len == 0)
            return 0;               /* clearing the buffer */
        Mbzip = mallocw((unsigned)len+1); /* room for \0 */
        strcpy(Mbzip, argv[1]);
    }
    return 0;
}
  
/* Set the QTH to be used in R: line when forwarding*/
static int
dombqth(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc > 2) {
        j2tputs("Usage: mbox qth <your qth> || \"<your qth, state>\"\n");
        return 0;
    }
  
    if(argc < 2) {
        if(Mbqth != NULLCHAR)
            tprintf("%s\n",Mbqth);
    }
    else {
        if(Mbqth != NULLCHAR){
            free(Mbqth);
            Mbqth = NULLCHAR;   /* reset the pointer */
        }
        if(!strlen(argv[1]))
            return 0;               /* clearing the buffer */
        Mbqth = mallocw(strlen(argv[1]) + 1);
        strcpy(Mbqth, argv[1]);
    }
    return 0;
}
  
/*Set the hierachical address to be used in R: line when forwarding*/
static int
dombhaddress(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc > 2) {
        j2tputs("Usage: mbox haddress <your H-address (WITH call)>\n");
        return 0;
    }
  
    if(argc < 2) {
        if(Mbhaddress != NULLCHAR)
            tprintf("%s\n",Mbhaddress);
    }
    else {
        if(Mbhaddress != NULLCHAR){
            free(Mbhaddress);
            Mbhaddress = NULLCHAR;   /* reset the pointer */
        }
        if(!strlen(argv[1]))
            return 0;               /* clearing the buffer */
        Mbhaddress = mallocw(strlen(argv[1]) + 1);
        strcpy(Mbhaddress, argv[1]);
    /*make sure the're upper case*/
        strupr(Mbhaddress);
    }
    return 0;
}
  
/*Set the R: line [info] to be used when forwarding*/
static int
dombfwdinfo(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc > 2) {
        j2tputs("Usage: mbox fwdinfo \"<your R:-line [info]>\"\n");
        return 0;
    }
  
    if(argc < 2) {
        if(Mbfwdinfo != NULLCHAR)
            tprintf("%s\n",Mbfwdinfo);
    }
    else {
        if(Mbfwdinfo != NULLCHAR){
            free(Mbfwdinfo);
            Mbfwdinfo = NULLCHAR;   /* reset the pointer */
        }
        if(!strlen(argv[1]))
            return 0;               /* clearing the buffer */
        Mbfwdinfo = mallocw(strlen(argv[1]) + 1);
        strcpy(Mbfwdinfo, argv[1]);
    }
    return 0;
}
#endif /*MBFWD*/
#endif

/* 29Dec2013, Maiko, Option to force 'm->to' when Hdr[TO] written to tfile */
static int dombrewriteToHdr (int argc, char **argv, void *p)
{
    return setbool (&MbrewriteToHdr, "Allow rewrite of smtp Hdrs[TO] in tfile", argc, argv);
}

/* Keep track of all past users */
struct pu {
    struct pu *next;    /* next one in list */
    int32 time;                 /* When was the last login ? */
    int number;                 /* Number of times logged in */
    char name[MBXNAME+1];       /* user name */
};
#define NULLPU (struct pu *)NULL
struct pu *Pu = NULLPU;
  
/* Look up an entry in the users-list*/
static
struct pu *
pu_lookup(name)
char *name;
{
    struct pu *ppu;
    struct pu *pulast = NULLPU;
  
    for(ppu = Pu;ppu != NULLPU;pulast = ppu,ppu = ppu->next){
        if(!strcmp(name,ppu->name)){ /* found it! */
            if(pulast != NULLPU){
                /* Move entry to top of list */
                pulast->next = ppu->next;
                ppu->next = Pu;
                Pu = ppu;
            }
            return ppu;
        }
    }
    return NULLPU;
}
  
/*Log all users of the mailbox*/
/* This gets kept track of in the file name UDefaults */
/* format is
user datestamp options
where options are separated by spaces
M# - use more with # lines
A - use area indication
X - use expert status
N - use netrom lookalike prompt
R - send 'reply-to' header with mail
P - linemode / charmode (XP command)
*/
  
#ifdef USERLOG
  
/* Write the new defaults - WG7J */
void
updatedefaults(m)
struct mbx *m;
{
    FILE *Ufile, *tfile;
    char buf[256];
    char *cp;
    time_t t;
  
    /* Save old defaults file to backup */
    unlink(UDefbak);
    if(rename(UDefaults,UDefbak))
        return;
  
    /*Write all users back, but update this one!*/
    if((Ufile = fopen(UDefaults,"w")) == NULLFILE) {
        /* Can't create defaults file ??? */
        rename(UDefbak,UDefaults);  /* undo */
        return;
    }
  
    if((tfile = fopen(UDefbak,"r")) == NULLFILE) {
        /* What on earth happened ???? */
        fclose(Ufile);
        unlink(UDefaults);
        rename(UDefbak,UDefaults);  /* undo */
        return;
    }
    while(fgets(buf,sizeof(buf),tfile) != NULLCHAR) {
        if((cp=strchr(buf,' ')) != NULLCHAR)
            *cp = '\0';
        if(!stricmp(m->name,buf)) {
            /*found this user*/
            time(&t);
            /* KF5MG
               the -n , -h and -e flags are for Name and Homebbs and email.
               Existing USER.DAT files should work. No -n, -h or -e will be
               found, so defaults will be used. The REGISTER command updates
               the username and homebbs fields.
            */
            sprintf(buf,"%s %lu M%d %c %c %c %c %c -n%s -h%s -e%s C%c\n",
            m->name,t,m->morerows,
            (m->sid&MBX_REPLYADDR) ? 'R' : ' ',
            (m->sid&MBX_AREA) ? 'A' : ' ' ,
            (m->sid&MBX_EXPERT) ? 'X' : ' ' ,
            (m->sid&MBX_NRID) ? 'N' : ' ',
            (m->sid&MBX_LINEMODE) ? 'P' : ' ',
            m->username?m->username : "",
            m->homebbs ? m->homebbs : "",
            m->IPemail ? m->IPemail : "",
            (m->family == AF_AX25) ? 'A' : \
            ((m->family == AF_NETROM) ? 'N' : 'T'));
        } else
            *cp = ' '; /* restore the space !*/
        fputs(buf,Ufile);
    }
    fclose(tfile);
    fclose(Ufile);
    return;
}
  
#ifdef MAILCMDS
/* scan the areas file, looking for those with .inf datestamps > m->last.
 * these are areas that have new mail received since the user
 * last logged in. - WG7J/N5KNX
 */
void listnewmail(struct mbx *m,int silent) {
    FILE *fp;
    char buf[LINELEN], path[FILE_PATH_SIZE+LINELEN];	/* 01Oct2019, Maiko, compiler format overflow warning, so added +LINELEN bytes */
    struct stat statbuf;
    int rval;
    int firstone=1, column=0;

    if((fp = fopen(Arealist,READ_TEXT)) == NULLFILE)
        return;
    while(fgets(buf,sizeof(buf),fp) != NULLCHAR) {
        pwait(NULL);
        /* The first word on each line is all that matters */
        firsttoken(buf);
        dirformat(buf);
	sprintf(path,"%s/%s.inf", Mailspool, buf);
	dotformat(buf);
	if ((rval = open(path, READBINARY)) == -1)
	     continue;
	if (fstat(rval, &statbuf) != -1) {
	    /* Is there new mail here ? */
            if(m->last < statbuf.st_mtime) {
        	if(firstone) {
                     tprintf("New mail in: %s  ",buf);
                     firstone = 0;
                     column = 2;
                } else {
                    tprintf("%s  ",buf);
                    if(column++ == 6)
                        tputc('\n'), column=0;
                }
            }
	}
	close(rval);
    }
    fclose(fp);
    if(!firstone)
        tputc('\n');
    else if(!silent)
        j2tputs("No new bulletins since last login\n");
    return;
}
#endif /* MAILCMDS */
  
#endif /* USERLOG */
  
int DiffUsers;
  
void
loguser(m)
struct mbx *m;
{
    struct pu *pu;
#ifdef USERLOG
    FILE *Ufile;
    char buf[256];
    char *cp,*cp2;
    int found=0;
    int xpert = 0;
#endif
  
    if((pu = pu_lookup(m->name)) == NULLPU) {   /* not 'known' user */
        pu = (struct pu *)callocw(1,sizeof(struct pu));
        strcpy(pu->name,m->name);
        pu->next = Pu;
        Pu = pu;
        DiffUsers++;    /* A new guy */
    }
    pu->time = secclock();
    pu->number++;
  
#ifdef USERLOG
    /* In case these are already set, clear them */
    free(m->homebbs);
    free(m->IPemail);
    free(m->username);
    m->username = m->IPemail = m->homebbs = NULLCHAR;
  
    /* Now get options from the userdefaults file, and add timestamp */
    if(!(m->sid&MBX_SID)) { /* only if not a bbs */
        sprintf(buf,"%s",UDefaults);
        if ((Ufile = fopen(buf,"r+")) == NULLFILE) {
            /* default file doesn't exist, create it */
            if((Ufile = fopen(buf,"w")) == NULLFILE)
                return;
            /* Add this user as first one */
            sprintf(buf,"%s 0 M%d A %c %c %c C%c\n",
            m->name,m->morerows,
            (m->sid&MBX_EXPERT) ? 'X' : ' ' ,
            (m->sid&MBX_NRID) ? 'N' : ' ',
            (m->sid&MBX_LINEMODE) ? 'P' : ' ',
            (m->family == AF_AX25) ? 'A' : \
            ((m->family == AF_NETROM) ? 'N' : 'T'));
            fputs(buf,Ufile);
            fclose(Ufile);

#ifdef MBX_AREA_PROMPT
// Allow 'area indication' by default
// in the BBS prompt  K6FSH  2010-02-24

            m->sid |= MBX_AREA;
#endif
            m->last = 0L;
            if(Usenrid)
                m->sid |= MBX_NRID;
            return;
        }
        /* Find user in the default file */
        while(!found) {
            if(fgets(buf,sizeof(buf),Ufile) == NULLCHAR)
                break;
            /* single out the name */
            if((cp=strchr(buf,' ')) != NULLCHAR)
                *cp++ = '\0';
            /* compare the name */
            if(!stricmp(m->name,buf)) {
                /* found user, now scan the options used */
                found = 1;
                fclose(Ufile);
                /* first read last login time */
                m->last = atol(cp);
                while(*cp != '\0') {
#if 0   /* save a few bytes...this code is not necessary */
                    while(*cp == ' ')   /*skip blanks*/
                        cp++;
#endif
                    switch(*cp){
                        case 'C':
                        /* All options end BEFORE the CT/CN or CA */
                            *(cp+1) = '\0';
                            break;
                        case 'R':
                            m->sid |= MBX_REPLYADDR;
                            break;
                        case 'M':
                            cp++;
                            m->morerows = atoi(cp);
                            break;
                        case 'A':
                            m->sid |= MBX_AREA;
                            break;
                        case 'X':
                            m->sid |= MBX_EXPERT;
                            xpert = 1;
                            break;
                        case 'N':
                            m->sid |= MBX_NRID;
                            break;
                        case 'P':
                            m->sid |= MBX_LINEMODE;
                            break;
                        case '-':
                            cp++;
                            cp2 = cp++;     /* cp2 point to option,
                                         * cp to beginning of option-data */
                            while(*cp != ' ')   /* find end of option-data */
                                cp++;
                            *cp = '\0';
                            switch(*cp2++){
                                case 'h':
                                    if(*cp2)
                                        m->homebbs  = j2strdup(cp2);
                                    break;
                                case 'e':
                                    if(*cp2)
                                        m->IPemail  = j2strdup(cp2);
                                    break;
                                case 'n':
                                    if(*cp2)
                                        m->username = j2strdup(cp2);
                                    break;
                            }
                            break;
                    }
                    cp++;
                }
            }
        } /* while(!found)*/
        if(found) {
            /* add the new timestamp to the defaults file */
            if(!xpert)
                m->sid &= ~MBX_EXPERT;
        } else {
            /* a new one, add to the end (where we now should be!)*/
            m->last = 0L;
            sprintf(buf,"%s 0 M%d A %c %c %c C%c\n",
            m->name,m->morerows,
            (m->sid & MBX_EXPERT) ? 'X' : ' ' ,
            (Usenrid) ? 'N' : ' ' ,
            (charmode_ok(m)) ? ' ' : 'P',
            (m->family == AF_AX25) ? 'A' : \
            ((m->family == AF_NETROM) ? 'N' : 'T'));
            fputs(buf,Ufile);
            fclose(Ufile);

#ifdef MBX_AREA_PROMPT
// Allow 'area indication' by default
// in the BBS prompt  K6FSH  2010-02-24
            m->sid |= MBX_AREA;
#endif
            if(Usenrid)
                m->sid |= MBX_NRID;
        }
    }/* if not bbs */
#endif /* USERLOG */
  
}
  
/*List all past users of the mailbox */
int
dombpast(argc,argv,p)
int argc;
char *argv[];
void *p;
{
  
    struct pu *pu;
    int col = 0;
    int max=10000;      /* Large enough :-) */
    int count=0;
  
    if(argc>1) {
        if(!strcmp(argv[1],"flush")) {  /* release all Past-user memory */
            struct pu *nxpu;
            for (pu=Pu;pu!=NULLPU;pu=nxpu) {
                nxpu=pu->next;
                pu->next=NULLPU;  /* in case is used by another process */
                free(pu);
            }
            Pu=NULLPU;
            /* DiffUsers=0; (may as well retain this count) */
            return 0;
        }
        max = atoi(argv[1])-1;
    }
  
    j2tputs("Past users:\n"
    "User       Logins  Time since last   "
    "User       Logins  Time since last\n");
    for (pu=Pu;pu!=NULLPU;pu=pu->next) {
        if(col)
            j2tputs(" : ");
        tprintf("%-10s   %4d     %12s",pu->name,pu->number,\
        tformat(secclock() - pu->time));
        count++;
        if(count>max)
            break;
        if(col) {
            col = 0;
            tputc('\n');
        } else
            col = 1;
    }
    if(col)
        tputc('\n');
    tputc('\n');
    return 0;
}
  
int
dombmailstats(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    tprintf(/* "Core: %lu\n" */
    "Up: %s\n"
    "Logins: %d\n"
    "Users: %d\n"
    "Count: %d\n",
    /* farcoreleft(), */ tformat(secclock()),Totallogins,BbsUsers,DiffUsers);
  
#ifdef MAILCMDS
#ifdef MBFWD
    tprintf("Sent: %d\n"
    "Read: %d\n"
    "Rcvd: %d\n"
    "Fwd: %d\n\n",\
    MbSent,MbRead,MbRecvd,MbForwarded);
#else
    tprintf("Sent: %d\n"
    "Read: %d\n"
    "Rcvd: %d\n\n",MbSent,MbRead,MbRecvd);
#endif
#endif
    return 0;
}
  
#ifdef USERLOG

/* scanning past cp, put non-blank string addr into *sp */
static char *
getstrg(char **sp, char *cp)
{
    char *cp2;

    cp2 = ++cp;
    cp=skipnonwhite(cp);
    *cp = '\0';
    if (*cp2)
        *sp = cp2;
/*  else
        *sp = NULLCHAR; */
    return cp;
}

/* Search for info on a certain user in the users.dat file */
/* Note: argc is used as a flag: 0 => lookup argv[1] (for fingerd)
   1 => lookup everyone (for fingerd). argv is null!
   2 => lookup argv[1] (for dombusers() (ie, ML mbox cmd))
 */
int
dombuserinfo(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    FILE *Ufile;
    char buf[MBXLINE];
    int found = 0;
    time_t t;
    struct tm *lt;
    char *cp;
    char *cp3;
    char *usercall;
    char *username;
    char *homebbs;
    char *IPemail;
  
    cp3 = "TELNET";
  
    if((Ufile = fopen(UDefaults,"r")) == NULLFILE) {
        j2tputs("Can't find user data\n");
        return 0;
    }
  
    while(!found || argc==1) {
        username = homebbs = IPemail = NULLCHAR;
        if(fgets(buf,sizeof(buf),Ufile) == NULLCHAR)
            break;
         /* single out the name */
        if((cp=strchr(buf,' ')) == NULLCHAR) continue;
        *cp++ = '\0';
         /* compare the name */
        if (argc==1)
            usercall=buf; /* always will match */
        else
            usercall=argv[1];
        if(!stricmp(usercall,buf)) {
            /* found user, now scan the options used */
            found = 1;
            /* read last login time */
            t = (time_t)atol(cp);	/* 11Oct2009, Maiko, not bad idea */
            lt = localtime(&t);
            while(*cp != '\0') {
                while(*cp == ' ')   /*skip blanks*/
                    cp++;
                switch(*cp){
                    case 'C':
                        cp++;
                    switch(*cp){
                        /* All options end BEFORE the CT/CN or CA */
                        case 'A':
                            cp3 = "AX.25";
                            break;
                        case 'N':
                            cp3 = "NETROM";
                            break;
                    }
                        *(cp+1) = '\0';  /* terminate scan here */
                        break;
                    case '-':
                        cp++;
                    switch(*cp){
                        case 'h':
                            cp = getstrg(&homebbs, cp);
                            break;
                        case 'e':
                            cp = getstrg(&IPemail, cp);
                            break;
                        case 'n':
                            cp = getstrg(&username, cp);
                            break;
                    }
                        break;
                }
                cp++;
            }
  
            if(username) tprintf("%s, ", username);
            tprintf("%s%s last connected via %s on %s",usercall,
                (username?",":""),cp3,asctime(lt));
            if(homebbs)
                tprintf("Home BBS: %s     ", homebbs);
            if(IPemail)
                tprintf("Internet: %s\n", IPemail);
            else if (homebbs) tputc('\n');

            tputc('\n');
        }
    } /* while(!found)*/
    if(!found && argc!=1)
        tprintf("%s never connected\n",argv[1]);
  
    fclose(Ufile);
    return 0;
}
  
#endif

/*
 * Removed this section of code from the 'dombusers()' function, and put
 * it into it's own 'singlembuser()' function, so that I can get details
 * of a particular user connection, for tracing of the source of an email
 * for example (ie, putting something like X-user-port into mail header).
 * - 23Jul2005, Maiko.
 * Added small output function to save code - 26Jul2005
 */

static void smuputs (FILE* tfile, char *buf)
{
	if (tfile == (FILE*)0)
		j2tputs (buf);
	else
		fputs (buf, tfile);
}

void singlembuser (struct mbx *m, struct mbx *caller, FILE *tfile)
{
    char fsocket[MAXSOCKSIZE];
	char upl[40], down[40];
	char *cp, *cp1;

#ifdef NETROM
    struct nrroute_tab *np;
    char temp[AXBUF];
	char *cp2, *cp3;
#endif

    struct usock *up, *up1;

	int s, len = MAXSOCKSIZE;

	if (j2getpeername(m->user,fsocket,&len) == 0 && len)
		cp = j2strdup(psocket(fsocket));
	else cp = j2strdup("");
  
	upl[0] = '\0';
	down[0] = '\0';
  
	switch (m->family) {     /* UPLINK */

#ifdef	HTTPVNC
		case AF_VNC:
			sprintf (upl, VNClink, m->name);  /* New, 03Jul09, VE4KLM */
			break;
#endif

#ifdef HFDD
		case AF_HFDD:
			sprintf (upl, HFlink, m->name);	/* New, 12Apr07, VE4KLM */
			break;
#endif
#ifdef AX25
		case AF_AX25:
			sprintf(upl,Uplink,cp);
			break;
#endif
#ifdef NETROM
		case AF_NETROM:
			if((cp1 = strchr(cp,' ')) != NULLCHAR)
			{
				*cp1 = '\0';
				cp1 += 3;
				setcall(temp,cp1);
				if ((np = find_nrroute(temp)) != NULLNRRTAB)
					cp2 = j2strdup(np->alias);
				else cp2 = j2strdup("?");
				if((cp3 = strchr(cp2,' ')) != NULLCHAR)
					*cp3 = '\0';
			}
			else cp1="";
    /*
        if(*cp2 == '#' || *cp2 == '\0')
        sprintf(upl,incircuit,"","",cp1,cp);
        else
     */
     /* show correct user name when outgoing forward over netrom
      * problem caused by use of the '.C xxx' lines.
      */
			if(m->state == MBX_TRYING || m->state == MBX_FORWARD)
				sprintf(upl,incircuit,cp2,":",cp1,m->name);
			else
				sprintf(upl,incircuit,cp2,":",cp1,cp);
			free(cp2);
			break;
#endif
		case AF_INET:
			if((cp1 = strchr(cp,':')) != NULLCHAR)
				*cp1 = '\0';
			sprintf(upl,Telnet,m->name,cp);
			break;
		case AF_LOCAL:
			sprintf(upl,Local,(m->type==TIP_LINK) ? m->name : Hostname);
			break;
		default:
			strcpy(upl,"Connect");
			break;
	}
	free(cp);

	/* moved up here, into a strcat instead of j2tputs */
	if(m->state != MBX_GATEWAY)
		strcat (upl, "  -> ");

	/* 26Jul2005, Maiko, Modified to also print to file */
	if (tfile == (FILE*)0)
		tprintf("%-36s",upl);
	else
		fprintf (tfile, "%-36s", upl);
 
	switch(m->state)
	{
		case MBX_GATEWAY:
			up1 = itop(m->user);
			s = 0;
			while((s=getnextsocket(s)) != -1)
			{
				if(s == m->user || (up = itop(s)) == NULLUSOCK )
					continue;

				if(up->owner == up1->owner)
				{
					len = MAXSOCKSIZE;
					if (j2getpeername(s,fsocket,&len) == 0 && len)
						cp = j2strdup(psocket(fsocket));
					else cp = j2strdup("");

					switch(up->type)
					{
						case TYPE_TCP:
							sprintf(down,Telnetdown,cp);
							break;
#ifdef AX25
						case TYPE_AX25I:
							sprintf(down,Downlink,cp);
							break;
#endif
#ifdef NETROM
						case TYPE_NETROML4:
							/*get rid of usercall*/
							if((cp1 = strchr(cp,' ')) != NULLCHAR)
							{
								*cp1 = '\0';
								cp1 += 3;   /*get rid of ' @ '*/
								setcall(temp,cp1);      /*get node call*/
								/*find alias, if any*/
								if((np = find_nrroute(temp)) !=  NULLNRRTAB)
									cp2 = j2strdup(np->alias);
								else cp2 = j2strdup("?");
								if((cp3 = strchr(cp2,' ')) != NULLCHAR)
									*cp3 = '\0';
							}
							else cp1="";
							sprintf(down,outcircuit,cp2,":",cp1);
							free(cp2);
							break;
#endif
						case TYPE_LOCAL_STREAM:
						case TYPE_LOCAL_DGRAM:
							sprintf(down,Local,Hostname);
							break;

						default:
							strcpy(down,"Connect");
							break;
					}
					free(cp);

				/* 26Jul2005, Maiko, Modified to also print to file */
					if (tfile == (FILE*)0)
						tprintf("<--> %s\n",down);
					else
						fprintf (tfile, "<--> %s\n",down);
					break;
				}
			}
			break;

		case MBX_LOGIN:
			smuputs(tfile, "Logging in\n");
			break;

		case MBX_CMD:
			smuputs(tfile, "Idle\n");
			break;

#ifdef MAILCMDS
		case MBX_SUBJ:
		case MBX_DATA:
			smuputs(tfile, "Sending message\n");
			break;
#ifdef MBFWD
		case MBX_REVFWD:
			smuputs(tfile, "Reverse Forwarding\n");
			break;
		case MBX_TRYING:
			smuputs(tfile, "Attempting Forward\n");
			break;
		case MBX_FORWARD:
			smuputs(tfile, "Forwarding\n");
			break;
#endif
		case MBX_READ:
			smuputs(tfile, "Reading message\n");
			break;
#endif
		case MBX_UPLOAD:
			smuputs(tfile, "Uploading file\n");
			break;
		case MBX_DOWNLOAD:
			smuputs(tfile, "Downloading file\n");
			break;
#ifdef CONVERS
		case MBX_CONVERS:
			smuputs(tfile, "Convers mode\n");
			break;
#endif
		case MBX_CHAT:
			smuputs(tfile, "Chatting with sysop\n");
			break;
		case MBX_WHAT:
			smuputs(tfile, "Listing files\n");
			break;
#ifdef XMODEM
		case MBX_XMODEM_RX:
			smuputs(tfile, "Xmodem Receiving\n");
			break;
		case MBX_XMODEM_TX:
			smuputs(tfile, "Xmodem Sending\n");
			break;
#endif
	}

    /* Only show callers with sysop-privs who is sysop-mode!
     * This prevents users from easily learning who's
     * got SYSOP privs
     */
	if(m->state == MBX_SYSOPTRY)
	{
		if(caller->privs & SYSOP_CMD)
			smuputs(tfile, "Attempting Sysop mode\n");
		else
			smuputs(tfile, "Idle\n");
	}
	else
	{
		if(m->state == MBX_SYSOP)
		{
			if(caller->privs & SYSOP_CMD)
				smuputs(tfile, "Sysop mode\n");
			else
				smuputs(tfile, "Idle\n");
		}
	}
}
  
int dombusers (int argc, char *argv[], void *p)
{
    struct mbx *m,*caller;
  
    caller = (struct mbx *) p;
  
    if(caller->stype == 'S')
        return dombmailstats(argc,argv,p);
  
#ifdef MAILCMDS
    if(((caller->stype == 'M') || (caller->stype == 'C')) &&
        (caller->privs & SYSOP_CMD))
        return dombmovemail(argc,argv,p);
#endif
  
    if (caller->stype == 'L')
	{
#ifdef USERLOG
        if((argc > 1) && (strspn(argv[1],"+0123456789") != strlen(argv[1])))
            return dombuserinfo(argc,argv,p);  /* arg[1] is a callsign */
        else
#endif
            return dombpast(argc,argv,p);
	}
  
    j2tputs("Users:\n");
	/*
	 * 23Jul2005, Maiko, All the code moved to new 'singlembuser()'
	 * function, since I need to be able to individually pick out
	 * the status for one particular user now.
	 */  
    for(m=Mbox;m;m=m->next)
		singlembuser (m, caller, (FILE*)0);
    tputc('\n');
    return 0;
}
  
#ifdef MBFWD
#ifdef FBBFWD
static int dombfbb (int argc, char **argv, void *p)
{
#ifdef FBBCMP
/* 10Jun2016, Maiko (VE4KLM), No more mbox fbb 3, just not a good idea */
#ifdef	NO_MORE_MBOX_FBB_3_JUST_A_STUPID_IDEA
#ifdef B2F
	int maxval = 3;
#else
	int maxval = 2;
#endif
#endif	/* end of NO_MORE_MBOX_FBB_3_JUST_A_STUPID_IDEA */
	int maxval = 2;
#else
	int maxval = 1;
#endif

    return setintrc(&Mfbb,"Mailbox FBB Style Forwarding",argc,argv,0,maxval);
}
#endif // FBBFWD

/* 29Jun2016, Maiko (VE4KLM), warnings can be a nuisance :) */
static int doeolwarnings (int argc, char **argv, void *p)
{
    return setbool (&Meolwarnings, "EOL Warnings", argc, argv);
}

static int dombtrace (int argc, char **argv, void *p)
{
    return setbool(&Mtrace,"Mailbox trace flag",argc,argv);
}
#endif	// MBFWD
  
static int
dombsecure(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&MBSecure,"Mailbox gateway secure flag",argc,argv);
}

#ifdef AX25
static int
dombnoax25(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setflag(argc,argv[1],NO_AX25,argv[2]);
}
#endif
  
static int
dombbonly(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setflag(argc,argv[1],BBS_ONLY,argv[2]);
}
  
static int
dombuonly(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setflag(argc,argv[1],USERS_ONLY,argv[2]);
}
  
static int
dombsonly(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setflag(argc,argv[1],SYSOP_ONLY,argv[2]);
}
  
#if defined USERLOG && defined REGISTER
static int
mbxpromptingread(char *prompt, struct mbx *m, char **result)
{
    int nread;

    if (prompt) {
       usputs(m->user,prompt);
       usflush(m->user);
    }
    if((nread=mbxrecvline(m)) > 0) {
        rip(m->line);
        free(*result);
        *result = j2strdup(m->line);
    }
    return nread;
}

int doregister(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    FILE *fp;
    struct mbx *m = (struct mbx *) p;
  
    /* Is there a registration file? */
    if((fp = fopen(Mregfile,READ_TEXT)) != NULLFILE) {
        sendfile(fp,m->user,ASCII_TYPE,0,m);
        fclose(fp);
    }
  
    tprintf("Your current settings are:\nName = %s\n", (m->username == NULL) ? "Unknown": m->username);
    tprintf("AX.25 Homebbs Address  = %s\n", (m->homebbs == NULL) ? "Unknown" : m->homebbs);
    tprintf("Internet Email Address = %s\n\n", (m->IPemail == NULL) ? "Unknown" : m->IPemail);
  
    if (mbxpromptingread("First name.(CR=cancel)\n", m, &m->username) <= 0)
        return 0;

    if (mbxpromptingread("AX.25 homebbs.(CR=cancel all)\n", m, &m->homebbs) <= 0)
        return 0;
    strupr(m->homebbs);

    mbxpromptingread("Internet Email.(CR=ignore)\n", m, &m->IPemail);

    log(m->user,"User %s - %s (AX.25 - %s) (Internet - %s) has registered.",
        m->name, m->username, m->homebbs, m->IPemail);

    updatedefaults(m);   /* go ahead and save the info (if he just disconnects it would be lost) */

    return 0;
}
#endif /* USERLOG && REGISTER */
  
char *cmd_line(int argc,char *argv[],char stype) {
    static char line[MBXLINE+1];
    int i;
    char *cp;
  
  
    cp = line;
    sprintf(cp,"%s ",argv[0]);
    cp+=strlen(cp);
    if(stype != ' ') {
        --cp;
        sprintf(cp,"%c ",stype);
        cp += 2;
    }
    for(i=1;i<argc;i++) {
        sprintf(cp,"%s ",argv[i]);
        cp += strlen(cp);
    }
    return line;
}
  
int MbShowAliases;
  
int dombshowalias(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&MbShowAliases,"Show aliases in prompts",argc,argv);
}
  
static int
dombreset(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int j;
    struct mbx *m;

    for(m=Mbox;m;m=m->next) {
        if (argc == 1) tprintf ("%s\n", m->name);
	else for (j=1;j<argc;j++){
            if (!strcmp(argv[j],m->name)) {
		tprintf ("mbox user %s reset\n",m->name);
                if (j2shutdown(m->user, 2) == -1  /* kill all user's i/o */
                    && errno == ENOTCONN) close_s(m->user); /* desperate! */
                m->user = -1;
	    }
	}
    }
    return 0;
}
  
#endif /* MAILBOX */
