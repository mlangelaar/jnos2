/*           RSPF - Radio Shortest Path First
 * This code may be used for non-commercial purposes only.
 *          Anders Klemets SM0RGV
 * 890918 - Version 2.0
 * 900816 - Version 2.1
 *
 * Mods by PA0GRI
 */
#include "global.h"
#ifdef RSPF
#include "mbuf.h"
#include "proc.h"
#include "timer.h"
#include "netuser.h"
#include "internet.h"
#include "pktdrvr.h"
#include "ip.h"
#include "iface.h"
#include "ax25.h"
#include "arp.h"
#include "icmp.h"
#include "socket.h"
#include "rspf.h"
  
extern struct route *rt_lookup __ARGS((int32 target));
  
#ifdef  AX25
extern struct lq *al_lookup __ARGS((struct iface *ifp,char *addr,int sort));
#else
static struct lq *al_lookup __ARGS((struct iface *ifp,char *addr,int sort));
#endif  /* AX25 */
  
struct rspf_stat Rspf_stat;
struct rspfreasm *Rspfreasmq = NULLRREASM;
struct rspfiface *Rspfifaces = NULLRIFACE;
struct rspfadj *Adjs = NULLADJ;
struct rspfrouter *Rspfrouters = NULLRROUTER;
struct mbuf *Rspfinq = NULLBUF;
struct timer Rspfreasmt, Susptimer;
char *Rrh_message = NULLCHAR;
unsigned short Rspfpingmax = 3;
static int Keeprouter = 0;
static int16 Envelopeid = 0;
static int Nrspfproc = 0;
static unsigned char Rspfsubseq = 0;
static short Rspfseq = -32768L + 1;
static char *findlowroute __ARGS((int32 addr,int *low,int add,int32 *resaddr,int *resbits));
static void makeroutes __ARGS((void));
static void partialupdate __ARGS((struct rspfrouter *rr,struct mbuf *bp));
static struct rspfrouter *rr_lookup __ARGS((int32 addr));
static void rr_delete __ARGS((int32 addr));
static struct rspfiface *rtif_lookup __ARGS((int32 addr));
static void rspfcsum __ARGS((struct mbuf **bpp,int32 dest));
static void update_in __ARGS((struct mbuf *bp,int32 addr));
static void node_in __ARGS((struct mbuf *bp,int32 addr,int full));
static void sendonerspf __ARGS((int32 addr,int32 dest));
static void sendtoall __ARGS((struct mbuf *bp,int nodecnt,struct rspfiface *riface));
static int sendupdate __ARGS((struct mbuf *bp,int nodecnt,int32 addr));
static int isnewer __ARGS((short a,short b));
static void del_reasm __ARGS((struct rspfreasm *re));
static void reasmtimeout __ARGS((void *t));
static struct mbuf *rspfreasm __ARGS((struct mbuf *bp,struct rspfpacketh *rph,int32 addr));
static struct mbuf *fragmenter __ARGS((struct mbuf *bp,int nodes,int16 mtu));
static struct mbuf *makeadjupdate __ARGS((int32 addr,struct rspfrouter *rr));
static void pushadj __ARGS((struct mbuf **bpp,int32 addr,int bits));
static void sendrrh __ARGS((struct rspfiface *ri));
static void sendrspf __ARGS((void));
static void goodbadnews __ARGS((struct rspfadj *adj));
static void add_rspfadj __ARGS((struct rspfadj *adj));
static void rspfcheck __ARGS((struct rspfadj *adj));
static void rspfping __ARGS((int oldstate,void *p,void *v));
  
/* IP passes its RSPF packets to this function */
void
rspf_input(iface,ip,bp,rxbroadcast)
struct iface *iface;
struct ip *ip;
struct mbuf *bp;
int rxbroadcast;
{
    struct pseudo_header ph;    /* Pseudo-header for checksumming */
  
    if(bp == NULLBUF)
        return;
    if(Rspfifaces == NULLRIFACE){   /* Rspf main loop is not running */
        free_p(bp);
        return;
    }
    ph.length = ip->length - IPLEN - ip->optlen;
    ph.source = ip->source;
    ph.dest = ip->dest;
    ph.protocol = ip->protocol;
    if(cksum(&ph,bp,ph.length) != 0){
        /* Checksum failed, ignore packet completely */
        free_p(bp);
        ++Rspf_stat.badcsum;
        return;
    }
    bp = pushdown(bp,1 + sizeof(ip->source) + sizeof(iface));
    *bp->data = RSPFE_PACKET;
    memcpy(bp->data + 1,&ip->source,sizeof(ip->source));
    memcpy(bp->data + 1 + sizeof(ip->source),&iface,sizeof(iface));
    enqueue(&Rspfinq,bp);
}
  
/* Main loop in RSPF process */
void
rspfmain(v,v1,v2)
int v;
void *v1,*v2;
{
    union rspf rspf;        /* Internal packet structure */
    struct rspfadj *adj = NULLADJ;  /* Adjacency */
    struct mbuf *bp, *tbp;
    struct rspfiface *riface;
    struct iface *iface;
    struct route *rp;
    int32 source;           /* Source address */
    int cmd;
  
	/*
	 * 05Dec2020, Maiko (VE4KLM), just to get rid of compiler warnings,
	 * but I am having great trouble understanding why 'source' is being
	 * passed to these functions expecting some type of RSP structure ?
	 *
	 * These particular warnings reported by Charles (N2NOV), so I am
	 * guessing he uses this feature, hopefully it still works now :]
	 */
	long source_l;

    for(;;) {
        if(Rspfinq == NULLBUF)
            pwait(&Rspfinq);
        bp = dequeue(&Rspfinq);    /* at least 5 bytes */
        cmd = PULLCHAR(&bp);   /* get internal event indicator */
        pullup(&bp,(char *)&source,sizeof(source));

		source_l = (long)source;	/* 05Dec2020, Maiko */

        switch(cmd) {
            case RSPFE_RRH:
                sendrrh((struct rspfiface *)source_l);
                break;
            case RSPFE_CHECK:
                rspfcheck((struct rspfadj *)source_l);
                break;
            case RSPFE_UPDATE:
                sendrspf();
                break;
            case RSPFE_ARP:
                adj = (struct rspfadj *) source_l;
                source = adj->addr;       /* fall through */
            case RSPFE_PACKET:
                pullup(&bp,(char *)&iface,sizeof(iface));
                break;
        }
        if(cmd != RSPFE_PACKET && cmd != RSPFE_ARP)
            continue;
        if(cmd == RSPFE_PACKET && ntohrspf(&rspf,&bp) == -1){
            free_p(bp);
            continue;
        }
        if(iface != NULLIF) {
            for(riface = Rspfifaces; riface != NULLRIFACE; riface =
                riface->next)
                if(riface->iface == iface)
                    break;
        } else
          /* fails if there is no route or no RSPF interface */
            riface = rtif_lookup(source);
  
        if(riface == NULLRIFACE) {
            free_p(bp);
            if(cmd == RSPFE_PACKET)
                ++Rspf_stat.norspfiface;
            continue; /* We do not use RSPF on this interface */
        }
         /* If we dont have an entry in the routing table for this station,
          * or if the entry is less than 32 bits specific or has a higher
          * cost than our new route, and is pointing to the wrong interface,
          * then add a correct, private, route.
          */
        rp = rt_lookup(source);
        if(rp == NULLROUTE || (rp != NULLROUTE && rp->iface !=
            riface->iface && (rp->bits < 32 || rp->metric >
        riface->quality))){
            rp = rt_add(source,32,0,iface,riface->quality,0,1);
            set_timer(&rp->timer,MSPTICK);  /* IT9LTA: dummy value, indicates non-manual route */
        }
  
        if(cmd == RSPFE_ARP) {
            if(adj != NULLADJ){
                adj->cost = riface->quality;  /* default cost */
                add_rspfadj(adj);
                continue;
            }
        }
        switch(rspf.hdr.type){     /* packet types */
            case RSPF_RRH:
                ++Rspf_stat.rrhin;
                free_p(bp);
                adj = (struct rspfadj *)callocw(1,sizeof(struct rspfadj));
                adj->addr = rspf.rrh.addr;
                adj->seq = rspf.rrh.seq;
                adj->cost = riface->quality;  /* Default cost */
                adj->state = RSPF_TENTATIVE;
                if(rspf.rrh.flags & RSPFMODE)
                    adj->tos = DELAY;
                else
                    adj->tos = RELIABILITY;
                add_rspfadj(adj);
                break;
            case RSPF_FULLPKT:
                ++Rspf_stat.updatein;
          /* Feed the packet through the reassembly queue */
                if((tbp = rspfreasm(bp,&rspf.pkthdr,source)) != NULLBUF)
                    update_in(tbp,source);
                break;
        }
    }
}
  
/* This function is called every time an RRH should be broadcasted.
 * An RRH is sent on the interface given by ri or on every RSPF interface.
 * The broadcast address of the interface must be routed onto the same
 * interface (using a static route for instance) otherwise there will be no
 * RRH sent on that interface.
 */
