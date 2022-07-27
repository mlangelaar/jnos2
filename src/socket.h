/* Mods by PA0GRI */
#ifndef _SOCKET_H
#define _SOCKET_H
  
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
#ifdef  ANSIPROTO
#include <stdarg.h>
#endif
  
#ifndef _MBUF_H
#include "mbuf.h"
#endif
  
#ifndef _PROC_H
#include "proc.h"
#endif
  
#ifndef _SOCKADDR_H
#include "sockaddr.h"
#endif
  
/* Local IP wildcard address */
#define INADDR_ANY  0x0L
  
/* IP protocol numbers */
/* now in internet.h */
  
/* TCP port numbers */
#define IPPORT_ECHO 7   /* Echo data port */
#define IPPORT_DISCARD  9   /* Discard data port */
#define IPPORT_FTPD 20  /* FTP Data port */
#define IPPORT_FTP  21  /* FTP Control port */
#define IPPORT_TELNET   23  /* Telnet port */
#define IPPORT_SMTP 25  /* Mail port */
#define IPPORT_TIME 37  /* Time port */
#define IPPORT_MTP  57  /* Secondary telnet protocol */
#define IPPORT_FINGER   79  /* Finger port */
#define IPPORT_HTTP     80  /* Http server port */
#define IPPORT_TTYLINK  87  /* Chat port */
#define IPPORT_TNLINK  88  /* RMS port, now more generic TN port */

/* 02Sep2020, Maiko (VE4KLM), Adding in mods from Brian (N1URO) */
#ifdef TNODE
#define IPPORT_NODE   3694 /* Linux/URONode port */
#endif

/*
 * 10Oct2004, Maiko, Added these for work I did with Barry (K2MF)
 */
#define	IPPORT_AXUDP	93	/* AX.25 within UDP port (VE4KLM) */
#define	IPPORT_IPUDP	94	/* IP within UDP port (K2MF) */

#define IPPORT_POP  109 /* Pop port */
#define IPPORT_POP2 109 /* Pop port */
#define IPPORT_POP3 110 /* Pop port */
#define IPPORT_IDENT 113 /* Ident (rfc1413) port */
#define IPPORT_NNTP 119 /* Netnews port */
#define IPPORT_RLOGIN 513      /* Remote login */
#define IPPORT_RSYSOP 1513      /* Remote sysop login (totally arbitrary #) */
#define IPPORT_CONVERS 3600     /* Converse */
#define IPPORT_XCONVERS 3601    /* LZW Convers */
#define IPPORT_CALLDB 1235      /* Pulled out of the air GRACILIS */
#define IPPORT_TRACE 1236       /* Pulled out of the air - WG7J */
#define IPPORT_TERM 5000        /* Serial interface server port */
  
/* UDP port numbers */
#define IPPORT_DOMAIN   53
#define IPPORT_BOOTPS   67  /* bootp server */
#define IPPORT_BOOTPC   68  /* bootp client */
#define IPPORT_RWHO 513
#define IPPORT_RIP  520
#define IPPORT_TIMED    525 /* Time daemon */
#define IPPORT_REMOTE   1234    /* Pulled out of the air */
#define IPPORT_BSR  5000    /* BSR X10 interface server port (UDP) */
  
#define AF_INET     0
#define AF_AX25     1
#define AF_NETROM   2
#define AF_LOCAL    3
#define AF_FILE     4
#ifdef	HFDD
#define AF_HFDD     5	/* New, 12Apr07, VE4KLM */
#endif
#ifdef HTTPVNC
#define AF_VNC      6	/* New, 03Jul2009, Maiko (VE4KLM) */
#endif
  
#define SOCK_STREAM 0
#define SOCK_DGRAM  1
#define SOCK_RAW    2
#define SOCK_SEQPACKET  3
  
/* Socket flag values - controls newline mapping */
#define SOCK_BINARY 0   /* socket in raw (binary) mode */
#define SOCK_ASCII  1   /* socket in cooked (newline mapping) mode */
#define SOCK_QUERY  2   /* Return setting without change */

/* 14Apr2016, Maiko (VE4KLM) - need to use 'flag' field for some bits */

#define	SOCK_FLAG_MASK	0x01	/* might as well mask out the top 7 bits,
				 * for future considerations.
				 */

#define SOCK_NO_IAC	0x10	/* 14Apr2016, Disable IAC in telnet forward */

/* Socket noblock values, set with sockblock() */
#define SOCK_BLOCK      0
#define SOCK_NOTXBLOCK  1
#define SOCK_NORXBLOCK  2   /* currently not implemented - WG7J */
  
#ifdef UNIX
  
/*
 * Avoid collisions with the C library's socket interface.  We may want to use
 * that in the future, anyway.  (Something like WAMPES tcpgate, but with some
 * protections.)
 */
  
/*
 * 10Apr06, Maiko, No more conflicts with system calls. The JNOS bind()
 * has been renamed to j2bind(), so we don't need the following anymore.
 *

#define accept j_accept
#define bind j_bind
#define listen j_listen
#define connect j_connect
#define socket j_socket
#define socketpair j_socketpair
#define recv j_recv
#define send j_send

#define shutdown j_shutdown
#define setflush j_setflush
#define getpeername j_getpeername
#define getsockname j_getsockname
#define recvfrom j_recvfrom
#define sendto j_sendto

 *
 */
  
#define EABORT ECONNRESET
#define EALARM ETIME
#define ECONNNOROUTE EHOSTUNREACH
  
#else /* not UNIX */

