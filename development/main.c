/* Main-level network program:
 * initialization
 * keyboard processing
 * generic user commands
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <time.h>
#if defined(__TURBOC__) && defined(MSDOS)
#include <fcntl.h>
#include <dos.h>
#include <io.h>
#include <conio.h>
#include <ctype.h>
#include <dir.h>
#include <alloc.h>
#endif
#ifdef UNIX
#include <fcntl.h>
#include <signal.h>
#endif
#include "global.h"
#ifdef  ANSIPROTO
#include <stdarg.h>
#endif
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "ax25.h"
#include "kiss.h"
#include "enet.h"
#include "netrom.h"
#include "ftpcli.h"
#include "telnet.h"
#include "tty.h"
#include "session.h"
#include "unix.h"
#include "bm.h"
#include "usock.h"
#include "socket.h"
#ifdef LZW
#include "lzw.h"
#endif
#include "cmdparse.h"
#include "commands.h"
#include "daemon.h"
#include "devparam.h"
#include "domain.h"
#include "files.h"
#include "main.h"
#include "mailbox.h"
#include "remote.h"
#include "trace.h"
#ifdef fprintf
#undef fprintf
#endif
#include "mailutil.h"
#include "smtp.h"
#include "index.h"
#ifdef XMS
#include "xms.h"
#endif
#ifdef EMS
#include "memlib.h"
#endif
#ifdef UNIX
#include "sessmgr.h"
#endif

#include "jlocks.h"

#ifdef	IPV6
#include "sockaddr.h"	/* 31Mar2023, Maiko, Not sure why I need this */
#endif

#undef BETA
  
#ifdef UNIX
/*#define BETA 1*/
#endif
#ifdef BSAHAX
#define BETA 1
#endif
  
#if defined(__TURBOC__) || (defined __BORLANDC__)
/* Don't even think about changing the following #pragma :-) - WG7J */
#pragma option -a-
  
/* The following is from the Borland Runtime Library Version 3.0 :
 * Copyright (c) 1987, 1990 by Borland International
 */
typedef struct
{
    unsigned char windowx1;
    unsigned char windowy1;
    unsigned char windowx2;
    unsigned char windowy2;
    unsigned char attribute;
    unsigned char normattr;
    unsigned char currmode;
    unsigned char screenheight;
    unsigned char screenwidth;
    unsigned char graphicsmode;
    unsigned char snow;
    union {
        char far * p;
        struct { unsigned off,seg; } u;
    } displayptr;
} VIDEOREC;
extern VIDEOREC _video;
  
char *Screen_Address(void){
    /* Might have to add some code to get the address of the virtualized
     * DV screen here, if we go that route.
     */
    return _video.displayptr.p;
}
#pragma option -a
  
#endif /* TURBOC || BORLANDC */
  
extern struct cmds DFAR Cmds[],DFAR Startcmds[],DFAR Stopcmds[],DFAR Attab[];
  
#if     (!defined(MSDOS) || defined(ESCAPE))    /* PC uses F-10 key always */
static char Escape = 0x1d;      /* default escape character is ^] */
#endif
  
#ifdef __TURBOC__
int dofstat __ARGS((void));
#endif
static char Prompt[] = "jnos> ";
char NoRead[] = "Can't read %s: %s\n";
char Badhost[] = "Unknown host %s\n";
char Badinterface[] = "Interface \"%s\" unknown\n";
char Existingiface[] = "Interface %s already exists\n";
char Nospace[] = "No space!!\n";    /* Generic malloc fail message */
char Nosversion[] = "JNOS version %s\n";
#ifdef MSDOS
char NosLoadInfo[] = "NOS load info: CS=0x%04x DS=0x%04x";
#endif
char Noperm[] = "Permission denied.\n";
char Nosock[] = "Can't create socket\n";
char SysopBusy[] = "The sysop is unavailable just now. Try again later.\n";
char TelnetMorePrompt[] = "--more--";
char BbsMorePrompt[] = "More(N=no)? ";
char *Hostname;
char *Motd;                     /* Message Of The Day */
#if !defined(MAILBOX) || defined(TTYLINKSERVER) || defined(TTYCALL)
int Attended = TRUE;            /* default to attended mode */
#else
int Attended;
#endif
int ThirdParty = TRUE;                  /* Allows 3rd party mail by default */
int main_exit;          /* from main program (flag) */
int DosPrompt;          /* Use a dos-like prompt */
int Mprunning;          /* flag for other parts (domain) to signal
                         * that we are fully configured running.
                         */
long UtcOffset;         /* UTC - localtime, in secs */

static int ErrorPause;
static int Step;
static int RmLocks = 1;
  
extern int StLen2;
extern char *StBuf2;
extern char *StBuf3;
#ifdef MAILBOX
#define MAXSTATUSLINES 3
#else
#define MAXSTATUSLINES 2
#endif
#ifdef STATUSWIN
int StatusLines = MAXSTATUSLINES;
#else
int StatusLines = 0;
#endif
  
struct proc *Cmdpp;
#ifndef UNIX
struct proc *Display;
#endif
#ifdef  LZW
int Lzwmode = LZWCOMPACT;
int16 Lzwbits = LZWBITS;
#endif
  
#ifdef TRACE
int Tracesession = 1;
struct session *Trace = NULLSESSION;
int TraceColor = 0;    /* PE1DGZ */
int stdoutSock = -1;
#endif
  
#if defined(ALLCMD) && defined(DOMDUMP)
static char *DumpAddr = NULL;           /* Memory dump pointer */
#endif

time_t StartTime;        /* Time that NOS was started */
static int Verbose;
  
extern void assign_filenames __ARGS((char *));
static void logcmd __ARGS((char *));
/* char *Screen_Address(void); */
  
  
int Numrows,Numcols;    /* screen size at startup - WG7J */
struct hist *Histry;    /* command recall stuff */
int Histrysize;
int Maxhistory = 10;
  
#ifdef MSDOS
#ifdef EMS
int EMS_Available;
#endif
#ifdef XMS
int XMS_Available;
#endif
#endif /* MSDOS */
  
#ifdef UNIX
static char **origargv;
int coredumpok=0;
#endif
  
#ifdef MSDOS
/* The text_info before we start JNOS */
struct text_info PrevTi;
#endif
#ifdef STATUSWIN
extern char MainStColors,SesStColors;
#endif
#ifdef SPLITSCREEN
extern char MainColors,SplitColors;
void GiveColor(char *s,char *attr);
#endif
  
#if defined(NNTP) || defined(NNTPS)
extern void RemoveNntpLocks(void);
#endif

#if defined(MEMLOG) && defined(__TURBOC__)
static FILE  *MemRecFp;
extern int memlogger;   /* see alloc.c */
#endif

/* 03Oct2005, Maiko, Functions dependent on config.h should not be here */
extern void jnos2_args (int, char*), jnos2_inits (void);

extern int isboolcmd (int *var, char *cmdstr);	/* 15Apr2016 (VE4KLM), fix
						 warnings, cmdparse.c */
