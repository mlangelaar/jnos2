/* IP header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
 /* Mods by PA0GRI */
/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "iface.h"
#include "ip.h"
#include "trace.h"
#include "netuser.h"
#include "socket.h"  

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
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;
  
#ifdef MONITOR
    if (!Trace_compact_header)
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
  
    ntohip(&ip,bpp);    /* Can't fail, we've already checked ihl */
  
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
