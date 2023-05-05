/* ICMP-related user commands
 * Copyright 1991 Phil Karn, KA9Q
 */
/* Mods by PA0GRI */
#include "global.h"
#include "icmp.h"
#include "ip.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "timer.h"
#include "socket.h"
#include "proc.h"
#include "session.h"
#include "cmdparse.h"
#include "commands.h"
#include "usock.h"

#ifdef	IPV6
/* 05Apr2023, Maiko, Time to support outgoing IPV6 ping */
#include "ipv6.h"
#include "icmpv6.h"
#endif

static int doicmpec __ARGS((int argc, char *argv[],void *p));
static int doicmpstat __ARGS((int argc, char *argv[],void *p));
static int doicmptr __ARGS((int argc, char *argv[],void *p));
static int doicmpquench __ARGS((int argc, char *argv[],void *p));
static int doicmptimeexceed __ARGS((int argc, char *argv[],void *p));
static void pingtx __ARGS((int s,void *ping1,void *p));
static void pinghdr __ARGS((struct session *sp,struct ping *ping));
  
static struct cmds DFAR Icmpcmds[] = {
    { "echo",         doicmpec,       0, 0, NULLCHAR },
    { "quench",       doicmpquench, 0, 0, NULLCHAR },
    { "status",       doicmpstat,     0, 0, NULLCHAR },
    { "timeexceed",   doicmptimeexceed,0,0, NULLCHAR },
    { "trace",        doicmptr,       0, 0, NULLCHAR },
    { NULLCHAR,		NULL,			0,	0, NULLCHAR }
};
  
int Icmp_trace;
int Icmp_SQ = 1;
static int Icmp_echo = 1;
  
int
doicmp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Icmpcmds,argc,argv,p);
}
  
static int
doicmpstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register int i;
    int lim;
  
    /* Note that the ICMP variables are shown in column order, because
     * that lines up the In and Out variables on the same line
     */
    lim = NUMICMPMIB/2;
    for(i=1;i<=lim;i++){
        tprintf("(%2d)%-20s%10d",i,Icmp_mib[i].name, Icmp_mib[i].value.integer);
        tprintf("     (%2d)%-20s%10d\n",i+lim,Icmp_mib[i+lim].name,
        Icmp_mib[i+lim].value.integer);
    }
    return 0;
}
  
static int
doicmptr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2) {
        tprintf("ICMP Tracing is %d\n",Icmp_trace) ;
        return 0 ;
    }
  
    switch(argv[1][0]) {
        case '0':
        case '1':
        case '2': Icmp_trace=atoi(argv[1]);
            break;
        default:
            j2tputs("Trace modes are: 0|1|2\n") ;
            return -1 ;
    }
  
    return 0 ;
}
  
static int
doicmpec(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Icmp_echo,"ICMP echo response accept",argc,argv);
}
  
static int
doicmpquench(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Icmp_SQ,"Mem Low Source Quench",argc,argv);
}
  
int Icmp_timeexceed = 1;
static int
doicmptimeexceed(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Icmp_timeexceed,"Ttl time exceed reply",argc,argv);
}
  
