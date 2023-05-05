/*
 * JNOS 2.0
 *
 * $Id: smtpcli.c,v 1.2 2016/11/02 20:02:41 root Exp root $
 *
 *  CLIENT routines for Simple Mail Transfer Protocol ala RFC821
 *  A.D. Barksdale Garbee II, aka Bdale, N3EUA
 *  Copyright 1986 Bdale Garbee, All Rights Reserved.
 *  Permission granted for non-commercial copying and use, provided
 *  this notice is retained.
 *  Modified 14 June 1987 by P. Karn for symbolic target addresses,
 *  also rebuilt locking mechanism
 *  Copyright 1987 1988 David Trulli, All Rights Reserved.
 *  Permission granted for non-commercial copying and use, provided
 *  this notice is retained.
 *
 * Mods by G1EMM and PA0GRI and VE4KLM
 */
#include <fcntl.h>
#include <time.h>
#include <setjmp.h>
#ifdef UNIX
#include <sys/types.h>
#endif
#ifdef  AMIGA
#include <stat.h>
#else
#include <sys/stat.h>
#endif
#ifdef  __TURBOC__
#include <dir.h>
#include <io.h>
#endif
#include "global.h"
#ifdef  ANSIPROTO
#include <stdarg.h>
#endif
#include "mbuf.h"
#include "cmdparse.h"
#include "proc.h"
#include "socket.h"
#ifdef  LZW
#include "lzw.h"
#endif
#include "timer.h"
#include "netuser.h"
#include "smtp.h"
#include "dirutil.h"
#include "commands.h"
#include "session.h"
#include "files.h"
#include "domain.h"
  
#ifdef HOPPER
#include "ip.h"
int UseHopper = 1;      /* G8FSL SMTP hopper default ON */
static int dohopper __ARGS((int argc,char *argv[],void *p));  /* g8fsl */
#endif

#define	OKAY_TO_GATEWAY_BADADDR	/* 28Jun2015, back to original function, in
				 * other words, let the gateway determine if
				 * the address is bad or whatever !!!
				 */

struct timer Smtpcli_t;        /* not static because referenced in dosend() */

static struct gateway {
    int32 ipaddr;
    char *name;
} Gateway = { 0, NULLCHAR };

static int smtp_running=0;
  
#ifdef SMTPTRACE
unsigned short Smtptrace = 0;        /* used for trace level */
static int dosmtptrace __ARGS((int argc,char *argv[],void *p));
static char smtp_recv[] = "smtpcli recv: %s\n";
#endif
  
static unsigned  short Smtpmaxcli  = MAXSESSIONS;   /* the max client connections allowed */
static int Smtpsessions = 0;        /* number of client connections
                    * currently open */
#ifdef STATUSWIN
#ifdef SMTPSERVER
extern
#endif
int SmtpUsers;
#endif // STATUSWIN

#ifdef  LZW
int Smtpslzw = 1;
int Smtpclzw = 1;
static int Smtpbatch = 1;
#else
static int Smtpbatch = 0;
#endif
int Smtpmode = 0;
  
int Smtpquiet = 0;
int UseMX = 0;          /* use MX records in domain lookup */
  
static struct smtpcli *cli_session[MAXSESSIONS]; /* queue of client sessions  */
  
#ifdef UNIX
/* n5knx: The Smtptrace msgs should NOT use printf (line endings wrong) */
#define printf tcmdprintf
#endif

static void smtp_fwd __ARGS((int unused,void *t,void *p));
static void del_job __ARGS((struct smtp_job *jp));
static void del_session __ARGS((struct smtpcli *cb));
static int dodebug (int argc, char **argv, void *p);	/* 11May2014, Maiko */
static int dogateway __ARGS((int argc,char *argv[],void *p));
static int dosmtpmaxcli __ARGS((int argc,char *argv[],void *p));
static int dosmtpmaxserv __ARGS((int argc,char *argv[],void *p));
static int dotimer __ARGS((int argc,char **argv,void *p));
static int doquiet __ARGS((int argc,char *argv[],void *p));
#ifdef LZW
static int doclzw __ARGS((int argc,char *argv[],void *p));
static int doslzw __ARGS((int argc,char *argv[],void *p));
#endif
#if defined (SMTP_DENY_RELAY) && defined (SDR_EXCEPTIONS)
static int dorelay __ARGS((int argc,char **argv,void *p));
#endif
static int dousemx __ARGS((int argc,char *argv[],void *p));
static int dosmtpkill __ARGS((int argc,char *argv[],void *p));
static int dosmtplist __ARGS((int argc,char *argv[],void *p));
static int dobatch __ARGS((int argc,char *argv[],void *p));
static void execjobs __ARGS((void));
static int getresp __ARGS((struct smtpcli *ftp,int mincode));
static void logerr __ARGS((struct smtpcli *cb,char *line));
#ifdef J2_MULTI_SMTP_SESSION
static struct smtpcli *lookup __ARGS((int32 destaddr, char *prefix));
#else
static struct smtpcli *lookup __ARGS((int32 destaddr));
#endif
static struct smtpcli *newcb __ARGS((void));
static int next_job __ARGS((struct smtpcli *cb));
static void retmail __ARGS((struct smtpcli *cb));
static void sendcmd __ARGS((struct smtpcli *cb,char *fmt,...));
static int smtpsendfile __ARGS((struct smtpcli *cb));
static int setsmtpmode __ARGS((int argc,char *argv[],void *p));
static struct smtp_job *setupjob __ARGS((struct smtpcli *cb,char *id,char *from));
static void smtp_send __ARGS((int unused,void *cb1,void *p));
static int smtpkick __ARGS((int argc,char **argv,void *p));
static int dosmtpt4 __ARGS((int argc,char **argv,void *p));
static int dosmtpservtdisc __ARGS((int argc,char **argv,void *p));
static void check_qtime __ARGS((struct smtpcli *cb));
static int dodtimeout __ARGS((int argc,char *argv[],void *p));
  
static struct cmds DFAR Smtpcmds[] = {
    { "batch", dobatch,    0,  0,  NULLCHAR },
    { "debug", dodebug, 0,  0,  NULLCHAR },	/* 11May2014, Maiko, New */
    { "dtimeout", dodtimeout, 0,  0,  NULLCHAR },
    { "gateway",  dogateway,  0,  0,  NULLCHAR },
#ifdef HOPPER
    { "hopper",   dohopper,   0,  0,  NULLCHAR },  /* g8fsl */
#endif
    { "kick",     smtpkick,   0,  0,  NULLCHAR },
    { "kill",     dosmtpkill, 0,  2,  "kill <jobnumber>" },
    { "list",     dosmtplist, 0,  0,  NULLCHAR },
    { "maxclients",   dosmtpmaxcli,   0,  0,  NULLCHAR },
#ifdef SMTPSERVER
    { "maxservers",   dosmtpmaxserv,  0,  0,  NULLCHAR },
#endif
    { "mode",     setsmtpmode,    0,  0,  NULLCHAR },
    { "quiet",    doquiet,    0,  0,  NULLCHAR },
#ifdef  LZW
    { "reclzw",   doslzw,     0,  0,  NULLCHAR },
#endif
#if defined (SMTP_DENY_RELAY) && defined (SDR_EXCEPTIONS)
    { "relay",	dorelay,    0,  0,  NULLCHAR },
#endif
#ifdef  LZW
    { "sendlzw",  doclzw,     0,  0,  NULLCHAR },
#endif
#ifdef SMTPSERVER
    { "tdisc",    dosmtpservtdisc,0,0,NULLCHAR },
#endif
    { "timer",    dotimer,    0,  0,  NULLCHAR },
#ifdef SMTPTRACE
    { "trace",    dosmtptrace,    0,  0,  NULLCHAR },
#endif
    { "t4",       dosmtpt4,   0,  0,  NULLCHAR },
    { "usemx",    dousemx,    0,  0,  NULLCHAR },
    { NULLCHAR,	NULL,		0,	0,	NULLCHAR }
};
  
