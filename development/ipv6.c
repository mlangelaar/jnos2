/*
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 */

#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "ip.h"
#include "pktdrvr.h"
#include "iface.h"

#include "ipv6.h"	/* 01Feb2023, Maiko (VE4KLM) */
#include "enet.h"	/* 18Mar2023, Maiko */

#include "icmpv6.h"

#include "icmp.h"	/* 05Apr2023, Maiko (revisit this, these are v4) */

#ifdef NOT_YET
  
struct mib_entry Ipv6_mib[20] = {
    { "",                 { 0 } },
    { "ipv6Forwarding",     { 1 } },
    { "ipv6DefaultTTL",     { MAXTTL } },
    { "ipv6InReceives",     { 0 } },
    { "ipv6InHdrErrors",    { 0 } },
    { "ipv6InAddrErrors",   { 0 } },
    { "ipv6ForwDatagrams",  { 0 } },
    { "ipv6InUnknownProtos",{ 0 } },
    { "ipv6InDiscards",     { 0 } },
    { "ipv6InDelivers",     { 0 } },
    { "ipv6OutRequests",    { 0 } },
    { "ipv6OutDiscards",    { 0 } },
    { "ipv6OutNoRoutes",    { 0 } },
    { "ipv6ReasmTimeout",   { TLB } },
    { "ipv6ReasmReqds",     { 0 } },
    { "ipv6ReasmOKs",       { 0 } },
    { "ipv6ReasmFails",     { 0 } },
    { "ipv6FragOKs",        { 0 } },
    { "ipv6FragFails",      { 0 } },
    { "ipv6FragCreates",    { 0 } }
};

#endif
 
/* 31Jan2023, Maiko, just a link list, same as regular ip */
struct raw_ip *Raw_ipv6;

/*
 * Send an IPV6 datagram - 03Feb2023, Maiko (VE4KLM), time to get this
 * one working, the big thing to change is source and dest addressing.
 * 
 * 18Mar2023, Maiko (VE4KLM), fundamental change, we will have to use
 * a TAP interface to get the full functionality of IPV6 to work here,
 * since Network Discovery is only available at link-layer. I'm going
 * to actually insert ethernet net header into this function instead,
 * since I have direct access to IPV6 info at this stage, and set the
 * hopper type to CL_ETHERNET instead of CL_NONE (for IP stuff). One
 * could construe this as 'bad design', but at this moment, my tiny
 * brain can't come up with a better idea, can revisit this later.
 */
