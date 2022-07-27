#ifndef _ENET_H
#define _ENET_H
  
/* Generic Ethernet constants and templates */
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
#ifndef _MBUF_H
#include "mbuf.h"
#endif
  
#ifndef _IFACE_H
#include "iface.h"
#endif
  
#define EADDR_LEN   6
/* Format of an Ethernet header */
struct ether {
    char dest[EADDR_LEN];
    char source[EADDR_LEN];
    int16 type;
};
#define ETHERLEN    14
  
/* Ethernet broadcast address */
extern char Ether_bdcst[];
  
/* Ethernet type fields */
#define IP_TYPE     0x800   /* Type field for IP */
#define ARP_TYPE    0x806   /* Type field for ARP */
#define REVARP_TYPE 0x8035  /* Type field for reverse ARP */
  
#define RUNT        60  /* smallest legal size packet, no fcs */
#define GIANT       1514    /* largest legal size packet, no fcs */
  
#define MAXTRIES    16  /* Maximum number of transmission attempts */
  
/* In file enet.c: */
char *pether __ARGS((char *out,char *addr));
int gether __ARGS((char *out,char *cp));
int enet_send __ARGS((struct mbuf *bp,struct iface *iface,int32 gateway,int prec,
int del,int tput,int rel));
int enet_output __ARGS((struct iface *iface,char dest[],char source[],int16 type,
struct mbuf *data));
void eproc __ARGS((struct iface *iface,struct mbuf *bp));
  
/* In enethdr.c: */
struct mbuf *htonether __ARGS((struct ether *ether,struct mbuf *data));
int ntohether __ARGS((struct ether *ether,struct mbuf **bpp));
  
#endif  /* _ENET_H */
  
