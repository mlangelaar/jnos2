/* Packet tracing - top level and generic routines, including hex/ascii
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Mods by G1EMM
 *
 * Tracing to session taken from WNOS3, by Johan. K. Reinalda, WG7J
 */
/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
#include <ctype.h>
#include <time.h>
#include "global.h"
#include "iface.h"
#include "socket.h"  
#ifdef TRACE
#ifdef ANSIPROTO
#include <stdarg.h>
#endif
#include "mbuf.h"
#include "pktdrvr.h"
#include "commands.h"
#include "session.h"
#include "trace.h"
#include "cmdparse.h"
#include "slip.h"
#include "devparam.h"
  
#ifdef J2_TRACESYNC
#include "usock.h"
#endif

#ifdef UNIX
/* #define usprintf traceusprintf  [obsoleted by new code in usputs...n5knx] */
/* Therefore, use usprintf or usputs for all trace writes; NEVER use usputc */
#endif
  
#ifdef MONITOR
static void plain_dump __ARGS((int s, struct mbuf **bpp));
#endif
static void ascii_dump __ARGS((int s,struct mbuf **bpp));
static void hex_dump __ARGS((int s,struct mbuf **bpp));
/*static*/ void showtrace __ARGS((struct iface *ifp));
extern struct session *Current;
extern struct session *Command;
#ifdef MULTITASK
extern int Nokeys;
#endif
  
extern int TraceColor;
extern int Tracesession;
extern struct session *Trace;
int LocalTraceColor;
extern int stdoutSock;  

#ifdef MONITOR
int Trace_compact_header = 0;
static char *kissname __ARGS((struct iface *ifp,struct mbuf *bp,int type));
  
static char *
kissname(ifp, bp, type)
struct iface *ifp;
struct mbuf *bp;
int type;
{
    int port;
  
    if (ifp->type != CL_AX25 || type != CL_KISS)
        return ifp->name;
    port = (bp->data[0] & 0xF0) >> 4;
    if (Slip[ifp->xdev].kiss[port] == NULLIF)
        return ifp->name;
    return Slip[ifp->xdev].kiss[port]->name;
}
  
#endif
  
int
dostrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(Trace == NULLSESSION)
        argc = 0; /* No session setup, so don't allow turning it on ! */
    return setbool(&Tracesession,"Trace to session",argc,argv);
}
  
/* Redefined here so that programs calling dump in the library won't pull
 * in the rest of the package
 */
  
struct tracecmd Tracecmd[] = {
    { "input",        IF_TRACE_IN,    IF_TRACE_IN },
    { "-input",       0,              IF_TRACE_IN },
    { "output",       IF_TRACE_OUT,   IF_TRACE_OUT },
    { "-output",      0,              IF_TRACE_OUT },
    { "broadcast",    0,              IF_TRACE_NOBC },
    { "-broadcast",   IF_TRACE_NOBC,  IF_TRACE_NOBC },
    { "raw",          IF_TRACE_RAW,   IF_TRACE_RAW },
    { "-raw",         0,              IF_TRACE_RAW },
    { "ascii",        IF_TRACE_ASCII, IF_TRACE_ASCII|IF_TRACE_HEX },
    { "-ascii",       0,              IF_TRACE_ASCII|IF_TRACE_HEX },
    { "hex",          IF_TRACE_HEX,   IF_TRACE_ASCII|IF_TRACE_HEX },
    { "-hex",         IF_TRACE_ASCII, IF_TRACE_ASCII|IF_TRACE_HEX },
#ifdef MONITOR
/* borrow a meaningless combination for the new trace type */
#define IF_TRACE_PLAIN (IF_TRACE_ASCII|IF_TRACE_HEX)
    { "monitor",  IF_TRACE_PLAIN, IF_TRACE_ASCII|IF_TRACE_HEX },
    { "-monitor", IF_TRACE_ASCII, IF_TRACE_ASCII|IF_TRACE_HEX },
#endif
#ifdef POLLEDKISS
    { "polls",        IF_TRACE_POLLS, IF_TRACE_POLLS },
    { "-polls",       0,              IF_TRACE_POLLS },
#endif
    { "off",          0,              0xffff },
    { NULLCHAR,       0,              0 }
};
  
