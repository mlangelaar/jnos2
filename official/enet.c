/* Stuff generic to all Ethernet controllers
 * Copyright 1991 Phil Karn, KA9Q
 */
/* Mods by PA0GRI */
#include "global.h"

/* 26Mar2023, Maiko, Get rid of dependence on PACKET definition */
#if (defined(PC_EC) || defined(PACKET) || defined(IPV6))

// #if defined PC_EC || defined PACKET

#warning "USING OLD ETHERNET CODE HA HA HA ..."

#include "mbuf.h"
#include "iface.h"
#include "arp.h"
#include "ip.h"
#include "enet.h"
  
char Ether_bdcst[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

#ifdef	IPV6
/* 13Mar2023, Maiko, Anything starting with 33:33: */
char Ether_ipv6_multicast[] = { 0x33, 0x33 };
#endif
  
/* Convert Ethernet header in host form to network mbuf */
struct mbuf *
htonether(ether,data)
struct ether *ether;
struct mbuf *data;
{
    struct mbuf *bp;
    register char *cp;
  
    bp = pushdown(data,ETHERLEN);
  
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
    ether->type = (int16) pull16(bpp);
    return ETHERLEN;
}
  
/* Format an Ethernet address into a printable ascii string */
char *
pether(out,addr)
char *out,*addr;
{
    sprintf(out,"%02x:%02x:%02x:%02x:%02x:%02x",
    uchar(addr[0]),uchar(addr[1]),
    uchar(addr[2]),uchar(addr[3]),
    uchar(addr[4]),uchar(addr[5]));
    return out;
}
  
/* Convert an Ethernet address from Hex/ASCII to binary */
int
gether(out,cp)
register char *out;
register char *cp;
{
    register int i;
  
    for(i=6; i!=0; i--){
        *out++ = htoi(cp);
        if((cp = strchr(cp,':')) == NULLCHAR)   /* Find delimiter */
            break;
        cp++;           /* and skip over it */
    }
    return i;
}
/* Send an IP datagram on Ethernet */
int
enet_send(bp,iface,gateway,prec,del,tput,rel)
struct mbuf *bp;    /* Buffer to send */
struct iface *iface;    /* Pointer to interface control block */
int32 gateway;      /* IP address of next hop */
int prec;
int del;
int tput;
int rel;
{
    char *egate;
  
    if(gateway == iface->broadcast) /* This is a broadcast IP datagram */
        return (*iface->output)(iface,Ether_bdcst,iface->hwaddr,IP_TYPE,bp);
  
    egate = res_arp(iface,ARP_ETHER,gateway,bp);
    if(egate != NULLCHAR)
        return (*iface->output)(iface,egate,iface->hwaddr,IP_TYPE,bp);
    return 0;
}
/* Send a packet with Ethernet header */
int
enet_output(iface,dest,source,type,data)
struct iface *iface;    /* Pointer to interface control block */
char *dest;     /* Destination Ethernet address */
char *source;       /* Source Ethernet address */
int16 type;     /* Type field */
struct mbuf *data;  /* Data field */
{
    struct ether ep;
    struct mbuf *bp;
  
    memcpy(ep.dest,dest,EADDR_LEN);
    memcpy(ep.source,source,EADDR_LEN);
    ep.type = type;
    if((bp = htonether(&ep,data)) == NULLBUF){
        free_p(data);
        return -1;
    }
    return (*iface->raw)(iface,bp);
}

#ifdef	IPV6
/* 23Mar2023, Maiko */ 
extern int ipv6_route (struct iface*, struct mbuf*, int);
#endif

/* Process incoming Ethernet packets. Shared by all ethernet drivers. */
void
eproc(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
    struct ether hdr;
  
    /* Remove Ethernet header and kick packet upstairs */
    ntohether(&hdr,&bp);

#ifdef	IPV6
	if (memcmp (hdr.dest,Ether_ipv6_multicast, 2) == 0)
	{
		// log (-1, "ipv6 ethernet multicast");
	}
	else
	/* 13Mar2023, Maiko (VE4KLM), eproc will crash if the HW address is not set ! */ 
	if (!(iface->hwaddr))
	{
		log (-1, "iface [%s] hw address not set!", iface->name);
		return;
	}
	else
	/*
	 * 20Mar2023, Maiko, IF I am to send anything OUT, then I need
	 * to make sure the check further below is NOT done IF I am the
	 * source of the packet, this is kludgy, but my only option at
	 * this point - until I figure out how to 'properly' get the
	 * TAP interface in place, it's been quite the challenge !
	 *  (eproc would never be called on any output of data, but
	 *   because I mam inserted ethernet header right in the IPV6
	 *   send, then eproc() from hopper again to process it :)
	 */
	if (memcmp (hdr.source, iface->hwaddr, EADDR_LEN) == 0)
	{
		// log (-1, "ipv6 ethernet output");
	}
	else
#endif

    if(memcmp(hdr.dest,iface->hwaddr,EADDR_LEN) &&
    memcmp(hdr.dest,Ether_bdcst,EADDR_LEN)){
        free_p(bp);
        return;
    }
    switch(hdr.type){
        case REVARP_TYPE:
        case ARP_TYPE:
            arp_input(iface,bp);
            break;
        case IP_TYPE:
            ip_route(iface,bp,hdr.dest[0] & 1);
            break;
#ifdef	IPV6
	/*
	 * 13Mar2023, Maiko (VE4KLM), multicast NDP stuff
	 * only shows up at link layer (ethernet) ...
	 */
        case IPV6_TYPE:
	/*
	 * 22Mar2023, Maiko, F#Dking hell, this broadcast 'bit' has
	 * screwed me around since I starting working on TAP iface,
	 * took me all this time to see it (with excess debugging).
	 *
            ipv6_route(iface,bp,hdr.dest[0] & 1);
	 *
	 */
            ipv6_route(iface,bp,0);
            break;
#endif
        default:
            free_p(bp);
            break;
    }
}
#endif /* PC_EC || PACKET */
  
