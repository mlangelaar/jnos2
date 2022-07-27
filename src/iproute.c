/* Lower half of IP, consisting of gateway routines
 * Includes routing and options processing code
 *
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Mods by PA0GRI
 */
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "iface.h"
#include "timer.h"
#include "internet.h"
#include "ip.h"
#include "netuser.h"
#include "icmp.h"
#include "rip.h"
#include "trace.h"
#include "pktdrvr.h"
#ifdef BOOTPCLIENT
#include "bootp.h"
#endif
#ifdef UNIX
#define printf tcmdprintf
#endif  

#ifdef DYNGWROUTES
/* From 04Apr2008 - N8CML (Ron) - New : functions to remove dyngw entries */
extern void rtdyngw_remove (struct route *ptr);
#endif

#ifdef RIPAMPRGW
extern int ismulticast (int32);
#endif
  
struct route *Routes[32][HASHMOD];  /* Routing table */

/* 17Oct2004, Maiko, Let's initialize this structure PROPERLY !!! */

struct route R_default = {	/* Default route entry */
	NULLROUTE,
	NULLROUTE,
	0,0,0,
    RIP_INFINITY,		/* Init metric to infinity */
#ifdef	ENCAP
	0,					/* protocol */
#endif
	(struct iface*)0,
	0,
	{					/* timer structure !!! */
		(struct timer*)0,
		0,0,
		NULL, NULL,
		0
	},
	0
#ifdef	RIP
	,0
#endif
};
  
static struct rt_cache Rt_cache;
  
#ifdef IPACCESS
#define NETBITS(bits) ((bits) == 0 ? 0 : (~0 << (32 - bits)))
struct rtaccess *IPaccess = NULLACCESS; /* access list */
#endif
  
/* Initialize modulo lookup table used by hash_ip() in pcgen.asm */
void
ipinit()
{
    int i;
  
    for(i=0;i<256;i++)
        Hashtab[i] = i % HASHMOD;
}

#define UNUSED(x) ((void)(x))	/* 04Feb2017, VE4KLM, trick to suppress any
				 * unused variable warnings, first learned
				 * to use this in convers.c
				 */
/*
 * Route an IP datagram. This is the "hopper" through which all IP datagrams,
 * coming or going, must pass.
 *
 * "rxbroadcast" is set to indicate that the packet came in on a subnet
 * broadcast. The router will kick the packet upstairs regardless of the
 * IP destination address.
 */
  
