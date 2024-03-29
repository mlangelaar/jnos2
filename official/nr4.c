/* net/rom level 4 (transport) protocol implementation
 * Copyright 1989 by Daniel M. Frank, W9NK.  Permission granted for
 * non-commercial distribution only.
 * Ported to NOS by SM0RGV, 890525.
 * Inactivity timeout by WG7J, 920325
 */
  
#include <stdio.h>
#include "global.h"
#ifdef NETROM
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "ax25.h"
#include "lapb.h"
#include "netrom.h"
#include "nr4.h"
#include "cmdparse.h"
#include <ctype.h>

#define WZOC_FIX	/* 05Apr2023, Michael Ford (WZ0C) */

#undef NR4DEBUG
 

/* Globals: */
  
/* The circuit table */
  
struct nr4circp Nr4circuits[NR4MAXCIRC];
  
/* Various limits */
  
unsigned short Nr4window = 4;           /* Max window to negotiate */
unsigned short Nr4retries = 10; /* Max retries */
unsigned short Nr4qlimit = 2048;        /* Max bytes on receive queue */
  
/* Timers */
  
int32 Nr4irtt = 45000;          /* Initial round trip time */
int32 Nr4acktime = 3000;                /* ACK delay timer */
int32 Nr4choketime = 180000;    /* CHOKEd state timeout */
  
static void nr4ackours __ARGS((struct nr4cb *, unsigned, int));
static void nr4choke __ARGS((struct nr4cb *));
static void nr4gotnak __ARGS((struct nr4cb *, unsigned));
static void nr4rframe __ARGS((struct nr4cb *, unsigned, struct mbuf *));
  
#ifdef NR4TDISC
  
int32 Nr4tdiscinit = 0UL;          /* Inactivity timeout - WG7J */
  
int donr4tdisc (int argc, char **argv, void *p)
{
    return setint32 (&Nr4tdiscinit, "NR4 redundancy timer (sec)", argc, argv);
}
  
/* Changes are if we reachs this, with 'normal' values of tdisc,
 * the other end has already timed  and retried out.
 * So simply reset the control block - WG7J
 */
  
#endif /* NR4TDISC */
  
  
  
/* This function is called when a net/rom layer four frame */
/* is discovered inside a datagram addressed to us */
  
void
nr4input(hdr,bp)
struct nr4hdr *hdr;
struct mbuf *bp;
{
    struct nr4hdr rhdr;
    struct nr4cb *cb, *cb2;
    int op;
    unsigned window;
    int acceptc;            /* indicates that connection should be accepted */
    int newconn;            /* indicates that this is a new incoming */
                            /* connection.  You'll see. */
    int gotchoke;           /* The choke flag was set in this packet */
    int i;
    int32 t;
  
    op = hdr->opcode & NR4OPCODE;   /* Mask off flags */
  
    rhdr.flags = 0;
#ifdef G8BPQ
    if(G8bpq && hdr->flags & NR4_G8BPQMASK)
        rhdr.flags = NR4_G8BPQMASK;
#endif
  
