/*
 * Mods by G1EMM
 *
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 */
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

#ifdef	IPV6
/*
 * 13Feb2023, Maiko (VE4KLM), Perhaps this is the best way
 * to incorporate IPV6 into the existing TCP code, keeping
 * changes to existing code as minimal as possible ?
 *
 * 14Feb2023, Maiko, Oops, address is 16, not 10 !
 * 21Feb2023, Maiko, ipv6 addresses changed from int16 to uchar
 */
struct j2socketv6 {
    unsigned char address[16]; /* IP address */
    int16 port;             /* port number */
};
#define NULLJ2SOCKV6       (struct j2socketv6*)0
#endif
  
/* Connection structure (two sockets) */
struct connection {
#ifdef	IPV6
	char ipver;	/* 14Feb2023, Maiko, which socket pair to use */
#endif
    struct socket local;
    struct socket remote;
#ifdef	IPV6
	/*
	 * 13Feb2023, Maiko (VE4KLM), Perhaps this is the best way
	 * to incorporate IPV6 into the existing TCP code, keeping
	 * changes to existing code as minimal as possible ?
	 */
    struct j2socketv6 localv6;
    struct j2socketv6 remotev6;
#endif
};
/* In domain.c: */
int32 resolve __ARGS((char *name));
#ifdef	IPV6
/* 13Apr2023, Maiko, New IPV6 resolve function */
extern unsigned char *resolve6(char*);
#endif
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

#ifdef	IPV6
/* 03Apr2023, Maiko */
char *pinetv6 (struct j2socketv6*);
#endif
  
#endif  /* _NETUSER_H */
