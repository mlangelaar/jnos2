/* Mods by G1EMM */
#ifndef _USOCK_H
#define _USOCK_H
  
#include <stdio.h>

#ifndef _CONFIG_H
#include "config.h"
#endif
  
#ifndef time_t
#include "time.h"
#endif
  
#ifndef _MBUF_H
#include "mbuf.h"
#endif
  
#ifndef _LZW_H
#include "lzw.h"
#endif
  
#ifndef _PROC_H
#include "proc.h"
#endif
  
#ifndef _TCP_H
#include "tcp.h"
#endif
  
#ifndef _UDP_H
#include "udp.h"
#endif
  
#ifndef _IP_H
#include "ip.h"
#endif
  
#ifndef _NETROM_H
#include "netrom.h"
#endif
  
#ifndef _SOCKADDR_H
#include "sockaddr.h"
#endif
  
struct loc {
    struct usock *peer;
    struct mbuf *q;
    int hiwat;              /* Flow control point */
    int flags;
#define LOC_SHUTDOWN    1
};
#define NULLLOC (struct loc *)0
#define LOCDFLOW        5       /* dgram socket flow-control point, packets */
#define LOCSFLOW        2048    /* stream socket flow control point, bytes */
#if defined(NNTP) || defined(NNTPS) || defined(CONVERS)
#define SOBUF		(512+3)	/* Size of buffer for usputc()/usprintf() */
#else
#define SOBUF           256     /* Size of buffer for usputc()/usprintf() */
#endif
  
#define SOCKBASE        128     /* Start of socket indexes */
  
union sp {
    struct sockaddr *sa;
    struct sockaddr_in *in;
    struct sockaddr_ax *ax;
    struct sockaddr_nr *nr;
    struct sockaddr_fp *fp;
    char *p;
};
  
union cb {
    struct tcb *tcb;
    struct ax25_cb *ax25;
    struct udp_cb *udp;
    struct raw_ip *rip;
    struct raw_nr *rnr;
    struct nr4cb *nr4;
    struct loc *local;
    FILE *fp;
    char *p;
};
  
/* User sockets */
struct usock {
    struct usock *next;     /* Link to the next socket */
    int number;             /* The socket number */
    struct proc *owner;
    int refcnt;
    char noblock;
    char type;
#define NOTUSED                 0
#define TYPE_TCP                1
#define TYPE_UDP                2
#define TYPE_AX25I              3
#define TYPE_AX25UI             4
#define TYPE_RAW                5
#define TYPE_NETROML3           6
#define TYPE_NETROML4           7
#define TYPE_LOCAL_STREAM       8
#define TYPE_LOCAL_DGRAM        9
#define TYPE_WRITE_ONLY_FILE   10
    int rdysock;
    union cb cb;
    char *name;
    int namelen;
    char *peername;
    int peernamelen;
    char errcodes[4];       /* Protocol-specific error codes */
    struct mbuf *obuf;      /* Output buffer */
    struct mbuf *ibuf;      /* Input buffer */
    char eol[3];            /* Text mode end-of-line sequence, if any */
    int flag;               /* Mode flags, defined in socket.h */
    int flush;              /* Character to trigger flush, if any */
#ifdef LZW
    struct lzw *zout;       /* Pointer to compression structure */
    struct lzw *zin;
#endif
    struct proc *look;      /* Sysop is tracing us ! */
    time_t created;         /* Time this socket was opened */
};
#define NULLUSOCK       ((struct usock *)0)
  
extern char Badsocket[];
extern char *Socktypes[];
  
struct usock *itop __ARGS((int s));
void st_garbage __ARGS((int red));
  
#endif /* _USOCK_H */
