/* Low level TCP routines:
 *  control block management
 *  sequence number logical operations
 *  state transitions
 *  RTT cacheing
 *  garbage collection
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "timer.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "iface.h"
#include "tcp.h"
#include "ip.h"
#ifdef UNIX
/* route output to Command session's screen */
#define PRINTF tcmdprintf
#else
#define PRINTF printf
#endif /* UNIX */
  
/* TCP connection states */
char *Tcpstates[] = {
    "",
    "Closed",
    "Listen",
    "SYN sent",
    "SYN received",
    "Established",
    "FIN wait 1",
    "FIN wait 2",
    "Close wait",
    "Last ACK",
    "Closing",
    "Time wait"
};
  
/* TCP closing reasons */
char *Tcpreasons[] = {
    "Normal",
    "Reset/Refused",
    "Timeout",
    "ICMP"
};
struct tcb *Tcbs;       /* Head of control block list */
int16 Tcp_mss = DEF_MSS;    /* Maximum segment size to be sent with SYN */
int32 Tcp_irtt = DEF_RTT;   /* Initial guess at round trip time */
int Tcp_retries = DEF_RETRIES;    /* Max retries before resetting tcb */
int Tcp_trace;          /* State change tracing flag */
int Tcp_syndata;
struct tcp_rtt Tcp_rtt[RTTCACHE];
struct mib_entry Tcp_mib[] = {
    { NULLCHAR,       { 0 } },
    { "tcpRtoAlgorithm",  { 4 } },  /* Van Jacobsen's algorithm */
    { "tcpRtoMin",        { 0 } },  /* No lower bound */
    { "tcpRtoMax",        { MAXINT32 } },   /* No upper bound */
    { "tcpMaxConn",       { -1L } },    /* No limit */
    { "tcpActiveOpens",   { 0 } },
    { "tcpPassiveOpens",  { 0 } },
    { "tcpAttemptFails",  { 0 } },
    { "tcpEstabResets",   { 0 } },
    { "tcpCurrEstab",     { 0 } },
    { "tcpInSegs",        { 0 } },
    { "tcpOutSegs",       { 0 } },
    { "tcpRetransSegs",   { 0 } },
    { NULLCHAR,       { 0 } },  /* Connection state goes here */
    { "tcpInErrs",        { 0 } },
    { "tcpOutRsts",       { 0 } }
};

/* Look up TCP connection
 * Return TCB pointer or NULLTCB if nonexistant.
 * Also move the entry to the top of the list to speed future searches.
 */
struct tcb *
lookup_tcb(conn)
register struct connection *conn;
{
    register struct tcb *tcb;
    struct tcb *tcblast = NULLTCB;
  
    for(tcb=Tcbs;tcb != NULLTCB;tcblast = tcb,tcb = tcb->next){
        /* Yet another structure compatibility hack */
        if(conn->remote.port == tcb->conn.remote.port
            && conn->local.port == tcb->conn.local.port
            && conn->remote.address == tcb->conn.remote.address
        && conn->local.address == tcb->conn.local.address){
            if(tcblast != NULLTCB){
                /* Move to top of list */
                tcblast->next = tcb->next;
                tcb->next = Tcbs;
                Tcbs = tcb;
            }
            return tcb;
        }
  
    }
    return NULLTCB;
}
  
/* Create a TCB, return pointer. Return pointer if TCB already exists. */
struct tcb *
create_tcb(conn)
struct connection *conn;
{
    struct tcb *tcb;
    struct tcp_rtt *tp;
  
    if((tcb = lookup_tcb(conn)) != NULLTCB)
        return tcb;
    tcb = (struct tcb *)callocw(1,sizeof (struct tcb));
    ASSIGN(tcb->conn,*conn);
  
    tcb->state = TCP_CLOSED;
    tcb->ssthresh = 65535U;
    /* All this is now done in open_tcp() and tcp_in() - WG7J */
    tcb->cwind = tcb->mss = Tcp_mss;
    if((tp = rtt_get(tcb->conn.remote.address)) != NULLRTT){
        tcb->srtt = tp->srtt;
        tcb->mdev = tp->mdev;
    } else {
        tcb->srtt = Tcp_irtt;   /* mdev = 0 */
    }
    /* Initialize timer intervals */
    set_timer(&tcb->timer,tcb->srtt);
    tcb->timer.func = tcp_timeout;
    tcb->timer.arg = tcb;
  
    /* point to the default interface parms block */
    tcb->parms = &def_iftcp;
  
    tcb->next = Tcbs;
    Tcbs = tcb;
    return tcb;
}
  
/* Close our TCB */
void
close_self(tcb,reason)
register struct tcb *tcb;
int reason;
{
    struct reseq *rp1;
    register struct reseq *rp;
  
    if(tcb == NULLTCB)
        return;
  
    stop_timer(&tcb->timer);
    tcb->reason = reason;
  
    /* Flush reassembly queue; nothing more can arrive */
    for(rp = tcb->reseq;rp != NULLRESEQ;rp = rp1){
        rp1 = rp->next;
        free_p(rp->bp);
        free((char *)rp);
    }
    tcb->reseq = NULLRESEQ;
    setstate(tcb,TCP_CLOSED);
}
  
