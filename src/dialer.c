/* Automatic SLIP/PPP line dialer.
 *
 * Copyright 1991 Phil Karn, KA9Q
 *
 *  Mar '91 Bill Simpson & Glenn McGregor
 *      completely re-written;
 *      human readable control file;
 *      includes wait for string, and speed sense;
 *      dials immediately when invoked.
 *  May '91 Bill Simpson
 *      re-ordered command line;
 *      allow dial only;
 *      allow inactivity timeout without ping.
 *  Sep '91 Bill Simpson
 *      Check known DTR & RSLD state for redial decision
 *
 * Mods by PA0GRI (newsession parameters)
 *
 * Mods by KF8NH:  iffailed, begin/end
 * Mods by N5KNX:  verbose; wait ipaddress; bug fixes
 * Mods by KF8NH:  Linux glibc 2.1 compatibility
 */
#include <ctype.h>
#ifdef UNIX
/* screwball ifdefs --- glibc2.1 changed library version defines midstream */
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 0)
#include <sys/ioctl.h>
#else
#if defined(linux) && (__GNU_LIBRARY__  >  1)
/* RH5.0 needs more to define TIOCMGET and TIOCM_CAR: */
#include <asm/termios.h>
#include <ioctls.h>
#else
#include <termios.h>
#endif
#endif
#endif /* UNIX */
#include "global.h"
#ifdef DIALER
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "netuser.h"
#ifdef UNIX
#include "unixasy.h"
#else
#include "i8250.h"
#endif /* UNIX */
#include "asy.h"
#include "tty.h"
#include "session.h"
#include "socket.h"
#include "cmdparse.h"
#include "devparam.h"
#include "icmp.h"
#include "files.h"
#include "main.h"
#include "trace.h"
#include "commands.h"
  
#define MIN_INTERVAL    5
#define MAXDEPTH      8
  
static int Failmode = 0;
static int Verbose = 1;
static char Failed;
static char Depth;
static char Skip[MAXDEPTH];
static char SkipOverride;
static char OverrideDepth;
  
static int redial __ARGS((struct iface *ifp,char *file));
  
static int dodial_nothing     __ARGS((int argc,char *argv[],void *p));
static int dodial_begin       __ARGS((int argc,char *argv[],void *p));
static int dodial_control     __ARGS((int argc,char *argv[],void *p));
static int dodial_end         __ARGS((int argc,char *argv[],void *p));
static int dodial_exit        __ARGS((int argc,char *argv[],void *p));
static int dodial_failmode    __ARGS((int argc,char *argv[],void *p));
static int dodial_iffail      __ARGS((int argc,char *argv[],void *p));
static int dodial_ifok        __ARGS((int argc,char *argv[],void *p));
static int dodial_send        __ARGS((int argc,char *argv[],void *p));
static int dodial_speed       __ARGS((int argc,char *argv[],void *p));
static int dodial_status      __ARGS((int argc,char *argv[],void *p));
static int dodial_verbose     __ARGS((int argc,char *argv[],void *p));
static int dodial_wait        __ARGS((int argc,char *argv[],void *p));
  
static struct cmds DFAR dial_cmds[] = {
    "",         dodial_nothing, 0, 0, "",
    "begin",    dodial_begin,   0, 1, "begin",
    "control",  dodial_control, 0, 2, "control up | down",
    "end",      dodial_end,     0, 1, "end",
    "exit",     dodial_exit,    0, 1, "exit",
    "failmode", dodial_failmode,0, 2, "failmode on | off",
    "iffail",   dodial_iffail,  0, 2, "iffail \"command\"",
    "ifok",     dodial_ifok,    0, 2, "ifok \"command\"",
    "send",     dodial_send,    0, 2, "send \"string\" [<milliseconds>]",
    "speed",    dodial_speed,   0, 2, "speed <bps>",
    "status",   dodial_status,  0, 2, "status up | down",
    "verbose",  dodial_verbose, 0, 2, "verbose on | off",
    "wait",     dodial_wait,    0, 2,
                    "wait <milliseconds> [ \"string\" [speed"
#ifdef SLIP
                    "|ipaddress"
#endif
                    "] ]",
    NULLCHAR,   NULLFP((int,char**,void*)), 0, 0, "Unknown command",
};
  
  
/* dial <iface> <filename> [ <seconds> [ <pings> [<hostid>] ] ]
 *  <iface>     must be asy type
 *  <filename>  contains commands which are executed.
 *          missing: kill outstanding dialer.
 *  <seconds>   interval to check for activity on <iface>.
 *  <pings>     number of missed pings before redial.
 *  <hostid>    interface to ping.
 */