/* Send ICMP Echo Request packets */
int
doping(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct proc *pinger = NULLPROC; /* Transmit process */
#ifdef	IPV6
	/* 05Apr2023, Maiko, It's a peername buffer, not really a 'socket' */
	char from[sizeof(struct j2sockaddr_in6)];
	struct icmpv6 icmpv6;
#else
    struct sockaddr_in from;
#endif
    struct icmp icmp;
    struct mbuf *bp;
    int32 timestamp,rtt,abserr;
    int s,fromlen;
    struct ping ping;
    struct session *sp;
  
    memset((char *)&ping,0,sizeof(ping));
    if(argc > 3) {
        /* Make sure this is a 'one-shot' ping
         * if not coming from the console - WG7J
         */
        if(Curproc->input != Command->input)
            return 0;
	/* 11Oct2009, Maiko, Use atoi for int32 vars */
        ping.interval = atoi(argv[3]);
    }
  
    if(argc > 2)
        ping.len = atoi(argv[2]);
  
    /* Optionally ping a range of IP addresses */
    if(argc > 4)
        ping.incflag = 1;

#ifdef IPV6

	/* 14Apr2023, Maiko, Make resolve6() the first entry point
	 * 16Apr2023, Maiko, but only if IPV6 configured
	 * Note : resolve6() will return no address if not, so it's fine
	 */
	if (ipv6iface)
	{
		tprintf ("Resolving %s (ipv6) ... ", argv[1]);
    		tflush();
	}

	/*
	 * 13Apr2023, Maiko, wrote new resolve6() function in domain.c, so
	 * let's try and use this instead of the previous code
	 */
	copyipv6addr (resolve6 (argv[1]), ping.target6);

	if (ping.target6[0] != 0x00)		/* if resolve fails, goto ipv4 */
	{
    	if ((s = j2socket(AF_INET6, SOCK_RAW, ICMPV6_PTCL)) == -1)
		{
        	j2tputs(Nosock);
        	return 1;
    	}
		/* 07Apr2023, Maiko */
		ping.version = IPV6VERSION;
	}
	else
	{
#endif
    	tprintf("Resolving %s... ",argv[1]);
    	tflush();

    if((ping.target = resolve(argv[1])) == 0){
        tprintf(Badhost,argv[1]);
        return 1;
    }
    if((s = j2socket(AF_INET,SOCK_RAW,ICMP_PTCL)) == -1){
        j2tputs(Nosock);
        return 1;
    }
		/* 07Apr2023, Maiko */
		ping.version = IPVERSION;

#ifdef IPV6
	}
#endif

    if(ping.interval == 0)
	{
        /* One shot ping; let echo_proc hook handle response. */
#ifdef	IPV6
		if (ping.version == IPV6VERSION)	/* 07Apr2023, Maiko */
        	pingem6(s,ping.target6,0,(int16)Curproc->output,ping.len);
		else
#endif
        pingem(s,ping.target,0,(int16)Curproc->output,ping.len);

        close_s(s);
        return 0;
    }

    /* Multi shot ping. Allocate a session descriptor */
    if((sp = ping.sp = newsession(argv[1],PING,0)) == NULLSESSION){
        j2tputs(TooManySessions);
        close_s(s);
        return 1;
    }
  
    sp->s = s;
  
    /* start the continuous pinger process */
    pinger = newproc("pingtx",300,pingtx,s,&ping,NULL,0);
  
    /* Now collect the replies */

    pinghdr(sp,&ping);
    for(;;){
        fromlen = sizeof(from);
        if(recv_mbuf(s,&bp,0,(char *)&from,&fromlen) == -1)
            break;

	// log (-1, "recv_mbuf -> %x %x %x %x", bp->data[0], bp->data[1], bp->data[2], bp->data[3]);

#ifdef	IPV6
	/*
	 * 06Apr2023, Maiko, recv_mbuf does not have IPV6 support
	 * yet, like then send_mbuf which I bypassed in pingem6,
	 * but not sure I can bypass this as easy, so later !
	 */
		if (ping.version == IPV6VERSION)
		{
		/*
		 * 08Apr2023, Maiko, Interesting, the ethernet header
		 * for IPV6 (tap) is 14 bytes and for some reason we
		 * are getting in middle of ipv6 header, and not the
		 * start of icmp data, why ? What did I forget in the
		 * RAW socket type in recv_mbuf() function ? Because
		 * we are using TAP iface, we need to pullup entire
		 * ethernet header BEFORE ntohipv6() in recv_mbuf()
		 * function call - this is now working :]
		 */
       		ntohicmpv6 (&icmpv6, &bp);

        	if(icmpv6.type != ICMPV6_ECHO_REPLY || icmpv6.args.echo.id != s){
            	/* Ignore other people's responses */
            	free_p(bp);
            	continue;
        	}
		}
		else
		{
#endif
        ntohicmp(&icmp,&bp);
        if(icmp.type != ICMP_ECHO_REPLY || icmp.args.echo.id != s){
            /* Ignore other people's responses */
            free_p(bp);
            continue;
        }
#ifdef	IPV6
		}
#endif
        /* Get stamp */
        if(pullup(&bp,(char *)&timestamp,sizeof(timestamp))
        != sizeof(timestamp)){
            /* The timestamp is missing! */
            free_p(bp);     /* Probably not necessary */
            continue;
        }
        free_p(bp);
  
        ping.responses++;
  
        /* Compute round trip time, update smoothed estimates */
        rtt = (int32)(msclock() % MAXINT) - timestamp;
        abserr = (rtt > ping.srtt) ? (rtt-ping.srtt) : (ping.srtt-rtt);
  
        if(ping.responses == 1){
            /* First response, base entire SRTT on it */
            ping.srtt = rtt;
            ping.mdev = 0;
        } else {
            ping.srtt = (7*ping.srtt + rtt + 4) >> 3;
            ping.mdev = (3*ping.mdev + abserr + 2) >> 2;
        }
        if((ping.responses % 20) == 0)
            pinghdr(sp,&ping);
        tprintf("%10d%10d%5d%10d%10d%10d\n",
                ping.sent, ping.responses,
                (ping.responses*100 + ping.sent/2)/ping.sent, rtt, ping.srtt,
                ping.mdev);
    }
    if(pinger != NULLPROC)
        killproc(pinger);
    freesession(sp);
    return 0;
}
  
