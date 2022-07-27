/* Process incoming TCP segments. Page number references are to ARPA RFC-793,
 * the TCP specification.
 *
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Mods by PA0GRI (access control)
 * Copyright 1992 Gerard J van der Grinten, PA0GRI
 */
#include "global.h"
#include "timer.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "iface.h"
#include "tcp.h"
#include "icmp.h"
#include "iface.h"
#include "ip.h"
  
static void update __ARGS((struct tcb *tcb,struct tcp *seg,int16 length));
static void proc_syn __ARGS((struct tcb *tcb,char tos,struct tcp *seg));
static void add_reseq __ARGS((struct tcb *tcb,char tos,struct tcp *seg,
struct mbuf *bp,int16 length));
static void get_reseq __ARGS((struct tcb *tcb,char *tos,struct tcp *seq,
struct mbuf **bp,int16 *length));
static int trim __ARGS((struct tcb *tcb,struct tcp *seg,struct mbuf **bpp,
int16 *length));
static int in_window __ARGS((struct tcb *tcb,int32 seq));
  
/* This function is called from IP with the IP header in machine byte order,
 * along with a mbuf chain pointing to the TCP header.
 */
void
tcp_input(iface,ip,bp,rxbroadcast)
struct iface *iface;    /* Incoming interface (ignored) */
struct mbuf *bp;    /* Data field, if any */
struct ip *ip;      /* IP header */
int rxbroadcast;    /* Incoming broadcast - discard if true */
{
    struct tcb *ntcb;
    struct tcb *tcb;   /* TCP Protocol control block */
    struct tcp seg;         /* Local copy of segment header */
    struct connection conn;     /* Local copy of addresses */
    struct pseudo_header ph;    /* Pseudo-header for checksumming */
    int hdrlen;         /* Length of TCP header */
    int16 length;

	int gotone = 0;	/* replaces GOTO 'gotone' label */
  
    if(bp == NULLBUF)
        return;
  
    tcpInSegs++;
    if(rxbroadcast){
        /* Any TCP packet arriving as a broadcast is
         * to be completely IGNORED!!
         */
        free_p(bp);
        return;
    }
    length = ip->length - IPLEN - ip->optlen;
    ph.source = ip->source;
    ph.dest = ip->dest;
    ph.protocol = ip->protocol;
    ph.length = length;
    if(cksum(&ph,bp,length) != 0){
        /* Checksum failed, ignore segment completely */
        tcpInErrs++;
        free_p(bp);
        return;
    }
    /* Form local copy of TCP header in host byte order */
    if((hdrlen = ntohtcp(&seg,&bp)) < 0){
        /* TCP header is too small */
        free_p(bp);
        return;
    }
    length -= hdrlen;
  
    /* Fill in connection structure and find TCB */
    conn.local.address = ip->dest;
    conn.local.port = seg.dest;
    conn.remote.address = ip->source;
    conn.remote.port = seg.source;
  
    if((tcb = lookup_tcb(&conn)) == NULLTCB){
        /* If memory low, reject it - WG7J */
        /* If this segment doesn't carry a SYN, reject it */
        if(
#ifndef UNIX
           (availmem() < Memthresh) ||
#endif
                                      !seg.flags.syn){
            free_p(bp);
            reset(ip,&seg);
            return;
        }
#ifdef  TCPACCESS
        if(tcp_check(TCPaccess,ip->source,seg.dest) != 0 ) {
            free_p(bp);
            reset(ip,&seg);
            return;
        }
#endif
        /* See if there's a TCP_LISTEN on this socket with
         * unspecified remote address and port
         */
        conn.remote.address = 0;
        conn.remote.port = 0;
        /* NOS currently always listens on unspecified addresses ! - WG7J */
        conn.local.address = 0;
        if((tcb = lookup_tcb(&conn)) == NULLTCB){
            /* No LISTENs, so reject */
            free_p(bp);
            reset(ip,&seg);
            return;
        }
        /* We've found an server listen socket, so clone the TCB */
        if(tcb->flags.clone){
            ntcb = (struct tcb *)mallocw(sizeof (struct tcb));
            ASSIGN(*ntcb,*tcb);
            tcb = ntcb;
            tcb->timer.arg = tcb;
            /* Put on list */
            tcb->next = Tcbs;
            Tcbs = tcb;
        }
        /* Put all the socket info into the TCB */
        tcb->conn.local.address = ip->dest;
        tcb->conn.remote.address = ip->source;
        tcb->conn.remote.port = seg.source;
  
        /* Now point to the tcp interface specific parameters if
         * none set. This is only needed for incoming connections.
         * Outgoing ones get the pointer set in open_tcp() - WG7J
         */
        tcb->parms = iface->tcp;
  
        /* Find a known rtt or load interface default */
        set_irtt(tcb);
    }
  
    tcb->flags.congest = ip->flags.congest;
    /* Do unsynchronized-state processing (p. 65-68) */
    switch(tcb->state){
        case TCP_CLOSED:
            free_p(bp);
            reset(ip,&seg);
            return;
        case TCP_LISTEN:
            if(seg.flags.rst){
                free_p(bp);
                return;
            }
            if(seg.flags.ack){
                free_p(bp);
                reset(ip,&seg);
                return;
            }
            if(seg.flags.syn){
            /* (Security check is bypassed) */
            /* page 66 */
                proc_syn(tcb,ip->tos,&seg);
                send_syn(tcb);
                setstate(tcb,TCP_SYN_RECEIVED);
                if(length != 0 || seg.flags.fin) {
                /* Continue processing if there's more */
                    break;
                }
                tcp_output(tcb);
            }
            free_p(bp); /* Unlikely to get here directly */
            return;
        case TCP_SYN_SENT:
            if(seg.flags.ack){
                if(!seq_within(seg.ack,tcb->iss+1,tcb->snd.nxt)){
                    free_p(bp);
                    reset(ip,&seg);
                    return;
                }
            }
            if(seg.flags.rst){  /* p 67 */
                if(seg.flags.ack){
                /* The ack must be acceptable since we just checked it.
                 * This is how the remote side refuses connect requests.
                 */
                    close_self(tcb,RESET);
                }
                free_p(bp);
                return;
            }
        /* (Security check skipped here) */
#ifdef  PREC_CHECK  /* Turned off for compatibility with BSD */
        /* Check incoming precedence; it must match if there's an ACK */
            if(seg.flags.ack && PREC(ip->tos) != PREC(tcb->tos)){
                free_p(bp);
                reset(ip,&seg);
                return;
            }
#endif
            if(seg.flags.syn){
                proc_syn(tcb,ip->tos,&seg);
                if(seg.flags.ack){
                /* Our SYN has been acked, otherwise the ACK
                 * wouldn't have been valid.
                 */
                    update(tcb,&seg,length);
                    setstate(tcb,TCP_ESTABLISHED);
                } else {
                    setstate(tcb,TCP_SYN_RECEIVED);
                }
                if(length != 0 || seg.flags.fin) {
                    break;      /* Continue processing if there's more */
                }
                tcp_output(tcb);
            } else {
                free_p(bp); /* Ignore if neither SYN or RST is set */
            }
            return;
    }
    /* We reach this point directly in any synchronized state. Note that
     * if we fell through from LISTEN or SYN_SENT processing because of a
     * data-bearing SYN, window trimming and sequence testing "cannot fail".
     */
  
    /* Trim segment to fit receive window. */
    if(trim(tcb,&seg,&bp,&length) == -1){
        /* Segment is unacceptable */
        if(!seg.flags.rst){ /* NEVER answer RSTs */
            /* In SYN_RECEIVED state, answer a retransmitted SYN
             * with a retransmitted SYN/ACK.
             */
            if(tcb->state == TCP_SYN_RECEIVED)
                tcb->snd.ptr = tcb->snd.una;
            tcb->flags.force = 1;
            tcp_output(tcb);
        }
        return;
    }
    /* If segment isn't the next one expected, and there's data
     * or flags associated with it, put it on the resequencing
     * queue, ACK it and return.
     *
     * Processing the ACK in an out-of-sequence segment without
     * flags or data should be safe, however.
     */
    if(seg.seq != tcb->rcv.nxt
    && (length != 0 || seg.flags.syn || seg.flags.fin)){
        add_reseq(tcb,ip->tos,&seg,bp,length);
        tcb->flags.force = 1;
        tcp_output(tcb);
        return;
    }
    /* This loop first processes the current segment, and then
     * repeats if it can process the resequencing queue.
     */
    for(;;)
	{
        /* We reach this point with an acceptable segment; all data and flags
         * are in the window, and the starting sequence number equals rcv.nxt
         * (p. 70)
         */
        if(seg.flags.rst){
            if(tcb->state == TCP_SYN_RECEIVED
            && !tcb->flags.clone && !tcb->flags.active){
                /* Go back to listen state only if this was
                 * not a cloned or active server TCB
                 */
                setstate(tcb,TCP_LISTEN);
            } else {
                close_self(tcb,RESET);
            }
            free_p(bp);
            return;
        }
        /* (Security check skipped here) p. 71 */
#ifdef  PREC_CHECK
        /* Check for precedence mismatch */
        if(PREC(ip->tos) != PREC(tcb->tos)){
            free_p(bp);
            reset(ip,&seg);
            return;
        }
#endif
        /* Check for erroneous extra SYN */
        if(seg.flags.syn){
            free_p(bp);
            reset(ip,&seg);
            return;
        }
        /* Check ack field p. 72 */
        if(!seg.flags.ack){
            free_p(bp); /* All segments after synchronization must have ACK */
            return;
        }
        /* Process ACK */
        switch(tcb->state){
            case TCP_SYN_RECEIVED:
                if(seq_within(seg.ack,tcb->snd.una+1,tcb->snd.nxt)){
                    update(tcb,&seg,length);
                    setstate(tcb,TCP_ESTABLISHED);
                } else {
                    free_p(bp);
                    reset(ip,&seg);
                    return;
                }
                break;
            case TCP_ESTABLISHED:
            case TCP_CLOSE_WAIT:
                update(tcb,&seg,length);
                break;
            case TCP_FINWAIT1:  /* p. 73 */
                update(tcb,&seg,length);
                if(tcb->sndcnt == 0){
                /* Our FIN is acknowledged */
                    setstate(tcb,TCP_FINWAIT2);
                }
                break;
            case TCP_FINWAIT2:
                update(tcb,&seg,length);
                break;
            case TCP_CLOSING:
                update(tcb,&seg,length);
                if(tcb->sndcnt == 0){
                /* Our FIN is acknowledged */
                    setstate(tcb,TCP_TIME_WAIT);
                    set_timer(&tcb->timer,MSL2*1000);
                    start_timer(&tcb->timer);
                }
                break;
            case TCP_LAST_ACK:
                update(tcb,&seg,length);
                if(tcb->sndcnt == 0){
                /* Our FIN is acknowledged, close connection */
                    close_self(tcb,NORMAL);
                    return;
                }
                break;
            case TCP_TIME_WAIT:
                start_timer(&tcb->timer);
                break;
        }
  
        /* (URGent bit processing skipped here) */
  
        /* Process the segment text, if any, beginning at rcv.nxt (p. 74) */
        if(length != 0){
            switch(tcb->state){
                case TCP_SYN_RECEIVED:
                case TCP_ESTABLISHED:
                case TCP_FINWAIT1:
                case TCP_FINWAIT2:
                /* Place on receive queue */
                    append(&tcb->rcvq,bp);
                    tcb->rcvcnt += length;
                    tcb->rcv.nxt += length;
                    tcb->rcv.wnd -= length;
                    tcb->flags.force = 1;
                /* Notify user */
                    if(tcb->r_upcall)
                        (*tcb->r_upcall)(tcb,tcb->rcvcnt);
                    break;
                default:
                /* Ignore segment text */
                    free_p(bp);
                    break;
            }
        }
        /* process FIN bit (p 75) */
        if(seg.flags.fin){
            tcb->flags.force = 1;   /* Always respond with an ACK */
  
            switch(tcb->state){
                case TCP_SYN_RECEIVED:
                case TCP_ESTABLISHED:
                    tcb->rcv.nxt++;
                    setstate(tcb,TCP_CLOSE_WAIT);
                    break;
                case TCP_FINWAIT1:
                    tcb->rcv.nxt++;
                    if(tcb->sndcnt == 0){
                    /* Our FIN has been acked; bypass TCP_CLOSING state */
                        setstate(tcb,TCP_TIME_WAIT);
                        set_timer(&tcb->timer,MSL2*1000);
                        start_timer(&tcb->timer);
                    } else {
                        setstate(tcb,TCP_CLOSING);
                    }
                    break;
                case TCP_FINWAIT2:
                    tcb->rcv.nxt++;
                    setstate(tcb,TCP_TIME_WAIT);
                    set_timer(&tcb->timer,MSL2*1000);
                    start_timer(&tcb->timer);
                    break;
                case TCP_CLOSE_WAIT:
                case TCP_CLOSING:
                case TCP_LAST_ACK:
                    break;      /* Ignore */
                case TCP_TIME_WAIT: /* p 76 */
                    start_timer(&tcb->timer);
                    break;
            }
            /* Call the client again so he can see EOF */
            if(tcb->r_upcall)
                (*tcb->r_upcall)(tcb,tcb->rcvcnt);
        }

        /* Scan the resequencing queue, looking for a segment we can handle,
         * and freeing all those that are now obsolete.
         */

		gotone = 0;	/* replaces GOTO 'gotone' label */

        while(tcb->reseq != NULLRESEQ &&
			seq_ge(tcb->rcv.nxt,tcb->reseq->seg.seq))
		{
            get_reseq(tcb,&ip->tos,&seg,&bp,&length);
            if(trim(tcb,&seg,&bp,&length) == 0)
			{
                gotone = 1;
				break;
			}
            /* Segment is an old one; trim has freed it */
        }

		if (!gotone)
        	break;	/* this forces break out of FOR loop */
    }
    tcp_output(tcb);    /* Send any necessary ack */
}
  
