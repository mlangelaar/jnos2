/* TCP timeout routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "netuser.h"
#include "internet.h"
#include "iface.h"
#include "tcp.h"
  
int tcptimertype;       /* default backoff to binary exponential */
int Tcp_blimit = 31;
  
/* Timer timeout */
void
tcp_timeout(p)
void *p;
{
    struct tcb *tcb;
  
    if(p == NULL)
        return;
    tcb = (struct tcb *)p;
  
    /* Make sure the timer has stopped (we might have been kicked) */
    stop_timer(&tcb->timer);
  
    switch(tcb->state){
        case TCP_TIME_WAIT: /* 2MSL timer has expired */
            close_self(tcb,NORMAL);
            break;
        default:        /* Retransmission timer has expired */
            tcb->flags.retran = 1;  /* Indicate > 1  transmission */
            if( !tcb->parms->retries || tcb->backoff < tcb->parms->retries) {
                tcb->backoff++;
                tcb->snd.ptr = tcb->snd.una;
            /* Reduce slowstart threshold to half current window */
                tcb->ssthresh = tcb->cwind / 2;
                tcb->ssthresh = max(tcb->ssthresh,tcb->mss);
            /* Shrink congestion window to 1 packet */
                tcb->cwind = tcb->mss;
                tcp_output(tcb);
            } else reset_tcp(tcb);
    }
}
  
/* Backoff function - the subject of much research */
int32
backoff(struct tcb *tcb)
{
    int n = tcb->backoff;
    struct iftcp *parms = tcb->parms;
  
    if(tcb->parms->timertype) {  /* Linear */
        if(n >= parms->blimit)     /* At backoff limit -- N1BEE */
            n = parms->blimit;
        else
            ++n;
        return (int32) n;     /* Linear backoff for sensible values! */
    } else {        /* Binary exponential */
        if(n > min(31,parms->blimit))
            n = min(31,parms->blimit);
                /* Prevent truncation to zero */
        return 1L << n; /* Binary exponential back off */
    }
}
  
