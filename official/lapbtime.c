/* LAPB (AX.25) timer recovery routines
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Mods by G1EMM
 */
#include "global.h"
#ifdef AX25
#include "mbuf.h"
#include "proc.h"
#include "ax25.h"
#include "timer.h"
#include "lapb.h"
#ifdef NETROM
#include "netrom.h"
#endif
  
static void tx_enq __ARGS((struct ax25_cb *axp));
  
int lapbtimertype = 0;      /* default to binary exponential */
  
/* Called whenever timer T1 expires */
void
recover(p)
void *p;
{
    struct ax25_cb *axp = (struct ax25_cb *)p;
    int32 waittime = dur_timer(&axp->t1);
  
    axp->flags.retrans = 1;
    axp->retries++;
  
#ifdef NETROM
    {
        struct nrnbr_tab *np;
  
    /* If this is a netrom neighbour, increment the retry count */
        if((axp->iface->flags & IS_NR_IFACE) && ((np = find_nrnbr(axp->remote,axp->iface)) != NULL))
            np->retries++;
    }
#endif
  
    switch(axp->iface->ax25->lapbtimertype){
        case 2:          /* original backoff mode*/
            waittime = axp->srt * 2;
            break;
        case 1:          /* linear backoff mode */
            if((1L << axp->retries) < axp->iface->ax25->blimit)
                waittime = dur_timer(&axp->t1) + axp->srt;
            break;
        case 0:          /* exponential backoff mode */
            if((1L << axp->retries) < axp->iface->ax25->blimit)
                waittime = dur_timer(&axp->t1) * 2;
            break;
    }

    /* It won't be until next time T1 expires that we correct this */
    if(axp->flags.remotebusy) /* Stretch T1 for busy condition. 1.11x2a - K5JB */
        waittime *= 4;

    {
        /* IF a maximum is set, and we surpass it, use the maximum */
        int32 maxwait = axp->iface->ax25->maxwait;
        if(maxwait && (waittime > maxwait))
            waittime = maxwait;
    }
    set_timer(&axp->t1,waittime);
  
    switch(axp->state){
        case LAPB_SETUP:
            if(axp->n2 != 0 && axp->retries > axp->n2){
#ifdef NETROM
                nr_derate(axp);
#endif
                free_q(&axp->txq);
                axp->reason = LB_TIMEOUT;
                lapbstate(axp,LAPB_DISCONNECTED);
            } else {
                sendctl(axp,LAPB_COMMAND,SABM|PF);
                start_timer(&axp->t1);
            }
            break;
        case LAPB_DISCPENDING:
            if(axp->n2 != 0 && axp->retries > axp->n2){
#ifdef NETROM
                nr_derate(axp);
#endif
                axp->reason = LB_TIMEOUT;
                lapbstate(axp,LAPB_DISCONNECTED);
            } else {
                sendctl(axp,LAPB_COMMAND,DISC|PF);
                start_timer(&axp->t1);
            }
            break;
        case LAPB_CONNECTED:
        case LAPB_RECOVERY:
            if(axp->n2 != 0 && axp->retries > axp->n2){
            /* Give up */
#ifdef NETROM
                nr_derate(axp);
#endif
                sendctl(axp,LAPB_RESPONSE,DM|PF);
                free_q(&axp->txq);
                axp->reason = LB_TIMEOUT;
                lapbstate(axp,LAPB_DISCONNECTED);
            } else {
            /* Transmit poll */
                tx_enq(axp);
                lapbstate(axp,LAPB_RECOVERY);
            }
            break;
    }
}
  
  
/* Send a poll (S-frame command with the poll bit set) */
void
pollthem(p)
void *p;
{
    register struct ax25_cb *axp;
  
    axp = (struct ax25_cb *)p;
    if(axp->proto == V1)
        return; /* Not supported in the old protocol */
    switch(axp->state){
        case LAPB_CONNECTED:
            axp->retries = 0;
            tx_enq(axp);
            lapbstate(axp,LAPB_RECOVERY);
            break;
    }
}
  
