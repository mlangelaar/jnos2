/* Internet FTP client (interactive user)
 * Copyright 1991 Phil Karn, KA9Q
 */
/* Mods by G1EMM and PA0GRI */
/* modifications for encrypted password by ik1che 900419 */
/* added "resume" and "rput" commands for interrupted file transfers
 * by iw0cnb 15 Feb 92 */
  
/* VIEW command added by Simon G1FHY. Mod by Paul@wolf.demon.co.uk */
/* Added restart/reget per RFC959, iff FTP_REGET is #defined, and made
 * the Jnos-specific alternative resume/rput commands contingent on
 * FTP_RESUME being #defined.  Also added rename cmd iff FTP_RENAME
 * is #defined.  -- James N5KNX 8/97 ver 1.11 */

#ifdef MSDOS
#include <dir.h>
#endif
#ifdef UNIX
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "global.h"
#include "mbuf.h"
#include "session.h"
#include "cmdparse.h"
#include "proc.h"
#include "tty.h"
#include "socket.h"
#include "mailbox.h"
#include "ftp.h"
#include "ftpcli.h"
#include "commands.h"
#include "netuser.h"
#include "dirutil.h"
#include "files.h"
#include "config.h"
  
#define DIRBUF  256
  
#ifdef FTPSESSION
  
#ifdef  LZW
#include "lzw.h"
  
int Ftpslzw = TRUE;
int Ftpclzw = TRUE;
int doftpslzw __ARGS((int argc,char *argv[],void *p));
int doftpclzw __ARGS((int argc,char *argv[],void *p));
static int dolocftpclzw __ARGS((int argc,char *argv[],void *p));
#endif
  
extern char System[];
static int doascii __ARGS((int argc,char *argv[],void *p));
static int dobatch __ARGS((int argc,char *argv[],void *p));
static int dobinary __ARGS((int argc,char *argv[],void *p));
static int doftpcd __ARGS((int argc,char *argv[],void *p));
static int doftphelp __ARGS((int argc,char *argv[],void *p));
static int doget __ARGS((int argc,char *argv[],void *p));
static int dohash __ARGS((int argc,char *argv[],void *p));
static int doverbose __ARGS((int argc,char *argv[],void *p));
static int dolist __ARGS((int argc,char *argv[],void *p));
static int dols __ARGS((int argc,char *argv[],void *p));
static int doldir  __ARGS((int argc,char *argv[],void *p));
static int dolcd  __ARGS((int argc,char *argv[],void *p));
static int dolmkdir __ARGS((int argc,char *argv[],void *p));
static int domkdir __ARGS((int argc,char *argv[],void *p));
static int domget __ARGS((int argc,char *argv[],void *p));
static int domput __ARGS((int argc,char *argv[],void *p));
static int doput __ARGS((int argc,char *argv[],void *p));
static int doquit __ARGS((int argc,char *argv[],void *p));
static int dormdir __ARGS((int argc,char *argv[],void *p));
#ifdef FTP_REGET
static int dorestart  __ARGS((int argc,char *argv[],void *p));
#endif
#ifdef FTP_RESUME
static int doresume __ARGS((int argc,char *argv[],void *p));
static int dorput __ARGS((int argc,char *argv[],void *p));
#endif
#ifdef FTP_RENAME
static int dorenam __ARGS((int argc,char *argv[],void *p));
#endif
static int dotype __ARGS((int argc,char *argv[],void *p));
static int doftpview __ARGS((int argc,char *argv[],void *p));
static int loc_getline __ARGS((struct session *sp,char *prompt,char *buf,int n));
static int getresp __ARGS((struct ftpcli *ftp,int mincode));
static long getsub __ARGS((struct ftpcli *ftp,char *command,char *remotename,char *localname));
static long putsub __ARGS((struct ftpcli *ftp,char *remotename,char *localname,int putr));
static void sendport __ARGS((int s,struct sockaddr_in *socket));
static char *ftpcli_login __ARGS((struct ftpcli *ftp,char *host));
  
static int Ftp_type = ASCII_TYPE;
static int Ftp_logbsize = 8;
  
static struct cmds DFAR Ftpcmds[] = {
    { "",             donothing,      0, 0, NULLCHAR },
    { "?",            doftphelp,      0, 0, NULLCHAR },
    { "ascii",        doascii,        0, 0, NULLCHAR },
    { "batch",        dobatch,        0, 0, NULLCHAR },
    { "binary",       dobinary,       0, 0, NULLCHAR },
    { "cd",           doftpcd,        0, 2, "cd <directory>" },
    { "dir",          dolist,         0, 0, NULLCHAR },
    { "list",         dolist,         0, 0, NULLCHAR },
    { "get",          doget,          0, 2, "get <remotefile> <localfile>" },
    { "hash",         dohash,         0, 0, NULLCHAR },
    { "help",         doftphelp,      0, 0, NULLCHAR },
    { "ls",           dols,           0, 0, NULLCHAR },
    { "lcd",          dolcd,          0, 1, NULLCHAR },
    { "ldir",         doldir,         0, 1, NULLCHAR },
    { "lmkdir",       dolmkdir,       0, 2, "lmkdir <local Directory>" },
#ifdef LZW
    { "reclzw",       dolocftpclzw,   0, 0, NULLCHAR },
    { "sendlzw",      doftpslzw,      0, 0, NULLCHAR },
#endif
    { "mget",         domget,         0, 2, "mget <file> [<file> ...]" },
    { "mkdir",        domkdir,        0, 2, "mkdir <directory>" },
    { "mput",         domput,         0, 2, "mput <file> [<file> ...]" },
    { "nlst",         dols,           0, 0, NULLCHAR },
    { "put",          doput,          0, 2, "put <localfile> <remotefile>" },
    { "quit",         doquit,         0, 0, NULLCHAR },
#ifdef FTP_RENAME
    { "rename",       dorenam,       0, 2, "rename <remotefrom> <remoteto> " },
#endif
#ifdef FTP_REGET
    { "reget",        doget,          0, 2, "reget <remotefile> <localfile>" },
    { "restart",      dorestart,      0, 1, "restart <fileoffset>" },
#endif
#ifdef FTP_RESUME
    { "resume",       doresume,       0, 2, "resume <remotefile> <localfile>" },
#endif
    { "rmdir",        dormdir,        0, 2, "rmdir <directory>" },
#ifdef FTP_RESUME
    { "rput",         dorput,         0, 2, "rput <localfile> <remotefile>" },
#endif
    { "type",         dotype,         0, 0, NULLCHAR },
    { "verbose",      doverbose,      0, 0, NULLCHAR },
    { "view",         doftpview,         0, 2,"view <remotefile>" },
    { NULLCHAR,       NULLFP((int,char**,void*)), 0, 0, NULLCHAR }
};
  
