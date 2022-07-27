#ifndef _UDP_H
#define _UDP_H
  
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
#ifndef _MBUF_H
#include "mbuf.h"
#endif
  
#ifndef _IFACE_H
#include "iface.h"
#endif
  
#ifndef _INTERNET_H
#include "internet.h"
#endif
  
#ifndef _IP_H
#include "ip.h"
#endif
  
#ifndef _NETUSER_H
#include "netuser.h"
#endif
  
/* SNMP MIB variables, used for statistics and control. See RFC 1066 */
extern struct mib_entry Udp_mib[];
#define udpInDatagrams  Udp_mib[1].value.integer
#define udpNoPorts  Udp_mib[2].value.integer
#define udpInErrors Udp_mib[3].value.integer
#define udpOutDatagrams Udp_mib[4].value.integer
#define NUMUDPMIB   4
  
/* User Datagram Protocol definitions */
  
/* Structure of a UDP protocol header */
struct udp {
    int16 source;   /* Source port */
    int16 dest; /* Destination port */
    int16 length;   /* Length of header and data */
    int16 checksum; /* Checksum over pseudo-header, header and data */
};
#define UDPHDR  8   /* Length of UDP header */
  
/* User Datagram Protocol control block
 * Each entry on the receive queue consists of the
 * remote socket structure, followed by any data
 */
struct udp_cb {
    struct udp_cb *next;
    struct socket socket;   /* Local port accepting datagrams */
    void (*r_upcall) __ARGS((struct iface *iface,struct udp_cb *,int));
                /* Function to call when one arrives */
    struct mbuf *rcvq;  /* Queue of pending datagrams */
    int rcvcnt;     /* Count of pending datagrams */
    int user;       /* User link */
};
extern struct udp_cb *Udps; /* Hash table for UDP structures */
#define NULLUDP (struct udp_cb *)0
  
/* UDP primitives */
  
/* In udp.c: */
int del_udp __ARGS((struct udp_cb *up));
struct udp_cb *open_udp __ARGS((struct socket *lsocket,
void (*r_upcall) __ARGS((struct iface *iface,struct udp_cb *,int))));
int recv_udp __ARGS((struct udp_cb *up,struct socket *fsocket,struct mbuf **bp));
int send_udp __ARGS((struct socket *lsocket,struct socket *fsocket,char tos,
char ttl,struct mbuf *data,int16 length,int16 id,char df));
void udp_input __ARGS((struct iface *iface,struct ip *ip,struct mbuf *bp,
int rxbroadcast));
void udp_garbage __ARGS((int drastic));
  
#ifdef HOPCHECK
void udp_icmp __ARGS((int32 icsource, int32 ipsource,int32 ipdest,
char ictype,char iccode,struct mbuf **bpp));
/* In hop.c: */
void hop_icmp __ARGS((struct udp_cb *ucb, int32 icsource, int32 ipdest,
int16 udpdest, char ictype, char iccode));
#endif
  
/* In udpcmd.c: */
int st_udp __ARGS((struct udp_cb *udp,int n));
  
/* In udphdr.c: */
struct mbuf *htonudp __ARGS((struct udp *udp,struct mbuf *data,struct pseudo_header *ph));
int ntohudp __ARGS((struct udp *udp,struct mbuf **bpp));
int16 udpcksum __ARGS((struct mbuf *bp));
  
#endif  /* _UDP_H */
