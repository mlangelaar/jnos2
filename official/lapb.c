/* Link Access Procedures Balanced (LAPB), the upper sublayer of
 * AX.25 Level 2.
 * Copyright 1991 Phil Karn, KA9Q
 *
 * 92/02/07 WG7J
 * Modified to drop ax.25 route records in cases where routes
 * were added by the connections. Inspired by K4TQL
 */
#include "global.h"
#ifdef AX25
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "ax25.h"
#include "lapb.h"
#include "ip.h"
#include "netrom.h"
  
/* This forces data sent by jumpstarting a mailbox connect to
 * resend the header etc. when the first UA reply is missed, and
 * a second SABM is received for the same connection
 * added by Ron Murray VK6ZJM, Murray_RJ@cc.curtin.edu.au
 */
#define SABM_HOLDOFF

#define K5JB_ENHANCE
  
static void handleit __ARGS((struct ax25_cb *axp,int pid,struct mbuf *bp));
static void procdata __ARGS((struct ax25_cb *axp,struct mbuf *bp));
static int ackours __ARGS((struct ax25_cb *axp,int16 n));
static void clr_ex __ARGS((struct ax25_cb *axp));
static void enq_resp __ARGS((struct ax25_cb *axp));
static void inv_rex __ARGS((struct ax25_cb *axp));
/* static void drop_axr __ARGS((struct ax25_cb *axp));*/
  
/*needed for mailbox jumpstart - WG7J */
#ifdef MAILBOX
int MbAx25Ifilter = 0;     /* mbox ifilter defaults to off. K5JB/N5KNX */
#ifdef NETROM
extern struct nrnbr_tab *find_nrnbr __ARGS((char *, struct iface *));
#endif
#endif
  
/* Process incoming frames */
int
lapb_input(axp,cmdrsp,bp)
struct ax25_cb *axp;            /* Link control structure */
int cmdrsp;                     /* Command/response flag */
struct mbuf *bp;                /* Rest of frame, starting with ctl */
{
    int control;
    int class;              /* General class (I/S/U) of frame */
    int16 type;             /* Specific type (I/RR/RNR/etc) of frame */
    char pf;                /* extracted poll/final bit */
    char poll = 0;
    char final = 0;
    int16 nr;               /* ACK number of incoming frame */
    int16 ns;               /* Seq number of incoming frame */
    int16 tmp;
    int recovery = 0;
  
    if(bp == NULLBUF || axp == NULLAX25){
        free_p(bp);
        return -1;
    }
  
