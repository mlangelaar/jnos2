/* Mods by PA0GRI (WFM IP access code ) */
#ifndef _IP_H
#define _IP_H
  
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
#ifndef _MBUF_H
#include "mbuf.h"
#endif
  
#ifdef __GNUC__
struct ip;         /* forward reference, needed by GNU C for iface.h */
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif
  
#ifndef _INTERNET_H
#include "internet.h"
#endif
  
#ifndef _TIMER_H
#include "timer.h"
#endif
  
#define TLB     30  /* Default reassembly timeout, sec */
#define IPVERSION   4
#define IP_CS_OLD   1   /* use saved checksum */
#define IP_CS_NEW   0   /* calculate checksum */
  
extern char Hashtab[];  /* Modulus lookup table */
  
/* SNMP MIB variables, used for statistics and control. See RFC 1066 */
extern struct mib_entry Ip_mib[];
#define ipForwarding        Ip_mib[1].value.integer
#define ipDefaultTTL        Ip_mib[2].value.integer
#define ipInReceives        Ip_mib[3].value.integer
#define ipInHdrErrors       Ip_mib[4].value.integer
#define ipInAddrErrors      Ip_mib[5].value.integer
#define ipForwDatagrams     Ip_mib[6].value.integer
#define ipInUnknownProtos   Ip_mib[7].value.integer
#define ipInDiscards        Ip_mib[8].value.integer
#define ipInDelivers        Ip_mib[9].value.integer
#define ipOutRequests       Ip_mib[10].value.integer
#define ipOutDiscards       Ip_mib[11].value.integer
#define ipOutNoRoutes       Ip_mib[12].value.integer
#define ipReasmTimeout      Ip_mib[13].value.integer
#define ipReasmReqds        Ip_mib[14].value.integer
#define ipReasmOKs      Ip_mib[15].value.integer
#define ipReasmFails        Ip_mib[16].value.integer
#define ipFragOKs       Ip_mib[17].value.integer
#define ipFragFails     Ip_mib[18].value.integer
#define ipFragCreates       Ip_mib[19].value.integer
  
#define NUMIPMIB    19
  
/* IP header, INTERNAL representation */
#define IPLEN       20  /* Length of standard IP header */
#define IP_MAXOPT   40  /* Largest option field, bytes */
struct ip {
    int32 source;       /* Source address */
    int32 dest;     /* Destination address */
    int16 length;       /* Total length */
    int16 id;       /* Identification */
    int16 offset;       /* Fragment offset in bytes */
    int16 checksum;     /* Header checksum */
  
    struct {
        char congest;   /* Congestion experienced bit (exp) */
        char df;    /* Don't fragment flag */
        char mf;    /* More Fragments flag */
    } flags;
  
    char version;       /* IP version number */
    char tos;       /* Type of service */
    char ttl;       /* Time to live */
    char protocol;      /* Protocol */
    char optlen;        /* Length of options field, bytes */
    char options[IP_MAXOPT];/* Options field */
};
#define NULLIP  (struct ip *)0
  
/* Fields in option type byte */
#define OPT_COPIED  0x80    /* Copied-on-fragmentation flag */
#define OPT_CLASS   0x60    /* Option class */
#define OPT_NUMBER  0x1f    /* Option number */
  
/* IP option numbers */
#define IP_EOL      0   /* End of options list */
#define IP_NOOP     1   /* No Operation */
#define IP_SECURITY 2   /* Security parameters */
#define IP_LSROUTE  3   /* Loose Source Routing */
#define IP_TIMESTAMP    4   /* Internet Timestamp */
#define IP_RROUTE   7   /* Record Route */
#define IP_STREAMID 8   /* Stream ID */
#define IP_SSROUTE  9   /* Strict Source Routing */
  
/* Timestamp option flags */
#define TS_ONLY     0   /* Time stamps only */
#define TS_ADDRESS  1   /* Addresses + Time stamps */
#define TS_PRESPEC  3   /* Prespecified addresses only */
  
/* IP routing table entry */
struct route {
    struct route *prev; /* Linked list pointers */
    struct route *next;
    int32 target;       /* Target IP address */
    unsigned int bits;  /* Number of significant bits in target */
    int32 gateway;      /* IP address of local gateway for this target */
    int32 metric;       /* Hop count or whatever */
#ifdef ENCAP
	int	protocol;		/* 12Oct2004, Maiko, IPUDP Support (K2MF) */
#endif
    struct iface *iface;    /* Device interface structure */
    int flags;
#define RTPRIVATE   0x1 /* Should the world be told of this route ? */
#define RTTRIG  0x2     /* Trigger is pending for this route */
#ifdef DYNGWROUTES
#define RTDYNGWROUTE 0x4 /* From 04Apr2008, N8CML (Ron) - New : dyngw flag */
#endif
    struct timer timer; /* Time until aging of this entry */
    int32 uses;     /* Usage count */
#ifdef RIP
    int16 route_tag;    /* Tag used by RIP-2, N0POY 4/93 */
#endif
};
#define NULLROUTE   (struct route *)0
extern struct route *Routes[32][HASHMOD];   /* Routing table */
extern struct route R_default;          /* Default route entry */
  