int
ip_route(i_iface,bp,rxbroadcast)
struct iface *i_iface;  /* Input interface */
struct mbuf *bp;    /* Input packet */
int rxbroadcast;    /* True if packet had link broadcast address */
{
    struct ip ip;           /* IP header being processed */
    int16 ip_len;           /* IP header length */
    int16 length;           /* Length of data portion */
    int32 gateway;          /* Gateway IP address */
    register struct route *rp;  /* Route table entry */
    struct iface *iface;        /* Output interface, possibly forwarded */
    int16 offset;           /* Offset into current fragment */
    int16 mf_flag;          /* Original datagram MF flag */
    int strict = 0;         /* Strict source routing flag */
    char prec;          /* Extracted from tos field */
    char del;
    char tput;
    char rel;
    int16 opt_len;      /* Length of current option */
    char *opt;      /* -> beginning of current option */
    int i;
    struct mbuf *tbp;
    int ckgood = IP_CS_OLD; /* Has good checksum without modification */
#ifdef IPACCESS
    int16 srcport, dstport; /* for use in access checking */
    struct rtaccess *tpacc; /* temporary for access checking */
#endif
    int pointer;        /* Relative pointer index for sroute/rroute */
  
    if(i_iface != NULLIF){
        ipInReceives++; /* Not locally generated */
        i_iface->iprecvcnt++;
    }
  
    if(len_p(bp) < IPLEN){
        /* The packet is shorter than a legal IP header */
        ipInHdrErrors++;
        free_p(bp);
        return -1;
    }
    /* Sneak a peek at the IP header's IHL field to find its length */
    ip_len = (bp->data[0] & 0xf) << 2;
    if(ip_len < IPLEN){
        /* The IP header length field is too small */
        ipInHdrErrors++;
        free_p(bp);
        return -1;
    }
    if(cksum(NULLHEADER,bp,ip_len) != 0){
        /* Bad IP header checksum; discard */
        ipInHdrErrors++;
        free_p(bp);
        return -1;
    }
    /* Extract IP header */
    ntohip(&ip,&bp);
  
    if(ip.version != IPVERSION){
        /* We can't handle this version of IP */
        ipInHdrErrors++;
        free_p(bp);
        return -1;
    }
  
    /* IP logging added - WG7J
     * only if not an AX.25 interface, since those are already
     * logged in ax_recv() in ax25.c
     */
    if((i_iface != NULLIF) && (i_iface->type != CL_AX25) &&
        (i_iface->flags & LOG_IPHEARD) )
        log_ipheard(ip.source,i_iface);
  
    /* Trim data segment if necessary. */
    length = ip.length - ip_len;    /* Length of data portion */
    trim_mbuf(&bp,length);
    if (bp == NULLBUF) {  /* possible to trim down to nothing! */
        ipInHdrErrors++;
        return -1;
    }
  
#ifndef UNIX
    /* If we're running low on memory, return a source quench */
    if(!rxbroadcast && availmem() < Memthresh) {
        extern int Icmp_SQ;
#ifdef QUENCH_NEVER_DROPS
        /* If we don't allow source quenches to be sent,
         * drop the packet - WG7J
         */
        if(!Icmp_SQ) {
            free_p(bp);
            return -1;
        }
        icmp_output(&ip,bp,ICMP_QUENCH,0,NULLICMP);
#else
/* new way: always drop but send a quench according to flag */
        if (Icmp_SQ)
            icmp_output(&ip,bp,ICMP_QUENCH,0,NULLICMP);
        free_p(bp);
        return -1;
#endif
    }
#endif /* UNIX */
  
    /* Process options, if any. Also compute length of secondary IP
     * header in case fragmentation is needed later
     */
    strict = 0;
    for(i=0;i<ip.optlen;i += opt_len)
	{
        if ((ip.options[i] & OPT_NUMBER) == IP_EOL)
			break;

        if ((ip.options[i] & OPT_NUMBER) == IP_NOOP)
		{
			opt_len = 1;
			continue;   /* No operation, skip to next option */
		}

        /* Not a 1-byte option, so ensure that there's at least
         * two bytes of option left, that the option length is
         * at least two, and that there's enough space left for
         * the specified option length.
         */
        if(ip.optlen - i < 2
            || ((opt_len = uchar(ip.options[i+1])) < 2)
        || ip.optlen - i < opt_len){
            /* Truncated option, send ICMP and drop packet */
            if(!rxbroadcast){
                union icmp_args icmp_args;
  
                icmp_args.pointer = IPLEN + i;
                icmp_output(&ip,bp,ICMP_PARAM_PROB,0,&icmp_args);
            }
            free_p(bp);
            return -1;
        }
        opt = &ip.options[i];
  
        switch(opt[0] & OPT_NUMBER){
            case IP_SSROUTE:    /* Strict source route & record route */
                strict = 1; /* note fall-thru */
            case IP_LSROUTE:    /* Loose source route & record route */
            /* Source routes are ignored unless we're in the
             * destination field
             */
                if(opt_len < 3){
                /* Option is too short to be a legal sroute.
                 * Send an ICMP message and drop it.
                 */
                    if(!rxbroadcast){
                        union icmp_args icmp_args;
  
                        icmp_args.pointer = IPLEN + i;
                        icmp_output(&ip,bp,ICMP_PARAM_PROB,0,&icmp_args);
                    }
                    free_p(bp);
                    return -1;
                }
                if(ismyaddr(ip.dest) == NULLIF)
                    break;  /* Skip to next option */
                pointer = uchar(opt[2]);
                if(pointer + 4 > opt_len)
                    break;  /* Route exhausted; it's for us */
  
            /* Put address for next hop into destination field,
             * put our address into the route field, and bump
             * the pointer. We've already ensured enough space.
             */
                ip.dest = get32(&opt[pointer]);
                put32(&opt[pointer],locaddr(ip.dest));
                opt[2] += 4;
                ckgood = IP_CS_NEW;
                break;
            case IP_RROUTE: /* Record route */
                if(opt_len < 3){
                /* Option is too short to be a legal rroute.
                 * Send an ICMP message and drop it.
                 */
                    if(!rxbroadcast){
                        union icmp_args icmp_args;
  
                        icmp_args.pointer = IPLEN + i;
                        icmp_output(&ip,bp,ICMP_PARAM_PROB,0,&icmp_args);
                    }
                    free_p(bp);
                    return -1;
                }
                pointer = uchar(opt[2]);
                if(pointer + 4 > opt_len){
                /* Route area exhausted; send an ICMP msg */
                    if(!rxbroadcast){
                        union icmp_args icmp_args;
  
                        icmp_args.pointer = IPLEN + i;
                        icmp_output(&ip,bp,ICMP_PARAM_PROB,0,&icmp_args);
                    }
                /* Also drop if odd-sized */
                    if(pointer != opt_len){
                        free_p(bp);
                        return -1;
                    }
                } else {
                /* Add our address to the route.
                 * We've already ensured there's enough space.
                 */
                    put32(&opt[pointer],locaddr(ip.dest));
                    opt[2] += 4;
                    ckgood = IP_CS_NEW;
                }
                break;
        }
    }

#ifdef RIPAMPRGW
	if (ismulticast (ip.dest))
		rxbroadcast = 1;
#endif

    /* See if it's a broadcast or addressed to us, and kick it upstairs */
    if (ismyaddr(ip.dest) != NULLIF || rxbroadcast
#ifdef BOOTPCLIENT
       || (WantBootp && bootp_validPacket(&ip, &bp))
#endif
      ){
#ifdef  GWONLY
    /* We're only a gateway, we have no host level protocols */
        if(!rxbroadcast)
            icmp_output(&ip,bp,ICMP_DEST_UNREACH,
            ICMP_PROT_UNREACH,NULLICMP);
        ipInUnknownProtos++;
        free_p(bp);
#else
        ip_recv(i_iface,&ip,bp,rxbroadcast);
#endif  /*GWONLY */
        return 0;
    }
  
    /* Packet is not destined to us. If it originated elsewhere, count
     * it as a forwarded datagram.
     */
    if(i_iface != NULLIF)
        ipForwDatagrams++;
  
    /* Adjust the header checksum to allow for the modified TTL */
    ip.checksum += 0x100;
    if((ip.checksum & 0xff00) == 0)
        ip.checksum++;  /* end-around carry */
  
    /* Decrement TTL and discard if zero. We don't have to check
     * rxbroadcast here because it's already been checked
     */
    if(--ip.ttl == 0){
        /* Send ICMP "Time Exceeded" message */
        /* If the following flag is not set, we don't send this !
         * This makes our system 'invisible' for traceroutes,
         * and might be a little more secure for internet<->ampr
         * gateways - WG7J
         */
        extern int Icmp_timeexceed;
  
        if(Icmp_timeexceed)
            icmp_output(&ip,bp,ICMP_TIME_EXCEED,0,NULLICMP);
        ipInHdrErrors++;
        free_p(bp);
        return -1;
    }
    /* Look up target address in routing table */
    if((rp = rt_lookup(ip.dest)) == NULLROUTE){
        /* No route exists, return unreachable message (we already
         * know this can't be a broadcast)
         */
        icmp_output(&ip,bp,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,NULLICMP);
        free_p(bp);
        ipOutNoRoutes++;
        return -1;
    }
    rp->uses++;
  
    /* Check for output forwarding and divert if necessary */
    iface = rp->iface;
    if(iface->forw != NULLIF)
        iface = iface->forw;
  
#ifdef IPACCESS
/* At this point we've decided to send the packet out 'iface' (unless it
won't fragment).  Check ip.protocol for tcp or udp.
Should probably have counter on number of dropped packets.
*/
	/* 04Feb2017, Maiko (VE4KLM), should probably initialize these !!! */
	srcport = dstport = 0;

    if((ip.protocol == TCP_PTCL) || (ip.protocol==UDP_PTCL)) {
        /* pull up source & destination port */
        srcport = get16(&(bp->data[0])); /*done for futures & debug*/
        dstport = get16(&(bp->data[2]));
    }
	UNUSED (srcport); /* 04Feb2017, Maiko (VE4KLM), compiler warning */
	
    /* for backwards compatibility, if there are *no* entries */
    /* we ignore access control, so everything is legal. Any  */
    /* entry for an iface means default is to drop if no      */
    /* permission found  */
    for( tpacc = IPaccess;tpacc != NULLACCESS;tpacc=tpacc->nxtiface) {
        /*find a matching iface*/
        if(tpacc->iface == iface ){ /* Needs to be authorized */
            if(!ip_check(tpacc, ip.protocol, ip.source, ip.dest, dstport))
                break;
            /* not found, throw it away. */
            /* if NO access, then drop & increment counter */
            /* see new(er) RFC for proper ICMP codes */
            /* maybe counter is in MIB? else create.*/
            icmp_output(&ip,bp,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,NULLICMP);
            ipOutNoRoutes++;
            free_p(bp);
            return -1;
        }
    }
#endif
    /* Find gateway; zero gateway in routing table means "send direct" */
    if(rp->gateway == 0)
        gateway = ip.dest;
    else
        gateway = rp->gateway;
  
    if(strict && gateway != ip.dest){
        /* Strict source routing requires a direct entry
         * Again, we know this isn't a broadcast
         */
        icmp_output(&ip,bp,ICMP_DEST_UNREACH,ICMP_ROUTE_FAIL,NULLICMP);
        free_p(bp);
        ipOutNoRoutes++;
        return -1;
    }
    prec = PREC(ip.tos);
    del = ip.tos & DELAY;
    tput = ip.tos & THRUPUT;
    rel = ip.tos & RELIABILITY;
  
#ifdef	ENCAP
	/* 14Oct2004, Maiko, IPUDP support (K2MF) */
	if (iface == &Encap)
		iface->protocol = rp->protocol;
#endif

    if(ip.length <= iface->mtu){
        /* Datagram smaller than interface MTU; put header
         * back on and send normally.
         */
        if((tbp = htonip(&ip,bp,ckgood)) == NULLBUF){
            free_p(bp);
            return -1;
        }
        iface->ipsndcnt++;
        return (*iface->send)(tbp,iface,gateway,prec,del,tput,rel);
    }
    /* Fragmentation needed */
    if(ip.flags.df){
        /* Don't Fragment set; return ICMP message and drop */
        union icmp_args icmp_args;
  
        icmp_args.mtu = iface->mtu;
        icmp_output(&ip,bp,ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED,&icmp_args);
        free_p(bp);
        ipFragFails++;
        return -1;
    }
    /* Create fragments */
    offset = ip.offset;
    mf_flag = ip.flags.mf;      /* Save original MF flag */
    while(length != 0){     /* As long as there's data left */
        int16 fragsize;     /* Size of this fragment's data */
        struct mbuf *f_data;    /* Data portion of fragment */
  
        /* After the first fragment, should remove those
         * options that aren't supposed to be copied on fragmentation
         */
        ip.offset = offset;
        if(length + ip_len <= iface->mtu){
            /* Last fragment; send all that remains */
            fragsize = length;
            ip.flags.mf = mf_flag;  /* Pass original MF flag */
        } else {
            /* More to come, so send multiple of 8 bytes */
            fragsize = (iface->mtu - ip_len) & 0xfff8;
            ip.flags.mf = 1;
        }
        ip.length = fragsize + ip_len;
  
        /* Duplicate the fragment */
        dup_p(&f_data,bp,offset,fragsize);
        if(f_data == NULLBUF){
            free_p(bp);
            ipFragFails++;
            return -1;
        }
        /* Put IP header back on, recomputing checksum */
        if((tbp = htonip(&ip,f_data,IP_CS_NEW)) == NULLBUF){
            free_p(f_data);
            free_p(bp);
            ipFragFails++;
            return -1;
        }
        /* and ship it out */
        if((*iface->send)(tbp,iface,gateway,prec,del,tput,rel) == -1){
            ipFragFails++;
            free_p(bp);
            return -1;
        }
        iface->ipsndcnt++;
        ipFragCreates++;
        offset += fragsize;
        length -= fragsize;
    }
    ipFragOKs++;
    free_p(bp);
    return 0;
}
  