    if(op == NR4OPCONRQ){                   /* process connect request first */
        acceptc = 1;
        newconn = 0;
  
        /* These fields are sent regardless of success */
        rhdr.yourindex = hdr->u.conreq.myindex;
        rhdr.yourid = hdr->u.conreq.myid;
  
        /* Check to see if we have already received a connect */
        /* request for this circuit. */
        if((cb = match_n4circ(hdr->u.conreq.myindex,
            hdr->u.conreq.myid,hdr->u.conreq.user,hdr->u.conreq.node))
        == NULLNR4CB) { /* No existing circuit if NULL */
  
            /* If low on memory, don't accept - WG7J */
            /* Try to get a new circuit */
            if(
#ifndef UNIX
               (availmem() < Memthresh) ||
#endif
                                          ((cb = new_n4circ()) == NULLNR4CB))
                acceptc = 0;
            else {
                /* See if we have any listening sockets */
                for(i = 0; i < NR4MAXCIRC; i++){
                    if((cb2 = Nr4circuits[i].ccb) == NULLNR4CB)
                        continue;/* not an open circuit */
                    if(cb2->state == NR4STLISTEN)
                        /* A listener was found */
                        break;
                }
                if(i == NR4MAXCIRC){ /* We are refusing connects */
                    acceptc = 0;
                    free_n4circ(cb);
                }
                if(acceptc){
                    /* Load the listeners settings */
                    cb->clone = cb2->clone;
                    cb->user = cb2->user;
                    cb->t_upcall = cb2->t_upcall;
                    cb->s_upcall = cb2->s_upcall;
                    cb->r_upcall = cb2->r_upcall;
                    ASSIGN(cb->local,cb2->local);
  
                    /* Window is set to min of the offered
                     * and local windows
                     */
                    window = hdr->u.conreq.window > Nr4window ?
                    Nr4window : hdr->u.conreq.window;
  
                    if(init_nr4window(cb, window) == -1){
                        free_n4circ(cb);
                        acceptc = 0;
                    } else {
                        /* Set up control block */
                        cb->yournum = hdr->u.conreq.myindex;
                        cb->yourid = hdr->u.conreq.myid;
                        memcpy(cb->remote.user,
                        hdr->u.conreq.user,AXALEN);
                        memcpy(cb->remote.node,
                        hdr->u.conreq.node,AXALEN);
                        /* Default round trip time */
                        cb->srtt = Nr4irtt;
                        /* set up timers, window pointers */
                        nr4defaults(cb);
                        cb->state = NR4STDISC;
                        newconn = 1;
                    } /* End if window successfully allocated */
                }   /* End if new circuit available */
            } /* End of memory-low else */
        } /* End if no existing circuit matching parameters */
  
        /* Now set up response */
        if(!acceptc){
            rhdr.opcode = NR4OPCONAK | NR4CHOKE;/* choke means reject */
            rhdr.u.conack.myindex = 0;
            rhdr.u.conack.myid = 0;
            rhdr.u.conack.window = 0;
        } else {
            rhdr.opcode = NR4OPCONAK;
            rhdr.u.conack.myindex = cb->mynum;
            rhdr.u.conack.myid = cb->myid;
            rhdr.u.conack.window = cb->window;
        }
        nr4sframe(hdr->u.conreq.node, &rhdr, NULLBUF);
  
        /* Why, you ask, do we wait until now for the state change
         * upcall?  Well, it's like this:  if the state change triggers
         * something like the mailbox to send its banner, the banner
         * would have gone out *before* the conn ack if we'd done this
         * in the code above.  This is what happens when you don't plan
         * too well.  Learn from my mistakes :-)
         */
        if(newconn)
            nr4state(cb, NR4STCON);/* connected (no 3-way handshake) */
  
        free_p(bp);
        return;
    } /* end connect request code */
  
    /* validate circuit number */
    if((cb = get_n4circ(hdr->yourindex, hdr->yourid)) == NULLNR4CB){
        free_p(bp);
        return;
    }
  
    /* Check for choke flag */
    if(hdr->opcode & NR4CHOKE)
        gotchoke = 1;
    else
        gotchoke = 0;
  
