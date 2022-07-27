#ifndef RSPFPKTLEN
struct rspfpacketh {
    char version;
    char type;
    unsigned char fragn;
    unsigned char fragtot;
    int16 csum;
    unsigned char sync;
    unsigned char nodes;
    int16 envid;
};
#define RSPFPKTLEN  10
struct rspfnodeh {
    int32 addr;
    short seq;          /* 16 bit signed int */
    unsigned char subseq;
    unsigned char links;
};
#define RSPFNODELEN 8
struct rspflinkh {
    unsigned char horizon;
    unsigned char erp;
    unsigned char cost;
    unsigned char adjn;
};
#define RSPFLINKLEN 4
struct rspfrouter {
    struct rspfrouter *next;
    char sent;          /* True if the data has already been sent */
    char subseq;        /* Sub-sequence number of latest update */
    int32 time;         /* Time when data was last received */
    struct mbuf *data;      /* Latest data, starting with node header */
};
#define NULLRROUTER (struct rspfrouter *)0
struct rspfreasm {
    struct rspfreasm *next;
    int32 addr;         /* Address of originating station */
    int32 time;         /* Time when a fragment was last received */
    struct mbuf *data;      /* A queue of fragments */
};
#define NULLRREASM  (struct rspfreasm *)0
  
#define RSPF_RTIME  30      /* Reassembly timeout in seconds */
  
/* RRH header, host format */
struct rrh {
    char version;
    char type;
    int16 csum;
    int32 addr;
    int16 seq;
    char flags;
#define RSPFMODE    1   /* Tells whether we want a VC link or not */
};
#define RRHLEN      11
union rspf {
    struct {
        char version;
#define RSPF_VERSION    21      /* Current version */
        char type;
#define RSPF_FULLPKT    1       /* Routing update */
#define RSPF_RRH    3       /* Router-Router Hello */
    } hdr;
    struct rspfpacketh pkthdr;
    struct rrh rrh;
};
  
#define RSPF_PROCMAX    5       /* Maximum number of processes handling
                       suspect adjacencies */
  
struct rspfadj {
    struct rspfadj *next;
    int32 addr;
    unsigned char cost;     /* Cost to reach this adjacency */
    int16 seq;          /* Number of AX.25 frames it has sent */
    int32 heard;        /* Number of heard AX.25 frames */
    struct timer timer;     /* Adjacency turns suspect if timer expires */
    char tos;           /* Preferred type of service */
    char added;         /* Used by the SPF algorithm */
    void *scratch;      /* also used by SPF (contains the interface) */
    struct proc *pinger;    /* Pointer to rspfping process */
    char state;
#define RSPF_TENTATIVE  0
#define RSPF_OK     1
#define RSPF_SUSPECT    2
#define RSPF_BAD    3
    int okcnt;          /* Times adjacency has entered OK state */
};
#define NULLADJ (struct rspfadj *)0
  
struct rspfiface {
    struct rspfiface *next;
    struct iface *iface;
    unsigned char quality;  /* Default quality for this interface */
    unsigned char horizon;  /* Default horizon value */
    unsigned char erp;      /* Default ERP factor */
};
#define NULLRIFACE (struct rspfiface *)0
  
struct rspf_stat {
    unsigned rrhin;        /* RRH's received */
    unsigned rrhout;       /* RRH's sent */
    unsigned updatein;     /* Updates received */
    unsigned updateout;    /* Updates sent */
    unsigned badcsum;      /* Bad checksums */
    unsigned badvers;      /* Bad versions */
    unsigned norspfiface;  /* RSPF packets from non RSPF interfaces */
    unsigned oldreport;    /* Node headers with old sequence numbers */
    unsigned outpolls;     /* Poll packets sent */
    unsigned noadjupdate;  /* Updates received from non-adjacencies */
};
  
/* Event types in main loop */
#define RSPFE_RRH   1   /* It is time to send a new RRH */
#define RSPFE_CHECK 2   /* An adjacency has becomed suspect */
#define RSPFE_UPDATE    3   /* Time to send a new routing updates */
#define RSPFE_ARP   4   /* An ARP reply was received */
#define RSPFE_PACKET    5   /* A packet was received (RRH or Update) */
  
extern struct rspf_stat Rspf_stat;
extern struct rspfreasm *Rspfreasmq;
extern struct rspfiface *Rspfifaces;
extern struct rspfadj *Adjs;
extern struct rspfrouter *Rspfrouters;
extern struct mbuf *Rspfinq;
extern struct timer Rspfreasmt, Susptimer;
extern char *Rrh_message;
extern int Rspfownmode;
extern unsigned short Rspfpingmax;
  
#ifdef __GNUC__
struct ip;            /* forward declarations */
struct pseudo_header;
#endif
  
void rspfmain __ARGS((int v,void *v1,void *v2));
void rspf_input __ARGS((struct iface *iface,struct ip *ip,struct mbuf *bp,int rxbroadcast));
void rspfarpupcall __ARGS((int32 addr,int16 hardware,struct iface *iface));
void rspfrouteupcall __ARGS((int32 addr,unsigned bits,int32 gateway));
void rspfevent __ARGS((void *t));
void rspfsuspect __ARGS((void *t));
struct mbuf *makeownupdate __ARGS((int32 dest,int new));
int ntohrspf __ARGS((union rspf *rspf,struct mbuf **bpp));
int ntohrspfnode __ARGS((struct rspfnodeh *nodeh,struct mbuf **bpp));
int ntohrspflink __ARGS((struct rspflinkh *linkh,struct mbuf **bpp));
struct mbuf *htonrrh __ARGS((struct rrh *rrh,struct mbuf *data,struct pseudo_header *ph));
struct mbuf *htonrspf __ARGS((struct rspfpacketh *pkth,struct mbuf *data));
struct mbuf *htonrspfnode __ARGS((struct rspfnodeh *nodeh,struct mbuf *data));
struct mbuf *htonrspflink __ARGS((struct rspflinkh *linkh,struct mbuf *data));
void rspfnodedump __ARGS((int s,struct mbuf **bpp,int adjcnt));
#endif