    /* Extract the various parts of the control field for easy use */
    if((control = PULLCHAR(&bp)) == -1){
        free_p(bp);     /* Probably not necessary */
        return -1;
    }
    type = ftype(control);
    class = type & 0x3;
    pf = control & PF;
    /* Check for polls and finals */
    if(pf){
        switch(cmdrsp){
            case LAPB_COMMAND:
                poll = YES;
                break;
            case LAPB_RESPONSE:
                final = YES;
                break;
        }
    }
    /* Extract sequence numbers, if present */
    switch(class){
        case I:
        case I+2:
            ns = (control >> 1) & MMASK;
        case S: /* Note fall-thru */
            nr = (control >> 5) & MMASK;
            break;
    }
    /* This section follows the SDL diagrams by K3NA fairly closely */
    switch(axp->state){
        case LAPB_DISCONNECTED:
        switch(type){
            case SABM:      /* Initialize or reset link */
        /* This a new incoming connection.
         * Accept if we have enough memory left
         * 920115 - WG7J
         */
#ifndef UNIX
                if(availmem() < Memthresh){
                    sendctl(axp,LAPB_RESPONSE,DM|pf);
                    break;
                }
#endif
                sendctl(axp,LAPB_RESPONSE,UA|pf);   /* Always accept */
                clr_ex(axp);
                axp->unack = axp->vr = axp->vs = 0;
                lapbstate(axp,LAPB_CONNECTED);/* Resets state counters */
                axp->srt = axp->iface->ax25->irtt;  /* Set ax.25 irtt per interface - WG7J */
                axp->mdev = 0;
                set_timer(&axp->t1,2*axp->srt);
                start_timer(&axp->t3);
                start_timer(&axp->t4);
#ifdef SABM_HOLDOFF
                axp->flags.rxd_I_frame = 0;    /* nothing received yet */
#endif
#ifdef MAILBOX
        /* Jump-start the mailbox. This causes the [NET-H$] prompt to
         * be sent Immediately, instead of after the first data packet
         * Only conenctions to the bbscall, the alias, or the convers
         * call get jumpstarted.
         * 5/17/93 WG7J
         */
                if(axp->jumpstarted & (ALIAS_LINK+CONF_LINK+TTY_LINK+TN_LINK)) {
                    axp->jumpstarted += JUMPSTARTED;
                    s_arcall(axp,0);
                }
#endif /* MAILBOX */
                break;
            case DISC:      /* Always answer a DISC with a DM */
                sendctl(axp,LAPB_RESPONSE,DM|pf);
                break;
            case DM:        /* Ignore to avoid infinite loops */
                break;
            default:        /* All others get DM */
                if(poll)
                    sendctl(axp,LAPB_RESPONSE,DM|pf);
                break;
        }
            if(axp->state == LAPB_DISCONNECTED){    /* we can close connection */
#if 0
/* Timers stopped, and txq freed, already done in del_ax25() .. n5knx */
                stop_timer(&axp->t1);   /* waste all the timers */
                stop_timer(&axp->t3);
                stop_timer(&axp->t4);
                free_q(&axp->txq);      /* lose transmit queue */
#endif
                /* drop_axr(axp);*/          /* drop ax25 route */
                del_ax25(axp);      /* clean out the trash */
                axp = NULLAX25;     /* del_ax25 freed it */
            }
            break;
        case LAPB_SETUP:
        switch(type){
            case SABM:      /* Simultaneous open */
                sendctl(axp,LAPB_RESPONSE,UA|pf);
                break;
            case DISC:
                sendctl(axp,LAPB_RESPONSE,DM|pf);
#ifdef NETROM
                nr_derate(axp);
#endif
                /*drop_axr(axp);*/      /* drop ax25 route */
                free_q(&axp->txq);
                stop_timer(&axp->t1);
                axp->reason = LB_DM;
                lapbstate(axp,LAPB_DISCONNECTED);
                break;
            case UA:        /* Connection accepted */
            /* Note: xmit queue not cleared */
                stop_timer(&axp->t1);
                start_timer(&axp->t3);
                axp->unack = axp->vr = axp->vs = 0;
                lapbstate(axp,LAPB_CONNECTED);
                start_timer(&axp->t4);
                break;
            case DM:        /* Connection refused */
                free_q(&axp->txq);
                stop_timer(&axp->t1);
                axp->reason = LB_DM;
                lapbstate(axp,LAPB_DISCONNECTED);
                break;
            default:        /* Respond with DM only to command polls */
#ifdef K5JB_ENHANCE
/* Wrong! All other frames should be ignored (Para 2.4.3.1) - K5JB */
#else
                if(poll)
                    sendctl(axp,LAPB_RESPONSE,DM|pf);
#endif
                break;
        }
            break;
        case LAPB_DISCPENDING:
        switch(type){
            case SABM:
                sendctl(axp,LAPB_RESPONSE,DM|pf);
                /*drop_axr(axp);*/          /* drop ax25 route */
                break;
            case DISC:
                sendctl(axp,LAPB_RESPONSE,UA|pf);
                /*drop_axr(axp);*/          /* drop ax25 route */
                break;
            case UA:
            case DM:
                stop_timer(&axp->t1);
                lapbstate(axp,LAPB_DISCONNECTED);
                break;
            default:        /* Respond with DM only to command polls */
                if(poll)
                    sendctl(axp,LAPB_RESPONSE,DM|pf);
                /*drop_axr(axp);*/          /* drop ax25 route */
                break;
        }
            break;
        case LAPB_RECOVERY:    /* folded these two cases together - K5JB */
            recovery = 1;    /* fall through */
        case LAPB_CONNECTED:
        switch(type){
            case SABM:
                sendctl(axp,LAPB_RESPONSE,UA|pf);
#ifdef SABM_HOLDOFF
                if (axp->flags.rxd_I_frame)
        /* only reset if we've had a */
        /* valid I-frame. Otherwise he */
        /* may just not have got our UA */
#endif
                {
                    clr_ex(axp);
                    if(!recovery)
                        free_q(&axp->txq);
                    stop_timer(&axp->t1);
                    start_timer(&axp->t3);
                    axp->unack = axp->vr = axp->vs = 0;
                    lapbstate(axp,LAPB_CONNECTED); /* Purge queues */
                    if(recovery && !run_timer(&axp->t4))
                        start_timer(&axp->t4);
                }
                break;
            case DISC:
                free_q(&axp->txq);
                sendctl(axp,LAPB_RESPONSE,UA|pf);
                stop_timer(&axp->t1);
                stop_timer(&axp->t3);
                axp->reason = LB_NORMAL;
                lapbstate(axp,LAPB_DISCONNECTED);
                break;
            case DM:
#if defined(NETROM)
                if(recovery)
                    nr_derate(axp);
#endif
                axp->reason = LB_DM;
                lapbstate(axp,LAPB_DISCONNECTED);
                break;
            case UA:
            case FRMR:
	/*
	 * 10Dec020, Maiko (VE4KLM), running into situation where we are
	 * getting two UA frames after we send an SABM frame. The first UA
	 * frame we acknowledge, and we move ourselves into LAPB_CONNECTED
	 * state. The second UA frame arrives immediately after the first,
	 * and we get 'here'. Why on earth do we try and re-establish the
	 * connection already made just now ? The result is a firestorm.
	 *
	 * A vicious loop results in this, basically a DOS attack !
	 *
	 * The 2 lines of code below are commented out now, and I don't see
	 * the point (at this time anyways), it's been running for several
	 * days (as a postnote) and the error has yet to show up on normal
	 * operations, so cross my fingers on the decision to do this.
	 *
	 * This was determined to be a problem at the remote end. Their node,
	 * which was dependent on the linux AX25 stack - was acting strange.
	 *
    	 * The problem was actually solved by recompiling and relinking their
	 * node software. My thought is they were running an old binary which
	 * was no longer compatible with the libraries of an upgraded linux.
	 *
	 * This scenario should definitely be logged to the JNOS logfile !
	 */
		log (-1, "lapb - unexpected UA frame");

#ifdef	HOW_TO_CREATE_A_FIRE_STORM
                est_link(axp);
                lapbstate(axp,LAPB_SETUP);      /* Re-establish link */
#endif
                break;
            case RR:
            case RNR:
            case REJ:
                axp->flags.remotebusy = (type == RNR) ? YES : NO;
                if(recovery){
                  if(axp->proto == V1 || final){
                    stop_timer(&axp->t1);
                    ackours(axp,nr);
                    if(axp->unack != 0)
                        inv_rex(axp);
                    else {
                        start_timer(&axp->t3);
                        /* There is a subtle reason for doing this: (1.10l1 K5JB) */
                        if(type != RNR)
                            lapbstate(axp,LAPB_CONNECTED);
                        if(!run_timer(&axp->t4))
                            start_timer(&axp->t4);
                    }
                  } else {
                    if(poll)
                        enq_resp(axp);
                    ackours(axp,nr);
                    if(type == REJ && axp->unack != 0)
                        inv_rex(axp);
                    /* A REJ that acks everything but doesn't
                     * have the F bit set can cause a deadlock.
                     * So make sure the timer is running.
                     */
                    if(!run_timer(&axp->t1))
                        start_timer(&axp->t1);
                  }
                }else{ /* !recovery */
                    if(poll)
                        enq_resp(axp);
                    ackours(axp,nr);
                    if(type == REJ){
                        stop_timer(&axp->t1);
                        start_timer(&axp->t3);
                        /* This may or may not actually invoke transmission,
                         * depending on whether this REJ was caused by
                         * our losing his prior ACK.
                         */
                        inv_rex(axp);
                    }
                }
                break;
            case I:
                ackours(axp,nr); /** == -1) */
#ifdef SABM_HOLDOFF
                axp->flags.rxd_I_frame = 1;   /* we got something */
#endif
                if(recovery){
            /* Make sure timer is running, since an I frame
             * cannot satisfy a poll
             */
                    if(!run_timer(&axp->t1))
                        start_timer(&axp->t1);
                }else
                    start_timer(&axp->t4);

                if(len_p(axp->rxq) >= axp->window){
                /* Too bad he didn't listen to us; he'll
                 * have to resend the frame later. This
                 * drastic action is necessary to avoid
                 * deadlock.
                 */
                    if(poll || recovery)
                        sendctl(axp,LAPB_RESPONSE,RNR|pf);
                    free_p(bp);
                    bp = NULLBUF;
                    break;
                }
            /* Reject or ignore I-frames with receive sequence number errors */
                if(ns != axp->vr){
                    if(axp->proto == V1 || !axp->flags.rejsent){
                        axp->flags.rejsent = YES;
                        sendctl(axp,LAPB_RESPONSE,REJ | pf);
                    } else if(poll)
                        enq_resp(axp);
                    axp->response = 0;
                    break;
                }
                axp->flags.rejsent = NO;
                axp->vr = (axp->vr+1) & MMASK;
                tmp = len_p(axp->rxq) >= axp->window ? RNR : RR;
                if(poll)
                    sendctl(axp,LAPB_RESPONSE,tmp|PF);
                else
                    axp->response = tmp;
                procdata(axp,bp);
                bp = NULLBUF;
                break;
            default:    /* All others ignored */
                break;
        }
            break;
    }
    free_p(bp);     /* In case anything's left */
  