/* Process an incoming ICMP response */
void
tcp_icmp(icsource,source,dest,type,code,bpp)
int32 icsource;         /* Sender of ICMP message (not used) */
int32 source;           /* Original IP datagram source (i.e. us) */
int32 dest;         /* Original IP datagram dest (i.e., them) */
char type,code;         /* ICMP error codes */
struct mbuf **bpp;      /* First 8 bytes of TCP header */
{
    struct tcp seg;
    struct connection conn;
    register struct tcb *tcb;
  
    /* Extract the socket info from the returned TCP header fragment
     * Note that since this is a datagram we sent, the source fields
     * refer to the local side.
     */
    ntohtcp(&seg,bpp);
    conn.local.port = seg.source;
    conn.remote.port = seg.dest;
    conn.local.address = source;
    conn.remote.address = dest;
    if((tcb = lookup_tcb(&conn)) == NULLTCB)
        return; /* Unknown connection, ignore */
  
    /* Verify that the sequence number in the returned segment corresponds
     * to something currently unacknowledged. If not, it can safely
     * be ignored.
     */
    if(!seq_within(seg.seq,tcb->snd.una,tcb->snd.nxt))
        return;
  
    /* Destination Unreachable and Time Exceeded messages never kill a
     * connection; the info is merely saved for future reference.
     */
    switch(uchar(type)){
        case ICMP_DEST_UNREACH:
        case ICMP_TIME_EXCEED:
            tcb->type = type;
            tcb->code = code;
            break;
        case ICMP_QUENCH:
         /* Source quench; reduce slowstart threshold to half
         * current window and restart slowstart
         */
            tcb->ssthresh = tcb->cwind / 2;
            tcb->ssthresh = max(tcb->ssthresh,tcb->mss);
        /* Shrink congestion window to 1 packet */
            tcb->cwind = tcb->mss;
            break;
    }
}
/* Send an acceptable reset (RST) response for this segment
 * The RST reply is composed in place on the input segment
 */
