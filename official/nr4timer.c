/* net/rom level 4 (transport) protocol timer management.
 * Copyright 1989 by Daniel M. Frank, W9NK.  Permission granted for
 * non-commercial distribution only.
 */
  
#include "global.h"
#ifdef NETROM
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "ax25.h"
#include "lapb.h"
#include "netrom.h"
#include "nr4.h"
#include <ctype.h>

#define WZOC_FIX	/* 05Apr2023, Michael Ford (WZ0C) */
  
#undef NR4DEBUG
  
unsigned Nr_timertype = 0;      /* default to binary exponential */
  
/* The ACK timer has expired without any data becoming available.
 * Go ahead and send an ACK.
 */
  
void
nr4ackit(p)
void *p ;
{
    struct nr4cb *cb  = (struct nr4cb *)p ;
    struct nr4hdr rhdr ;
  
#ifdef NR4DEBUG
    printf("ACKIT called.\n") ;
#endif
  
#ifdef WZOC_FIX
    /* 05Apr2023, Michael Ford (WZ0C) */
    cb->rxunacked = 0;
#endif

    stop_timer(&cb->tack);  /* fixed N1BEE 920811 */
    if (cb->qfull)              /* Are we choked? */
        rhdr.opcode = NR4OPACK | NR4CHOKE ;
    else
        rhdr.opcode = NR4OPACK ;
    rhdr.yourindex = cb->yournum ;
    rhdr.yourid = cb->yourid ;
    rhdr.u.ack.rxseq = cb->rxpected ;
  
    nr4sframe(cb->remote.node, &rhdr, NULLBUF) ;
}
  
/* Called when one of the transmit timers has expired */
  
void
nr4txtimeout(p)
void *p ;
{
    struct nr4cb *cb = (struct nr4cb *)p ;
    unsigned seq ;
    struct nr4txbuf *t ;
 
	/* 01Dec2014, Maiko, is CB valid ? check like I did in nr4.c */
	if (!nr4valcb (cb))
	{
		log (-1, "nr4txtimeout - (info) callback disappeared");
		return;
	}

    /* Sanity check */
  
    if (cb->state != NR4STCON)
        return ;
  
    /* Scan through the send window looking for expired timers */
  
    for (seq = cb->ackxpected ;
        nr4between(cb->ackxpected, seq, cb->nextosend) ;
    seq = (seq + 1) & NR4SEQMASK) {

	/* 01Dec2014, Maiko, Make sure we have a cb->window, or MODULO illegal ! */
	if (!(cb->window))
	{
		log (-1, "nr4txtimeout - (info) zero callback window");
		return;
	}

        t = &cb->txbufs[seq % cb->window] ;
  
        if (t->tretry.state == TIMER_EXPIRE) {
            t->tretry.state = TIMER_STOP ;  /* So we don't do it again */
            /* This thing above fails because the timer code
               itself does the reverse, changing TIMER_STOP to
               TIMER_EXPIRE.  What we really want to do here
               is properly restart the timer.  -- N1BEE */
            /* start_timer(&(t->tretry)); */
  
            if (t->retries == Nr4retries) {
                cb->dreason = NR4RTIMEOUT ;
                nr4state(cb, NR4STDISC) ;
            }
  
            t->retries++ ;
  
            /* We keep track of the highest retry count in the window. */
            /* If packet times out and its new retry count exceeds the */
            /* max, we update the max and bump the backoff level.  This */
            /* expedient is to avoid bumping the backoff level for every */
            /* expiration, since with more than one timer we would back */
            /* off way too fast (and at a rate dependent on the window */
            /* size! */
  
            if (t->retries > cb->txmax) {
                cb->blevel++ ;
                cb->txmax = t->retries ;    /* update the max */
            }
  
            nr4sbuf(cb,seq) ;   /* Resend buffer */
        }
    }
  
}
  
/* Connect/disconnect acknowledgement timeout */
  
void
nr4cdtimeout(p)
void *p ;
{
    struct nr4cb *cb = (struct nr4cb *)p ;
    struct nr4hdr hdr ;
  
    switch(cb->state) {
        case NR4STCPEND:
            if (cb->cdtries == Nr4retries) {    /* Have we tried long enough? */
                cb->dreason = NR4RTIMEOUT ;
                nr4state(cb, NR4STDISC) ;       /* Give it up */
            } else {
            /* Set up header */
  
                hdr.opcode = NR4OPCONRQ ;
                hdr.u.conreq.myindex = cb->mynum ;
                hdr.u.conreq.myid = cb->myid ;
                hdr.u.conreq.window = Nr4window ;
                memcpy(hdr.u.conreq.user,cb->local.user,AXALEN) ;
                memcpy(hdr.u.conreq.node,cb->local.node,AXALEN) ;
  
            /* Bump tries counter and backoff level, and restart timer */
            /* We use a linear or binary exponential backoff. */
  
                cb->cdtries++ ;
                cb->blevel++ ;
  
                if(Nr_timertype)
                /* linear */
                    set_timer(&cb->tcd,dur_timer(&cb->tcd)+cb->srtt);
                else
                /* exponential */
                    set_timer(&cb->tcd,dur_timer(&cb->tcd)*2);
  
                start_timer(&cb->tcd) ;
  
            /* Send connect request packet */
  
                nr4sframe(cb->remote.node,&hdr, NULLBUF) ;
            }
            break ;
  
        case NR4STDPEND:
            if (cb->cdtries == Nr4retries) {    /* Have we tried long enough? */
                cb->dreason = NR4RTIMEOUT ;
                nr4state(cb, NR4STDISC) ;       /* Give it up */
            } else {
            /* Format header */
  
                hdr.opcode = NR4OPDISRQ ;
                hdr.yourindex = cb->yournum ;
                hdr.yourid = cb->yourid ;
  
            /* Bump retry count and start timer */
            /* We don't really need to be fancy here, since we */
            /* should have a good idea of the round trip time by now. */
  
                cb->cdtries++ ;
                start_timer(&cb->tcd) ;
  
            /* Send disconnect request packet */
  
                nr4sframe(cb->remote.node,&hdr, NULLBUF) ;
            }
            break ;
    }
}
  
/* The choke timer has expired.  Unchoke and kick. */
  
void
nr4unchoke(p)
void *p ;
{
    struct nr4cb *cb = (struct nr4cb *)p ;
  
    cb->choked = 0 ;
    nr4output(cb) ;
}
  
#endif /* NETROM */
  