int
dosmtp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Smtpcmds,argc,argv,p);
}
  
int Sdtimer = 0;

static int
dodtimeout(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint(&Sdtimer,"Delivery timeout (hours)",argc,argv);
}

int32 Smtpt4;
  
static int dosmtpt4 (int argc, char **argv, void *p)
{
    return setint32 (&Smtpt4, "SMTP T4", argc, argv);
}
  
static int
dobatch(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Smtpbatch,"SMTP batching",argc,argv);
}
  
#ifdef LZW
static int
doclzw(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Smtpclzw,"SMTP send lzw",argc,argv);
}
#endif
  
static int
doquiet(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Smtpquiet,"SMTP quiet",argc,argv);
}

#if defined (SMTP_DENY_RELAY) && defined (SDR_EXCEPTIONS)

extern void sdre_add (char*, char*);
extern void sdre_delete (char*, char*);
extern void sdre_list ();

static int dorelay (int argc, char **argv, void *p)
{
	int dousage = 0;

	if (argc == 4)
	{
		if (!strcmp ("add", argv[1]))
			sdre_add (argv[2], argv[3]);
		else if (!strcmp ("delete", argv[1]))
			sdre_delete (argv[2], argv[3]);
		else
			dousage = 1;
	}
	else if (argc == 2)
	{
		if (!strcmp ("list", argv[1]))
			sdre_list ();
		else
			dousage = 1;
	}
	else dousage = 1;

	if (dousage)
        tprintf ("usage : smtp relay [add|delete] [ip] [netmask]\n      : smtp relay [list]\n");

    return 0;
}

#endif
  
#ifdef LZW
static int
doslzw(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Smtpslzw,"SMTP recv lzw",argc,argv);
}
#endif
  
static int
dousemx(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&UseMX,"MX records used",argc,argv);
}
static int
dosmtpmaxcli(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setshort(&Smtpmaxcli,"Max clients",argc,argv);
}
  
#ifdef SMTPSERVER
int32 SmtpServTdisc;
  
static int dosmtpservtdisc (int argc, char **argv, void *p)
{
    return setint32 (&SmtpServTdisc,"Server tdisc (sec)",argc,argv);
}
  
extern int Smtpmaxserv;

static int
dosmtpmaxserv(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint(&Smtpmaxserv,"Max servers",argc,argv);
}
#endif
  
static int
setsmtpmode(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if (argc < 2) {
        tprintf("smtp mode: %s\n",
        (Smtpmode & QUEUE) ? "queue" : "route");
    } else {
        switch(*argv[1]) {
            case 'q':
                Smtpmode |= QUEUE;
                break;
            case 'r':
                Smtpmode &= ~QUEUE;
                break;
            default:
                j2tputs("Usage: smtp mode [queue | route]\n");
                break;
        }
    }
    return 0;
}

#ifdef	SGW_EXCEPTIONS
/*
 * New gateways modes - 14Dec2011, Maiko, Default to the old way
 */

static char *gwmodestr[] = { "original", "force", "first", "last" };

enum sgwm { SMTPGWORIG, SMTPGWFORCE, SMTPGWFIRST, SMTPGWLAST };

enum sgwm smtp_gwmode = SMTPGWORIG;

extern void sgwe_add (char*, char*);
extern void sgwe_delete (char*, char*);
extern void sgwe_list ();
extern int sgwe_check (int32);

#endif

/* 11May2014, Maiko, Changed to be always defined and now a global, not static */
int smtp_debug = 0;

/* 11May2014, Maiko, Function to enable or disable SMTP debug code */
static int dodebug (int argc, char **argv, void *p)
{
    return setbool (&smtp_debug, "some smtp debug code", argc, argv);
}

static int dogateway (int argc, char **argv, void *p)
{
    int32 n;
  
    if(argc < 2){
        tprintf("%s\n",Gateway.name ? Gateway.name : "none");
    } else {
#ifdef	SGW_EXCEPTIONS
        if (!stricmp (argv[1], "exception"))
		{
    		if (argc == 5)
			{
				if (!strcmp ("add", argv[2]))
					sgwe_add (argv[3], argv[4]);
				else if (!strcmp ("delete", argv[2]))
					sgwe_delete (argv[3], argv[4]);
				else
        			tprintf ("usage : smtp gateway exception [add|delete] [ip] [netmask]\n");
			}
			else sgwe_list ();

			return 0;
		}
		else if (!stricmp (argv[1], "mode"))
		{
    		if (argc == 3)
			{
        		if (!stricmp (argv[2], "original"))
					smtp_gwmode = SMTPGWORIG;
        		else if (!stricmp (argv[2], "force"))
					smtp_gwmode = SMTPGWFORCE;
        		else if (!stricmp (argv[2], "first"))
					smtp_gwmode = SMTPGWFIRST;
        		else if (!stricmp (argv[2], "last"))
					smtp_gwmode = SMTPGWLAST;
				else
        			tprintf ("usage : smtp gateway mode [original|force|first|last]\n");
			}
			else tprintf ("smtp gateway mode [%s]\n", gwmodestr[smtp_gwmode]);

			return 0;
		}
		else
#endif
        if(!stricmp(argv[1],"none"))
            n = 0;  /* reset gateway */
        else if((n = resolve(argv[1])) == 0 /*|| ismyaddr(n)*/){
            tprintf(Badhost,argv[1]);
            return 1;
        }
#ifdef	SGW_EXCEPTIONS
		/* 14Dec2011, Maiko, Reset to old way if gateway is undefined */
		if (!n) smtp_gwmode = 0;
#endif
        free(Gateway.name);
        Gateway.name = ((Gateway.ipaddr = n)!=0) ?
                       domainsuffix(argv[1]) : NULLCHAR;
    }
    return 0;
}
  
#ifdef HOPPER
static int
dohopper(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&UseHopper,"G8FSL mail hopper",argc,argv);
}
#endif

#ifdef SMTPTRACE
static int
dosmtptrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setshort(&Smtptrace,"SMTP tracing",argc,argv);
}
#endif
  
