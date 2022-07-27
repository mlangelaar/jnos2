#ifndef _REMOTE_H
#define _REMOTE_H
  
/* Remote reset/restart server definitions */
  
#ifdef REMOTESERVER
extern char *Rempass;   /* Remote access password */
extern char *Gatepass;  /* Remote encap-gateway password */
#endif

/* Commands */
#define SYS_RESET   1
#define SYS_EXIT    2
#define KICK_ME     3
#define ROUTE_ME    4
#define UNROUTE_ME  5

/* 14Oct2004, Maiko, IPUDP Support (K2MF) */
#define UDPROUTE_ME  6

#endif  /* _REMOTE_H */
