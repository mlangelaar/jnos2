/*
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 */
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "ip.h"
#include "icmp.h"

#include "ipv6.h"	/* 02Feb2023, Maiko (VE4KLM) */
#include "icmpv6.h"	/* 23Feb2023, Maiko */

/*
 * Generate ICMP V6 header in network byte order, and a light
 * bulb moment, pushdown only what you pulled up, that leaves
 * the optional data still sitting in the mbuf to send back,
 * brilliant way of doing things - took me this long to see
 * it, how funny is that :]  06Feb2023, Maiko
 *
 * 07Feb2023, Maiko (VE4KLM), in IPV6 we need a few fields from the
 * actual IP header (some refer to it as pseudo-header, but I just
 * pass the actual ipv6 structure instead, works just as well).
 */ 
struct mbuf* htonicmpv6 (struct ipv6 *ipv6, struct icmpv6 *icmpv6, struct mbuf *data)
{
    struct mbuf *bp;
    unsigned char *cp;		/* 04Feb2023, Maiko, must be unsigned */
    int16 checksum, len;

	/* 19Mar2023, Maiko, I REALLY do NOT like doing it this way, sigh */ 
	if (icmpv6->type == ICMPV6_NEIGHBOR_ADVERTISEMENT)
    	bp = pushdown(data,ICMPLEN + 24);
	/* 21Mar2023, Maiko, I REALLY do NOT like doing it this way, sigh */ 
	else if (icmpv6->type == ICMPV6_ROUTER_ADVERTISEMENT)
    	bp = pushdown(data,ICMPLEN + 16);
	/* 22Mar2023, Maiko, I REALLY do NOT like doing it this way, sigh */ 
	else if (icmpv6->type == ICMPV6_ROUTER_SOLICITATION)
    	bp = pushdown(data,ICMPLEN + 8);
	else
    	bp = pushdown(data,ICMPLEN);

    cp = (unsigned char*)(bp->data);
  
    *cp++ = icmpv6->type;
    *cp++ = icmpv6->code;

/* 12Feb2023, Maiko, New put16ub() function in misc.c */

    cp = put16ub (cp,0);       /* Clear checksum */