int main(int argc,char *argv[])
{
    int ForceIndex = 0;
    char *inbuf,*intmp;
    FILE *fp;
    struct mbuf *bp;
    struct tm *tm;
    int c;
#ifdef UNIX
	int no_itimer;
#ifdef DOTNOSRC
	int did_init;
#endif
    char *trace_sm = 0, *trace_smopt = 0;
    char *def_sm = 0, *def_smopt = 0;
    static int oops;
#endif
    struct cur_dirs dirs;

#ifdef MSDOS
#ifdef EMS
    unsigned long emssize;
#endif
#ifdef XMS
    long XMS_Ret;
#endif
  
#if defined(MEMLOG) && defined(__TURBOC__)
     if ((MemRecFp = fopen("MEMLOG.DAT", "wb")) != NULLFILE)
         memlogger = fileno(MemRecFp);
#endif
#endif /* MSDOS */

#ifdef UNIX
    if (oops++)
    {
#ifdef HEADLESS
    deinit_sys();
#else
        iostop();
#endif
        fprintf(stderr, "NOS PANIC: NOS main re-entered.\n");
        fflush(stderr);
        fflush(stdout);
        kill(getpid(), 11);
    }
    origargv = argv;
#endif
  
    time(&StartTime);           /* NOS Start_Up time */
    /* Adjust timezone if dst is in effect, to yield UtcOffset */
    tm = localtime(&StartTime);
    UtcOffset = timezone - (tm->tm_isdst>0 ? 3600L : 0L);  /* UTC - localtime, in secs */

	log (-1, "UtcOffset set to %ld", UtcOffset);

#ifdef UNIX
    SRANDOM((getpid() << 16) ^ time((long *) 0));
#else
    randomize();
#endif
  
#if defined(__TURBOC__) || defined(__BORLANDC__)
    /* Borland's library calls int10. Some vga mode utilities do not
     * report the screen sizes correctly into the internal _video structure.
     * This can cause the screen size to be faulty in the gettextinfo call.
     * Instead, read the BIOS data area to get the correct screen info,
     * and update the _video structure for later calls to
     * gettextinfo(), clrscr(), etc... - WG7J
     *
     * If this doesn't work, you can now overwrite the values with
     * the -r and -c command line options - WG7J
     */
    Numrows = (int) *(char far *)MK_FP(0x40,0x84) + 1;
    gettextinfo(&PrevTi);
    Numcols = PrevTi.screenwidth;
#ifdef SPLITSCREEN
    MainColors = PrevTi.attribute;
    MainColors &= 0xf7; /* turn off highlite */
#endif
/*
    Numrows = PrevTi.screenheight;
*/
    if(Numrows == 1)
        Numrows = NROWS;  /* use value from config.h */
  
#endif
  
#ifdef MSDOS
    SwapMode = MEM_SWAP;
    Screen = Screen_Address();
  
    /* If EMS is found, XMS is not used for swapping - WG7J */
#ifdef EMS
    if(ememavl(&emssize) == 0) {
        EMS_Available = 1;
        SwapMode = EMS_SWAP;
    }
#endif
  
#ifdef XMS
    if((XMS_Available = Installed_XMS()) != 0)
#ifdef EMS
        if(!EMS_Available)
#endif
            SwapMode = XMS_SWAP;
#endif
  
#endif /* MSDOS */

    Hostname = j2strdup("unknown");  /* better than (null) */

#ifdef UNIX
    no_itimer = 0;
#ifdef DOTNOSRC
    did_init = 0;
#endif
    while((c = j2getopt(argc,argv,"a:d:f:g:R:u:w:x:y:z:nveltilCDS:T:")) != EOF){
#else
    while((c = j2getopt(argc,argv,"a:g:d:r:R:c:f:m:u:w:x:y:z:nbveltiI")) != EOF){
#endif
        switch(c)
		{

	/* 03Oct2005, Maiko, Init's dependent on config.h in external function */
			case 'a':
			case 'R':
				jnos2_args (c, j2optarg);
				break;

#ifdef  __TURBOC__
            case 'b':       /* Use BIOS for screen output */
                directvideo = 0;
                break;
#endif
            case 'd':   /* Root directory for various files */
                initroot(j2optarg);
                break;
            case 'e':   /* Pause after error lines */
                ErrorPause = 1;
                break;
#if defined(MSDOS) || defined(UNIX)
            case 'f':
                assign_filenames(j2optarg);
#if defined(UNIX) && defined(DOTNOSRC)
                did_init = 1;
#endif
                break;
#endif
#if defined(TRACE)
            case 'g':   /* PE1DGZ: trace color (0=>none, 1=>mono, 2=>color) */
                TraceColor = atoi(j2optarg);
                break;
            case 'n':
                Tracesession = 0; /* No session for tracing */
                break;
#endif /* TRACE */
            case 'i':
                ForceIndex = 1;
                break;
            case 'I':
                ForceIndex = 2;  /* k5jb: -I => don't check indices */
                break;
            case 'l':   /* Don't remove mail locks */
                RmLocks = 0;
                break;
#ifdef MSDOS
            case 'm':   /* Swap mode, 0=EMS (default),1=XMS,2=MEM,3=FILE - WG7J */
                i = atoi(j2optarg);
#ifdef XMS
                if(i == 1 && XMS_Available) {
                    SwapMode = XMS_SWAP;
                } else
#endif
                    if(i == 2)
                        SwapMode = MEM_SWAP;
                    else if(i == 3)
                        SwapMode = FILE_SWAP;
                break;
#endif

#ifndef UNIX
            case 'r':   /* Number of rows on screen */
                Numrows = atoi(j2optarg);
                break;
            case 'c':   /* Number of columns on screen */
                Numcols = atoi(j2optarg);
                break;
#endif /* !UNIX */
            case 't':
                Step = 1;
            /* note fallthrough that engages -v too */
            case 'v':
                Verbose = 1;
                break;

#ifdef STATUSWIN
            case 'u':   /* No status lines */
                StatusLines = atoi(j2optarg);
                if(StatusLines > MAXSTATUSLINES)
                    StatusLines = MAXSTATUSLINES;
                break;
        /* Color options - WG7J */
            case 'w':
                GiveColor(j2optarg,&MainStColors);
                MainStColors |= 0x08; /* Turn on highlite, so it shows on b/w */
                break;
            case 'x':
                GiveColor(j2optarg,&SesStColors);
                SesStColors |= 0x08; /* Turn on highlite, so it shows on b/w */
                break;
#endif
#ifdef SPLITSCREEN
            case 'y':
                GiveColor(j2optarg,&MainColors);
                MainColors &= 0xf7; /* Turn off highlite, so high video shows! */
                textattr(MainColors);   /* Set the color for startup screen ! */
                break;
            case 'z':
                GiveColor(j2optarg,&SplitColors);
                SplitColors |= 0x08;    /* Turn on higlite, so it shows on b/w */
                break;
#endif

#ifdef UNIX
            case 'C':
                coredumpok = 1;  /* for debugging */
                break;

            case 'D':
                no_itimer = 1;	/* for debugging */
                break;
#ifndef HEADLESS
            case 'S':
                if (sm_lookup(j2optarg, &def_smopt))
                    def_sm = j2optarg;
                else
                    printf("No session manager \"%s\", using default\n", j2optarg);
                break;
            case 'T':
                if (sm_lookup(j2optarg, &trace_smopt))
                    trace_sm = j2optarg;
                else
                    printf("Session manager for trace not found, using default\n");
                break;
#endif
#endif
        }
    }

	/* 03Oct2005, Maiko, Init's dependent on config.h in external function */
	jnos2_inits ();

#if defined(UNIX) && defined(DOTNOSRC)
    if (!did_init)
	assign_filenames(".nosrc");
#endif

#ifndef UNIX
#if defined(__TURBOC__) || defined(__BORLANDC__)
    /* Set the internal structure, in case there was a command
     * line overwrite - WG7J
     */
    _video.screenheight = (unsigned char)Numrows;
    _video.windowx2 = (unsigned char)(Numcols - 1);
    _video.windowy2 = (unsigned char)(Numrows - 1);
#endif
    ScreenSize = 2 * Numrows * Numcols;
#endif /* !UNIX */  
  
#ifdef STATUSWIN
#ifdef UNIX
    Numcols = 132;  /* reasonable max value? We could query terminfo DB...*/
#endif

    if(StatusLines > 1) {
#ifdef MAILBOX
        StBuf2 = mallocw(Numcols+3);
        StLen2 = sprintf(StBuf2,"\r\nBBS:");
        if(StatusLines > 2)
#endif
        {
            StBuf3 = mallocw(Numcols+3);
            sprintf(StBuf3,"\r\n");
        }
    }
#endif
  
#ifdef MSDOS
#ifdef XMS
    if(XMS_Available)
        /* Calculate space in kb for screen */
        ScreenSizeK = (ScreenSize / 1024) + 1;
#endif
#endif
  
  
    kinit();
    ipinit();
#ifdef UNIX
#ifdef HEADLESS
    init_sys(no_itimer);
#else
    ioinit(no_itimer);
#endif
#else
    ioinit();
#endif
    Cmdpp = mainproc("cmdintrp");
  
    Sessions = (struct session *)callocw(Nsessions,sizeof(struct session));
#ifdef TRACE
    /* Map stdout to a socket so that trace code works properly - WG7J */
    if((stdoutSock = stdoutsockfopen()) == -1)
        j2tputs("Error: stdout socket can not be opened!");

    if(Tracesession)
#ifdef UNIX
    {
        if (!trace_sm)
            trace_sm = Trace_sessmgr;
        Trace = sm_newsession(trace_sm, NULLCHAR, TRACESESSION, 0);
    }
#else
        Trace = newsession(NULLCHAR,TRACESESSION,0);
#endif
#endif /* TRACE */

#ifdef UNIX
    if (!def_sm)
        def_sm = Command_sessmgr;
    Command = Lastcurr = sm_newsession(def_sm, NULLCHAR, COMMAND, 0);
#else
    Command = Lastcurr = newsession(NULLCHAR,COMMAND,0);
#endif
    init_dirs(&dirs);
    Command->curdirs=&dirs;
  
#ifndef UNIX
    Display = newproc("display",250,display,0,NULLCHAR,NULL,0);
#endif
    tprintf(Nosversion,Version);
    j2tputs(Version2);
    j2tputs("Copyright 1991 by Phil Karn (KA9Q) and contributors.\n");
#ifdef MSDOS
#ifdef EMS
    if(EMS_Available)
        j2tputs("EMS detected.\n");
#endif
#ifdef XMS
    if(XMS_Available)
        j2tputs("XMS detected.\n");
#endif
#endif
  
#ifdef BETA
    j2tputs("\007\007\007\n==> This is a BETA version; be warned ! <==\n\n");
#endif
 
#ifndef HEADLESS 
    rflush();
#endif
  
    /* Start background Daemons */
    {
        struct daemon *tp;
  
        for(tp=Daemons;;tp++){
            if(tp->name == NULLCHAR)
                break;
            newproc(tp->name,tp->stksize,tp->fp,0,NULLCHAR,NULL,0);
        }
    }
  
    if(j2optind < argc){
        /* Read startup file named on command line */
        if((fp = fopen(argv[j2optind],READ_TEXT)) == NULLFILE)
            tprintf(NoRead,argv[j2optind],strerror(errno));
    } else {
        /* Read default startup file named in files.c (autoexec.nos) */
        if((fp = fopen(Startup,READ_TEXT)) == NULLFILE)
            tprintf(NoRead,Startup,strerror(errno));
    }
    if(fp != NULLFILE){
        inbuf = mallocw(BUFSIZ);
        intmp = mallocw(BUFSIZ);
        while(fgets(inbuf,BUFSIZ,fp) != NULLCHAR){
            strcpy(intmp,inbuf);
            if(Verbose){
                j2tputs(intmp);
#ifndef HEADLESS 
                rflush();
#endif
            }
            /* Are we stepping through autoexec.nos ? - WG7J */
            if(Step) {
                int c;
                Command->ttystate.edit = Command->ttystate.echo = 0;
                c = toupper(keywait("Execute cmd ?",1));
                Command->ttystate.edit = Command->ttystate.echo = 1;
                if(c != 'Y')
                    continue;
            }
            if(cmdparse(Cmds,inbuf,NULL) != 0){
                tprintf("input line: %s",intmp);
                if(ErrorPause)
                    keywait(NULLCHAR,1);
#ifndef HEADLESS 
                rflush();
#endif
            }
        }
        fclose(fp);
        free(inbuf);
        free(intmp);
    }
  
    /* Log that we started nos, but do this after the config file is read
     * such that logging can be disabled - WG7J
     */
#ifdef WHOFOR
    log(-1,"JNOS %s (%.24s) was started", Version, WHOFOR);
#else
    log(-1,"JNOS %s was started", Version);
#endif
  
    /* Update .txt files that have old index files - WG7J (unless bypassed by -I) */
    if (ForceIndex != 2)
        UpdateIndex(NULL,ForceIndex);
  
    if(RmLocks) {
        RemoveMailLocks();
#if defined(NNTP) || defined(NNTPS)
        RemoveNntpLocks();
#endif
#ifdef MBFWD
        (void)rmlock(Historyfile,NULLCHAR);
#endif
    }
  
    Mprunning = 1;  /* we are on speed now */
  
    /* Now loop forever, processing commands */
    for(;;){
        if(DosPrompt)
            tprintf("%s ",dirs.dir);
#ifdef DONT_COMPILE
        if(!StatusLines)
            tprintf("%lu ",farcoreleft());
#endif
        j2tputs(Prompt);
        usflush(Command->output);
        if(recv_mbuf(Command->input,&bp,0,NULLCHAR,0) != -1)
		{
            logcmd(bp->data);

#ifdef	JNOS_LOG_CONSOLE
	/* 14Apr2015, Maiko (VE4KLM), Log all console (F10) commands */
			if (*(bp->data))
            	log (-2, bp->data);
#endif
            (void)cmdparse(Cmds,bp->data,Lastcurr);
            free_p(bp);
        }
    }
}

#ifndef HEADLESS
/* Keyboard input process */
/* Modified to support F-key session switching,
 * from the WNOS3 sources - WG7J
 */
void
keyboard(i,v1,v2)
int i;
void *v1;
void *v2;
{
    int c;
    struct mbuf *bp;
    int j,k;
    struct session *sp;
#ifdef STATUSWIN
    int newsession;
#endif
  
    /* Keyboard process loop */
    for(;;){
        c = kbread();
#ifdef STATUSWIN
        newsession = 0;
#endif
#if(!defined(MSDOS) || defined(ESCAPE))
        if(c == Escape && Escape != 0)
            c = -2;
#endif
        if(c == -2 && Current != Command){
            /* Save current tty mode and set cooked */
            Lastcurr = Current;
            Current = Command;
            swapscreen(Lastcurr,Current);
#ifdef STATUSWIN
            newsession = 1;
#endif
        }
        if((c < -2) && (c > -12)) {             /* F1 to F9 pressed */
#ifdef TRACE
            /* If F9 is pressed, -11 is returned and we swap to Trace - WG7J */
            if(c == -11 && Tracesession) {
                if(Current != Trace) {
                    /* Save current tty mode and set cooked */
                    swapscreen(Current,Trace);
                    Lastcurr = Current;
                    Current = Trace;
#ifdef STATUSWIN
                    newsession = 1;
#endif
                } else {
                    /* Toggle back to previous session */
                    Current = Lastcurr;
                    Lastcurr = Trace;
                    swapscreen(Lastcurr,Current);
#ifdef STATUSWIN
                    newsession = 1;
#endif
                }
            } else {
#endif
                /* Swap directly to F-key session - WG7J */
                k = (-1 * c) - 2;
                for(sp = Sessions, j = 0; sp < &Sessions[Nsessions]; sp++) {
                    if(sp->type == COMMAND)
                        continue;
                    j++;
                    if(sp->type != FREE && j == k) {
                        Lastcurr = Current;
                        Current = sp;
                        swapscreen(Lastcurr,Current);
#ifdef STATUSWIN
                        newsession = 1;
#endif
                        break;
                    }
                }
#ifdef TRACE
            }
#endif
        }
#ifdef STATUSWIN
        if(newsession)
            UpdateStatus();
#endif
        Current->row = Numrows - 1 - StatusLines;
#ifdef SPLITSCREEN
        if (Current->split)
            Current->row -= 2;
#endif
        j2psignal(&Current->row,1);
        if(c >= 0){
#ifdef UNIX
            if (Current->morewait) /* end display pause, if any */
                Current->morewait = 2;
#endif
            /* If the screen driver was in morewait state, this char
             * has woken him up. Toss it so it doesn't also get taken
             * as normal input. If the char was a command escape,
             * however, it will be accepted; this gives the user
             * a way out of lengthy output.
             */
            if(!Current->morewait && (bp = ttydriv(Current,c)) != NULLBUF)
                send_mbuf(Current->input,bp,0,NULLCHAR,0);
        }
    }
}

extern int Kblocked;
extern char *Kbpasswd;
#ifdef LOCK
  
/*Lock the keyboard*/
int
dolock(argc,argv,p)
int argc;
char *argv[];
void *p;
{
  
    extern char Noperm[];
  
    /*allow only keyboard users to access the lock command*/
    if(Curproc->input != Command->input) {
        j2tputs(Noperm);
        return 0;
    }
    if(argc == 1) {
        if(Kbpasswd == NULLCHAR)
            j2tputs("Set password first\n");
        else {
            Kblocked = 1;
            j2tputs("Keyboard locked\n");
            Command->ttystate.echo = 0; /* Turn input echoing off! */
        }
        return 0;
    }
    if(argc == 3) {
        if(*argv[1] == 'p') {   /*set the password*/
            free(Kbpasswd);             /* OK if already null */
            Kbpasswd = NULLCHAR;        /* reset the pointer */

            if(!strlen(argv[2]))
                return 0;           /* clearing the buffer */
            Kbpasswd = j2strdup(argv[2]);
            return 0;
        }
    }
  
    j2tputs("Usage: lock password \"<unlock password>\"\n"
    "or    'lock' to lock the keyboard\n");
  
    return 0;
}
  
#endif
#endif	/* HEADLESS */
  
/*this is also called from the remote-server for the 'exit' command - WG7J*/
void
where_outta_here(resetme,retcode)
int resetme, retcode;
{
    time_t StopTime;
    FILE *fp;
    char *inbuf,*intmp;
#if (defined(UNIX) || defined(EXITSTATS)) && defined(MAILBOX)
    extern int Totallogins;
    extern int BbsUsers;
    extern int DiffUsers;
#ifdef MAILCMDS
    extern int MbSent,MbRead,MbRecvd;
#ifdef MBFWD
    extern int MbForwarded;
#endif
#endif /* MAILCMDS */
#endif /* (UNIX||EXITSTATS) && MAILBOX */
  
    /* Execute sequence of commands taken from file "~/onexit.nos" */
    /* From iw0cnb */
    if((fp = fopen(Onexit,READ_TEXT)) != NULLFILE){
        inbuf = malloc(BUFSIZ);  /* n5knx: don't block forever here! */
        intmp = malloc(BUFSIZ);
        if (inbuf && intmp)
        while(fgets(inbuf,BUFSIZ,fp) != NULLCHAR){
            strcpy(intmp,inbuf);
            if(Verbose){
                j2tputs(intmp);
#ifndef HEADLESS
                rflush();
#endif
            }
            if(cmdparse(Cmds,inbuf,NULL) != 0){
                tprintf("input line: %s",intmp);
            }
        }
        fclose(fp);
        free(inbuf);
        free(intmp);
    }
    time(&StopTime);
    main_exit = TRUE;       /* let everyone know we're out of here */
    reset_all();
    if(Dfile_updater != NULLPROC)
        alert(Dfile_updater,0); /* don't wait for timeout */
    j2pause(3000);    /* Let it finish */
#ifdef TRACE
    shuttrace();
#endif
#ifdef WHOFOR
    log(-1,"JNOS %s (%.24s) was stopped (%d)", Version, WHOFOR, retcode);
#else
    log(-1,"JNOS %s was stopped (%d)", Version, retcode);
#endif
#ifdef UNIX
#ifdef MAILBOX
    /* 1.11d: robbed dombmailstats code to log stats at shutdown */ 
    log(-1,/* "Core: %lu, " */
    "Up: %s, "
    "Logins: %d, "
    "Users: %d, "
    "Count: %d",
    /* farcoreleft(), */ tformat(secclock()),Totallogins,BbsUsers,DiffUsers);
  
#ifdef MAILCMDS
#ifdef MBFWD
    log(-1,"Sent: %d, "
    "Read: %d, "
    "Rcvd: %d, "
    "Fwd: %d",
    MbSent,MbRead,MbRecvd,MbForwarded);
#else
    log(-1,"Sent: %d, "
    "Read: %d, "
    "Rcvd: %d", MbSent,MbRead,MbRecvd);
#endif
#endif /* MAILCMDS */
#endif /* MAILBOX */

    detach_all_asy();     /* make sure everything is unlocked */
    pwait(NULL);
#endif
#ifdef HEADLESS
    deinit_sys();
#else
    iostop();
#endif
#if defined(MEMLOG) && defined(__TURBOC__)
    if (memlogger) {
        memlogger=0;
        fclose(MemRecFp);
    }
#endif

    if(resetme)
#ifdef UNIX
    {
        if (fork() == 0)
            abort();
        execvp(origargv[0], origargv); /* re-run NOS */
    }
#else
    sysreset();
#endif

#ifdef MSDOS
#ifdef EMS
    effreeall();
#endif
#ifdef XMS
    /* Free any possible XMS used for the screen */
    Free_Screen_XMS();
#endif
    window(1,1,Numcols,Numrows);
    textattr(PrevTi.attribute);
    clrscr();
#endif /* MSDOS */

    exit(retcode);
}
  
int
doexit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int retcode = 0;
  
#if defined(MEMLOG) && defined(__TURBOC__)
    if (memlogger) {
        memlogger=0;
        fclose(MemRecFp);
    }
#endif

    if (argc == 2) retcode = atoi(argv[1]);

    if(strnicmp(Curproc->name, "at ",3) == 0)   /* From the AT command */
        where_outta_here(0, retcode);
  
    if(Curproc->input == Command->input) {  /* From keyboard */
        Command->ttystate.edit = Command->ttystate.echo = 0;
        if(toupper(keywait("Are you sure? ",0))=='Y') {
            tprintf("JNOS Exiting - Uptime => %s\n", tformat(secclock()));
            where_outta_here(0,retcode); /*No reset!*/
        }
        Command->ttystate.edit = Command->ttystate.echo = 1;
        return 0;
    }
   /* Anything else; probably mailbox-sysop */
    return -2;
}
  