void
dump(ifp,direction,type,bp)
register struct iface *ifp;
int direction;
unsigned type;
struct mbuf *bp;
{
    struct mbuf *tbp;
    void (*func) __ARGS((int,struct mbuf **,int));
    int16 size;
    time_t timer;
    char *cp;
  
#ifdef KISS /* Let's straighten out this multiport tracing - K5JB */
    if(type == CL_KISS){  /* I don't think we will see CL_AX25 */
        int port;    /* don't need this but it improves readability */
        struct iface *kifp;

        port = (bp->data[0] & 0xF0) >> 4;
        if((kifp = Slip[ifp->xdev].kiss[port]) != NULLIF)
            ifp = kifp;

#ifdef POLLEDKISS
        if(Slip[ifp->xdev].polled && !(ifp->trace & IF_TRACE_POLLS)
           && bp->cnt==2 && (*bp->data & 0x0f)==PARAM_POLL)
            return;
#endif
    }
#endif

    if(ifp == NULL || (ifp->trace & direction) == 0)
        return; /* Nothing to trace */
  
#ifdef UNIX
#ifndef HEADLESS
	/* need to check if the traced-to session is a "blocking" session */
	if (sm_blocked(Tracesession? Trace: Command))
	    return;
#endif
#else
    /* N.B. Linux version can keep the trace at all times. */
    if(Tracesession) {
    /* Disable trace if this is not Trace-sessions,
     * or when shelled out, and not tracing to file */
#ifdef MULTITASK
        if((Current != Trace || Nokeys) && (ifp->trsock == stdoutSock))
#else
            if((Current != Trace) && (ifp->trsock == stdoutSock))
#endif /* MULTITASK */
                return; /* Nothing to trace */
    } else {
    /* Disable trace on non-command sessions or when shelled out */
#ifdef MULTITASK
        if((Current != Command || Nokeys) && (ifp->trsock == stdoutSock))
#else
           if((Current != Command) && (ifp->trsock == stdoutSock))
#endif
                return; /* Nothing to trace */
    }
#endif /* UNIX */
  
    time(&timer);
    cp = ctime(&timer);
  
    /* G8ECJ: only stdoutSock(==local console) can have colors unless bit 2^2 set */
    LocalTraceColor = (ifp->trsock != stdoutSock && TraceColor < 3) ? 0 : TraceColor&3;
    switch(direction){
        case IF_TRACE_IN:
            if((ifp->trace & IF_TRACE_NOBC)
                && (Tracef[type].addrtest != NULLFP((struct iface*,struct mbuf*)))
                && (*Tracef[type].addrtest)(ifp,bp) == 0)
                return;         /* broadcasts are suppressed */
            if (LocalTraceColor == 1)            /* PE1DGZ */                
                usprintf(ifp->trsock,"\33[37m");
            else if (LocalTraceColor == 2)
                usprintf(ifp->trsock,"\33[32m");
                    
#ifdef MONITOR
            if ((ifp->trace & IF_TRACE_PLAIN) == IF_TRACE_PLAIN)
#ifdef MONSTAMP
                usprintf(ifp->trsock, "\n%s recv: %.24s\n", kissname(ifp, bp, type),cp);
#else
                usprintf(ifp->trsock, "(%s) ", kissname(ifp, bp, type));
#endif /* MONSTAMP */
            else
#endif
                usprintf(ifp->trsock,"\n%.24s - %s recv:\n",cp,ifp->name);
            if (LocalTraceColor)            /* PE1DGZ */
                usprintf(ifp->trsock,"\33[37m");
            break;
        case IF_TRACE_OUT:
            if (LocalTraceColor == 1)            /* PE1DGZ */                
                usprintf(ifp->trsock,"\33[1;37m");
            else if (LocalTraceColor == 2)
                usprintf(ifp->trsock,"\33[31m");
#ifdef MONITOR
            if ((ifp->trace & IF_TRACE_PLAIN) == IF_TRACE_PLAIN)
#ifdef MONSTAMP
                usprintf(ifp->trsock, "\n%s sent: %.24s\n", kissname(ifp, bp, type),cp);
#else
                usprintf(ifp->trsock, "(%s) ", kissname(ifp, bp, type));
#endif /* MONSTAMP */
            else
#endif
                usprintf(ifp->trsock,"\n%.24s - %s sent:\n",cp,ifp->name);
            if (LocalTraceColor)            /* PE1DGZ */
                usprintf(ifp->trsock,"\33[0;37m");
            break;
    }
    if(bp == NULLBUF || (size = len_p(bp)) == 0){
        usprintf(ifp->trsock,"empty packet!!\n");
        usflush(ifp->trsock);   /* Needed for remote mailbox tracing - WG7J */
        return;
    }
  
