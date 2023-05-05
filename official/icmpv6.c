/*
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 *
 * 01Feb2023, Maiko, first day off attempting icmp functions for IPV6
 * 
 */
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "ip.h"
#include "icmp.h"
#include "netuser.h"

#include "ipv6.h"
#include "icmpv6.h"	/* 03Feb2023, Maiko */

#include "enet.h"	/* 18Mar2023, Maiko */

extern unsigned char myipv6addr[16];	/* 14Mar2023, Maiko */

extern unsigned char mylinklocal[16];	/* 21Mar2023, Maiko */

#ifdef	NOT_YET
 
struct mib_entry Icmp_mib[] = {
    { "",                     { 0 } },
    { "icmpInMsgs",           { 0 } },
    { "icmpInErrors",         { 0 } },
    { "icmpInDestUnreachs",   { 0 } },
    { "icmpInTimeExcds",      { 0 } },
    { "icmpInParmProbs",      { 0 } },
    { "icmpInSrcQuenchs",     { 0 } },
    { "icmpInRedirects",      { 0 } },
    { "icmpInEchos",          { 0 } },
    { "icmpInEchoReps",       { 0 } },
    { "icmpInTimestamps",     { 0 } },
    { "icmpInTimestampReps",  { 0 } },
    { "icmpInAddrMasks",      { 0 } },
    { "icmpInAddrMaskReps",   { 0 } },
    { "icmpOutMsgs",          { 0 } },
    { "icmpOutErrors",        { 0 } },
    { "icmpOutDestUnreachs",  { 0 } },
    { "icmpOutTimeExcds",     { 0 } },
    { "icmpOutParmProbs",     { 0 } },
    { "icmpOutSrcQuenchs",    { 0 } },
    { "icmpOutRedirects",     { 0 } },
    { "icmpOutEchos",         { 0 } },
    { "icmpOutEchoReps",      { 0 } },
    { "icmpOutTimestamps",    { 0 } },
    { "icmpOutTimestampReps", { 0 } },
    { "icmpOutAddrMasks",     { 0 } },
    { "icmpOutAddrMaskReps",  { 0 } }
};

#endif

#ifdef	SERVES_NO_PURPOSE_RIGHT_NOW

/*
 * 21Mar2023, Maiko (VE4KLM), test function to send out RS message
 *
 * REF -> https://www.rfc-editor.org/rfc/rfc4861.html#section-4.1
 *
 * TOOL -> wget "https://github.com/pbiering/
 *          ipv6calc/archive/refs/heads/master.zip"
 *
 * 22Mar2023, Maiko, This should not even go on the hopper !!!
 *  send it out direct, what a damn mess, how to do this all :(
 */
int icmpv6_router_solicitation (int argc, char **argv, void *p)
{
	struct icmpv6 icmpv6;
	struct ipv6 ipv6;
	struct mbuf *bp;

	static unsigned char rs_src[16] = {
		0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x56, 0x4e, 0x45, 0xff, 0xfe, 0x00, 0x00, 0x00
	};

	static unsigned char rs_dst[16] = {
		0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
	};

	/*
	 * ETH (done in ipv6hdr.c for now)
	 *
	 *   src = 54:4e:45:00:00:00
	 *   dst = 33:33:00:00:00:02
	 */

	/*
	 * IPV6
	 *
	 *   (build eui-64) 54:4e:45 + FF:FE + 00:00:00, then invert 7th bit
	 *
	 *   https://unix.stackexchange.com/questions/136674/how-to-use-shell-to-derive-an-ipv6-address-from-a-mac-address
	 *
	 *   ipv6calc --action prefixmac2ipv6 --in prefix+mac --out ipv6addr fe80:: 54:4e:45:00:00:00
	 *
	 *   src = fe80:0000:0000:0000:564e:45ff:fe00:0000
	 *   dst = ff02:0000:0000:0000:0000:0000:0000:0002
	 *
	 */
	copyipv6addr (rs_src, ipv6.source);
	copyipv6addr (rs_dst, ipv6.dest);

	/* 
	 * 22Mar2023, Maiko, Make damn sure src, dest, and next_header are set,
	 * since icmpv6 checksum uses them, had not set this one before hand.
	 */
	ipv6.next_header = ICMPV6_PTCL;

	icmpv6.type = ICMPV6_ROUTER_SOLICITATION;
	icmpv6.code = 0;
	icmpv6.args.rd.type = 1;
	icmpv6.args.rd.len = 1;
	memcpy (icmpv6.args.rd.mac, myipv6iface()->hwaddr, EADDR_LEN);

	if ((bp = htonicmpv6 (&ipv6, &icmpv6, NULLBUF)) == NULLBUF)
	{
		log (-1, "htonicmpv6 RS out failed");
		free_p (bp);
		return -1;
	}

	ipv6_send (ipv6.source, ipv6.dest, ICMPV6_PTCL, ipv6.traffic_class, 255, bp, len_p (bp), 0, 0);

	return 0;
}

