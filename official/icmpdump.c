/*
 * Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J
 *
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 */
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "netuser.h"
#include "icmp.h"
#include "trace.h"
#include "ip.h"
#include "socket.h"  

#ifdef	IPV6
#include "ipv6.h"
#include "icmpv6.h"
#endif

/*
 * 17Feb2023, Maiko (VE4KLM), Break out common code, note icmp is now a pointer
 */
void icmpdump_break_out (int s, struct icmp *icmp, struct mbuf **bpp, int check, int16 csum)
{
#ifdef MONITOR
    if (Trace_compact_header)
        usprintf(s, "%s", smsg(Icmptypes,ICMP_TYPES,uchar(icmp->type)));
    else
#endif
        usprintf(s,"ICMP: type %s",smsg(Icmptypes,ICMP_TYPES,uchar(icmp->type)));
  
    switch(uchar(icmp->type)){
        case ICMP_DEST_UNREACH:
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, "%s", smsg(Unreach,NUNREACH,uchar(icmp->code)));
            else
#endif
                usprintf(s," code %s",smsg(Unreach,NUNREACH,uchar(icmp->code)));
            break;
        case ICMP_REDIRECT:
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, "%s", smsg(Redirect,NREDIRECT,uchar(icmp->code)));
            else
#endif
                usprintf(s," code %s",smsg(Redirect,NREDIRECT,uchar(icmp->code)));
            usprintf(s," new gateway %s",inet_ntoa(icmp->args.address));
            break;
        case ICMP_TIME_EXCEED:
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, "%s", smsg(Exceed,NEXCEED,uchar(icmp->code)));
            else
#endif
                usprintf(s," code %s",smsg(Exceed,NEXCEED,uchar(icmp->code)));
            break;
        case ICMP_PARAM_PROB:
            usprintf(s," pointer %u",icmp->args.pointer);
            break;
        case ICMP_ECHO:
        case ICMP_ECHO_REPLY:
        case ICMP_INFO_RQST:
        case ICMP_INFO_REPLY:
        case ICMP_TIMESTAMP:
        case ICMP_TIME_REPLY:
#ifdef MONITOR
            if (!Trace_compact_header)
#endif
                usprintf(s," id %u seq %u",icmp->args.echo.id,icmp->args.echo.seq);
            break;


    }
    if(check && csum != 0){
        usprintf(s," CHECKSUM ERROR (%u)",csum);
    }
    usprintf(s,"\n");
    /* Dump the offending IP header, if any */
    switch(icmp->type){
        case ICMP_DEST_UNREACH:
        case ICMP_TIME_EXCEED:
        case ICMP_PARAM_PROB:
        case ICMP_QUENCH:
        case ICMP_REDIRECT:
            usprintf(s,"Returned ");
            ip_dump(s,bpp,0);
    }
}

#ifdef	IPV6

/* 16Mar2023, Maiko, quick and dirty, smaller versions of fmtline, more usefull */

static void ctohex (char *buf, int16 c)
{
    static char hex[] = "0123456789abcdef";

    *buf++ = hex[hinibble(c)];

    *buf = hex[lonibble(c)];
}

static char *showlinklayer (char *addr, int len)
{
	static char line[20];

	char c, *aptr = line;
  
	while (len-- != 0)
	{
		c = *addr++;
		ctohex (aptr, (int16)uchar(c));
		aptr += 2;
		if (len)
			*aptr++ = ':';
	}

	*aptr = '\0';

	return line;
}

/* 16Mar2023, Maiko */
static char* NDstring[2] = { "Solicitation", "Advertisment" };

/*
 * 17Feb2023, Maiko, Get proper ICMP tracing in place, now that things moving
 * forward with IPV6, although this one is a bit more complicated - might just
 * write a specific and minimal dump for now, since haven't completed the full
 * types & codes for icmp V6 yet, although I could still breakout a function,
 * and 'map' to a dummy 'struct icmp' ? Yes, do that, makes sense to use as
 * much of the existing dump code as possible, for full trace information.
 */
void icmp_dumpv6 (int s, struct mbuf **bpp, struct ipv6 *ipv6, int check)
{
	struct icmpv6 icmpv6;
	struct icmp dummy;
	int16 csum;

	int breakout = 1;	/* 16Mar2023, Maiko */
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;

    csum = icmpv6cksum (ipv6, *bpp, len_p(*bpp));

	ntohicmpv6 (&icmpv6, bpp);

	/* this allows me to reuse existing ICMP trace code */
	switch (icmpv6.type)
	{
		case ICMPV6_ECHO:
			dummy.type = ICMP_ECHO;
			break;
		case ICMPV6_ECHO_REPLY:
			dummy.type = ICMP_ECHO_REPLY;
			break;

		/* 21Mar2023, Maiko, Adding Router side for ND(P) */
		case ICMPV6_ROUTER_SOLICITATION:

       		usprintf (s,"ICMPV6: type Router %s\n",
				NDstring[icmpv6.type-ICMPV6_ROUTER_SOLICITATION]);

			usprintf (s, " link-layer [%d] [%d] address %s\n",
 				icmpv6.args.rd.type, icmpv6.args.rd.len,
					showlinklayer (icmpv6.args.rd.mac, 6));

			breakout = 0;
			break;

		case ICMPV6_ROUTER_ADVERTISEMENT:

       		usprintf (s,"ICMPV6: type Router %s\n",
				NDstring[icmpv6.type-ICMPV6_ROUTER_SOLICITATION]);

			breakout = 0;
			break;

		/*
		 * 15Mar2023, Maiko (VE4KLM), should probably add the ND(P)
		 * cases - Neighbor Solicitation and Advertisment (ND), but
		 * these are new and specific to ICMPV6, so they can not go
		 * into the breakout function & show more info, 16Mar2023.
		 */
		case ICMPV6_NEIGHBOR_SOLICITATION:
		case ICMPV6_NEIGHBOR_ADVERTISEMENT:

       		usprintf (s,"ICMPV6: type Neighbor %s\n target %s\n",
				NDstring[icmpv6.type-ICMPV6_NEIGHBOR_SOLICITATION],
				 human_readable_ipv6 (icmpv6.args.nd.target));

			usprintf (s, " link-layer [%d] [%d] address %s\n",
 				icmpv6.args.nd.type, icmpv6.args.nd.len,
					showlinklayer (icmpv6.args.nd.mac, 6));

			breakout = 0;
			break;
	}

	if (breakout)
	{
		dummy.code = icmpv6.code;
		dummy.args.echo.id = icmpv6.args.echo.id;
		dummy.args.echo.seq = icmpv6.args.echo.seq;

		icmpdump_break_out (s, &dummy, bpp, check, csum);
	}
}

#endif

/* Dump an ICMP header */
void
icmp_dump(s,bpp,source,dest,check)
int s;
struct mbuf **bpp;
int32 source,dest;
int check;      /* If 0, bypass checksum verify */
{
    struct icmp icmp;
    int16 csum;
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;
    csum = cksum(NULLHEADER,*bpp,len_p(*bpp));
  
    ntohicmp(&icmp,bpp);

	icmpdump_break_out (s, &icmp, bpp, check, csum);
}