    /* Here's where the interesting stuff gets done */
    switch(cb->state){
        case NR4STCPEND:
        switch(op){
            case NR4OPCONAK:
            /* Save the round trip time for later use */
                t = dur_timer(&cb->tcd) - read_timer(&cb->tcd);
                stop_timer(&cb->tcd);
                if(gotchoke){           /* connect rejected */
                    cb->dreason = NR4RREFUSED;
                    nr4state(cb, NR4STDISC);
                    break;
                }
                cb->yournum = hdr->u.conack.myindex;
                cb->yourid = hdr->u.conack.myid;
                window = hdr->u.conack.window > Nr4window ?
                Nr4window : hdr->u.conack.window;
  
                if(init_nr4window(cb, window) == -1){
                    cb->dreason = NR4RRESET;
                    nr4state(cb, NR4STDISC);
                } else {
                    nr4defaults(cb);        /* set up timers, window pointers */
  
                    if(cb->cdtries == 1)    /* No retries */
                    /* Use measured rtt */
                        cb->srtt = t;
                    else
                    /* else use default */
                        cb->srtt = Nr4irtt;
  
                    nr4state(cb, NR4STCON);
                    nr4output(cb);          /* start sending anything on the txq */
                }
                break;
            default:
            /* We can't respond to anything else without
             * Their ID and index
             */
                free_p(bp);
                return;
        }
            break;
        case NR4STCON:
        switch(op){
            case NR4OPDISRQ:
            /* format reply packet */
                rhdr.opcode = NR4OPDISAK;
                rhdr.yourindex = cb->yournum;
                rhdr.yourid = cb->yourid;
                nr4sframe(cb->remote.node,&rhdr,NULLBUF);
                cb->dreason = NR4RREMOTE;
                nr4state(cb, NR4STDISC);
                break;
            case NR4OPINFO:
            /* Do receive frame processing */
                nr4rframe(cb, hdr->u.info.txseq, bp);
  
            /* Reset the choke flag if no longer choked.  Processing
             * the ACK will kick things off again.
             */
                if(cb->choked && !gotchoke){
                    stop_timer(&cb->tchoke);
                    cb->choked = 0;
                }
  
            /* We delay processing the receive sequence number until
             * now, because the ACK might pull more off the txq and send
             * it, and we want the implied ACK in those frames to be right
             *
             * Only process NAKs if the choke flag is off.  It appears
             * that NAKs should never be sent with choke on, by the way,
             * but you never know, considering that there is no official
             * standard for this protocol
             */
                if(hdr->opcode & NR4NAK && !gotchoke)
                    nr4gotnak(cb, hdr->u.info.rxseq);
  
            /* We always do ACK processing, too, since the NAK of one
             * packet may be the implied ACK of another.  The gotchoke
             * flag is used to prevent sending any new frames, since
             * we are just going to purge them next anyway if this is
             * the first time we've seen the choke flag.  If we are
             * already choked, this call will return immediately.
             */
                nr4ackours(cb, hdr->u.info.rxseq, gotchoke);
  
            /* If we haven't seen the choke flag before, purge the
             * send window and set the timer and the flag.
             */
                if(!cb->choked && gotchoke)
                    nr4choke(cb);
                break;
            case NR4OPACK:
                if(cb->choked && !gotchoke){
                /* clear choke if appropriate */
                    stop_timer(&cb->tchoke);
                    cb->choked = 0;
                }
                if(hdr->opcode & NR4NAK && !gotchoke)
                    nr4gotnak(cb, hdr->u.ack.rxseq);        /* process NAKs */
  
                nr4ackours(cb, hdr->u.ack.rxseq, gotchoke); /* and ACKs */
  
                if(!cb->choked && gotchoke)     /* First choke seen */
                    nr4choke(cb);           /* Set choke status */
  
                break;
        }
            break;
        case NR4STDPEND:
        switch(op){
            case NR4OPDISAK:
                cb->dreason = NR4RNORMAL;
                nr4state(cb, NR4STDISC);
                break;
            case NR4OPINFO:
            /* We can still do receive frame processing until
             * the disconnect acknowledge arrives, but we won't
             * bother to process ACKs, since we've flushed our
             * transmit buffers and queue already.
             */
                nr4rframe(cb, hdr->u.info.txseq, bp);
                break;
        }
    }       /* End switch(state) */
}
  
  
/* Send a net/rom layer 4 frame.  bp should be NULLBUF unless the frame
 * type is info.
 */
void
nr4sframe(dest, hdr, bp)
char *dest;
struct nr4hdr *hdr;
struct mbuf *bp;
{
    struct mbuf *n4b;
  
    if((n4b = htonnr4(hdr)) == NULLBUF){
        free_p(bp);
        return;
    } else {
        append(&n4b, bp);
        nr3output(dest, n4b);
    }
}
  
/* Receive frame processing */
static void
nr4rframe(cb, rxseq, bp)
struct nr4cb *cb;
unsigned rxseq;
struct mbuf *bp;
{
    struct nr4hdr rhdr;
    unsigned window = cb->window;
    unsigned rxbuf = rxseq % window;
    unsigned newdata = 0;           /* whether to upcall */
  
#ifdef NR4DEBUG
    printf("Processing received info\n");
#endif
  
#ifdef NR4TDISC
    /* We received data, reset the inactivity timer - WG7J */
    start_timer(&cb->tdisc);
#endif
  