    switch(icmpv6->type)
	{
#ifdef NOT_YET
        case ICMP_DEST_UNREACH:
            if(icmp->code == ICMP_FRAG_NEEDED){
            /* Deering/Mogul max MTU indication */
                cp = put16ub(cp,0);
                cp = put16ub(cp,icmp->args.mtu);
            } else
                cp = put32ub(cp,0L);
            break;
        case ICMP_PARAM_PROB:
            *cp++ = icmp->args.pointer;
            *cp++ = 0;
            cp = put16ub(cp,0);
            break;
        case ICMP_REDIRECT:
            cp = put32ub(cp,icmp->args.address);
            break;
#endif
	/* 07Feb2023, Maiko, Oops, should use the IPV6 counterparts :] */
        case ICMPV6_ECHO:
        case ICMPV6_ECHO_REPLY:
#ifdef NOT_YET
        case ICMP_TIMESTAMP:
        case ICMP_TIME_REPLY:
        case ICMP_INFO_RQST:
        case ICMP_INFO_REPLY:
#endif
            cp = put16ub(cp,icmpv6->args.echo.id);
            cp = put16ub(cp,icmpv6->args.echo.seq);
            break;

		/* 21Mar2023, Maiko, I have the feeling that none of this is
		 * working because JNOS is not advertising itself as a router,
		 * which means we need to respond to Router solicitations ?
		 *
		 *   https://www.rfc-editor.org/rfc/rfc4861
		 */

		case ICMPV6_ROUTER_ADVERTISEMENT:

			/* carefull here, these are from the first pushdown */
			*cp++ = icmpv6->args.rd.cur_hop_limit;
			*cp++ = icmpv6->args.rd.reserved;
			cp = put16ub (cp, icmpv6->args.rd.router_lifetime);

			cp = put32ub (cp, icmpv6->args.rd.reachable_time);
			cp = put32ub (cp, icmpv6->args.rd.retrans_time);
			*cp++ = icmpv6->args.rd.type;
			*cp++ = icmpv6->args.rd.len;
			memcpy (cp, icmpv6->args.rd.mac, 6);
			cp += 6;
			break;

		/* 21Mar2023, Maiko, I think I need to send one out
		 * when JNOS comes up and send it out regularily ?
		 */
		case ICMPV6_ROUTER_SOLICITATION:
            		cp = put32ub(cp,0L);
			*cp++ = icmpv6->args.rd.type;
			*cp++ = icmpv6->args.rd.len;
			memcpy (cp, icmpv6->args.rd.mac, 6);
			cp += 6;
			break;

		/*
		 * 15Mar2023, Maiko (VE4KLM), Time to add the response :]
		 *
		 * Might as well add the SOLICITATION, won't be ready to use
		 * it for a while though, focusing on the incoming first ...
		 */
		case ICMPV6_NEIGHBOR_ADVERTISEMENT:

			/* carefull here, these are from the first pushdown */
			cp = put16ub (cp, icmpv6->args.nd.reserved_hi);
			cp = put16ub (cp, icmpv6->args.nd.reserved_lo);
#ifdef	DONT_COMPILE
			/*
		 	 * carefull here, pushdown 24 minimum (is this allowed?)
			 * 19Mar2023, Maiko, I don't think this works, so note
			 * the change to the pushdown call done 'above', sigh.
		 	 */
			log (-1, "is this allowed");
			bp = pushdown (bp, 24);
    		cp = (unsigned char*)(bp->data);
#endif
			copyipv6addr (icmpv6->args.nd.target, cp);
			cp += 16;
			*cp++ = icmpv6->args.nd.type;
			*cp++ = icmpv6->args.nd.len;
			memcpy (cp, icmpv6->args.nd.mac, 6);
			cp += 6;
			break;

		case ICMPV6_NEIGHBOR_SOLICITATION:
			log (-1, "not supported, still need to do this");
            		cp = put32ub(cp,0L);
			break;
        default:
            cp = put32ub(cp,0L);
            break;

	/* 12Feb2023, Maiko, New put32ub() function in misc.c */
    }

 /* 07Feb2023, Maiko (VE4KLM), Now incorporating new cksum function */

    /* Compute checksum, and stash result */

    checksum = icmpv6cksum (ipv6, bp, len = len_p (bp));

	// 08Feb2023, Maiko (VE4KLM), huge milestone ping and reply works !!!! */
    // log (-1, "checksum %x len %d", checksum, len);

    cp = (unsigned char*)(&bp->data[2]);

    cp = put16ub(cp,checksum);
  
    return bp;
}

/*
 * Pull off ICMP V6 header
 *
 * Reference -> https://www.rfc-editor.org/rfc/rfc4443.html
 *
 */