static void
pinghdr(sp,ping)
struct session *sp;
struct ping *ping;
{
    tprintf("Pinging %s (%s)%s; data %d interval %d ms:\n",
            sp->name,inet_ntoa(ping->target),ping->incflag?"++":"",
            (int)(ping->len), ping->interval);
    if (!ping->incflag)
        j2tputs("      sent      rcvd    %       rtt   avg rtt      mdev\n");
}

#ifdef	IPV6
/*
 * 06Apr2023, Maiko (VE4KLM), Need an IPV6 version of echo_proc()
 *  (worry about breakout of common code in a later version)
 */
void echo6_proc (unsigned char *src, unsigned char *dst, struct icmpv6 *icmpv6, struct mbuf *bp)
{
    int32 timestamp,rtt;
    int16 s;
    struct usock *up;
  
    if(Icmp_echo) {
        s = icmpv6->args.echo.id;   /* is socket TYPE_RAW ? */
        if ((up=itop(s)) != NULLUSOCK && up->type != TYPE_RAW) { 
            if (pullup(&bp,(char *)&timestamp,sizeof(timestamp))
            == sizeof(timestamp)){
                /* Compute round trip time */
                rtt = (int32)(msclock() % MAXINT) - timestamp;
		/* 12Oct2009, Maiko, Use "%d" for int32 vars */
                usprintf(s,"%s: rtt %d\n", ipv6shortform (human_readable_ipv6 (src), 0), rtt);
            }
            else
                usprintf(s,"ICMP timestamp missing in %s response\n", ipv6shortform (human_readable_ipv6 (src), 0));
        }
    }
    free_p(bp);
}
#endif
  
void
echo_proc(source,dest,icmp,bp)
int32 source;
int32 dest;
struct icmp *icmp;
struct mbuf *bp;
{
    int32 timestamp,rtt;
    int16 s;
    struct usock *up;
  
