/* net/rom level 4 (transport) protocol user level calls
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
  
#undef NR4DEBUG
  
/* Open a NET/ROM transport connection */
struct nr4cb *
open_nr4(local,remote,mode,r_upcall,t_upcall,s_upcall,user)
struct nr4_addr *local ;    /* local node address */
struct nr4_addr *remote ;   /* destination node address */
int mode ;          /* active/passive/server */
void (*r_upcall)__ARGS((struct nr4cb *,int)) ;      /* received data upcall */
void (*t_upcall)__ARGS((struct nr4cb *,int)) ;      /* transmit upcall */
void (*s_upcall)__ARGS((struct nr4cb *,int,int)) ;  /* state change upcall */
int user ;          /* user linkage area */
{
    struct nr4cb *cb ;
    struct nr4hdr hdr ;
    struct nr4_addr nr4tmp;
  
    if ((cb = new_n4circ()) == NULLNR4CB)
        return NULLNR4CB ;      /* No circuits available */
  
    if(remote == NULLNRADDR){
        remote = &nr4tmp;
        setcall(remote->user," ");
        setcall(remote->node," ");
    }
  
    /* Stuff what info we can into control block */
  
    ASSIGN(cb->remote,*remote) ;
    /* Save local address for connect retries */
    ASSIGN(cb->local,*local) ;
  
    cb->r_upcall = (void (*)(struct nr4cb *,int16)) r_upcall ;
    cb->t_upcall = (void (*)(struct nr4cb *,int16)) t_upcall ;
    cb->s_upcall = s_upcall ;
    cb->user = user ;
    cb->clone = 0 ;
  
    switch(mode){
        case AX_SERVER:
            cb->clone = 1;
        case AX_PASSIVE:    /* Note fall-thru */
            cb->state = NR4STLISTEN;
            return cb;
        case AX_ACTIVE:
            break;
    }
    /* Format connect request header */
  
    hdr.opcode = NR4OPCONRQ ;
    hdr.u.conreq.myindex = cb->mynum ;
    hdr.u.conreq.myid = cb->myid ;
    hdr.u.conreq.window = Nr4window ;
    memcpy(hdr.u.conreq.user,local->user,AXALEN);
  
    /* If I have a unique callsign per interface, then a layer violation */
    /* will be required to determine the "real" callsign for my */
    /* (virtual) node.  This suggests that callsign-per-interface is not */
    /* desirable, which answers *that* particular open question. */
  
    memcpy(hdr.u.conreq.node,local->node,AXALEN);
  
    /* Set and start connection retry timer */
  
    cb->cdtries = 1 ;
    cb->srtt = Nr4irtt ;
    set_timer(&cb->tcd,2 * cb->srtt);
    cb->tcd.func = nr4cdtimeout ;
    cb->tcd.arg = cb ;
    start_timer(&cb->tcd) ;
  
    /* Send connect request packet */
  
    nr4sframe(remote->node,&hdr,NULLBUF) ;
  
    /* Set up initial state and signal state change */
  
    cb->state = NR4STDISC ;
    nr4state(cb, NR4STCPEND) ;
  
    /* Return control block address */
  
    return cb ;
}
  
/* Send a net/rom transport data packet */
/* Modified to fragment packets larger then Nr_iface->mtu
 * into smaller ones. This allows NETROM to work over ax.25 connections
 * with paclen < 256, WITHOUT fragmenting in the ax.25 layer (ie. v2.1)
 * Thus staying compatible with Thenet, BPQ etc... - WG7J
 */
int
send_nr4(cb,bp)
struct nr4cb *cb ;
struct mbuf *bp ;
{
    struct mbuf *bp1;
    int16 offset,len,size;
  
    if (cb == NULLNR4CB || bp == NULLBUF) {
        free_p(bp);
        return -1 ;
    }
  
    if((len = len_p(bp)) > Nr_iface->mtu) {
        offset = 0;
        /* It is important that all the pushdowns be done before
         * any part of the original packet is freed.
         * Otherwise the pushdown might erroneously overwrite
         * a part of the packet that had been duped and freed.
         */
        while(len != 0){
            size = min(len,Nr_iface->mtu);
            dup_p(&bp1,bp,offset,size);
            len -= size;
            offset += size;
            enqueue(&cb->txq,bp1);
        }
        free_p(bp);
    } else
        enqueue(&cb->txq,bp);
  
    return nr4output(cb) ;
}
  
/* Receive incoming net/rom transport data */
struct mbuf *
recv_nr4(cb,cnt)
struct nr4cb *cb ;
int16 cnt ;
{
    struct mbuf *bp ;
  
    if (cb->rxq == NULLBUF)
        return NULLBUF ;
  
    if (cnt == 0) {
        bp = cb->rxq ;          /* Just give `em everything */
        cb->rxq = NULLBUF ;
    }
    else {
        bp = ambufw(cnt);
        bp->cnt = pullup(&cb->rxq,bp->data,cnt);
    }
    /* If this has un-choked us, reopen the window */
    if (cb->qfull && len_p(cb->rxq) < Nr4qlimit) {
        cb->qfull = 0 ;             /* Choke flag off */
        nr4ackit(cb) ;      /* Get things rolling again */
    }
  
    return bp ;
}
  
/* Close a NET/ROM connection */
void
disc_nr4(cb)
struct nr4cb *cb ;
{
    struct nr4hdr hdr ;
  
    if (cb->state == NR4STLISTEN) {
        free_n4circ(cb);
        return;
    }
    if (cb->state != NR4STCON)
        return ;
  
    /* Format disconnect request packet */
  
    hdr.opcode = NR4OPDISRQ ;
    hdr.yourindex = cb->yournum ;
    hdr.yourid = cb->yourid ;
  
    /* Set and start timer */
  
    cb->cdtries = 1 ;
    set_timer(&cb->tcd,2 * cb->srtt);
    cb->tcd.func = nr4cdtimeout ;
    cb->tcd.arg = cb ;
    start_timer(&cb->tcd) ;
  
    /* Send packet */
  
    nr4sframe(cb->remote.node, &hdr, NULLBUF) ;
  
    /* Signal state change.  nr4state will take care of stopping */
    /* the appropriate timers and resetting window pointers. */
  
    nr4state(cb, NR4STDPEND) ;
  
}
  
/* Abruptly terminate a NET/ROM transport connection */
void
reset_nr4(cb)
struct nr4cb *cb ;
{
    /* Be nice and send a disconnect frame */
    disc_nr4(cb);
    cb->dreason = NR4RRESET ;
    nr4state(cb,NR4STDISC) ;
}
  
  
/* Force retransmission on a NET/ROM transport connection */
int
kick_nr4(cb)
struct nr4cb *cb ;
{
    unsigned seq ;
    struct timer *t ;
  
    if(!nr4valcb(cb))
        return -1 ;
  
    switch (cb->state) {
        case NR4STCPEND:
        case NR4STDPEND:
            stop_timer(&cb->tcd) ;
            nr4cdtimeout(cb) ;
            break ;
  
        case NR4STCON:
            if (cb->nextosend != cb->ackxpected) {  /* if send window is open: */
                for (seq = cb->ackxpected ;
                    nr4between(cb->ackxpected, seq, cb->nextosend) ;
                seq = (seq + 1) & NR4SEQMASK) {
                    t = &cb->txbufs[seq % cb->window].tretry ;
                    stop_timer(t) ;
                    t->state = TIMER_EXPIRE ;   /* fool retry routine */
                }
                nr4txtimeout(cb) ;
            }
            break ;
    }
  
    return 0 ;
}
  
#endif /* NETROM */
  
