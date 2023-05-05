#ifndef _DOMAIN_H
#define _DOMAIN_H
  
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
#ifndef _MBUF_H
#include "mbuf.h"
#endif
  
#ifndef _PROC_H
#include "proc.h"
#endif

#include "sockaddr.h"	/* 23Mar2021, Maiko (VE4KLM), Need this for ntohdomain() prototype */

#include "ipv6.h"	/* 10Apr2023, Maiko */
  
#define MAXCNAME    10  /* Maximum amount of cname recursion */
  
#define TYPE_A      1   /* Host address */
#define TYPE_NS     2   /* Name server */
#define TYPE_MD     3   /* Mail destination (obsolete) */
#define TYPE_MF     4   /* Mail forwarder (obsolete) */
#define TYPE_CNAME  5   /* Canonical name */
#define TYPE_SOA    6   /* Start of Authority */
#define TYPE_MB     7   /* Mailbox name (experimental) */
#define TYPE_MG     8   /* Mail group member (experimental) */
#define TYPE_MR     9   /* Mail rename name (experimental) */
#define TYPE_NULL   10  /* Null (experimental) */
#define TYPE_WKS    11  /* Well-known sockets */
#define TYPE_PTR    12  /* Pointer record */
#define TYPE_HINFO  13  /* Host information */
#define TYPE_MINFO  14  /* Mailbox information (experimental)*/
#define TYPE_MX     15  /* Mail exchanger */
#define TYPE_TXT    16  /* Text strings */

#ifdef	IPV6
/*
 * 10Apr2023, Maiko (VE4KLM), Adding IPV6 support
 *  (rfc 3596 (Oct 2003), obsoletes 3152, 1886)
 */
#define TYPE_AAAA   28   /* IPV6 Host address */
#endif

#define TYPE_AXFR   252 /* Transfer zone of athority */
#define TYPE_MAILB  253 /* Transfer mailbox records */
#define TYPE_MAILA  254 /* Transfer mail agent records */
#define TYPE_ANY    255 /* Matches any type */
  
#define CLASS_IN    1   /* The ARPA Internet */
#define CLASS_CH    3   /* The CHAOS net at MIT */
#define CLASS_ANY   255 /* wildcard match */
  
/* Buffer size for domain nameserver replies.  It must be large enough
 * to insure that the domain nameserver stack will not be violated.
 * - K2MF */
#define DNSBUFLEN     4096

struct dserver {

    struct dserver *prev;   /* Linked list pointers */
    struct dserver *next;

#ifdef	IPV6
	/*
	 * 10Apr2023, Maiko (VE4KLM), Adding IPV6 support
	 *  (rfc 3596 (Oct 2003), obsoletes 3152, 1886)
	 */
	char ipver;
	unsigned char *address6;	/* IPV6 address of server */
#endif
  
    int32 address;      /* IP address of server */
    int16 port;         /* TCP port for request */
    int32 timeout;      /* Current timeout, ticks */
    int32 srtt;     /* Smoothed round trip time, ticks */
    int32 mdev;     /* Mean deviation, ticks */
    int32 queries;      /* Query packets sent to this server */
    int32 responses;    /* Response packets received from this server */
    int32 timeouts;     /* Query timeouts (misses) */
};
#define NULLDOM (struct dserver *)0
extern struct dserver *Dlist;
extern int Dsocket;     /* Socket to use for domain queries */
  
/* Round trip timing parameters */
#define AGAIN   8   /* Average RTT gain = 1/8 */
#define LAGAIN  3   /* Log2(AGAIN) */
#define DGAIN   4   /* Mean deviation gain = 1/4 */
#define LDGAIN  2   /* log2(DGAIN) */
  
#define DOM_RESPONSE    0x8000
#define DOM_AUTHORITY   0x0400
#define DOM_TRUNC   0x0200
#define DOM_DORECURSE   0x0100
#define DOM_CANRECURSE  0x0080
  
/* Header for all domain messages */
struct dhdr {
    int16 id;       /* Identification */
    char qr;        /* Query/Response */
#define QUERY       0
#define RESPONSE    1
    char opcode;
#define QUERY       0
#define IQUERY      1
#define DOMSTATUS   2
#define ZONEINIT    14
#define ZONEREF     15
    char aa;        /* Authoratative answer */
    char tc;        /* Truncation */
    char rd;        /* Recursion desired */
    char ra;        /* Recursion available */
    char rcode;     /* Response code */
#define NO_ERROR    0
#define FORMAT_ERROR    1
#define SERVER_FAIL 2
#define NAME_ERROR  3
#define NOT_IMPL    4
#define REFUSED     5
    int16 qdcount;      /* Question count */
    int16 ancount;      /* Answer count */
    int16 nscount;      /* Authority (name server) count */
    int16 arcount;      /* Additional record count */
    struct rr *questions;   /* List of questions */
    struct rr *answers; /* List of answers */
    struct rr *authority;   /* List of name servers */
    struct rr *additional;  /* List of additional records */
};
  
