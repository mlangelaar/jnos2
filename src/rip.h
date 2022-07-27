#ifndef _RIP_H
#define _RIP_H
  
/* Routing Information Protocol (RIP)
 *
 *  This code is derived from the 4.2 BSD version which was
 * used as a spec since no formal specification is known to exist.
 * See RFC 1009, Gateway Requirements, for more details. AGB 4-29-88
 *
 * The draft RIP RFC was used to develop most of this code. The above
 * referred to the basics of the rip_recv() function of RIP.C. The RIP
 * RFC has now been issued as RFC1058. AGB 7-23-88
 *
 * Substantially rewritten and integrated into NOS 9/1989 by KA9Q
 *
 * Mods by PA0GRI
 *
 * Rehack for RIP-2 (RFC1388) by N0POY 4/1993
 *
 * Beta release 8/12/93 V0.9
 *
 * 2/19/94 release V1.0
 *
 *
 */
  
#ifndef _MBUF_H
#include "mbuf.h"
#endif
  
#ifndef _IFACE_H
#include "iface.h"
#endif
  
#ifndef _UDP_H
#include "udp.h"
#endif
  
#define  RIP_VERSION_0           0
#define  RIP_VERSION_1           1
#define  RIP_VERSION_2           2
#ifdef RIP98
/* RIP 98 Mods -G4HIP */
/* RIP_VERSION_X should be one more than the latest 'official' RIP release */
#define  RIP_VERSION_X           3
#define  RIP_VERSION_98          98
#define  RIP_VERSIONS            4
#else
#define  RIP_VERSIONS            3
#endif
  
#define  RIP_METRIC_UNREACHABLE  16
#define  RIP_INFINITY            RIP_METRIC_UNREACHABLE
#define  RIP_METRIC_SHUTDOWN     (RIP_METRIC_UNREACHABLE - 1)
  
#define  RIP_AUTH_SIZE           16
  
#define  RIP_PKTSIZE             512
#define  RIP_HEADER              4
#define  RIP_ENTRY               20
#define  RIP98_ENTRY             6
#define  RIP_ADDR_MC             0xe0000009  /* 224.0.0.9 */
#define  RIP_PORT                520
#define  RIP_HOP                 1        /* Minimum hop count when passing through */
  
#define  RIP_AF_UNSPEC           0        /* Unknown family */
#define  RIP_AF_INET             2        /* IP Family */
#define  RIP_AF_AUTH             0xffff   /* Authetication header */
#define  RIP_NO_AUTH             "NONE"   /* No authentication */
  
#define  RIP_AUTH_NONE           0
#define  RIP_AUTH_SIMPLE         2
#define  RIP_TTL                 240      /* Default time-to-live for an entry */
#define  RIP98_MAX_FRAME         30  
/*
 * Packet types.
 */
  
#define  RIPCMD_REQUEST          1        /* want info */
#define  RIPCMD_RESPONSE         2        /* responding to request */
#define  RIPCMD_TRACEON          3        /* turn tracing on */
#define  RIPCMD_TRACEOFF         4        /* turn it off */
#define  RIPCMD_POLL             5        /* like request, but anyone answers */
#define  RIPCMD_POLLENTRY        6        /* like poll, but for entire entry */
#define  RIPCMD_MAX              7
  
  
/* RIP Flags */
  
#define  RIP_SPLIT               0x01     /* Do split horizon processing */
#define  RIP_US                  0x02     /* Include ourselves in the list */
#define  RIP_BROADCAST           0x04     /* Broadcast RIP packets */
#define  RIP_MULTICAST           0x08     /* Multicast RIP packets */
#define  RIP_POISON              0x10     /* Poisoned reverse on */
#define  RIP_AUTHENTICATE        0x20     /* Authenticate each packet */
  
  
/* RIP statistics counters */
struct rip_stat {
    struct version_data {
        int32 output;        /* Packets sent */
        int32 rcvd;          /* Packets received */
        int32 request;       /* Number of request packets received */
        int32 response;      /* Number of responses received */
        int32 unknown;       /* Number of unknown command pkts received */
    } vdata[RIP_VERSIONS];
    int32 version;          /* Number of version errors */
    int32 addr_family;      /* Number of address family errors */
    int32 refusals;         /* Number of packets dropped from a host
                              on the refuse list */
    int32 wrong_domain;     /* Refused due to wrong domain for interface */
    int32 auth_fail;        /* Authentication failures */
    int32 unknown_auth;     /* Unknown authentication type */
};
  
