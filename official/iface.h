#ifndef _IFACE_H
#define _IFACE_H
  
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
#ifndef _MBUF_H
#include "mbuf.h"
#endif
  
#ifndef _PROC_H
#include "proc.h"
#endif

/*
 * 29Sep2019, Maiko (VE4KLM), this is here to prevent
 * the 'struct iface' declared inside parameter list'
 * compiler warnings, but I would rather just define
 * the structure before even referring to it, no ?
 *
 * But in this case we can't do that, we need to use
 * a forward declaration since both iface and iftype
 * structs are intermingled with each other, ugh !
 *
 * Why '#ifdef __GNUC__' - in case of DOS compile ?
 *
 */

struct iface;	/* forward declaration for gcc */

/*
 * 15Nov08, Maiko (VE4KLM), Headers in NEW baycom.c screw up without this
 * 05Dec08, Maiko, Oops, only for BAYCOM module - new definition
 * 29Sep2019, Maiko, This use of BAYCOM_MODULE is rediculous, get rid of it
 *
 * #ifndef BAYCOM_MODULE
 */
  
#ifndef _IFTCP_H
// 29Sep2019, Maiko (VE4KLM), we only need a few things, not the entire tcp.h !
#include "iftcp.h"
#endif
  
#ifdef AX25
#ifndef _IFAX25_H
// 29Sep2019, Maiko (VE4KLM), we only need a few things, not the entire ax25.h !
#include "ifax25.h"
#endif
#endif
 
/* 
 * End of BAYCOM_MODULE
 * #endif
 */

/*
 * Interface encapsulation mode table entry. An array of these structures
 * are initialized in config.c with all of the information necessary to
 * attach a device.
 */
  
struct iftype {
    char *name;     /* Name of encapsulation technique */
    int (*send) __ARGS((struct mbuf *,struct iface *,int32,int,int,int,int));
                /* Routine to send an IP datagram */
    int (*output) __ARGS((struct iface *,char *,char *,int16,struct mbuf *));
                /* Routine to send link packet */
    char *(*format) __ARGS((char *,char *));
                /* Function that formats addresses */
    int (*scan) __ARGS((char *,char *));
                /* Reverse of format */
    int type;       /* Type field for network process */
    int hwalen;     /* Length of hardware address, if any */
};

#define NULLIFT (struct iftype *)0

extern struct iftype Iftypes[];
  
/* Interface control structure */
struct iface {
    struct iface *next; /* Linked list pointer */
    char *name;     /* Ascii string with interface name */
    char *descr;    /* Description of interface */
  
    int32 addr;     /* IP address */
    int32 broadcast;    /* Broadcast address */
    int32 netmask;      /* Network mask */
  
    int16 mtu;      /* Maximum transmission unit size */
  
    int32 flags;        /* Configuration flags */
#define DATAGRAM_MODE   0   /* Send datagrams in raw link frames */
#define CONNECT_MODE    1   /* Send datagrams in connected mode */
#define IS_NR_IFACE     2   /* Activated for NET/ROM - WG7J */
#define NR_VERBOSE      4   /* NET/ROM broadcast is verbose - WG7J */
#define IS_CONV_IFACE   8   /* Activated for conference call access - WG7J */
#define AX25_BEACON     16  /* Broadcast AX.25 beacons */
#define MAIL_BEACON     32  /* Send MAIL beacons */
#define HIDE_PORT       64  /* Don't show port in mbox P command */
#define AX25_DIGI       128 /* Allow digipeating */
#define ARP_EAVESDROP   256 /* Listen to ARP replies */
#define ARP_KEEPALIVE   512 /* Keep arp entries alive after timeout */
#define LOG_AXHEARD    1024 /* Do ax.25 heard logging on this interface */
#define LOG_IPHEARD    2048 /* Do IP heard logging on this interface */
#define NO_AX25        4096 /* No ax.25 mbox connections on this port */
#define BBS_ONLY       8192 /* BBS's only in mbox via this port */
#define USERS_ONLY     16384 /* Users only on this port */
#define SYSOP_ONLY     32768 /* Sysops only on this port */
  
#ifdef NETROM
    int quality;            /* Netrom interface quality */
    int nr_autofloor;       /* Netrom minimum route quality for broadcast */
#endif
  
    int16 trace;        /* Trace flags */
#define IF_TRACE_OUT    0x01    /* Output packets */
#define IF_TRACE_IN 0x10    /* Packets to me except broadcast */
#define IF_TRACE_ASCII  0x100   /* Dump packets in ascii */
#define IF_TRACE_HEX    0x200   /* Dump packets in hex/ascii */
#define IF_TRACE_NOBC   0x1000  /* Suppress broadcasts */
#define IF_TRACE_RAW    0x2000  /* Raw dump, if supported */
#define IF_TRACE_POLLS  0x4000  /* Include polls in pkiss output trace */
    char *trfile;       /* Trace file name, if any */
    int trsock;         /* Socket to trace to - WG7J */

#ifdef J2_TRACESYNC
	/*
	 * 05Nov2010, Maiko, I don't like adding stuff to key structures since it
	 * means a mass compile is required, but it makes the most sense to have
	 * the trace file sync timer struct and interval value in here. It gives
	 * me alot more flexibility on what can be done, and simplifies the code.
	 */
	struct timer trsynct;
	uint32 trsynci;
#endif