int ipv6_send (source, dest, protocol, tos, ttl, bp, length, id, df)
	unsigned char *source;       /* Source ipv6 address */
	unsigned char *dest;         /* Destination ipv6 address */
	char protocol;       /* Protocol */
	char tos;            /* Type of service */
	char ttl;            /* Time-to-live */
	struct mbuf *bp;     /* Data portion of datagram */
	int16 length;        /* Optional length of data portion */
	int16 id;            /* Optional identification */
	char df;             /* Don't-fragment flag */
{
	struct mbuf *tbp;
    struct ipv6 ipv6;           /* IP header */
    static int16 id_cntr = 0;   /* Datagram serial number */
    struct phdr phdr;

	//struct ether ep;	/* 18Mar2023, Maiko */

	extern char *ipv6iface;		/* 18Mar2023, Maiko, ipv6cmd.c */
  
    ipOutRequests++;

#ifdef	NOT_YET
    if(source == INADDR_ANY)
        source = locaddr(dest);
#endif
    if(length == 0 && bp != NULLBUF)
        length = len_p(bp);
    if(id == 0)
        id = id_cntr++;
    if(ttl == 0)
        ttl = (char)ipDefaultTTL;
  
    /* Fill in IP header */

    ipv6.version = IPV6VERSION;
	ipv6.traffic_class = 0;

	/* 15Feb2023, Maiko, oops, why did I put IPV6LEN in here ? */
    ipv6.payload_len = length;

    ipv6.hop_limit = ttl;
    ipv6.next_header = protocol;

    copyipv6addr (source, ipv6.source);
    copyipv6addr (dest, ipv6.dest);

    if ((tbp = htonipv6 (&ipv6, bp)) == NULLBUF)
	{
        free_p (bp);
        return -1;
    }

	/* 18Mar2023, Maiko, Defaults when using just TAP */
    phdr.iface = NULLIF;
    phdr.type = CL_NONE;

	/*
	 * Only do this for TAP iface, yes you can still use TUN actually
	 * for very limited local use of IPV6, but you won't get any ntwk
	 * discovery and link-layer. Also if TUN accidently configured.
	 */
	if (strstr (ipv6iface, "tap"))
	{
		/*
		 * 18Mar2023, Maiko, Inserting ethernet (link-layer) header
		 * in this function, and just going direct link-layer into
		 * the network hopper (CL_ETHERNET). The tun/tap driver is
		 * really just a conduit to the kernel, not a 'driver' as 
		 * a real network card driver - does that make sense ?
		 *
		 * This means NO choice but to use TAP for IPV6 iface !
		 *
		 */
        	phdr.iface = myipv6iface();	/* HAVE TO DO THIS !!! */

#ifdef MOVED_INTO_IPV6HDR_SOURCE_AVOID_MBUF_ISSUE
		memcpy (ep.dest, dest, EADDR_LEN);
   		memcpy (ep.source, phdr.iface->hwaddr, EADDR_LEN);
		ep.type = IPV6_TYPE;

		log (-1, "insert ethernet header");

	   	if ((tbp = htonether (&ep, tbp)) == NULLBUF)
		{
   			free_p (tbp);
   			return -1;
		}

		// len = len_p (tbp);	/* this is important */
#endif
	/*
	 * end of Ethernet Header insertion
	 */

    	phdr.type = CL_ETHERNET;	/* 18Mar2023, Maiko, Pass as link-layer */
	}

    bp = pushdown (tbp, sizeof(phdr));

	/* 06Apr2023, Maiko, Oops, we're not using the local address anymore (comment out) */
    if (ismyipv6addr(&ipv6, 0) /* || ismyipv6addr (&ipv6, 1) */)
	{
        /* Pretend it has been sent by the loopback interface before
         * it appears in the receive queue
         */
        phdr.iface = &Loopback;
        Loopback.ipsndcnt++;
        Loopback.rawsndcnt++;
        Loopback.lastsent = secclock();

	/* 19Mar2023, Maiko, Terrible by commenting out
	 * the iface initially, I put the else on the memcpy
	 * and you wonder why it was not working - me bad !
	`*/
    }/* else
        phdr.iface = NULLIF; */
    // phdr.type = CL_NONE;	/* moved above 18Mar2023, Maiko */
    memcpy(&bp->data[0],(char *)&phdr,sizeof(phdr));
    enqueue(&Hopper,bp);
    return 0;
}

/*
 * 01Feb2023, Maiko (VE4KLM), okay, let's get ipv6_recv () doing something ...
 *
 * Reassemble incoming IP fragments and dispatch completed datagrams
 * to the proper transport module
 */