int
dodialer(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp;
    struct asy *ap;
    int32 interval = 0L;        /* in seconds */
    int32 last_wait = 0L;
    int32 target = 0L;
    int pings = 0;
    int countdown;
    char *filename;
    char *ifn;
    int result;
    int s;
  
    if((ifp = if_lookup(argv[1])) == NULLIF){
        tprintf(Badinterface,argv[1]);
        return 1;
    }
    if( ifp->dev >= ASY_MAX || Asy[ifp->dev].iface != ifp ){
        tprintf("Interface %s not asy port\n",argv[1]);
        return 1;
    }
  
    if(ifp->supv != NULLPROC){
        while ( ifp->supv != NULLPROC ) {
            alert(ifp->supv, EABORT);
            pwait(NULL);
        }
        tprintf("dialer terminated on %s\n",argv[1]);
    }
  
    if ( argc < 3 ) {
        /* just terminating */
        return 0;
    }
  
    chname( Curproc, ifn = if_name( ifp, " dialer" ) );
    free( ifn );
    filename = argv[2];
#ifdef UNIX
    if (argv[2][0] != '/')
#else
    if ((argv[2][0] != '/') && (argv[2][0] != '\\') && (argv[2][1] != ':'))
#endif
        filename = rootdircat(argv[2]);
  
    /* handle minimal command (just thru filename) */
    if ( argc < 4 ) {
        /* just dialing */
        result = redial(ifp, filename);
  
        if ( filename != argv[2] )
            free(filename);
        return result;
  
    /* get polling interval (arg 3) */
	/* 11Oct2009, Maiko, Use atoi() and "%d" for int32 vars */
    } else if ( (interval = atoi(argv[3])) <= MIN_INTERVAL ) {
        tprintf("interval must be > %d seconds\n", MIN_INTERVAL);
        return 1;
    }
  
    /* get the number of pings before redialing (arg 4) */
    if ( argc < 5 ) {
    } else if ( (pings = atoi(argv[4])) <= 0 ){
        tprintf("pings must be > 0\n");
        return 1;
    }
  
    /* retrieve the host name (arg 5) */
    if ( argc < 6 ) {
    } else if ( (target = resolve(argv[5])) == 0L ) {
        tprintf(Badhost,argv[5]);
        return 1;
    }
  
    countdown = pings;
    ifp->supv = Curproc;
    ap = &Asy[ ifp->dev ];
  
    while ( !main_exit ) {
        int32 wait_for = interval;
#ifdef UNIX
        int ctlbits;

        if (ap->flags&ASY_CARR && !ioctl(ap->fd, TIOCMGET, &ctlbits) && !(ctlbits&TIOCM_CAR)) {
#else
        if ( !(ap->dtr_usage&FOUND_UP) || !(ap->rlsd_line_control&FOUND_UP) ) {
#endif
            /* definitely down */
            if ( redial(ifp,filename) < 0 )
                break;
        } else
            if ( ifp->lastrecv >= last_wait ) {
            /* got something recently */
                wait_for -= secclock() - ifp->lastrecv;
                if (wait_for < 0) wait_for=interval;  /* n5knx: for safety */
                countdown = pings;
            } else if ( countdown < 1 ) {
            /* we're down, or host we ping is down */
                if ( redial(ifp,filename) < 0 )
                    break;
                countdown = pings;
            } else if ( target != 0L
            && (s = j2socket(AF_INET,SOCK_RAW,ICMP_PTCL)) != -1 ) {
                pingem(s,target,0,0,0);  /* k2mf: use 0 so we never print an icmp-reply */
                close_s(s);
                countdown--;
            } else if ( ifp->echo != NULLFP((struct iface*,struct mbuf*)) ) {
                (*ifp->echo)(ifp,NULLBUF);
                countdown--;
            }
  
        last_wait = secclock();
        if ( wait_for != 0L ) {
            j2alarm( wait_for * 1000 );
            if ( pwait( &(ifp->supv) ) == EABORT )
                break;
            j2alarm(0);      /* clear alarm */
        }
    }
  
    if ( filename != argv[2] )
        free(filename);
    ifp->supv = NULLPROC;   /* We're being terminated */
    return 0;
}
  
  
/* execute dialer commands
 * returns: -1 fatal error, 0 OK, 1 try again
 */
static int
redial( ifp, file )
struct iface *ifp;
char *file;
{
    char *inbuf, *intmp;
    FILE *fp;
    int (*rawsave) __ARGS((struct iface *,struct mbuf *));
    struct session *sp = NULLSESSION;
    int result = 0;
    int LineCounter = 0;
    int save_input = Curproc->input;
    int save_output = Curproc->output;
  
    /* Can't proceed if tip or we already took over output */
    if(ifp->raw == bitbucket){
        tprintf("redial: tip or dialer already active on %s\n",ifp->name);
        return -1;
    }
  
    if((fp = fopen(file,READ_TEXT)) == NULLFILE){
        tprintf("redial: can't read %s\n",file);
        return -1;  /* Causes dialer proc to terminate */
    }

    /* allocate a session descriptor unless we aren't in verbose mode */
    if (Verbose) {
        if ( (sp = newsession( ifp->name, DIAL, 0 )) == NULLSESSION ) {
            j2tputs(TooManySessions);
            fclose(fp);
            return -1;
        }
        tprintf( "Dialing on %s\n\n", ifp->name );
    }
  
    /* Save output handler and temporarily redirect output to null */
    rawsave = ifp->raw;
    ifp->raw = bitbucket;
  
    /* Suspend the packet input driver. Note that the transmit driver
     * is left running since we use it to send buffers to the line.
     */
    suspend(ifp->rxproc);
  
#ifdef notdef
    tprintf("rlsd: 0x%02x, dtr: 0x%02x\n",
    Asy[ifp->dev].rlsd_line_control,
    Asy[ifp->dev].dtr_usage );
#endif
  
    Failed = 0;
    Depth = 0;
    Skip[0] = 0;
    SkipOverride = -1;
/*  Verbose = 1;  LET verbose setting persist across dial attempts */
  
    inbuf = mallocw(BUFSIZ);
    intmp = mallocw(BUFSIZ);
    while ( fgets( inbuf, BUFSIZ, fp ) != NULLCHAR ) {
        LineCounter++;
        strcpy(intmp,inbuf);
        rip( intmp );
#ifdef DIALDEBUG
        log( -1, "%s dialer: %s", ifp->name, intmp );
#endif
        if( (result = cmdparse(dial_cmds,inbuf,ifp)) != 0 ){
            if (Failmode)
                Failed = 1;
            else
            {
                tprintf("input line #%d: %s",LineCounter,intmp);
#ifndef DIALDEBUG
                log( -1, "%s dialer: Failure at line %d: %s", ifp->name, LineCounter, intmp );
#endif
                break;
            }
        }
        else
            Failed = 0;
        if (Depth == -1)
            break;
        if (SkipOverride != -1)
        {
            Skip[OverrideDepth] = SkipOverride;
            SkipOverride = -1;
        }
    }
    if (Depth > 0)
        tprintf("Warning: %d unmatched `begin's in command file\n", Depth);
    free(inbuf);
    free(intmp);
    fclose(fp);
  
    if ( result == 0 ) {
        ifp->lastsent = ifp->lastrecv = secclock();
    }
  
    ifp->raw = rawsave;
    resume(ifp->rxproc);
    if (Verbose) tprintf( "\nDial %s complete\n", ifp->name );
  
    /* Wait for a while, so the user can read the screen */
    j2pause(2000);
    if (sp) {  /* since Verbose can change state...*/
        freesession( sp );
        Curproc->input = save_input;
        Curproc->output = save_output;
    }
    return result;
}
  
static int
dodial_control(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
    int32 val=0;
    int param;
  
    if (Skip[Depth])
        return 0;
 
	/* 11Oct2009, Maiko, Use atoi() and "%d" for int32 vars */
    if (argc>2) val=atoi(argv[2]);
    if (Verbose) tprintf("control %s %d\n", argv[1], val);
    if ( ifp->ioctl == NULL )
        return -1;
  
    if ( (param = devparam( argv[1] )) == -1 )
        return -1;
  
    (*ifp->ioctl)( ifp, param, TRUE, val );

    return 0;
}
  
  
static int
dodial_send(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
    struct mbuf *bp;
  
    if (Skip[Depth])
        return 0;
  
    if (Verbose) tprintf("send <%s>\n", argv[1]);
    if(argc > 2){
        /* Send characters with inter-character delay
         * (for dealing with prehistoric Micom switches that
         * can't take back-to-back characters...yes, they
         * still exist.)
         */
        char *cp;
        int32 cdelay = atoi(argv[2]);	/* alarm bell */
  
        for(cp = argv[1];*cp != '\0';cp++){
            bp = qdata(cp,1);
            asy_send(ifp->dev,bp);
            j2pause(cdelay);
        }
    } else {
        bp = qdata( argv[1], (int16)strlen(argv[1]) );
  
        if (ifp->trace & IF_TRACE_RAW)
            raw_dump( ifp, IF_TRACE_OUT, bp );
        asy_send( ifp->dev, bp );
    }
    return 0;
}
  
  
static int
dodial_speed(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    if (Skip[Depth])
        return 0;
  
    if ( argc < 2 ) {
        tprintf( "current speed = %lu bps\n", Asy[ifp->dev].speed );
        return 0;
    }
    if (Verbose) tprintf("speed %ld\n", atol(argv[1]));
    return asy_speed( ifp->dev, atol( argv[1] ) );
}
  
  
static int
dodial_status(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
    int32 val=0;
    int param;
  
    if (Skip[Depth])
        return 0;
 
    if ( ifp->iostatus == NULL )
        return -1;
  
    if ( (param = devparam( argv[1] )) == -1 )
        return -1;
 
	/* 11Oct2009, Use atoi() for int32 vars */ 
    if (argc>2) val=atoi(argv[2]);

    (*ifp->iostatus)( ifp, param, val );

    return 0;
}
  
  
static int
dodial_wait(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
    register int c = -1;
  
    if (Skip[Depth])
        return 0;
  
    j2alarm( atoi( argv[1] ) );	/* alarm bell */
  
    if ( argc == 2 ) {
        if (Verbose) {
            tprintf("wait %ld\nimsg <", atol(argv[1]));
            tflush();
        }
        while ( (c = get_asy(ifp->dev)) != -1 ) {
            if (Verbose) tputc( c &= 0x7F );
            if (c == '\n') pwait(NULL);  /* let output procs run */
        }
        j2alarm( 0 );
        if (Verbose) tprintf(">\n");
        return 0;
    } else {
        register char *cp = argv[2];
  
        if (Verbose) {
            tprintf("waitfor <%s>\nimsg <", argv[2]);
            tflush();
        }
        while ( *cp != '\0'  &&  (c = get_asy(ifp->dev)) != -1 ) {
            if (Verbose) tputc( c &= 0x7F );
            if (c == '\n') pwait(NULL);  /* let output procs run */
  
            if (*cp++ != c) {
                cp = argv[2];
            }
        }
  
        if ( argc > 3 && c != -1) {
            if ( stricmp( argv[3], "speed" ) == 0
#ifdef SLIP
                 || stricmp( argv[3], "ipaddress" ) == 0
#endif
               ){
                int32 speed = 0;
#ifdef SLIP
                int32 ipaddress = 0;
                int getipaddr = 0;

                if (tolower(*argv[3]) == 'i') getipaddr=1;
#endif
                while ( (c = get_asy(ifp->dev)) != -1 ) {
                    if (Verbose) {
                        tputc( c &= 0x7F );
                        tflush();
                    }
  
                    if ( isdigit(c) ) {
                        speed *= 10;
                        speed += c - '0';
                    } else {
#ifdef SLIP
                        if (getipaddr) {
                            switch(c) {
                            case '.':
                                ipaddress = (ipaddress<<8) | speed;
                                ++getipaddr;
                                speed = 0;
                                break;
                            default:
                                if(getipaddr==4) {
                                    j2alarm(0);
                                    ifp->addr = (ipaddress<<8) | speed;
                                    if (Verbose) tprintf("> ok\n");
                                    return 0;
                                }
                                else if (getipaddr != 1) {  /* trashed addr */
                                    j2alarm(0);
                                    j2tputs("> bad ipaddress syntax\n");
                                    return 1;
                                }
                                /* else ignore trash preceding ipaddress */
                            }
                        } else
#endif
                        {
                            j2alarm( 0 );
                            if (Verbose) tprintf("> ok\n");
                            return asy_speed( ifp->dev, speed );
                        }
                    }
                }
            } else {
                j2tputs("> bad command syntax\n");
                return -1;
            }
        }
    }
    j2alarm( 0 );
    if (Verbose) tprintf("> %s\n", (c == -1? "failed": "ok"));
    return ( c == -1 );
}
  
static int
dodial_failmode(argc, argv, p)
int argc;
char **argv;
void *p;
{
    if (Skip[Depth])
        return 0;
    if (Verbose) tprintf("failmode %s\n", argv[1]);
    return setbool(&Failmode, "Continue on dial command failure", argc, argv);
}
  
static int
dodial_iffail(argc, argv, p)
int argc;
char **argv;
void *p;
{
    if (!Skip[Depth])
    {
        if (SkipOverride == -1)
        {
            SkipOverride = Skip[Depth];
            OverrideDepth = Depth;
        }
        Skip[Depth] = !Failed;
    }
    return cmdparse(dial_cmds, argv[1], p);
}
  
static int
dodial_ifok(argc, argv, p)
int argc;
char **argv;
void *p;
{
    if (!Skip[Depth])
    {
        if (SkipOverride == -1)
        {
            SkipOverride = Skip[Depth];
            OverrideDepth = Depth;
        }
        Skip[Depth] = Failed;
    }
    return cmdparse(dial_cmds, argv[1], p);
}
  
static int
dodial_begin(argc, argv, p)
int argc;
char **argv;
void *p;
{
    if (Depth == MAXDEPTH)
    {
        Depth = -1;
        j2tputs("dialer: Blocks nested too deeply\n");
        return -1;
    }
    if (!Skip[Depth] && Verbose)
        j2tputs("begin\n");
    Skip[Depth + 1] = Skip[Depth];
    Depth++;
    return 0;
}
  
static int
dodial_end(argc, argv, p)
int argc;
char **argv;
void *p;
{
    if (!Skip[Depth] && Verbose)
        j2tputs("end\n");
    if (Depth-- == 0)
    {
        j2tputs("dialer: `end' without `begin'\n");
        return -1;
    }
    return 0;
}
  
static int
dodial_exit(argc, argv, p)
int argc;
char **argv;
void *p;
{
    if (Skip[Depth])
        return 0;
    if (Verbose) {
        if (argc > 1)
            tprintf("exit %d\n", atoi(argv[1]));
        else
            j2tputs("exit\n");
    }
    Depth = -1;
    return (argc > 1? atoi(argv[1]): Failed);
}
  
static int
dodial_verbose(argc, argv, p)
int argc;
char **argv;
void *p;
{
    if (setbool(&Verbose, "Verbose progress reports", argc, argv))
        return 1;  /* not set */
    if (Verbose)
        tprintf("verbose on\n");
    return 0;
}
  
/*
 * cmdparse sends blank lines to the first command, sigh
 */
  
static int
dodial_nothing(argc, argv, p)
int argc;
char **argv;
void *p;
{
    return 0;
}
#endif /* DIALER */
  
