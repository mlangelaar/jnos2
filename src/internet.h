/* Mods by PA0GRI */
#ifndef _INTERNET_H
#define _INTERNET_H
  
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
/* Global structures and constants pertaining to the interface between IP and
 *  higher level protocols
 */
  
/* IP protocol field values */
#define ICMP_PTCL   1   /* Internet Control Message Protocol */
#define	IP_PTCL     4   /* IP inside IP */
#define TCP_PTCL    6   /* Transmission Control Protocol */
#define UDP_PTCL    17  /* User Datagram Protocol */
#define RSPF_PTCL   73  /* Radio Shortest Path First Protocol */
#define AX25_PTCL   93  /* AX25 inside IP according to RFC-1226 */
#define IP_PTCL_OLD 94  /* JI's IP inside IP (deprecated, in favor of IP_PTCL) */
  
#define MAXTTL      255 /* Maximum possible IP time-to-live value */
  
/* DoD-style precedences */
#define ROUTINE     0x00
#define PRIORITY    0x20
#define IMMEDIATE   0x40
#define FLASH       0x60
#define FLASH_OVER  0x80
#define CRITIC      0xa0
#define INET_CTL    0xc0
#define NET_CTL     0xe0
  
/* Amateur-style precedences */
#define AM_ROUTINE  0x00
#define AM_WELFARE  0x20
#define AM_PRIORITY 0x40
#define AM_EMERGENCY    0x60
  
/* Class-of-service bits */
#define LOW_DELAY   0x10
#define THROUGHPUT  0x08
#define RELIABILITY 0x04
  
/* IP TOS fields */
#define PREC(x)     (((x)>>5) & 0x7)
#define DELAY       0x10
#define THRUPUT     0x8
#define RELIABLITY  0x4
  
/* Pseudo-header for TCP and UDP checksumming */
struct pseudo_header {
    int32 source;       /* IP source */
    int32 dest;     /* IP destination */
    char protocol;      /* Protocol */
    int16 length;       /* Data field length */
};
#define NULLHEADER  (struct pseudo_header *)0
  
/* Format of a MIB entry for statistics gathering */
struct mib_entry {
    char *name;
    struct {
        int32 integer;
    } value;
};
  
#endif  /* _INTERNET_H */