void ipv6_recv (iface, ipv6, bp, rxbroadcast)
struct iface *iface;    /* Incoming interface */
struct ipv6 *ipv6;      /* Extracted IP header */
struct mbuf *bp;    /* Data portion */
int rxbroadcast;    /* True if received on subnet broadcast address */
{
    /* Function to call with completed datagram */
    register struct raw_ip *rp, *prevrp, *nextrp;
    struct mbuf *bp1,*tbp;
    int rxcnt = 0;

    register struct ipv6link *ippv6;	/* 01Feb2023, Maiko */
 
#ifdef	NOT_EVEN_CONSIDERING_FRAGMENTATION_RIGHT_NOW

    /* 31Jan2023, Maiko (VE4KLM), as the #ifdef says above :| */

    /* If we have a complete packet, call the next layer
     * to handle the result. Note that fraghandle passes back
     * a length field that does NOT include the IP header
     */
    if((bp = fraghandle(ip,bp)) == NULLBUF)
        return;     /* Not done yet */
  
#endif

    // ipv6InDelivers++;

	/*
	 * 05Apr2023, Maiko (VE4KLM), okay we need this raw processing
	 * if we are to get any of the outgoing PING ipv6 to work !
	 */
 
    prevrp = NULLRIP;

    for (rp = Raw_ipv6; rp != NULLRIP; rp = nextrp)
	{
        nextrp = rp->next;

        if(rp->protocol == -1)	/* delete-me flag? */
		{
            if(prevrp != NULLRIP)
                prevrp->next = nextrp;
            else
                Raw_ipv6 = nextrp;

            free (rp);
            continue;
        }
        else if(rp->protocol == ipv6->next_header)
		{
            rxcnt++;
            /* Duplicate the data portion, and put the header back on */
            dup_p(&bp1,bp,0,len_p(bp));
            if(bp1 != NULLBUF && (tbp = htonipv6 (ipv6,bp1)) != NULLBUF)
			{
                enqueue(&rp->rcvq,tbp);
                if(rp->r_upcall != NULLVFP((struct raw_ip *)))
                    (*rp->r_upcall)(rp);
            }
			else free_p(bp1);
        }

        prevrp = rp;
    }

#ifdef	OPTIONS_ARE_DONE_DIFFERENTLY_IN_IPV6
	/* 31Jan2023, Maiko, As the #ifdef above says :| */
    if((IPLEN + ip->optlen + len_p(bp)) != ip->length) {
        /* then we lost some bytes somewhere :-( VE3DTE */
        free_p(bp);
        return;
    }
#endif

    /* Look it up in the transport protocol table */
    for(ippv6 = Ipv6link;ippv6->funct != NULL;ippv6++)
    {
        if(ippv6->proto == ipv6->next_header)	/* protocol field in ipv4 */ 
            break;
    }

	/*
	 * 31Jan2023, Maiko, and here starts the real challenge !!!
	 *
	 * technically TCP, UDP, and whatever else should not care about
	 * the lower level ipv4 or ipv6, and I don't want to have to create
	 * an ipv6 version of tcp_in for example, so need to break away from
	 * passing the complete ip (ipv6) structure to something smaller that
	 * contains only the header info needed by tcp_in for instance, does
	 * that makes sense to anyone ? tcp_in doesn't need entire details.
	 *
	 * So collect up the Iplink protocol functions, and see exactly what
	 * they need from IP header, maybe create new iptransport structure,
	 * to contain all this info, then pass that to funct instead of ip,
	 * or ipv6 if I was to go the route of separate protocol functions,
	 * which I don't want to do, I only want ONE tcp_in, and so on !
	 *
	 *  NO ! unfortunately, too many structures to modify, and so on :(
	 *
	 *  tcp_in :  length, optlen, source, dest, protocol, flags.congest, tos
 	 *   reset() is passed ip struct : only needs dest, source, new ipv6_send
	 *
	 * So the goal for starters, is just convert the current tcp_in() to use
	 * the new iptransport structure instead of ip structure, should be able
	 * to do it without wrecking the current JNOS functionality, slowly ease
	 * in the underlying requirements before we even add any real ipv6 stuff.
	 *  (NO !!! scrap that idea, too many structures to modify)
	 *
	 * OR
	 *
	 * Do I get lazy (that's not the right word), and just create a new
	 * Ipv6link ? But I am thinking major amount of code is duplicated by
	 * going that route, darn it Bob (VE3TOK), you had to ask (but probably
	 * should be done), we need to get the obsolescence of JNOS well into
	 * the 22nd century you know ... Let's try the first approach first !
	 *
	 * Oh man, even the socket structure has ipv4 address embedded into
	 * it, meaning I have to go right down low to a new ipv6socket :(
	 */
 
    if(ippv6->funct != NULL)
    {
        /* Found, call transport protocol */
        (*ippv6->funct)(iface,ipv6,bp,rxbroadcast);
    }
    else
    {

/*
 * 05Apr2023, Maiko, Time to put this section into operation as well,
 * might need a bit of work, but time to 'put it back', even if there
 * is little to no chance it will ever get called anyways ?
 *
 *  #ifdef NOT_YET 31Jan2023, Maiko, need to figure out ipv6 icmp still
 *
 */
        /* Not found */
        if(rxcnt == 0)
		{
            /* Send an ICMP Protocol Unknown response... */
            ipInUnknownProtos++;

            /* ...unless it's a broadcast */
            if(!rxbroadcast)
				icmpv6_output (ipv6, bp, ICMP_DEST_UNREACH, ICMP_PROT_UNREACH, (union icmpv6_args*)0);
        }

        free_p(bp);
    }
}

/*
 * 05Apr2023, Maiko, I am using raw ip stuff, but already defined in ip.c,
 * and no changes are required to them (they're just link lists) for ipv6,
 *
 * 07Apr2023, Maiko, Oops, we will need new functions, since the 'root' is
 * unique for IPV6, technically we could create a common set of functions,
 * but for now, keep them separate (just to get things working first).
 */

/* Arrange for receipt of raw IP datagrams */
struct raw_ip *
raw_ipv6(protocol,r_upcall)
int protocol;
void (*r_upcall)__ARGS((struct raw_ip *));
{
    register struct raw_ip *rp;
  
    rp = (struct raw_ip *)callocw(1,sizeof(struct raw_ip));

	/* 08Apr2023, Maiko */
	rp->ipver = IPV6VERSION;

    rp->protocol = protocol;
    rp->r_upcall = r_upcall;
    rp->next = Raw_ipv6;
    Raw_ipv6 = rp;
    return rp;
}
/* Free a raw IP descriptor */
void
del_ipv6(rpp)
struct raw_ip *rpp;
{
    register struct raw_ip *rp;
  