void
reset(ip,seg)
struct ip *ip;          /* Offending IP header */
register struct tcp *seg;   /* Offending TCP header */
{
    struct mbuf *hbp;
    struct pseudo_header ph;
    int16 tmp;
  
    if(seg->flags.rst)
        return; /* Never send an RST in response to an RST */
  
    /* Compose the RST IP pseudo-header, swapping addresses */
    ph.source = ip->dest;
    ph.dest = ip->source;
    ph.protocol = TCP_PTCL;
    ph.length = TCPLEN;
  
    /* Swap port numbers */
    tmp = seg->source;
    seg->source = seg->dest;
    seg->dest = tmp;
  
    if(seg->flags.ack){
        /* This reset is being sent to clear a half-open connection.
         * Set the sequence number of the RST to the incoming ACK
         * so it will be acceptable.
         */
        seg->flags.ack = 0;
        seg->seq = seg->ack;
        seg->ack = 0;
    } else {
        /* We're rejecting a connect request (SYN) from TCP_LISTEN state
         * so we have to "acknowledge" their SYN.
         */
        seg->flags.ack = 1;
        seg->ack = seg->seq;
        seg->seq = 0;
        if(seg->flags.syn)
            seg->ack++;
    }
    /* Set remaining parts of packet */
    seg->flags.urg = 0;
    seg->flags.psh = 0;
    seg->flags.rst = 1;
    seg->flags.syn = 0;
    seg->flags.fin = 0;
    seg->wnd = 0;
    seg->up = 0;
    seg->mss = 0;
    seg->optlen = 0;
    if((hbp = htontcp(seg,NULLBUF,&ph)) == NULLBUF)
        return;
    /* Ship it out (note swap of addresses) */
    ip_send(ip->dest,ip->source,TCP_PTCL,ip->tos,0,hbp,ph.length,0,0);
    tcpOutRsts++;
}
  