/* Called whenever timer T4 (link rudundancy timer) expires */
void
redundant(p)
void *p;
{
    register struct ax25_cb *axp;
  
    axp = (struct ax25_cb *)p;
    switch(axp->state){
        case LAPB_CONNECTED:
        case LAPB_RECOVERY:
            axp->retries = 0;
            sendctl(axp,LAPB_COMMAND,DISC|PF);
            start_timer(&axp->t1);
            free_q(&axp->txq);
            lapbstate(axp,LAPB_DISCPENDING);
            break;
    }
}
  
/* Transmit query */
static void
tx_enq(axp)
register struct ax25_cb *axp;
{
    char ctl;
    struct mbuf *bp;
  
    /* I believe that retransmitting the oldest unacked
     * I-frame tends to give better performance than polling,
     * as long as the frame isn't too "large", because
     * chances are that the I frame got lost anyway.
     * This is an option in LAPB, but not in the official AX.25.
     */
    /* Including the remotebusy here breaks the V1/V2 rules = K5JB */
    if(axp->txq != NULLBUF && !axp->flags.remotebusy   /* 1.10l1 - K5JB */
        && (len_p(axp->txq) < axp->pthresh || axp->proto == V1)){
        /* Retransmit oldest unacked I-frame */
        dup_p(&bp,axp->txq,0,len_p(axp->txq));
        ctl = PF | I | (((axp->vs - axp->unack) & MMASK) << 1)
        | (axp->vr << 5);
        sendframe(axp,LAPB_COMMAND,ctl,bp);
    } else {
        ctl = len_p(axp->rxq) >= axp->window ? RNR|PF : RR|PF;
        sendctl(axp,LAPB_COMMAND,ctl);
    }
    axp->response = 0;
    stop_timer(&axp->t3);
    start_timer(&axp->t1);
}
/* Start data transmission on link, if possible - Called on expiration
 * of T2 effective, moved to here from lapb.c - K5JB
 */
void
dlapb_output(p)
void *p;
{
    register struct mbuf *bp;
    struct mbuf *tbp;
    char control;
    int sent = 0; /* not used - K5JB */
    int i;
  
    struct ax25_cb *axp = (struct ax25_cb *)p;

    if(axp == NULLAX25
          || (axp->state != LAPB_RECOVERY && axp->state != LAPB_CONNECTED))
        return;

    if(axp->flags.remotebusy){ /* To break RNR deadlock 1.10l1 - K5JB */
        if(!run_timer(&axp->t1))
            start_timer(&axp->t1);
        return;
    }
    /* Dig into the send queue for the first unsent frame */
    bp = axp->txq;
    for(i = 0; i < axp->unack; i++){
        if(bp == NULLBUF)
            break;  /* Nothing to do */
        bp = bp->anext;
    }
    /* Start at first unsent I-frame, stop when either the
     * number of unacknowledged frames reaches the maxframe limit,
     * or when there are no more frames to send
     */
    while(bp != NULLBUF && axp->unack < axp->maxframe){
        control = I | (axp->vs++ << 1) | (axp->vr << 5);
        axp->vs &= MMASK;
        dup_p(&tbp,bp,0,len_p(bp));
        if(tbp == NULLBUF)
            return; /* Probably out of memory */
        /* This COMMAND is what causes monkey see, monkey do on the PF
         * bits.  Maybe return to this - K5JB
         */
        axp->unack++;   /* moved up from after start_timer() - K5JB */
        sendframe(axp,LAPB_COMMAND,control,tbp);
        start_timer(&axp->t4);
        /* We're implicitly acking any data he's sent, so stop any
         * delayed ack
         */
        axp->response = 0;
        if(!run_timer(&axp->t1)){
            stop_timer(&axp->t3);
            start_timer(&axp->t1);
        }
        sent++;
        bp = bp->anext;   /* not used */
        if(!axp->flags.rtt_run){
        /* Start round trip timer */
            axp->rtt_seq = (control >> 1) & MMASK;
            axp->rtt_time = msclock();
            axp->flags.rtt_run = 1;
        }
    }
    /* little trick from NET - K5JB */
    if(axp->response != 0)
        sendctl(axp,LAPB_RESPONSE,axp->response);
    axp->response = 0;
    return;
}
#endif /* AX25 */
  