#endif		// SERVES_NO_PURPOSE_RIGHT_NOW

/*
 * Process an incoming ICMPV6 packet
 *
 * 23Feb2023, Maiko (VE4KLM), forgot a few types, no processing
 * of offending packets was being done either, get it done now.
 */

void icmpv6_input(iface,ipv6,bp,rxbroadcast)
struct iface *iface;    /* Incoming interface (ignored) */
struct ipv6 *ipv6;          /* Pointer to decoded IP header structure */
struct mbuf *bp;        /* Pointer to ICMP message */
int rxbroadcast;
{
    struct icmpv6link *ippv6;	/* 23Feb2023, Maiko, Forgot about this one */
    struct icmpv6 icmpv6;	/* ICMP header */
    struct mbuf *tbp;

    unsigned char type;		/* Type of ICMP message */

    int16 length;

    struct ipv6 oipv6;		/* Offending IPV6 datagram header */
    int ok_trace;
    char temp[40];
  
    // icmpInMsgs++;

#ifdef	NOT_FOR_IPV6_DO_NOT_IGNORE

/*
 * 13Mar2023, Maiko (VE4KLM), do NOT ignore broadcast for IPV6 :]
 *  (this is where the NDP stuff comes in via TAP (ethernet) driver)
 */

    if(rxbroadcast){
        /* Broadcast ICMP packets are to be IGNORED !! */
        // icmpInErrors++;
        free_p(bp);
        return;
    }
#endif

	/* 15Feb2023, Maiko, Oops, why am I subtracing IPV6LEN ? */
    length = ipv6->payload_len;

#ifdef	NOT_YET
    if(cksum(NULLHEADER,bp,length) != 0){
        /* Bad ICMP checksum; discard */
        icmpInErrors++;
        free_p(bp);
        return;
    }
#endif

  /* 01Feb2023, Maiko, need new ntohicmpv6 functions (not done yet) */
    if (ntohicmpv6 (&icmpv6, &bp) == 1)
	{
		log (-1, "ntohicmpv6 - bad header");
		free_p(bp);
		return;
	}
  
    /* Process the message. Some messages are passed up to the protocol
     * module for handling, others are handled here.
     */
    type = icmpv6.type;

/*
 * 13Mar2023, Maiko (VE4KLM), RFC4443 states, if type = 255, then
 * use what is in the code field to designate the type, and possibly
 * add more details in the payload, something like that :
 *
 * https://www.rfc-editor.org/rfc/rfc4443.html
 *
 * I thought that is how NDP would come in, but no, my mistake, it comes
 * in via code 135, but leave this hear, just incase we get something, it
 * at least screens it out then and does not cause immediate problems.
 */
	if (type == 255)
	{
		log (-1, "special icmpv6 processing");
	}
	else
  
    switch (type)		/* 04Feb2023, Maiko, cheating, using abs to fix a coding issue,
				 * just want to send a response to a ping right now, nothing
				 * else, getting an icmp ping IPV6 to work wud be great !
				 */
    {

	/*
	 * 21Mar2023, Maiko (VE4KLM), Router Solicitation and Advertisement
	 *  (there is no way any of this will work without Router stuff)
	 *
	 * AND I have this backwards, JNOS needs to send out
	 * an RS to get RA from linux system !!!
	 */
		case ICMPV6_ROUTER_SOLICITATION:

	/* 
	 * 22Mar2023, Maiko, Make damn sure src, dest, and next_header are set,
	 * since icmpv6 checksum uses them, had not set this one before hand.
	 */
	ipv6->next_header = ICMPV6_PTCL;

            icmpv6.type = ICMPV6_ROUTER_ADVERTISEMENT;
			icmpv6.args.rd.router_lifetime = 0xff00;
			icmpv6.args.rd.type = 2;
			memcpy (icmpv6.args.rd.mac, iface->hwaddr, EADDR_LEN);

            if ((tbp = htonicmpv6 (ipv6, &icmpv6, bp)) == NULLBUF)
			{
				log (-1, "reply failed");
               	free_p (bp);
               	return;
            }

			length = len_p(tbp);

	/* 21Mar2023, Maiko, new link-local address on JNOS side, part of IPV6 ! */	
            ipv6_send (mylinklocal /*ipv6addr*/, ipv6->source, ICMPV6_PTCL, ipv6->traffic_class, 255, tbp, length, 0, 0);

			break;

	/*
	 * 13Mar2023, Maiko (VE4KLM), NDP support (comes in via TAP iface)
	 * REF -> https://www.rfc-editor.org/rfc/rfc4861.html#section-4.3
	 *
 	 * In my trails, the 16 byte target is followed by 'options' :
	 *
	 *   01 01 f2 67 26 11 d9 fd
	 *
 	 * Not sure what 01s are, but last 6 are MAC address of source LL iface
	 */
		case ICMPV6_NEIGHBOR_SOLICITATION:

	/* 
	 * 22Mar2023, Maiko, Make damn sure src, dest, and next_header are set,
	 * since icmpv6 checksum uses them, had not set this one before hand.
	 */
	ipv6->next_header = ICMPV6_PTCL;

            icmpv6.type = ICMPV6_NEIGHBOR_ADVERTISEMENT;
			icmpv6.args.nd.reserved_hi = 0x0600;	/* set 'S' flag */
		/* 19Mar2023, Maiko, apparently override needs to be set as well, so change 0x04 to 0x06 */
			icmpv6.args.nd.type = 2;	/* we advertise now */

			memcpy (icmpv6.args.nd.mac, iface->hwaddr, EADDR_LEN); /* 18Mar2023, Maiko */

	/* 23Mar2023, Maiko, Took me long enough to figure it out, checksum is wrong because
	 * the ipv6 src and dest do NOT match what I am actually sending out - bingo !!!
	 */
			copyipv6addr (ipv6->source, ipv6->dest);
			copyipv6addr (myipv6addr, ipv6->source);

            if ((tbp = htonicmpv6 (ipv6, &icmpv6, bp)) == NULLBUF)
			{
				log (-1, "reply failed");
               	free_p (bp);
               	return;
            }
		/*
		 * 20Mar2023, Maiko, MAKE SURE length is correct, the size of
		 * the response is NOT always same as request (sure, for echo
		 * and echo reply it was working, but not network discovery !
		 *  (do it for all cases just to be safe and sure)
		 *
		 * This did NOT solve my checksum error, and actually length
		 * of request did match that of reply, but still leave here.
		 */
			length = len_p(tbp);
	
		/* 23Mar2023, Maiko, Alluding to my checksum whoas, use the reorganized
			ipv6 structure - done before the htonicmpv() call !!!

            ipv6_send (myipv6addr, ipv6->source, ICMPV6_PTCL, ipv6->traffic_class, 255, tbp, length, 0, 0);
 		*/
            ipv6_send (ipv6->source, ipv6->dest, ICMPV6_PTCL, ipv6->traffic_class, 255, tbp, length, 0, 0);

			break;

        case ICMP_TIME_EXCEED:  /* Time-to-live Exceeded */
        case ICMP_DEST_UNREACH: /* Destination Unreachable */
        case ICMP_QUENCH:       /* Source Quench */
        switch(uchar(type)){
            case ICMP_TIME_EXCEED:  /* Time-to-live Exceeded */
                icmpInTimeExcds++;
                break;
            case ICMP_DEST_UNREACH: /* Destination Unreachable */
                icmpInDestUnreachs++;
                break;
            case ICMP_QUENCH:       /* Source Quench */
                icmpInSrcQuenchs++;
                break;
        }
            ntohipv6(&oipv6,&bp);       /* Extract offending IPV6 header */

            if(Icmp_trace) {
                switch(uchar(type)) {
                    case ICMP_TIME_EXCEED:
                        ok_trace=1;
                        sprintf(temp,"%s",
                        smsg(Exceed,NEXCEED,uchar(icmpv6.code)));
                        break;
                    case ICMP_DEST_UNREACH:
                        ok_trace=1;
                        sprintf(temp,"%s",
                        smsg(Unreach,NUNREACH,uchar(icmpv6.code)));
                        break;
                    default:
                        ok_trace=0;
                        sprintf(temp,"%u",uchar(icmpv6.code));
                        break;
                }

			/* 23Feb2023, Maiko, Forgot a few things, including this section */
                if ((Icmp_trace==1) || (Icmp_trace==2 && ok_trace))
				{
                    printf("ICMP from %s:", human_readable_ipv6 (ipv6->source));
                    printf(" dest %s %s ", human_readable_ipv6 (oipv6.dest), smsg(Icmptypes,ICMP_TYPES,uchar(type)));
                    printf(" %s\n",temp);
                }
            }

	/*
	 * 01Feb2023, Maiko, Created Icmpv6link in ipv6link.c and ipv6.h, but
	 * might be pointless in their current form, which is 32 bit addressing
	 * so this will take a bit of work to figure out ? Stopping here !
	 *
	 * 23Feb2023, Maiko, Now implementing this, probably important, noticed
	 * some lagging, and no data flow - which Bob reported in his testing.
	 */
            for(ippv6 = Icmpv6link;ippv6->funct != NULL;ippv6++)
                if(ippv6->proto == oipv6.next_header)
                    break;
            if(ippv6->funct != NULL){
                (*ippv6->funct)(ipv6->source, oipv6.source, oipv6.dest, icmpv6.type, icmpv6.code, &bp);
            }
            break;

        case ICMPV6_ECHO:         /* Echo Request */
        /* Change type to ECHO_REPLY, recompute checksum,
         * and return datagram.
	 * 07Feb2023, Maiko, Need to pass ipv6 to htonicmpv6 for checksum !
         */
            icmpInEchos++;
            icmpv6.type = ICMPV6_ECHO_REPLY;
            if ((tbp = htonicmpv6 (ipv6, &icmpv6, bp)) == NULLBUF)
			{
				log (-1, "reply failed");
                free_p (bp);
                return;
            }
            icmpOutEchoReps++;
	/* 03Feb2023, Maiko (VE4KLM), the big moment - our first IPV6 response ? */
	/* 07Feb2023, Maiko, I had ttl set to 0, setting to 64, BUT, why did it not grab MAXTTL in _send() ? */
            ipv6_send (ipv6->dest, ipv6->source, ICMPV6_PTCL, ipv6->traffic_class, 64, tbp, length, 0, 0);
            return;

        case ICMP_REDIRECT:     /* Redirect */
            icmpInRedirects++;
            break;
        case ICMP_PARAM_PROB:   /* Parameter Problem */
            icmpInParmProbs++;
            break;

        case ICMPV6_ECHO_REPLY:   /* Echo Reply */
            icmpInEchoReps++;
	/* 06Feb2023, Maiko (VE4KLM), actually this is just a notification function
		deal with it later ...
            echo_proc(ip->source,ip->dest,&icmp,bp);
	*/
            bp = NULLBUF;   /* so it won't get freed */
            break;

        case ICMP_TIMESTAMP:    /* Timestamp */
            icmpInTimestamps++;
            break;
        case ICMP_TIME_REPLY:   /* Timestamp Reply */
            icmpInTimestampReps++;
            break;
        case ICMP_INFO_RQST:    /* Information Request */
            break;
        case ICMP_INFO_REPLY:   /* Information Reply */
            break;
    }
    free_p(bp);
}