/* Process an incoming acknowledgement and window indication.
 * From page 72.
 */
static void
update(tcb,seg,length)
register struct tcb *tcb;
register struct tcp *seg;
int16 length;
{
    int16 acked;
    int16 expand;
  
    acked = 0;
    if(seq_gt(seg->ack,tcb->snd.nxt)){
        tcb->flags.force = 1;   /* Acks something not yet sent */
        return;
    }
    /* Decide if we need to do a window update.
     * This is always checked whenever a legal ACK is received,
     * even if it doesn't actually acknowledge anything,
     * because it might be a spontaneous window reopening.
     */
    if(seq_gt(seg->seq,tcb->snd.wl1) || ((seg->seq == tcb->snd.wl1)
    && seq_ge(seg->ack,tcb->snd.wl2))){
        /* If the window had been closed, crank back the
         * send pointer so we'll immediately resume transmission.
         * Otherwise we'd have to wait until the next probe.
         */
        if(tcb->snd.wnd == 0 && seg->wnd != 0)
            tcb->snd.ptr = tcb->snd.una;
        tcb->snd.wnd = seg->wnd;
        tcb->snd.wl1 = seg->seq;
        tcb->snd.wl2 = seg->ack;
    }
    /* See if anything new is being acknowledged */
    if(!seq_gt(seg->ack,tcb->snd.una)){
        if(seg->ack != tcb->snd.una)
            return; /* Old ack, ignore */
  
        if(length != 0 || seg->flags.syn || seg->flags.fin)
            return; /* Nothing acked, but there is data */
  
        /* Van Jacobson "fast recovery" code */
        if(++tcb->dupacks == TCPDUPACKS){
            /* We've had a burst of do-nothing acks, so
             * we almost certainly lost a packet.
             * Resend it now to avoid a timeout. (This is
             * Van Jacobson's 'quick recovery' algorithm.)
             */
            int32 ptrsave;
  
            /* Knock the threshold down just as though
             * this were a timeout, since we've had
             * network congestion.
             */
            tcb->ssthresh = tcb->cwind/2;
            tcb->ssthresh = max(tcb->ssthresh,tcb->mss);
  
            /* Manipulate the machinery in tcp_output() to
             * retransmit just the missing packet
             */
            ptrsave = tcb->snd.ptr;
            tcb->snd.ptr = tcb->snd.una;
            tcb->cwind = tcb->mss;
            tcp_output(tcb);
            tcb->snd.ptr = ptrsave;
  
            /* "Inflate" the congestion window, pretending as
             * though the duplicate acks were normally acking
             * the packets beyond the one that was lost.
             */
            tcb->cwind = tcb->ssthresh + TCPDUPACKS*tcb->mss;
        } else if(tcb->dupacks > TCPDUPACKS){
            /* Continue to inflate the congestion window
             * until the acks finally get "unstuck".
             */
            tcb->cwind += tcb->mss;
        }
        return;
    }
    if(tcb->dupacks >= TCPDUPACKS && tcb->cwind > tcb->ssthresh){
        /* The acks have finally gotten "unstuck". So now we
         * can "deflate" the congestion window, i.e. take it
         * back down to where it would be after slow start
         * finishes.
         */
        tcb->cwind = tcb->ssthresh;
    }
    tcb->dupacks = 0;
  
    /* We're here, so the ACK must have actually acked something */
    acked = (int16) (seg->ack - tcb->snd.una);
  
    /* Expand congestion window if not already at limit and if
     * this packet wasn't retransmitted
     */
    if(tcb->cwind < tcb->snd.wnd && !tcb->flags.retran){
        if(tcb->cwind < tcb->ssthresh){
            /* Still doing slow start/CUTE, expand by amount acked */
            expand = min(acked,tcb->mss);
        } else {
            /* Steady-state test of extra path capacity */
		/* TODO - 14Aug2010, Maiko, This does not look right */
            expand = (int16) (((long)tcb->mss * tcb->mss) / tcb->cwind);
        }
        /* Guard against arithmetic overflow */
        if(tcb->cwind + expand < tcb->cwind)
            expand = MAXINT16 - tcb->cwind;
  
        /* Don't expand beyond the offered window */
        if(tcb->cwind + expand > tcb->snd.wnd)
            expand = tcb->snd.wnd - tcb->cwind;
  
        if(expand != 0)
            tcb->cwind += expand;
    }
    /* Round trip time estimation */
    if(tcb->flags.rtt_run && seq_ge(seg->ack,tcb->rttseq)){
        /* A timed sequence number has been acked */
        tcb->flags.rtt_run = 0;
        if(!(tcb->flags.retran)){
            int32 rtt;  /* measured round trip time */
            int32 abserr;   /* abs(rtt - srtt) */
  
            /* This packet was sent only once and now
             * it's been acked, so process the round trip time
             */
            rtt = (int32)(msclock() - tcb->rtt_time);
  
            abserr = (rtt > tcb->srtt) ? rtt - tcb->srtt : tcb->srtt - rtt;
            /* Run SRTT and MDEV integrators, with rounding */
            tcb->srtt = ((AGAIN-1)*tcb->srtt + rtt + (AGAIN/2)) >> LAGAIN;
            tcb->mdev = ((DGAIN-1)*tcb->mdev + abserr + (DGAIN/2)) >> LDGAIN;
  
            rtt_add(tcb->conn.remote.address,rtt);
            /* Reset the backoff level */
            tcb->backoff = 0;
        }
    }
    tcb->sndcnt -= acked;   /* Update virtual byte count on snd queue */
    tcb->snd.una = seg->ack;
  
    /* If we're waiting for an ack of our SYN, note it and adjust count */
    if(!(tcb->flags.synack)){
        tcb->flags.synack = 1;
        acked--;    /* One less byte to pull from real snd queue */
    }
    /* Remove acknowledged bytes from the send queue and update the
     * unacknowledged pointer. If a FIN is being acked,
     * pullup won't be able to remove it from the queue, but that
     * causes no harm.
     */
    pullup(&tcb->sndq,NULLCHAR,acked);
  
    /* Stop retransmission timer, but restart it if there is still
     * unacknowledged data. If there is no more unacked data,
     * the transmitter has gone at least momentarily idle, so
     * record the time for the VJ restart-slowstart rule.
     */
    stop_timer(&tcb->timer);
    if(tcb->snd.una != tcb->snd.nxt)
        start_timer(&tcb->timer);
    else
        tcb->lastactive = msclock();
  
    /* If retransmissions have been occurring, make sure the
     * send pointer doesn't repeat ancient history
     */
    if(seq_lt(tcb->snd.ptr,tcb->snd.una))
        tcb->snd.ptr = tcb->snd.una;
  
    /* Clear the retransmission flag since the oldest
     * unacknowledged segment (the only one that is ever retransmitted)
     * has now been acked.
     */
    tcb->flags.retran = 0;
  
    /* If outgoing data was acked, notify the user so he can send more
     * unless we've already sent a FIN.
     */
    if(acked != 0 && tcb->t_upcall
    && (tcb->state == TCP_ESTABLISHED || tcb->state == TCP_CLOSE_WAIT)){
        (*tcb->t_upcall)(tcb,tcb->window - tcb->sndcnt);
    }
}
  