int
doftphelp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct cmds DFAR *cmdp;
    int i;
    char buf[77];
  
    j2tputs("\nFTP commands:\n");
    memset(buf,' ',sizeof(buf));
    buf[75] = '\n';
    buf[76] = '\0';
    for(i=0,cmdp = Ftpcmds;cmdp->name != NULL;cmdp++,i = (i+1)%7){
        strncpy(&buf[i*10],cmdp->name,strlen(cmdp->name));
        if(i == 6){
            j2tputs(buf);
            memset(buf,' ',sizeof(buf));
            buf[75] = '\n';
            buf[76] = '\0';
        }
    }
    if(i != 0)
        j2tputs(buf);
    tputc('\n');
    return 0;
}
  
/* Handle top-level FTP command */
int
doftp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct session *sp;
    struct ftpcli ftp;
    struct sockaddr_in fsocket;
    int resp,vsave;
    char *buf,*bufsav,*cp,*un;
#ifdef MD5AUTHENTICATE
    char *cp1;
#endif
    char prmt[40];
    int control;
    FILE *fp1 = NULLFILE;
    struct  cur_dirs dirs;
  
    /* Only from console - WG7J */
    if(Curproc->input != Command->input)
        return 0;
  
    /* Allocate a session control block */
    if((sp = newsession(argv[1],FTP,0)) == NULLSESSION){
        j2tputs(TooManySessions);
        return 1;
    }
    memset((char *)&ftp,0,sizeof(ftp));
    ftp.control = ftp.data = -1;
    ftp.verbose = V_BYTE;           /* Default changed by IW0CNB */
    ftp.type = Ftp_type;
    ftp.logbsize = Ftp_logbsize;
#ifdef LZW
    ftp.uselzw = Ftpclzw;
#endif
    buf = NULLCHAR;
  
    sp->cb.ftp = &ftp;      /* Downward link */
    ftp.session = sp;       /* Upward link */
  
    sp->curdirs = ftp.curdirs = &dirs;
  
    fsocket.sin_family = AF_INET;
    fsocket.sin_port = IPPORT_FTP;
  
    tprintf("Resolving %s... ",sp->name);
    if((fsocket.sin_addr.s_addr = resolve(sp->name)) == 0){
        tprintf(Badhost,sp->name);
        keywait(NULLCHAR,1);
        freesession(sp);
        return 1;
    }
  
    /* Open the control connection */
    if((control = sp->s = ftp.control = j2socket(AF_INET,SOCK_STREAM,0)) == -1){
        j2tputs("Can't create socket\n");
        keywait(NULLCHAR,1);
        freesession(sp);
        return 1;
    }
  
    sockmode(sp->s,SOCK_ASCII);
    j2setflush(sp->s,-1);     /* Flush output only when we call getresp() */
    tprintf("Trying %s...\n",psocket((struct sockaddr *)&fsocket));
    tprintf("Local Directory - %s\n",init_dirs(&dirs));

    if(j2connect(control,(char *)&fsocket,sizeof(fsocket)) != -1)
	{
    	tprintf("FTP session %u connected to %s\n",
			(unsigned)(sp-Sessions), sp->name);
  
    	/* Wait for greeting from server */
    	resp = getresp(&ftp,200);
  
    	if (resp >= 400)
			resp = -1;	/* replaces GOTO quit label */
		else
		{
#ifdef UNIX
    		if (!(un = getenv("LOGNAME")))
#endif
    			un = getenv("USER");
    		if(un != NULLCHAR)
        		sprintf(prmt,"Enter user name (%s): ",un);
    		else
        		sprintf(prmt,"Enter user name: ");
  
    		/* Now process responses and commands */
    		buf = mallocw(LINELEN);
  
    		if (argc > 2)
			{
        		if((fp1 = fopen(argv[2],READ_TEXT)) == NULLFILE)
					resp = -1; /* replaces the GOTO quit label */
    		}
		}
	}
	else resp = -1;	/* replaces the GOTO quit label on connect */

    while (resp != -1)
	{
        if (resp == 220)
		{
            /* Sign-on banner; prompt for and send USER command */
            if((cp = ftpcli_login(&ftp, sp->name)) == NULLCHAR){
                if (loc_getline(sp,prmt,buf,LINELEN) == -1) break;
                /* Send the command only if the user response
                 * was non-null
                 */
                rip(buf);
                if(*buf) cp=j2strdup(buf);
                else if(un != NULLCHAR)
                    cp=j2strdup(un);
                else {
                    j2tputs("No username sent\n");
                    resp = 200;     /* dummy */
                    continue;
                }
            }
            usprintf(control,"USER %s\n",cp);
            free(cp);
            resp = getresp(&ftp,200);
        } else if(resp == 331){
            if(ftp.password == NULLCHAR){
                /* turn off echo */
                sp->ttystate.echo = 0;
                if (loc_getline(sp,"Password: ",buf,LINELEN) == -1) break;
                tputc('\n');
                /* Turn echo back on */
                sp->ttystate.echo = 1;
                /* Send the command only if the user response
                 * was non-null
                 */
                rip(buf);
                if(*buf)
                    ftp.password = j2strdup(buf);
                else {
                    j2tputs("Password must be provided.\nLogin failed.\n");
                    resp = 200;     /* dummy */
#ifdef MD5AUTHENTICATE
                    free(ftp.line);
                    ftp.line = NULLCHAR;
#endif
                    continue;
                }
            }
#ifdef MD5AUTHENTICATE
            /* look for an MD5 challenge in the PASS prompt */
            if ( (cp=strchr(ftp.line,'[')) != NULLCHAR && 
                 (cp1=strchr(cp,']')) != NULLCHAR )
			{
                    *cp1 = '\0';
				/* 16Nov2009, Maiko, htol() changed to htoi() for 32 bits */
                    usprintf(control,"PASS %s\n",
						md5sum(htoi(cp+1),ftp.password));
            }
            else
                usprintf(control,"PASS %s\n",ftp.password);
            free(ftp.line);
            ftp.line = NULLCHAR;
#else
            usprintf(control,"PASS %s\n",ftp.password);
#endif
            resp = getresp(&ftp,200);
            free(ftp.password);
            ftp.password = NULLCHAR;        /* clean up */
        } else if(resp == 230) {    /* Successful login */
            /* Find out what type of system we're talking to */
            tprintf("ftp> syst\n");
            usprintf(control,"SYST\n");
            resp = getresp(&ftp,200);
        } else if(resp == 215) {    /* Response to SYST command */
            cp = strchr(ftp.line,' ');
            if(cp != NULLCHAR && strnicmp(cp+1,System,strlen(System)) == 0){
                ftp.type = IMAGE_TYPE;
                j2tputs("Defaulting to binary mode transfers\n");
            }
            resp = 200; /* dummy */
#ifdef DES_AUTH
        } else if(resp == 399) {        /* Encrypted password login */
            char l[17];

            if(ftp.password == NULLCHAR){
                if (loc_getline(sp,"Key ? --> ",buf,LINELEN) == -1) break;
                /* Send the command only if the user response
                 * was non-null
                 */
                if(buf[0] != '\n'){
                    cp = strchr(ftp.line,':');
                    cp += 2;
                    epass(htol(cp),buf,l);
                    l[16] = '\0';
                    free(ftp.line);
                    ftp.line = NULLCHAR;
                    usprintf(control,"PASS %s\n",l);
                    resp = getresp(&ftp,200);
                } else {
                    j2tputs("Password must be provided.\nLogin failed.\n");
                    resp = 200;     /* dummy */
                }
            } else {
                cp = strchr(ftp.line,':');
                cp += 2;
                epass(htol(cp),ftp.password,l);
                l[16] = '\0';
                free(ftp.line);
                ftp.line = NULLCHAR;
                usprintf(control,"PASS %s\n",l);
                resp = getresp(&ftp,200);
                free(ftp.password);
                ftp.password = NULLCHAR;        /* clean up */
            }
#endif
        } else {
            /* Test the control channel first */
            if(sockstate(control) == NULLCHAR)
                break;
            if(argc > 2){
                if(fgets(buf,LINELEN,fp1) == NULLCHAR)
                    break; /* replaces GOTO quit label */
            } else
                if (loc_getline(sp,"ftp> ",buf,LINELEN) == -1) break;
  
            /* Copy because cmdparse modifies the original */
            bufsav = j2strdup(buf);
            if((resp = cmdparse(Ftpcmds,buf,&ftp)) != -1){
                /* Valid command, free buffer and get another */
                free(bufsav);
            } else {
                /* Not a local cmd, send to remote server */
                usputs(control,bufsav);
                free(bufsav);
  
                /* Enable display of server response */
                vsave = ftp.verbose;
                ftp.verbose = V_NORMAL;
                resp = getresp(&ftp,200);
                ftp.verbose = vsave;
            }
        }
    }
