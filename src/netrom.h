#ifndef _NETROM_H
#define _NETROM_H

#ifndef _MBUF_H
#include "mbuf.h"
#endif
  
#ifndef _IFACE_H
#include "iface.h"
#endif
  
#ifndef _AX25_H
#include "ax25.h"
#endif
  
#ifndef _NR4_H
#include "nr4.h"
#endif
  
/* If you want to support G8BPQ's 'advanced' handshaking,
 * and be recorgnized as such by other BPQ switches,
 * define the following - WG7J
 */
#define G8BPQ 1
  
/* net/rom support definitions
 * Copyright 1989 by Daniel M. Frank, W9NK.  Permission granted for
 * non-commercial distribution only.
 */
  
#define NR3HLEN     15  /* length of a net/rom level 3 hdr, */
#define NR3DLEN     241 /* max data size in net/rom l3 packet */
#define NR3NODESIG  0xff    /* signature for nodes broadcast */
#define NR3POLLSIG  0xfe    /* signature for route poll - WG7J */
#define NR3NODEHL   7   /* nodes bc header length */
  
#define NRNUMCHAINS 17  /* number of chains in the */
                /* neighbor and route hash tables */
#define NRRTDESTLEN 21  /* length of destination entry in */
                /* nodes broadcast */
#define NRDESTPERPACK   11  /* maximum number of destinations per */
                /* nodes packet */
  
/* NET/ROM protocol numbers */
#define NRPROTO_IP  0x0c
  
/* Internal representation of net/rom network layer header */
struct nr3hdr {
    char source[AXALEN] ;   /* callsign of origin node */
    char dest[AXALEN] ;     /* callsign of destination node */
    unsigned ttl ;              /* time-to-live */
} ;
  
/* Internal representation of net/rom routing broadcast destination */
/* entry */
struct nr3dest {
    char dest[AXALEN] ;     /* destination callsign */
    char alias[AXALEN] ;            /* ident, upper case ASCII, blank-filled */
    char neighbor[AXALEN] ; /* best-quality neighbor */
    unsigned quality ;      /* quality of route for this neighbor */
} ;
  
/* net/rom neighbor table structure */
struct nrnbr_tab {
    struct nrnbr_tab *next ;    /* doubly linked list pointers */
    struct nrnbr_tab *prev ;
    char call[AXALEN] ;         /* call of neighbor + 2 digis max */
    struct iface *iface ;
    unsigned refcnt ;           /* how many routes for this neighbor? */
    int32 lastsent;             /* time when last sent via this neigbour */
    unsigned tries;                /* Number of tries via this neighbour */
    unsigned retries;              /* Number of retries via this neighbour */

/* 22Aug2011, Maiko, Working on INP again after a few years away from it */
#ifdef INP2011
    unsigned char inp_state;	/* inp3 state */
    unsigned int rtt;		/* inp3 return-time */
#endif

};
  
#define NULLNTAB    (struct nrnbr_tab *)0
  
  
/* A list of these structures is provided for each route table */
/* entry.  They bind a destination to a neighbor node.  If the */
/* list of bindings becomes empty, the route table entry is    */
/* automatically deleted.                                      */
  
struct nr_bind {
    struct nr_bind *next ;      /* doubly linked list */
    struct nr_bind *prev ;
    unsigned quality ;      /* quality estimate */
    unsigned usage;         /* How many packets via this route ? */
    unsigned obsocnt ;      /* obsolescence count */
    unsigned flags ;
/* 22Aug2011, Maiko, Working on INP again after a few years away from it */
#ifdef INP2011
    unsigned tt;		/* inp3 target time */
    unsigned hops;		/* inp3 hop counter */
#endif
#define NRB_PERMANENT   0x01        /* entry never times out */
#define NRB_RECORDED    0x02        /* a "record route" entry */
    struct nrnbr_tab *via ;     /* route goes via this neighbor */
} ;
  
#define NULLNRBIND  (struct nr_bind *)0
  
  
/* net/rom routing table entry */
  
struct nrroute_tab {
    struct nrroute_tab *next ;  /* doubly linked list pointers */
    struct nrroute_tab *prev ;
    char alias[AXALEN] ;        /* alias of node */
    char call[AXALEN] ;         /* callsign of node */
    unsigned num_routes ;       /* how many routes in bindings list? */
    struct nr_bind *routes ;    /* list of neighbors */
#ifdef G8BPQ
    int flags;
#define G8BPQ_NODETTL   0x01
#define G8BPQ_NODERTT   0x02
#define G8BPQ_NODEMASK  0x03
    unsigned int hops;
    unsigned int irtt;
#endif
/* 22Aug2011, Maiko, Working on INP again after a few years away from it */
#ifdef INP2011
    unsigned int ltt;		/* inp3 last send target-time */
#endif
};
  
