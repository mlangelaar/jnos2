/* Ethernet header conversion routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#if defined PC_EC || defined PACKET
#include "mbuf.h"
#include "enet.h"
  
/* Convert Ethernet header in host form to network mbuf */
struct mbuf *
htonether(ether,bp)
struct ether *ether;
struct mbuf *bp;
{
    register char *cp;
  
    bp = pushdown(bp,ETHERLEN);
  
    cp = bp->data;
  
    memcpy(cp,ether->dest,EADDR_LEN);
    cp += EADDR_LEN;
    memcpy(cp,ether->source,EADDR_LEN);
    cp += EADDR_LEN;
    put16(cp,ether->type);
  
    return bp;
}
/* Extract Ethernet header */
int
ntohether(ether,bpp)
struct ether *ether;
struct mbuf **bpp;
{
    pullup(bpp,ether->dest,EADDR_LEN);
    pullup(bpp,ether->source,EADDR_LEN);
    ether->type = pull16(bpp);
    return ETHERLEN;
}
#endif /* PC_EC || PACKET */
  