    if(type < NCLASS)
        func = Tracef[type].tracef;
    else
        func = NULLVFP((int,struct mbuf**,int));
  
    dup_p(&tbp,bp,0,size);
    if(tbp == NULLBUF){
        usprintf(ifp->trsock,Nospace);
        usflush(ifp->trsock);   /* Needed for remote mailbox tracing - WG7J */
        return;
    }
#ifdef MONITOR
    Trace_compact_header = ((ifp->trace&IF_TRACE_PLAIN) == IF_TRACE_PLAIN);
#endif
    if(func != NULLVFP((int,struct mbuf**,int)))
        (*func)(ifp->trsock,&tbp,1);
#ifdef MONITOR
    if ((ifp->trace & IF_TRACE_PLAIN) == IF_TRACE_PLAIN)
        plain_dump(ifp->trsock, &tbp);
    else
#endif
        if(ifp->trace & IF_TRACE_ASCII){
        /* Dump only data portion of packet in ascii */
            ascii_dump(ifp->trsock,&tbp);
        } else if(ifp->trace & IF_TRACE_HEX){
        /* Dump entire packet in hex/ascii */
            free_p(tbp);
            dup_p(&tbp,bp,0,len_p(bp));
            if(tbp != NULLBUF)
                hex_dump(ifp->trsock,&tbp);
            else
                usprintf(ifp->trsock,Nospace);
        }
    free_p(tbp);
    usflush(ifp->trsock);   /* Needed for remote mailbox tracing - WG7J */
}
  
/* Dump packet bytes, no interpretation */
void
raw_dump(ifp,direction,bp)
struct iface *ifp;
int direction;
struct mbuf *bp;
{
    struct mbuf *tbp;
  
    /* Dump entire packet in hex/ascii */
    usprintf(ifp->trsock,"\n******* raw packet dump (%s %s)\n",
    ((direction & IF_TRACE_OUT) ? "send" : "recv"),ifp->name);
    dup_p(&tbp,bp,0,len_p(bp));
    if(tbp != NULLBUF)
        hex_dump(ifp->trsock,&tbp);
    else
        usprintf(ifp->trsock,Nospace);
    usprintf(ifp->trsock,"*******\n");
    free_p(tbp);
    return;
}
  