#ifdef ENCAP
int EncapPID = IP_PTCL; /* was IP_PTCL_OLD; see IP ENCAP cmd -- N5KNX */

int
ip_encap(bp,iface,gateway,prec,del,tput,rel)
struct mbuf *bp;
struct iface *iface;
int32 gateway;
int prec;
int del;
int tput;
int rel;
{
	struct ip ip;
	/* 12Oct2004, Maiko, New IPUDP support (K2MF) */
	struct socket lsocket, rsocket;
  
    dump(iface,IF_TRACE_OUT,CL_NONE,bp);
#ifdef J2_SNMPD_VARS
    iface->rawsndbytes += len_p (bp);
#endif
    iface->rawsndcnt++;
    iface->lastsent = secclock();
  
    if(gateway == 0L){
        /* Gateway must be specified */
        ntohip(&ip,&bp);
        icmp_output(&ip,bp,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,NULLICMP);
        free_p(bp);
        ipOutNoRoutes++;
        return -1;
    }

	/* 12Oct2004, Maiko, New IPUDP support (K2MF) */
	if(iface->protocol == UDP_PTCL)
	{
		lsocket.address = 0L;
		lsocket.port = IPPORT_IPUDP;
		rsocket.address = gateway;
		rsocket.port = IPPORT_IPUDP;

		/* We set the length to zero and let the function determine it */
		return send_udp(&lsocket,&rsocket,0,0,bp,0,0,0);
	}
	else return ip_send(INADDR_ANY,gateway,EncapPID,0,0,bp,0,0,0);
}