extern char Chostname[];
  
int
dohostname(argc,argv,p)
int argc;
char *argv[];
void *p;
{
#ifdef CONVERS
    char *cp;
#endif
  
    if(argc < 2)
        tprintf("%s\n",Hostname);
    else {
        struct iface *ifp;
        char *name;
  
        if((ifp = if_lookup(argv[1])) != NULLIF){
            if((name = resolve_a(ifp->addr, FALSE)) == NULLCHAR){
                j2tputs("Interface address not resolved\n");
                return 1;
            } else {
                free(Hostname);           /* ok if already null */
                Hostname = name;
                tprintf("Hostname set to %s\n", name );
            }
        } else {
            free(Hostname);           /* ok if already null */
            Hostname = j2strdup(argv[1]);
            /* Remove trailing dot */
            if(Hostname[strlen(Hostname)-1] == '.')
                Hostname[strlen(Hostname)-1] = '\0';
        }
#ifdef CONVERS
    /* If convers hostname not set yet, set it to first 10 chars
     * of the hostname. If there are '.' from the right, cut off
     * before that. - WG7J
     */
        if(Chostname[0] == '\0') {
            strncpy(Chostname,Hostname,CNAMELEN);
            Chostname[CNAMELEN] = '\0';  /* remember is CNAMELEN+1 chars */
            if((cp = strrchr(Chostname,'.')) != NULLCHAR)
                *cp = '\0';
        }
#endif
#if defined(MAILBOX) && !defined(AX25)
        setmbnrid();
#endif
    }
    return 0;
}
  
int Uselog = 1;

static int dailylogfile = 1; /* 21Oct2014, Maiko, default to daily logfiles */

extern int getusage (char*, char*);	/* 28May2020, Maiko (VE4KLM), new usage function.
					 * 03Jun2020, Maiko, now returns a status value.
					 * 25Jun2020, Maiko, added prefix argument.
					 */
static int logusage ()
{
	/* 03Jun2020, Maiko, If usage file is missing or not available, resort to hardcoded */
	getusage ("", "log");

	return 1;
}

/* 23Oct2014, Maiko (VE4KLM), Rewrote the dolog, and created usage function */

void j2_add_trigger (char *pattern, char *cmd);
void j2_list_triggers (void);
void j2_del_trigger (char*);

static int dotimer (int argc, char **argv, void *p);	/* need prototype, further below */

void j2_que_show ();	/* need prototype, further below */

int dolog (int argc, char **argv, void *p)
{
	int retval = 0;

	if (argc < 2)
		retval = logusage ();
/*
 * 18Apr2020, Maiko, this is very convenient, I can pass the j2_add_trigger right
 * here in the same source file, although the trigger code should go into it's own
 * source file, but his is the prototype, so let's do it :)
 *
 */
	else if (!strcmpi (argv[1], "trigger"))
	{
		if (argc < 3)
			retval = logusage ();

		/* 27May2020, Maiko (VE4KLM), now subcmds of trigger */

		else
		{
			/* make sure argc is 5, since j2_add_trigger does not look for null args */
			if (!strcmpi (argv[2], "add") && argc == 5)
				j2_add_trigger (argv[3], argv[4]);

			else if (!strcmpi (argv[2], "list"))
				j2_list_triggers ();

			else if (!strcmpi (argv[2], "del") && argc == 4)
				j2_del_trigger (argv[3]);

			else if (!strcmpi (argv[2], "timer"))
				dotimer (argc, argv, p);

			else if (!strcmpi (argv[2], "show"))
				j2_que_show ();

			else
				retval = logusage ();
		}
	}
	else if (!strcmpi (argv[1], "file"))
	{
		if (argc < 3)
			retval = logusage ();

		else
		{
			if (!strcmpi (argv[2], "nos.log"))
				dailylogfile = 0;

			else if (!strcmpi (argv[2], "daily"))
 				dailylogfile = 1;

			else
				retval = logusage ();
		}
	}
	else if (!isboolcmd (&Uselog, argv[1]))
		retval = logusage ();

	return retval;	/* the old function returned 1 on any 'failure' */
}

/*
 * 01Feb2011, Maiko (VE4KLM), Code split out of the original nos_log() call,
 * which has in turn been changed to call this new function. I need to do this
 * for a new nos_log_peerless() call for clients that get UDP *connects*, where
 * it would be nice if the ip address and port showed up in the JNOS log file.
 */
static FILE *nos_logfile (void)
{
    char *cp, ML[FILE_PATH_SIZE];
    time_t t;
    FILE *fp;
 
    if (!Uselog) return NULLFILE;

    time (&t);
    cp = ctime (&t);
#ifdef UNIX
    if (*(cp + 8) == ' ') *(cp + 8) = '0';	/* 04 Feb, not b4 Feb */
#endif

	/*
	 * 21Oct2014, Maiko (VE4KLM), incorporate VE3TOK code, but instead of
	 * compile time, make it a runtime option, ie : dailylogfile flag.
	 */
	if (dailylogfile)
    	sprintf (ML, "%s/%2.2s%3.3s%2.2s", LogsDir, cp + 8, cp + 4, cp + 22);
	else
	{
    	/* 28Sep2014, Boudewijn(Bob) VE3TOK, changed log name to nos.log */
    	sprintf (ML, "%s/%s", LogsDir, "nos.log");
	}

	if ((fp = fopen (ML, APPEND_TEXT)) == NULLFILE)
		return NULLFILE;

	if (dailylogfile)
    	fprintf (fp, "%9.9s", cp + 11);
	else
	{
    	/* 30Sep2014, Boudewijn VE3TOK, added Day Month Year to time stamp */
    	fprintf (fp, "%.24s ", cp);
	}

	return fp;
}

/*
 * 01Feb2011, Maiko, New log function for peerless (DATAGRAM) clients, such
 * as the brand new snmpd.c module I began working on, back in December.
 * 16Apr2023, Maiko (VE4KLM), make this usable for IPV6 (using *void) ...
 */