/* quit:  GOTO label now replaced with setting resp = -1 and a break */
    free(buf);
    cp = sockerr(control);
    tprintf("FTP session %u closed: %s\n",(unsigned)(sp - Sessions),
    cp != NULLCHAR ? cp : "EOF");
  
    free(ftp.line);      /* fixed memory leak - K5JB */

    if(ftp.fp != NULLFILE && ftp.fp != stdout)
        fclose(ftp.fp);
    if(ftp.data != -1)
        close_s(ftp.data);
    if(ftp.control != -1)
        close_s(ftp.control);
    if(argc < 3)
        keywait(NULLCHAR,1);
    else
        fclose(fp1);
    if(ftp.session != NULLSESSION)
        freesession(ftp.session);
    free_dirs(&dirs);
    return 0;
}
  
#ifdef LZW
int
doftpslzw(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Ftpslzw,"FTP server lzw",argc,argv);
}
  
int
doftpclzw(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Ftpclzw,"FTP client lzw",argc,argv);
}
  
static int
dolocftpclzw(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
    int result;

/* We'd like to keep ftp.uselzw in sync with Ftpclzw (just so we can turn
   lzw on and off within an ftp session ... is it really worth it?).
   We need a separate proc since the ftpclzw cmd exists. --N5KNX */

    result = setbool(&Ftpclzw,"FTP client lzw",argc,argv);
    if((ftp = (struct ftpcli *)p) != NULLFTP &&
       (result == 0)) ftp->uselzw = Ftpclzw;  /* keep in sync with changes */
    return result;
}
#endif
  
/* Control verbosity level */
static int
doverbose(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
    return setshort(&ftp->verbose,"Verbose",argc,argv);
}
/* Enable/disable command batching */
static int
dobatch(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
    return setbool(&ftp->batch,"Command batching",argc,argv);
}
/* Set verbosity to high (convenience command) */
static int
dohash(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
    j2tputs("Hash Printing ");
    if (ftp->verbose==V_HASH){
        ftp->verbose = V_HASH+1;
        j2tputs("Off\n");
    } else {
        j2tputs("On\n");
        ftp->verbose = V_HASH;
    }
    return 0;
}
  
/* Close session */
static int
doquit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
    usputs(ftp->control,"QUIT\n");
  
    getresp(ftp,200);       /* Get the closing message */
    getresp(ftp,200);       /* Wait for the server to close */
    return -1;
}
  
/* Translate 'cd' to 'cwd' for convenience */
static int
doftpcd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
    usprintf(ftp->control,"CWD %s\n",argv[1]);
    return getresp(ftp,200);
}
/* Translate 'mkdir' to 'xmkd' for convenience */
static int
domkdir(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
    usprintf(ftp->control,"XMKD %s\n",argv[1]);
    return getresp(ftp,200);
}
/* Translate 'rmdir' to 'xrmd' for convenience */
static int
dormdir(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
    usprintf(ftp->control,"XRMD %s\n",argv[1]);
    return getresp(ftp,200);
}
static int
dobinary(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char *args[2];
  
    args[1] = "I";
    return dotype(2,args,p);
}
static int
doascii(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char *args[2];
  
    args[1] = "A";
    return dotype(2,args,p);
}
  
/* Handle "type" command from user */
static int
dotype(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
    if(argc < 2){
        switch(ftp->type){
            case IMAGE_TYPE:
                j2tputs("Image\n");
                break;
            case ASCII_TYPE:
                j2tputs("Ascii\n");
                break;
            case LOGICAL_TYPE:
                tprintf("Logical bytesize %u\n",ftp->logbsize);
                break;
        }
        return 0;
    }
    switch(*argv[1]){
        case 'i':
        case 'I':
        case 'b':
        case 'B':
            ftp->type = IMAGE_TYPE;
            break;
        case 'a':
        case 'A':
            ftp->type = ASCII_TYPE;
            break;
        case 'L':
        case 'l':
            ftp->type = LOGICAL_TYPE;
            ftp->logbsize = atoi(argv[2]);
            break;
        default:
            tprintf("Invalid type %s\n",argv[1]);
            return 1;
    }
    return 0;
}
  
