#ifndef _IFTCP_H
#define _IFTCP_H

/* 29Sep2019, Maiko (VE4KLM), Split out of tcp.h */

/* interface dependent tcp parameters */
struct iftcp {
    int32 irtt;
    int32 maxwait;
    int16 window;
    int16 mss;
    int blimit;
    int retries;
    int timertype;
    int syndata;
};

extern struct iftcp def_iftcp;

/*
 * TCP segment header -- internal representation
 * Note that this structure is NOT the actual header as it appears on the
 * network (in particular, the offset field is missing).
 * All that knowledge is in the functions ntohtcp() and htontcp() in tcpsubr.c
 */

#define TCPLEN      20  /* Minimum Header length, bytes */
#define TCP_MAXOPT  40  /* Largest option field, bytes */

struct tcp {
    int16 source;   /* Source port */
    int16 dest; /* Destination port */
    int32 seq;  /* Sequence number */
    int32 ack;  /* Acknowledgment number */
    int16 wnd;          /* Receiver flow control window */
    int16 checksum;         /* Checksum */
    int16 up;           /* Urgent pointer */
    int16 mss;          /* Optional max seg size */
    struct {
        char congest;   /* Echoed IP congestion experienced bit */
        char urg;
        char ack;
        char psh;
        char rst;
        char syn;
        char fin;
    } flags;
    char optlen;            /* Length of options field, bytes */
    char options[TCP_MAXOPT];   /* Options field */
};

#define DEF_MSS 512 /* Default maximum segment size */
#define DEF_WND 2048    /* Default receiver window */
#define DEF_RETRIES 10   /* default number of retries before resetting tcb */
#define DEF_RTT 5000    /* Initial guess at round trip time (5 sec) */

#endif

