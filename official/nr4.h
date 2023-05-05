#ifndef _NR4_H
#define _NR4_H
/* nr4.h:  defines for netrom layer 4 (transport) support */
  
#ifndef _MBUF_H
#include "mbuf.h"
#endif
  
#ifndef _TIMER_H
#include "timer.h"
#endif
  
#ifndef _AX25_H
#include "ax25.h"
#endif
  
#ifdef __GNUC__
struct nr4cb;        /* forward def to keep GCC happy */
#endif

#ifndef _NETROM_H
#include "netrom.h"
#endif

#define WZOC_FIX	/* 05Apr2023, Michael Ford (WZ0C) */
  
/* compile-time limitations */
  
#define NR4MAXCIRC  20      /* maximum number of open circuits */
#define NR4MAXWIN   127     /* maximum window size, send and receive */
  
/* protocol limitation: */
  
#define NR4MAXINFO  236     /* maximum data in an info packet */
  
/* sequence number wraparound mask */
  
#define NR4SEQMASK  0xff    /* eight-bit sequence numbers */
  
/* flags in high nybble of opcode byte */
  
#define NR4CHOKE    0x80
#define NR4NAK      0x40
#define NR4MORE     0x20    /* The "more follows" flag for */
                            /* pointless packet reassembly */
  
/* mask for opcode nybble */
  
#define NR4OPCODE   0x0f
  
/* opcodes */
  
#define NR4OPPID    0       /* protocol ID extension to network layer */
#define NR4OPCONRQ  1       /* connect request */
#define NR4OPCONAK  2       /* connect acknowledge */
#define NR4OPDISRQ  3       /* disconnect request */
#define NR4OPDISAK  4       /* disconnect acknowledge */
#define NR4OPINFO   5       /* information packet */
#define NR4OPACK    6       /* information ACK */
#define NR4NUMOPS   7       /* number of transport opcodes */
  
/* minimum length of NET/ROM transport header */
  
#define NR4MINHDR   5
  
/* host format net/rom transport header */
  
struct nr4hdr {
    unsigned char opcode ;      /* opcode and flags */
    unsigned char yourindex ;   /* receipient's circuit index */
    unsigned char yourid ;      /* receipient's circuit ID */
    unsigned char flags;        /* G8BPQ detection, etc. */
#define NR4_G8BPQTTL    0x01
#define NR4_G8BPQRTT    0x02
#define NR4_G8BPQMASK   0x03

#ifdef	NRR
/* 11Oct2005, Maiko (VE4KLM), Now supporting NRR frames */
/* 04Mar2006, Maiko, Now independent of INP3 code */
#define NR4_NRR		0x04
#endif

    union {
  
        struct {                /* network extension */
            unsigned char family ;  /* protocol family */
            unsigned char proto ;   /* protocol within family */
        } pid ;
  
        struct {                /* connect request */
            unsigned char myindex ; /* sender's circuit index */
            unsigned char myid ;    /* sender's circuit ID */
            unsigned char window ;  /* sender's proposed window size */
            char user[AXALEN] ;     /* callsign of originating user */
            char node[AXALEN] ;     /* callsign of originating node */
            unsigned int t4init;    /* G8BPQ's transport timeout in seconds */
  
        } conreq ;
  
        struct {                /* connect acknowledge */
            unsigned char myindex ; /* sender's circuit index */
            unsigned char myid ;    /* sender's circuit ID */
            unsigned char window ;  /* accepted window size */
            unsigned char ttl;      /* G8BPQ's initial ttl */
        } conack ;
  
        struct {                /* information */
            unsigned char txseq ;   /* sender's tx sequence number */
            unsigned char rxseq ;   /* sender's rx sequence number */
        } info ;
  
        struct {                /* information acknowledge */
            unsigned char rxseq ;   /* sender's rx sequence number */
        } ack ;
  
    } u ;   /* End of union */
  
} ;
  