/* Determine if the given sequence number is in our receiver window.
 * NB: must not be used when window is closed!
 */
static
int
in_window(tcb,seq)
struct tcb *tcb;
int32 seq;
{
    return seq_within(seq,tcb->rcv.nxt,(int32)(tcb->rcv.nxt+tcb->rcv.wnd-1));
}
  
/* Process an incoming SYN */
static void
proc_syn(tcb,tos,seg)
struct tcb *tcb;
char tos;
struct tcp *seg;
{
    int16 mtu;
    struct tcp_rtt *tp;
    struct iftcp *parms = tcb->parms;
  
    tcb->flags.force = 1;   /* Always send a response */
  
    /* Note: It's not specified in RFC 793, but SND.WL1 and
     * SND.WND are initialized here since it's possible for the
     * window update routine in update() to fail depending on the
     * IRS if they are left unitialized.
     */
    /* Check incoming precedence and increase if higher */
    if(PREC(tos) > PREC(tcb->tos))
        tcb->tos = tos;
    tcb->rcv.nxt = seg->seq + 1;    /* p 68 */
    tcb->snd.wl1 = tcb->irs = seg->seq;
    tcb->snd.wnd = seg->wnd;
    if(seg->mss != 0)
        tcb->mss = seg->mss;
    /* Check the MTU of the interface we'll use to reach this guy
     * and lower the MSS so that unnecessary fragmentation won't occur
     */
    if((mtu = ip_mtu(tcb->conn.remote.address)) != 0){
        /* Allow space for the TCP and IP headers */
        mtu -= TCPLEN + IPLEN;
        /* Find the minimum of the mss received, the mtu for the interface,
         * AND the mss set for the interface ! - WG7J
         */
        mtu = min(mtu,parms->mss);
        tcb->cwind = tcb->mss = min(mtu,tcb->mss);
    }
    /* Set the window size to the incoming interface value */
    tcb->window = tcb->rcv.wnd = parms->window;
  
