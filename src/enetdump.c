/* Ethernet header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
#include "global.h"
#if defined PC_EC || defined PACKET
#include "mbuf.h"
#include "enet.h"
#include "trace.h"
#include "socket.h"  

void
ether_dump(s,bpp,check)
int s;
struct mbuf **bpp;
int check;  /* Not used */
{
    struct ether ehdr;
    char src[20],dest[20];
  
    ntohether(&ehdr,bpp);
    pether(src,ehdr.source);
    pether(dest,ehdr.dest);
    usprintf(s,"Ether: len %u %s->%s",ETHERLEN + len_p(*bpp),src,dest);
  
    switch(ehdr.type){
        case IP_TYPE:
            usprintf(s," type IP\n");
            ip_dump(s,bpp,1);
            break;
        case REVARP_TYPE:
            usprintf(s," type REVARP\n");
            arp_dump(s,bpp);
            break;
        case ARP_TYPE:
            usprintf(s," type ARP\n");
            arp_dump(s,bpp);
            break;
        default:
            usprintf(s," type 0x%x\n",ehdr.type);
            break;
    }
}
int
ether_forus(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
    /* Just look at the multicast bit */
  
    if(bp->data[0] & 1)
        return 0;
    else
        return 1;
}
#endif /* PC_EC || PACKET */
  
