/* RIP packet tracing
 * Copyright 1991 Phil Karn, KA9Q
 *
 *  Changes Copyright (c) 1993 Jeff White - N0POY, All Rights Reserved.
 *  Permission granted for non-commercial copying and use, provided
 *  this notice is retained.
 *
 * Rehack for RIP-2 (RFC1388) by N0POY 4/1993
 *
 * Beta release 11/10/93 V0.91
 *
 * 2/19/94 release V1.0
 *
 * Rip98 support added for 1.11d, G4HIP/N5KNX
 *
 */
/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
#include "global.h"
#ifdef RIP
#include "mbuf.h"
#include "netuser.h"
#include "timer.h"
#include "rip.h"
#include "trace.h"
#include "socket.h"  

#ifdef UNIX
/* We can't use usputc since we don't intercept that call for rerouting to
   the trace screen (for efficiency or stubbornness, hard to say which! n5knx
   */
#define PUTNL(s)	usputs(s,"\n")
#else
#define PUTNL(s)	usputc(s,'\n')
#endif

// from trace.c
//void fmtline __ARGS((int s,int16 addr,char *buf,int16 len));
  
void
rip_dump(s,bpp)
int s;
struct mbuf **bpp;
{
    struct rip_route entry;
    struct rip_authenticate *ripauth;
    int i;
    int cmd,version;
    int16 len;
    int16 domain;
    char ipaddmask[25];
#ifdef RIP98
    int entrylen = RIP_ENTRY;
#else
#define entrylen RIP_ENTRY
#endif
  
    usprintf(s,"RIP: ");
    cmd = PULLCHAR(bpp);
    version = PULLCHAR(bpp);
    switch(cmd){
        case RIPCMD_REQUEST:
            usprintf(s,"REQUEST");
            break;
        case RIPCMD_RESPONSE:
            usprintf(s,"RESPONSE");
            break;
        default:
            usprintf(s," cmd %u",cmd);
            break;
    }
  
    domain = (int16)pull16(bpp);
  
#ifdef RIP98
    if (version == RIP_VERSION_98)
        entrylen = RIP98_ENTRY;
#endif

    len = len_p(*bpp);
    usprintf(s," vers %u entries %u domain %u:\n",version,len / entrylen, domain);
  
    i = 0;
    while(len >= entrylen){
        /* Pull an entry off the packet */
#ifdef RIP98
        if (version == RIP_VERSION_98)
	        pull98entry (&entry,bpp);
        else
#endif
        pullentry(&entry,bpp);
        len -= entrylen;
  
        if (entry.rip_family == RIP_AF_AUTH) {
            ripauth = (struct rip_authenticate *)&entry;
            usprintf(s,"RIP: AUTHENTICATION Type %d  Password: %.16s\n",
            ripauth->rip_auth_type,ripauth->rip_auth_str);
            continue;
        }
  
        if(entry.rip_family != RIP_AF_INET) {
            /* Skip non-IP addresses */
            continue;
        }
  
        if (version >= RIP_VERSION_2) {
            sprintf(ipaddmask, "%s/%-4d", inet_ntoa(entry.rip_dest),
            mask2width(entry.rip_dest_mask));
        } else {
            sprintf(ipaddmask, "%s/??", inet_ntoa(entry.rip_dest));
        }
        usprintf(s,"%-20s%-3u",ipaddmask, entry.rip_metric);
  
        if((++i % 3) == 0){
            PUTNL(s);
        }
    }
    if((i % 3) != 0)
        PUTNL(s);
}
  
#endif /* RIP */
  