/* We use the following symbolic codes, all predefined under Linux, and we
   must be sure we don't redefine any already defined by the system's
   (eg, Borland's) errno.h, which may be referenced in the same src module
   (eg, in convers.c).  Also, let's be careful to assign numbers unlikely
   to be returned by any system routine, ie, 200+ error codes are unlikely.
   Include <errno.h>  before "socket.h", where needed.
   -- n5knx
*/

#ifndef EMFILE
#define EMFILE  201
#endif
#ifndef EBADF
#define EBADF   202
#endif
#ifndef EINVAL
#define EINVAL  203
#endif
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT 204
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT    205
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP  206
#endif
#ifndef EFAULT
#define EFAULT      207
#endif
#ifndef ENOTCONN
#define ENOTCONN    208
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED    209
#endif
#ifndef EAFNOSUPP
#define EAFNOSUPP   210
#endif
#ifndef EISCONN
#define EISCONN     211
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK 212
#endif
#ifndef EINTR
#define EINTR       213
#endif
#ifndef EADDRINUSE
#define EADDRINUSE  214
#endif
#ifndef ENOMEM
#define ENOMEM      215
#endif
#ifndef EMSGSIZE
#define EMSGSIZE    216
#endif
#ifndef EALARM
#define EALARM      217
#endif
#ifndef EABORT
#define EABORT      218
#endif
#ifndef ECONNNOROUTE
#define ECONNNOROUTE 219
#endif
  
#endif /* UNIX */
  
/* In socket.c: */
extern int Axi_sock;    /* Socket listening to AX25 (there can be only one) */
  
/*
 * April 2006, Maiko (VE4KLM), rename JNOS implementations of the UNIX socket
 * library functions to be specific to JNOS itself, this avoid conflicts with
 * system calls, and perhaps opens the door for me to port to WINDOWS !!!
 */
extern int j2accept (int, char*,int*);
extern int j2bind (int, char*, int);
extern int j2listen (int, int);
extern int j2connect (int, char*, int);
extern int j2socket (int, int, int);
extern int j2socketpair (int, int, int, int[]);

#ifdef	DONT_COMPILE
/* 10Apr06, Maiko, Amazingly enough this is not even used by JNOS anywhere */
extern int j2recv (int s,char *buf,int len,int flags);
#endif

extern int j2send (int, char*, int, int);

extern int j2shutdown (int, int);
extern int j2setflush (int, int);
extern int j2getpeername (int, char*, int*);
extern int j2getsockname (int, char*, int*);

#ifdef	DONT_COMPILE
/* 11Apr06, Maiko, Amazingly enough this is not even used by JNOS anywhere */
extern int j2recvfrom (int, char*, int, int, char*, int*);
#endif

extern int j2sendto (int, char*, int, int, char*, int);

/* End of renamed functions */

int close_s __ARGS((int s));
void freesock __ARGS((struct proc *pp));
int recv_mbuf __ARGS((int s,struct mbuf **bpp,int flags,char *from,int *fromlen));
int send_mbuf __ARGS((int s,struct mbuf *bp,int flags,char *to,int tolen));
#ifdef UNIX
void sockinit __ARGS((void));
#endif
int sockkick __ARGS((int s));
int socklen __ARGS((int s,int rtx));
struct proc *sockowner __ARGS((int s,struct proc *newowner));
int usesock __ARGS((int s));
int getnextsocket __ARGS((int s));
int sockfopen __ARGS((char *filename,char *mode));
int stdoutsockfopen __ARGS((void));

/* 20Apr2016, Maiko (VE4KLM), start_tcp extended to pass flags to i_upcall */
int new_start_tcp (int16, char*, void (*task)(int, void*, void*), int, int);

int start_tcp (int16, char*, void (*task)(int, void*, void*), int);
int stop_tcp (int16);
  
/* In sockuser.c: */
void flushsocks __ARGS((void));
int keywait __ARGS((char *prompt,int flush));


int recvchar __ARGS((int s));
int recvline __ARGS((int s,char *buf,unsigned len));
int rrecvchar __ARGS((int s));

int seteol __ARGS((int s,char *seq));
int sockmode __ARGS((int s,int mode));
int sockblock __ARGS((int s,int value));
void tflush __ARGS((void));
int tprintf __ARGS((char *fmt,...));
int tputc __ARGS((char c));

#ifdef UNIX
/*
 * 11Apr06, Maiko, No more of this j_xxx renaming. Function is now a
 * real JNOS specific function called j2tputs, to avoid conflicts with
 * the curses and possibly other libraries.
 *
#define tputs j_tputs
 *
 */
#endif

extern int j2tputs (char*);	/* 11Apr06, Maiko (VE4KLM) */

#ifdef UNIX
int traceusprintf __ARGS((int s, char *fmt, ...))
#ifdef __GNUC__
    __attribute__ ((format (printf, 2, 3)))
#endif
    ;
int tcmdprintf __ARGS((const char *fmt, ...))
#ifdef __GNUC__
    __attribute__ ((format (printf, 1, 2)))
#endif
    ;
int tfprintf __ARGS((FILE *fp, char *fmt, ...))
#ifdef __GNUC__
    __attribute__ ((format (printf, 2, 3)))
#endif
    ;
#endif
int usflush __ARGS((int s));
int usprintf __ARGS((int s,char *fmt,...));
int usputc __ARGS((int s,char c));
int usputs __ARGS((int s,char *x));
int usvprintf __ARGS((int s,char *fmt, va_list args));
  
/* In file sockutil.c: */
char *psocket __ARGS((void *p));
char *sockerr __ARGS((int s));
char *sockstate __ARGS((int s));
  
/* In mailbox.c: */
extern char Nosock[];
  
#endif  /* _SOCKET_H */