/* list jobs waiting to be sent in the mqueue */
static int
dosmtplist(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char tstring[80];
    char line[20];
    char host[LINELEN];
    char to[LINELEN];
    char from[LINELEN];
    char *cp;
    char status;
    int flowsave;
    struct ffblk ff;
#ifndef UNIX
    struct ftime *ft;
#endif
    FILE *fp;
  
    flowsave = Current->flowmode;
    Current->flowmode = 1; /* Enable the more mechanism */
    j2tputs("S   Job   Size Date  Time  Host                 From\n");
    for (filedir(Mailqueue,0,line); line[0] != '\0'; filedir(Mailqueue,1,line)) {
        sprintf(tstring,"%s/%s",Mailqdir,line);
        if ((fp = fopen(tstring,READ_TEXT)) == NULLFILE) {
            tprintf("Can't open %s: %s\n",tstring,strerror(errno));
            continue;
        }
        if ((cp = strrchr(line,'.')) != NULLCHAR)
            *cp = '\0';
        sprintf(tstring,"%s/%s.lck",Mailqdir,line);
        if (access(tstring,0))
            status = ' ';
        else
            status = 'L';
        /* This is a no-no for Borland compilers...
         * Blows up DV and OS/2 VDM !
         * stat(tstring,&stbuf);
         */
        sprintf(tstring,"%s/%s.txt",Mailqdir,line);
        findfirst(tstring,&ff,0);
#ifndef UNIX
        ft = (struct ftime *) &ff.ff_ftime;
#endif
        host[0] = from[0] = '\0';   /* in case fgets() fails */
        fgets(host,sizeof(host),fp);
        rip(host);
        fgets(from,sizeof(from),fp);
        rip(from);
  
        tprintf("%c %5s %6ld %02d/%02d %02d:%02d %-20s %s\n  To:",
        status, line,
        ff.ff_fsize,
#ifdef UNIX
        ff.ff_ftime.tm_mon+1, ff.ff_ftime.tm_mday,
        ff.ff_ftime.tm_hour, ff.ff_ftime.tm_min,
#else
        ft->ft_month,ft->ft_day,
        ft->ft_hour,ft->ft_min,
#endif
        host,from);
        while (fgets(to,sizeof(to),fp) != NULLCHAR) {
            rip(to);
            tprintf(" %s",to);
        }
        tputc('\n');
        fclose(fp);
#ifdef UNIX
        findlast(&ff);   /* free allocs */
#endif
        pwait(NULL);
    }
    Current->flowmode = flowsave;
    return 0;
}
  
/* kill a job in the mqueue */
static int
dosmtpkill(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char s[SLINELEN];
    char *cp,c;
    sprintf(s,"%s/%s.lck",Mailqdir,argv[1]);
    cp = strrchr(s,'.');
    if (!access(s,0)) {
        Current->ttystate.echo = Current->ttystate.edit = 0;
        c = keywait("Warning, job is locked by SMTP. Remove (y/n)? ",0);
        Current->ttystate.echo = Current->ttystate.edit = 1;
        if (c != 'y')
            return 0;
        unlink(s);
    }
    strcpy(cp,".wrk");
    if (unlink(s))
        tprintf("Job id %s not found\n",argv[1]);
    strcpy(cp,".txt");
    unlink(s);
    return 0;
}
  
/* Set outbound spool scan interval */
static int dotimer (int argc, char **argv, void *p)
{
    if(argc < 2)
	{
        tprintf("smtp timer = %d/%d\n", read_timer(&Smtpcli_t)/1000,
        	dur_timer(&Smtpcli_t)/1000);
        return 0;
    }
    Smtpcli_t.func = (void (*)__ARGS((void*)))smtptick;/* what to call on timeout */
    Smtpcli_t.arg = NULL;       /* dummy value */
    set_timer(&Smtpcli_t,(uint32)atoi(argv[1])*1000);  /* set timer duration */
    start_timer(&Smtpcli_t);        /* and fire it up */
    return 0;
}
  
static int smtpkick (int argc, char **argv, void *p)
{
    int32 addr = 0;

	/*
	 * 01Oct2009, Maiko, Use a temporary variable to bridge between the use
	 * of int32 and void. Compiling on 64 bit (x86_64) is generating warnings
	 * about casting pointers to integer of different size. I have run some
	 * test programs and the warnings *seem* to be of no consequence, BUT
	 * it should be fixed anyways so that there are NO warnings !
	 */
    long addr_l;

    if(argc > 1 && (addr = resolve(argv[1])) == 0){
        tprintf(Badhost,argv[1]);
        return 1;
    }

	addr_l = (long)addr;

    smtptick ((void *)addr_l);

    return 0;
}
  
/* This is the routine that gets called every so often to do outgoing
 * mail processing. When called with a null argument, it runs the entire
 * queue; if called with a specific non-zero IP address from the remote
 * kick server, it only starts up sessions to that address.
 */
void
smtptick(t)
void *t;
{
#ifdef	JNOS_HEAVY_DEBUG
	log (-1, "begin smtptick - smtp_running %d", smtp_running);
#endif
    if (!smtp_running) {
        smtp_running=1;
        if(newproc("smtp_fwd", 4096, smtp_fwd, 0, t,NULL,0) == NULLPROC) {
            start_timer(&Smtpcli_t);
            smtp_running = 0;
        }
    }

#ifdef	JNOS_HEAVY_DEBUG
	log (-1, "end smtptick");
#endif
}


/* The old smtptick function now becomes a process "smtp_fwd"
*/

