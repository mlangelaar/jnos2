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
    struct sockaddr_in from;
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
  
    if(ping.interval == 0) {
        /* One shot ping; let echo_proc hook handle response. */
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
        ntohicmp(&icmp,&bp);
        if(icmp.type != ICMP_ECHO_REPLY || icmp.args.echo.id != s){
            /* Ignore other people's responses */
            free_p(bp);
            continue;
        }
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
            pingem(s,ping->target++,0,Curproc->output,ping->len);
            ping->sent++;
            j2pause(ping->interval);
        }
    } else {
        for(;;){
            pingem(s,ping->target,(int16)ping->sent++,(int16)s,ping->len);
            j2pause(ping->interval);
        }
    }
}
  
  
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

    struct sockaddr_in from;
    struct icmp icmp;
    struct mbuf *bp;
    struct route *rp;
    int32 timestamp,rtt;
    int s,fromlen,rval;
    int32 target;
    int16 len = 0;
    int32 timeout;
#ifndef MBOX_PING_ALLOWED
    struct mbx *m = (struct mbx *)p;
  
    if (!(m->privs & TELNET_CMD)) {   /* mbox user allowed to ping? */
        j2tputs(Noperm);
        return 0;
    }
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
  
    tprintf(" pinging [%s]\n",inet_ntoa(target));
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
        ntohicmp(&icmp,&bp);
        if(icmp.type != ICMP_ECHO_REPLY || icmp.args.echo.id != s){
            /* Ignore other people's responses */
            free_p(bp);
            continue;
        }
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

	/* 11Oct2009, Maiko, Use "%d" on int32 vars */
    tprintf("[%s]  rtt %d ms\n\n",inet_ntoa(target),rtt);

    return 0;
}
  
#endif  /* GATECMDS */
  
