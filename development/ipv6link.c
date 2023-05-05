/*
 *
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 *
 * 01Feb2023, Maiko (VE4KLM), Put the ipv6 versions of this
 * structure into it's own file, instead of config.c, as is
 * done for the ipv4 counterparts, and right now, lets just
 * focus on icmp v6, ping, network unreachable, etc ...
 */

#include "global.h"
#include "internet.h"
#include "iface.h"
#include "mbuf.h"
#include "ipv6.h"
#include "icmpv6.h"	/* 03Feb2023, Maiko */
#include "udp.h"	/* 13Apr2023, Maiko */

/* Transport protocols atop IP V6 */

/* 03Feb2023, Maiko, Okay, using 0 for functions not defined
 * was a bad idea, then the table looks empty from the start
 * for all protocols, so use -1 instead ... duh !!!
 *
 * Also for ICMP, ipv4 was 1 (ICMP_PTCL) but ipv6 is 58,
 * adding a new ICMPV6_PTCL in ipv6.h to reflect this.
 *
 * 12Feb2023, Maiko (VE4KLM), properly cast 'empty functions' !
 *
 */

/* 17Feb2023, Maiko, Fix arg ordering in prototype, bad mistaken */
extern void tcpv6_input (struct iface*, struct ipv6*, struct mbuf*, int);

/* 23Feb2023, Maiko, there is an ICMP component to TCP traffic we need to put in */
extern void tcpv6_icmp (unsigned char*, unsigned char*, unsigned char*, char, char, struct mbuf**);

/* 27Apr2023, Maiko, Prototype for the axip for ipv6 input function */
extern void axipv6_input (struct iface*, struct ipv6*, struct mbuf*, int);

struct ipv6link Ipv6link[] = {
    { TCP_PTCL,         tcpv6_input },
    { UDP_PTCL,  	udp_inputv6 },		/* 13Apr2023, Maiko, Finally in place */
    { ICMPV6_PTCL,      icmpv6_input },
#ifdef  AXIP
    { AX25_PTCL,        axipv6_input },	/* 27Apr2023, Maiko, Time to support AXIP wormholes over IPV6 */
#endif
    { 0,               0 }
};

/* Transport protocols atop ICMP V6 */

struct icmpv6link Icmpv6link[] = {
    { TCP_PTCL,       tcpv6_icmp },	/* 23Feb2023, Maiko, Should use this */
    { 0,              0 }
};

