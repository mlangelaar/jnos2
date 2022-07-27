/* Mods by G1EMM */
#ifndef _NETUSER_H
#define _NETUSER_H
  
/* Global structures and constants needed by an Internet user process */
  
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
#define NCONN   20              /* Maximum number of open network connections */
  
extern int32 Ip_addr;   /* Our IP address */
extern int Net_error;   /* Error return code */
#define NONE            0       /* No error */
#define CON_EXISTS      1       /* Connection already exists */
#define NO_CONN         2       /* Connection does not exist */
#define CON_CLOS        3       /* Connection closing */
#define NO_MEM          4       /* No memory for TCB creation */
#define WOULDBLK        5       /* Would block */
#define NOPROTO         6       /* Protocol or mode not supported */
#define INVALID         7       /* Invalid arguments */
#define NOROUTE         8       /* No route exists */
  
#define INET_EOL        "\r\n"  /* Standard Internet end-of-line sequence */
  
/* Codes for the tcp_open call */
#define TCP_PASSIVE     0
#define TCP_ACTIVE      1
#define TCP_SERVER      2       /* Passive, clone on opening */
  
/* Local IP wildcard address */
#define INADDR_ANY      0x0L
  
/* Socket structure */
#ifdef UNIX
#ifndef socket
#include "socket.h"
#endif
#endif
struct socket {
    int32 address;          /* IP address */
    int16 port;             /* port number */
};
#define NULLSOCK        (struct socket *)0
  
/* Connection structure (two sockets) */
struct connection {
    struct socket local;
    struct socket remote;
};
/* In domain.c: */
int32 resolve __ARGS((char *name));
#ifdef SMTP_A_BEFORE_WILDMX
int32 resolve_mx __ARGS((char *name, int32 A_RR_addr));
#else
int32 resolve_mx __ARGS((char *name));
#endif
int   resolve_amx __ARGS((char *name,int32 not_thisone,int32 Altmx[]));
  
/* In netuser.c: */
int32 aton __ARGS((char *s));
char *inet_ntoa __ARGS((int32 a));
char *pinet __ARGS((struct socket *s));
char *inet_ntobos __ARGS((int32 a));
  
#endif  /* _NETUSER_H */