static void
sendrrh(ri)
struct rspfiface *ri;       /* interface to use, if specified */
{
    struct pseudo_header ph;
    struct mbuf *bp, *data = NULLBUF;
    struct rspfiface *riface;
    struct route *rp;
    struct rrh rrh;
  
    for(riface = Rspfifaces; riface != NULLRIFACE; riface = riface->next) {
        if(ri != NULLRIFACE && riface != ri)
            continue;
        if((rp = rt_lookup(riface->iface->broadcast)) != NULLROUTE &&
        rp->iface == riface->iface){
            rrh.version = RSPF_VERSION;
            rrh.addr = riface->iface->addr;
            ph.length = 0;
            if(Rrh_message != NULLCHAR) {
                data = ambufw(strlen(Rrh_message));
                memcpy(data->data,Rrh_message,strlen(Rrh_message));
                ph.length = data->cnt = strlen(Rrh_message);
            }
            ph.length += RRHLEN;
            ph.source = riface->iface->addr;
            ph.dest = riface->iface->broadcast;
            ph.protocol = RSPF_PTCL;
          /* Start the RRH link level packet counter at 1, since adj->seq
           * is 0 only for ARP generated adjacencies. This is also correct
           * since rawsndcnt will increase by one when the RRH is sent.
           */
            rrh.seq = riface->iface->rawsndcnt + 1;
          /* Default is to use the same mode as the interface */
            if(Rspfownmode == -1)
                rrh.flags = !(riface->iface->flags & CONNECT_MODE);
            else
                rrh.flags = !(Rspfownmode & CONNECT_MODE);
  
            bp = htonrrh(&rrh,data,&ph);
            ++Rspf_stat.rrhout;
            ip_send(riface->iface->addr,riface->iface->broadcast,RSPF_PTCL,
            0,2,bp,0,0,0); /* the ttl will be decremented to 1 */
        }
    }
}
  
/* This function is called whenever an RRH packet has been received or when
 * a new entry is added to the ARP table.
 */
static void
add_rspfadj(adj)
struct rspfadj *adj;
{
    struct rspfadj *oldadj, *tmp;
    struct rspfiface *riface;
    struct arp_tab *atp;
    struct lq *alp;
    int dif, origdif;
  
    if(adj == NULLADJ)
        return;         /* sanity but it happens... */
    if (ismyaddr(adj->addr)) /*is it my RRH (ie, heard my own bcst)? N8RAW */
        return;
    /* Find the previous copy of the adjacency, if there was any */
    /* Insert the new adjacency */
    adj->next = Adjs;
    Adjs = adj;
    for(oldadj = Adjs; oldadj->next != NULLADJ; oldadj = oldadj->next)
        if(oldadj->next->addr == adj->addr) {
            tmp = oldadj->next;
            oldadj->next = oldadj->next->next;
            oldadj = tmp;
            break;
        }
  
    if(oldadj->addr != adj->addr || oldadj == adj)
        oldadj = NULLADJ;
  
    /* ARP entries give no sequence number, so save the old one */
    if(oldadj != NULLADJ) {
        if(adj->seq == 0)
            adj->seq = oldadj->seq;
        if(adj->tos == 0)
            adj->tos = oldadj->tos;   /* they give no TOS either */
    }
  
    switch(adj->state) {
        case RSPF_TENTATIVE:
            if(oldadj != NULLADJ) {
         /* If the adjacency was in OK state, it will remain there.
          * An RRH or ARP upcall can never generate an OK -> Tentative
          * transition.
          */
                if(oldadj->state == RSPF_OK)
                    adj->state = RSPF_OK;
                adj->okcnt = oldadj->okcnt;
         /* If old state was Bad, don't announce this adjacency until
          * it is known to be OK, to prevent announcing an adjacency
          * with an state transition loop such as OK -> Suspect -> Bad ->
          * Tentative -> Suspect -> Bad -> Tentative -> ...
          */
                if(oldadj->state == RSPF_BAD) {
                    adj->okcnt = 0;
                    stop_timer(&oldadj->timer);
          /* Enter Suspect state at once, there is no point in
           * dwelling as Tentative.
           */
                    rspfcheck(adj);
                } else {
          /* Inherit the old timer */
                    adj->timer.func = rspfevent;
                    adj->timer.arg = (void *) &adj->timer;
          /* continue where the old timer is stopped */
                    adj->timer.duration = oldadj->timer.duration;
                    stop_timer(&oldadj->timer);
          /* Set the timer to Suspectimer in the unlikely event that
           * the timer was stopped and oldadj is not Suspect or Bad.
           * Suspect state is dealt with further below.
           */
                    if(dur_timer(&adj->timer) == 0)
                        set_timer(&adj->timer,dur_timer(&Susptimer));
                    start_timer(&adj->timer);
                    set_timer(&adj->timer,dur_timer(&oldadj->timer));
                }
        /* We must now try to figure out from which AX.25 callsign this
         * packet came. Looking at the ARP table may sometimes help.
         */
                if(oldadj->seq != 0 && adj->seq != 0 &&
                    (riface = rtif_lookup(adj->addr)) != NULLRIFACE &&
                    (atp = arp_lookup(ARP_AX25,adj->addr,riface->iface)) != NULLARP &&
                    atp->state == ARP_VALID &&
                (alp = al_lookup(riface->iface,atp->hw_addr,0)) != NULLLQ){
        /* The wrapping of the modulus is taken into account here */
                    if(oldadj->seq > (MAXINT16 - 100) && adj->seq < 100)
                        origdif = adj->seq + MAXINT16 - oldadj->seq;
                    else
                        origdif = adj->seq - oldadj->seq;
                    if((alp->currxcnt - adj->heard) > (MAXINT16 - 100)
                        && adj->seq < 100)
                        dif = (int)(origdif + MAXINT16 - (alp->currxcnt - adj->heard));
                    else
                        dif = (int)(origdif - (alp->currxcnt - adj->heard));
                    adj->heard = alp->currxcnt; /* Update the counter */
        /* At this point, "origdif" equals the difference in sequence
         * numbers between the two latest RRH packets, i.e. the
         * number of AX.25 frames the station has sent during since
         * the last RRH packet we heard. "dif" equals the number of
         * AX.25 frames that we actually heard from that station
         * during the same period.
         */
                    if(origdif > 0 && dif > 0)
            /* This algorithm can be improved, see 2.1 spec */
                        adj->cost = riface->quality*2-riface->quality*dif/origdif;
                }
        /* Ignore any new RRH's if a pinger process is already running */
                if(oldadj->state == RSPF_SUSPECT) {
                    if(adj->heard != 0)        /* save new heard count */
                        oldadj->heard = adj->heard;
                    oldadj->next = adj->next;  /* adj is at start of chain */
                    Adjs = oldadj;
                    stop_timer(&adj->timer);
                    free((char *)adj);
                    return;
                }
            } else {                    /* oldadj == NULLADJ */
                rspfcheck(adj);         /* enter Suspect state */
        /* A new adjacency may affect the routing table even though no
         * routing updates have yet been received from it.
         */
                makeroutes();
            }
            break;
        case RSPF_OK:
            if(oldadj != NULLADJ) {
                adj->okcnt += oldadj->okcnt;    /* Do these before possible */
                stop_timer(&oldadj->timer);     /* killproc() -- KZ1F */
                if(oldadj->state == RSPF_SUSPECT){
                    killproc(oldadj->pinger);
                    --Nrspfproc;            /* Bug fix - N1BEE */
                }
            }
            else
                makeroutes();          /* routing table may change */
            if(adj->okcnt == 1)         /* A previously unkown route */
                goodbadnews(adj);           /* ..that is good news */
            break;
    }
    if (oldadj != NULLADJ) {
        stop_timer(&oldadj->timer);     /* stop timer before free() -- KZ1F */
#ifndef KZ1F
        free((char *)oldadj);
#endif
    }
}
  
/* Take appropriate action if an adjacency is Bad or Tentative */
static void
rspfcheck(adj)
struct rspfadj *adj;
{
    struct rspfadj *prev;
  
    for(prev = Adjs; prev != NULLADJ; prev = prev->next)
        if(prev->next == adj)
            break;
        switch(adj->state){
            case RSPF_OK:
                adj->state = RSPF_TENTATIVE;   /* note fall-thru */
            case RSPF_TENTATIVE:
                if (Nrspfproc < RSPF_PROCMAX) {
                    Nrspfproc++;
                    adj->pinger = newproc("RSPF ping",1024,rspfping,
                    (int)adj->state,adj,NULL,0);
                    adj->state = RSPF_SUSPECT; /* The adjacency is now Suspect */
                } else {       /* Try later */
                    set_timer(&adj->timer,dur_timer(&Susptimer));
                    start_timer(&adj->timer);
                }
                break;
            case RSPF_BAD:
                rr_delete(adj->addr);
                adj->cost = 255;
                if(adj->okcnt != 0)
                    goodbadnews(adj);     /* This is bad news... */
                if(prev != NULLADJ)
                    prev->next = adj->next;   /* Unlink */
                else
                    Adjs = adj->next;
                stop_timer(&adj->timer);   /* just in case */
                free((char *)adj);     /* delete the adjacency */
                makeroutes();          /* update the routing table */
                break;
        }
}
  
