/*
 * TCP header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J
 *
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 */
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "iface.h"
#include "tcp.h"
#include "ip.h"
#include "trace.h"
#include "socket.h"  

#ifdef	IPV6
/* 17Feb2023, Maiko, Forgot about icmpv6cksum prototypes, Ubuntu crashes without */
#include "ipv6.h"
#include "icmpv6.h"
#endif
/* TCP segment header flags */
static char *Tcpflags[] = {
    "FIN",  /* 0x01 */
    "SYN",  /* 0x02 */
    "RST",  /* 0x04 */
    "PSH",  /* 0x08 */
    "ACK",  /* 0x10 */
    "URG",  /* 0x20 */
    "CE"    /* 0x40 */
};

void tcp_dump_breakout (int s, int16 csum, int check, struct mbuf **bpp)
{
    struct tcp seg;
    int16 dlen;

    ntohtcp (&seg, bpp);
  
#ifdef MONITOR
    if (Trace_compact_header)
        usprintf(s, "%u->%u", seg.source, seg.dest);
    else
#endif
        usprintf(s,"TCP: %u->%u Seq x%lx",seg.source,seg.dest,seg.seq,seg.ack);
#ifdef MONITOR
    if (!Trace_compact_header)
#endif
        if(seg.flags.ack)
            usprintf(s," Ack x%lx",seg.ack);
    if(seg.flags.congest)
        usprintf(s," %s",Tcpflags[6]);
    if(seg.flags.urg)
        usprintf(s," %s",Tcpflags[5]);
    if(seg.flags.ack)
        usprintf(s," %s",Tcpflags[4]);
    if(seg.flags.psh)
        usprintf(s," %s",Tcpflags[3]);
    if(seg.flags.rst)
        usprintf(s," %s",Tcpflags[2]);
    if(seg.flags.syn)
        usprintf(s," %s",Tcpflags[1]);
    if(seg.flags.fin)
        usprintf(s," %s",Tcpflags[0]);
  
#ifdef MONITOR
    if (!Trace_compact_header)
#endif
        usprintf(s," Wnd %u",seg.wnd);
    if(seg.flags.urg)
        usprintf(s," UP x%x",seg.up);
    /* Print options, if any */
#ifdef MONITOR
    if (!Trace_compact_header)
#endif
        if(seg.mss != 0)
            usprintf(s," MSS %u",seg.mss);
#ifdef MONITOR
    if (!Trace_compact_header)
#endif
        if((dlen = len_p(*bpp)) != 0)
            usprintf(s," Data %u",dlen);
    if(check && csum != 0)
        usprintf(s," CHECKSUM ERROR (%u)",csum);
    usprintf(s,"\n");
}

#ifdef	IPV6
/*
 * 17Feb2023, Maiko, Get proper TCP tracing in place, now that things
 * moving forward with IPV6, and creating another breakout function,
 * and I don't believe I will need to write a new ntohtcpv6() yet,
 * since the checksum call in this case is outside the function ?
 *  (yup, confirmed working, looks just like the ipv4 counterpart)
 */
void tcp_dumpv6 (int s, struct mbuf **bpp, struct ipv6 *ipv6, int check)
{
    int16 csum;
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;

    /* Verify checksum */
	csum = icmpv6cksum (ipv6, *bpp, len_p (*bpp));

	tcp_dump_breakout (s, csum, check, bpp);
}
#endif
  
/* Dump a TCP segment header. Assumed to be in network byte order */
void
tcp_dump(s,bpp,source,dest,check)
int s;
struct mbuf **bpp;
int32 source,dest;  /* IP source and dest addresses */
int check;      /* 0 if checksum test is to be bypassed */
{
    // struct tcp seg;
    struct pseudo_header ph;
    int16 csum;
    // int16 dlen;
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;
  
    /* Verify checksum */
    ph.source = source;
    ph.dest = dest;
    ph.protocol = TCP_PTCL;
    ph.length = len_p(*bpp);
    csum = cksum(&ph,*bpp,ph.length);

	tcp_dump_breakout (s, csum, check, bpp);
}
  