#ifdef	IPV6
void nos_log_peerless (void *anysock, char *fmt, ...)
#else
void nos_log_peerless (struct sockaddr_in *fsock, char *fmt, ...)
#endif
{
    va_list ap;
    FILE *fp;

#ifdef	IPV6
	short *family = (short*)anysock;
#endif

    if ((fp = nos_logfile ()) == NULLFILE)
        return;  

#ifdef IPV6
	if (*family == AF_INET6)
	{
		struct j2sockaddr_in6 *ipv6sock = (struct j2sockaddr_in6*)anysock;

		fprintf (fp, "%s: %s:%d", Curproc->name, ipv6shortform (human_readable_ipv6 (ipv6sock->sin6_addr.s6_addr), 0), ipv6sock->sin6_port);
	}
	else
	{
		struct sockaddr_in *fsock = (struct sockaddr_in*)anysock;
#endif
	/* 21Sep2013, Maiko, Let's include the process name */
	fprintf (fp, "%s: %s:%d", Curproc->name, inet_ntoa (fsock->sin_addr.s_addr), fsock->sin_port);
#ifdef	IPV6
	}
#endif

    fprintf (fp, " - ");

    va_start (ap, fmt);
    vfprintf (fp, fmt, ap);
    va_end (ap);

    fprintf (fp, "\n");
    fclose (fp);
}

#define	J2LOGTRIGGER

#ifdef	J2LOGTRIGGER

#define	ATRIGGER_OR		1
#define	ATRIGGER_AND	2

struct atrigger {
	char *pattern;			/* pattern to look for */
	char *cmd;				/* only needed once, waste of space, figure it out later */
	int flags;				/* & | for starters */
	struct atrigger *next;	/* point to next item */
};

typedef struct atrigger J2ATRIGGER;

#define	MAXTRIGGERS	5

static J2ATRIGGER *mytriggers[MAXTRIGGERS];

static int triggercnt = 0;

/*
 * Mid April, 2020, Charles Hargrove (N2NOV) gave me a great idea !!!
 * 
 * this will be an interesting code design experience, it's been a
 * while since I got into link lists and stuff. Got tons of time on
 * my hands lately, it's awful what this whole COVID 19 is doing.
 *
 */

struct qtrigger {
	int whichone;			/* shell script identifier */
	char *logentry;			/* copy of the log entry */
	struct qtrigger *next;	/* point to next item */
};

/*
 * Queuing routines called from within nos_log ()
 */

typedef struct qtrigger J2QTRIGGER;

static J2QTRIGGER *qtriggers = (void*)0;

void j2_que_show ()
{
	J2QTRIGGER	*ptr = qtriggers;

	if (ptr) tprintf ("queued log triggers\n");

	while (ptr)
	{
		//
		// 20Apr2020, Maiko, 8:30 pm, just realized logentry does not have
		// the time and psock stamp that nos_log adds to the log entry ! So
		// we should add that in there somehow, the trigger (filter) should
		// be able to use that data as well, it's quite important actually.
		//
		tprintf ("\t%s\n", ptr->logentry);

		ptr = ptr->next;
	}
}

void j2_que_trigger (FILE *fp, int whichone, char *logentry)
{
	J2QTRIGGER	*ptr;

	fprintf (fp, "\n\tqueuing log trigger");

	ptr = malloc (sizeof(J2QTRIGGER));
	ptr->whichone = whichone;
	//ptr->logentry = (void*)0;
	ptr->logentry = j2strdup (logentry);
	ptr->next = (void*)0;

	if (!qtriggers) qtriggers = ptr;

	else
	{
		ptr->next = qtriggers;
		qtriggers = ptr;
	}
}

/* 18Apr2020, Maiko, 4:45 pm, functions to add triggers (filters) */

static J2ATRIGGER *add_trigger_item (char *item, int flags)
{
	J2ATRIGGER *ptr = malloc (sizeof(J2ATRIGGER));

	log (-1, "add log trigger item [%s] [%d]", item, flags);

	ptr->pattern = j2strdup (item);
	ptr->cmd = (void*)0;
	ptr->flags = flags;
	ptr->next = (void*)0;

	return ptr;
}

void j2_del_trigger (char *pattern)
{

#ifdef	NOT_YET

	J2ATRIGGER *ptr;

	int cnt = atoi (pattern);

	if (cnt >= triggercnt)
	{
		tprintf ("invalid selection\n");
		return;
	}

	ptr = mytriggers[cnt];
#endif
	tprintf ("not working yet\n");
}

void j2_list_triggers (void)
{
	J2ATRIGGER *ptr;

	int cnt = 0;

	while (cnt < triggercnt)
	{
		ptr = mytriggers[cnt];

		tprintf (" %2d\t%s\t%s\n", cnt, ptr->pattern, ptr->cmd);

		cnt++;
	}
}

void j2_add_trigger (char *pattern, char *cmd)
{
	char *pptr, c, parser[80];

	J2ATRIGGER *iptr;

	pptr = parser;

	log (-1, "add log trigger [%s] [%s]", pattern, cmd);

	while ((c = *pattern))
	{
		if (c == '&')
		{
			*pptr = 0;	/* terminate the parsed out item string */
			iptr = add_trigger_item (parser, ATRIGGER_AND);
			pptr = parser;

			if (!mytriggers[triggercnt])
				mytriggers[triggercnt] = iptr;
			else
			{
				iptr->next = mytriggers[triggercnt];
				mytriggers[triggercnt] = iptr;
			}

			pattern++;
		}

		else if (c == '|')
		{
			*pptr = 0;	/* terminate the parsed out item string */
			iptr = add_trigger_item (parser, ATRIGGER_OR);
			pptr = parser;

			if (!mytriggers[triggercnt])
				mytriggers[triggercnt] = iptr;
			else
			{
				iptr->next = mytriggers[triggercnt];
				mytriggers[triggercnt] = iptr;
			}

			pattern++;
		}

		else *pptr++ = *pattern++;
	}

	/*
	 * 28May2020, Maiko (VE4KLM), not terminating the pattern with the '&' or '|'
	 * results in JNOS crashing - why ? It took me long enough to figure out that
	 * if you don't terminate the pattern then mytriggers[x] will be NULL, since
	 * no items would have ever been added to it :| So, the solution is to check
	 * if mytriggers[x] is NULL and if so, flag an error to the user saying they
	 * are missing the all important '&' (AND) or '|' (OR) operators - hello !
	 */

	if (!mytriggers[triggercnt])
	{
		tprintf ("pattern must contain an '&' (AND) or an '|' (OR) operator\n");
	}
	else
	{
		/* place command in the top (root) entry */
		mytriggers[triggercnt]->cmd = j2strdup (cmd);
		triggercnt++;
	}
}

/*
 * Trigger (pattern match) routines called from within nos_log ()
 */

void j2_log_trigger (FILE *fp, char *logentry)
{
	J2ATRIGGER *ptr;

	int cnt, matched, match_cnt;

	/*
	 * 18Apr2020, Maiko, after 5 pm, now have 'log trigger "conditions"' command :)
	 * now loop through the array of pointers to each trigger loaded with new command.
 	 */

	cnt = 0;

	while (cnt < triggercnt)
	{

	ptr = mytriggers[cnt];

	match_cnt = matched = 0;	/* 18Apr2020, 8:45 pm, need match count for proper trigger */

	/*
	 * 18Apr2020, Maiko, Very simplified match algorithm, just as proof
	 * of concept right now, it's not great - but just something quick
	 * that I came up with for 'A and B and C' or 'A or B or C' ???
	 */

	while (ptr)
	{
		match_cnt++;

		if (strstr (logentry, ptr->pattern))
		{
			/* debugging
			fprintf (fp, "\n(%d %d) matched [%s] [%s] [%d]",
				cnt, triggercnt, logentry, ptr->pattern, ptr->flags);
			 */

			matched++;	/* one of the items got matched */

			if (ptr->flags == ATRIGGER_OR)
			{
				match_cnt = -1;
				break;
			}

			/* if ((ptr->flags == ATRIGGER_AND) ... not needed, if not OR must be AND ... */
		}

		ptr = ptr->next;
	}

	if ((match_cnt == -1) || (match_cnt == matched++))
		j2_que_trigger (fp, cnt, logentry);

	cnt++;

	}
}

/*
 * 18Apr2020, Maiko, 9:30 pm :)
 *
 * Now we do the log trigger service, which scans the queued log triggers,
 * and does the actual command shell outs, this has been a very COOL quick
 * project - even for the sysop themselves, mailing them on any log event
 * they setup a trigger (a filter or pattern) for, and multiples triggers.
 *
 * Okay, it''s 9:30 pm, April 19, 2020, I am clearly missing something in
 * the whole sequence, something is not right, the first newproc command
 * shows up fine, the subsequent ones are just 'sh', and cmdparse barfs,
 * so at this point almost there, but not there (frustrating, yup) ...
 *
 */

struct timer logtrigger_t;

/* Process that actually handles 'log trigger' command execution */

void logtriggerproc (int i, void *p1, void *p2)
{
    extern struct cmds DFAR Cmds[];

	char *cmd;

	J2QTRIGGER *nextptr, *ptr;
/*
 * so the question is now (20Apr2020, Maiko), do I parse the whole list of queued
 * triggers and then remove the whole list ? or do I just remove the item executed
 * from the queue at the point of execution ? Also, should each queued execution
 * be done in it's own process ? See the THIS_DOES_NOT_WORK further down ...
 */
	ptr = nextptr = qtriggers;

	if (ptr) log (-1, "executing queued log triggers");

	while (ptr)
	{
		// 20Apr2020, You have to give a COPY of the string, cmdparse() alters it !
		cmd = j2strdup (mytriggers[ptr->whichone]->cmd);

		/* 20Apr2020, Maiko, remove from the queue, we have what we need now (from at.c) */
		if (ptr == qtriggers)
		{
			qtriggers = ptr->next;
			nextptr = ptr->next;
			free (ptr);
			ptr = nextptr;
		}
		else
		{
			nextptr->next = ptr->next;
			free (ptr);
			ptr = nextptr->next;
		}

		//
		// 20Apr2020, Maiko, interesting, cmdparse() actually alters 'cmd' string,
		// writing string terminations to take out space or tab seperators, etc !
		// so the solution to this now makes sense (since at.c has command being
		// allocated by mallocw and then freed afterwards) - and now it works :)
		//
		cmdparse (Cmds, cmd, NULL);

		free (cmd);		// added 20Apr2020, Maiko

		pwait (NULL);	/* is this necessary ? */
	}
}

/* process that goes through the log trigger queues and runs commands */

void logtriggertick (void *t)
{

#ifdef	THIS_DOES_NOT_WORK

 /* okay this code should really be put into the new process ! */

 /* 9:53 pm, this makes no difference ! still getting the 'sh' only', I think
 * I need to look at the j2_queue_trigger() function, something is wrong here.
 */
	J2QTRIGGER	*ptr = qtriggers;

	log (-1, "processing queued log triggers");

	while (ptr)
	{
		// 19Apr2020, 10:10 am, nope need to use newproc, reference at.c file,
		// oops forgot to set the arg in the timer structure, important, or else
		// JNOS sqawks about shell command without args ?
#endif
		newproc ("log trigger", 1024, logtriggerproc, 0, NULL, NULL, 0);
		// and ptr->cmd previously is now just NULL

#ifdef	THIS_DOES_NOT_WORK

		ptr = ptr->next;
	}
#endif

    start_timer (&logtrigger_t);	/* and fire it up again */
}

/*
 * 27May2020, Maiko (VE4KLM), adjust argc and argv indices
 * since this func is now a subcmd under trigger, not log.
 */