    /* If we're choked, just reset the ACK timer to blast out
     * another CHOKE indication after the ackdelay
     */
    if(cb->qfull){
        start_timer(&cb->tack);
        return;
    }
  
    /* If frame is out of sequence, it is either due to a lost frame
     * or a retransmission of one seen earlier.  We do not want to NAK
     * the latter, as the far end would see this as a requirement to
     * retransmit the expected frame, which is probably already in the
     * pipeline.  This in turn would cause another out-of-sequence
     * condition, another NAK, and the process would repeat indefinitely.
     * Therefore, if the frame is out-of-sequence, but within the last
     * 'n' frames by sequence number ('n' being the window size), just
     * accept it and discard it.  Else, NAK it if we haven't already.
     *      (Modified by Rob Stampfli, kd8wk, 9 Jan 1990)
     */
    if(rxseq != cb->rxpected && !cb->naksent){
#ifdef NR4DEBUG
        printf("Frame out of sequence -- expected %u, got %u.\n",
        cb->rxpected, rxseq);
#endif
        if(nr4between(cb->rxpected,
            (rxseq + window) & NR4SEQMASK, cb->rxpastwin))
            /* just a repeat of old frame -- queue ack for
             * expected frame
             */
            start_timer(&cb->tack);
        else {                  /* really bogus -- a NAKable frame */
            rhdr.opcode = NR4OPACK | NR4NAK;
            rhdr.yourindex = cb->yournum;
            rhdr.yourid = cb->yourid;
            rhdr.u.ack.rxseq = cb->rxpected;
            nr4sframe(cb->remote.node,&rhdr,NULLBUF);
  
            /* Now make sure we don't send any more of these until
             * we see some good data.  Otherwise full window
             * retransmissions would result in a flurry of NAKs
             */
  
            cb->naksent = 1;
        }
    }
  
