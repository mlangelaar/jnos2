/* Various I/O definitions specific to asynch I/O on Unix */
#ifndef	_UNIXASY_H
#define	_UNIXASY_H

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef _PROC_H
#include "proc.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

/* Asynch controller control block */
struct asy
{
    struct iface *iface;
    struct mbuf *sndq;		/* Transmit queue */
    struct mbuf *rcvq;		/* Receive queue */
    struct proc *rxproc;	/* Low-level receive process */
    struct proc *monitor;	/* Low-level DCD-signal monitoring process */
#ifdef POLLEDKISS
    struct proc *poller;	/* G8BPQ poll process */
#endif
    int fd;			/* Fildes for tty device */
    char uulock[60];		/* UUCP lock file name */
    long speed;			/* port speed */
#ifdef NRS
    int nrs_cts;                /* false unless we want NRS_CTS flow control */
#endif
    unsigned short flags;	/* various flags */
#define ASY_RTSCTS	0x01	/*   RTS/CTS enabled */
#define ASY_CARR	0x02	/*   DCD detection */
#define	ASY_PACTOR	0x04	/*   PACTOR enabled - 15Feb2005, new flag */
    unsigned char pktsize;	/* nonblocking or termios VMIN (blocking) */
    unsigned long rxints;	/* simulated rx interrupts */
    unsigned long txints;	/* simulated tx interrupts */
    unsigned long rxchar;	/* Received characters */
    unsigned long txchar;	/* Transmitted characters */
/* new parameters for asy tuning */
    unsigned rxbuf;		/* chars to read at once */
    int rxq;			/* number of reads before sleeping */
    int txq;			/* number of writes before sleeping */
/* new status values */
    long rxput;			/* number of puts to asy rx */
    long rxovq;			/* number of times rxq full */
    long rxblock;		/* number of false (EWOULDBLOCK) reads */
    long txget;			/* number of gets from tx queue */
    long txovq;			/* number of times txq full */
    long txblock;		/* number of EWOULDBLOCK writes */
};

extern int Nasy;		/* Actual number of asynch lines */
extern struct asy Asy[];

extern int carrier_detect __ARGS((int));

#endif	/* _UNIXASY_H */