static int dotimer (int argc, char **argv, void *p)
{
	static char *fakearg = "fake argument";

  /* argc 4, argv[0] log, argv[1] trigger, argv[2] timer, argv[3] seconds */

    if (argc < 4)
	{
		tprintf ("log trigger timer = %d/%d\n",
			read_timer (&logtrigger_t) / 1000,
				dur_timer (&logtrigger_t) / 1000);

        return 0;
    }

    logtrigger_t.func = (void (*)__ARGS((void*)))logtriggertick;

	// 19Apr2020, Maiko, Oops, I see a problem here, each AT timer has it's own
	// timer structure, and we need to do the same for the log trigger ? since a
	// command needs to be passed ? need to think about this for a bit, sigh ...

	/* 19Apr2020, Maiko, you have pass 'something' or else cmdparse complains !!! */
    logtrigger_t.arg = mallocw (strlen (fakearg) + 1);
	strcpy (logtrigger_t.arg, fakearg);

    set_timer (&logtrigger_t, (uint32)atoi(argv[3])*1000);	/* set timer duration */

    start_timer (&logtrigger_t);	/* and fire it up */

    return 0;
}

#endif	/* end of J2LOGTRIGGER */

/*
 * 02Oct2005, Maiko, renamed because 'log' conflicts with mathlib call
 * 01Feb2011, Maiko, partially rewritten for code that I split out above.
 */
void nos_log (int s, char *fmt, ...)
{
    va_list ap;
    int i;
/*
 * 31Mar2023, Maiko, replace this with a buffer that can
 * accomodate the size of the biggest sockaddr struct !
 *
    struct sockaddr fsocket;
    i = SOCKSIZE;
 */
#ifdef	IPV6
	unsigned char socketbuf[sizeof(struct j2sockaddr_in6)];
	i = sizeof (struct j2sockaddr_in6);
#else
	unsigned char socketbuf[sizeof(struct sockaddr)];
	i = sizeof (struct sockaddr);
#endif

    FILE *fp;

#ifdef	J2LOGTRIGGER
	/* 20Apr2020, Maiko, oops, we MUST include time and psock stamp */
	char timesockstamp[30], *tssptr = timesockstamp;

	*tssptr = 0;	/* this is VERY important, starting string must be NULL terminated */
#endif
  
    if ((fp = nos_logfile ()) == NULLFILE)
        return;  

#ifdef	JNOS_LOG_CONSOLE
	/* 14Apr2015, Maiko (VE4KLM), Log all console (F10) commands */
	if (s == -2)
	{
#ifdef	J2LOGTRIGGER
        	tssptr += sprintf (tssptr, " CONSOLE");
#else
        	fprintf (fp, " CONSOLE");
#endif
	}

	/*
	 * 01Feb2011, Maiko, No point calling j2getpeername() if the incoming
	 * socket value is negative. It just wastes time and CPU to go through
	 * the entire list of user sockets, knowing it will never be found !
	 */
    else
#endif
    if (s != -1)
	{
#ifdef	JNOS_LOG_INCOMING_LOCAL_PORTS
		struct usock *up;
#endif
    	// i = SOCKSIZE;

  	// 	if (j2getpeername (s, (char*)&fsocket, &i) != -1)

    	if (j2getpeername (s, (char*)socketbuf, &i) != -1)
		{
#ifdef	J2LOGTRIGGER
        	// tssptr += sprintf (tssptr, " %s", psocket (&fsocket));
        	tssptr += sprintf (tssptr, " %s", psocket (socketbuf));
#else
			// fprintf (fp, " %s", psocket (&fsocket));
			fprintf (fp, " %s", psocket (socketbuf));
#endif
		}

#ifndef IPV6
/* 31Mar2023, Maiko, Fix this up later, specific to IPV4 right now */
#ifdef	JNOS_LOG_INCOMING_LOCAL_PORTS
		/*
		 * 22Apr2016, Maiko (VE4KLM), would like to see the local port.
		 *
		 * 16Oct2016, Maiko, 'up->name' can be a NULL I discovered during
	     * debug of the smtp kick code - grrrrrrr !!!
	     */
		if ((up = itop(s)) != NULLUSOCK && up->type == TYPE_TCP && up->name)
		{
			int localport = ((struct sockaddr_in*)up->name)->sin_port;

			if (localport > 1000)
			{
#ifdef	J2LOGTRIGGER
				tssptr += sprintf (tssptr, " (%d)", localport);
#else
				fprintf (fp, " (%d)", localport);
#endif
			}
		}
/*
   NOTES : google 'tcp determine inbound outbound'

   I didn't realise that netstat -a will list an entry for LISTENING and
   ESTABLISHED, in the case of an incoming connection.

   Whereas for an outgoing connection, there is only an entry for
   ESTABLISHED. 

 */

#endif
#endif	// if IPV6 not defined

	}

#ifdef	J2LOGTRIGGER
	fprintf (fp, "%s - ", timesockstamp);
#else
    fprintf (fp, " - ");
#endif

    va_start (ap, fmt);
    vfprintf (fp, fmt, ap);
    va_end (ap);

#ifdef	J2LOGTRIGGER
	{

	/*
	 * Mid April, 2020, Maiko (VE4KLM) - Charles Hargrove (N2NOV) gave me a great idea !!!
	 *
	 * 20Apr2020, Maiko, time and psock should be included, since those are actually
	 * very important pieces of information to base your filters (triggers) on ...
	 * 
	 * correction ! this only does the psock portion, time is done in nos_logfile (),
	 * which will require even more mods, so leave the time alone for now - psock is
	 * an important one to be able to trigger on, so we can live without time 4 now.
	 *
	 */
		char buffer[150], *bptr = buffer;

		bptr += sprintf (bptr, "%s - ", timesockstamp);

    	va_start (ap, fmt);
    	vsprintf (bptr, fmt, ap);
	    va_end (ap);

		j2_log_trigger (fp, buffer);
	}
#endif

    fprintf (fp, "\n");
    fclose (fp);
}

#if defined(UNIX) && defined(DOS_NAME_COMPAT)
static char *
dosnameformat(const char *name)
{
    static char dosname[9];  /* 8 + NUL */

    strncpy(dosname, name, 8);  /* copy at most first 8 */
    dosname[8] = '\0';  /* in case was > 8 chars long */
    return dosname;
}
#endif

int
dohelp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct cmds *cmdp;
    int i;
    char buf[FILE_PATH_SIZE];
#if defined(MORESESSION) || defined(DIRSESSION) || defined(FTPSESSION)
    char **pargv;
#else
    FILE *fp;
#ifdef MAILBOX
    struct mbx *m = NULLMBX;
#endif /* MAILBOX */
#endif /* MORESESSION || DIRSESSION || FTPSESSION */
  
/* ?  => display compiled-command names 
 * help   =or=   help ?  => show /help/help
 * help X => show /help/X
 */
    if(*argv[0] == '?' ) {
        j2tputs("Main commands:\n");
        memset(buf,' ',sizeof(buf));
        buf[75] = '\n';
        buf[76] = '\0';
        for(i=0,cmdp = Cmds;cmdp->name != NULL;cmdp++,i = (i+1)%5){
            strncpy(&buf[i*15],cmdp->name,strlen(cmdp->name));
            if(i == 4){
                j2tputs(buf);
                memset(buf,' ',sizeof(buf));
                buf[75] = '\n';
                buf[76] = '\0';
            }
        }
        if(i != 0)
            j2tputs(buf);
    } else {
        sprintf(buf,"%s/%s",CmdsHelpdir,"help");  /* default */
        if(argc > 1) {
            for(i=0; Cmds[i].name != NULLCHAR; ++i)
                if(!strncmp(Cmds[i].name,argv[1],strlen(argv[1]))) {
                    if(*argv[1] != '?')
#if defined(UNIX) && defined(DOS_NAME_COMPAT)
                        sprintf(buf,"%s/%s",CmdsHelpdir,dosnameformat(Cmds[i].name));
#else
                        sprintf(buf,"%s/%s",CmdsHelpdir,Cmds[i].name);
#endif
                    break;
                }
            if(Cmds[i].name == NULLCHAR) {
                j2tputs("Unknown command; type \"?\" for list\n");
                return 0;
            }
        }
#if defined(MORESESSION) || defined(DIRSESSION) || defined(FTPSESSION)
        pargv = (char **)callocw(2,sizeof(char *));
        pargv[1] = j2strdup(buf);
        if(Curproc->input == Command->input) {   /* from console? */
            newproc("more",512,(void (*)__ARGS((int,void *,void *)))morecmd,2,(void *)pargv,p,1);
        } else {
            morecmd(2,pargv,p);
            free(pargv[1]);
            free(pargv);
        }
#else
        if((fp = fopen(buf,READ_TEXT)) == NULLFILE)
            tprintf(NoRead,buf,strerror(errno));
        else {
#ifdef MAILBOX
            for (m=Mbox; m!=NULLMBX; m=m->next)
                if (m->proc == Curproc) break;
#endif
            sendfile(fp,Curproc->output,ASCII_TYPE,0,m);
            fclose(fp);
        }
#endif
    }
    return 0;
}
  
/* Attach an interface
 * Syntax: attach <hw type> <I/O address> <vector> <mode> <label> <bufsize> [<speed>]
 */
int
doattach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Attab,argc,argv,p);
}
/* Manipulate I/O device parameters */
int
doparam(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int param,set;
    int32 val;
    register struct iface *ifp;
  
    if((ifp = if_lookup(argv[1])) == NULLIF){
        tprintf(Badinterface,argv[1]);
        return 1;
    }
    if(ifp->ioctl == NULL){
        j2tputs("Not supported\n");
        return 1;
    }
    if(argc < 3){
        for(param=1;param<=PARAM_MAXOPT;param++){
            val = (*ifp->ioctl)(ifp,param,FALSE,0);
            if(val != -1)
                tprintf("%s: %d\n",parmname(param),val);
        }
        return 0;
    }
    param = devparam(argv[2]);
    if(param == -1){
        tprintf("Unknown parameter %s\n",argv[2]);
        return 1;
    }
    if(argc < 4){
        set = FALSE;
        val = 0;
    } else {
        set = TRUE;
	/* 11Oct2009, Maiko, Use atoi and tprintf "%d" for int32 vars */
        val = atoi(argv[3]);
    }
    val = (*ifp->ioctl)(ifp,param,set,val);
    if(val == -1){
        tprintf("Parameter %s not supported\n",argv[2]);
    } else {
        tprintf("%s: %d\n",parmname(param),val);
    }
    return 0;
}
  
/* Display or set IP interface control flags */
int
domode(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct iface *ifp;
  
    if((ifp = if_lookup(argv[1])) == NULLIF){
        tprintf(Badinterface,argv[1]);
        return 1;
    }
    if(argc < 3){
        tprintf("%s: %s\n",ifp->name,
        (ifp->flags & CONNECT_MODE) ? "VC mode" : "Datagram mode");
        return 0;
    }
    switch(argv[2][0]){
        case 'v':
        case 'c':
        case 'V':
        case 'C':
            ifp->flags |= CONNECT_MODE;
            break;
        case 'd':
        case 'D':
            ifp->flags &= ~CONNECT_MODE;
            break;
        default:
            tprintf("Usage: %s [vc | datagram]\n",argv[0]);
            return 1;
    }
    return 0;
}
  
#if     (!defined(MSDOS) || defined(ESCAPE))
int
doescape(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2)
        tprintf("0x%x\n",Escape);
    else
        Escape = *argv[1];
    return 0;
}
#endif
  
  
#if defined(REMOTESERVER) || defined(REMOTECLI)
/* Generate system command UDP packet.
 * Synopsis (client commands):
 * remote [-p port#] [-k key] <hostname> reset|exit
 * remote [-p port#] [-a kick_target_hostname] <hostname> kickme
 * remote [-p port] -k key -r dest_ip_addr[/#bits] <hostname> add|drop
 * Synopsis (server commands):
 * remote -g gateway_key  (for gateway add/drop validation)
 * remote -s system_key   (for reset/exit validation)
 *
 * Oct 2004, VE4KLM, Added 'udp' to the 'add' and 'drop' commands.
 */