#define NULLNRRTAB  (struct nrroute_tab *)0
  
  
/* The net/rom nodes broadcast filter structure */
struct nrnf_tab {
    struct nrnf_tab *next ;     /* doubly linked list */
    struct nrnf_tab *prev ;
    char neighbor[AXALEN] ;     /* call of neighbor to filter */
    struct iface *iface ;       /* filter on this interface */
    unsigned quality;           /* explicit quality of this node */
} ;
  
#define NULLNRNFTAB (struct nrnf_tab *)0
  
/* Structure for handling raw NET/ROM user sockets */
struct raw_nr {
    struct raw_nr *prev;
    struct raw_nr *next;
  
    struct mbuf *rcvq;  /* receive queue */
    char protocol;      /* Protocol */
};
#define NULLRNR ((struct raw_nr *)0)
  
/* The neighbor hash table (hashed on neighbor callsign) */
extern struct nrnbr_tab *Nrnbr_tab[NRNUMCHAINS] ;
  
/* The routes hash table (hashed on destination callsign) */
extern struct nrroute_tab *Nrroute_tab[NRNUMCHAINS] ;
  
/* The nodes broadcast filter table */
extern struct nrnf_tab *Nrnf_tab[NRNUMCHAINS] ;
  
extern char Nr_nodebc[AXALEN];
  
/* filter modes: */
#define NRNF_NOFILTER   0   /* don't filter */
#define NRNF_ACCEPT 1   /* accept broadcasts from stations in list */
#define NRNF_REJECT 2   /* reject broadcasts from stations in list */
  
/* Emulate G8BPQ - WG7J */
extern int G8bpq;
  
/* The filter mode */
extern unsigned Nr_nfmode ;
  
/* The time-to-live for net/rom network layer packets */
extern unsigned short Nr_ttl ;
  
/* The quality threshhold below which routes in a broadcast will */
/* be ignored */
extern unsigned Nr_autofloor ;
  
/* Whether we want to broadcast the contents of our routing
 * table, or just our own callsign and alias:
 */
extern int Nr_verbose ;
  
/* The netrom pseudo-interface */
extern struct iface *Nr_iface ;

/* 04Oct2005, Maiko, New NETROM and INP3 debugging flag */
extern int Nr_debug;

/* Functions */
  
/* In nr3.c: */
void del_rnr __ARGS((struct raw_nr *rpp));
char *find_nralias __ARGS((char *));
struct nrroute_tab *find_nrroute __ARGS((char *));
struct nrnbr_tab *find_nrnbr __ARGS((char *, struct iface *));
/* 24Aug2011, Maiko, new function */
struct nrnbr_tab *add_nrnbr (char*, struct iface*);

void nr_bcnodes __ARGS((struct iface *ifp));
void nr_bcpoll __ARGS((struct iface *ifp));
void nr_nodercv __ARGS((struct iface *iface,char *source,struct mbuf *bp));
int nr_nfadd __ARGS((char *, struct iface *, unsigned));
int nr_nfdrop __ARGS((char *, struct iface *));
void nr_route __ARGS((struct mbuf *bp,struct ax25_cb *iaxp));

/* 24Aug2011, Maiko (VE4KLM), now returns PTR to active bind - for updates */
struct nr_bind *nr_routeadd __ARGS((char *, char *, struct iface *,
    unsigned, char *, unsigned, unsigned));

int nr_routedrop __ARGS((char *, char *, struct iface *));
int nr_send __ARGS((struct mbuf *bp,struct iface *iface,int32 gateway,int prec,
    int del,int tput,int rel));
void nr_sendraw __ARGS((char *dest,unsigned family,unsigned proto,
    struct mbuf *data));
void nr3output __ARGS((char *dest,struct mbuf *data));
int16 nrhash __ARGS((char *s));
struct raw_nr *raw_nr __ARGS((char));
struct nrroute_tab *find_nrboth __ARGS((char *alias,char *call));
void if_nrdrop __ARGS((struct iface *ifp));
  
/* In nrcmd.c: */
void donrdump __ARGS((struct nr4cb *cb));

/* 07Aug2018, Maiko (VE4KLM), added filter parameter to doroutedump */
int doroutedump __ARGS((char*));

int dorouteinfo __ARGS((int argc,char *argv[],void *p));
int putalias __ARGS((char *to, char *from,int complain));
  
/* In nrhdr.c: */
struct mbuf *htonnr3 __ARGS((struct nr3hdr *));
struct mbuf *htonnrdest __ARGS((struct nr3dest *));
int ntohnr3 __ARGS((struct nr3hdr *, struct mbuf **));
int ntohnrdest __ARGS((struct nr3dest *ds,struct mbuf **bpp));

/* 01Nov2005, Maiko (VE4KLM), New netrom table locks - nr3.c */

extern int conflicts_nrroute (void);	/* 03Nov2005, Maiko */
extern void lock_nrroute (void);
extern void unlock_nrroute (void);

#endif  /* _NETROM_H */