/* Dump an mbuf in hex */
static void
hex_dump(s,bpp)
int s;
register struct mbuf **bpp;
{
    int16 n;
    int16 address;
    char buf[16];
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;
  
    address = 0;
    while((n = pullup(bpp,buf,sizeof(buf))) != 0){
        fmtline(s,address,buf,n);
        address += n;
    }
}
/* Dump an mbuf in ascii */
static void
ascii_dump(s,bpp)
int s;
register struct mbuf **bpp;
{
    int c;
    register int16 tot;
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;
  
    tot = 0;
    if (LocalTraceColor)    /* PE1DGZ */
        usprintf(s,"\33[36m");
    while((c = PULLCHAR(bpp)) != -1){
        if((tot % 64) == 0)
            usprintf(s,"%04x  ",tot);
        usprintf(s,"%c",(isprint(uchar(c)) ? c : '.'));
        if((++tot % 64) == 0)
            usprintf(s,"\n");
    }
    if((tot % 64) != 0)
        usprintf(s,"\n");
    if (LocalTraceColor)    /* PE1DGZ */
        usprintf(s,"\33[37m");
}
  
#ifdef MONITOR
/* Dump an mbuf in ascii with newlines but no others. */
/* Actually, we do limited VT100 parsing, since that seems popular here */
static void
plain_dump(s,bpp)
int s;
register struct mbuf **bpp;
{
    struct mbuf *tmp;
    int c, esc, nl;
  
    if (bpp == NULLBUFP || *bpp == NULLBUF)
        return;
  
    /* check for lots of non-ASCII, non-VT100 and ascii_dump instead? */
    dup_p(&tmp, *bpp, 0, len_p(*bpp));
    nl = 0;
    while ((c = PULLCHAR(&tmp)) != -1)
    {
        /*
         * Printable characters are okay, as are \n \t \r \b \f \a \E
         * Nulls and other control characters are verboten, as are meta
         * controls.  Meta-printables are accepted, since they may be
         * intended as PC graphics (but don't expect them to dump right
         * from here because I don't decode them.  Maybe someday).
         */
        if (c < 8 || (c > 13 && c < 26) || (c > 27 && c < 32) ||
            (c > 126 && c < 174) || c > 223)
            nl = 1;
    }
    if (nl)
    {
        ascii_dump(s, bpp);
        return;
    }
    esc = 0;
    nl = 1;
    if (LocalTraceColor)    /* PE1DGZ */
        usprintf(s, "\33[36m");
    while ((c = PULLCHAR(bpp)) != -1)
    {
        if (c == 0x1B)
            esc = !esc;
        else if (esc == 1 && c == '[')
            esc = 2;
        else if (esc == 1)
            esc = 0;
        else if (esc == 2 && c != ';' && !isdigit(c))
        {
          /* handle some common cases? */
            esc = 0;
        }
        else if (esc == 0 && c == '\r')
        {
#ifdef UNIX
            usputs(s, "\n");
#else
            usputc(s, '\n');
#endif
            nl = 1;
        }
      /* safe programming: not everyone *always* agrees on isprint */
        else if (esc == 0 && c != '\n' && (isprint(c) || c == '\t'))
        {
#ifdef UNIX
            usprintf(s, "%c", c);
#else
            usputc(s, c);
#endif
            nl = 0;
        }
    }
    if (!nl)
#ifdef UNIX
        usputs(s, "\n");
#else
        usputc(s, '\n');
#endif
    if (LocalTraceColor)    /* PE1DGZ */
        usprintf(s, "\33[37m");
}
#endif
  
#ifdef	J2_TRACESYNC

/*
 * 04Nov2010, Maiko, Sync the trace socket every few seconds, this is
 * something that should have been added years ago. I hate having to wait
 * for trace files to 'sync up', sometimes it takes minutes, even hours to
 * do so. I should thank N6MEF (Michael Fox) for pushing me to do this.
 */

static void synctrace (void *t)
{
	register struct iface *ifp;

	if (t == NULL) return;

	ifp = (struct iface*)t;

	stop_timer (&ifp->trsynct);	/* just in case */

	if (ifp->trsock != -1 )
	{
		register struct usock *up;

		// log (ifp->trsock, "sync");

		usflush (ifp->trsock);	/* flush JNOS buffers first */

		if ((up = itop (ifp->trsock)) == NULLUSOCK)
			return;

		fflush (up->cb.fp);	/* now flush the actual OS buffers */

		start_timer (&ifp->trsynct);
	}
}

