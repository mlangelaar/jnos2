/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "netuser.h"
#include "icmp.h"
#include "trace.h"
#include "ip.h"
#include "socket.h"  

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
  
#ifdef MONITOR
    if (Trace_compact_header)
        usprintf(s, "%s", smsg(Icmptypes,ICMP_TYPES,uchar(icmp.type)));
    else
#endif
        usprintf(s,"ICMP: type %s",smsg(Icmptypes,ICMP_TYPES,uchar(icmp.type)));
  
    switch(uchar(icmp.type)){
        case ICMP_DEST_UNREACH:
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, "%s", smsg(Unreach,NUNREACH,uchar(icmp.code)));
            else
#endif
                usprintf(s," code %s",smsg(Unreach,NUNREACH,uchar(icmp.code)));
            break;
        case ICMP_REDIRECT:
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, "%s", smsg(Redirect,NREDIRECT,uchar(icmp.code)));
            else
#endif
                usprintf(s," code %s",smsg(Redirect,NREDIRECT,uchar(icmp.code)));
            usprintf(s," new gateway %s",inet_ntoa(icmp.args.address));
            break;
        case ICMP_TIME_EXCEED:
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, "%s", smsg(Exceed,NEXCEED,uchar(icmp.code)));
            else
#endif
                usprintf(s," code %s",smsg(Exceed,NEXCEED,uchar(icmp.code)));
            break;
        case ICMP_PARAM_PROB:
            usprintf(s," pointer %u",icmp.args.pointer);
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
                usprintf(s," id %u seq %u",icmp.args.echo.id,icmp.args.echo.seq);
            break;
    }
    if(check && csum != 0){
        usprintf(s," CHECKSUM ERROR (%u)",csum);
    }
    usprintf(s,"\n");
    /* Dump the offending IP header, if any */
    switch(icmp.type){
        case ICMP_DEST_UNREACH:
        case ICMP_TIME_EXCEED:
        case ICMP_PARAM_PROB:
        case ICMP_QUENCH:
        case ICMP_REDIRECT:
            usprintf(s,"Returned ");
            ip_dump(s,bpp,0);
    }
}
  