    /* See if we can send some data, perhaps piggybacking an ack.
     * If successful, lapb_output will clear axp->response.
     * N5KNX: Linux experience has shown that axp may be non-null, BUT
     *        no longer in the Ax25_cb chain.  del_ax25() race condition??!!
     *        We'd better test that the axp ptr is still chained.
     */
    {   struct ax25_cb *axp2;
        for (axp2 = Ax25_cb; axp2 && axp2!=axp; axp2=axp2->next);
        if (axp2 && axp2->state != LAPB_DISCONNECTED)
            lapb_output(axp2);
    }
    return 0;
}
  
/* Handle incoming acknowledgements for frames we've sent.
 * Free frames being acknowledged.
 * Return -1 to cause a frame reject if number is bad, 0 otherwise
 */
static int
ackours(axp,n)
struct ax25_cb *axp;
int16 n;
{
    struct mbuf *bp;
    int acked = 0;  /* Count of frames acked by this ACK */
    int16 oldest;   /* Seq number of oldest unacked I-frame */
    int32 rtt,abserr;
  
    /* Free up acknowledged frames by purging frames from the I-frame
     * transmit queue. Start at the remote end's last reported V(r)
     * and keep going until we reach the new sequence number.
     * If we try to free a null pointer,
     * then we have a frame reject condition.
     */
    oldest = (axp->vs - axp->unack) & MMASK;
    while(axp->unack != 0 && oldest != n){
        if((bp = dequeue(&axp->txq)) == NULLBUF){
            /* Acking unsent frame */
            return -1;
        }
        free_p(bp);
        axp->unack--;
        acked++;
        if(axp->flags.rtt_run && axp->rtt_seq == oldest)
        {
            /* A frame being timed has been acked */
            axp->flags.rtt_run = 0;

            /* Update only if frame wasn't retransmitted */
            if(!axp->flags.retrans)
            {
                int32 waittime, maxwait = axp->iface->ax25->maxwait;

                rtt = (int32)(msclock() - axp->rtt_time);
                abserr = (rtt > axp->srt) ? rtt - axp->srt :
                axp->srt - rtt;
  
                /* Run SRT and mdev integrators */
                axp->srt = ((axp->srt * 7) + rtt + 4) >> 3;
                axp->mdev = ((axp->mdev*3) + abserr + 2) >> 2;
                /* Update timeout */
                /* F1GYG: If a maximum is set, and we surpass it, use the maximum */
                waittime = 4*axp->mdev+axp->srt;
                if(maxwait && (waittime > maxwait))
                    waittime = maxwait;
                set_timer(&axp->t1,waittime);
            }
        }
        axp->flags.retrans = 0;
        axp->retries = 0;
        oldest = (oldest + 1) & MMASK;
    }
    if(axp->unack == 0){
        /* All frames acked, stop timeout */
        stop_timer(&axp->t1);
        start_timer(&axp->t3);
    } else if(acked != 0) {
        /* Partial ACK; restart timer */
        start_timer(&axp->t1);
    }
    if(acked != 0){
        /* If user has set a transmit upcall, indicate how many frames
         * may be queued
         */
        if(axp->t_upcall != NULLVFP((struct ax25_cb*,int)))
            (*axp->t_upcall)(axp,axp->paclen * (axp->maxframe - axp->unack));
    }
    return 0;
}
  
