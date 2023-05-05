/*
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 *
 * 03Feb2023, Maiko, Trace mods completed, now get ICMP V6 working
 */
#ifndef _ICMPV6_JNOS2_H
#define _ICMPV6_JNOS2_H
  
#ifndef _GLOBAL_H
#include "global.h"
#endif

#include "iface.h"
#include "mbuf.h"

#include "ipv6.h"

/*
 * 13Mar2023, Maiko (VE4KLM), Okay, time to process NDP :]
 * 14Mar2023, Maiko, Added type 136 for advertisement
 * 21Mar2023, Maiko, Added types 133 and 134
 *
 * Reference -> [RFC4861]
 */

#define	ICMPV6_ROUTER_SOLICITATION	133
#define	ICMPV6_ROUTER_ADVERTISEMENT	134

#define	ICMPV6_NEIGHBOR_SOLICITATION	135
#define	ICMPV6_NEIGHBOR_ADVERTISEMENT	136

/* Reference -> https://www.rfc-editor.org/rfc/rfc4443.html */

#define ICMPV6_ECHO       128   /* Echo Request */
#define ICMPV6_ECHO_REPLY 129   /* Echo Reply */

/* 15Apr2023, Maiko, Should get my act together with regard to the
 * other ICMPV6 messages types that I need to deal with, now that
 * I am looking at stuff like DNS and HOP CHECK, not the same !
 */

#define ICMPV6_TIME_EXCEED	3

#define	ICMPV6_HOP_LIMIT_EXCEED	0

#define	ICMPV6_DEST_UNREACH	1

#define	ICMPV6_NET_UNREACH	0
#define	ICMPV6_ADMIN_PROHIB	1
#define	ICMPV6_HOST_UNREACH	3
#define	ICMPV6_PORT_UNREACH	4

struct icmpv6 {

    unsigned char type;
    unsigned char code;

    int16 checksum;

    union icmpv6_args {

        struct {
            int16 id;
            int16 seq;
        } echo;

	/*
	 * 15Mar2023, Maiko, New structure for ND (Network Discovery)
	 */
		struct {
			int16 reserved_hi;
			int16 reserved_lo;
			unsigned char target[16];
			unsigned char type;
			unsigned char len;
			unsigned char mac[6];
		} nd;
	/*
	 * 21Mar2023, Maiko, New structure for RD (Router Discovery), this
	 * is 'much different' then the Router Solicitation and ND stuff !
	 *  (https://www.rfc-editor.org/rfc/rfc4861#section-4.2)	
	 */
		struct {
			unsigned char cur_hop_limit;
			unsigned char reserved;
			int16 router_lifetime;
			unsigned int reachable_time;
			unsigned int retrans_time;
			unsigned char type;
			unsigned char len;
			unsigned char mac[6];
		} rd;
    } args;
};

/* 07Feb2023, Maiko (VE4KLM), need to now pass ipv6 for checksum calculation */
extern struct mbuf* htonicmpv6 (struct ipv6 *ipv6, struct icmpv6 *icmpv6, struct mbuf *data);

extern int ntohicmpv6 (struct icmpv6 *icmpv6, struct mbuf **bpp);

extern void icmpv6_input (struct iface*, struct ipv6*, struct mbuf*, int);

/* 30Mar2023, Maiko, Finally a proper icmpv6 output (very important function) */
extern int icmpv6_output (struct ipv6*, struct mbuf*, char, char, union icmpv6_args*);

extern int16 icmpv6cksum (struct ipv6 *ipv6, struct mbuf *m, int16 len);

/* 23Feb2023, Maiko (VE4KLM), originally in ipv6.h, but now here */

struct icmpv6link {
    char proto;
    void (*funct) (unsigned char*, unsigned char*, unsigned char*, char, char, struct mbuf **);
};

extern struct icmpv6link Icmpv6link[];

#endif  /* _IPCMPV6_JNOS2_H */