    if(Icmp_echo) {
        s = icmp->args.echo.id;   /* is socket TYPE_RAW ? */
        if ((up=itop(s)) != NULLUSOCK && up->type != TYPE_RAW) { 
            if (pullup(&bp,(char *)&timestamp,sizeof(timestamp))
            == sizeof(timestamp)){
                /* Compute round trip time */
                rtt = (int32)(msclock() % MAXINT) - timestamp;
		/* 12Oct2009, Maiko, Use "%d" for int32 vars */
                usprintf(s,"%s: rtt %d\n",inet_ntoa(source),rtt);
            }
            else
                usprintf(s,"ICMP timestamp missing in %s response\n",inet_ntoa(source));
        }
    }
    free_p(bp);
}
/* Ping transmit process. Runs until killed */
static void
pingtx(s,ping1,p)
int s;          /* Socket to use */
void *ping1;
void *p;
{
    struct ping *ping;
  
    ping = (struct ping *)ping1;
    ping->sent = 0;
    if(ping->incflag){
        for(;;){
#ifdef	IPV6
			if (ping->version == IPV6VERSION)	/* 07Apr2023, Maiko */
            	tprintf ("incremental ping not supported for ipv6\n");
			else
#endif
            pingem(s,ping->target++,0,Curproc->output,ping->len);

            ping->sent++;
            j2pause(ping->interval);
        }
    } else {
        for(;;){
#ifdef	IPV6
			if (ping->version == IPV6VERSION)	/* 07Apr2023, Maiko */
            	pingem6(s,ping->target6,(int16)ping->sent++,(int16)s,ping->len);
			else
#endif
            pingem(s,ping->target,(int16)ping->sent++,(int16)s,ping->len);

            j2pause(ping->interval);
        }
    }
}
 
#ifdef	IPV6
 
/*
 * Send ICMP Echo Request packet IPV6
 *
 * For now, just duplicate the IPV4 function,then
 * combine common code later on, Maiko 05Apr2023.
 */

int pingem6 (s,target6,seq,id,len)
int s;          /* Raw socket on which to send ping */
unsigned char *target6;   /* ipv6 Site to be pinged */
int16 seq;      /* ICMP Echo Request sequence number */
int16 id;       /* ICMP Echo Request ID */
int16 len;      /* Length of optional data field */
{
    struct mbuf *data;
    struct mbuf *bp;
    struct icmpv6 icmpv6;
    struct ipv6 ipv6;
    int32 clock;

    // struct j2sockaddr_in6 to;	/* 09Apr2023, Maiko (VE4KLM) */

	extern unsigned char myipv6addr[16];
  
    if(len > 32000) len=32000;   /* Jnos can't alloc > 32KB due to sbrk() */
    clock = (int32)(msclock() % MAXINT);
    data = ambufw((int16)(len+sizeof(clock)));
    data->cnt = len+sizeof(clock);
    /* Set optional data field, if any, to all 55's */
    if(len != 0)
        memset(data->data+sizeof(clock),0x55,len);
  
    /* Insert timestamp and build ICMP header */
    memcpy(data->data,(char *)&clock,sizeof(clock));
    icmpOutEchos++;
    icmpOutMsgs++;
    icmpv6.type = ICMPV6_ECHO;
    icmpv6.code = 0;
    icmpv6.args.echo.seq = seq;
    icmpv6.args.echo.id = id;

	/* Make sure these are first, so checksum is correct !!! */
    copyipv6addr (myipv6addr, ipv6.source);
    copyipv6addr (target6, ipv6.dest);
	ipv6.next_header = ICMPV6_PTCL;

    if((bp = htonicmpv6(&ipv6, &icmpv6,data)) == NULLBUF){
        free_p(data);
        return 0;
    }

/*
 * 09Apr2023, Maiko, THe original code 'does use' send_mbuf, suppose
 * we could stick with that, I mean it winds up calling ipv6_send()
 * anyways, now that I have patched TYPE_RAW in socket, makes more
 * sense to still call ipv6_send() direct, less code to go thru.
 *
 * But I just want to be sure this 'still works' - it does :]
 */

#ifdef	DONT_COMPILE

    to.sin6_family = AF_INET6;
    copyipv6addr (target6, to.sin6_addr.s6_addr);
    send_mbuf(s,bp,0,(char *)&to,sizeof(to));

#endif

/*
 * No point using send_mbuf, just noticed it calls ip_send() anyways, so I
 * should probably check for IPV6 in send_mbuf TYPE_RAW as well, but not
 * today though (TODO), so just send it as direct as possible ...
 *  (06Apr2023, Maiko, oops, had source and dest backwards)
 */
    ipv6_send (ipv6.source, ipv6.dest, ICMPV6_PTCL, 0, 0, bp, len_p (bp), 0, 0);

    return 0;
}
#endif 
  