char remote_options[] =
#ifdef REMOTECLI
                        "a:p:k:"
#if defined(UDP_DYNIPROUTE) && defined(ENCAP)
                        "r:"
#endif
#endif /* REMOTECLI */
#ifdef REMOTESERVER
                        "s:"
#if defined(UDP_DYNIPROUTE) && defined(ENCAP)
                        "g:"
#endif
#endif /* REMOTESERVER */
                             ;  /* all supported options */
  
int
doremote(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int c;
#ifdef REMOTECLI
    char *cmd,*host, *key = NULLCHAR;
    char *data,x;
#if defined(UDP_DYNIPROUTE) && defined(ENCAP)
    char *route = NULLCHAR;
    int rlen;
#endif
    int s;
    int32 addr = 0;
    int16 port = IPPORT_REMOTE;   /* Set default */
    int16 len;
    int klen=0;
    struct sockaddr_in fsock;
	int cleanup;
#endif
  
    j2optind = 1;             /* reinit j2getopt() */
    while((c = j2getopt(argc,argv,remote_options)) != EOF){
        switch(c){
#ifdef REMOTECLI
            case 'a':
                if((addr = resolve(j2optarg)) == 0){
                    tprintf(Badhost,j2optarg);
                    return -1;
                }
                break;
            case 'p':
                port = atoi(j2optarg);
                break;
            case 'k':
                key = j2optarg;
                klen = strlen(key);
                break;
#if defined(UDP_DYNIPROUTE) && defined(ENCAP)
            case 'r':
                route = j2optarg;
                rlen = strlen(route);
                break;
#endif
#endif /* REMOTECLI */
#ifdef REMOTESERVER
            case 's':
                free(Rempass);
                Rempass = j2strdup(j2optarg);
                return 0;       /* Only set local password */
#if defined(UDP_DYNIPROUTE) && defined(ENCAP)
            case 'g':
                free(Gatepass);
                Gatepass = j2strdup(j2optarg);
                return 0;       /* set gateway-route password */
#endif
#endif /* REMOTESERVER */
        }
    }
    if(j2optind > argc - 2){
        j2tputs("Insufficient args\n");
        return -1;
    }

#ifdef REMOTECLI
    host = argv[j2optind++];
    cmd = argv[j2optind];
    fsock.sin_family = AF_INET;
    if((fsock.sin_addr.s_addr = resolve(host)) == 0){
        tprintf(Badhost,host);
        return -1;
    }
    fsock.sin_port = port;
  
    if((s = j2socket(AF_INET,SOCK_DGRAM,0)) == -1){
        j2tputs(Nosock);
        return 1;
    }
    len = 1;
    /* Did the user include a password or kick target? */
    if(addr != 0 && cmd[0] == 'k')       /* kick */
        len += sizeof(int32);
  
    if (key)
	{
        if (cmd[0] == 'r' || cmd[0] == 'e')  /* reset|exit */
            len += klen;
#if defined(UDP_DYNIPROUTE) && defined(ENCAP)
		/* 27Jul2005, Maiko, Added 'udp' to 'add' and 'drop' commands */
        else if (cmd[0] == 'a' || cmd[0] == 'd' || cmd[0] == 'u')
		{
            if (route)
                len += rlen+1 + klen;
		}
#endif
    }
 
    //data = (len == 1) ? &x : mallocw((size_t)len);

	if (len == 1)
		data = &x;
	else
		data = mallocw((size_t)len);

	cleanup = 0;
 
    switch(cmd[0])
	{
        case 'e':
        case 'r':
            data[0] = (cmd[0] == 'r' ? SYS_RESET : SYS_EXIT);
            if(key)
                strncpy(&data[1],key,(size_t)klen);
            break;

        case 'k':
            data[0] = KICK_ME;
            if(addr)
                put32(&data[1],addr);
            break;

#if defined(UDP_DYNIPROUTE) && defined(ENCAP)
        case 'a':
        case 'd':
            data[0] = (cmd[0] == 'a') ? ROUTE_ME : UNROUTE_ME;

		/* 14Oct2004, Maiko, IPUDP Support (K2MF) */
		case 'u':
			if (cmd[0] == 'u') data[0] = UDPROUTE_ME;

            if (route)
			{
                data[1] = (char)rlen;
                strcpy(&data[2],route);
				/* always a key too */
                strncpy(&data[2+rlen], key, (size_t)klen);
            }
            break;
#endif

        default:
            tprintf("Unknown remote command %s\n",cmd);
            cleanup = 1;
    }

	if (!cleanup)
	{
    	/* Form the command packet and send it */
    	if(j2sendto(s,data,len,0,(char *)&fsock,sizeof(fsock)) == -1)
        	tprintf("j2sendto failed: %s\n",strerror(errno));
	}

    //if(data != &x)
	if (len != 1)
        free(data);

    close_s(s);
#endif /* REMOTECLI */
    return 0;
}
#endif /* REMOTESERVER || REMOTECLI */

/*
 * 24Apr2021, Maiko (VE4KLM), Need a way to print date to the console,
 * since using 'sh date' creates a zombie process, which I need to fix
 * as well, but this makes more sense - for redirecting to a file.
 *
 * Example - source 'report.nos' : (uses just JNOS console commands)
 *
    remark > /tmp/a.txt
    remark "heard list from ve4klm on the south facing sloper" >> /tmp/a.txt
    remark >> /tmp/a.txt
    date >> /tmp/a.txt
    remark >> /tmp/a.txt
    ax25 h rp0 >> /tmp/a.txt
    remark >> /tmp/a.txt
    ax25 h digid rp0 >> /tmp/a.txt
    remark >> /tmp/a.txt
 *
 */
int dodate (int argc, char **argv, void *p)
{
    time_t nowtime;

    time (&nowtime);  /* current time */

    tprintf ("%s", ctime (&nowtime));

    return 0;
}
  
/* Remark [args]  --  copy arguments to stdout, then write a NL.  -- N5KNX */
int
doremark(argc,argv,p)
int argc;
char *argv[];
void *p;
{

    while (argc>1) {
        j2tputs(argv[1]);
        argv++;
        argc--;
        if (argc != 1)
            tputc(' ');
    }
    tputc ('\n');
    return 0;
}

#if defined(MORESESSION) || defined(DIRSESSION) || defined(FTPSESSION)
  
int
morecmd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct session *sp = (struct session*)0;
    FILE *fp;
    char fname[FILE_PATH_SIZE];
    int werdone = 0, row = 0;
    int usesession = 0;
  
    /* Use a session if this comes from console - WG7J*/
    if(Curproc->input == Command->input) {
        usesession = 1;
        if((sp = newsession(argv[1],MORE,0)) == NULLSESSION){
            return 1;
        }
        /* Put tty into raw mode so single-char responses will work */
        sp->ttystate.echo = sp->ttystate.edit = 0;
        row = Numrows - 1 - StatusLines;
    }
  
    strcpy(fname,make_fname(Command->curdirs->dir,argv[1]));
    if((fp = fopen(fname,READ_TEXT)) == NULLFILE){
        tprintf(NoRead,fname,strerror(errno));
        if(usesession) {
            keywait(NULLCHAR,1);
            freesession(sp);
        }
        return 1;
    }
    while(fgets(fname,sizeof(fname),fp) != NULLCHAR)
	{
        if((argc < 3) || (strstr(fname,argv[2])!=NULLCHAR))
		{
            j2tputs(fname);
            if(usesession)
			{
                if(--row == 0)
				{
                    row = keywait(TelnetMorePrompt,0);
                    switch(row)
					{
                        case -1:
                        case 'q':
                        case 'Q':
                            werdone = 1;
							break;
                        case '\n':
                        case '\r':
                            row = 1;
                            break;
                        case '\t':
#ifndef HEADLESS
                            clrscr();
#endif
                        case ' ':
                        default:
                            row = Numrows - 1 - StatusLines;
                    }
					if (werdone) /* leave while loop, replaces GOTO 'done' */
						break;
                }
            }
        }
    }
	fclose(fp);
    if(usesession)
	{
        keywait(NULLCHAR,1);
        freesession(sp);
    }
    return 0;
}
#endif /* MORE | DIR | FTP SESSION */
  
#ifdef MORESESSION
int
domore(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char **pargv;
    int i;
  
    if(Curproc->input == Command->input) {
        /* Make private copy of argv and args,
         * spawn off subprocess and return.
         */
        pargv = (char **)callocw((size_t)argc,sizeof(char *));
        for(i=0;i<argc;i++)
            pargv[i] = j2strdup(argv[i]);
        newproc("more",512,(void (*)__ARGS((int,void *,void *)))morecmd,argc,(void *)pargv,p,1);
    } else
        morecmd(argc,argv,p);
    return 0;
}
#endif /* MORESESSION */
  
#ifdef ALLCMD
int
dotaillog(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char logfile[FILE_PATH_SIZE];
    time_t t;
    char *cp;
  
    /* Create the log file name */
    time(&t);
    cp = ctime(&t);
#ifdef UNIX
    if (*(cp+8) == ' ') *(cp+8) = '0';  /* 04 Feb, not b4 Feb */
#endif
    sprintf(logfile,"%s/%2.2s%3.3s%2.2s",LogsDir,cp+8,cp+4,cp+22);
  
    argc = 2;   /* ignore any provided args */
    argv[1] = logfile;
    return dotail(argc,argv,p);
}
  
int
dotail(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register int handle, i;
    register unsigned line = 0;
    size_t rdsize = 2000;	/* should be type size_t, not int */
    long length;
    char *buffer;
    char fname[FILE_PATH_SIZE];
    int maxlines = Numrows - StatusLines - 6;   /* same room for status lines, + fudge */
  
    buffer = callocw(2000, sizeof (char));
  
    strcpy(fname,make_fname(Command->curdirs->dir,argv[1]));  /* WA7TAS: relativize path */
    if ((handle = open (fname, O_BINARY | O_RDONLY)) == -1) {
        tprintf(NoRead,argv[1],strerror(errno));
        free(buffer);
        return -1;
    }
    length = filelength(handle);
  
    if (length > 2000) {
        length -= 2000;
    } else {
        rdsize = (size_t) length;
        length = 0;
    }
  
    lseek (handle, length, SEEK_SET);
    if (read (handle, buffer, rdsize) == -1) {
        tprintf(NoRead,argv[1],strerror(errno));
        close(handle);
        free(buffer);
        return -1;
    }
  
    for (i = (int)rdsize - 1; i > 0; i--) {
        if (buffer[i] == '\n')
            line++;
        if (line == (unsigned)maxlines)
            break;
    }
    for (; i < (int)rdsize; i++)
        tputc(buffer[i]);
  
    tputc('\n');
    close(handle);
    free(buffer);
    return 0;
}
#endif /*ALLCMD*/
  
/* No-op command */
int
donothing(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return 0;
}

/* Pause command */  
int
dopause(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int secs;

    if(argc > 1) {
        secs = atoi(argv[1]);
        if (secs > 0) j2pause(1000L * secs);
    }
    return 0;
}

#ifdef MAILERROR
static int SendError = 1;
  
int doerror(int argc,char *argv[],void *p) {
    return setbool(&SendError,"Mail errors",argc,argv);
}
  
