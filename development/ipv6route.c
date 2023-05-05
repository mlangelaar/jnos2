/*
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
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

#include "ipv6.h"	/* 01Feb2023, Maiko */
#include "icmpv6.h"	/* 30Mar2023, Maiko */

/*
 * Route an IPV6 datagram - 31Jan2023, Maiko, New IPV6 handling
 *
 * If rxbroadcast is set to indicate that the packet came in on a subnet
 * broadcast. The router will kick the packet upstairs regardless of the
 * IP destination address.
 */
  
int ipv6_route (struct iface *i_iface, struct mbuf *bp, int rxbroadcast)
{
    struct ipv6 ipv6;
    struct mbuf *tbp;
    struct iface *iface;	/* 07Feb2023, Maiko, needed for output interface ! */

/*
 * 30Mar2023, Maiko (VE4KLM), I am an IDIOT !!!!
 *
 * Ubuntu let's not send to tap iface, rest of linux ? no problem who cares
 *
 * It would help that I initialize vars (like result2) that I commented
 * out further below, since I still look at these vars to determine if a
 * packet needs to go out the interface - BIG F'N GRRRRRRR !!!!
 */
	int result = 0, result2 = 0, result3 = 0;	/* do ALL of them !!! */

    // char prec, del, tput, rel;	/* put back 06Feb2023, probably useless */

	// int16	gateway[16];	/* 06Feb2023, need gateway to be ipv6 gateway */
 
#ifdef NOT_YET 

	if (i_iface != NULLIF)
	{
		ipv6InReceives++; /* Not locally generated */
		i_iface->ipv6recvcnt++;
	}
#endif

#ifdef	WRONG_SHOULD_NOT_EVEN_BE_HERE
	/* 03Feb2023, Maiko, this is why ipv6_receive was never being called, damn it */
	if (len_p (bp) != IPV6LEN)
	{
		/* The packet length does not match a legal IPV6 header */
		// ipv6InHdrErrors++;
		free_p (bp);
		return -1;
	}
#endif

    /* Extract IP header */
    ntohipv6 (&ipv6, &bp);

/*
 * 13Mar2023, Maiko (VE4KLM), Got TAP interace running, so that we can receive
 * the IPV6 version of arp request (NDP), so we now have to look for multicast
 * addresses, so let's look for ff02 in the top octect (for starters anyway).
 */
	if (ipv6.dest[0] == 0xff && ipv6.dest[1] == 0x02)
	{
		/* okay I think I should NOT be setting broadcast, arp errors
		 occur, just match on the dest instead, multicast, not broadcast
		 noticing this on the tcpdumps, yeah broadcast is not MULTICAST
			(22Mar2023, Maiko, Move this to ismyipv6addr area !)
		 */
	}

/* 18Apr2023, Maiko (VE4KLM), Time to put IPV6 info into the IP heard list */
	if ((i_iface != NULLIF) && (i_iface->type != CL_AX25) && (i_iface->flags & LOG_IPHEARD))
        log_ipheard (ipv6.source, i_iface);

#ifdef NOT_YET
    /* Trim data segment if necessary. */
    length = ipv6.length - ip_len;    /* Length of data portion */
    trim_mbuf(&bp,length);
    if (bp == NULLBUF) {  /* possible to trim down to nothing! */
        ipInHdrErrors++;
        return -1;
    }
#endif

#ifdef	OPTIONS_WORK_DIFFERENTLY_IN_IPV6
 
   /* 31Jan2023, Maiko (VE4KLM), not right now, just get ipv6 working */

    /* Process options, if any. Also compute length of secondary IP
     * header in case fragmentation is needed later
     */
    strict = 0;
    for(i=0;i<ip.optlen;i += opt_len)
      blah blah blah

#endif	/* OPTIONS_WORK_DIFFERENTLY_IN_IPV6 */

	/*	
	 * 03Feb2023, Maiko, would help if I actually had an ipv6addr setup,
	 * coded that is, for JNOS before we can expect entry into ipv6_recv
	 * function - hardcoding it in ipv6iface.c for now ...
	 *
	 * And it appears I need to work on this 'ismyipv6addr function' :|
	 *
	 */

    /* See if it's a broadcast or addressed to us, and kick it upstairs */

	/* 03Feb2023, Maiko, Decided to just pass entire ipv6 struct, dest
	 * is NOT working, no idea why, obviously not understanding something
	 * here with regard to pointers and int16 arrays :(
	 * 26Mar2023, Maiko, Only need to watch for my public and multicast
	 */
	result =  ismyipv6addr (&ipv6, 0);
#ifdef	WE_DONT_NEED_TO_LOOK_FOR_LINK_LOCAL_ADDRESS
	result2 = ismyipv6addr (&ipv6, 1);
#endif
	result3 = ((ipv6.dest[0] == 0xff && ipv6.dest[1] == 0x02));

	if (result || result2 || result3 || rxbroadcast)
	{
		// log (-1, "ipv6_route - this packet is for us");	/* 07Feb2023, Maiko */

		/* 31Jan2023, Maiko, No BOOTPCLIENT or GWONLY at this time */

        ipv6_recv (i_iface, &ipv6, bp, rxbroadcast);

        return 0;
    }

/*
 * 06Feb2023, Maiko (VE4KLM), another huge learning moment with NOS, yeah
 * even after all these years since taking it over, EVERYTHING in and out
 * of JNOS goes through the network() hopper in config.c - for example :
 * if I ping JNOS from linux host, it gets handle in the above ipv6_recv()
 * which creates a reponse that is put back onto the hopper, and that msg
 * then gets to this comment (it's not ours anymore), so original iproute
 * had code to send it to where it should go, so I will now restore bits
 * and pieces of that code further below, including an all important call
 * to the IFACE->send() stub :] Yup, that's how it all works, very cool.
 */

#ifdef NOT_YET
    /* Packet is not destined to us. If it originated elsewhere, count
     * it as a forwarded datagram.
     */
    if (i_iface != NULLIF)
        ipv6ForwDatagrams++;
#endif

	/*
	 * 21Mar2023, Maiko, Wonder if wrong hop_limit is why this is not working
	 *
	 * 31Mar2023, Maiko, Should add to this rule, I am worried about it going
	 * into a wicked loop, right now this is the only way for me to really know
	 * if this is a Network Discovery icmpv6 message, without having to do sneak
	 * peak into the data. So far been okay, should I put in a safety counter ?

	if (ipv6.hop_limit == 255)
	{
		log (-1, "network discovery - don't touch hop_limit");
	}
	else

	 */

    /* Decrement TTL and discard if zero. We don't have to check
     * rxbroadcast here because it's already been checked
	 * 31Mar2023, Put the 255 check here, no more debug log() needed
     */
	if ((ipv6.hop_limit != 255) && (--ipv6.hop_limit <= 0))
	{
		/*
		 * 31Jan2023, Maiko, use < instead of = 0 since I think with IPV6
		 * you actually accept the packet when you hit the limit ? Need to
		 * read up on this.
		 */

		log (-1, "ipv6 hop limit (ttl) exceeded");

	/* 14Apr2023, Maiko, Terrible of me, forgot
	 * about this one after adding code below,
	 * discovered only during hop check work.
	 */
       	//	free_p(bp);

		/*
		 * 30Mar2023, Maiko, Time to start using icmpv6_output !!!
	 	 *
         * Send ICMP "Time Exceeded" message
         * If the following flag is not set, we don't send this !
         * This makes our system 'invisible' for traceroutes,
         * and might be a little more secure for internet<->ampr
         * gateways - WG7J
         */
        extern int Icmp_timeexceed;
  
	/* 15Apr2023, Maiko, make sure to use the ICMPV6 message type, not ICMP */
        if(Icmp_timeexceed)
			icmpv6_output (&ipv6, bp, ICMPV6_TIME_EXCEED, 0, (union icmpv6_args*)0);

        ipInHdrErrors++;
        free_p(bp);
        return -1;
    }

	/*
	 * 31Jan2023, Maiko (VE4KLM), routing table will be 'interesting' :(
	 */

	/*
	 * 07Feb2023, Maiko (VE4KLM), temporary solution till routing tables are put
	 * into place.
	 *
	 *    iface = myipv6iface ("tun1");
	 *
	 * 09Feb2023, Maiko, modified to accomodate new 'ipv6 iface' command, and
	 * also make sure if the iface is not set, then stop, discard, and exit !
	 */
	if (!(iface = myipv6iface ()))
	{
		log (-1, "no ipv6 interface defined, no routes available");
		/* NOTE : should probably put in an icmp no dest house later on */
    		free_p (bp);
            	return -1;
	}

    //// CONTINUE FROM HERE /////

	/*
	 * 01Feb2023, Maiko, No route tables, no output forwarding right now
	 *
	 * 07Feb2023, Maiko (VE4KLM), because I have not bothered with routing
	 * tables quite yet, there is still the issue of setting the 'iface'
	 * needed for iface->send(), so even for local host, there is a route
	 * table entry that tells us what the iface is, so for now we need to
	 * set it to whatever myipv6addr() returns (a few lines above) ...
	 *
	 * AND with that done ? WAITING
         */


	/*
	 * 23Jan2023, Maiko (VE4KLM), Not even interested in the IPACCESS portion
	 * of this new IPV6 attempt, not right now anyways, just get it working !
	 */

	/* 06Feb2023, Maiko, Not sure the follow 4 lines are even relevant here (yet)
    prec = PREC (ipv6.traffic_class);
    del = ipv6.traffic_class & DELAY;
    tput = ipv6.traffic_class & THRUPUT;
    rel = ipv6.traffic_class & RELIABILITY;
 */

	/*
	 * 06Feb2023, Maiko (VE4KLM), BINGO, and here is where the magic of the
	 * return packet occurs, after 23 years and just learning this now ...
	 */
	if (ipv6.payload_len <= iface->mtu)
	{
        /* Datagram smaller than interface MTU; put header
         * back on and send normally.
         */
        if ((tbp = htonipv6 (&ipv6, bp)) == NULLBUF)
		{
            free_p(bp);
            return -1;
        }
        iface->ipsndcnt++;

		/* gateway, prec, del, tput, rel are all unused for TUN interface ! */

		return (*iface->send)(tbp,iface,0,0,0,0,0);
    }

	/* 30Mar2023, Maiko (VE4KLM), Okay, I really need to get icmpv6_output() in place */
	log (-1, "ipv6_route - payload %d exceeds iface MTU %d", ipv6.payload_len, iface->mtu);

	/* 01Feb2023, Maiko, No gateway either, just ICMP everything from this point on */

	/* 30Mar2023, Maiko, Finally have a 'working' icmpv6 output function !!! */
	icmpv6_output (&ipv6, bp, ICMP_DEST_UNREACH, ICMP_ROUTE_FAIL, (union icmpv6_args*)0);

    // ipOutNoRoutes++;

    /*
     * Not dealing with fragments right now - just return ICMP msg and drop
     */

    free_p (bp);

    return -1;
}