/* Handle "ftype" command */
int
doftype(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2){
        j2tputs("Ftp initial TYPE is ");
        switch(Ftp_type){
            case IMAGE_TYPE:
                j2tputs("Image\n");
                break;
            case ASCII_TYPE:
                j2tputs("Ascii\n");
                break;
            case LOGICAL_TYPE:
                tprintf("Logical bytesize %u\n",Ftp_logbsize);
                break;
        }
        return 0;
    }
    switch(*argv[1]){
        case 'i':
        case 'I':
        case 'b':
        case 'B':
            Ftp_type = IMAGE_TYPE;
            break;
        case 'a':
        case 'A':
            Ftp_type = ASCII_TYPE;
            break;
        case 'L':
        case 'l':
            Ftp_type = LOGICAL_TYPE;
            Ftp_logbsize = atoi(argv[2]);
            break;
        default:
            tprintf("Invalid type %s\n",argv[1]);
            return 1;
    }
    return 0;
}
  
/* View added to jnos1.08 by Simon G1FHY _ mod by Paul@wolf.demon.co.uk */
/* Start view transfer. Syntax: view <remote name> */
static int
doftpview(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char *remotename;
    register struct ftpcli  *ftp;
  
    ftp = (struct ftpcli *)p;
    remotename = argv[1];
  
    getsub(ftp,"RETR",remotename,NULLCHAR);
    return 0;
}
/* Start receive transfer. Syntax: get <remote name> [<local name>] */
static int
doget(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char *remotename,*localname;
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
#ifdef FTP_REGET
    if(!strcmp(argv[0],"reget"))
        ftp->startpoint = -1;  /* special flag */
#endif
    remotename = argv[1];
    if(argc < 3)
        localname = remotename;
    else
        localname = argv[2];
  
    getsub(ftp,"RETR",remotename,localname);
    return 0;
}
/* Get a collection of files */
static int
domget(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
    FILE *files, *filel;
    char tmpname[80];
    char *buf, *local;
    int i;
    long r;
#ifdef MSDOS
    char *c;
	int inlist;
#endif
  
    ftp = (struct ftpcli *)p;
    j2tmpnam(tmpname);
    buf = mallocw(DIRBUF);
    ftp->state = RECEIVING_STATE;
    for(i=1;i<argc;i++){
        if(argv[i][0] == '@'){
#ifdef MSDOS
            inlist = 1;
#endif
            if((filel = fopen(make_fname(ftp->curdirs->dir,&argv[i][1]), "r")) == NULLFILE){
                tprintf("Can't open listfile: %s\n", &argv[i][1]);
                continue;
            }
            if((files = fopen(tmpname, "w")) == NULLFILE){
                tprintf("Can't open tempfile: %s\n", tmpname);
                continue;
            }
            while(fgets(buf,DIRBUF,filel) != NULLCHAR){
                fputs(buf,files);
            }
            fclose(files);
            fclose(filel);
            if((files = fopen(tmpname, "r")) == NULLFILE){
                tprintf("Can't open tempfile: %s\n", tmpname);
                continue;
            }
        } else {
#ifdef MSDOS
            inlist = 0;
#endif
            r = getsub(ftp,"NLST",argv[i],tmpname);
            if(ftp->abort)
                break;  /* Aborted */
            if(r == -1 || (files = fopen(tmpname,"r")) == NULLFILE){
                tprintf("Can't NLST %s\n",argv[i]);
                unlink(tmpname);
                continue;
            }
        }
        /* The tmp file now contains a list of the remote files, so
         * go get 'em. Break out if the user signals an abort.
         */
        while(fgets(buf,DIRBUF,files) != NULLCHAR){
            rip(buf);
            local = j2strdup(buf);
#ifdef  MSDOS
            if(inlist){
                strrev(local);
                strtok(local, "\\/[]<>,?#~()&%");
                strrev(local);
            }
            if((c = strstr(local, ".")) != NULLCHAR) {
                c++;
                c = strtok(c, ".");     /* remove 2nd period if any*/
            }
#endif
            getsub(ftp,"RETR",buf,local);
            free(local);
            if(ftp->abort){
                /* User abort */
                ftp->abort = 0;
                fclose(files);
                unlink(tmpname);
                free(buf);
                ftp->state = COMMAND_STATE;
                return 1;
            }
        }
        fclose(files);
        unlink(tmpname);
    }
    free(buf);
    ftp->state = COMMAND_STATE;
    ftp->abort = 0;
    return 0;
}
#ifdef FTP_RESUME
/* Resume interrupted file receive. Syntax: resume <remote name> [<local name>] */
static int
doresume(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char *remotename,*localname;
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
    remotename = argv[1];
    if(argc < 3)
        localname = remotename;
    else
        localname = argv[2];
  
    getsub(ftp,"RSME",remotename,localname);
    return 0;
}
#endif
/* List remote directory. Syntax: dir <remote files> [<local name>] */
static int
dolist(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char *remotename,*localname;
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
    remotename = argv[1];
    if(argc > 2)
        localname = argv[2];
    else
        localname = NULLCHAR;
  
    getsub(ftp,"LIST",remotename,localname);
    return 0;
}
/* Remote directory list, short form. Syntax: ls <remote files> [<local name>] */
static int
dols(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char *remotename,*localname;
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
    remotename = argv[1];
    if(argc > 2)
        localname = argv[2];
    else
        localname = NULLCHAR;
  
    getsub(ftp,"NLST",remotename,localname);
    return 0;
}

/* 21Dec2004, Maiko, Replaces GOTO 'failure' label in getsub() */
static int do_failure2 (FILE *fp, struct ftpcli *ftp,
		int prevstate, int savmode)
{
    if(fp != NULLFILE && fp != stdout)
        fclose(fp);
    close_s(ftp->data);
    ftp->data = -1;
    ftp->state = prevstate;
    ftp->type = savmode;
    return -1;
}

/* Common code to LIST/NLST/RETR/RSME and mget
 * Returns number of bytes received if successful
 * Returns -1 on error
 */