/* Send a ping to a suspect or tentative adjacency */
static void
rspfping(oldstate, p, v)
int oldstate;
void *p, *v;
{
    int s, fromlen, pcnt;
    struct icmp icmp;
    struct rspfadj *adj;
    struct sockaddr_in from;
    struct mbuf *bp;
 
    j2pause ((int32)(((ptol(p) & 7)+1)*1000));	/* Pause for 1 to 8 seconds */

    adj = (struct rspfadj *) p;
    adj->timer.func = rspfevent;        /* Fill in timer values */
    adj->timer.arg = (void *) &adj->timer;
    set_timer(&adj->timer,dur_timer(&Susptimer));
    if((s = j2socket(AF_INET,SOCK_RAW,ICMP_PTCL)) == -1){
        --Nrspfproc;
        adj->state = oldstate;
        start_timer(&adj->timer);
        return;
    }
  
    fromlen = sizeof(from);
    for(pcnt=0; pcnt < Rspfpingmax; ++pcnt) {
        pingem(s,adj->addr,0,(int16)s,0);
    /* Now collect the reply */
        j2alarm(60*1000);/* Let each ping timeout after 60 seconds */
        for(;;) {
            if(recv_mbuf(s,&bp,0,(char *)&from,&fromlen) == -1){
                if(errno == EALARM){ /* We timed out */
                    break;
                }
                j2alarm(0);
                adj->state = oldstate;
                close_s(s);
                --Nrspfproc;
                start_timer(&adj->timer);
                return;
            }
            ntohicmp(&icmp,&bp);
            free_p(bp);
            if(icmp.type != ICMP_ECHO_REPLY || from.sin_addr.s_addr !=
                adj->addr || icmp.args.echo.id != s)
          /* Ignore other people's responses */
                continue;
            j2alarm(0);
            if(++adj->okcnt == 1)
                goodbadnews(adj);     /* Good news */
            close_s(s);
            --Nrspfproc;
            start_timer(&adj->timer);
            adj->state = RSPF_OK;      /* Finally change state */
            return;
        }
    }
    /* we give up, mark the adjacency as bad */
    adj->state = RSPF_BAD;
    close_s(s);
    --Nrspfproc;
    start_timer(&adj->timer);
}
  
/* ARP upcalls come in two flavors: When an ARP Reply has been received, we
 * know for certain that bidirectional communication is possible with the
 * particular station. But when an ARP Request is received, or if an ARP
 * entry is added manually, it has yet to be determined if the link is
 * bidirectional so iface is NULLIF in those cases.
 */
void
rspfarpupcall(addr,hardware,iface)
int32 addr;         /* Address being added to ARP table */
int16 hardware;         /* Hardware type */
struct iface *iface;        /* Interface used, if known */
{
    struct rspfadj *adj;
    struct mbuf *bp;
    struct rspfiface *riface;
  
    if((riface = rtif_lookup(addr)) == NULLRIFACE)
        return;        /* Not a route to an RSPF interface */
    /* Proceed only if the ARP entry is for a hardware type that is relevant
     * for the interface onto which IP datagrams are routed.
     */
    switch(hardware) {
        case ARP_NETROM:
            if(riface->iface->type != CL_NETROM)
                return;
            break;
        case ARP_AX25:
            if(riface->iface->type != CL_AX25)
                return;
            break;
        case ARP_ETHER:
            if(riface->iface->type != CL_ETHERNET)
                return;
            break;
        case NHWTYPES:
            break;     /* "Any interface type is ok" type entry */
        default:
            return;
    }
    if((adj = (struct rspfadj *)calloc(1,sizeof(struct rspfadj)))==NULLADJ)
        return;
    adj->addr = addr;
    if(iface == NULLIF)   /* A manual ARP entry or Request, may be no-good */
        adj->state = RSPF_TENTATIVE;
    else {
        adj->state = RSPF_OK;
        adj->okcnt++;
        adj->timer.func = rspfevent;        /* Fill in timer values */
        adj->timer.arg = (void *) &adj->timer;
        set_timer(&adj->timer,dur_timer(&Susptimer));
        start_timer(&adj->timer);
    }
    if((bp = alloc_mbuf(1+sizeof(int32)+sizeof(iface))) == NULLBUF) {
        stop_timer(&adj->timer);
        free((char *)adj);
        return;
    }
    *bp->data = RSPFE_ARP;
    memcpy(bp->data + 1, (char *) &adj, sizeof(adj));
    memcpy(bp->data + 1 + sizeof(adj), (char *) &iface, sizeof(iface));
    bp->cnt = bp->size;
    enqueue(&Rspfinq,bp);
}
  
/* Called by "route add" command. */
void
rspfrouteupcall(addr,bits,gateway)
int32 addr;         /* Address added to routing table */
unsigned bits;          /* Significant bits in address */
int32 gateway;          /* Address of gateway station */
{
    /* We are only interested in 32 bit specific routes that use a
     * gateway. Direct routes will be discovered anyway.
     */
    if(bits != 32 || gateway == 0 || gateway == addr)
        return;
    if(rtif_lookup(addr) == NULLRIFACE) /* not routed onto RSPF interface */
        return;
    rspfarpupcall(addr,NHWTYPES,NULLIF); /* proceed as an "arp add" upcall */
}
  
/* When the suspect timer expires, we scan through the routing table for all
 * manual 32 bit specific routes through a gateway that are not an adjacency,
 * and calls rspfrouteupcall(). A similar thing is done for all manual ARP
 * entries. This will make sure that a station that was not reachable when
 * the "route add" or "arp add" command was executed will eventually be
 * discovered if it joins the network.
 */
void
rspfsuspect(t)
void *t;
{
    struct rspfadj *adj;
    struct route *rp;
    struct arp_tab *ap;
    int i;
    start_timer(&Susptimer);           /* restart the timer */
    for(i = 0; i < HASHMOD; i++)       /* Check the routing table */
        for(rp = Routes[31][i]; rp != NULLROUTE; rp = rp->next) {
            if((rp->flags & RTPRIVATE) || dur_timer(&rp->timer) != 0)
                continue;   /* not this route */
            for(adj = Adjs; adj != NULLADJ; adj = adj->next)
                if(adj->addr == rp->target)
                    break;
            if(adj == NULLADJ) /* it is not already an adjacency */
                rspfrouteupcall(rp->target,32,rp->gateway);
        }
    for(i=0; i < HASHMOD; ++i) /* scan the ARP table */
        for(ap = Arp_tab[i]; ap != NULLARP; ap = ap->next) {
            if(dur_timer(&ap->timer))    /* not a manual entry */
                continue;
            for(adj = Adjs; adj != NULLADJ; adj = adj->next)
                if(adj->addr == ap->ip_addr)
                    break;
            if(adj == NULLADJ)   /* not already an adjacency */
                rspfarpupcall(ap->ip_addr,ap->hardware,NULLIF);
        }
}
  
/* This non-layered function peeks at the routing table to figure out to which
 * interface datagrams for addr will be routed. Then it returns the matching
 * rspfiface structure.
 */
static
struct rspfiface *
rtif_lookup(addr)
int32 addr;
{
    struct rspfiface *riface;
    struct route *rp;
    if((rp = rt_lookup(addr)) == NULLROUTE)
        return NULLRIFACE;
    riface = Rspfifaces;
    while(riface != NULLRIFACE){
        if(riface->iface == rp->iface)
            return riface;
        riface = riface->next;
    }
    return NULLRIFACE;
}
  
/* Send good or bad news partial routing updates. A cost of 255 should be
 * interpreted as bad news by the remote station.
 */
