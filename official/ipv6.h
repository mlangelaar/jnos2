/*
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 *
 * 31Jan2023, Maiko, attempt at IPV6 stack for JNOS ...
 */
#ifndef _IPV6_JNOS2_H
#define _IPV6_JNOS2_H
  
#ifndef _GLOBAL_H
#include "global.h"
#endif

#include "iface.h"
#include "mbuf.h"

#include "sockaddr.h"	/* 03Apr2023, Maiko */

/*
 * 21Feb2023, Maiko, I don't see need for pseudo structure, and I ...
 *
 * have to change all my ipv6 address storage from int16 to uchar, not sure
 * why I started this off with int16, but the sockaddr_in6 and in6_addr are
 * using uchar, so a few function calls will also have to converted, ugh !
 */

#define	IPV6VERSION	6
#define	IPV6LEN		40

struct ipv6 {

    char version;
    char traffic_class;
    int16 payload_len;
    unsigned char next_header;	/* 20Mar2023, Maiko, should be unsigned !!! grrr again
					since next_header is > 135 for NS and NA icmpv6,
						but no, that's not why bad checksum */
    unsigned char hop_limit;	/* 15Mar2023, Maiko, should be unsigned !!!! grrrr */

    unsigned char source[16];

    unsigned char dest[16];
};

#define	ICMPV6_PTCL	58	/* 03Feb2023, Maiko */

/* Transport protocol link table */
struct ipv6link {
    char proto;
    void (*funct) __ARGS((struct iface *,struct ipv6 *,struct mbuf *,int));
};

extern struct ipv6link Ipv6link[];	/* in new ipv6link.c source file */

/* Need a function to copy ipv6 addresses - new one in ifacev6.c */
extern void copyipv6addr (unsigned char *source, unsigned char *dest);

/* 14Feb2023, Maiko, zero ipv6 address, looking for listens */
extern void zeroipv6addr (unsigned char *addr);

extern struct mbuf *htonipv6 (struct ipv6 *ipv6, struct mbuf *data);

extern int ntohipv6 (struct ipv6 *ipv6, struct mbuf **bpp);

/* 03Feb2023, Maiko, Change it to take the ipv6 struct instead
 * 21Mar2023, Maiko, Added link local argument
 * 22Mar2023, Maiko, Change return value to in, this is causing me grief
 */
extern int ismyipv6addr (struct ipv6 *ipv6, int);

extern void ipv6_recv (struct iface*, struct ipv6*, struct mbuf*, int);

extern int ipv6_send (unsigned char*, unsigned char*, char, char, char, struct mbuf*, int16, int16, char);

/* 09Feb2023, Maiko */

extern struct iface *myipv6iface (void);

/* 23Feb2023, Maiko, This really needs to go into a header file */
extern char *human_readable_ipv6 (unsigned char*);

/* 03Apr2023, Maiko (VE4KLM), cut code duplication, put it into new function */
extern int j2_ipv6_asc2ntwk (char*, struct j2sockaddr_in6*);
extern char *ipv6shortform (char *addr, int verbose);

#endif  /* _IPV6_JNOS2_H */


