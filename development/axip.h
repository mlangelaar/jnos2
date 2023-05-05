#ifndef	_AXIP_H_
#define	_AXIP_H_

/*
 * Max number of fake AX.25 interfaces for AXIP and AXUDP
 */
#ifndef	NAX25
#define NAX25	16
#endif

/* check domain name every 30 minutes */
#define	AXRESOLVTIMER	1800000

/*
 * Some information we need to keep track of for AXIP / AXUDP interfaces.
 *
 *    ipaddr - IP address of the 'other end', the destination.
 *    source - UDP source port - not used for axip (set to 0).
 *      dest - UDP dest port - not used for axip (set to 0).
 *  resolv_t - timer info for periodic resolution of 'axhost' below.
 *    axhost - domain (dyndns) name of the remote host to resolve.
 *
 * This structure is what I used in my last JNOS 1.11f AXUDP update kit.
 *
 * 27Apr2023, Maiko (VE4KLM), Added ipaddr6 for IPV6 support !
 * 28Apr2023, Maiko, Having ipver would be nice, so added it.
 *
 * 03May2023, Maiko, Adding axtype (UDP_PTCL or AX25_PTCL) to
 * save me from having to check dest == src == 0 each time I
 * have to go through the for address check loop, less CPU,
 * so just set the axtype when doing the axip/axudp attach.
 *  (fixing an oversite from 20 years ago when I added axudp)
 */

typedef struct gfcpixa {

	uint32	ipaddr;

#ifdef	IPV6
	unsigned char *ipaddr6;  /* just malloc the address space */
	char ipver;
#endif
	int	source;
	int	dest;

	struct	timer	resolv_t;

	char	*axhost;

	unsigned char axtype;	/* 03May2023, Maiko */

} AXIPCFG;

#endif