#endif /*ENCAP*/
  
/* Add an entry to the IP routing table. */
struct route *
rt_add(target,bits,gateway,iface,metric,ttl,private)
int32 target;       /* Target IP address prefix */
unsigned int bits;  /* Size of target address prefix in bits (0-32) */
int32 gateway;      /* Optional gateway to be reached via interface */
struct iface *iface;    /* Interface to which packet is to be routed */
int32 metric;       /* Metric for this route entry */
int32 ttl;      /* Lifetime of this route entry in sec */
char private;       /* Inhibit advertising this entry ? */
{
    struct route *rp,**hp;
    struct route *rptmp;
    int32 gwtmp;
  
#ifdef ENCAP
	/* 14Oct2004, Maiko, IPUDP support (K2MF) - rt_add mods differ from his */
	extern int GLOB_encap_protocol;
#endif

    if(iface == NULLIF)
        return NULLROUTE;
  
    if(bits == 32 && ismyaddr(target))
        return NULLROUTE;   /* Don't accept routes to ourselves */
  
    if(bits > 32)
        bits = 32;      /* Bulletproofing */
#ifdef ENCAP
    /* Encapsulated routes must specify gateway, and it can't be
     *  ourselves
     */
    if(iface == &Encap && (gateway == 0 || ismyaddr(gateway)))
        return NULLROUTE;
#endif
  
    Rt_cache.route = NULLROUTE; /* Flush cache */
  
    /* Mask off don't-care bits of target */
    target &= ~0L << (32-bits);
  
    /* Zero bits refers to the default route */
    if(bits == 0){
        rp = &R_default;
    } else {
        rp = rt_blookup(target,bits);
    }

    if(rp == NULLROUTE)
    {
        /* The target is not already in the table, so create a new
         * entry and put it in.
         */
        rp = (struct route *)callocw(1,sizeof(struct route));
        /* Insert at head of table */
        rp->prev = NULLROUTE;
        hp = &Routes[bits-1][hash_ip(target)];
        rp->next = *hp;
        if(rp->next != NULLROUTE)
            rp->next->prev = rp;
        *hp = rp;
        rp->uses = 0;
	rp->flags = 0;	/* From 04Apr2008 - N8CML (Ron) */
    }

#ifdef DYNGWROUTES
    /* From 04Apr2008 - N8CML (Ron) - New : remove dyngw route if it exists */
    else if (rp->flags & RTDYNGWROUTE)
    {
		rtdyngw_remove (rp); /* The flag was set, remove the entry */
		rp->flags &= ~RTDYNGWROUTE; /* clear the flag just in case */
    }
#endif

    rp->target = target;
    rp->bits = bits;
    rp->gateway = gateway;
    rp->metric = metric;
    rp->iface = iface;
    rp->flags = private ? RTPRIVATE : 0; /* hide this from public view ? */

#ifdef ENCAP
    /* 14Oct2004, Maiko, IPUDP support (K2MF) - rt_add mods differ from his */
    if (rp->iface == &Encap)
        rp->protocol = GLOB_encap_protocol;
#endif

    rp->timer.func = rt_timeout;  /* Set the timer field */
    rp->timer.arg = (void *)rp;
    set_timer(&rp->timer,ttl*1000);
    stop_timer(&rp->timer);
    start_timer(&rp->timer); /* start the timer if appropriate */
  
#ifdef ENCAP
    /* Check to see if this created an encapsulation loop */
    gwtmp = gateway;
    for(;;){
        rptmp = rt_lookup(gwtmp);
        if(rptmp == NULLROUTE)
            break;  /* No route to gateway, so no loop */
        if(rptmp->iface != &Encap)
            break;  /* Non-encap interface, so no loop */
        if(rptmp == rp){
            rt_drop(target,bits);   /* Definite loop */
            return NULLROUTE;
        }
        if(rptmp->gateway != 0)
            gwtmp = rptmp->gateway;
    }
#endif
    return rp;
}
  