static void
smtp_fwd(unused,t,p)
int unused;
void *t;
void *p;
{
    register struct smtpcli *cb;
    struct smtp_job *jp;
    struct list *ap;
    char    tmpstring[LINELEN], wfilename[13], prefix[9];
    char    from[LINELEN], to[LINELEN];
    char *cp, *cp1;
    int32 destaddr,target;
    FILE *wfile;
#ifndef UNIX
    int  qcount=0;
#endif

	/*
	 * 01Oct2009, Maiko, Use a temporary variable to bridge between the use
	 * of int32 and void. See my more detailed comments under smtpkick ().
	 */
    long addr_l = (long)t;

#ifdef	JNOS_HEAVY_DEBUG
	log (-1, "SM begin");
#endif
    target = (int32)addr_l;

#ifdef SMTPTRACE
    if (Smtptrace > 5)
        printf("smtp daemon entered, target = %s\n",inet_ntoa(target));
#endif
#ifndef UNIX
    if(availmem() < Memthresh){
        /* Memory is tight, don't do anything */
        /* Restart timer */
        start_timer(&Smtpcli_t);
        smtp_running = 0;
        return;
    }
#endif
    /* Recent Internet SMTP gateway practice is to return one of a set of
     * ip addresses when a gateway is looked up in the DNS, to effect load
     * sharing.  For Jnos to benefit, it must allow the Gateway.ipaddr to
     * vary also.  But, it need not vary for each connection; once per
     * smtp_fwd process invocation should be sufficient. -- n5knx 4/99
     * Note that invoking 'smtp gateway xxx' could occur as we block in
     * this process, thus yielding an unexpected change in the gateway spec.
     * This seems harmless ....
     */
    if(Gateway.name)
        Gateway.ipaddr = resolve(Gateway.name);

    for(filedir(Mailqueue,0,wfilename);wfilename[0] != '\0';
    	filedir(Mailqueue,1,wfilename))
	{
        /* save the prefix of the file name which it job id */
        cp = wfilename;
        cp1 = prefix;
        while (*cp && *cp != '.')
            *cp1++ = *cp++;
        *cp1 = '\0';

#ifdef	JNOS_HEAVY_DEBUG
		log (-1, "SM locking prefix [%s]", prefix);
#endif
        /* lock this file from the smtp daemon */
        if (mlock(Mailqdir,prefix))
            continue;

        sprintf(tmpstring,"%s/%s",Mailqdir,wfilename);
        if ((wfile = fopen(tmpstring,READ_TEXT)) == NULLFILE)
		{
#ifdef	JNOS_HEAVY_DEBUG
			log (-1, "SM fopen [%s] failed, rmlock [%s]", tmpstring, prefix);
#endif
            /* probably too many open files */
            rmlock(Mailqdir,prefix);

#ifdef	JNOS_HEAVY_DEBUG
			log (-1, "SM after fopen rmlock");
#endif
            /* continue to next message. The failure
            * may be temporary */
            continue;
        }
  
        fgets(tmpstring,LINELEN,wfile);  /* read target host */
        rip(tmpstring);

        if ((destaddr = mailroute(tmpstring)) == 0)
		{
#ifdef	JNOS_HEAVY_DEBUG
			log (-1, "SM mailroute [%s] failed", tmpstring);
#endif
            fclose(wfile);
            printf("** smtp: Unknown address %s\n",tmpstring);
            rmlock(Mailqdir,prefix);

#ifdef	JNOS_HEAVY_DEBUG
			log (-1, "SM after mailroute rmlock");
#endif
            continue;
        }

        if(target != 0 && destaddr != target)
		{
#ifdef	JNOS_HEAVY_DEBUG
			log (-1, "SM not proper target [%d] destaddr [%d]",
				target, destaddr);
#endif
            fclose(wfile);
            rmlock(Mailqdir,prefix);

#ifdef	JNOS_HEAVY_DEBUG
			log (-1, "SM after not proper target rmlock");
#endif
            continue;   /* Not the proper target of a kick */
        }

#ifdef J2_MULTI_SMTP_SESSION

/*
 * 26Oct2016, Maiko (VE4KLM), The JNOS smtp client is limiting in that if one
 * sends an email to a main recipient and multiple carbon copy recipients, all
 * going to the same destination host, then delivery of carbon copy recipients
 * is delayed till the next SMTP tick, and so on. This scenario was reported
 * by N6MEF (Fox), so I've made small changes to allow multiple SMTP sessions
 * to exist to the same destination box, which should solve the 'this system
 * is already sending mail' scenario further down below in the code.
 */
        if ((cb = lookup(destaddr, prefix)) == NULLSMTPCLI)
#else
        if ((cb = lookup(destaddr)) == NULLSMTPCLI)
#endif
	{
            if ((cb = newcb()) == NULLSMTPCLI)
			{
#ifdef	JNOS_HEAVY_DEBUG
				log (-1, "SM newcb failed");
#endif

                fclose(wfile);
                rmlock(Mailqdir,prefix);

#ifdef	JNOS_HEAVY_DEBUG
				log (-1, "SM after newcb failed rmlock");
#endif
                break;
            }
            cb->ipdest = destaddr;
            cb->destname = j2strdup(tmpstring);
#ifdef J2_MULTI_SMTP_SESSION
            cb->prefix = j2strdup(prefix);
#endif
        } else {
            if(cb->lock)
			{
#ifdef	JNOS_HEAVY_DEBUG
				log (-1, "SM system busy, don't interfere");
#endif
                /* This system is already is sending mail lets not
                * interfere with its send queue.
                */
                fclose(wfile);
                rmlock(Mailqdir,prefix);

#ifdef	JNOS_HEAVY_DEBUG
				log (-1, "SM after system busy rmlock");
#endif
                continue;
            }
        }
  
        fgets(from,LINELEN,wfile);   /* read from */
        rip(from);

#ifdef	JNOS_HEAVY_DEBUG
		log (-1, "SM from [%s]", from);
#endif
        if ((jp = setupjob(cb,prefix,from)) == NULLJOB)
		{
#ifdef	JNOS_HEAVY_DEBUG
			log (-1, "SM setupjob failed");
#endif
            fclose(wfile);
            rmlock(Mailqdir,prefix);

#ifdef	JNOS_HEAVY_DEBUG
			log (-1, "SM after setupjob failed rmlock");
#endif
            del_session(cb);

#ifdef	JNOS_HEAVY_DEBUG
			log (-1, "SM after del_session");
#endif
            break;
        }

        while (fgets(to,LINELEN,wfile) != NULLCHAR)
		{
            rip(to);

#ifdef	JNOS_HEAVY_DEBUG
			log (-1, "SM fgets TO [%s]", to);
#endif
			if (strchr (to, '@') == NULL)
			{
				strcat (to, "@");
				strcat (to, Hostname);

				if (smtp_debug)
					log (-1, "smtpcli - NEW to [%s]", to);
			}

            if (addlist(&jp->to,to,DOMAIN,NULLCHAR) == NULLLIST)
			{
#ifdef	JNOS_HEAVY_DEBUG
				log (-1, "SM addlist failed");
#endif
                fclose(wfile);
                del_session(cb);

#ifdef	JNOS_HEAVY_DEBUG
				log (-1, "SM after addlist del_session failed");
#endif
            }
        }

#ifdef	JNOS_HEAVY_DEBUG
		log (-1, "SM close wfile");
#endif
        fclose(wfile);

#ifdef	JNOS_HEAVY_DEBUG
		log (-1, "SM queue job %s From: %s", prefix, from);

		for (ap = jp->to; ap != NULLLIST; ap = ap->next)
                log (-1, "SM To: %s", ap->val);
#endif

#ifdef SMTPTRACE
        if (Smtptrace > 2) {
            printf("queue job %s From: %s To:",prefix,from);
            for (ap = jp->to; ap != NULLLIST; ap = ap->next)
                printf(" %s",ap->val);
            printf("\n");
        }
#endif
#ifndef UNIX
        if (!(++qcount % 10) && (availmem() < 2*Memthresh))  /* check mem every 10th entry */
            break;  /* quit early if we are running low on memory */
#endif
    }

	/* pscurproc ();	20Oct2009, Maiko, Debugging the stack */
  
#ifdef	JNOS_HEAVY_DEBUG
	log (-1, "SM before execjobs");
#endif
    /* start sending that mail */
    execjobs();

#ifdef	JNOS_HEAVY_DEBUG
	log (-1, "SM restart timer");
#endif

    /* Restart timer */
    start_timer(&Smtpcli_t);

#ifdef	JNOS_HEAVY_DEBUG
	log (-1, "SM done");
#endif

    smtp_running = 0;

    return;
}

/* 29Dec2004, Function to replace GOTO 'quit' labels */
static void do_quit (struct smtpcli *cb)
{
    sendcmd(cb,"QUIT\n");
    check_qtime(cb);
    if (cb->errlog != NULLLIST)
	{
        retmail(cb);
        unlink(cb->wname);   /* unlink workfile */
        unlink(cb->tname);   /* unlink text */
    }
    close_s(cb->s);
#ifdef STATUSWIN
    SmtpUsers--;
#endif
    if(cb->tfile != NULLFILE)
        fclose(cb->tfile);
    cb->lock = 0;
    del_session(cb);
}