static long
getsub(ftp,command,remotename,localname)
register struct ftpcli *ftp;
char *command,*remotename,*localname;
{
    long total;
    FILE *fp;
    int resp,i,control,savmode;
    char *mode;
    struct sockaddr_in lsocket;
    struct sockaddr_in lcsocket;
    int64 startclk;
    int32 rate;
    int vsave;
    int typewait = 0, portwait = 0;
#ifdef FTP_REGET
    int restwait = 0;
#endif
    int prevstate;
    long starting;
#ifdef  LZW
    int lzwmode, lzwbits;
    int rcode;
    extern int16 Lzwbits;
    extern int Lzwmode;
#endif
  
    control = ftp->control;
    savmode = ftp->type;
  
#ifdef FTP_REGET
    if(ftp->startpoint && strcmp(command,"RETR"))
        ftp->startpoint=0;  /* must have consecutive:  REST n; RETR */
#endif

    switch(ftp->type){
        case IMAGE_TYPE:
        case LOGICAL_TYPE:
#ifdef FTP_RESUME
            if(strcmp(command,"RSME") == 0)
                mode = APPEND_BINARY;
            else
#endif
#ifdef FTP_REGET
            if(ftp->startpoint)
                mode = READ_BINARY "+";  /* open read-update, seek, write */
            else
#endif
                mode = WRITE_BINARY;
            break;
        case ASCII_TYPE:
#ifdef FTP_RESUME
            if(strcmp(command,"RSME") == 0)
                mode = APPEND_TEXT;
            else
#endif
#ifdef FTP_REGET
            if(ftp->startpoint)
                mode = READ_TEXT "+";  /* open read-update, seek, write */
            else
#endif
                mode = WRITE_TEXT;
            break;
#ifdef __GNUC__
        default:
            mode = NULLCHAR;  /* semi-spurious warning avoidance */
            break;
#endif
    }
    /* Open the file */
    if(localname == NULLCHAR){
        fp = NULLFILE;
    } else if((fp = fopen(make_fname(ftp->curdirs->dir,localname),mode)) == NULLFILE){
        tprintf("Can't write %s: %s\n",localname,strerror(errno));
        return -1;
    }
    /* Open the data connection */
    ftp->data = j2socket(AF_INET,SOCK_STREAM,0);
    j2listen(ftp->data,0);    /* Accept only one connection */
    prevstate = ftp->state;
    ftp->state = RECEIVING_STATE;
  
    /* Send TYPE message, if necessary */
    if(strcmp(command,"LIST") == 0 || strcmp(command,"NLST") == 0){
        /* Directory listings are always in ASCII */
        ftp->type = ASCII_TYPE;
    }
    if(ftp->typesent != ftp->type){
        switch(ftp->type){
            case ASCII_TYPE:
                usputs(control,"TYPE A\n");
                break;
            case IMAGE_TYPE:
                usputs(control,"TYPE I\n");
                break;
            case LOGICAL_TYPE:
                usprintf(control,"TYPE L %d\n",ftp->logbsize);
                break;
        }
        ftp->typesent = ftp->type;
        if(!ftp->batch){
            resp = getresp(ftp,200);
            if(resp == -1 || resp > 299)
				return (do_failure2 (fp, ftp, prevstate, savmode));
        } else
            typewait = 1;
    }
    /* Send the PORT message. Use the IP address
     * on the local end of our control connection.
     */
    i = SOCKSIZE;
    j2getsockname(ftp->data,(char *)&lsocket,&i); /* Get port number */
    i = SOCKSIZE;
    j2getsockname(ftp->control,(char *)&lcsocket,&i);
    lsocket.sin_addr.s_addr = lcsocket.sin_addr.s_addr;
    sendport(control,&lsocket);
    if(!ftp->batch){
        /* Get response to PORT command */
        resp = getresp(ftp,200);
        if(resp == -1 || resp > 299)
			return (do_failure2 (fp, ftp, prevstate, savmode));
    }
    else portwait=1;
#ifdef LZW
    if(ftp->uselzw && ftp->type == ASCII_TYPE) {
        /* Send XLZW string */
        usprintf(control, "XLZW %d %d\n", Lzwbits, Lzwmode);
        if(ftp->batch){
            /* Get response to TYPE command, if sent */
            if(typewait){
                resp = getresp(ftp,200);
                if(resp == -1 || resp > 299){
					return (do_failure2 (fp, ftp, prevstate, savmode));
                }
                typewait=0;
            }
            /* Get response to PORT command, if needed */
            if(portwait){
                resp = getresp(ftp,200);
                if(resp == -1 || resp > 299){
					return (do_failure2 (fp, ftp, prevstate, savmode));
                }
                portwait=0;
            }
        }
        resp = getresp(ftp, 200);
        if (resp == -1 || resp > 299)
            ftp->uselzw=0;    /* Command not supported. */
        else {
            /* strip off any garbage. */
            rip(ftp->line);
            rcode = lzwmode = lzwbits = 0;
            /* Get bits from the XLZW response. */
            sscanf(ftp->line, "%d %d %d", &rcode, &lzwbits, &lzwmode);
            if ((rcode >= 200) && (rcode < 300)) {
                if (lzwmode != Lzwmode || lzwbits != Lzwbits) {
                    lzwmode = LZWCOMPACT;
                    lzwbits = LZWBITS;
                }
            }
            /* Turn on the LZW stuff. */
            lzwinit(ftp->data,lzwbits,lzwmode);
        }
    }
#endif
#ifdef FTP_REGET
    if(ftp->startpoint) {  /* need to send a REST <offset> command */
        if((starting=ftp->startpoint) == -1L) {  /* reget needs localfile length */
            if((starting = getsize(fp)) == -1)
                starting = 0L;
        }
        fseek(fp,starting,SEEK_SET);
        ftp->startpoint = 0;  /* only get one chance */
        if(starting) {
            usprintf(control, "REST %ld\n", starting);
            if(!ftp->batch){
                /* Get response to REST command */
                resp = getresp(ftp,350);
                if(resp == -1 || resp > 350)
					return (do_failure2 (fp, ftp, prevstate, savmode));
            }
            else restwait=1;
        }
    }
#endif
    /* Generate the command to start the transfer */
    if(remotename != NULLCHAR)
        usprintf(control,"%s %s\n",command,remotename);
    else
        usprintf(control,"%s\n",command);
  
    if(ftp->batch){
        /* Get response to TYPE command, if sent */
        if(typewait){
            resp = getresp(ftp,200);
            if(resp == -1 || resp > 299)
				return (do_failure2 (fp, ftp, prevstate, savmode));
        }
        /* Get response to PORT command, if needed */
        if(portwait){
            resp = getresp(ftp,200);
            if(resp == -1 || resp > 299)
				return (do_failure2 (fp, ftp, prevstate, savmode));
        }
#ifdef FTP_REGET
        /* Get response to REST command, if sent */
        if(restwait){
            resp = getresp(ftp,350);
            if(resp == -1 || resp > 350)
				return (do_failure2 (fp, ftp, prevstate, savmode));
        }
#endif
    }
    /* Get the intermediate "150" response */
    resp = getresp(ftp,100);
    if(resp == -1 || resp >= 400)
		return (do_failure2 (fp, ftp, prevstate, savmode));
  
    /* Wait for the server to open the data connection */
    j2accept(ftp->data,NULLCHAR,(int *)NULL);
    startclk = msclock();
  
    /* If output is to the screen, temporarily disable hash marking */
    vsave = ftp->verbose;
    if(vsave >= V_HASH && fp == NULLFILE)
        ftp->verbose = V_NORMAL;
  
#ifdef FTP_RESUME
    if(strcmp(command,"RSME") == 0){
        if((starting = getsize(fp)) == -1)
            starting = 0L;
        usprintf(control,"%ld %lu\n",starting,checksum(fp,starting));
        usflush(control);
        fseek(fp,starting,SEEK_SET);
    }
#endif
    total = recvfile(fp,ftp->data,ftp->type,(ftp->verbose >= V_HASH) ? ftp->verbose : 0, 0);
    /* Immediately close the data connection; some servers (e.g., TOPS-10)
     * wait for the data connection to close completely before returning
     * the completion message on the control channel
     */
    close_s(ftp->data);
    ftp->data = -1;
  
    if(fp != NULLFILE && fp != stdout)
        fclose(fp);
    if(remotename == NULLCHAR)
        remotename = "";
    if(total == -1)
        tprintf("%s %s: Error/abort during data transfer\n",command,remotename);
    else if(ftp->verbose >= V_SHORT)
    {
        startclk = msclock() - startclk;
        rate = 0;
        if (startclk != 0) /* Avoid divide-by-zero */
	{
	/* Avoid overflow */
            if (total < MAXLONG)
                rate = (int32)(total * 1000 / startclk);
            else
                rate = (int32)(total / startclk / 1000);
        }
        tprintf("%s %s: %ld bytes in %d sec (%d/sec)\n", command,
		remotename, total, (int32)(startclk / 1000), rate);
    }
    /* Get the "Sent" message */
    getresp(ftp,200);
  
    ftp->state = prevstate;
    ftp->verbose = vsave;
    ftp->type = savmode;
    return total;
 
/* wow this is not even reached, so there is no point putting
 * the new do_failure2 () function in here to replace it
 
	return (do_failure2 (fp, ftp, prevstate, savmode));

    failure:
    if(fp != NULLFILE && fp != stdout)
        fclose(fp);
    close_s(ftp->data);
    ftp->data = -1;
    ftp->state = prevstate;
    ftp->type = savmode;
    return -1;
*/
}
/* Send a file. Syntax: put <local name> [<remote name>] */
static int
doput(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
    char *remotename,*localname;
  
    ftp = (struct ftpcli *)p;
    localname = argv[1];
    if(argc < 3)
        remotename = localname;
    else
        remotename = argv[2];
  
    putsub(ftp,remotename,localname,0);
    return 0;
}
/* Put a collection of files */
static int
domput(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
    FILE *files;
    int i;
    char tmpname[80];
    char *buf,*file;
  
    ftp = (struct ftpcli *)p;
    j2tmpnam(tmpname);
    if((files = fopen(tmpname,"w+")) == NULLFILE){
        j2tputs("Can't list local files\n");
        unlink(tmpname);
        return 1;
    }
  
    for(i=1;i<argc;i++) {
    /* Use the path in the ftp client struct, since user may have done
     * a lcd command to change dir !
     */
        file = pathname(ftp->curdirs->dir,argv[i]);
        getdir(file,0,files);
        free(file);
    }
    rewind(files);
    buf = mallocw(DIRBUF);
    ftp->state = SENDING_STATE;
    while(fgets(buf,DIRBUF,files) != NULLCHAR){
        rip(buf);
        putsub(ftp,buf,buf,0);
        if(ftp->abort)
            break;          /* User abort */
    }
    fclose(files);
    unlink(tmpname);
    free(buf);
    ftp->state = COMMAND_STATE;
    ftp->abort = 0;
    return 0;
}
#ifdef FTP_RESUME
/* Put a file, appending data to it - iw0cnb */
static int
dorput(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
    char *remotename,*localname;
  
    ftp = (struct ftpcli *)p;
    localname = argv[1];
    if(argc < 3)
        remotename = localname;
    else
        remotename = argv[2];
  
    putsub(ftp,remotename,localname,1);
    return 0;
}
#endif