/* Remove an entry from the IP routing table. Returns 0 on success, -1
 * if entry was not in table.
 */
int
rt_drop(target,bits)
int32 target;
unsigned int bits;
{
    register struct route *rp;
  
    Rt_cache.route = NULLROUTE; /* Flush the cache */
  
    if(bits == 0){
        /* Nail the default entry */
        stop_timer(&R_default.timer);
        R_default.iface = NULLIF;
        return 0;
    }
    if(bits > 32)
        bits = 32;
  
    /* Mask off target according to width */
    target &= ~0L << (32-bits);
  
    /* Search appropriate chain for existing entry */
    for(rp = Routes[bits-1][hash_ip(target)];rp != NULLROUTE;rp = rp->next){
        if(rp->target == target)
            break;
    }
    if(rp == NULLROUTE)
        return -1;  /* Not in table */

#ifdef DYNGWROUTES
    /*
	 * From 04Apr2008 - N8CML (Ron) - New code by N8CML (Ron). Delete any
	 * dynamic gateway entries, otherwise we end up with a dynamic gateway
	 * entry that points to released memory that would be updated on the
	 * next dynamic gateway update.
	 */
	if (rp->flags & RTDYNGWROUTE) rtdyngw_remove (rp);
#endif
  
    stop_timer(&rp->timer);
    if(rp->next != NULLROUTE)
        rp->next->prev = rp->prev;
    if(rp->prev != NULLROUTE)
        rp->prev->next = rp->next;
    else
        Routes[bits-1][hash_ip(target)] = rp->next;
  
    free((char *)rp);
    return 0;
}