#ifdef	WPAGES
/* 28Oct2019, Slightly different SMTP message for WP updates */
static char *smSentJob[2] = { "sent", "processed WP" };
#endif

/* This is the master state machine that handles a single SMTP transaction.
 * It is called with a queue of jobs for a particular host.
 * The logic is complicated by the "Smtpbatch" variable, which controls
 * the batching of SMTP commands. If Smtpbatch is true, then many of the
 * SMTP commands are sent in one swell foop before waiting for any of
 * the responses. Unfortunately, this breaks many brain-damaged SMTP servers
 * out there, so provisions have to be made to operate SMTP in lock-step mode.
 */
static void
smtp_send(unused,cb1,p)
int unused;
void *cb1;
void *p;
{
    register struct smtpcli *cb;
    register struct list *tp;
    struct sockaddr_in fsocket;
    char *cp;
    int32 Altmx[5];
    int rcode;
    int rcpts;
    int goodrcpt;
    int i;
    int smtpbatch;
    int init = 1;
	int connected = 0;
#ifdef	WPAGES
	int smSentIdx = 0;
#endif
#ifdef  LZW
    int lzwmode, lzwbits;
    extern int16 Lzwbits;
    extern int Lzwmode;
#endif
  
    cb = (struct smtpcli *)cb1;
    cb->lock = 1;

    fsocket.sin_family = AF_INET;

	/*
	 * New gateways modes - 14Dec2011, FIRST implies there are exceptions,
	 * so why do I have a FORCE option ? Why not just use FIRST without any
	 * configured exceptions ? What if in an emergency you just wanted to
	 * force everything (without exception) but leave your configuration
	 * intact ? That's why. Note the new sgwe_check () function call !
	 */
#ifdef	SGW_EXCEPTIONS
	if (smtp_gwmode == SMTPGWFORCE)
    	fsocket.sin_addr.s_addr = Gateway.ipaddr;
	else if ((smtp_gwmode == SMTPGWFIRST) && !sgwe_check ((int32)cb->ipdest))
   		fsocket.sin_addr.s_addr = Gateway.ipaddr;
	else
#endif
    fsocket.sin_addr.s_addr = cb->ipdest;

    fsocket.sin_port = IPPORT_SMTP;
  
    cb->s = j2socket(AF_INET,SOCK_STREAM,0);
    sockmode(cb->s,SOCK_ASCII);
    j2setflush(cb->s,-1); /* We'll explicitly flush before reading */
#ifdef	SGW_EXCEPTIONS
	if (smtp_debug)
		log (cb->s, "smtp [%s] trying", inet_ntoa (fsocket.sin_addr.s_addr));
#endif
#ifdef SMTPTRACE
    if (Smtptrace>1)
        printf("SMTP client Trying...\n");
#endif
    /* Set a timeout for this connection */
    j2alarm(Smtpt4 * 1000);
    if(j2connect(cb->s,(char *)&fsocket,SOCKSIZE) != 0)
	{
        j2alarm(0);
        j2shutdown(cb->s,2);  /* K2MF: To make sure it doesn't linger around */
        close_s(cb->s);     /* to make sure it's closed */

#ifdef	SGW_EXCEPTIONS
	if (smtp_debug)
		log (cb->s, "smtp [%s] timeout", inet_ntoa (fsocket.sin_addr.s_addr));
#endif
		connected = 0;	/* replaces GOTO 'connected' label */

        /* Selcuk: Let's try other MX's before Gateway */
        if(UseMX && fsocket.sin_addr.s_addr != (uint32)Gateway.ipaddr
        && fsocket.sin_addr.s_addr != (uint32)Ip_addr)
		{
#ifdef	SGW_EXCEPTIONS
			if (smtp_debug)
				log (cb->s, "smtp MX attempt");
#endif
            if(resolve_amx(cb->destname,(int32)fsocket.sin_addr.s_addr,Altmx))
			{
#ifdef SMTPTRACE
                if (Smtptrace > 1)
                    printf("SMTP client trying MX...\n");
#endif
                for(i=0;Altmx[i];i++) /* try highest to lowest preference */
				{
                    fsocket.sin_addr.s_addr = Altmx[i];
                    /* n5knx: don't deliver to self, that's a loop!  Also, to avoid indirect
                       loops we ignore sites with lower preference than ourselves */
                    if (ismyaddr((int32)fsocket.sin_addr.s_addr)) break;  /* was continue */
                    cb->s = j2socket(AF_INET,SOCK_STREAM,0);
                    sockmode(cb->s,SOCK_ASCII);
                    j2setflush(cb->s,-1);
#ifdef	SGW_EXCEPTIONS
					if (smtp_debug)
						log (cb->s, "smtp MX [%s] trying",
							inet_ntoa (fsocket.sin_addr.s_addr));
#endif
                    j2alarm(Smtpt4 * 1000);
                    if(j2connect(cb->s,(char *)&fsocket,SOCKSIZE) == 0)
					{
                        connected = 1;	/* replaces GOTO 'connected' */
						break;
					}
                    else
					{
                        j2alarm(0);
                        j2shutdown(cb->s,2);  /* K2MF: To make sure it doesn't linger around */
                        close_s(cb->s);
#ifdef	SGW_EXCEPTIONS
						if (smtp_debug)
							log (cb->s, "smtp MX [%s] timeout",
								inet_ntoa (fsocket.sin_addr.s_addr));
#endif
                    }
                }
            }
        }
		if (!connected)
		{
        if(Smtpt4 && Gateway.ipaddr
#ifdef	SGW_EXCEPTIONS
		/* 15Dec2011, Maiko (VE4KLM), Only do last resort if original mode */
			&& (smtp_gwmode == SMTPGWORIG)
#endif
           && (fsocket.sin_addr.s_addr != (uint32)Gateway.ipaddr)
           && (!ismyaddr(Gateway.ipaddr))
          ) {
            /* Try it via the gateway */
            fsocket.sin_addr.s_addr = Gateway.ipaddr;
            cb->s = j2socket(AF_INET,SOCK_STREAM,0);
            sockmode(cb->s,SOCK_ASCII);
            j2setflush(cb->s,-1); /* We'll explicitly flush before reading */
#ifdef	SGW_EXCEPTIONS
			if (smtp_debug)
				log (cb->s, "smtp (last resort) [%s] trying",
					inet_ntoa (fsocket.sin_addr.s_addr));
#endif
#ifdef SMTPTRACE
            if (Smtptrace>1)
                printf("SMTP client Trying gateway...\n");
#endif
            /* Set a timeout for this connection */
            j2alarm(Smtpt4 * 1000);
            if(j2connect(cb->s,(char *)&fsocket,SOCKSIZE) != 0){
                j2alarm(0);
                cp = sockerr(cb->s);
                j2shutdown(cb->s,2);  /* K2MF: To make sure it doesn't linger around */
                close_s(cb->s);     /* to make sure it's closed */
#ifdef	SGW_EXCEPTIONS
				if (smtp_debug)
					log (cb->s, "smtp [%s] timeout",
						inet_ntoa (fsocket.sin_addr.s_addr));
#endif
#ifdef SMTPTRACE
                if (Smtptrace>1)
                    printf("Connect failed: %s\n",cp != NULLCHAR ? cp : "");
#endif
                log(cb->s,"SMTP %s Connect failed: %s",psocket(&fsocket),
                    cp != NULLCHAR ? cp : "");
#ifdef STATUSWIN
                SmtpUsers++;                // because it is decremented in quit
#endif
				return (do_quit (cb));
            }
        } else {
#ifdef STATUSWIN
            SmtpUsers++;                // because it is decremented in quit
#endif
			return (do_quit (cb));
        }

		}	/* end of (!connected) */
    }

    j2alarm(0);
#ifdef	SGW_EXCEPTIONS
	if (smtp_debug)
		log (cb->s, "smtp connected");
#endif
#ifdef SMTPTRACE
    if (Smtptrace>1)
        printf("Connected\n");
#endif

#ifdef STATUSWIN
    SmtpUsers++;
#endif
  
#ifdef  LZW
    rcode = getresp(cb,200);
    if(rcode == -1 || rcode >= 400)
		return (do_quit (cb));
  
    if(Smtpclzw && (ismyaddr((int32)fsocket.sin_addr.s_addr)==NULLIF)) { /* don't try LZW if it's us */
        char cp[LINELEN];
        sendcmd(cb,"XLZW %d %d\n",Lzwbits,Lzwmode);
        usflush(cb->s);
	/*
	 * 02Feb2017, VE4KLM (Maiko), Added timeout code to this recvline, it
	 * never had one if you can believe it. Fox was running into SMTP hang
	 * scenarios and after looking at all the RF trace files he gave me, I
	 * can conclude it looks like JNOS is waiting forever for a response to
	 * the XLZW command. They have very heavy IP usage over 1200 baud for
	 * their SMTP sessions, so I am guessing network congestion is behind
	 * this. Putting an alarm on this recvline should definitely fix it.
 	 */
        j2alarm (Smtpt4 * 1000); /* set a timeout */

        if(recvline(cb->s,cp,sizeof(cp)) == -1)
		{
            j2alarm (0);   /* reset a timeout */

			log (cb->s, "outgoing SMTP failed, xlzw timeout");	/* 08Feb2017 */

			return (do_quit (cb));
		}

		j2alarm (0);   /* reset a timeout */

        rip(cp);

#ifdef  SMTPTRACE
        if(Smtptrace>1)
            printf(smtp_recv,cp);/* Display to user */
#endif
        rcode = lzwmode = lzwbits = 0;
        sscanf(cp,"%d %d %d",&rcode,&lzwbits,&lzwmode);
        if((rcode >= 200) && (rcode < 300)) {
            smtpbatch = 1;
            if(lzwmode != Lzwmode || lzwbits != Lzwbits) {
                lzwmode = LZWCOMPACT;
                lzwbits = LZWBITS;
            }
            lzwinit(cb->s,lzwbits,lzwmode);
        } else {
            smtpbatch = Smtpbatch;
        }
    } else {
        smtpbatch = Smtpbatch;
    }
#else
    smtpbatch = Smtpbatch;
    if(!smtpbatch){
        rcode = getresp(cb,200);
        if(rcode == -1 || rcode >= 400)
			return (do_quit (cb));
    }
#endif
    /* Say HELO */
    sendcmd(cb,"HELO %s\n",Hostname);
    if(!smtpbatch){
        rcode = getresp(cb,200);
        if(rcode == -1 || rcode >= 400)
			return (do_quit (cb));
    }
    do {    /* For each message... */

	/* pscurproc ();	20Oct2009, Maiko, Debugging */

        /* if this file open fails, skip it */
        if ((cb->tfile = fopen(cb->tname,READ_TEXT)) == NULLFILE)
            continue;

        /* Send MAIL and RCPT commands */
        sendcmd(cb,"MAIL FROM:<%s>\n",cb->jobq->from);
        if(!smtpbatch){
            rcode = getresp(cb,200);
            if(rcode == -1 || rcode >= 400)
				return (do_quit (cb));
        }
        rcpts = 0;
        goodrcpt = 0;
        for (tp = cb->jobq->to; tp != NULLLIST; tp = tp->next){
            sendcmd(cb,"RCPT TO:<%s>\n",tp->val);
            if(!smtpbatch){
                rcode = getresp(cb,200);
                if(rcode == -1)
					return (do_quit (cb));
                if(rcode < 400)
                    goodrcpt = 1; /* At least one good */
            }
            rcpts++;
        }
        if (!smtpbatch && !goodrcpt)
			return (do_quit (cb));
        /* Send DATA command */
        sendcmd(cb,"DATA\n");
        if(!smtpbatch){
            rcode = getresp(cb,200);
            if(rcode == -1 || rcode >= 400)
				return (do_quit (cb));
        }
        if(smtpbatch){
            /* Now wait for the responses to come back. The first time
             * we do this, we wait first for the start banner and
             * HELO response. In any case, we wait for the response to
             * the MAIL command here.
             */
#ifdef  LZW
            for(i= init ? 2 : 1;i > 0;i--)
#else
            for(i= init ? 3 : 1;i > 0;i--)
#endif
		{
                rcode = getresp(cb,200);
                if(rcode == -1 || rcode >= 400)
					return (do_quit (cb));
            }
            init = 0;
  
            /* Now process the responses to the RCPT commands */
            for(i=rcpts;i!=0;i--){
                rcode = getresp(cb,200);
                if(rcode == -1)
					return (do_quit (cb));
                if(rcode < 400)
                    goodrcpt = 1; /* At least one good */
            }
            /* And finally get the response to the DATA command.
             * Some servers will return failure here if no recipients
             * are valid, some won't.
             */
            rcode = getresp(cb,200);
            if(rcode == -1 || rcode >= 400)
				return (do_quit (cb));
  
            /* check for no good rcpt on the list */
            if (goodrcpt == 0){
                sendcmd(cb,".\n");  /* Get out of data mode */
				return (do_quit (cb));
            }
        }
        /* Send the file. This also closes it */
        smtpsendfile(cb);
  
        /* Wait for the OK response */
        rcode = getresp(cb,200);
        if(rcode == -1)
			return (do_quit (cb));
        if((rcode >= 400) && (rcode < 500))    /* temp failure? */
            check_qtime(cb);
        if((rcode >= 200 && rcode < 300) || cb->errlog != NULLLIST){
        /* if a good transfer or permanent failure [or too long in mqueue] remove job */
            if (cb->errlog != NULLLIST)
                retmail(cb);
            /* Unlink the textfile */
            unlink(cb->tname);
            unlink(cb->wname);   /* unlink workfile */

#ifdef	WPAGES
		/* 28Oct2019, Maiko, Kludge to note WP processing ? */
			if (!strnicmp (cb->jobq->to->val, "wp@", 3))
				smSentIdx = 1;
			else
				smSentIdx = 0;

            log(cb->s,"SMTP %s job %s To: %s From: %s", smSentJob[smSentIdx],
            	cb->jobq->jobname,cb->jobq->to->val,cb->jobq->from);
#else
            log(cb->s,"SMTP sent job %s To: %s From: %s",
            cb->jobq->jobname,cb->jobq->to->val,cb->jobq->from);
#endif
        }
    } while(next_job(cb));

    sendcmd(cb,"QUIT\n");
    check_qtime(cb);

    if (cb->errlog != NULLLIST)
	{
        retmail(cb);
        unlink(cb->wname);   /* unlink workfile */
        unlink(cb->tname);   /* unlink text */
    }
    close_s(cb->s);
#ifdef STATUSWIN
    SmtpUsers--;
#endif
    if(cb->tfile != NULLFILE)
        fclose(cb->tfile);
    cb->lock = 0;
    del_session(cb);
}
  