/* Send ICMP Echo Request packet */
int
pingem(s,target,seq,id,len)
int s;          /* Raw socket on which to send ping */
int32 target;   /* Site to be pinged */
int16 seq;      /* ICMP Echo Request sequence number */
int16 id;       /* ICMP Echo Request ID */
int16 len;      /* Length of optional data field */
{
    struct mbuf *data;
    struct mbuf *bp;
    struct icmp icmp;
    struct sockaddr_in to;
    int32 clock;
  
    if(len > 32000) len=32000;   /* Jnos can't alloc > 32KB due to sbrk() */
    clock = (int32)(msclock() % MAXINT);
    data = ambufw((int16)(len+sizeof(clock)));
    data->cnt = len+sizeof(clock);
    /* Set optional data field, if any, to all 55's */
    if(len != 0)
        memset(data->data+sizeof(clock),0x55,len);
  
    /* Insert timestamp and build ICMP header */
    memcpy(data->data,(char *)&clock,sizeof(clock));
    icmpOutEchos++;
    icmpOutMsgs++;
    icmp.type = ICMP_ECHO;
    icmp.code = 0;
    icmp.args.echo.seq = seq;
    icmp.args.echo.id = id;
    if((bp = htonicmp(&icmp,data)) == NULLBUF){
        free_p(data);
        return 0;
    }
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = target;
    send_mbuf(s,bp,0,(char *)&to,sizeof(to));
    return 0;
}
  