/* 21Dec2004, Maiko, Replaces GOTO 'failure' label in putsub() */
static int do_failure (FILE *fp, struct ftpcli *ftp, int prevstate)
{
    /* Error, quit */
    fclose (fp);
    close_s (ftp->data);
    ftp->data = -1;
    ftp->state = prevstate;
    return -1;
}

/* Common code to put, mput, rput.
 * Returns number of bytes sent if successful
 * Returns -1 on error
 */
static long
putsub(ftp,remotename,localname,putr)
register struct ftpcli *ftp;
char *remotename,*localname;
int putr;       /* Flag: 0 if standard put, 1 if put with resume */
{
    char *mode;
    int i,resp,control;
    long total;
    FILE *fp;
    struct sockaddr_in lsocket,lcsocket;
    int32 startclk,rate;
    int typewait = 0, portwait = 0;
#ifdef FTP_REGET
    int restwait = 0;
#endif
    int prevstate;
    char *line;
    long starting;
    unsigned long check, local_check;
#ifdef  LZW
    int lzwmode, lzwbits;
    int rcode;
    extern int16 Lzwbits;
    extern int Lzwmode;
#endif
  
    control = ftp->control;
    if(ftp->type == IMAGE_TYPE)
        mode = READ_BINARY;
    else
        mode = READ_TEXT;
  
    /* Open the file */
    if((fp = fopen(make_fname(ftp->curdirs->dir,localname),mode)) == NULLFILE){
        tprintf("Can't read %s: %s\n",localname,strerror(errno));
        return -1;
    }
    if(ftp->type == ASCII_TYPE && isbinary(fp)){
        tprintf("Warning: type is ASCII and %s appears to be binary\n",localname);
    }
    /* Open the data connection */
    ftp->data = j2socket(AF_INET,SOCK_STREAM,0);
    j2listen(ftp->data,0);
    prevstate = ftp->state;
    ftp->state = SENDING_STATE;
  
    /* Send TYPE message, if necessary */
    if(ftp->typesent != ftp->type){
        switch(ftp->type){
            case ASCII_TYPE:
                usputs(control,"TYPE A\n");
                break;
            case IMAGE_TYPE:
                usputs(control,"TYPE I\n");
                break;
            case LOGICAL_TYPE:
                usprintf(control,"TYPE L %d\n",ftp->logbsize);
                break;
        }
        ftp->typesent = ftp->type;
  
        /* Get response to TYPE command */
        if(!ftp->batch){
            resp = getresp(ftp,200);
            if(resp == -1 || resp > 299){
				return (do_failure (fp, ftp, prevstate));
            }
        } else
            typewait = 1;
    }
    /* Send the PORT message. Use the IP address
     * on the local end of our control connection.
     */
    i = SOCKSIZE;
    j2getsockname(ftp->data,(char *)&lsocket,&i);
    i = SOCKSIZE;
    j2getsockname(ftp->control,(char *)&lcsocket,&i);
    lsocket.sin_addr.s_addr = lcsocket.sin_addr.s_addr;
    sendport(control,&lsocket);
    if(!ftp->batch){
        /* Get response to PORT command */
        resp = getresp(ftp,200);
        if(resp == -1 || resp > 299){
			return (do_failure (fp, ftp, prevstate));
        }
    }
    else portwait=1;
#ifdef FTP_REGET
    if(ftp->startpoint) {  /* need to send a REST <offset> command */
        starting=ftp->startpoint;
        fseek(fp,starting,SEEK_SET);
        ftp->startpoint = 0;  /* only get one chance */
        usprintf(control, "REST %ld\n", starting);
        if(!ftp->batch){
            /* Get response to REST command */
            resp = getresp(ftp,350);
            if(resp == -1 || resp > 350)
				return (do_failure (fp, ftp, prevstate));
        }
        else restwait=1;
    }
#endif
#ifdef LZW
    if(ftp->uselzw && ftp->type == ASCII_TYPE) {
        /* Send XLZW string */
        usprintf(control, "XLZW %d %d\n", Lzwbits, Lzwmode);
        if(ftp->batch){
            /* Get response to TYPE command, if sent */
            if(typewait){
                resp = getresp(ftp,200);
                if(resp == -1 || resp > 299){
					return (do_failure (fp, ftp, prevstate));
                }
                typewait=0;
            }
            /* Get response to PORT command, if needed */
            if(portwait){
                resp = getresp(ftp,200);
                if(resp == -1 || resp > 299){
					return (do_failure (fp, ftp, prevstate));
                }
                portwait=0;
            }
#ifdef FTP_REGET
            /* Get response to REST command, if sent */
            if(restwait){
                resp = getresp(ftp,350);
                if(resp == -1 || resp > 350)
					return (do_failure (fp, ftp, prevstate));
                restwait=0;
            }
#endif
        }
        resp = getresp(ftp, 200);
        if (resp == -1 || resp > 299)
            ftp->uselzw=0;    /* Command not supported. */
        else {
            /* strip off any garbage. */
            rip(ftp->line);
            rcode = lzwmode = lzwbits = 0;
            /* Get bits from the XLZW response. */
            sscanf(ftp->line, "%d %d %d", &rcode, &lzwbits, &lzwmode);
            if ((rcode >= 200) && (rcode < 300)) {
                if (lzwmode != Lzwmode || lzwbits != Lzwbits) {
                    lzwmode = LZWCOMPACT;
                    lzwbits = LZWBITS;
                }
            }
            /* Turn on the LZW stuff. */
            lzwinit(ftp->data,lzwbits,lzwmode);
        }
    }
#endif
    /* Generate the command to start the transfer */
#ifdef FTP_RESUME
    if(putr == 1)
        usprintf(control,"RPUT %s\n",remotename);
    else
#endif
        usprintf(control,"STOR %s\n",remotename);
  
    if(ftp->batch){
        /* Get response to TYPE command, if sent */
        if(typewait){
            resp = getresp(ftp,200);
            if(resp == -1 || resp > 299){
				return (do_failure (fp, ftp, prevstate));
            }
        }
        /* Get response to PORT command, if needed */
        if(portwait){
            resp = getresp(ftp,200);
            if(resp == -1 || resp > 299){
				return (do_failure (fp, ftp, prevstate));
            }
        }
#ifdef FTP_REGET
        /* Get response to REST command, if sent */
        if(restwait){
            resp = getresp(ftp,350);
            if(resp == -1 || resp > 350)
				return (do_failure (fp, ftp, prevstate));
        }
#endif
    }
    /* Get the intermediate "150" response */
    resp = getresp(ftp,100);
    if(resp == -1 || resp >= 400){
		return (do_failure (fp, ftp, prevstate));
    }
  
    /* Wait for the data connection to open. Otherwise the first
     * block of data would go out with the SYN, and this may confuse
     * some other TCPs
     */
    j2accept(ftp->data,NULLCHAR,(int *)NULL);
  
    startclk = msclock();
#ifdef FTP_RESUME
    if(putr == 1){          /* Wait for file offset and checksum */
        line=mallocw(40);
        if (recvline(control,line,40) == -1)
			return (do_failure (fp, ftp, prevstate));
        starting=atol(line);
        check = (unsigned long)atol(strchr(line,' '));
        free(line);
        local_check = checksum(fp,starting);
        if(ftp->verbose >= V_HASH)
            tprintf("Remote checksum: %lu Local checksum: %lu Offset: %ld\n",check,local_check,starting);
        check -= local_check;
        if(check != 0){
            tprintf("Can't send %s: files are different\n",localname);
            j2shutdown(ftp->data,1);
            getresp(ftp,200);
			return (do_failure (fp, ftp, prevstate));
        }
    }
#endif
  
    total = sendfile(fp,ftp->data,ftp->type,(ftp->verbose >= V_HASH) ? ftp->verbose : 0,NULL);
    close_s(ftp->data);
    ftp->data = -1;
    fclose(fp);
  
    /* Wait for control channel ack before calculating transfer time;
     * this accounts for transmitted data in the pipe.
     */
    getresp(ftp,200);
  
    if(total == -1){
        tprintf("STOR %s: Error/abort during data transfer\n",remotename);
    } else if(ftp->verbose >= V_SHORT){
        startclk = msclock() - startclk;
        rate = 0;
        if(startclk != 0){      /* Avoid divide-by-zero */
            if(total < 4294967L){
                rate = (int32)total * 1000 / startclk;
            } else {        /* Avoid overflow */
                rate = (int32)total / (startclk / 1000);
            }
        }
        tprintf("STOR %s: %ld bytes in %d sec (%d/sec)\n",
    	   	remotename, total, startclk / 1000, rate);
    }
    ftp->state = prevstate;

    return total;

/* wow, this does not even get in here, so we don't have to
   replace this chunk of code with new do_failure() func.

	return (do_failure (fp, ftp, prevstate));
 
    failure:
    fclose(fp);
    close_s(ftp->data);
    ftp->data = -1;
    ftp->state = prevstate;
    return -1;
*/
}