/* check if msg stayed too long in the mqueue */
static void
check_qtime(cb)
register struct smtpcli *cb;
{
    struct stat tstat;
    time_t now;
    char tmp[80];


    if(cb == NULLSMTPCLI || cb->jobq == NULLJOB)
        return;

    if(Sdtimer && cb->errlog == NULLLIST) {
        time(&now);
        if(cb->tfile == NULLFILE)
            if((cb->tfile = fopen(cb->tname,READ_TEXT)) == NULLFILE)
                return;

        fstat(fileno(cb->tfile),&tstat);
        fclose(cb->tfile);
        cb->tfile = NULLFILE;
        if((now - tstat.st_ctime) > (Sdtimer * 3600L)) { 
            sprintf(tmp," >>> Your message could not be delivered for %d hour(s); giving up!",Sdtimer);
            logerr(cb,tmp);
        }
    }
    return;
}

/* free the message struct and data */
static void
del_session(cb)
register struct smtpcli *cb;
{
    register struct smtp_job *jp,*tp;
    register int i;

    if (cb == NULLSMTPCLI)
        return;
    for(i=0; i<MAXSESSIONS; i++)
        if(cli_session[i] == cb) {
            cli_session[i] = NULLSMTPCLI;
            break;
        }

    free(cb->wname);
    free(cb->tname);
    free(cb->destname);
#ifdef J2_MULTI_SMTP_SESSION
    free(cb->prefix);
#endif
    for (jp = cb->jobq; jp != NULLJOB;jp = tp) {
        tp = jp->next;
        del_job(jp);
    }
    del_list(cb->errlog);
    free((char *)cb);
    Smtpsessions--; /* number of connections active */
}

