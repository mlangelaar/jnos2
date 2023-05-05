/*
 * IP header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Mods by PA0GRI
 *
 * Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J
 *
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 */
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "iface.h"
#include "ip.h"
#include "trace.h"
#include "netuser.h"
#include "socket.h"  

#include "ipv6.h"	/* 02Feb2023, Maiko (VE4KLM) */

void
ip_dump(s,bpp,check)
int s;
struct mbuf **bpp;
int check;
{
    struct ip ip;
    int16 ip_len;
    int16 length;
    int16 csum;

#ifdef	IPV6
	struct ipv6 ipv6;	/* 02Feb2023, Maiko (VE4KLM) */
	char version;

	/* 17Feb2023, Forgot a couple of prototypes, very important */
	extern void tcp_dumpv6 (int, struct mbuf**, struct ipv6*, int);
	extern void icmp_dumpv6 (int, struct mbuf**, struct ipv6*, int);
#endif

    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;
  
#ifdef MONITOR
    if (!Trace_compact_header)
#endif

#ifdef IPV6
	/* 02Feb2023, Maiko - get IP dump working with IPV6 */
	version = (*bpp)->data[0] >> 4;
	if (version == IPVERSION)
	{
#endif
    usprintf(s,"IP: ");
    /* Sneak peek at IP header and find length */
    ip_len = ((*bpp)->data[0] & 0xf) << 2;
    if(ip_len < IPLEN){
        usprintf(s,"bad header\n");
        return;
    }
    if(check)
        csum = cksum(NULLHEADER,*bpp,ip_len);
    else
        csum = 0;
  
    ntohip(&ip,bpp);    /* Can't fail, we've already checked ihl (bad to assume 02Feb2023, Maiko) */

#ifdef IPV6
	}
	else if (version == IPV6VERSION)
	{
		/* 23Feb2023, Maiko, now in ipv6.h where it belongs
		 extern char *human_readable_ipv6 (unsigned char *addr);
		 */

       	usprintf(s,"IPV6: ");

		/* no checksum in IPV6 */

    	if (ntohipv6 (&ipv6,bpp) == -1)
		{
        	usprintf(s,"bad header\n");
        	return;
    	}

        usprintf (s,"len %u next header %d hop limit %d prot ",
			ipv6.payload_len + IPV6LEN, (int)(ipv6.next_header), (int)(ipv6.hop_limit));

		switch (ipv6.next_header)
		{
			case 6:
				usprintf (s, "TCP");
				break;

			/* 12Apr2023, Maiko, Now working on UDP related code */
			case 17:
				usprintf (s, "UDP");
				break;

			case 58:
				usprintf (s, "ICMP");
				break;

			default:
				usprintf (s, "?");
				break;
		}

		usprintf (s, "\n src %s\n", human_readable_ipv6 (ipv6.source));
		usprintf (s, " dst %s\n", human_readable_ipv6 (ipv6.dest));

		/* 17Feb2023, Maiko (VE4KLM), Update the trace code from when I
		 * first started, more mistakes, did not have proper args !!! */
		switch (ipv6.next_header)
		{
			case 6:
	          	tcp_dumpv6 (s,bpp,&ipv6,check);
				break;

			/* 12Apr2023, Maiko, Now working on UDP related code */
			case 17:
            	udp_dumpv6 (s,bpp,&ipv6,check);
				break;

			case 58:
            	icmp_dumpv6(s,bpp,&ipv6,check);
				break;
		}

		return;	/* just return for now, at least there is a hex dump to follow */
	}
	else
	{
		usprintf(s,"CRAP: no idea what this is\n");

		return;	/* just return for now, at least there is a hex dump to follow */
	}
#endif
  
    /* Trim data segment if necessary. */
    length = ip.length - ip_len;    /* Length of data portion */
    trim_mbuf(bpp,length);
#ifdef MONITOR
    if (!Trace_compact_header)
#endif
        usprintf(s,"len %u",ip.length);
    usprintf(s," %s",inet_ntoa(ip.source));
#ifdef MONITOR
    if (Trace_compact_header)
        usprintf(s, "->%s", inet_ntoa(ip.dest));
    else
#endif
        usprintf(s,"->%s ihl %u ttl %u",
        inet_ntoa(ip.dest),ip_len,uchar(ip.ttl));
#ifdef MONITOR
    if (!Trace_compact_header)
#endif
        if(ip.tos != 0)
            usprintf(s," tos %u",uchar(ip.tos));
    if(ip.offset != 0 || ip.flags.mf)
        usprintf(s," id %u offs %u",ip.id,ip.offset);
    if(ip.flags.congest)
        usprintf(s," CE");
    if(ip.flags.df)
        usprintf(s," DF");
    if(ip.flags.mf){
        usprintf(s," MF");
        check = 0;  /* Bypass host-level checksum verify */
    }
    if(csum != 0)
        usprintf(s," CHECKSUM ERROR (%u)",csum);
  
    if(ip.offset != 0){
        usprintf(s,"\n");
        return;
    }
    switch(uchar(ip.protocol)){
        case TCP_PTCL:
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, " TCP ");
            else
#endif
                usprintf(s," prot TCP\n");
            tcp_dump(s,bpp,ip.source,ip.dest,check);
            break;
        case UDP_PTCL:
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, " UDP ");
            else
#endif
                usprintf(s," prot UDP\n");
            udp_dump(s,bpp,ip.source,ip.dest,check);
            break;
        case ICMP_PTCL:
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, " ICMP ");
            else
#endif
                usprintf(s," prot ICMP\n");
            icmp_dump(s,bpp,ip.source,ip.dest,check);
            break;
        case IP_PTCL:
        case IP_PTCL_OLD:
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, " <= ");
            else
#endif
                usprintf(s," prot IP\n");
            ip_dump(s,bpp,check);
            break;
#ifdef AX25
        case AX25_PTCL:
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, " <= ");
            else
#endif
                usprintf(s," prot AX25\n");
            ax25_dump(s,bpp,check);
            break;
#endif
#ifdef  RSPF
        case RSPF_PTCL:
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, " RSPF\n");
            else
#endif
                usprintf(s," prot RSPF\n");
            rspf_dump(s,bpp,ip.source,ip.dest,check);
            break;
#endif
        default:
            usprintf(s," prot %u\n",uchar(ip.protocol));
            break;
    }
}