    /* If this is a new frame, within the window, buffer it,
     * then see what we can deliver
     */
    if(nr4between(cb->rxpected,rxseq,cb->rxpastwin)
    && !cb->rxbufs[rxbuf].occupied){
#ifdef NR4DEBUG
        printf("Frame within window\n");
#endif
        cb->rxbufs[rxbuf].occupied = 1;
        cb->rxbufs[rxbuf].data = bp;
  
        for(rxbuf = cb->rxpected % window; cb->rxbufs[rxbuf].occupied;
        rxbuf = cb->rxpected % window){
#ifdef NR4DEBUG
            printf("Removing frame from buffer %d\n", rxbuf);
#endif
            newdata = 1;
            cb->rxbufs[rxbuf].occupied = 0;
            append(&cb->rxq,cb->rxbufs[rxbuf].data);
            cb->rxbufs[rxbuf].data = NULLBUF;
            cb->rxpected = (cb->rxpected + 1) & NR4SEQMASK;
            cb->rxpastwin = (cb->rxpastwin + 1) & NR4SEQMASK;
        }
        if(newdata){
            cb->naksent = 0;        /* OK to send NAKs again */
            if(cb->r_upcall != NULLVFP((struct nr4cb*,int16)))
                (*cb->r_upcall)(cb,len_p(cb->rxq));
  
            /* Now that our upcall has had a shot at the queue, */
            /* see if it's past the queue length limit.  If so, */
            /* go into choked mode (i.e. flow controlled). */

#ifdef WZOC_FIX
	/* 05Apr2023, Michael Ford (WZ0C) */
            cb->rxunacked++;
#endif
            if(len_p(cb->rxq) > Nr4qlimit){
                cb->qfull = 1;
                nr4ackit((void *)cb);   /* Tell `em right away */
#ifdef WZOC_FIX
	/* 05Apr2023, Michael Ford (WZ0C) */
            } else if( cb->nbuffered == 0 && cb->rxunacked >= 4 ){
                nr4ackit((void *)cb);   /* Tell `em right away */
#endif
            } else
                start_timer(&cb->tack);
        }
    } else  /* It's out of the window or we've seen it already */
        free_p(bp);
}
  
  
/* Send the transmit buffer whose sequence number is seq */
void
nr4sbuf(cb, seq)
struct nr4cb *cb;
unsigned seq;
{
    struct nr4hdr hdr;
    struct mbuf *bufbp, *bp;
    unsigned bufnum = seq % cb->window;
    struct timer *t;
  
	/* 30Nov2014, Maiko, We should validate the cb, it seems that since
	 * me tinkering with netrom parameters in autoexec.nos, JNOS has been
	 * crashing inconsistently on incoming NETROM connects, and the debug
	 * traces are suggesting I've lost the BP, therefore the CB, so check
	 * the validity of the CB before we go any further to make sure !
	 */
	if (!nr4valcb (cb))
	{
		log (-1, "nr4sbuf - (info) callback disappeared");
		return;
	}

    /* sanity check */
    if(bufnum >= cb->window){
#ifdef NRDEBUG
        printf("sbuf: buffer number %u beyond window\n",bufnum);
#endif
        return;
    }
  
    /* Stop the ACK timer, since our sending of the frame is
     * an implied ACK.
     */
#ifdef WZOC_FIX
    /* 05Apr2023, Michael Ford (WZ0C) */
    cb->rxunacked = 0;
#endif
    stop_timer(&cb->tack);
  
    /* Duplicate the mbuf, since we have to keep it around
     * until it is acknowledged
     */
    bufbp = cb->txbufs[bufnum].data;
  
    /* Notice that we use copy_p instead of dup_p.  This is because
     * a frame can still be sitting on the AX.25 send queue when it
     * get acknowledged, and we don't want to deallocate its data
     * before it gets sent!
     */
    if((bp = copy_p(bufbp, len_p(bufbp))) == NULLBUF){
        free_mbuf(bp);
        return;
    }
  
    /* Prepare the header */
    if(cb->qfull)                           /* are we choked? */
        hdr.opcode = NR4OPINFO | NR4CHOKE;
    else
        hdr.opcode = NR4OPINFO;
    hdr.yourindex = cb->yournum;
    hdr.yourid = cb->yourid;
    hdr.u.info.txseq = (unsigned char)(seq & NR4SEQMASK);
    hdr.u.info.rxseq = cb->rxpected;
  
    /* Send the frame, then set and start the timer */
    nr4sframe(cb->remote.node, &hdr, bp);
  
    t = &cb->txbufs[bufnum].tretry;
    set_timer(t, (1 << cb->blevel) * (4 * cb->mdev + cb->srtt));
    start_timer(t);
}
  
/* Check to see if any of our frames have been ACKed */
  
