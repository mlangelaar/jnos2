/* Mods by PA0GRI */
#ifndef _ARP_H
#define _ARP_H
  
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
#ifndef _MBUF_H
#include "mbuf.h"
#endif
  
#ifndef _IFACE_H
#include "iface.h"
#endif
  
#ifndef _TIMER_H
#include "timer.h"
#endif
  
/* Lifetime of a valid ARP entry */
#ifndef ARPLIFE
#define ARPLIFE     900 /* 15 minutes */
#endif
/* Lifetime of a pending ARP entry */
#define PENDTIME    15  /* 15 seconds */
/* Maximum number of datagrams in queue while pending an resolution */
#define ARP_QUEUE   5   /* 5 packets max on queue */
  
/* ARP definitions (see RFC 826) */
  
#define ARPLEN  16      /* Size of ARP hdr, minus hardware addresses */
  
/* Address size definitions */
#define IPALEN  4       /* Length in bytes of an IP address */
/* Should be long enough below */
#define MAXHWALEN   16 /* Maximum length of a hardware address */
  
/* ARP opcodes */
#define ARP_REQUEST 1
#define ARP_REPLY   2
#define REVARP_REQUEST  3
#define REVARP_REPLY    4
  
/* Hardware types */
#define ARP_NETROM  0   /* Fake for NET/ROM (never actually sent) */
#define ARP_ETHER   1   /* Assigned to 10 megabit Ethernet */
#define ARP_EETHER  2   /* Assigned to experimental Ethernet */
#define ARP_AX25    3   /* Assigned to AX.25 Level 2 */
#define ARP_PRONET  4   /* Assigned to PROnet token ring */
#define ARP_CHAOS   5   /* Assigned to Chaosnet */
#define ARP_IEEE802 6   /* Who uses this? */
#define ARP_ARCNET  7
#define ARP_APPLETALK   8
extern char *Arptypes[];    /* Type fields in ASCII, defined in arpcmd */
#define NHWTYPES 9
  
/* Table of hardware types known to ARP */
struct arp_type {
    int16 hwalen;       /* Hardware length */
    int16 iptype;       /* Hardware type field for IP */
    int16 arptype;      /* Hardware type field for ARP */
    int16 rarptype;     /* Hardware type field for RARP */
    int16 pendtime;     /* # secs to wait pending response */
    char *bdcst;        /* Hardware broadcast address */
    char *(*format) __ARGS((char *,char *));
                /* Function that formats addresses */
    int (*scan) __ARGS((char *,char *));
                /* Reverse of format */
};
extern struct arp_type far Arp_type[];
#define NULLATYPE   (struct arp_type *)0
  
/* Format of an ARP request or reply packet. From p. 3 */
struct arp {
    int16 hardware;         /* Hardware type */
    int16 protocol;         /* Protocol type */
    char hwalen;            /* Hardware address length, bytes */
    char pralen;            /* Length of protocol address */
    int16 opcode;           /* ARP opcode (request/reply) */
    char shwaddr[MAXHWALEN];    /* Sender hardware address field */
    int32 sprotaddr;        /* Sender Protocol address field */
    char thwaddr[MAXHWALEN];    /* Target hardware address field */
    int32 tprotaddr;        /* Target protocol address field */
};
  
/* Format of ARP table */
struct arp_tab {
    struct arp_tab *next;   /* Doubly-linked list pointers */
    struct arp_tab *prev;
    struct timer timer; /* Time until aging this entry */
    struct mbuf *pending;   /* Queue of datagrams awaiting resolution */
    int32 ip_addr;      /* IP Address, host order */
    int16 hardware;     /* Hardware type */
    char state;     /* (In)complete */
#define ARP_PENDING 0
#define ARP_VALID   1
    char pub;       /* Respond to requests for this entry? */
    char *hw_addr;      /* Hardware address */
    struct iface *iface;    /* Interface to use route on -- sm6rpz */
};
#define NULLARP (struct arp_tab *)0
extern struct arp_tab *Arp_tab[];
  
#ifdef UNIX
#ifndef recv
/* hack; we #define'd recv to j_recv to avoid stdc botch, so we need it here
   else some things call this arp_stat.recv and others arp_stat.j_recv! */
#include "socket.h"
#endif
#endif
struct arp_stat {
    unsigned recv;      /* Total number of ARP packets received */
    unsigned badtype;   /* Incoming requests for unsupported hardware */
    unsigned badlen;    /* Incoming length field(s) didn't match types */
    unsigned badaddr;   /* Bogus incoming addresses */
    unsigned inreq;     /* Incoming requests for us */
    unsigned replies;   /* Replies sent */
    unsigned outreq;    /* Outoging requests sent */
};
extern struct arp_stat Arp_stat, Rarp_stat;
  
/* In arp.c: */
struct arp_tab *arp_add __ARGS((int32 ipaddr,int16 hardware,char *hw_addr,
int pub,struct iface *iface));
void arp_drop __ARGS((void *p));
int arp_init __ARGS((unsigned int hwtype,int hwalen,int iptype,int arptype,
int pendtime,char *bdcst,char *(*format) __ARGS((char *,char *)),
int  (*scan) __ARGS((char *,char *)) ));
void arp_input __ARGS((struct iface *iface,struct mbuf *bp));
struct arp_tab *arp_lookup __ARGS((int16 hardware,int32 ipaddr,struct iface *iface));
char *res_arp __ARGS((struct iface *iface,int16 hardware,int32 target,struct mbuf *bp));
  
/* In arphdr.c: */
struct mbuf *htonarp __ARGS((struct arp *arp));
int ntoharp __ARGS((struct arp *arp,struct mbuf **bpp));
  
/* In rarp.c: */
void rarp_input __ARGS((struct iface *iface,struct mbuf *bp));
  
#endif /* _ARP_H */