#ifndef GWONLY
/* Given an IP address, return the MTU of the local interface used to
 * reach that destination. This is used by TCP to avoid local fragmentation
 */
int16
ip_mtu(addr)
int32 addr;
{
    register struct route *rp;
    struct iface *iface;
  
    rp = rt_lookup(addr);
    if(rp == NULLROUTE || rp->iface == NULLIF)
        return 0;
  
    iface = rp->iface;
    if(iface->forw != NULLIF)
        return iface->forw->mtu;
    else
        return iface->mtu;
}
/* Given a destination address, return the IP address of the local
 * interface that will be used to reach it. If there is no route
 * to the destination, pick the first non-loopback address.
 */
int32
locaddr(addr)
int32 addr;
{
    register struct route *rp;
    struct iface *ifp;
  
    if(ismyaddr(addr) != NULLIF)
        return addr;    /* Loopback case */
  
    rp = rt_lookup(addr);
    if(rp != NULLROUTE && rp->iface != NULLIF)
        ifp = rp->iface;
    else {
        /* No route currently exists, so just pick the first real
         * interface and use its address
         */
        for(ifp = Ifaces;ifp != NULLIF;ifp = ifp->next){
#ifdef ENCAP
            if(ifp != &Loopback && ifp != &Encap)
#else
                if(ifp != &Loopback)
#endif
                    break;
        }
    }
    if(ifp == NULLIF || ifp == &Loopback)
        return 0;   /* No dice */
  
#ifdef ENCAP
    if(ifp == &Encap){
        /* Recursive call - we assume that there are no circular
         * encapsulation references in the routing table!!
         * (There is a check at the end of rt_add() that goes to
         * great pains to ensure this.)
         */
        /* Next couple of lines are a point of discussion
         * The ultimate source for encaptulation is the local
         * IP address. Phil looks for the address of the addres
         * to be used, ending up with the wrong one (in my oppinion)
         * If you disagree set encap ip address to what you want.
         */
        if(Encap.addr != 0)
            return Encap.addr;
        return Ip_addr;
    }
#endif /*ENCAP*/
    if(ifp->forw != NULLIF)
        return ifp->forw->addr;
    else
        return ifp->addr;
}
#endif /*GWONLY*/
/* Look up target in hash table, matching the entry having the largest number
 * of leading bits in common. Return default route if not found;  if default
 * route not set, or route set to Loopback interface, return NULLROUTE
 */