/* Mail a system message to the sysop - WG7J */
void
mail_error(char *fmt, ...)
{
    FILE *wrk,*txt;
    va_list ap;
    char *cp;
    long t,msgid;
    char fn[FILE_PATH_SIZE];
  
    if(!SendError)
        return;
  
    /* Get current time */
    time(&t);
  
    /* get the message id for this message */
    msgid = get_msgid();
  
    /* Create the smtp work file */
    sprintf(fn,"%s/%ld.wrk",Mailqdir,msgid);
    if((wrk = fopen(fn,"w")) == NULL)
        return;
  
     /* Create the smtp text file */
    sprintf(fn,"%s/%ld.txt",Mailqdir,msgid);
    if((txt = fopen(fn,"w")) == NULL) {
        fclose(wrk);
        return;
    }
  
    /* Fill in the work file */
    fprintf(wrk,"%s\nMAILER-DAEMON@%s\nsysop@%s",Hostname,Hostname,Hostname);
    fclose(wrk);
  
    /* Fill in the text file headers */
    fprintf(txt,"%s%s",Hdrs[DATE],ptime(&t));
    fprintf(txt,"%s<%ld@%s>\n",Hdrs[MSGID],msgid,Hostname);
    fprintf(txt,"%sMAILER-DAEMON@%s\n",Hdrs[FROM],Hostname);
    fprintf(txt,"%ssysop@%s\n",Hdrs[TO],Hostname);
    fprintf(txt,"%sSystem message\n\n",Hdrs[SUBJECT]);
  
    /* Print the text body */
    cp = ctime(&t);
    fprintf(txt,"On %s",cp);
    va_start(ap,fmt);
    vfprintf(txt,fmt,ap);
    va_end(ap);
    fputc('\n',txt);
    fclose(txt);
  
    /* Now kick the smtp server */
    smtptick(NULL);
}
#endif /* MAILERROR */

/*
 * 16Jan2021, Maiko (VE4KLM), Would like to source a NOS file
 * from within the new vara.c driver code, so break it out of
 * the original dosource() further below, and it will now call
 * this function as well.
 *
 * 24Jan2021, Maiko, Added sock parameter so I can direct output
 * to the main JNOS log file instead of the usual JNOS console.
 */
int j2directsource (int socknum, char *filename)
{
    int linenum = 0;
    char *inbuf,*intmp;
    FILE *fp;
  
    /* Read command source file */
    if((fp = fopen(filename,READ_TEXT)) == NULLFILE){
        tprintf(NoRead,filename,strerror(errno));
        return 1;
    }
 
    inbuf = malloc(BUFSIZ);
    intmp = malloc(BUFSIZ);
    while(fgets(inbuf,BUFSIZ,fp) != NULLCHAR){
        strcpy(intmp,inbuf);
        linenum++;
        if(Verbose)
            j2tputs (intmp);
        if(cmdparse(Cmds,inbuf,NULL) != 0){
            tprintf("*** file \"%s\", line %d: %s\n",
            filename,linenum,intmp);
        }
    }
    fclose(fp);
    free(inbuf);
    free(intmp);
    return 0;
}
  
int
dosource(argc,argv,p)
int argc;
char *argv[];
void *p;
{
   /*
    * 16Jan2021, Maiko (VE4KLM), Need to be able to directly
    * source a PPP initialization file from new vara.c driver
    * so moved all of the code into a new function (above).
    */
   return (j2directsource (-1, argv[1]));
}
  
#ifdef TTYLINKSERVER
/* if unattended mode is set - restrict ax25, telnet and maybe other sessions */
int
doattended(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Attended,"Attended flag",argc,argv);
}
#endif
  
/* if ThirdParty is not set - restrict the mailbox (S)end command to local only */
int
dothirdparty(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&ThirdParty,"Third-Party mail flag",argc,argv);
}

#if defined(ALLCMD) && defined(DOMDUMP)
/* 06Oct2009, Maiko, This doesn't look useful anymore, was it ever ? */  
int domdump (int argc, char **argv, void *p)
{
    unsigned int i;
    char *addr,*cp;
    unsigned int segment,offset;
    unsigned int len = 8 * 16;      /* default is 8 lines of hex dump */
#ifdef UNIX
    extern int _start;   /* Approximates the lowest addr we can access */
#endif
  
    if(argc < 2 || argc > 3) {
        j2tputs("Usage:- dump <hex-address | .> [decimal-range] \n");
        return 0;
    }
    if(argv[1][0] == '.')
        addr = DumpAddr;                /* Use last end address */
    else {
        if((cp=strchr(argv[1],':')) != NULL) {
            /* Interpret segment:offset notation */
            *cp++ = '\0';
            segment = (unsigned int) htol(argv[1]);
            offset = (unsigned int) htol(cp);
            addr = MK_FP(segment,offset);
        } else
            addr = ltop(htol(argv[1]));     /* get address of item being dumped */
    }
  
    if(argc == 3) {
        len = atoi(argv[2]);
        len = ((len + 15) >> 4) << 4;   /* round up to modulo 16 */
    }
  
#ifdef UNIX
    if (addr < (char *)&_start  ||  addr+len > (char *)sbrk(0)) {
        tprintf("Address exceeds allowable range %lx..%lx\n",
                FP_SEG(&_start), FP_SEG(sbrk(0)));
        return 0;
    }
#endif

    if(len < 1 || len > 256) {
        j2tputs("Invalid dump range. Valid is 1 to 256\n");
        return 0;
    }
#ifdef UNIX
    tprintf("            Main Memory Dump Of Location %lx\n", FP_SEG(addr));
#else
    tprintf("            Main Memory Dump Of Location %Fp\n", addr);
#endif
    j2tputs("Addr (offset)           Hexadecimal                         Ascii\n");
    j2tputs("----                    -----------                         -----\n\n");
  
    for(i = 0; i < len; i += 16)
        fmtline(Curproc->output, (int16)i, (char *)(addr + i), (int16)16);
    DumpAddr = (char *)(addr + i);          /* update address */
    return 0;
}
#endif
  
int
dowrite(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int s;
    struct mbx *m;
  
    if((s = atoi(argv[1])) == 0) { /* must be a name */
#ifdef MAILBOX
        /* check the mailbox users */
        for(m=Mbox;m;m=m->next){
            if(!stricmp(m->name,argv[1]))
                break;
        }
        if(!m)
            return 0;
        s = m->user;
#else
        return 0;
#endif
    }
    usprintf(s,"*** Msg from SYSOP: %s\n",argv[2]);
    usflush(s);
  
    return 0;
}
  
#ifdef MAILBOX
/* write a message to all nodeshell users
 * argv[1] is the message.
 */
int
dowriteall(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
  
    for(m=Mbox;m;m=m->next){
        if(m->sid & MBX_SID)
            continue;
        usprintf(m->user,"*** Msg from SYSOP: %s\n",argv[1]);
        usflush(m->user);
    }
    return 0;
}
#endif
  
  
int
dostatus(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    time_t nowtime;
    struct tm *tm;
  
#if defined(STATUSWIN) && defined(STATUSWINCMD)
    if (argc == 2 && Curproc->input == Command->input) {
        /* only keyboard user can issue statuswindow on|off */
        static int orig_statuslines = 0;
        int onoff;

        if (!setbool(&onoff, "", argc, argv)) {  /* on or off */
            if (!onoff && StatusLines) { /* valid off */
                orig_statuslines = StatusLines;
                StatusLines = 0;
            }
            else if (onoff && !StatusLines) {  /* valid on */
                StatusLines = orig_statuslines;
            }
            else return 0;  /* ignore invalid args */
        }
        else return 0;  /* ignore invalid args */

        window(1,1+StatusLines,Numcols,Numrows);  /* don't bother to restore split screen */
        clrscr();       /* Start with a fresh slate */
        Command->row = Numrows - 1 - StatusLines;  /* revise session info */
#ifdef SPLITSCREEN
        Command->split = 0;
        textattr(MainColors);
#endif
        return 0;
    }
#endif /* STATUSWIN && STATUSWINCMD */

    time(&nowtime);                       /* current time */
    /* May as well revise our stored UtcOffset in case we crossed a boundary */
    tm = localtime(&nowtime);
    UtcOffset = timezone - (tm->tm_isdst>0 ? 3600L : 0L);  /* UTC - localtime, in secs */
  
    tprintf(Nosversion,Version);
    j2tputs(Version2);
#if defined( WHOFOR ) && ! defined(ALLCMD)
    tprintf("for %s\n", WHOFOR);
#endif
    tprintf("Tty: %d rows, %d columns\n",Numrows,Numcols);
  
#ifdef  MSDOS
    tprintf(NosLoadInfo, _CS, _DS);
#endif
    tprintf("\nThe system time is %s", ctime(&nowtime));
    tprintf("NOS was started on %s\n", ctime(&StartTime));
    tprintf("Uptime => %s\n", tformat(secclock()));
    tprintf("Localtime is UTC%+ld minutes\n", -UtcOffset/60L);
    tputc('\n');
#ifdef TTYLINKSERVER
    tprintf("The station is currently %sttended.\n", Attended ? "A" : "Una");
#endif
	jnos_lockstats();
#ifdef  __TURBOC__
    dofstat();              /* print status of open files */
#endif
    return 0;
}
  
#ifdef ALLCMD
int
domotd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc > 2) {
        j2tputs("Usage: motd \"<your message>\"\n");
        return 1;
    }
  
    if(argc < 2) {
        if(Motd != NULLCHAR)
            j2tputs(Motd);
    } else {
        free(Motd);                     /* OK if already NULL */
        Motd = NULLCHAR;                /* reset the pointer */
  
        if(!strlen(argv[1]))
            return 0;                       /* clearing the buffer */
  
        Motd = mallocw(strlen(argv[1])+5);      /* allow for the EOL char etc */
        strcpy(Motd, argv[1]);
        strcat(Motd, "\n");                     /* add the EOL char */
    }
    return 0;
}
#endif /*ALLCMD*/
  
#ifdef  __TURBOC__
/*
 * Fstat utility code.
 * Converted to go into NOS by Kelvin Hill - G1EMM  April 9, 1990
 */
  
extern unsigned char _osmajor;
  
static char    *localcopy(char far *);
static char    *progname(int16);
  
extern char *Taskers[];
extern int Mtasker;
  
/* n5knx: see simtelnet msdos/info/inter54?.zip by Ralf Brown for details */
int
dofstat()
{
    union REGS regs;
    struct SREGS segregs;
    char far *pfiletab, far *pnext, far *fp;
    char far *name, far *plist, far *entry;
    char file[13], ownername[9], ownerext[5];
    int nfiles, i, j, numhandles, entrylen;
    int16 access, devinfo, progpsp;
    long length, offset;
    int heading = 0;
    static char DFAR header[] =
           "\n"
           "                 Table of Open Files.\n"
           "                 --------------------\n"
           "Name           length   offset hnd acc PSP device type/owner\n"
           "----           ------   ------ --- --- --- -----------------\n";

  
    regs.h.ah = 0x52;       /* DOS list of lists */
    intdosx(&regs, &regs, &segregs);
  
    /* make a pointer to start of master list */
    plist = (char far *) MK_FP(segregs.es, regs.x.bx);
  
    /* pointer to start of file table */
    pfiletab = (char far *) MK_FP(*(int far *) (plist + 6), *(int far *) (plist + 4));
  
    switch (_osmajor) {
        case 2:
            entrylen = 40;  /* DOS 2.x */
            break;
        case 3:
            entrylen = 53;  /* DOS 3.1 + */
            break;
        case 4:
        case 5:                 /* DOS 5.x - like dos 4.x */
        case 6:                 /* and NOW DOS 6.x also */
        /* case 7:            DOS 7.x gives blank filenames .. why bother? */
            entrylen = 59;  /* DOS 4.x through 6.x */
            break;
        default:
            tprintf("File table not available under %s\n",Taskers[Mtasker]);
            return 1;
    }
  
    for (;;) {
        /* pointer to next file table */
        pnext = (char far *) MK_FP(*(int far *) (pfiletab + 2), *(int far *) (pfiletab + 0));
        nfiles = *(int far *) (pfiletab + 4);
#ifdef FDEBUG
        tprintf("\nFile table at %Fp entries for %d files\n", pfiletab, nfiles);
#endif
        for (i = 0; i < nfiles; i++) {
  
            /*
             * cycle through all files, quit when we reach an
             * unused entry
             */
            entry = pfiletab + 6 + (i * entrylen);
            if (_osmajor >= 3) {
                name = entry + 32;
                strncpy(file, localcopy(name), 11);
                file[11] = '\0';
                numhandles = *(int far *) (entry + 0);
                access = (int) *(char far *) (entry + 2);
                length = *(long far *) (entry + 17);
                offset = *(long far *) (entry + 21);
                devinfo = *(int far *) (entry + 5);
                progpsp = *(int far *) (entry + 49);
            } else {
                name = entry + 4;
                strncpy(file, localcopy(name), 11);
                file[11] = '\0';
                numhandles = (int) *(char far *) (entry + 0);
                access = (int) *(char far *) (entry + 1);
                length = *(long far *) (entry + 19);
                offset = *(long far *) (entry + 36);
                devinfo = (int) *(char far *) (entry + 27);
            }
            if ((strlen(file) > 0) && (numhandles > 0) && !(devinfo & 0x80)) {
                if(!heading) {
                    j2tputs(header);
                    heading++;              /* header now printed */
                }
                tprintf("%8.8s.%3.3s%9ld%9ld %3d ",
                        file, &file[8], length, offset, numhandles);
                switch (access) {
                    case 0:
                        j2tputs("r  ");
                        break;
                    case 1:
                        j2tputs("w  ");
                        break;
                    case 2:
                        j2tputs("rw ");
                        break;
                    default:
                        j2tputs("   ");
                }
                if (_osmajor >= 3)
                    tprintf("%04X ", progpsp);
                else
                    j2tputs("---- ");
                tprintf("drive %c: ", 'A' + (devinfo & 0x1F));
                if (devinfo & 0x8000)
                    j2tputs("(network) ");
                if (_osmajor >= 3) {
                    /*
                     * only DOS 3+ can find out
                     * the name of the program
                     */
                    fnsplit(progname(progpsp), NULL, NULL, ownername, ownerext);
                    tprintf("   [%s%s]\n", strlwr(ownername), strlwr(ownerext));
                } else {
                    tputc('\n');
                }
            }
            if (!strlen(file))
                return 0;
        }
        pfiletab = pnext;
    }
}
  