/* Establish data link */
void
est_link(axp)
struct ax25_cb *axp;
{
    clr_ex(axp);
    axp->retries = 0;
    sendctl(axp,LAPB_COMMAND,SABM|PF);
    stop_timer(&axp->t3);
    start_timer(&axp->t1);
}
/* Clear exception conditions */
static void
clr_ex(axp)
struct ax25_cb *axp;
{
    axp->flags.remotebusy = NO;
    axp->flags.rejsent = NO;
    axp->response = 0;
    stop_timer(&axp->t3);
}
/* Enquiry response */
static void
enq_resp(axp)
struct ax25_cb *axp;
{
    char ctl;
  
    ctl = len_p(axp->rxq) >= axp->window ? RNR|PF : RR|PF;
    sendctl(axp,LAPB_RESPONSE,ctl);
    axp->response = 0;
    stop_timer(&axp->t3);
}
/* Invoke retransmission */
static void
inv_rex(axp)
struct ax25_cb *axp;
{
    axp->vs -= axp->unack;
    axp->vs &= MMASK;
    axp->unack = 0;
}
/* Send S or U frame to currently connected station */
int
sendctl(axp,cmdrsp,cmd)
struct ax25_cb *axp;
int cmdrsp;
int cmd;
{
    if((ftype((char)cmd) & 0x3) == S)       /* Insert V(R) if S frame */
        cmd |= (axp->vr << 5);
    return sendframe(axp,cmdrsp,cmd,NULLBUF);
}
/* defer output with timer, give time for ack to abort retry - K5JB */
void
lapb_output(axp)
register struct ax25_cb *axp;
{
    if (axp->t2.duration == 0L)  /* disabled? */
        dlapb_output((void *)axp);
    else {
        /* function installed in lapbtime.c */
        axp->t2.arg = (void *)axp;
        start_timer(&axp->t2);
    }
}
/* Set new link state */
void
lapbstate(axp,s)
struct ax25_cb *axp;
int s;
{
    int oldstate;
  
    oldstate = axp->state;
    axp->state = s;
    if(s == LAPB_DISCONNECTED){
        stop_timer(&axp->t1);
        stop_timer(&axp->t2);    /* K5JB */
        stop_timer(&axp->t3);
        stop_timer(&axp->t4);
        free_q(&axp->txq);
        /*drop_axr(axp);*/          /* any ax25 route that hasn't been dropped yet*/
    }
    /* Don't bother the client unless the state is really changing */
    if((oldstate != s) && (axp->s_upcall != NULLVFP((struct ax25_cb*,int,int))))
        (*axp->s_upcall)(axp,oldstate,s);
}
/* Process a valid incoming I frame */
static void
procdata(axp,bp)
struct ax25_cb *axp;
struct mbuf *bp;
{
    int pid;
    int seq;
  