/*
 * 30Mar2023, Maiko (VE4KLM), I've been ducking this for too long,
 * we seriously need to get icmpv6_output() function working here.
 *
 * Return an ICMP response to the sender of a datagram.
 * Unlike most routines, the callER frees the mbuf.
 */
int icmpv6_output (ipv6, data, type, code, v6args)
	struct ipv6 *ipv6;          /* Header of offending datagram */
	struct mbuf *data;      /* Data portion of datagram */
	char type, code;         /* Codes to send */
	union icmpv6_args *v6args;
{
    struct mbuf *bp = NULLBUF;
    struct icmpv6 icmpv6;   /* ICMP protocol header */
    int16 dlen;             /* Length of data portion of offending pkt */
    int16 length;           /* Total length of reply */
 /* 
    if(ip == NULLIPV6)
        return -1;
*/

    if(uchar(ipv6->next_header) == ICMP_PTCL)
	{
        /* Peek at type field of ICMP header to see if it's safe to
         * return an ICMP message
         */
        switch(uchar(data->data[0]))
		{
            case ICMPV6_ECHO_REPLY:
            case ICMPV6_ECHO:
#ifdef NOT_YET
            case ICMP_TIMESTAMP:
            case ICMP_TIME_REPLY:
            case ICMP_INFO_RQST:
            case ICMP_INFO_REPLY:
#endif
                break;  /* These are all safe */
            default:
            /* Never send an ICMP error message about another
             * ICMP error message!
             */
                return -1;
        }
    }