/* Make a copy of a string pointed to by a far pointer */
static char *
localcopy(s)
char far *s;
{
    static char localstring[80];
    char far *p = s;
    char *l = localstring;
    int i = 0;
  
    while (*p && i++ < 79) {
        *l++ = *p++;
    }
  
    *l = '\0';
  
    return (localstring);
}
  
/*
 * Return a near pointer to a character string with the full path name of the
 * program whose PSP is given in the argument.  If the argument is invalid,
 * this may return gibberish since [psp]:0026, the segment address of the
 * environment of the program, could be bogus.  However, a good PSP begins
 * with 0xCD 0x20, and we'll check for that (N5KNX). Beyond the last
 * environment string is a null marker, a word count (usually 1), then the
 * full pathname of the owner of the environment.  This only works for DOS 3+.
 */
static char *
progname(pid)
int16 pid;
{
    int16 far   *envsegptr; /* Pointer to seg address of environment */
    char far       *envptr; /* Pointer to pid's environment  */
    int16 far  *envsizeptr; /* Pointer to environment size */
    int16          envsize; /* Size of pid's environment */
    int16             ppid; /* Parent psp address */
  
    /* Verify that this is a valid PSP, ie, it begins with INT 20h (2 bytes) */
    if (*(int16 far *) MK_FP(pid, 0) != 0x20CD)
        return "unknown";

    /* find the parent process psp at offset 0x16 of the psp */
    ppid = *(int16 far *) MK_FP(pid, 0x16);
  
    /* find the environment at offset 0x2C of the psp */
    envsegptr = (int16 far *) MK_FP(pid, 0x2C);
    envptr = (char far *) MK_FP(*envsegptr, 0);
  
    /*
     * Make a pointer that contains the size of the environment block.
     * Must point back one paragraph (to the environments MCB plus three
     * bytes forward (to the MCB block size field).
     */
    envsizeptr = (int16 far *) MK_FP(*envsegptr - 1, 0x3);
    envsize = *envsizeptr * 16;     /* x 16 turns it into bytes */
  
    while (envsize) {
        /* search for end of environment block, or NULL */
        while (--envsize && *envptr++);
  
        /*
         * Now check for another NULL immediately following the first
         * one located and a word count of 0001 following that.
         */
        if (!*envptr && *(int16 far *) (envptr + 1) == 0x1) {
            envptr += 3;
            break;
        }
    }
  
    if (envsize) {
        /* Owner name found - return it */
        return (localcopy(envptr));
    } else {
        if (pid == ppid) {
            /*
             * command.com doesn't leave it's name around, but if
             * pid = ppid then we know we have a shell
             */
            return ("-shell-");
        } else {
            return ("unknown");
        }
    }
}
#endif /*__TURBOC__*/
  
int
doprompt(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&DosPrompt,"prompt",argc,argv);
}
  
/* Command history, see also pc.c - WG7J */
void logcmd(char *cmd) {
    struct hist *new;
    char *cp;
  
    if(!Maxhistory)     /* don't keep history */
        return;
  
    /* Get rid of \n; this is also done in cmdparse().
     * We HAVE to do this here, since the string is NOT null-terminated when
     * it comes from recv_mbuf()  !!!! rip() makes it nullterminated.
     */
    rip(cmd);
    cp = cmd;
    while(*cp == ' ' || *cp == '\t')
        cp++;
    if(!*cp)     /* Empty command */
        return;
  
    if(Histrysize < Maxhistory) {     /* Add new one */
        Histrysize++;
        if(!Histry) {           /* Empty list */
        /* Initialize circular linked list */
            Histry = mallocw(sizeof(struct hist));
            Histry->next = Histry->prev = Histry;
        } else {
            new = mallocw(sizeof(struct hist));
        /* Now link it in */
            Histry->next->prev = new;
            new->next = Histry->next;
            new->prev = Histry;
            Histry->next = new;
            Histry = new;
        }
    } else {
    /* Maximum number stored already, use the oldest entry */
        Histry = Histry->next;
        free(Histry->cmd);
    }
    Histry->cmd = j2strdup(cp);
}
  
int
dohistory(int argc,char *argv[],void *p) {
    struct hist *h;
    int num;
  
    if(argc > 1) {
        Maxhistory = atoi(argv[1]);
        return 0;
    }
    tprintf("Max recall %d\n",Maxhistory);
    if((h = Histry) == NULL)
        return 0;
    num = 0;
    do {
        tprintf("%.2d: %s\n",num++,h->cmd);
        h = h->prev;
    } while(h != Histry);
    return 0;
}
  
#ifdef __BORLANDC__
  
/* This adds some additional checks to the fopen()
 * in the Borland C++ Run Time Library.
 * It fixes problem with users trying to open system devices like
 * CON, AUX etc and hang a system.
 * WG7J, 930205
 * reworked by N5KNX 8/94 to allow output to printers iff PRINTEROK is defined,
 * AND the printer can accept output.  It's still risky; JNOS can lockup if
 * the printer won't accept output.
 */
#undef fopen
FILE _FAR *_Cdecl fopen(const char _FAR *__path, const char _FAR *__mode);

static char *InvalidName[] = {
    "NUL",
    "CON","CON:",
    "AUX","AUX:",
    "PRN","PRN:",
    "LPT1","LPT1:",
    "LPT2","LPT2:",
    "LPT3","LPT3:",
    "COM1","COM1:",
    "COM2","COM2:",
    "COM3","COM3:",
    "COM4","COM4:",
    "MOUSE$",
    "CLOCK$",
    NULLCHAR
};
  
FILE *newfopen (const char *filename, const char *type) {
    char entname[9];
    char *cp, *cp1;
    int i,j;
#include <bios.h>
  
    cp = strrchr(filename, '\\');
    cp1 = strrchr(filename, '/');
    if (cp < cp1) cp = cp1;
    if (cp == NULLCHAR) cp = (char *)filename;
    else cp++;
    if ((cp1 = strchr(cp, '.')) == NULLCHAR) j = strlen(cp);
    else j = (int)(cp1 - cp);
    if (j==0) return NULL;   /* path is to a dir, or entryname is .XXX, clearly bad */
    if (j>8) j=8;
    strncpy(entname, cp, j); entname[j] = '\0';

    for(i=0;InvalidName[i] != NULLCHAR;i++)
	if(stricmp(InvalidName[i],entname) == 0) {
	    if (strpbrk(type, "wWaA") == NULLCHAR) return NULL;  /* must be writing */
#if defined(PRINTEROK)
	    j=-1;
	    if (strnicmp(cp, "prn", 3)==0 ) j=0;
	    else if (strnicmp(cp, "lpt", 3)==0) j=(*(cp+3) - '1');
	    if (j>=0 && j<3 && ((i=biosprint(2,0,j)&0xb9) == 0x90)) break;  /* must be selected, no errors, not busy */
#endif
	    return NULL;
	}

    return fopen (filename, type);
}
#endif /* __BORLANDC__ */
  
#ifdef REPEATSESSION
  
#if defined(__TURBOC__) && !defined(__BORLANDC__)
/* N5KNX: TurboC 2.0 lacks _setcursortype(), which we supply here for dorepeat */
void _setcursortype(int style)
{
/* From TurboC++ conio.h: */
#define _NOCURSOR      0
#define _SOLIDCURSOR   1
#define _NORMALCURSOR  2
  
/* Int 0x10 reg cx codes: */
#define   STD_CURSOR      0x0607
#define   BLK_CURSOR      0x0006
#define   NO_CURSOR       0x2607
  
    union REGS regs;
  
    if (style == _NOCURSOR) regs.x.cx = NO_CURSOR;
    else if (style == _SOLIDCURSOR) regs.x.cx = BLK_CURSOR;
    else if (style == _NORMALCURSOR) regs.x.cx = STD_CURSOR;
    else return;
    regs.x.ax = 0x0100; /* set cursor */
    int86(0x10, &regs, &regs);
}
#endif
  
/* Repeat a command - taken from 930104 KA9Q NOS
   WA3DSP 1/93
*/
int
dorepeat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int32 interval;
    int ret;
    struct session *sp;
  
    if(isdigit(argv[1][0])){
	/* 11Oct2009, Maiko, Use atoi for int32 vars */
        interval = atoi(argv[1]);
        argc--;
        argv++;
    } else {
        interval = MSPTICK;
    }
    if((sp = newsession(argv[1],REPEAT,1)) == NULLSESSION){
        j2tputs(TooManySessions);
        return 1;
    }
    _setcursortype(_NOCURSOR);
    while(sp==Current){
        /*  clrscr(); */
        /* gotoxy seems to work better - turn cursor off?? */
        gotoxy(1,1);
        ret = subcmd(Cmds,argc,argv,p);
        if(ret != 0 || j2pause(interval) == -1)
            break;
    }
    _setcursortype(_NORMALCURSOR);
    freesession(sp);
    return 0;
}
#endif /* REPEATSESSION */
  
  
/* Index given mailbox files */
int doindex(int argc,char *argv[],void *p) {
    int i;
  
    for(i=1;i<argc;i++) {
        if(*argv[i] == '*') {
            UpdateIndex(NULL,1);
            /* No reason to do others */
            return 0;
        } else {
            dirformat(argv[i]);
            /* Attempt to lock the mail file! */
            if(mlock(Mailspool,argv[i])) {
                tprintf("Can not lock '%s/%s.txt'!\n",Mailspool,argv[i]);
                continue;
            }
            if(IndexFile(argv[i],0) != 0)
                tprintf("Error writing index file for %s\n",argv[i]);
            /* Remove the lock */
            rmlock(Mailspool,argv[i]);
        }
    }
  
    return 0;
}
  
/* interpret a string "f+b", where f and b are numbers indicating
 * foreground and background colors to make up the text attribute
 */
void GiveColor(char *s,char *attr) {
    char *cp;
  
    if((cp=strchr(s,'+')) != NULL) {
        *cp++ = '\0';
        *attr = (char) atoi(s) & 0x0f;  /* Foreground color */
        *attr += ((char) atoi(cp) & 0x07) << 4;  /* Background, no blinking ! */
    }
}