    /* Extract level 3 PID */
#ifdef K5JB_ENHANCE
/* K5JB: ignore zero len info frames (to prevent false mailbox autostarts by ROSE switches) */
    if((pid = PULLCHAR(&bp)) == -1 || bp == NULLBUF)
#else
    if((pid = PULLCHAR(&bp)) == -1)
#endif
        return; /* No PID */
  
    if(axp->segremain != 0){
        /* Reassembly in progress; continue */
        seq = PULLCHAR(&bp);
        if(pid == PID_SEGMENT
        && (seq & SEG_REM) == axp->segremain - 1){
            /* Correct, in-order segment */
            append(&axp->rxasm,bp);
            if((axp->segremain = (seq & SEG_REM)) == 0){
                /* Done; kick it upstairs */
                bp = axp->rxasm;
                axp->rxasm = NULLBUF;
                pid = PULLCHAR(&bp);
                handleit(axp,pid,bp);
            }
        } else {
            /* Error! */
            free_p(axp->rxasm);
            axp->rxasm = NULLBUF;
            axp->segremain = 0;
            free_p(bp);
        }
    } else {
        /* No reassembly in progress */
        if(pid == PID_SEGMENT){
            /* Start reassembly */
            seq = PULLCHAR(&bp);
            if(!(seq & SEG_FIRST)){
                free_p(bp);     /* not first seg - error! */
            } else {
                /* Put first segment on list */
                axp->segremain = seq & SEG_REM;
                axp->rxasm = bp;
            }
        } else {
            /* Normal frame; send upstairs */
            handleit(axp,pid,bp);
        }
    }
}
/* New-style frame segmenter. Returns queue of segmented fragments, or
 * original packet if small enough
 */