#if defined GATECMDS
/* Send ICMP Echo Request and wait for response
   For mailbox command 'p <hostid> [length]'
   WA3DSP 12/93
   Modified by WG7J to add a timeout value in seconds, 2/12/94
*/
int
dombping(argc,argv,p)
int argc;
char *argv[];
void *p;
{
#ifndef MBOX_PING_ALLOWED
#include "mailbox.h"
#endif

#ifdef	IPV6
	/* 08Apr2023, Maiko (VE4KLM), Won't work if buffer is not big enough */
	char from[sizeof(struct j2sockaddr_in6)];
#else
    struct sockaddr_in from;
#endif
    struct icmp icmp;
    struct mbuf *bp;
    struct route *rp;
    int32 timestamp,rtt;
    int s,fromlen,rval;
    int32 target;
#ifdef IPV6
	/* 08Apr2023, Maiko */
	unsigned char target6[16];
	struct icmpv6 icmpv6;
	int pversion;
#endif
    int16 len = 0;
    int32 timeout;
#ifndef MBOX_PING_ALLOWED
    struct mbx *m = (struct mbx *)p;
  
    if (!(m->privs & TELNET_CMD)) {   /* mbox user allowed to ping? */
        j2tputs(Noperm);
        return 0;
    }
#endif

	/* 08Apr2023, Maiko, Adding ping IPV6 to BBS prompt, cut and
	 * paste the code I used in the console for now, breakout any
	 * common code at a later date ?
	 */
#ifdef IPV6

	/*
	 * 16Apr2023, Maiko, Only do this if IPV6 is configured
	 * Note : resolve6() will return no address if not, so it's fine
	 */
	if (ipv6iface)
	{
		tprintf ("Resolving %s (ipv6) ... ", argv[1]);
    	tflush();
	}

	/*
	 * 13Apr2023, Maiko, wrote new resolve6() function in domain.c, so
	 * let's try and use this instead of the previous code
	 */
	copyipv6addr (resolve6 (argv[1]), target6);

	if (target6[0] != 0x00)		/* if resolve fails, goto ipv4 */
	{
    	if ((s = j2socket(AF_INET6, SOCK_RAW, ICMPV6_PTCL)) == -1)
		{
        	j2tputs(Nosock);
        	return 1;
    	}
		/* 07Apr2023, Maiko */
		pversion = IPV6VERSION;

    	tprintf(" pinging [%s]\n", ipv6shortform (human_readable_ipv6 (target6), 0));
	}
	else
	{
#endif
    tprintf("Resolving %s...",argv[1]);
    tflush();
  
    if((target = resolve(argv[1])) == 0){
        tprintf(Badhost,argv[1]);
        return 0;
    }
    if((rp=rt_lookup(target)) == NULL && !ismyaddr(target)) {
        tprintf("No route to %s\n",argv[1]);
        return 0;
    }
    if((s = j2socket(AF_INET,SOCK_RAW,ICMP_PTCL)) == -1){
        j2tputs(Nosock);
        return 1;
    }
		pversion = IPVERSION;
    tprintf(" pinging [%s]\n",inet_ntoa(target));
#ifdef	IPV6
 	} 
#endif
    tflush();
  
    if(argc > 2)
        /* Set a maximum value for the data, so no-one can
         * wedge us by send a large data ping - WG7J
         */
        len = min(atoi(argv[2]),512);
    if(argc > 3)
        timeout = atoi(argv[3]) * 1000;	/* alarm bell */
    else
        timeout = 30000;
  
#ifdef	IPV6
	if (pversion == IPV6VERSION)
    	pingem6(s,target6,0,(int16)s,len);
	else
#endif
    pingem(s,target,0,(int16)s,len);
  
    for(;;){
        fromlen = sizeof(from);
        j2alarm(timeout);
        rval=recv_mbuf(s,&bp,0,(char *)&from,&fromlen);
        j2alarm(0);
        if (rval == -1 || errno == EALARM) {
            tprintf("Timeout - No response\n\n");
            close_s(s);
            return 0;
        }

#ifdef	IPV6
		if (pversion == IPV6VERSION)
		{
       		ntohicmpv6 (&icmpv6, &bp);

        	if(icmpv6.type != ICMPV6_ECHO_REPLY || icmpv6.args.echo.id != s){
            	/* Ignore other people's responses */
            	free_p(bp);
            	continue;
        	}
		}
		else
		{
#endif
        ntohicmp(&icmp,&bp);
        if(icmp.type != ICMP_ECHO_REPLY || icmp.args.echo.id != s){
            /* Ignore other people's responses */
            free_p(bp);
            continue;
        }
#ifdef	IPV6
		}
#endif
        /* Get stamp */
        if(pullup(&bp,(char *)&timestamp,sizeof(timestamp))
        != sizeof(timestamp)){
            /* The timestamp is missing! */
            free_p(bp);     /* Probably not necessary */
            tprintf("Response Received - missing timestamp\n\n");
            close_s(s);
            return 0;
        } else {
            break;
        }
    }
    close_s(s);
    free_p(bp);
  
    /* Compute round trip time */
    rtt = (int32)(msclock() % MAXINT) - timestamp;

#ifdef	IPV6
	if (pversion == IPV6VERSION)
    	tprintf ("[%s]  rtt %d ms\n\n", ipv6shortform (human_readable_ipv6 (target6), 0), rtt);
	else
#endif
	/* 11Oct2009, Maiko, Use "%d" on int32 vars */
    tprintf("[%s]  rtt %d ms\n\n",inet_ntoa(target),rtt);

    return 0;
}
  
#endif  /* GATECMDS */
  