/* A netrom send buffer structure */
  
struct nr4txbuf {
    struct timer tretry ;       /* retry timer */
    unsigned retries ;          /* number of retries */
    struct mbuf *data ;         /* data sent but not acknowledged */
} ;
  
/* A netrom receive buffer structure */
  
struct nr4rxbuf {
    unsigned char occupied ;    /* flag: buffer in use */
    struct mbuf *data ;         /* data received out of sequence */
} ;
  
/* address structure */
struct nr4_addr {
    char user[AXALEN];
    char node[AXALEN];
};
#define NULLNRADDR  (struct nr4_addr *)0
  
struct sockaddr_nr {
    short nr_family;
    struct nr4_addr nr_addr;
};
  
/* The netrom circuit control block */
  
struct nr4cb {
    unsigned mynum ;            /* my circuit number */
    unsigned myid ;             /* my circuit ID */
    unsigned yournum ;          /* remote circuit number */
    unsigned yourid ;           /* remote circuit ID */
    struct nr4_addr remote ;        /* address of remote node */
    struct nr4_addr local ;         /* our own address */
  
    unsigned window ;           /* negotiated window size */
  
    /* Data for round trip timer calculation and setting */
  
    int32 srtt ;                 /* Smoothed round trip time */
    int32 mdev ;                 /* Mean deviation in round trip time */
    unsigned blevel ;           /* Backoff level */
    unsigned txmax ;            /* The maximum number of retries among */
                                /* the frames in the window.  This is 0 */
                                /* if there are no frames in the window. */
                                /* It is used as a baseline to determine */
                                /* when to increment the backoff level. */
  
    /* flags */
  
    char clone ;                /* clone this cb upon connect */
    char choked ;               /* choke received from remote */
    char qfull ;                /* receive queue is full, and we have */
                                /* choked the other end */
    char naksent ;              /* a NAK has already been sent */
  
    /* transmit buffers and window variables */
  
    struct nr4txbuf *txbufs ;   /* pointer to array[windowsize] of bufs */
    unsigned char nextosend ;   /* sequence # of next frame to send */
    unsigned char ackxpected ;  /* sequence number of next expected ACK */
    unsigned nbuffered ;        /* number of buffered TX frames */
    struct mbuf *txq ;          /* queue of unsent data */
  
    /* receive buffers and window variables */
  
    struct nr4rxbuf *rxbufs ;   /* pointer to array[windowsize] of bufs */
    unsigned char rxpected ;    /* # of next receive frame expected */
    unsigned char rxpastwin ;   /* top of RX window + 1 */
    struct mbuf *rxq ;          /* "fully" received data queue */
#ifdef WZOC_FIX
    /* 05Apr2023, Michael Ford (WZ0C) */
    unsigned int rxunacked;     /* # of received frames unacked */
#endif

    /* Connection state */
  
    int state ;                 /* connection state */
#define NR4STDISC   0           /* disconnected */
#define NR4STCPEND  1           /* connection pending */
#define NR4STCON    2           /* connected */
#define NR4STDPEND  3           /* disconnect requested locally */
#define NR4STLISTEN 4           /* listening for incoming connections */
  
    int dreason ;               /* Reason for disconnect */
#define NR4RNORMAL  0           /* Normal, requested disconnect */
#define NR4RREMOTE  1           /* Remote requested */
#define NR4RTIMEOUT 2           /* Connection timed out */
#define NR4RRESET   3           /* Connection reset locally */
#define NR4RREFUSED 4           /* Connect request refused */
  
    /* Per-connection timers */
  
    struct timer tchoke ;       /* choke timeout */
    struct timer tack ;         /* ack delay timer */
    struct timer tdisc ;        /* Inactivity timer - WG7J */
  
    struct timer tcd ;          /* connect/disconnect timer */
    unsigned cdtries ;          /* Number of connect/disconnect tries */
  