static void
del_job(jp)
register struct smtp_job *jp;
{
    if ( *jp->jobname != '\0')
        rmlock(Mailqdir,jp->jobname);
    free(jp->from);
    del_list(jp->to);
    free((char *)jp);
}

/* delete a list of list structs */
void
del_list(lp)
struct list *lp;
{
    register struct list *tp, *tp1;
    for (tp = lp; tp != NULLLIST; tp = tp1) {
        tp1 = tp->next;
        free(tp->val);
        free(tp->aux);
        free((char *)tp);
    }
}

/* stub for calling mdaemon to return message to sender */
static void
retmail(cb)
struct smtpcli *cb;
{
    FILE *infile;
#ifdef SMTPTRACE
    if (Smtptrace > 5) {
        printf("smtp job %s returned to sender\n",cb->wname);
    }
#endif
    if ((infile = fopen(cb->tname,READ_TEXT)) == NULLFILE)
        return;
    mdaemon(infile,cb->jobq->from,cb->errlog,1);
    fclose(infile);
}

/* look to see if a smtp control block exists for this ipdest */
static struct smtpcli *
#ifdef J2_MULTI_SMTP_SESSION
lookup (int32 destaddr, char *prefix)
#else
lookup(destaddr)
int32 destaddr;
#endif
{
    register int i;

    for(i=0; i<MAXSESSIONS; i++) {
        if (cli_session[i] == NULLSMTPCLI)
            continue;
#ifdef J2_MULTI_SMTP_SESSION
        if(cli_session[i]->ipdest == destaddr && !strcmp (cli_session[i]->prefix, prefix))
#else
        if(cli_session[i]->ipdest == destaddr)
#endif
            return cli_session[i];
    }
    return NULLSMTPCLI;
}
  
/* create a new  smtp control block */
static struct smtpcli *
newcb()
{
    register int i;
    register struct smtpcli *cb;

    /* Are there enough processes running already? */
    if (Smtpsessions >= Smtpmaxcli) {
#ifdef SMTPTRACE
        if (Smtptrace>1) {
            printf("smtp daemon: too many processes\n");
        }
#endif
        return NULLSMTPCLI;
    }
    for(i=0; i<MAXSESSIONS; i++) {
        if(cli_session[i] == NULLSMTPCLI) {
            cb = (struct smtpcli *)callocw(1,sizeof(struct smtpcli));
            cb->wname = mallocw((unsigned)strlen(Mailqdir)+JOBNAME);
            cb->tname = mallocw((unsigned)strlen(Mailqdir)+JOBNAME);
            cli_session[i] = cb;
            Smtpsessions++; /* number of connections active */
            return(cb);
        }
    }
    return NULLSMTPCLI;
}

static void
execjobs()
{
    register struct smtpcli *cb;
    register int i, insave, outsave;

    for(i=0; i<MAXSESSIONS; i++) {
        cb = cli_session[i];
        if (cb == NULLSMTPCLI)
            continue;
        if(cb->lock)
            continue;

        sprintf(cb->tname,"%s/%s.txt",Mailqdir,cb->jobq->jobname);
        sprintf(cb->wname,"%s/%s.wrk",Mailqdir,cb->jobq->jobname);
  
        /* This solves the nasty hack in mailbox.c, from Mark ve3dte */
        insave = Curproc->input;
        outsave = Curproc->output;
        Curproc->input = -1;
        Curproc->output = -1;
        /* Now we can call newproc with null parent sockets! */
        newproc("smtp_send", 1536, smtp_send, 0, cb,NULL,0);
        /* now restore parent sockets so parent can continue */
        Curproc->input = insave;
        Curproc->output = outsave;
  
#ifdef SMTPTRACE
        if (Smtptrace>1)
            printf("Trying Connection to %s\n",inet_ntoa(cb->ipdest));
#endif

    }
}
  
/* add this job to control block queue */
static struct smtp_job *
setupjob(cb,id,from)
struct smtpcli *cb;
char *id,*from;
{
    register struct smtp_job *p1,*p2;

    p1 = (struct smtp_job *)callocw(1,sizeof(struct smtp_job));
    p1->from = j2strdup(from);
    strcpy(p1->jobname,id);
/* now add to end of jobq */
    if ((p2 = cb->jobq) == NULLJOB)
        cb->jobq = p1;
    else {
        while(p2->next != NULLJOB)
            p2 = p2->next;
        p2->next = p1;
    }
    return p1;
}
  