static void
nr4ackours(cb, seq, gotchoke)
struct nr4cb *cb;
unsigned seq;
int gotchoke;   /* The choke flag is set in the received frame */
{
    unsigned txbuf;
    struct timer *t;
  
    /* If we are choked, there is nothing in the send window
     * by definition, so we can just return.
     */
    if(cb->choked)
        return;
  
    /* Adjust seq to point to the frame being ACK'd, not the one
     * beyond it, which is how it arrives.
     */
    seq = (seq - 1) & NR4SEQMASK;
  
    /* Free up all the ack'd frames, and adjust the round trip
     * timing stuff
     */
    while (nr4between(cb->ackxpected, seq, cb->nextosend)){
#ifdef NR4DEBUG
        printf("Sequence # %u acknowledged\n", seq);
#endif
        cb->nbuffered--;
        txbuf = cb->ackxpected % cb->window;
        free_mbuf(cb->txbufs[txbuf].data);
        cb->txbufs[txbuf].data = NULLBUF;
        cb->ackxpected = (cb->ackxpected + 1) & NR4SEQMASK;
  
        /* Round trip time estimation, cribbed from TCP */
        if(cb->txbufs[txbuf].retries == 0){
            /* We only sent this one once */
            int32 rtt;
            int32 abserr;
  
            t = &cb->txbufs[txbuf].tretry;
            /* get our rtt in msec */
            rtt = dur_timer(t) - read_timer(t);
            abserr = (rtt > cb->srtt) ? rtt - cb->srtt : cb->srtt - rtt;
            cb->srtt = (cb->srtt * 7 + rtt) >> 3;
            cb->mdev = (cb->mdev * 3 + abserr) >> 2;
  
            /* Reset the backoff level */
            cb->blevel = 0;
        }
        stop_timer(&cb->txbufs[txbuf].tretry);
    }
    /* Now we recalculate tmax, the maximum number of retries for
     * any frame in the window.  tmax is used as a baseline to
     * determine when the window has reached a new high in retries.
     * We don't want to increment blevel for every frame that times
     * out, since that would lead to us backing off too fast when
     * all the frame timers expired at around the same time.
     */
    cb->txmax = 0;
  
    for(seq = cb->ackxpected;
        nr4between(cb->ackxpected, seq, cb->nextosend);
        seq = (seq + 1) & NR4SEQMASK)
        if(cb->txbufs[seq % cb->window].retries > cb->txmax)
            cb->txmax = cb->txbufs[seq % cb->window].retries;
  
    /* This is kind of a hack.  This function is called under
     * three different conditions:  either we are choked, in
     * which case we return immediately, or we are not choked,
     * in which case we proceed normally to keep the send
     * window full, or we have seen the choke flag for the first
     * time.  In the last case, gotchoke is true while cb->choked
     * is false.  We want to process any acknowledgments of existing
     * frames in the send window before we purge it, while at the
     * same time we don't want to take anything else off the txq
     * or send it out.  So, in the third case we listed, we return
     * now since we've processed the ACK.
     */
  
    if(gotchoke)
        return;
  
    nr4output(cb);                  /* yank stuff off txq and send it */
  
    /* At this point, either the send window is full, or
     * nr4output() didn't find enough on the txq to fill it.
     * If the window is not full, then the txq must be empty,
     * and we'll make a tx upcall
     */
    if(cb->nbuffered < cb->window && cb->t_upcall != NULLVFP((struct nr4cb*,int16)))
        (*cb->t_upcall)(cb, (int16)((cb->window - cb->nbuffered) * NR4MAXINFO));
  
}
  
  
/* If the send window is open and there are frames on the txq,
 * move as many as possible to the transmit buffers and send them.
 * Return the number of frames sent.
 */
int
nr4output(cb)
struct nr4cb *cb;
{
    int numq, i;
    struct mbuf *bp;
    struct nr4txbuf *tp;
  
    /* Are we in the proper state? */
    if(cb->state != NR4STCON || cb->choked)
        return 0;               /* No sending if not connected */
                    /* or if choked */
  
    /* See if the window is open */
    if(cb->nbuffered >= cb->window)
        return 0;
  
    numq = len_q(cb->txq);
  
#ifdef NR4DEBUG
    printf("nr4output: %d packets on txq\n", numq);
#endif
  
    for(i = 0; i < numq; i++){
        bp = dequeue(&cb->txq);
#ifdef NR4DEBUG
        if(len_p(bp) > NR4MAXINFO){     /* should be checked higher up */
            printf("Upper layers queued too big a buffer\n");
            continue;
        }
#endif
        /* Set up and send buffer */
        tp = &cb->txbufs[cb->nextosend % cb->window];
        tp->retries = 0;
        tp->data = bp;
        nr4sbuf(cb, cb->nextosend);
  
        /* Update window and buffered count */
        cb->nextosend = (cb->nextosend + 1) & NR4SEQMASK;
        if(++cb->nbuffered >= cb->window)
            break;
    }
    return i;
}
  