static void
goodbadnews(adj)
struct rspfadj *adj;
{
    struct rspfnodeh nodeh;
    struct rspflinkh linkh;
    struct rspfiface *riface;
    struct rspfrouter *rr;
    struct mbuf *bp, *tbp, *wbp;
    int nodecnt = 1;
    if((riface = rtif_lookup(adj->addr)) == NULLRIFACE)
        return;
    bp = ambufw(5);
    bp->cnt = bp->size;
    *bp->data = 32;     /* the number of significant bits in the address */
    put32(bp->data+1,adj->addr);
    linkh.horizon = riface->horizon;
    linkh.erp = riface->erp;
    linkh.cost = adj->cost;
    linkh.adjn = 1;
    tbp = htonrspflink(&linkh,bp);
    nodeh.seq = Rspfseq;
    nodeh.subseq = ++Rspfsubseq;
    nodeh.links = 1;
    for(riface = Rspfifaces; riface != NULLRIFACE; riface = riface->next) {
        if(dup_p(&wbp,tbp,0,len_p(tbp)) != len_p(tbp)) {
            free_p(wbp);
            continue;
        }
        nodeh.addr = riface->iface->addr;
        bp = htonrspfnode(&nodeh,wbp);
        sendtoall(bp,1,riface);
    }
    free_p(tbp);
    /* If this is a new adjacency, then send it a routing update immediately */
    if(adj->cost == 255 || adj->okcnt != 1)
        return;
    /* When two RSPF stations learn about each others existence, one of
     * them will usually have received an RRH from the other. The one that
     * received the RRH will send the peer a routing update immediately.
     * So in the code below, if no RRH has been received (seq is 0) and no
     * routing update has yet been received, we should expect to receive a
     * routing update shortly if the adjacency is running RSPF.
     * This could fail though if two RSPF stations learn about each other
     * solely through ARP upcalls and no RRH's or Updates were exchanged
     * prior to or during the establishment of the adjacency.
     */
    if(adj->seq == 0 && rr_lookup(adj->addr) == NULLRROUTER) {
        if(adj->state != RSPF_SUSPECT)  /* running in RSPF process, give up */
            return;
        j2alarm(15*1000);   /* wait for an Update */
        if(pwait(adj) == EALARM)
            return;    /* still no sign of RSPF activity from the adjacency */
        j2alarm(0);
    }
    ++adj->okcnt;   /* we don't want to get here again */
    if((bp = makeownupdate(adj->addr,1)) == NULLBUF)
        return;
    for(rr = Rspfrouters; rr != NULLRROUTER; rr = rr->next)
        if((tbp = makeadjupdate(get32(rr->data->data),rr)) != NULLBUF){
            append(&bp,tbp);
            nodecnt++;
        }
    sendupdate(bp,nodecnt,adj->addr);
}
  
/* This function is called whenever it is time to send a new RSPF update */
static void
sendrspf()
{
    struct rspfrouter *rr;
    struct mbuf *bp, *wbp, *tmp = NULLBUF;
    struct rspfiface *riface;
    struct rspfadj *adj;
    int nodecnt, incr = 1;
  
    for(nodecnt = 1, rr = Rspfrouters; rr != NULLRROUTER; rr = rr->next)
        if(!rr->sent)      /* don't send stale data */
            if((bp = makeadjupdate(get32(rr->data->data),rr)) != NULLBUF){
                append(&tmp,bp);
                nodecnt++;
            }
    for(riface = Rspfifaces; riface != NULLRIFACE; riface = riface->next) {
     /* Find an address that is routed onto this interface */
        for(adj = Adjs; adj != NULLADJ; adj = adj->next)
            if(rtif_lookup(adj->addr) == riface)
                break;
        if(adj == NULLADJ) /* no adjacency on that interface? */
            continue;
        if(dup_p(&wbp,tmp,0,len_p(tmp)) != len_p(tmp)) {
            free_p(wbp);
            continue;
        }
        if((bp = makeownupdate(adj->addr,incr)) != NULLBUF) {
            incr = 0; /* sequence number is incremented only once */
            append(&bp,wbp);
        }
        else
            break;
        sendtoall(bp,nodecnt,riface);
    }
    free_p(tmp);
    for(rr = Rspfrouters; rr != NULLRROUTER; rr = rr->next)
        if(!rr->sent)      /* Mark router data as propagated */
            ++rr->sent;
}
  
/* This function is used to answer "poll" messages */
static void
sendonerspf(addr,dest)
int32 addr; /* address of station whose routing update we will make */
int32 dest; /* address of station to send the update to */
{
    struct mbuf *bp;
    if(ismyaddr(addr)){
        if((bp = makeownupdate(dest,0)) == NULLBUF)
            return;
        sendupdate(bp,1,dest);
        return;
    }
    if((bp = makeadjupdate(addr,NULLRROUTER)) == NULLBUF)
        return;
    sendupdate(bp,1,dest);
}
  
/* send an update to all adjacencies on an RSPF interface */
static void
sendtoall(bp,nodecnt,riface)
struct mbuf *bp;
int nodecnt;            /* number of reporting nodes in update */
struct rspfiface *riface;   /* interface to sent to */
{
    struct rspfadj *adj;
    struct mbuf *wbp;
    int broad;
    for(broad = 0, adj = Adjs; adj != NULLADJ; adj = adj->next)
      /* If adj->seq is 0 we have never received an RRH from the
       * adjacency, and if there is no stored routing update, then
       * it is not known if the adjacency understands RSPF.
       */
        if((adj->seq != 0 || rr_lookup(adj->addr) != NULLRROUTER) &&
        riface == rtif_lookup(adj->addr)) {
            if((adj->tos & RELIABILITY) && !(adj->tos & DELAY)) {
                if(dup_p(&wbp,bp,0,len_p(bp)) != len_p(bp)) {
                    free_p(wbp);
                    continue;
                }
                sendupdate(wbp,nodecnt,adj->addr); /* private copy */
            }
            else
                ++broad;    /* send to broadcast IP address instead */
        }
    if(broad != 0)
	{
        if(dup_p(&wbp,bp,0,len_p(bp)) != len_p(bp))
            free_p(wbp);
        else
            sendupdate(wbp,nodecnt,riface->iface->broadcast);
	}
    free_p(bp);
}
  
/* This function sends a routing update to the adjacencies that we have
 * recevied RRH's from.
 */
static int
sendupdate(bp,nodecnt,addr)
struct mbuf *bp;    /* Full packet, except for the packet header */
int nodecnt;        /* Number of reporting nodes in the packet */
int32 addr;     /* Station to send update to */
{
    struct rspfadj *adj;
    struct mbuf *tmp;
    int tos = 0;
  
    /* See if we are sending to a known adjacency, in use its TOS in
     * that case.
     */
    for(adj = Adjs; adj != NULLADJ; adj = adj->next)
        if(adj->addr == addr) {
            tos = adj->tos;
            break;
        }
    if((tmp = fragmenter(bp,nodecnt,ip_mtu(addr) - IPLEN)) == NULLBUF)
        return -1;
    while((bp = dequeue(&tmp)) != NULLBUF){
        rspfcsum(&bp,addr);    /* Calculate the checksum */
        ++Rspf_stat.updateout;
        ip_send(INADDR_ANY,addr,RSPF_PTCL,INET_CTL | tos,2,bp,0,0,0);
    }
    return 0;
}
  
/* Fragment a large mbuf if necessary into packets of maximum mtu bytes.
 * Each packet is prepended a RSPF routing update header, without the checksum.
 */
static struct mbuf *
fragmenter(bp,nodes,mtu)
struct mbuf *bp; /* packet to send, without packet header */
int nodes;  /* The number of reporting nodes in the routing update */
int16 mtu;  /* The maximum transmission unit */
{
    struct rspfnodeh nodeh;
    struct rspflinkh linkh;
    struct rspfpacketh pkth;
    struct mbuf *tbp, *tmp, *bpq = NULLBUF;
    int n, nodecnt, linkcnt, adjcnt, nodemax, sync;
    char *cp, fragn = 1;
  
    if(mtu < RSPFPKTLEN + RSPFNODELEN || bp == NULLBUF) {
        free_p(bp);
        return NULLBUF;
    }
    ++Envelopeid;
    nodemax = nodes;        /* total number of nodes in envelope */
    nodecnt = nodes;        /* nodes left to packetize */
    linkcnt = adjcnt = 0;
    while(len_p(bp) != 0){
        n = min(mtu,len_p(bp)+RSPFPKTLEN);  /* length of this packet */
        n -= RSPFPKTLEN;
        tbp = NULLBUF;
        sync = 0;
        if(adjcnt){
            tbp = ambufw(min(adjcnt*5,n/5*5));
            tbp->cnt = tbp->size;
            cp = tbp->data;
        }
        while(n > 0){
            while(adjcnt){
                if((n -= 5) < 0)
                    break;
                pullup(&bp,cp,5);
                cp += 5;
                adjcnt--;
            }
            if(linkcnt && n > 0){
                if((n -= RSPFLINKLEN) < 0)
                    break;
                ntohrspflink(&linkh,&bp);
                adjcnt = linkh.adjn;
                tmp = htonrspflink(&linkh,NULLBUF);
                append(&tbp,tmp);
                tmp = ambufw(min(5*adjcnt,n/5*5));
                tmp->cnt = tmp->size;
                cp = tmp->data;
                append(&tbp,tmp);
                linkcnt--;
                continue;
            }
            if(nodecnt && linkcnt == 0 && n > 0){
                if((n -= RSPFNODELEN) < 0)
                    break;
                if(nodecnt == nodes)        /* Set the sync octet */
                    sync = len_p(tbp) + 4;
                ntohrspfnode(&nodeh,&bp);
                linkcnt = nodeh.links;
                tmp = htonrspfnode(&nodeh,NULLBUF);
                append(&tbp,tmp);
                nodecnt--;
            }
        }
        pkth.version = RSPF_VERSION;    /* The version number */
        pkth.type = RSPF_FULLPKT;   /* The packet type */
        pkth.fragn = fragn++;       /* The fragment number */
        pkth.fragtot = 0;       /* The total number of fragments */
        pkth.csum = 0;          /* The checksum */
        pkth.sync = sync < 256 ? sync : 0;  /* The sync octet */
        pkth.nodes = nodemax;       /* The number of nodes in envelope */
        pkth.envid = Envelopeid;    /* The envelope-ID */
        tmp = htonrspf(&pkth,tbp);  /* add packet header */
        enqueue(&bpq,tmp);      /* add packet to chain */
        nodes = nodecnt;        /* number of nodes left */
    }
    free_p(bp);
    for(tbp = bpq; tbp != NULLBUF; tbp = tbp->anext)
        *(tbp->data + 3) = len_q(bpq);  /* Set the fragment total counter */
    return bpq;
}
  