    void (*r_upcall) __ARGS((struct nr4cb *,int16));
                    /* receive upcall */
    void (*t_upcall) __ARGS((struct nr4cb *,int16));
                    /* transmit upcall */
    void (*s_upcall) __ARGS((struct nr4cb *,int,int));
                    /* state change upcall */
    int user ;          /* user linkage area */
} ;
  
#define NULLNR4CB   (struct nr4cb *)0
  
/* The netrom circuit pointer structure */
  
struct nr4circp {
    unsigned char cid ;         /* circuit ID; incremented each time*/
                                /* this circuit is used */
    struct nr4cb *ccb ;         /* pointer to circuit control block, */
                                /*  NULLNR4CB if not in use */
} ;
  
/* The circuit table: */
  
extern struct nr4circp Nr4circuits[NR4MAXCIRC] ;
  
/* Some globals */
  
extern unsigned short Nr4window ;   /* The advertised window size, in frames */
extern int32 Nr4irtt ;           /* The initial round trip time */
extern unsigned short Nr4retries ;  /* The number of times to retry */
extern int32 Nr4acktime ;        /* How long to wait until ACK'ing */
extern char *Nr4states[] ;      /* NET/ROM state names */
extern char *Nr4reasons[] ;     /* Disconnect reason names */
extern unsigned short Nr4qlimit ;       /* max receive queue length before CHOKE */
extern int32 Nr4choketime ;      /* CHOKEd state timeout */
extern char Nr4user[AXALEN];    /* User callsign in outgoing connects */
  
/* function definitions */
  
/* In nr4hdr.c: */
int ntohnr4 __ARGS((struct nr4hdr *, struct mbuf **));
struct mbuf *htonnr4 __ARGS((struct nr4hdr *));
  
/* In nr4subr.c: */
void free_n4circ __ARGS((struct nr4cb *));
struct nr4cb *get_n4circ __ARGS((int, int));
int init_nr4window __ARGS((struct nr4cb *, unsigned));
int nr4between __ARGS((unsigned, unsigned, unsigned));
struct nr4cb *match_n4circ __ARGS((int, int,char *,char *));
struct nr4cb *new_n4circ __ARGS((void));
void nr4defaults __ARGS((struct nr4cb *));
int nr4valcb __ARGS((struct nr4cb *));
void nr_garbage __ARGS((int red));

/* 10Mar2021, Maiko (VE4KLM), now by remote call as well, new function */
struct nr4cb *getnr4cb (char*);
  
/* In nr4.c: */
void nr4input __ARGS((struct nr4hdr *hdr,struct mbuf *bp));
int nr4output __ARGS((struct nr4cb *));
void nr4sbuf __ARGS((struct nr4cb *, unsigned));
void nr4sframe __ARGS((char *, struct nr4hdr *, struct mbuf *));
void nr4state __ARGS((struct nr4cb *, int));
  
/* In nr4timer.c */
void nr4ackit __ARGS((void *));
void nr4cdtimeout __ARGS((void *));
void nr4txtimeout __ARGS((void *));
void nr4unchoke __ARGS((void *));
  
/* In nr4user.c: */
void disc_nr4 __ARGS((struct nr4cb *));
int kick_nr4 __ARGS((struct nr4cb *));
struct nr4cb *open_nr4 __ARGS((struct nr4_addr *, struct nr4_addr *, int,
void (*) __ARGS((struct nr4cb *, int)),
void (*) __ARGS((struct nr4cb *, int)),
void (*) __ARGS((struct nr4cb *, int, int)),int));
struct mbuf *recv_nr4 __ARGS((struct nr4cb *, int16));
void reset_nr4 __ARGS((struct nr4cb *));
int send_nr4 __ARGS((struct nr4cb *, struct mbuf *));
  
/* In nrcmd.c: */
void nr4_state __ARGS((struct nr4cb *, int, int));
void donodetick __ARGS((void));  /* called from remote kick proc now */
  
#endif  /* _NR4_H */