struct route *
rt_lookup(target)
int32 target;
{
    register struct route *rp;
    int bits;
    int32 tsave = target;
    int32 mask = ~0L; /* All ones */
  
    /* Examine cache first */
    if(target == Rt_cache.target && Rt_cache.route != NULLROUTE)
        return Rt_cache.route;
  
    for(bits = 31;bits >= 0; bits--){
        target &= mask;
        for(rp = Routes[bits][hash_ip(target)];rp != NULLROUTE;rp = rp->next){
            if(rp->target == target){
                /* Loopback interface produces no IP route!
                 * - 07/94 K2MF */
                if(rp->iface == &Loopback)
                    rp = NULLROUTE;
                else {
                    /* Stash in cache and return */
                    Rt_cache.target = tsave;
                    Rt_cache.route = rp;
                }
                return rp;
            }
        }
        mask <<= 1;
    }

    if(R_default.iface == NULLIF || R_default.iface == &Loopback)
        return NULLROUTE;
    else {
        Rt_cache.target = tsave;
        Rt_cache.route = &R_default;
        return &R_default;
    }
}
/* Search routing table for entry with specific width */
struct route *
rt_blookup(target,bits)
int32 target;
unsigned int bits;
{
    register struct route *rp;
  
    if(bits == 0){
        if(R_default.iface != NULLIF)
            return &R_default;
        else
            return NULLROUTE;
    }
    /* Mask off target according to width */
    target &= ~0L << (32-bits);
  
    for(rp = Routes[bits-1][hash_ip(target)];rp != NULLROUTE;rp = rp->next){
        if(rp->target == target){
            return rp;
        }
    }
    return NULLROUTE;
}
/* Scan the routing table. For each entry, see if there's a less-specific
 * one that points to the same interface and gateway. If so, delete
 * the more specific entry, since it is redundant.
 *
 * N7IPB - modified so that entries with target = gateway are correctly merged
 * into entries without a gateway specified.  This allows RIP to work better
 * in situations where you have multiple gateways on the same frequency.
 * Logic is now:
 * if newroute.interface == existingroute.interface and
 *    (newroute.gateway == existingroute.gateway or
 *     newroute.gateway == newroute.targetaddress) then drop the new entry.
 */