/* Calculate the checksum on an RSPF routing update packet */
static void
rspfcsum(bpp,dest)
struct mbuf **bpp;
int32 dest;
{
    struct pseudo_header ph;
    int16 checksum;
    ph.length = len_p(*bpp);
    ph.source = locaddr(dest);
    ph.dest = dest;
    ph.protocol = RSPF_PTCL;
    if((checksum = cksum(&ph,*bpp,ph.length)) == 0)
        checksum = 0xffffL;
        /* checksum = 0xffffffffL; is wrong, 05Dec2020 , Maiko */
    /* This assumes that the checksum really is at this position */
    put16((*bpp)->data+4,checksum);
}
  
/* This function creates our own routing update and returns it in an mbuf */
struct mbuf *
makeownupdate(dest,new)
int32 dest;     /* Address of a station that will get this update */
int new;        /* True if the sequence number should be incremented */
{
    struct rspfadj *adj;
    struct rspflinkh linkh;
    struct rspfnodeh nodeh;
    struct rspfiface *riface, rifdefault;
    struct mbuf *bp = NULLBUF, *tbp, *tmp;
    struct route *rp;
    int i, adjcnt, lnkcnt, bits;
    int32 prev, low, cur;
  
    /* Set default values to apply to non-RSPF interfaces */
    rifdefault.horizon = 32;
    rifdefault.erp = 0;
    rifdefault.quality = 0;
    /* Our adjacencies has to be sorted into groups of the same horizon,
     * ERP factor and cost. This is done by combining these numbers into a
     * single one, modulo 256 and take the lowest number first.
     */
    low = 0;
    lnkcnt = 0;
    for(;;){
        prev = low;
        low = 255*65536L+255*256+255;
        for(adj = Adjs; adj != NULLADJ; adj = adj->next)
         /* Include all adjacecies that have been or are in OK state
          * in the update. Bad adjacencies are also included to give
          * them a chance to recover. Hopelessly Bad adjacencies are
          * eventually deleted elsewhere.
          */
            if(adj->okcnt != 0 && (riface = rtif_lookup(adj->addr)) !=
            NULLRIFACE) {
                cur = riface->horizon*65536L+riface->erp*256+adj->cost;
                if(cur > prev && cur < low)
                    low = cur;
            }
    /* Add any manual public, 1-31 bit specific routes.
     * Use the route metric only if it is greater than the default
     * quality to lessen a possible "wormhole" effect.
     */
        for(bits = 1; bits <= 31; bits++)
            for(i = 0; i < HASHMOD; i++)
                for(rp = Routes[bits-1][i]; rp != NULLROUTE; rp = rp->next)
                    if(!(rp->flags & RTPRIVATE) && !dur_timer(&rp->timer)) {
                        if((riface = rtif_lookup(rp->target)) == NULLRIFACE)
                            riface = &rifdefault;
                        cur = riface->horizon*65536L+riface->erp*256+
                        (rp->metric > riface->quality ? rp->metric :
                        riface->quality);
                        if(cur > prev && cur < low)
                            low = cur;
                    }
    /* Add any 32 bit routes on interfaces not using RSPF */
        for(i = 0; i < HASHMOD; i++)
            for(rp = Routes[31][i]; rp != NULLROUTE; rp = rp->next)
                if(!(rp->flags & RTPRIVATE) && rtif_lookup(rp->target)
                == NULLRIFACE) {
                    cur = rifdefault.horizon*65536L+rifdefault.erp*256+
                    (rp->metric > rifdefault.quality ? rp->metric :
                    rifdefault.quality);
                    if(cur > prev && cur < low)
                        low = cur;
                }
    /* Add the default route */
        if((rp = rt_blookup(0,0)) != NULLROUTE && !(rp->flags & RTPRIVATE) &&
        !dur_timer(&rp->timer)) {
            if((riface = rtif_lookup(0)) == NULLRIFACE)
                riface = &rifdefault;
            cur = riface->horizon*65536L+riface->erp*256+
            (rp->metric > riface->quality ? rp->metric : riface->quality);
            if(cur > prev && cur < low)
                low = cur;
        }
        if(low == 255*65536L+255*256+255) /* All done */
            break;
    /* now start adding the routes */
        adjcnt = 0;
        tbp = NULLBUF;
        for(adj = Adjs; adj != NULLADJ; adj = adj->next)
            if(adj->okcnt != 0 && (riface = rtif_lookup(adj->addr)) !=
                NULLRIFACE)
                if(riface->horizon*65536L+riface->erp*256+adj->cost == low){
                    pushadj(&tbp,adj->addr,32);
                    adjcnt++;
                }
        for(bits = 1; bits <= 31; bits++)
            for(i = 0; i < HASHMOD; i++)
                for(rp = Routes[bits-1][i]; rp != NULLROUTE; rp = rp->next)
                    if(!(rp->flags & RTPRIVATE) && !dur_timer(&rp->timer)){
                        if((riface = rtif_lookup(rp->target)) == NULLRIFACE)
                            riface = &rifdefault;
            /* Manually entered local routes only */
                        if(riface->horizon*65536L+riface->erp*256+
                            (rp->metric > riface->quality ? rp->metric :
                        riface->quality) == low){
                            pushadj(&tbp,rp->target,bits);
                            adjcnt++;
                        }
                    }
        for(i = 0; i < HASHMOD; i++)    /* 32 bit specific routes */
            for(rp = Routes[31][i]; rp != NULLROUTE; rp = rp->next)
                if(!(rp->flags & RTPRIVATE) && rtif_lookup(rp->target)
                    == NULLRIFACE)
                    if(rifdefault.horizon*65536L+rifdefault.erp*256+
                        (rp->metric > rifdefault.quality ? rp->metric :
                    rifdefault.quality) == low){
                        pushadj(&tbp,rp->target,bits);
                        adjcnt++;
                    }
    /* Add the default route */
        if((rp = rt_blookup(0,0)) != NULLROUTE && !(rp->flags & RTPRIVATE) &&
        !dur_timer(&rp->timer)) {
            if((riface = rtif_lookup(0)) == NULLRIFACE)
                riface = &rifdefault;
            if(riface->horizon*65536L+riface->erp*256+ (rp->metric >
            riface->quality ? rp->metric : riface->quality) == low){
                pushadj(&tbp,0,0);
                adjcnt++;
            }
        }
        if(adjcnt != 0){
        /* Prepend the link header */
            linkh.horizon = ((low >> 16) & 255);/* Horizon value */
            linkh.erp = ((low >> 8) & 255); /* ERP factor */
            linkh.cost = (low & 255);       /* Link cost */
            linkh.adjn = adjcnt;        /* Number of adjacencies */
            tmp = htonrspflink(&linkh,tbp);
            append(&bp,tmp);
            lnkcnt++;
        }
    }
    /* Prepend the node header */
    if(lnkcnt != 0){
    /* Set our address to the IP address used on the destinations
     * interface.
     */
        if(dest == INADDR_ANY || (riface = rtif_lookup(dest)) == NULLRIFACE)
            nodeh.addr = Ip_addr;
        else
            nodeh.addr = riface->iface->addr;
        if(new){    /* increase sequence number, clear subseq. number */
            if(Rspfseq == 32768L - 2 || Rspfseq == -32768L + 1)
                Rspfseq = 0;           /* wrap around */
            else
                ++Rspfseq;
            Rspfsubseq = 0;
        }
        nodeh.seq = Rspfseq;
        nodeh.subseq = 0;
        nodeh.links = lnkcnt;
        return htonrspfnode(&nodeh,bp);
    }
    else {
        free_p(bp);
        return NULLBUF;
    }
}
/* Prepends an adjacency to bpp. Allocates bpp in large chunk for efficency */
static void
pushadj(bpp,addr,bits)
struct mbuf **bpp;
int32 addr;
int bits;
{
    if(bpp == NULLBUFP)
        return;
    if(*bpp == NULLBUF) {
        *bpp = ambufw(60);        /* good for 12 adjacencies */
        (*bpp)->data += 55;
        (*bpp)->cnt = 5;
    }
    else
        *bpp = pushdown(*bpp,5);
    *(*bpp)->data = bits;
    put32((*bpp)->data+1,addr);
}
  
/* This function returns the latest routing update in network fromat from
 * the adjacency denoted by addr.
 */
