/* UDP packet tracing
 * Copyright 1991 Phil Karn, KA9Q
 */
/* Mods by PA0GRI */
/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "udp.h"
#include "ip.h"
#include "socket.h"
#include "trace.h"
#include "socket.h"  

#ifdef	IPV6
#include "ipv6.h"	/* 12Apr2023, Maiko, Working on IPV6 UDP now */
#include "icmpv6.h"	/* 14Apr2023, Maiko */
#endif

#ifdef RWHO_DUMP
/* 06Oct2009, Maiko, Looks like it's not in use anymore, comment out ! */
void rwho_dump __ARGS((int s,struct mbuf **bpp));
#endif

/* 12Apr2023, Maiko (VE4KLM), breakout common code */
void udp_dump_breakout (int s, int16 csum, int check, struct mbuf **bpp)
{
    struct udp udp;

    ntohudp(&udp,bpp);
  
#ifdef MONITOR
    if (!Trace_compact_header)
#endif
        usprintf(s,"UDP: len %u %u->%u",udp.length,udp.source,udp.dest);
#ifdef MONITOR
    if (!Trace_compact_header)
#endif
        if(udp.length > UDPHDR)
            usprintf(s," Data %u",udp.length - UDPHDR);
    if(udp.checksum == 0)
        check = 0;
    if(check)
        usprintf(s," CHECKSUM ERROR (%u)",csum);
  
    usprintf(s,"\n");
  
    switch(udp.dest){
#ifdef RIP
        case IPPORT_RIP:
            rip_dump(s,bpp);
#endif
#ifdef RWHO_DUMP
/* 06Oct2009, Maiko, Looks like it's not in use anymore, comment out ! */
        case IPPORT_RWHO:
            rwho_dump(s,bpp);
#endif
    }
}

#ifdef	IPV6
/* 12Apr2023, Maiko, Get proper UDP tracing in place for IPV6 */
void udp_dumpv6 (int s, struct mbuf **bpp, struct ipv6 *ipv6, int check)
{
    int16 csum;
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;

   /* Verify checksum */
    csum = icmpv6cksum (ipv6, *bpp, len_p (*bpp));

	if (csum == 0)
     	check = 0;  /* No checksum error */

    udp_dump_breakout (s, csum, check, bpp);
}
#endif
  
/* Dump a UDP header */
void
udp_dump(s,bpp,source,dest,check)
int s;
struct mbuf **bpp;
int32 source,dest;
int check;      /* If 0, bypass checksum verify */
{
    struct udp udp;
    struct pseudo_header ph;
    int16 csum;
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;
  
#ifdef MONITOR
    if (!Trace_compact_header)
#endif
        usprintf(s,"UDP:");
  
    /* Compute checksum */
    ph.source = source;
    ph.dest = dest;
    ph.protocol = UDP_PTCL;
    ph.length = len_p(*bpp);
    if((csum = cksum(&ph,*bpp,ph.length)) == 0)
        check = 0;  /* No checksum error */

    udp_dump_breakout (s, csum, check, bpp);
}
  