    /* See if there's round-trip time experience */
    if((tp = rtt_get(tcb->conn.remote.address)) != NULLRTT){
        tcb->srtt = tp->srtt;
        tcb->mdev = tp->mdev;
    } else
        tcb->srtt = parms->irtt;
}
  
/* Generate an initial sequence number and put a SYN on the send queue */
void
send_syn(tcb)
struct tcb *tcb;
{
    /* tcb->iss = (int32)msclock() << 12; */
    /* 07Aug2010, Maiko (VE4KLM), perhaps this is better way to do it */
    tcb->iss = (int32)((msclock() % MAXINT) << 12);

    tcb->rttseq = tcb->snd.wl2 = tcb->snd.una = tcb->iss;
    tcb->snd.ptr = tcb->snd.nxt = tcb->rttseq;
    tcb->sndcnt++;
    tcb->flags.force = 1;
}
  
/* Add an entry to the resequencing queue in the proper place */
static void
add_reseq(tcb,tos,seg,bp,length)
struct tcb *tcb;
char tos;
struct tcp *seg;
struct mbuf *bp;
int16 length;
{
    register struct reseq *rp,*rp1;
  
    /* Allocate reassembly descriptor */
    /* 22Dec2005, Maiko, Replace malloc with mallocw ! */
    if((rp = (struct reseq *)mallocw(sizeof (struct reseq))) == NULLRESEQ){
        /* No space, toss on floor */
        free_p(bp);
        return;
    }
    ASSIGN(rp->seg,*seg);
    rp->tos = tos;
    rp->bp = bp;
    rp->length = length;
  
    /* Place on reassembly list sorting by starting seq number */
    rp1 = tcb->reseq;
    if(rp1 == NULLRESEQ || seq_lt(seg->seq,rp1->seg.seq)){
        /* Either the list is empty, or we're less than all other
         * entries; insert at beginning.
         */
        rp->next = rp1;
        tcb->reseq = rp;
    } else {
        /* Find the last entry less than us */
        for(;;){
            if(rp1->next == NULLRESEQ || seq_lt(seg->seq,rp1->next->seg.seq)){
                /* We belong just after this one */
                rp->next = rp1->next;
                rp1->next = rp;
                break;
            }
            rp1 = rp1->next;
        }
    }
}
  