static struct mbuf *
makeadjupdate(addr,rr)
int32 addr;
struct rspfrouter *rr;  /* pointer to stored routing entry, if known */
{
    struct mbuf *tmp, *tmp2, *tmp3, *bp = NULLBUF;
    struct rspfnodeh nodeh;
    struct rspflinkh linkh;
    int linkcnt = 0;
    if(rr == NULLRROUTER && (rr = rr_lookup(addr)) == NULLRROUTER)
        return NULLBUF;
    if(dup_p(&tmp,rr->data,0,len_p(rr->data)) != len_p(rr->data)) {
        free_p(tmp);
        return NULLBUF;
    }
    ntohrspfnode(&nodeh,&tmp);           /* Get the node header */
    while(nodeh.links--){
        ntohrspflink(&linkh,&tmp);
    /* Decrement and check the horizon value */
        if(--linkh.horizon > 0){
        /* Duplicate the adjacencies */
            if(dup_p(&tmp2,tmp,0,5*linkh.adjn) != 5*linkh.adjn){
                free_p(tmp);
                free_p(tmp2);
                free_p(bp);
                return NULLBUF;
            }
        /* Prepend the link header */
            tmp3 = htonrspflink(&linkh,tmp2);
            append(&bp,tmp3);
            linkcnt++;
        }
        pullup(&tmp,NULLCHAR,5*linkh.adjn); /* Skip the adjacencies */
    }
    free_p(tmp);
    if(linkcnt == 0){       /* No links at all? */
        free_p(bp);
        return NULLBUF;
    }
    nodeh.subseq = rr->subseq;
    nodeh.links = linkcnt;
    /* Prepend the node header */
    return htonrspfnode(&nodeh,bp);
}
  
/* Returns 1 if sequence number a is newer than b. Sequence numbers start
 * at -32768 + 1 and then continues with 0 and the positive integer numbers
 * until reaching 32768 - 2 after which they continue with 0.
 * Algorithm taken from RFC-1131 but fixed bug when a or b is 0.
 */
static int
isnewer(a,b)
short a,b;
{
    if((b < 0 && a > b) ||
        (a >= 0 && b >= 0 &&    /* bug corrected here */
        (((32768L - 1)/2 > (a-b) && (a-b) > 0) ||
        (a-b) < -(32768L - 1)/2)))
        return 1;
    return 0;
}

/* 29Dec2004, Replace GOTO 'timstart:' with function */
static struct mbuf *do_timstart ()
{
    if (!run_timer(&Rspfreasmt))
	{
        Rspfreasmt.func = reasmtimeout;
        Rspfreasmt.arg = NULL;
        set_timer(&Rspfreasmt,RSPF_RTIME*1000); /* The reassembly timeout */
        start_timer(&Rspfreasmt);
    }
    return NULLBUF;
}
  
/* Reassemble possibly fragmented packets into a single large one */
static struct mbuf *
rspfreasm(bp,rph,addr)
struct mbuf *bp;      /* Routing update packet without packet header */
struct rspfpacketh *rph;  /* Packet header */
int32 addr;       /* Address of station we got the packet from */
{
    union rspf rspf;
    struct rspfreasm *re, *rtmp;
    struct mbuf *tbp, *bp1, *bp2;
    for(re = Rspfreasmq; re != NULLRREASM; re = re->next)
        if(re->addr == addr){   /* found another fragment */
            if(dup_p(&tbp,re->data,0,RSPFPKTLEN) != RSPFPKTLEN) {
                free_p(tbp);
                free_p(bp);
                return NULLBUF;
            }
            ntohrspf(&rspf,&tbp);   /* get its packet header */
            if(rph->envid != rspf.pkthdr.envid) {
                del_reasm(re);     /* an obsolete entry, delete it */
                break;
            }
        /* Now try to find a place where this fragment fits in the chain */
            bp1 = re->data;
            bp2 = NULLBUF;
            while(rph->fragn > rspf.pkthdr.fragn && bp1->anext != NULLBUF){
                bp2 = bp1;
                bp1 = bp1->anext;
                if(dup_p(&tbp,bp1,0,RSPFPKTLEN) != RSPFPKTLEN) {
                    free_p(bp);
                    free_p(tbp);
                    return NULLBUF;
                }
                ntohrspf(&rspf,&tbp);
            }
            if(rspf.pkthdr.fragn == rph->fragn) {
                free_p(bp);
                return NULLBUF;    /* We already had this one */
            }
            bp = htonrspf(rph,bp);  /* Put the packet header back on */
        /* Insert the fragment at the right place in the chain */
            if(rph->fragn > rspf.pkthdr.fragn){
                bp1->anext = bp;
                bp->anext = NULLBUF;
            }
            else {
                if(bp2 != NULLBUF)
                    bp2->anext = bp;
                else
                    re->data = bp;
                bp->anext = bp1;
            }
            if(len_q(re->data) == rspf.pkthdr.fragtot){
                bp1 = NULLBUF;          /* we have all the fragments */
                while((bp2 = dequeue(&re->data)) != NULLBUF){
                    pullup(&bp2,NULLCHAR,RSPFPKTLEN); /* strip the headers */
                    append(&bp1,bp2);       /* and form a single packet */
                }
                del_reasm(re);
                stop_timer(&Rspfreasmt);
                reasmtimeout(NULL); /* restarts timer if necessary */
                return bp1;     /* return the completed packet */
            }
            re->time = secclock();      /* Update the timestamp */
			return (do_timstart ());
            /* GOTO timstart; We have to wait for fragments */
        }
    /* At this point we know that there is no matching entry in the
       reassembly queue (any more) */
    if(rph->fragtot == 1)   /* Simple case, an un-fragmented packet */
        return bp;
    tbp = htonrspf(rph,bp); /* Put the packet header back on */
    rtmp = (struct rspfreasm *) callocw(1,sizeof(struct rspfreasm));
    /* The values are filled in */
    rtmp->addr = addr;
    rtmp->time = secclock();
    rtmp->data = tbp;
    rtmp->next = Rspfreasmq;
    Rspfreasmq = rtmp;

	return (do_timstart ());	/* start timer if needed */
}
  
/* Delete a reassembly descriptor from the queue */
static void
del_reasm(re)
struct rspfreasm *re;
{
    struct rspfreasm *r, *prev = NULLRREASM;
    for(r = Rspfreasmq; r != NULLRREASM; prev = r, r = r->next)
        if(r == re){
            free_q(&re->data);
            if(prev != NULLRREASM)
                prev->next = re->next;
            else
                Rspfreasmq = re->next;
            free((char *)re);
            break;
        }
}
  
/* When the reassembly timer times out, this function tries to make use of
 * any fragments in the reassembly queue.
 */
static void
reasmtimeout(t)
void *t;
{
    union rspf rspf;
    struct rspfreasm *re;
    struct mbuf *bp, *tbp;
    int last = 0;
    int32 low;
    re = Rspfreasmq;
    while(re != NULLRREASM)
        if((secclock() - re->time) >= RSPF_RTIME){
        /* Try to use what we have */
            bp = NULLBUF;
            while((tbp = dequeue(&re->data)) != NULLBUF){
                ntohrspf(&rspf,&tbp);
                if(rspf.pkthdr.fragn != (last+1)){  /* a missing fragment */
                    if(bp != NULLBUF)
                        update_in(bp,re->addr);
                    bp = NULLBUF;
                    if(rspf.pkthdr.sync != 0)
                        pullup(&tbp,NULLCHAR,rspf.pkthdr.sync - 4);
                    else {  /* no sync possible in this fragment */
                        free_p(tbp);
                        continue;
                    }
                }
                append(&bp,tbp);
                last = rspf.pkthdr.fragn;
            }
            if(bp != NULLBUF)
                update_in(bp,re->addr);
            del_reasm(re);
            re = Rspfreasmq;    /* start over again */
        }
        else
            re = re->next;
    /* Find the oldest fragment and restart the reassembly timer */
    low = 0;
    for(re = Rspfreasmq; re != NULLRREASM; re = re->next)
        if(re->time < low || low == 0)
            low = re->time;
    if(low){
        set_timer(&Rspfreasmt,(RSPF_RTIME - secclock() + low)*1000);
        if(dur_timer(&Rspfreasmt) > 0) /* just to be safe */
            start_timer(&Rspfreasmt);
    }
}
  