    /* Do sanity check on arg */
    for(rp = Raw_ipv6;rp != NULLRIP;rp = rp->next)
        if(rp == rpp)
            break;
    if(rp == NULLRIP)
        return; /* Doesn't exist */
  
    /* We can't unlink, as ip_recv() could be in mid-scan of the Raw_ipv6 queue
     * and wanting to follow rp->next when it runs next!  Instead, we'll just
     * change rp->protocol to -1 so ip_recv() can unlink (rp) later. N5KNX
     */
    rp->protocol = -1;  /* delete-me-later flag */

    /* Free resources */
    free_q(&rp->rcvq);
}

#ifdef	NOT_NEEDED

static struct reasm *
lookup_reasm(ip)
struct ip *ip;
{
    register struct reasm *rp;
    struct reasm *rplast = NULLREASM;
  
    for(rp = Reasmq;rp != NULLREASM;rplast=rp,rp = rp->next){
        if(ip->id == rp->id && ip->source == rp->source
        && ip->dest == rp->dest && ip->protocol == rp->protocol){
            if(rplast != NULLREASM){
                /* Move to top of list for speed */
                rplast->next = rp->next;
                rp->next = Reasmq;
                Reasmq = rp;
            }
            return rp;
        }
  
    }
    return NULLREASM;
}
/* Create a reassembly descriptor,
 * put at head of reassembly list
 */
static struct reasm *
creat_reasm(ip)
register struct ip *ip;
{
    register struct reasm *rp;
  
    if((rp = (struct reasm *)calloc(1,sizeof(struct reasm))) == NULLREASM)
        return rp;  /* No space for descriptor */
    rp->source = ip->source;
    rp->dest = ip->dest;
    rp->id = ip->id;
    rp->protocol = ip->protocol;
    set_timer(&rp->timer,ipReasmTimeout * 1000);
    rp->timer.func = ip_timeout;
    rp->timer.arg = rp;
  
    rp->next = Reasmq;
    Reasmq = rp;
    return rp;
}
  
/* Free all resources associated with a reassembly descriptor */
static void
free_reasm(r)
struct reasm *r;
{
    register struct reasm *rp;
    struct reasm *rplast = NULLREASM;
    register struct frag *fp;
  
    for(rp = Reasmq;rp != NULLREASM;rplast = rp,rp=rp->next)
        if(r == rp)
            break;
    if(rp == NULLREASM)
        return; /* Not on list */
  
    stop_timer(&rp->timer);
    /* Remove from list of reassembly descriptors */
    if(rplast != NULLREASM)
        rplast->next = rp->next;
    else
        Reasmq = rp->next;
  
    /* Free any fragments on list, starting at beginning */
    while((fp = rp->fraglist) != NULLFRAG){
        rp->fraglist = fp->next;
        free_p(fp->buf);
        free((char *)fp);
    }
    free((char *)rp);
}
  
/* Handle reassembly timeouts by deleting all reassembly resources */
static void
ip_timeout(arg)
void *arg;
{
    register struct reasm *rp;
  
    rp = (struct reasm *)arg;
    free_reasm(rp);
    ipReasmFails++;
}
/* Create a fragment */
static struct frag *
newfrag(offset,last,bp)
int16 offset,last;
struct mbuf *bp;
{
    struct frag *fp;
  
    if((fp = (struct frag *)calloc(1,sizeof(struct frag))) == NULLFRAG){
        /* Drop fragment */
        free_p(bp);
        return NULLFRAG;
    }
    fp->buf = bp;
    fp->offset = offset;
    fp->last = last;
    return fp;
}
/* Delete a fragment, return next one on queue */
static void
freefrag(fp)
struct frag *fp;
{
    free_p(fp->buf);
    free((char *)fp);
}
  
#ifndef UNIX
  
/* In red alert mode, blow away the whole reassembly queue. Otherwise crunch
 * each fragment on each reassembly descriptor
 */
void
ip_garbage(red)
int red;
{
    struct reasm *rp,*rp1;
    struct frag *fp;
    struct raw_ip *rwp;
  
    /* Run through the reassembly queue */
    for(rp = Reasmq;rp != NULLREASM;rp = rp1){
        rp1 = rp->next;
        if(red){
            free_reasm(rp);
        } else {
            for(fp = rp->fraglist;fp != NULLFRAG;fp = fp->next){
                mbuf_crunch(&fp->buf);
            }
        }
    }
    /* Run through the raw IP queue */
    for(rwp = Raw_ipv6;rwp != NULLRIP;rwp = rwp->next)
        mbuf_crunch(&rwp->rcvq);
}
  
#endif /* UNIX */

#endif	/* end of NOT_NEEDED */

