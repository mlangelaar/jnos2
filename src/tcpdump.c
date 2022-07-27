/* TCP header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "iface.h"
#include "tcp.h"
#include "ip.h"
#include "trace.h"
#include "socket.h"  

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
  
/* Dump a TCP segment header. Assumed to be in network byte order */
void
tcp_dump(s,bpp,source,dest,check)
int s;
struct mbuf **bpp;
int32 source,dest;  /* IP source and dest addresses */
int check;      /* 0 if checksum test is to be bypassed */
{
    struct tcp seg;
    struct pseudo_header ph;
    int16 csum;
    int16 dlen;
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;
  
    /* Verify checksum */
    ph.source = source;
    ph.dest = dest;
    ph.protocol = TCP_PTCL;
    ph.length = len_p(*bpp);
    csum = cksum(&ph,*bpp,ph.length);
  
    ntohtcp(&seg,bpp);
  
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
  