    /* Compute amount of original datagram to return.
     * We return the original IP header, and up to 8 bytes past that.
     */
    dlen = min(8,len_p (data));
    length = dlen + ICMPLEN + IPV6LEN;	/* no option length */

    /* Take excerpt from data portion */
    if (data != NULLBUF && dup_p (&bp, data, 0, dlen) == 0)
        return -1;      /* The caller will free data */
  
    /* Recreate and tack on offending IP header */
    if ((data = htonipv6 (ipv6, bp)) == NULLBUF)
	{
        free_p(bp);
        icmpOutErrors++;
        return -1;
    }
    icmpv6.type = type;
    icmpv6.code = code;
    //icmp.args.unused = 0;

    switch (uchar(icmpv6.type))
	{
#ifdef	NOT_YET
        case ICMP_PARAM_PROB:
            icmpOutParmProbs++;
            icmp.args.pointer = args->pointer;
            break;
        case ICMP_REDIRECT:
            icmpOutRedirects++;
            icmp.args.address = args->address;
            break;
#endif
        case ICMPV6_ECHO:
            icmpOutEchos++;
            break;
        case ICMPV6_ECHO_REPLY:
            icmpOutEchoReps++;
            break;
#ifdef	NOT_YET
        case ICMP_INFO_RQST:
            break;
        case ICMP_INFO_REPLY:
            break;
        case ICMP_TIMESTAMP:
            icmpOutTimestamps++;
            break;
        case ICMP_TIME_REPLY:
            icmpOutTimestampReps++;
            icmp.args.echo.id = args->echo.id;
            icmp.args.echo.seq = args->echo.seq;
            break;
        case ICMP_ADDR_MASK:
            icmpOutAddrMasks++;
            break;
        case ICMP_ADDR_MASK_REPLY:
            icmpOutAddrMaskReps++;
            break;
#endif
        case ICMP_DEST_UNREACH:
#ifdef WE_DONT_FRAGMENT
            if(icmp.code == ICMP_FRAG_NEEDED)
                icmp.args.mtu = args->mtu;
#endif
            icmpOutDestUnreachs++;
            break;
        case ICMP_TIME_EXCEED:
            icmpOutTimeExcds++;
            break;
        case ICMP_QUENCH:
            icmpOutSrcQuenchs++;
            break;
    }
  
    icmpOutMsgs++;

    /* Now stick on the ICMP header */
    if ((bp = htonicmpv6 (ipv6, &icmpv6, data)) == NULLBUF)
	{
        free_p(data);
        return -1;
    }

	/* this is very important for proper checksum calculations */
	copyipv6addr (ipv6->source, ipv6->dest);
	copyipv6addr (myipv6addr, ipv6->source);

    return ipv6_send (ipv6->source, ipv6->dest, ICMPV6_PTCL, 0, 0, bp, length, 0, 0);
}