/* Sequence number comparisons
 * Return true if x is between low and high inclusive,
 * false otherwise
 */
int
seq_within(x,low,high)
register int32 x,low,high;
{
    if(low <= high){
        if(low <= x && x <= high)
            return 1;
    } else {
        if(low >= x && x >= high)
            return 1;
    }
    return 0;
}
int
seq_lt(x,y)
register int32 x,y;
{
    return (int)(x-y) < 0;
}
int
seq_gt(x,y)
register int32 x,y;
{
    return (int)(x-y) > 0;
}
int
seq_ge(x,y)
register int32 x,y;
{
    return (int)(x-y) >= 0;
}
  
void
setstate(tcb,newstate)
register struct tcb *tcb;
register int newstate;
{
    register char oldstate;
  
    oldstate = tcb->state;
    tcb->state = newstate;
    if(Tcp_trace)
        PRINTF("TCB %lx %s -> %s\n",ptol(tcb),
        Tcpstates[(int)oldstate],Tcpstates[newstate]);
  
    /* Update MIB variables */
    switch(oldstate){
        case TCP_CLOSED:
            if(newstate == TCP_SYN_SENT)
                tcpActiveOpens++;
            break;
        case TCP_LISTEN:
            if(newstate == TCP_SYN_RECEIVED)
                tcpPassiveOpens++;
            break;
        case TCP_SYN_SENT:
            if(newstate == TCP_CLOSED)
                tcpAttemptFails++;
            break;
        case TCP_SYN_RECEIVED:
        switch(newstate){
            case TCP_CLOSED:
            case TCP_LISTEN:
                tcpAttemptFails++;
                break;
        }
            break;
        case TCP_ESTABLISHED:
        case TCP_CLOSE_WAIT:
        switch(newstate){
            case TCP_CLOSED:
            case TCP_LISTEN:
                tcpEstabResets++;
                break;
        }
            tcpCurrEstab--;
            break;
    }
    if(newstate == TCP_ESTABLISHED || newstate == TCP_CLOSE_WAIT)
        tcpCurrEstab++;
  
    if(tcb->s_upcall)
        (*tcb->s_upcall)(tcb,oldstate,newstate);
  
    switch(newstate){
        case TCP_SYN_RECEIVED:  /***/
        case TCP_ESTABLISHED:
        /* Notify the user that he can begin sending data */
            if(tcb->t_upcall)
                (*tcb->t_upcall)(tcb,tcb->window - tcb->sndcnt);
            break;
    }
}
/* Round trip timing cache routines.
 * These functions implement a very simple system for keeping track of
 * network performance for future use in new connections.
 * The emphasis here is on speed of update (rather than optimum cache hit
 * ratio) since rtt_add is called every time a TCP connection updates
 * its round trip estimate.
 */
void
rtt_add(addr,rtt)
int32 addr;     /* Destination IP address */
int32 rtt;
{
    register struct tcp_rtt *tp;
    int32 abserr;
  
    if(addr == 0)
        return;
    tp = &Tcp_rtt[(unsigned short)addr % RTTCACHE];
    if(tp->addr != addr){
        /* New entry */
        tp->addr = addr;
        tp->srtt = rtt;
        tp->mdev = 0;
	tp->mods++;	/* 01Apr2008, Maiko, Track address changes, gives
			 * a better idea of cache utilization !
			 */
    } else {
        /* Run our own SRTT and MDEV integrators, with rounding */
        abserr = (rtt > tp->srtt) ? rtt - tp->srtt : tp->srtt - rtt;
        tp->srtt = ((AGAIN-1)*tp->srtt + rtt + (AGAIN/2)) >> LAGAIN;
        tp->mdev = ((DGAIN-1)*tp->mdev + abserr + (DGAIN/2)) >> LDGAIN;
    }
}
struct tcp_rtt *
rtt_get(addr)
int32 addr;
{
    register struct tcp_rtt *tp;
  
    if(addr == 0)
        return NULLRTT;
    tp = &Tcp_rtt[(unsigned short)addr % RTTCACHE];
    if(tp->addr != addr)
        return NULLRTT;
    return tp;
}
  
#ifndef UNIX
  
/* TCP garbage collection - called by storage allocator when free space
 * runs low. The send and receive queues are crunched. If the situation
 * is red, the resequencing queue is discarded; otherwise it is
 * also crunched.
 */
void
tcp_garbage(red)
int red;
{
    register struct tcb *tcb;
    struct reseq *rp,*rp1;
  
    for(tcb = Tcbs;tcb != NULLTCB;tcb = tcb->next){
        mbuf_crunch(&tcb->rcvq);
        mbuf_crunch(&tcb->sndq);
        for(rp = tcb->reseq;rp != NULLRESEQ;rp = rp1){
            rp1 = rp->next;
            if(red){
                free_p(rp->bp);
                free((char *)rp);
            } else {
                mbuf_crunch(&rp->bp);
            }
        }
        if(red)
            tcb->reseq = NULLRESEQ;
    }
}
#endif /* UNIX */
  