void
nr4state(cb, newstate)
struct nr4cb *cb;
int newstate;
{
    int i;
    int oldstate = cb->state;
  
    cb->state = newstate;
  
    switch(cb->state){
#ifdef NR4TDISC
        case NR4STCON:
    /* We're connected now, start inactivity timer - WG7J */
            set_timer(&cb->tdisc,Nr4tdiscinit*1000);
    /*
        cb->tdisc.func = nr4_redundant ;
     * Second try; the call to nr4_redundant() (see above)
     * doesn't seem to catch ALL situations.
     * Changes are if we reachs this, with 'normal' values of tdisc,
     * the other end has already timed out and retried out.
     * So simply reset the control block - WG7J
     */
            cb->tdisc.func = (void (*)__ARGS((void*))) reset_nr4;
            cb->tdisc.arg = cb ;
            start_timer(&cb->tdisc) ;
            break;
#endif
        case NR4STDPEND:
            stop_timer(&cb->tchoke);
#ifdef NR4TDISC
            start_timer(&cb->tdisc);
#endif
  
        /* When we request a disconnect, we lose the contents of
         * our transmit queue and buffers, but we retain our ability
         * to receive any packets in transit until a disconnect
         * acknowledge arrives
         */
            free_q(&cb->txq);
  
            for(i = 0; i < (int)(cb->window); i++){
                free_mbuf(cb->txbufs[i].data);
                cb->txbufs[i].data = NULLBUF;
                stop_timer(&cb->txbufs[i].tretry);
            }
  
        /* Tidy up stats: roll the top window pointer back
         * and reset nbuffered to reflect this.  Not really
         * necessary, but leads to a bit more truth telling
         * in the status displays.
         */
            cb->nextosend = cb->ackxpected;
            cb->nbuffered = 0;
            break;
        case NR4STDISC:
            stop_timer(&cb->tchoke);
            stop_timer(&cb->tack);
            stop_timer(&cb->tcd);
#ifdef NR4TDISC
            stop_timer(&cb->tdisc);
#endif
  
        /* We don't clear the rxq, since the state change upcall
         * may pull something off of it at the last minute.
         */
            free_q(&cb->txq);
  
        /* The following loop will only be executed if the
         * window was set, since when the control block is
         * calloc'd the window field gets a 0 in it.  This
         * protects us from dereferencing an unallocated
         * window buffer pointer
         */
            for(i = 0; i < (int)(cb->window); i++){
                free_mbuf(cb->rxbufs[i].data);
                cb->rxbufs[i].data = NULLBUF;
                free_mbuf(cb->txbufs[i].data);
                cb->txbufs[i].data = NULLBUF;
                stop_timer(&cb->txbufs[i].tretry);
            }
            break;
    }
  
    if(oldstate != newstate && cb->s_upcall != NULLVFP((struct nr4cb*,int,int)))
        (*cb->s_upcall)(cb, oldstate, newstate);
  
    /* We take responsibility for deleting the circuit
     * descriptor.  Don't do this anywhere else!
     */
    if(newstate == NR4STDISC)
        free_n4circ(cb);
}
  
/* Process NAKs.  seq indicates the next frame expected by the
 * NAK'ing station.
 */
  
static void
nr4gotnak(cb, seq)
struct nr4cb *cb;
unsigned seq;
{
    if(nr4between(cb->ackxpected, seq, cb->nextosend))
        nr4sbuf(cb, seq);
}
  
  
/* This is called when we first get a CHOKE indication from the
 * remote.  It purges the send window and sets the choke timer.
 */
  
static void
nr4choke(cb)
struct nr4cb *cb;
{
    unsigned seq;
    struct mbuf *q, *bp;
    struct nr4txbuf *t;
  
    q = cb->txq;
  
    /* We purge the send window, returning the buffers to the
     * txq in the proper order.
     */
    for(seq = (cb->nextosend - 1) & NR4SEQMASK;
        nr4between(cb->ackxpected, seq, cb->nextosend);
    seq = (seq - 1) & NR4SEQMASK){
  
        t = &cb->txbufs[seq % cb->window];
        stop_timer(&t->tretry);
        bp = t->data;
        t->data = NULLBUF;
        enqueue(&bp, q);        /* prepend this packet to the queue */
        q = bp;
    }
  
    cb->nextosend = cb->ackxpected; /* close the window */
    cb->nbuffered = 0;              /* nothing in the window */
    cb->txq = q;                    /* Replace the txq with the one that has */
                    /* the purged packets prepended */
    cb->choked = 1;         /* Set the choked flag */
  
    start_timer(&cb->tchoke);
}
  
#endif /* NETROM */
  