/* Handle incoming routing updates (a reassembled envelope) */
static void
update_in(bp,addr)
struct mbuf *bp;    /* Reassembled data packet, without packet header */
int32 addr;     /* Senders address (in host order) */
{
    struct rspfnodeh nodeh;
    struct rspflinkh linkh;
    struct rspfadj *adj;
    struct mbuf *tbp, *tmp, *b;
    int linkcnt = 0, adjcnt = 0;
    int16 offset = 0;
    tbp = b = NULLBUF;
    /* Check to see if the sender is an adjacency */
    for(adj = Adjs; adj != NULLADJ; adj = adj->next)
        if(adj->addr == addr)
            break;
    /* One may argue that updates from non-adjacencies should not be
     * accepted since they will not contribute to the routing table.
     * But it might happen that the sender will very shortly become an
     * adjacency, and then its routing update will come handy. By increasing
     * Keeprouter, routing updates from disjoint routers will not be not be
     * purged when makeroutes() is called this time.
     */
    if(adj == NULLADJ) {
        ++Rspf_stat.noadjupdate;
        Keeprouter += 2;
    }
    else
        j2psignal(adj,1);     /* alert anyone waiting for the update */
    while(offset < len_p(bp)){
        if(adjcnt){
            if(dup_p(&tmp,bp,offset,5) == 5){
                append(&tbp,tmp);
                offset += 5;
                adjcnt--;
                continue;
            }
            else break;
        }
        if(tbp != NULLBUF){
            tmp = htonrspflink(&linkh,tbp);
            append(&b,tmp);
            tbp = NULLBUF;
        }
        if(linkcnt){
            if(dup_p(&tbp,bp,offset,RSPFLINKLEN) == RSPFLINKLEN){
                ntohrspflink(&linkh,&tbp);
                adjcnt = linkh.adjn;
                offset += RSPFLINKLEN;
                linkcnt--;
                continue;
            }
            else
                break;
        }
        if(b != NULLBUF){
            tmp = htonrspfnode(&nodeh,b);
            node_in(tmp,addr,1);     /* a full router update */
            b = NULLBUF;
        }
        if(dup_p(&tmp,bp,offset,RSPFNODELEN) == RSPFNODELEN){
            ntohrspfnode(&nodeh,&tmp);
            linkcnt = nodeh.links;
            offset += RSPFNODELEN;
        }
        else {
            free_p(bp);
            free_p(tmp);
            return;
        }
    }
    if(tbp != NULLBUF){
    /* adjust the adjacency counter in the link header */
        linkh.adjn -= adjcnt;
        tmp = htonrspflink(&linkh,tbp);
        append(&b,tmp);
    }
    /* adjust the link counter in the node header */
    nodeh.links -= linkcnt;
    tmp = htonrspfnode(&nodeh,b);
    free_p(bp);
    if(linkcnt || adjcnt)
        node_in(tmp,addr,0);    /* a partial router update */
    else
        node_in(tmp,addr,1);
}
  
static void
node_in(bp,addr,full)
struct mbuf *bp;    /* A single update, starting with the node header */
int32 addr;     /* Address of station we got packet from */
int full;       /* False if we got a partial update */
{
    struct mbuf *tbp;
    struct rspfnodeh nodeh, rnodeh;
    struct rspfrouter *rr;
    if(dup_p(&tbp,bp,0,RSPFNODELEN) != RSPFNODELEN) {
        free_p(bp);
        free_p(tbp);
        return;
    }
    ntohrspfnode(&nodeh,&tbp);
    if(ismyaddr(nodeh.addr)){
    /* If another station thinks our routing update sequence number is
     * larger than it really is, this might be because we have had
     * a fast system reset and routing updates from the old "epoch"
     * are still present in the network.
     */
        if(isnewer(nodeh.seq,Rspfseq)) {
            Rspfseq = nodeh.seq + 1;            /* Catch up */
            if(nodeh.seq == 32768L - 2 || nodeh.seq == -32768L + 1)
                Rspfseq = 0;               /* Wrap around */
            sendonerspf(nodeh.addr,addr); /* Send him the latest packet */
        }
        free_p(bp);         /* We already know our own adjacencies! */
        return;
    }
    if((rr = rr_lookup(nodeh.addr)) != NULLRROUTER) {
        if(dup_p(&tbp,rr->data,0,RSPFNODELEN) != RSPFNODELEN) {
            free_p(bp);
            free_p(tbp);
            return;
        }
        ntohrspfnode(&rnodeh,&tbp);
        if(nodeh.seq == rnodeh.seq && nodeh.subseq <= rr->subseq){
            free_p(bp);
            return; /*  We already have this one */
        }
        if(isnewer(rnodeh.seq,nodeh.seq)){
        /* Send him the latest packet */
            sendonerspf(nodeh.addr,addr);
            free_p(bp);
            ++Rspf_stat.oldreport;
            return;
        }
        if(nodeh.subseq > rr->subseq && nodeh.seq == rnodeh.seq){
            rr->subseq = nodeh.subseq;
            partialupdate(rr,bp);
            makeroutes();
            return;
        }
        if(isnewer(nodeh.seq,rnodeh.seq) && !full){
            partialupdate(rr,bp);
            makeroutes();
        /* Send a "poll" packet */
            --nodeh.seq;
            nodeh.subseq = 0;
            nodeh.links = 0;
            tbp = htonrspfnode(&nodeh,NULLBUF);
            sendupdate(tbp,1,addr);
            ++Rspf_stat.outpolls;
            return;
        }
    }
    else {
        rr = (struct rspfrouter *) callocw(1,sizeof(struct rspfrouter));
        rr->next = Rspfrouters;
        Rspfrouters = rr;
    }
    free_p(rr->data);
    rr->data = bp;
    rr->subseq = nodeh.subseq;
    rr->time = secclock();
    rr->sent = 0;
    makeroutes();
    return;
}
  
/* Return the matching RSPF route entry for addr (in host order) */
static struct rspfrouter *
rr_lookup(addr)
int32 addr;
{
    struct rspfrouter *rr;
    for(rr = Rspfrouters; rr != NULLRROUTER; rr = rr->next)
    /* this assumes that the first word of the header is the address */
        if(rr->data != NULLBUF && get32(rr->data->data) == addr)
            return rr;
    return NULLRROUTER;
}
  
/* Delete the route entry for address addr */
static void
rr_delete(addr)
int32 addr;
{
    struct rspfrouter *rr, *prev = NULLRROUTER;
    for(rr = Rspfrouters; rr != NULLRROUTER; prev = rr, rr = rr->next)
    /* this assumes that the first word of the header is the address */
        if(rr->data != NULLBUF && get32(rr->data->data) == addr) {
            if(prev != NULLRROUTER)
                prev->next = rr->next;
            else
                Rspfrouters = rr->next;
            free_p(rr->data);
            free((char *)rr);
            return;
        }
}
  
/* Handle incoming partial routing updates. Adjacencies from bp will be
 * merged into rr->data
 */
static void
partialupdate(rr,bp)
struct rspfrouter *rr;  /* current node entry in routing table */
struct mbuf *bp;    /* data packet, starting with node header */
{
    struct rspflinkh linkh, rlinkh;
    struct rspfnodeh rnodeh;
    struct mbuf *wbp, *tbp, *tmp, *b;
    int adjcnt = 0, radjcnt, rlinkcnt = 0, bits, rbits, added = 0;
    int32 addr, raddr;
  
    rlinkh.adjn = 0;
    rr->time = secclock();
    rr->sent = 0;
    /* Make a working copy of the stored routing update */
    if(dup_p(&wbp,rr->data,0,len_p(rr->data)) != len_p(rr->data)) {
        free_p(wbp);
        free_p(bp);
        return;
    }
    ntohrspfnode(&rnodeh,&wbp);
    pullup(&bp,NULLCHAR,RSPFNODELEN);   /* Strip off the node header */
    while(len_p(bp) > 0)
        if(adjcnt == 0) {
            if(ntohrspflink(&linkh,&bp) == -1) {
                free_p(wbp);
                free_p(bp);
                return;
            }
            adjcnt = linkh.adjn;
        }
        else {
            bits = PULLCHAR(&bp);   /* get one adjacency to merge */
            if(pullup(&bp,(char *)&addr,4) != 4) {
                free_p(wbp);
                free_p(bp);
                return;
            }
            addr = get32((char *)&addr); /* convert to host order */
            --adjcnt;
            radjcnt = 0;
            b = tbp = NULLBUF;
            for(;;) {           /* search through stored update */
                if(radjcnt == 0 || len_p(wbp) == 0) {
                    if(tbp != NULLBUF){
                        rlinkh.adjn -= radjcnt;
                        tmp = htonrspflink(&rlinkh,tbp);
                        ++rlinkcnt;
                        append(&b,tmp);
                        tbp = NULLBUF;
                    }
                    if(len_p(wbp) == 0)
                        break;
                }
                if(radjcnt == 0) {
                    ntohrspflink(&rlinkh,&wbp);
                    radjcnt = rlinkh.adjn;
                    if(rlinkh.horizon == linkh.horizon && rlinkh.cost ==
                    linkh.cost && rlinkh.erp == linkh.erp) {
                        pushadj(&tbp,addr,bits);
                        ++rlinkh.adjn;
                        added = 1;
                    }
                }
                else {
                    rbits = PULLCHAR(&wbp);
                    raddr = pull32(&wbp);
                    --radjcnt;
                    if(bits != rbits || addr != raddr)
                        pushadj(&tbp,raddr,rbits);  /* Put it back */
                    else
                        --rlinkh.adjn; /* deleted one adjacency */
                }
            }
            wbp = b;    /* wbp now keeps link headers and adjacencies */
            if(linkh.cost < 255 && !added){ /* Append the new link */
                ++rnodeh.links;      /* We will add one extra link */
                tmp = ambufw(5);
                *tmp->data = bits;
                put32(tmp->data+1,addr);
                tmp->cnt = tmp->size;
                tbp = htonrspflink(&linkh,tmp);
                append(&wbp,tbp);
            }
            added = 0;
        }
    free_p(rr->data);
    rnodeh.links = rlinkcnt;
    rr->data = htonrspfnode(&rnodeh,wbp);
}
  