/* called to advance to the next job */
static int
next_job(cb)
register struct smtpcli *cb;
{
    register struct smtp_job *jp;

    jp = cb->jobq->next;
    del_job(cb->jobq);
/* remove the error log of previous message */
    del_list(cb->errlog);
    cb->errlog = NULLLIST;
    cb->jobq = jp;
    if (jp == NULLJOB)
        return 0;
    sprintf(cb->tname,"%s/%s.txt",Mailqdir,jp->jobname);
    sprintf(cb->wname,"%s/%s.wrk",Mailqdir,jp->jobname);
#ifdef SMTPTRACE
    if (Smtptrace > 5) {
        printf("sending job %s\n",jp->jobname);
    }
#endif
    return 1;

}
  
  
/* Mail routing function. */
int32
mailroute(dest)
char *dest;
{
    int32 destaddr = 0;
    int32 a;

#ifdef HOPPER
    struct route *rp;
#endif
  
#ifdef SMTPTRACE
    if (Smtptrace > 6)
        printf("Route lookup for = %s\n",dest);
#endif

    if(!*dest || ((a = resolve(dest))!=0 && ismyaddr(a))) {
#ifdef SMTPTRACE
        if (Smtptrace > 6)
            printf("Local mail\n");
#endif
        return Ip_addr;
    }

    /* look up address or use the gateway */
    if(UseMX){
#ifdef SMTP_A_BEFORE_WILDMX
        destaddr = resolve_mx(dest, a);
#else
        destaddr = resolve_mx(dest);
#endif
#ifdef SMTPTRACE
        if (Smtptrace > 6)
            printf("MX lookup returned = %s\n",inet_ntoa(destaddr));
#endif
    }

/* 04Dec2014, If dest addr is BAD, then why the heck should we gateway it ? */
    if(destaddr == 0)
#ifdef	OKAY_TO_GATEWAY_BADADDR
        if((destaddr = a) == 0)
            if (Gateway.ipaddr != 0)
                destaddr = Gateway.ipaddr; /* Use the gateway  */
#else
        destaddr = a;
#endif

#ifdef SMTPTRACE
    if (Smtptrace > 6)
        printf("Address resolver returned = %s\n",inet_ntoa(destaddr));
#endif

#ifdef HOPPER
    if (UseHopper && (destaddr != Ip_addr)) {
        if ((rp=rt_lookup(destaddr)) != NULLROUTE)
            if (rp->gateway != 0)
                destaddr=rp->gateway;
#ifdef SMTPTRACE
        if (Smtptrace > 6)
            printf("Hopper returned = %s\n",inet_ntoa(destaddr));
#endif
    }
#endif

#ifdef SMTPTRACE
    if (Smtptrace > 6)
        printf("Mailroute returned = %s\n",inet_ntoa(destaddr));
#endif
    return destaddr;

}
  
/* save line in error list */
static void
logerr(cb,line)
struct smtpcli *cb;
char *line;
{
    register struct list *lp,*tp;
    tp = (struct list *)callocw(1,sizeof(struct list));
    tp->val = j2strdup(line);
    /* find end of list */
    if ((lp = cb->errlog) == NULLLIST)
        cb->errlog = tp;
    else {
        while(lp->next != NULLLIST)
            lp = lp->next;
        lp->next = tp;
    }
}

static int
smtpsendfile(cb)
register struct smtpcli *cb;
{
    int error = 0, continued = FALSE;

    while(fgets(cb->buf,sizeof(cb->buf),cb->tfile) != NULLCHAR) {
    /* Escape a single '.' character at the beginning of a line */
        if(!continued && *(cb->buf) == '.')
            usputc(cb->s,'.');
        continued = (strchr(cb->buf,'\n') == NULLCHAR);
        usputs(cb->s,cb->buf);
    }
    fclose(cb->tfile);
    cb->tfile = NULLFILE;
    /* Send the end-of-message command */
    if(continued)
        sendcmd(cb,"\n.\n");
    else
        sendcmd(cb,".\n");
    return error;
}

/* do a printf() on the socket with optional local tracing */
#ifdef  ANSIPROTO
static void
sendcmd(struct smtpcli *cb,char *fmt, ...)
{
    va_list args;

    va_start(args,fmt);
#ifdef  SMTPTRACE
    if(Smtptrace>1){
        printf("smtp sent: ");
        vprintf(fmt,args);
    }
#endif
    vsprintf(cb->buf,fmt,args);
    usputs(cb->s,cb->buf);
    va_end(args);
}
#else
static void
sendcmd(cb,fmt,arg1,arg2,arg3,arg4)
struct smtpcli *cb;
char *fmt;
int arg1,arg2,arg3,arg4;
{
#ifdef  SMTPTRACE
    if(Smtptrace>1){
        printf("smtp sent: ");
        printf(fmt,arg1,arg2,arg3,arg4);
    }
#endif
    sprintf(cb->buf,fmt,arg1,arg2,arg3,arg4);
    usputs(cb->s,cb->buf);
}
#endif
  
/* Wait for, read and display response from server. Return the result code. */
static int
getresp(cb,mincode)
struct smtpcli *cb;
int mincode;    /* Keep reading until at least this code comes back */
{
    int rval;
    char line[LINELEN];
    /*
	struct sockaddr_in fsocket;
	int i;
	*/

    usflush(cb->s);
    for(;;){
    /* Get line */
        j2alarm(Smtpt4 * 1000);   /* set a timeout */
        if(recvline(cb->s,line,LINELEN) == -1){
            j2alarm(0);   /* reset a timeout */
			log (cb->s, "outgoing SMTP failed, timeout"); /* 09Feb2017 */
            rval = -1;
            break;
        }
        j2alarm(0);   /* reset a timeout */
        rip(line);      /* Remove cr/lf */
        rval = atoi(line);
#ifdef  SMTPTRACE
        if(Smtptrace>1)
            printf(smtp_recv,line);/* Display to user */
#endif
        if(rval >= 500) {   /* Save permanent error replies */
            char tmp[LINELEN+5];	/* 01Oct2019, Maiko, compiler format overflow warning, so added +5 bytes */
            if(cb->errlog == NULLLIST) {
			/*
			 * 19May2015, Maiko (VE4KLM), this should reflect the mail server
			 * we are actually talking to - NOT the recipients domain name !
			 *
                sprintf(tmp,"While talking to %s:", cb->destname);
			 *
			 * 06Jun2015, Maiko (VE4KLM), blast ! peername not working here
			 *
    			if (j2getpeername (cb->s, (char*)&fsocket, &i) != -1)
				{
					sprintf (tmp, "While talking to %s:",
						inet_ntoa (fsocket.sin_addr.s_addr));
				}
			 */
                sprintf(tmp,"While talking to %s:", inet_ntoa (cb->ipdest));

                logerr(cb,tmp);
            }
            if(cb->buf[0] != '\0') { /* Save offending command */
                rip(cb->buf);
                sprintf(tmp,">>> %s",cb->buf);
                logerr(cb,tmp);
                cb->buf[0] = '\0';
            }
            sprintf(tmp,"<<< %s",line);
            logerr(cb,tmp);     /* save the error reply */
        }
        /* Messages with dashes are continued */
        if(line[3] != '-' && rval >= mincode)
            break;
    }
    return rval;
}