void
rt_merge(trace)
int trace;
{
    int bits,i,j;
    struct route *rp,*rpnext,*rp1;
  
    for(bits=32;bits>0;bits--){
        for(i = 0;i<HASHMOD;i++){
            for(rp = Routes[bits-1][i];rp != NULLROUTE;rp = rpnext){
                rpnext = rp->next;
                for(j=bits-1;j >= 0;j--){
                    if((rp1 = rt_blookup(rp->target,j)) != NULLROUTE
                        && rp1->iface == rp->iface
                        && (rp1->gateway == rp->gateway
                    || rp->gateway == rp->target)){
                        if(trace > 1)
                            printf("merge %s %d\n",
                            inet_ntoa(rp->target),
                            rp->bits);
                        rt_drop(rp->target,rp->bits);
                        break;
                    }
                }
            }
        }
    }
}
  
#ifdef IPACCESS
/* check to see if packet is "authorized".  Returns 0 if matching */
/* permit record is found, -1 if not found or deny record found */
int
ip_check(accptr,protocol,src,dest,port)
struct rtaccess *accptr;
int16 protocol,port;
int32 src,dest;
{
    int32 smask,tmask;
  
    for(;accptr != NULLACCESS;accptr = accptr->nxtbits) {
        if ((accptr->protocol == 0) ||
        (accptr->protocol == protocol)) {
            smask = NETBITS(accptr->sbits);
            tmask = NETBITS(accptr->bits);
            if ((accptr->source == (smask & src)) &&
                (accptr->target == (tmask & dest)) &&
                ((accptr->lowport == 0) ||
                ((port >= accptr->lowport) &&
            (port <= accptr->highport)))) {
                return (accptr->status);
            }
        }
    }
    return -1; /* fall through to here if not found */
}
/* add an entry to the access control list */
/* not a lot of error checking 8-) */
void
addaccess(protocol,source,sbits,target,tbits,ifp,low,high,permit)
int16 protocol;         /* IP protocol */
int32 source,target;        /* Source & target IP address prefix */
unsigned int sbits,tbits;   /* Size of address prefix in bits (0-32) */
struct iface *ifp;      /* Interface to which packet may be routed */
int16 low;
int16 high;
int16 permit;
{
    struct rtaccess *tpacc; /*temporary*/
    struct rtaccess *holder; /*for the new record*/
  
    holder = (struct rtaccess *)callocw(1,sizeof(struct rtaccess));
    holder->nxtiface = NULLACCESS;
    holder->nxtbits = NULLACCESS;
    holder->protocol = protocol;
    holder->source = source;
    holder->sbits = sbits;
    holder->target = target;
    holder->bits = tbits;
    holder->iface = ifp;
    holder->lowport = low;
    holder->highport = high;
    holder->status = permit;
    for(tpacc = IPaccess;tpacc != NULLACCESS;tpacc = tpacc->nxtiface)
        if(tpacc->iface == ifp) { /* get to end */
            while(tpacc->nxtbits != NULLACCESS)
                tpacc = tpacc->nxtbits;
            tpacc->nxtbits = holder;
            return;
        }
  /* iface wasn't found, so just add at head of list */
    holder->nxtiface = IPaccess;
    IPaccess = holder;
}
#endif
  
/* Direct IP input routine for packets without link-level header */
void
ip_proc(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
    ip_route(iface,bp,0);
}
  
  
