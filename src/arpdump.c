/* ARP packet tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
#include "global.h"
#include "mbuf.h"
#include "arp.h"
#include "netuser.h"
#include "trace.h"
#include "socket.h"  

void
arp_dump(s,bpp)
int s;
struct mbuf **bpp;
{
    struct arp arp;
    struct arp_type *at;
    int is_ip = 0;
    char tmp[25];
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;
#ifdef MONITOR
    if (Trace_compact_header)
    {
      /* it's different enough that we just do it here */
      /* except for the setup, that is... */
        if (ntoharp(&arp,bpp) == -1)
        {
            usprintf(s, "bad packet\n");
            return;
        }
        if (arp.hardware < NHWTYPES)
            at = &Arp_type[arp.hardware];
        else
            at = NULLATYPE;
        if (at != NULLATYPE && arp.protocol == at->iptype)
            is_ip = 1;
        switch (arp.opcode)
        {
            case ARP_REQUEST:
            case REVARP_REQUEST:
                usprintf(s, "REQ");
                break;
            case ARP_REPLY:
            case REVARP_REPLY:
                usprintf(s, "REP");
                break;
            default:
                usprintf(s, "%u", arp.opcode);
                break;
        }
        if (is_ip)
            usprintf(s, " %s@", inet_ntoa(arp.sprotaddr));
        if (at)
            usprintf(s, "%s->", at->format(tmp, arp.shwaddr));
        if (is_ip)
            usprintf(s, "%s@", inet_ntoa(arp.tprotaddr));
        if (at)
            usprintf(s, "%s", at->format(tmp, arp.thwaddr));
        usprintf(s, "\n");
        return;
    }
#endif
  
    usprintf(s,"ARP: len %d",len_p(*bpp));
    if(ntoharp(&arp,bpp) == -1){
        usprintf(s," bad packet\n");
        return;
    }
    if(arp.hardware < NHWTYPES)
        at = &Arp_type[arp.hardware];
    else
        at = NULLATYPE;
  
    /* Print hardware type in Ascii if known, numerically if not */
    usprintf(s," hwtype %s",smsg(Arptypes,NHWTYPES,arp.hardware));
  
    /* Print hardware length only if unknown type, or if it doesn't match
     * the length in the known types table
     */
    if(at == NULLATYPE || arp.hwalen != at->hwalen)
        usprintf(s," hwlen %u",arp.hwalen);
  
    /* Check for most common case -- upper level protocol is IP */
    if(at != NULLATYPE && arp.protocol == at->iptype){
        usprintf(s," prot IP");
        is_ip = 1;
    } else {
        usprintf(s," prot 0x%x prlen %u",arp.protocol,arp.pralen);
    }
    switch(arp.opcode){
        case ARP_REQUEST:
            usprintf(s," op REQUEST");
            break;
        case ARP_REPLY:
            usprintf(s," op REPLY");
            break;
        case REVARP_REQUEST:
            usprintf(s," op REVERSE REQUEST");
            break;
        case REVARP_REPLY:
            usprintf(s," op REVERSE REPLY");
            break;
        default:
            usprintf(s," op %u",arp.opcode);
            break;
    }
    usprintf(s,"\n");
    usprintf(s,"sender");
    if(is_ip)
        usprintf(s," IPaddr %s",inet_ntoa(arp.sprotaddr));
    usprintf(s," hwaddr %s\n",at->format(tmp,arp.shwaddr));
  
    usprintf(s,"target");
    if(is_ip)
        usprintf(s," IPaddr %s",inet_ntoa(arp.tprotaddr));
    usprintf(s," hwaddr %s\n",at->format(tmp,arp.thwaddr));
}