    struct iface *forw; /* Forwarding interface for output, if rx only */
#ifdef RXECHO  
    struct iface *rxecho;       /* Echo received packets here - WG7J */ 
#endif

    struct proc *rxproc;    /* Receiver process, if any */
    struct proc *txproc;    /* Transmitter process, if any */
    struct proc *supv;  /* Supervisory process, if any */
  
    /* Device dependant */
    int dev;        /* Subdevice number to pass to send */
                /* To device -- control */
    int32 (*ioctl) __ARGS((struct iface *,int cmd,int set,int32 val));
                /* From device -- when status changes */
    int (*iostatus) __ARGS((struct iface *,int cmd,int32 val));
                /* Call before detaching */
    int (*stop) __ARGS((struct iface *));
    char *hwaddr;       /* Device hardware address, if any */
  
    /* Encapsulation dependant */
#ifdef AX25
    struct ifax25 *ax25;    /* Pointer to ax.25 protocol structure */
#endif
    struct iftcp *tcp;      /* Tcp protocol variables */
    void *edv;      /* Pointer to protocol extension block, if any */
    int type;       /* Link header type for phdr */
    int xdev;       /* Associated Slip or Nrs channel, if any */
    int port;       /* Sub port for multy port kiss */
    struct iftype *iftype;  /* Pointer to appropriate iftype entry */

#ifdef ENCAP
	int	protocol;	/* ip protocol - 12Oct2004, Maiko, IPUDP Support */
#endif
            /* Encapsulate an IP datagram */
    int (*send) __ARGS((struct mbuf *,struct iface *,int32,int,int,int,int));
            /* Encapsulate any link packet */
    int (*output) __ARGS((struct iface *,char *,char *,int16,struct mbuf *));
            /* Send raw packet */
    int (*raw)      __ARGS((struct iface *,struct mbuf *));
            /* Display status */
    void (*show)        __ARGS((struct iface *));
  
    int (*discard)      __ARGS((struct iface *,struct mbuf *));
    int (*echo)     __ARGS((struct iface *,struct mbuf *));
  
    /* Counters */
    int32 ipsndcnt;     /* IP datagrams sent */
    int32 rawsndcnt;    /* Raw packets sent */
    int32 iprecvcnt;    /* IP datagrams received */
    int32 rawrecvcnt;   /* Raw packets received */
    int32 lastsent;     /* Clock time of last send */
    int32 lastrecv;     /* Clock time of last receive */

#ifdef J2_SNMPD_VARS
	/*
	 * 31Jan2011, Maiko (VE4KLM), Again, I don't like adding stuff to a key
	 * structure like this one, since it means a mass compile is required, but
	 * it only makes the most sense to have these SNMP counters in here.
	 */
    int32 rawsndbytes;   /* Raw bytes sent so far on this interface */
    int32 rawrecbytes;   /* Raw bytes received so far on this interface */
#endif

};

#define NULLIF  (struct iface *)0
extern struct iface *Ifaces;    /* Head of interface list */
extern struct iface  Loopback;  /* Optional loopback interface */
extern struct iface  Encap; /* IP-in-IP pseudo interface */
  
/* Header put on front of each packet in input queue */
struct phdr {
    struct iface *iface;
    unsigned short type;    /* Use pktdrvr "class" values */
};
  
/* Header put on front of each packet sent to an interface */
struct qhdr {
    char tos;
    int32 gateway;
};
  
extern char Noipaddr[];
extern struct mbuf *Hopper;
  
/* In iface.c: */
struct iface *if_lookup (char*);

/*
 * 30Aug2010, Maiko, Support functions for modified sockaddr_ax structure
 * 03Sep2010, Maiko, Structure Byte Alignment issue forced me to change
 * the index field to a char
 */
#ifdef JNOS20_SOCKADDR_AX
char if_indexbyname (char*);
struct iface *if_lookup2 (char);
#endif

struct iface *ismyaddr __ARGS((int32 addr));
int if_detach __ARGS((struct iface *ifp));
int setencap __ARGS((struct iface *ifp,char *mode));
char *if_name __ARGS((struct iface *ifp,char *comment));
int bitbucket __ARGS((struct iface *ifp,struct mbuf *bp));
int mask2width __ARGS((int32 mask));         /* Added N0POY, for rip code */
void removetrace __ARGS((void));  

/* In config.c: */
int net_route __ARGS((struct iface *ifp,int type,struct mbuf *bp));
  
#endif  /* _IFACE_H */