#endif
  
/* Modify or displace interface trace flags */
int
dotrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp;
    struct tracecmd *tp;
  
    if(argc < 2){
        for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
            showtrace(ifp);
        return 0;
    }
    if((ifp = if_lookup(argv[1])) == NULLIF){
        tprintf(Badinterface,argv[1]);
        return 1;
    }
    if(argc == 2){
        showtrace(ifp);
        return 0;
    }
    /* MODIFY THIS TO HANDLE MULTIPLE OPTIONS */
    if(argc >= 3)
	{
#ifdef	J2_TRACESYNC
	/* 05Nov2010, Maiko, Make sure we stop timer or else we crash !!! */
		if (ifp->trsynci)
		{
			stop_timer (&ifp->trsynct);
			ifp->trsynci = 0;
		}
#endif
        for(tp = Tracecmd;tp->name != NULLCHAR;tp++)
            if(strncmp(tp->name,argv[2],strlen(argv[2])) == 0)
                break;
        if(tp->name != NULLCHAR)
            ifp->trace = (ifp->trace & ~tp->mask) | tp->val;
        else
            ifp->trace = htoi(argv[2]);
    }
    /* Always default to stdout unless trace file is given */
    if(ifp->trsock != stdoutSock)
        close_s(ifp->trsock);
    ifp->trsock = stdoutSock;
    if(ifp->trfile != NULLCHAR)
        free(ifp->trfile);
    ifp->trfile = NULLCHAR;
  
    if(argc >= 4){
        if((ifp->trsock = sockfopen(argv[3],APPEND_TEXT)) == -1){
            tprintf("Can't open socket for %s\n",argv[3]);
            ifp->trsock = stdoutSock;
        } else {
            ifp->trfile = j2strdup(argv[3]);
        }
#ifdef	J2_TRACESYNC
		if (argc >= 5)
		{
			int tval = atoi(argv[4]);

			/* start a timer to sync on regular basis, if user asks to do so */
			if (tval)
			{
				/* 05Nov2010, Maiko, Not a bad idea to warn the user */
				if (tval < 1000)
					j2tputs ("warning - your sync is under one second !!!\n");

				ifp->trsynci = tval;

				log (ifp->trsock, "sync every %d ms", ifp->trsynci);

				ifp->trsynct.func = (void (*)(void*)) synctrace;

				ifp->trsynct.arg = ifp;		/* need to know which interface */

                set_timer (&ifp->trsynct, tval);

				start_timer (&ifp->trsynct);
        	}
		}
#endif
    } else {
        /* If not from the console, trace to the current output socket! */
        if(Curproc->input != Command->input) {
            /* this comes from a remote connection, ie a user in
             * 'sysop' mode on the bbs or maybe in a TRACESESSION
             */
            ifp->trsock = Curproc->output;
            /* make sure stopping trace doesn't kill connection */
            usesock(ifp->trsock);
        }
    }
    showtrace(ifp);
    return 0;
}
/* Display the trace flags for a particular interface */
/*static*/ void
showtrace(ifp)
register struct iface *ifp;
{
    if(ifp == NULLIF)
        return;
    tprintf("%s:",ifp->name);
    if(ifp->trace & (IF_TRACE_IN | IF_TRACE_OUT | IF_TRACE_RAW)){
        if(ifp->trace & IF_TRACE_IN)
            j2tputs(" input");
        if(ifp->trace & IF_TRACE_OUT)
            j2tputs(" output");
  
        if(ifp->trace & IF_TRACE_NOBC)
            j2tputs(" - no broadcasts");
  
#ifdef POLLEDKISS
        if(ifp->trace & IF_TRACE_POLLS)
            j2tputs(" - pkiss polls");
#endif
  
#ifdef MONITOR
        if ((ifp->trace & IF_TRACE_PLAIN) == IF_TRACE_PLAIN)
            tprintf(" (Monitoring)");
        else
#endif
            if(ifp->trace & IF_TRACE_HEX)
                j2tputs(" (Hex/ASCII dump)");
            else if(ifp->trace & IF_TRACE_ASCII)
                j2tputs(" (ASCII dump)");
            else
                j2tputs(" (headers only)");
  
        if(ifp->trace & IF_TRACE_RAW)
            j2tputs(" Raw output");
  
        if(ifp->trfile != NULLCHAR)
            tprintf(" trace file: %s",ifp->trfile);

#ifdef J2_TRACESYNC
	if (ifp->trsynci)
            tprintf(" sync: %d ms", ifp->trsynci);
#endif
        tputc('\n');
    } else
        j2tputs(" tracing off\n");
}
  