/* IP access header entry  (WFM) */
struct rtaccess{
    struct rtaccess *nxtiface;  /* Linked list pointer */
    struct rtaccess *nxtbits;
    int16 status;       /* 0=permit, 1=deny */
    int16 protocol;     /* 0=any, otherwise IP protocol # */
    int32 source;       /* Source IP address */
    unsigned int sbits; /* Number of significant bits in source */
    int32 target;       /* Target IP address */
    unsigned int bits;  /* Number of significant bits in target */
    struct iface *iface;    /* Device interface structure */
    int16 lowport;      /* tcp & udp port range. low=0 implies all */
    int16 highport;
#ifdef TCPACCESS_EXPIRY
    int32 age;		/*
			 * 12Feb2015, Maiko (VE4KLM), ability to expire
			 * the entry (for my blacklist feature), set from
			 * returned value of secclock () function.
			 */
	// #warning "rtaccess now contains age (expiry) information"
#endif
};
#define NULLACCESS  (struct rtaccess *)0
extern struct rtaccess *IPaccess;
/* end WFM access control */

/* Cache for the last-used routing entry, speeds up the common case where
 * we handle a burst of packets to the same destination
 */
struct rt_cache {
    int32 target;
    struct route *route;
};

/* Reassembly descriptor */
struct reasm {
    struct reasm *next; /* Linked list pointer */
    struct timer timer; /* Reassembly timeout timer */
    struct frag *fraglist;  /* Head of data fragment chain */
    int16 length;       /* Entire datagram length, if known */
    int32 source;       /* src/dest/id/protocol uniquely describe a datagram */
    int32 dest;
    int16 id;
    char protocol;
};
#define NULLREASM   (struct reasm *)0
  
/* Fragment descriptor in a reassembly list */
struct frag {
    struct frag *prev;  /* Previous fragment on list */
    struct frag *next;  /* Next fragment */
    struct mbuf *buf;   /* Actual fragment data */
    int16 offset;       /* Starting offset of fragment */
    int16 last;     /* Ending offset of fragment */
};
#define NULLFRAG    (struct frag *)0
  
extern struct reasm *Reasmq;    /* The list of reassembly descriptors */
  
/* Structure for handling raw IP user sockets */
struct raw_ip {
    struct raw_ip *next;    /* Linked list pointer */
  
    struct mbuf *rcvq;  /* receive queue */
    void (*r_upcall) __ARGS((struct raw_ip *));
    int protocol;       /* Protocol */
    int user;       /* User linkage */
};
#define NULLRIP ((struct raw_ip *)0)
extern struct raw_ip *Raw_ip;
  
/* Transport protocol link table */
struct iplink {
    char proto;
    void (*funct) __ARGS((struct iface *,struct ip *,struct mbuf *,int));
};
extern struct iplink Iplink[];
extern int Tcp_interact[];
  
/* IP heard structure */
struct iph {
    struct iph *next;
    int32 addr;
    struct iface *iface;
    long count;
    long time;
};
#define NULLIPH (struct iph *)0
extern struct iph *Iph;
#define MAXIPHEARD 16
  
/* In ip.c: */
void ip_garbage __ARGS((int drastic));
void ip_recv __ARGS((struct iface *iface,struct ip *ip,struct mbuf *bp,
int rxbroadcast));
void ipip_recv __ARGS((struct iface *iface,struct ip *ip,struct mbuf *bp,
int rxbroadcast));
int ip_send __ARGS((int32 source,int32 dest,char protocol,char tos,char ttl,
struct mbuf *bp,int16 length,int16 id,char df));
struct raw_ip *raw_ip __ARGS((int protocol,void (*r_upcall) __ARGS((struct raw_ip *)) ));
void del_ip __ARGS((struct raw_ip *rrp));
  
/* In iproute.c: */
void ipinit __ARGS((void));
int16 ip_mtu __ARGS((int32 addr));
int ip_encap __ARGS((struct mbuf *bp,struct iface *iface,int32 gateway,
int prec,int del,int tput,int rel));
void ip_proc __ARGS((struct iface *ifp,struct mbuf *bp));
int ip_route __ARGS((struct iface *i_iface,struct mbuf *bp,int rxbroadcast));
int32 locaddr __ARGS((int32 addr));
void rt_merge __ARGS((int trace));
struct route *rt_add __ARGS((int32 target,unsigned int bits,int32 gateway,
struct iface *iface,int32 metric,int32 ttl,char private));
int rt_drop __ARGS((int32 target,unsigned int bits));
struct route *rt_lookup __ARGS((int32 target));
struct route *rt_blookup __ARGS((int32 target,unsigned int bits));
int ip_check __ARGS((struct rtaccess *accptr,int16 protocol,
int32 src,int32 dest,int16 port));
void addaccess __ARGS((int16 protocol,int32 source,unsigned int sbits,
int32 target,unsigned int tbits,struct iface *ifp,
int16 low,int16 high,int16 permit));
  
/* In iphdr.c: */
int16 cksum __ARGS((struct pseudo_header *ph,struct mbuf *m,int16 len));
int16 eac __ARGS((int32 sum));
struct mbuf *htonip __ARGS((struct ip *ip,struct mbuf *data,int cflag));
int ntohip __ARGS((struct ip *ip,struct mbuf **bpp));
  
/* In either lcsum.c or pcgen.asm: */
int16 lcsum __ARGS((int16 *wp,int16 len));
  
/* In ipcmd.c */
void log_ipheard __ARGS((int32 addr, struct iface *iface));
  
#endif /* _IP_H */