/* The shortest path first algorithm */
static void
makeroutes()
{
    int bits, i, low, adjcnt;
    struct mbuf *bp;
    struct route *rp, *rp2, *saved[HASHMOD];
    struct rspfadj *adj, *lowadj, *gateway;
    char *lowp, *r;
    int32 lowaddr;
    struct rspflinkh linkh;
    struct rspfrouter *rr, *rrprev;
  
    if(Keeprouter)  /* if false, purge unreachable router entries */
        --Keeprouter;
    /* Before we change anything in the routing table, we have to save
     * each local adjacencies riface pointer
     */
    for(adj = Adjs; adj != NULLADJ; adj = adj->next)
        adj->scratch = (void *) rtif_lookup(adj->addr);
    /* Remove all non-manual routes */
    for(bits = 1; bits <= 32; bits++)
        for(i = 0; i < HASHMOD; i++)
            for(rp = Routes[bits-1][i]; rp != NULLROUTE; rp = rp2) {
                rp2 = rp->next;         /* BIG BUG FIX -- sm6rpz */
                if(dur_timer(&rp->timer) != 0)
                    rt_drop(rp->target,bits);
            /* rp will be undefined here if(dur_timer(&rp->timer) */
            }
  
    if((rp = rt_blookup(0,0)) != NULLROUTE && dur_timer(&rp->timer) != 0)
        rt_drop(0,0);  /* delete non-manual default route */
  
    /* Temporarily remove all 32-bit specific manual routes. This will make
     * it possible to prevent loops in findlowroute()
     */
    for(i = 0; i < HASHMOD; i++) {
        saved[i] = Routes[31][i];
        Routes[31][i] = NULLROUTE;
    }
  
    for(;;) {
        lowadj = NULLADJ;
        lowp = NULLCHAR;
        low = 255;
        for(adj = Adjs; adj != NULLADJ; adj = adj->next){
            if(adj->scratch == NULL)
                continue;       /* skip unreachable adjacency */
            if(!adj->added){
                if(adj->cost <= low){
                    low = adj->cost;
                    lowadj = adj;
                    lowp = NULLCHAR;
                }
            }
            else
                if((r = findlowroute(adj->addr,&low,adj->cost,&lowaddr,&bits))
                != NULLCHAR) {
                    lowp = r;
                    gateway = adj;
                    lowadj = NULLADJ;
                }
        }
        if(lowadj != NULLADJ){
            lowadj->added++;
            rp = rt_add(lowadj->addr,32,0,
            ((struct rspfiface *)lowadj->scratch)->iface,
            (int32)lowadj->cost,0,0);
        /* set the timer to a dummy value. This makes it possible to
         * distinguish between manual and RSPF-generated routes.
         * The timer is never started however since RSPF handles the
         * expiration of routes itself.
         */
            set_timer(&rp->timer,MSPTICK);
            continue;
        }
        if(lowp != NULLCHAR){
        /* Check if we already have this one */
            rp = rt_blookup(lowaddr,bits);
            if((rp == NULLROUTE || (rp != NULLROUTE &&
            rp->metric > (int32) low)) && !ismyaddr(lowaddr)) {
         /* This is a new route, or a route with strict lower cost than
          * the previous route (possible only if it was manual)
          */
                rp = rt_add(lowaddr,bits,gateway->addr,
                ((struct rspfiface *)gateway->scratch)->iface,
                (int32)low,0,0);
                set_timer(&rp->timer,MSPTICK); /* a dummy value */
            }
            *lowp |= 128; /* mark the route as added in any case */
        }
        else
            break; /* no more routes */
    }
  
    /* Add the saved 32 bit routes, if there isn't now a better route */
    for(i = 0; i < HASHMOD; i++) {
        rp = saved[i];
        while(rp != NULLROUTE) {
            rp2 = rt_blookup(rp->target,32);
            if(rp2 == NULLROUTE || (rp2 != NULLROUTE && rp2->metric >= rp->metric)) {
                rp2 = rt_add(rp->target,32,rp->gateway,rp->iface,rp->metric,
                0,rp->flags & RTPRIVATE);
                rp2->uses = rp->uses;
            }
            rp2 = rp->next;
            stop_timer(&rp->timer); /* Stop timer before free() -- KZ1F */
#ifndef KZ1F
            free((char *)rp);
#endif
            rp = rp2;
        }
    }
  
    /* Reset all flags */
    for(adj = Adjs; adj != NULLADJ; adj = adj->next) {
        adj->added = 0;
        adj->scratch = NULL;
    }
  
    for(rr = Rspfrouters; rr != NULLRROUTER; rr = rr->next){
        i = len_p(rr->data) - RSPFNODELEN;
    /* jump past the node header */
        if(dup_p(&bp,rr->data,RSPFNODELEN,i) != i) {
            free_p(bp);
            continue;
        }
        adjcnt = 0;
        while(i > 0){
            if(adjcnt){
                if(!Keeprouter && (*bp->data & 128) == 0) {
             /* This router has an adjacency that was not added. That
              * means that the router is unreachable. Mark the
              * stored routing update for deletion.
              */
                    free_p(bp);
                    free_p(rr->data);
                    rr->data = NULLBUF;    /* indicates disposal */
                    break;
                }
                *bp->data &= ~128;  /* Clear the "added" bit */
                pullup(&bp,NULLCHAR,5);
                i -= 5;
                --adjcnt;
                continue;
            }
            ntohrspflink(&linkh,&bp);
            adjcnt = linkh.adjn;
            i -= RSPFLINKLEN;
        }
    }
  
    if(Keeprouter)  /* nothing more to do */
        return;
    /* Delete any routers that were discovered as being unreachable */
    rrprev = NULLRROUTER;
    rr = Rspfrouters;
    for(;;) {
        for(rrprev = NULLRROUTER, rr = Rspfrouters; rr != NULLRROUTER;
            rrprev = rr, rr = rr->next)
            if(rr->data == NULLBUF) {
                if(rrprev != NULLRROUTER)
                    rrprev->next = rr->next;
                else
                    Rspfrouters = rr->next;
                free((char *)rr);
                break;
            }
        if(rr == NULLRROUTER)
            break;
    }
}
  
/* Find a route from addr with the lowest cost. Returns a pointer to the
 * buffer that keeps the significant bit count of the selected route.
 */
static char *
findlowroute(addr,low,add,resaddr,resbits)
int32 addr;         /* The node whose routes will be examined */
int *low;           /* Lowest cost so far */
int add;            /* Cost of this node */
int32 *resaddr;         /* Resulting route stored here */
int *resbits;           /* Significant bits of resulting route */
{
    struct mbuf *bp;
    struct rspfrouter *rr;
    struct rspflinkh linkh;
    struct route *rp;
    char *r, *retval = NULLCHAR;
    int adjcnt = 0;
  
    linkh.cost = 0;
    if((rr = rr_lookup(addr)) == NULLRROUTER)
        return NULLCHAR;
    if((rp = rt_blookup(addr,32)) != NULLROUTE && rp->metric < add)
        return NULLCHAR;   /* already added this one, no loops thanks */
    if(dup_p(&bp,rr->data,RSPFNODELEN,len_p(rr->data) - RSPFNODELEN) !=
    len_p(rr->data) - RSPFNODELEN) {
        free_p(bp);
        return NULLCHAR;
    }
    while(len_p(bp) > 0){
        if(adjcnt){
            if(*bp->data & 128) {
                (void) PULLCHAR(&bp);
                if((r = findlowroute(pull32(&bp),low,add+linkh.cost,resaddr,
                    resbits)) != NULLCHAR)
                    retval = r;
            }
            else {
                *low = add + linkh.cost;
                retval = bp->data;
                *resbits = PULLCHAR(&bp);
                *resaddr = pull32(&bp);
                pullup(&bp,NULLCHAR,5*(adjcnt-1));
                adjcnt = 1; /* No need to check the rest of this link */
            }
            --adjcnt;
            continue;
        }
        ntohrspflink(&linkh,&bp);
        if((add + linkh.cost) <= *low)
            adjcnt = linkh.adjn;
        else
            pullup(&bp,NULLCHAR,5*linkh.adjn);
    }
    return retval;
}
  
#ifndef AX25
/*
 * Dummy stub for RSPF if AX25 is not configured.
 */
static struct lq *
al_lookup(ifp,addr,sort)
struct iface *ifp;
char *addr;
int sort;
{
    return NULLLQ;
}
#endif  /* AX25 */
#endif /* RSPF */
  