/* shut down all trace files */
void
shuttrace()
{
    struct iface *ifp;
  
    for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next){
        if(ifp->trsock != stdoutSock)
            close_s(ifp->trsock);
        if(ifp->trfile != NULLCHAR)
            free(ifp->trfile);
        ifp->trfile = NULLCHAR;
        ifp->trsock = -1;
    }
}
#endif /*TRACE*/
  
#ifdef PPP
/* Log messages of the form
 * Tue Jan 31 00:00:00 1987 44.64.0.7:1003 open FTP
 */
#if     defined(ANSIPROTO)
void
trace_log(struct iface *ifp,char *fmt, ...)
{
    va_list ap;
    char *cp;
    long t;
  
    if(ifp->trsock == -1)
        return;
  
    time(&t);
    cp = ctime(&t);
    usprintf(ifp->trsock,"%.24s",cp);
  
    usprintf(ifp->trsock," - ");
    va_start(ap,fmt);
    usvprintf(ifp->trsock,fmt,ap);
    va_end(ap);
    usprintf(ifp->trsock,"\n");
}
#else
/*VARARGS2*/
void
trace_log(ifp,fmt,arg1,arg2,arg3,arg4,arg5)
struct iface *ifp;
char *fmt;
int arg1,arg2,arg3,arg4,arg5;
{
    char *cp;
    long t;
  
    if(ifp->trsock == -1)
        return;
  
    time(&t);
    cp = ctime(&t);
    usprintf(ifp->trsock,"%.24s",cp);
  
    usprintf(ifp->trsock," - ");
    usprintf(ifp->trsock,fmt,arg1,arg2,arg3,arg4,arg5);
    usprintf(ifp->trsock,"\n");
}
#endif
#endif /* PPP */

#if defined(TRACE) || defined(ALLCMD)
static void ctohex __ARGS((char *buf,int16 c));

/* Print a buffer up to 16 bytes long in formatted hex with ascii
 * translation, e.g.,
 * 0000: 30 31 32 33 34 35 36 37 38 39 3a 3b 3c 3d 3e 3f  0123456789:;<=>?
 */
void
fmtline(s,addr,buf,len)
int s;
int16 addr;
char *buf;
int16 len;
{
    char line[80];
    register char *aptr,*cptr;
    register char c;
  
    memset(line,' ',sizeof(line));
    ctohex(line,(int16)hibyte(addr));
    ctohex(line+2,(int16)lobyte(addr));
    aptr = &line[6];
    cptr = &line[55];
    while(len-- != 0){
        c = *buf++;
        ctohex(aptr,(int16)uchar(c));
        aptr += 3;
        c &= 0x7f;
        *cptr++ = isprint(uchar(c)) ? c : '.';
    }
    *cptr++ = '\n';
    *cptr++ = '\0';
    usputs(s,line);
}
/* Convert byte to two ascii-hex characters */
static void
ctohex(buf,c)
register char *buf;
register int16 c;
{
    static char hex[] = "0123456789abcdef";
  
    *buf++ = hex[hinibble(c)];
    *buf = hex[lonibble(c)];
}
#endif /* TRACE || ALLCMD */