struct rip_list {
    struct rip_list *prev;
    struct rip_list *next;   /* doubly linked list */
  
   /* address to scream at periodically:
    * this address must have a direct network interface route and an
    * ARP entry for the appropriate  hardware broadcast address, if approp.
    */
    int32 dest;
  
   /* basic rate of RIP clocks on this net interface */
    int32 interval;
  
    struct timer rip_time;     /* time to output next on this net. iface */
  
    /* the interface to transmit on  and receive from */
    struct iface *iface;
    char  rip_version;         /* Type of RIP packets */
    int16 flags;
    int16 domain;              /* Routing domain in */
    int16 route_tag;           /* Route tag to send */
    int32 proxy_route;         /* For Proxy RIP-2, 0 if none */
    char rip_auth_code[RIP_AUTH_SIZE+1];
                              /* Authentication code to send */
};
#define  NULLRL   (struct rip_list *)0
  
struct rip_refuse {
    struct rip_refuse *prev;
    struct rip_refuse *next;
    int32    target;
};
#define  NULLREF  (struct rip_refuse *)0
  
struct rip_auth {
    struct rip_auth *prev;
    struct rip_auth *next;
    char *ifc_name;            /* Name of the interface */
    int16 domain;              /* Check against valid routing domain */
    char rip_auth_code[RIP_AUTH_SIZE+1];
                              /* Authentication password accepted */
};
#define  NULLAUTH (struct rip_auth *)0
#define  DEFAULTIFC     "DEFAULT"
  
/* Host format for the RIP-II header */
  
struct rip_head {
    unsigned char rip_cmd;
    unsigned char rip_vers;
    unsigned short rip_domain;
};
  
/* Host format of a single entry in a RIP response packet */
struct rip_route {
    int16 rip_family;
    int16 rip_tag;
    int32 rip_dest;
    int32 rip_dest_mask;
    int32 rip_router;
    int32 rip_metric;
};
  
/* Host format of an authentication packet */
struct rip_authenticate {
    int16 rip_family;
    int16 rip_auth_type;
    char  rip_auth_str[RIP_AUTH_SIZE];
};
  
/* RIP primitives */
  
int rip_init __ARGS((void));
void rt_timeout __ARGS((void *s));
void rip_trigger __ARGS((void));
int rip_add __ARGS((int32 dest,int32 interval,char flags, char version,
char authpass[RIP_AUTH_SIZE], int16 domain, int16 route_tag, int32 proxy));
int riprefadd __ARGS((int32 gateway));
int riprefdrop __ARGS((int32 gateway));
int ripreq __ARGS((int32 dest,int16 replyport,int16 version));
int rip_drop __ARGS((int32 dest,int16 domain));
int ripauthdrop __ARGS((char *ifcname,int16 domain));
int ripauthadd __ARGS((char *ifcname,int16 domain,char *password));
int nbits __ARGS((int32 target, int32 ifaddr, int32 ifnetmask));
void pullentry __ARGS((struct rip_route *ep,struct mbuf **bpp));
#ifdef RIP98
void pull98entry __ARGS((struct rip_route *ep,struct mbuf **bpp));
#endif
void rip_shout __ARGS((void *p));
  
/* RIP Definition */
  
#ifdef RIP98
extern int Rip98allow;
#endif
extern int16 Rip_trace;
extern FILE *Rip_trace_file;
extern char *Rip_trace_fname;
extern int Rip_merge;
extern int32 Rip_ttl;
extern int16 Rip_ver_refuse;
extern int Rip_default_refuse;
extern struct rip_stat Rip_stat;
extern struct rip_list *Rip_list;
extern struct rip_refuse *Rip_refuse;
extern struct rip_auth *Rip_auth;
extern struct udp_cb *Rip_cb;
  
#endif  /* _RIP_H */
  