struct mbuf *
segmenter(bp,ssize)
struct mbuf *bp;        /* Complete packet */
int16 ssize;            /* Max size of frame segments */
{
    struct mbuf *result = NULLBUF;
    struct mbuf *bptmp,*bp1;
    int16 len,offset;
    int segments;
  
    /* See if packet is too small to segment. Note 1-byte grace factor
     * so the PID will not cause segmentation of a 256-byte IP datagram.
     */
    len = len_p(bp);
    if(len <= ssize+1)
        return bp;      /* Too small to segment */
  
    ssize -= 2;             /* ssize now equal to data portion size */
    segments = 1 + (len - 1) / ssize;       /* # segments  */
    offset = 0;
  
    while(segments != 0){
        offset += dup_p(&bptmp,bp,offset,ssize);
        if(bptmp == NULLBUF){
            free_q(&result);
            break;
        }
        /* Make room for segmentation header */
        bp1 = pushdown(bptmp,2);
        bp1->data[0] = PID_SEGMENT;
        bp1->data[1] = --segments;
        if(offset == ssize)
            bp1->data[1] |= SEG_FIRST;
        enqueue(&result,bp1);
    }
    free_p(bp);
    return result;
}
  
static void
handleit(axp,pid,bp)
struct ax25_cb *axp;
int pid;
struct mbuf *bp;
{
    struct axlink *ipp;
  
    for(ipp = Axlink;ipp->funct != NULL;ipp++){
        if(ipp->pid == pid)
            break;
    }
#ifdef MAILBOX /* prevent spurious mailbox starts - K5JB/N5KNX */
    if(MbAx25Ifilter && pid == PID_NO_L3 && axp->user == -1){
        free_p(bp);
        return;
    }
#endif
    if(ipp->funct != NULL)
        (*ipp->funct)(axp->iface,axp,NULLCHAR,NULLCHAR,bp,0);
    else
        free_p(bp);
}
  
#endif /* AX25 */
  