int ntohicmpv6 (struct icmpv6 *icmpv6, struct mbuf **bpp)
{
	/*
	 * 04Feb2023, Maiko, This needs to be unsigned, use of char
	 * with IPV6 data is problematic.
	 * 15Mar2023, Maiko, Added ndpbuf for Network Discovery
	 */
	unsigned char icmpbuf[ICMPLEN], ndpbuf[24];

	int16 useless;	/* 21Mar2023, Maiko */
  
	if (pullup (bpp, (char*)icmpbuf, ICMPLEN) != ICMPLEN)
	{
		log (-1, "ntohicmpv6 - bad header");
		return -1;
	}
 
    icmpv6->type = icmpbuf[0];
    icmpv6->code = icmpbuf[1];

    icmpv6->checksum = get16ub (&icmpbuf[2]);

    switch (icmpv6->type)
	{
#ifdef	NOT_YET
        case ICMP_DEST_UNREACH:
        /* Retrieve Deering/Mogul MTU value */
            if(icmp->code == ICMP_FRAG_NEEDED)
                icmp->args.mtu = get16ub(&icmpbuf[6]);
            break;
        case ICMP_PARAM_PROB:
            icmp->args.pointer = icmpbuf[4];
            break;
        case ICMP_REDIRECT:
            icmp->args.address = get32ub(&icmpbuf[4]);
            break;
#endif
	/* 07Feb2023, Maiko, Oops, should use the IPV6 counterparts :] */
        case ICMPV6_ECHO:
        case ICMPV6_ECHO_REPLY:

#ifdef NOT_YET
        case ICMP_TIMESTAMP:
        case ICMP_TIME_REPLY:
        case ICMP_INFO_RQST:
        case ICMP_INFO_REPLY:
#endif
            icmpv6->args.echo.id = get16ub(&icmpbuf[4]);
            icmpv6->args.echo.seq = get16ub(&icmpbuf[6]);
            break;

		/*
		 * 21Mar2023, Maiko, I have the feeling that none of this is
		 * working because JNOS is not advertising itself as a router,
		 * which means we need to respond to Router solicitations ?
		 *
		 *   https://www.rfc-editor.org/rfc/rfc4861
		 */

		case ICMPV6_ROUTER_ADVERTISEMENT:

			/* carefull here, these are from the first pushdown */
			icmpv6->args.rd.cur_hop_limit = icmpbuf[4];
			icmpv6->args.rd.reserved = icmpbuf[5];
			icmpv6->args.rd.router_lifetime = get16ub (&icmpbuf[6]);

			/* carefull here, pullup the expected 16 minimum */
			if (pullup (bpp, (char*)ndpbuf, 16) != 16)
			{
				log (-1, "router discovery truncated data");
				return -1;
			}
			icmpv6->args.rd.reachable_time = get32ub (ndpbuf);
			icmpv6->args.rd.retrans_time = get32ub (&ndpbuf[4]);
			icmpv6->args.rd.type = ndpbuf[8];
			icmpv6->args.rd.len = ndpbuf[9];
			memcpy (icmpv6->args.rd.mac, &ndpbuf[10], 6);
			break;

		case ICMPV6_ROUTER_SOLICITATION:

			/* carefull here, these are from the first pullup */
			useless = get16ub (&icmpbuf[4]);
			useless = get16ub (&icmpbuf[6]);

			/* carefull here, pullup the expected 8 minimum */
			if (pullup (bpp, (char*)ndpbuf, 8) != 8)
			{
				log (-1, "router discovery truncated data");
				return -1;
			}
			icmpv6->args.rd.type = ndpbuf[0];
			icmpv6->args.rd.len = ndpbuf[1];
			memcpy (icmpv6->args.rd.mac, &ndpbuf[2], 6);
			break;

		/*
		 * 15Mar2023, Maiko (VE4KLM), Should extract everything, doing
		 * pullup calls in icmpv6 is not working out, should put it in
		 * here instead - requires a bit of focus (not my strength) :/
		 *
		 * Might as well ad the ADVERTISEMENT, won't be ready to use
		 * it for a while though, focusing on the incoming first ...
		 */
		case ICMPV6_NEIGHBOR_SOLICITATION:
	/* trace needs this ADVERTISEMENT to be supported 19Mar2023, Maiko */
		case ICMPV6_NEIGHBOR_ADVERTISEMENT:

			/* carefull here, these are from the first pullup */
			icmpv6->args.nd.reserved_hi = get16ub (&icmpbuf[4]);
			icmpv6->args.nd.reserved_lo = get16ub (&icmpbuf[6]);

			/* carefull here, pullup the expected 24 minimum */
			if (pullup (bpp, (char*)ndpbuf, 24) != 24)
			{
				log (-1, "neighbor discovery truncated data");
				return -1;
			}
			copyipv6addr (ndpbuf, icmpv6->args.nd.target);
			icmpv6->args.nd.type = ndpbuf[16];
			icmpv6->args.nd.len = ndpbuf[17];
			memcpy (icmpv6->args.nd.mac, &ndpbuf[18], 6);
			break;
    }

    return 0;
}

