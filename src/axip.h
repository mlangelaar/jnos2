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
 */

typedef struct gfcpixa {

	uint32	ipaddr;

	int	source;
	int	dest;

	struct	timer	resolv_t;

	char	*axhost;

} AXIPCFG;

#endif