/* Abort a GET or PUT operation in progress. Note: this will leave
 * the partial file on the local or remote system
 */
int
doabort(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct session *sp;
    register struct ftpcli *ftp;
  
    sp = (struct session *)p;
    if(sp == NULLSESSION)
        return -1;
  
    /* Default is the current session, but it can be overridden with
     * an argument.
     */
    if(argc > 1)
        sp = sessptr(argv[1]);
  
    if(sp == NULLSESSION || sp->type != FTP){
        j2tputs("Not an active FTP session\n");
        return 1;
    }
    ftp = sp->cb.ftp;
    switch(ftp->state){
        case COMMAND_STATE:
            j2tputs("No active transfer\n");
            return 1;
        case SENDING_STATE:
        /* Send a premature EOF.
         * Unfortunately we can't just reset the connection
         * since the remote side might end up waiting forever
         * for us to send something.
         */
            j2shutdown(ftp->data,1);  /* Note fall-thru */
            ftp->abort = 1;
            break;
        case RECEIVING_STATE:
        /* Just blow away the receive socket */
            j2shutdown(ftp->data,2);  /* Note fall-thru */
            ftp->abort = 1;
            break;
    }
    return 0;
}
/* send PORT message */
static void
sendport(s,socket)
int s;
struct sockaddr_in *socket;
{
    /* Send PORT a,a,a,a,p,p message */
    usprintf(s,"PORT %u,%u,%u,%u,%u,%u\n",
    hibyte(hiword(socket->sin_addr.s_addr)),
    lobyte(hiword(socket->sin_addr.s_addr)),
    hibyte(loword(socket->sin_addr.s_addr)),
    lobyte(loword(socket->sin_addr.s_addr)),
    hibyte(socket->sin_port),
    lobyte(socket->sin_port));
}
  