struct mx {
    int16 pref;
    char *exch;
};
  
struct hinfo {
    char *cpu;
    char *os;
};
  
struct minfo {
    char *rmailbx;
    char *emailbx;
};
  
struct wks {
    int32 addr;
    unsigned char protocol;
    unsigned char bitmap[64];
};
  
struct soa {
    char *mname;
    char *rname;
    int32 serial;
    int32 refresh;
    int32 retry;
    int32 expire;
    int32 minimum;
};
  
struct rr {
    struct rr *last;
    struct rr *next;
    char source;
#define RR_NONE     0
#define RR_FILE     1   /* from file */
#define RR_QUESTION 4   /* from server reply */
#define RR_ANSWER   5   /* from server reply */
#define RR_AUTHORITY    6   /* from server reply */
#define RR_ADDITIONAL   7   /* from server reply */
#define RR_QUERY    8   /* test name (see QUERY)*/
#define RR_INQUERY  9   /* test resource (see IQUERY)*/
  
    char *comment;      /* optional comment */
    char *name;     /* Domain name, ascii form */
    char *suffix;       /* Suffix name */
    int32 ttl;      /* Time-to-live */
#define TTL_MISSING 0x10000000	/* 11Oct2009, Maiko, 0x8.. exceeds MAXINT32 */
    int16 class;        /* IN, etc */
#define CLASS_MISSING   0
    int16 type;     /* A, MX, etc */
#define TYPE_MISSING    0
    int16 rdlength;     /* Length of data field */
    union {
        int32 addr;     /* Used for type == A */
#ifdef	IPV6
	/*
	 * 10Apr2023, Maiko (VE4KLM), Adding IPV6 support
	 *  (rfc 3596 (Oct 2003), obsoletes 3152, 1886)
	 *
	 * 15Apr2023, Maiko, This is silly, me using malloc
	 * is wasteful and risky, requires freeing, so just
	 * define the full buffer for crying out loud ...
	 */
	// unsigned char *addr6;	/* Used for type == AAAA */

	unsigned char addr6[16];
#endif
        struct soa soa;     /* Used for type == SOA */
        struct mx mx;       /* Used for type == MX */
        struct hinfo hinfo; /* Used for type == HINFO */
        struct minfo minfo; /* Used for type == MINFO */
        struct wks wks;     /* Used for type == WKS */
        char *name;     /* for domain names */
        char *data;     /* for anything else */
    } rdata;
};
#define NULLRR  (struct rr *)0
  
extern struct proc *Dfile_updater;
extern int DTranslate;
extern int DVerbose;
  
/* In domain.c */
void free_rr __ARGS((struct rr *rrlp));
struct rr *inverse_a __ARGS((int32 ip_address));
struct rr *resolve_rr __ARGS((char *dname,int16 dtype,int recurse));
char *resolve_a __ARGS((int32 ip_address, int shorten));
struct rr *resolve_mailb __ARGS((char *name));
#ifdef	IPV6
/* 10Apr2023, Maiko (VE4KLM), need to talk with IPV6 dns servers */
int add_nameserver (void *address, int timeout, int ipver);
#else
int add_nameserver __ARGS((int32 address,int timeout));
#endif
char *domainsuffix __ARGS((char *dname));
  
/* In domhdr.c: */

/*
 * 19Mar2021, Maiko (VE4KLM), added socket (s) parameter, better logging
 * 23Mar2021, Maiko, adding type of packet, query or reply - AND now
 * passing socket structure, not socket number (for peerless logging)
 * 24Apr2023, Maiko, passing *void to accomodate both IP versions
 *
int ntohdomain __ARGS((struct sockaddr_in *fsock, int ptype, struct dhdr *dhdr, struct mbuf **bpp));
 */

int ntohdomain __ARGS((void *fsock, int ptype, struct dhdr *dhdr, struct mbuf **bpp));

int htondomain __ARGS((struct dhdr *dhdr,char *buf,int16 len));
  
#endif  /* _DOMAIN_H */