/* Fetch the first entry off the resequencing queue */
static void
get_reseq(tcb,tos,seg,bp,length)
register struct tcb *tcb;
char *tos;
struct tcp *seg;
struct mbuf **bp;
int16 *length;
{
    register struct reseq *rp;
  
    if((rp = tcb->reseq) == NULLRESEQ)
        return;
  
    tcb->reseq = rp->next;
  
    *tos = rp->tos;
    ASSIGN(*seg,rp->seg);
    *bp = rp->bp;
    *length = rp->length;
    free((char *)rp);
}
  
/* Trim segment to fit window. Return 0 if OK, -1 if segment is
 * unacceptable.
 */
static int
trim(tcb,seg,bpp,length)
register struct tcb *tcb;
register struct tcp *seg;
struct mbuf **bpp;
int16 *length;
{
    int32 dupcnt,excess;
    int16 len;      /* Segment length including flags */
    char accept = 0;
  
    len = *length;
    if(seg->flags.syn)
        len++;
    if(seg->flags.fin)
        len++;
  
    /* Acceptability tests */
    if(tcb->rcv.wnd == 0){
        /* Only in-order, zero-length segments are acceptable when
         * our window is closed.
         */
        if(seg->seq == tcb->rcv.nxt && len == 0){
            return 0;   /* Acceptable, no trimming needed */
        }
    } else {
        /* Some part of the segment must be in the window */
        if(in_window(tcb,seg->seq)){
            accept++;   /* Beginning is */
        } else if(len != 0){
            if(in_window(tcb,(int32)(seg->seq+len-1)) || /* End is */
            seq_within(tcb->rcv.nxt,seg->seq,(int32)(seg->seq+len-1))){ /* Straddles */
                accept++;
            }
        }
    }
    if(!accept){
        tcb->rerecv += len; /* Assume all of it was a duplicate */
        free_p(*bpp);
        return -1;
    }
    if((dupcnt = tcb->rcv.nxt - seg->seq) > 0){
        tcb->rerecv += dupcnt;
        /* Trim off SYN if present */
        if(seg->flags.syn){
            /* SYN is before first data byte */
            seg->flags.syn = 0;
            seg->seq++;
            dupcnt--;
        }
        if(dupcnt > 0){
            pullup(bpp,NULLCHAR,(int16)dupcnt);
            seg->seq += dupcnt;
            *length -= (int16)dupcnt;
        }
    }
    if((excess = seg->seq + *length - (tcb->rcv.nxt + tcb->rcv.wnd)) > 0){
        tcb->rerecv += excess;
        /* Trim right edge */
        *length -= (int16) excess;
        trim_mbuf(bpp,*length);
        seg->flags.fin = 0; /* FIN follows last data byte */
    }
    return 0;
}