/* Wait for, read and display response from FTP server. Return the result code.
 */
static int
getresp(ftp,mincode)
struct ftpcli *ftp;
int mincode;    /* Keep reading until at least this code comes back */
{
    register char *line;
    int rval;
  
    usflush(ftp->control);
    line = mallocw(LINELEN);
    for(;;){
        /* Get line */
        if(recvline(ftp->control,line,LINELEN) == -1){
            rval = -1;
            break;
        }
        rip(line);              /* Remove cr/lf */
        rval = atoi(line);
        if(rval >= 400 || ftp->verbose >= V_NORMAL)
            tprintf("%s\n",line);   /* Display to user */
  
        /* Messages with dashes are continued */
        if(line[3] != '-' && rval >= mincode)
            break;
    }

    if((rval == 215) /* SYST */
#ifdef LZW
        || (rval == 299)  /* XLZW */
#endif
#ifdef DES_AUTH
        || (rval == 399)  /* iw0cnb: encrypted password request */
#endif
#ifdef MD5AUTHENTICATE
        || (rval == 331)  /* n5knx: pass request, perhaps with challenge */
#endif
      ) {
        ftp->line = line;         /* caller must free it, before next call! */
    }
    else free(line);
    return rval;
}
  
/* Issue a prompt and read a line from the user */
static int loc_getline (struct session *sp, char *prompt, char *buf, int n)
{
    int rtn;  /* K5JB */

    /* If there's something already there, don't issue prompt */
    if(socklen(sp->input,0) == 0)
        j2tputs(prompt);
  
    usflush(sp->output);
    rtn = recvline(sp->input,buf,(unsigned)n);
    /* This added so our commands added to record file -- K5JB */
    if(sp->ttystate.echo && sp->record != NULLFILE)
           fputs(buf,sp->record);
    return rtn;
}
  
/* Read net.rc file, looking for host's entry.  Then return the username (or NULL),
 * and alloc & set ftp->password if passwd in net.rc too, else set to NULL.
 */
static char *
ftpcli_login(ftp,host)
struct ftpcli *ftp;
char *host; {
  
    char buf[80],*cp = (char*)0,*cp1;
    FILE *fp;
  
    extern char *Hostfile;  /* List of user names and permissions */
  
    if((fp = fopen(Hostfile,"r")) == NULLFILE){
        return NULLCHAR;
    }
    while(fgets(buf,sizeof(buf),fp),!feof(fp)){
        buf[strlen(buf)-1] = '\0';      /* Nuke the newline */
        if(buf[0] == '#')
            continue;       /* Comment */
        if((cp = strpbrk(buf," \t")) == NULLCHAR)
            /* Bogus entry */
            continue;
        *cp++ = '\0';           /* Now points to user name */
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
    cp = skipwhite(cp);
    if((cp1 = strpbrk(cp," \t")) == NULLCHAR)
        /* if not there then we'll prompt */
        ftp->password = NULLCHAR;
    else {
        *cp1++ = '\0';          /* Now points to password */
        cp1 = skipwhite(cp1);
        ftp->password = j2strdup(cp1);
    }
    if(strcmp(cp,"*") == 0)
        cp = "anonymous";
    return j2strdup(cp);
}
  
static int
dolcd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
  
    if(argc > 1){
        if (!dir_ok(argv[1],ftp->curdirs)) {
  
            tprintf("Invalid Drive/Directory - %s\n",argv[1]);
            return 1;
        }
  
    }
    tprintf("Local Directory - %s\n",ftp->curdirs->dir);
  
    return 0;
}
  
  
static int
doldir(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
    char ** margv;
  
    margv=(char **)callocw(2,sizeof(char*));
  
    ftp = (struct ftpcli *)p;
  
    tputc('\n');
    margv[1]=j2strdup(make_dir_path(argc,argv[1],ftp->curdirs->dir));
    dircmd(2,margv,p);
    free(margv[1]);
    free(margv);
    tputc('\n');
    return 0;
}
  
static int
dolmkdir(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
    char *buf;
  
    ftp = (struct ftpcli *)p;
    undosify(argv[1]);
    buf=j2strdup(make_fname(ftp->curdirs->dir,argv[1]));
#ifdef UNIX
    if (mkdir(buf, (mode_t)0777) == -1)
#else
        if (mkdir(buf) == -1)
#endif
            tprintf("Can't make %s: %s\n",buf,strerror(errno));
        else
            tprintf("Directory %s Created\n",buf);
    free(buf);
    return 0;
}
  
#ifdef FTP_REGET
static int
dorestart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
  
    ftp = (struct ftpcli *)p;
    ftp->startpoint = atol(argv[1]);
    tprintf("restarting at %ld.  Next issue a get, put, or append.\n", ftp->startpoint);
    return 0;
}
#endif

#ifdef FTP_RENAME
/* rename <from> <to> on remote system */
static int
dorenam(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ftpcli *ftp;
    register int resp;
  
    ftp = (struct ftpcli *)p;
    usprintf(ftp->control,"RNFR %s\n",argv[1]);
    resp = getresp(ftp,200);
    if (resp == 350) {
        usprintf(ftp->control,"RNTO %s\n",argv[2]);
        resp = getresp(ftp, 200);
        if (resp == 250)
            return 0;  /* all OK */
    }
    return 1;
}
#endif
#endif /* FTPSESSION */
