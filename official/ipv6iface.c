/*
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 */

#include "global.h"

#include "iface.h"

#include "ipv6.h"

/*
 * Return iface pointer if 'addr' belongs to one of our interfaces,
 * NULLIF otherwise. This is used to tell if an incoming IPV6 datagram
 * is for us, or if it has to be routed.
 *
 * 31Jan2023, Maiko, attempt at IPV6 stack for JNOS
 */

/*
 * 11Feb2023, Maiko (VE4KLM), Testing a theory with Ubuntu and pointers, adding
 * an extra 'safety zone' element, but still just using 16 bytes - nope, turns
 * out Ubuntu is very very sensitive to lack of proper function prototyping.
 *
 * 21Feb2023, Maiko, Need to change address array from int16 to uchar, so as
 * to properly match with the newly introduced in6_addr and sockaddr_in6 ...
 */
unsigned char myipv6addr[16] = {

	0xfd, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
};

/*
 * 21Mar2023, Maiko, Humbling and Learning experience to say the
 * least, and there is probably crap loads left to do on this.
 * IPV6 also requires link-local address for ND(P) - random ?
 * 23Mar2023, Maiko, Put in the correct calculated IP !!!!
 */
unsigned char mylinklocal[16] = {

	0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x56, 0x4e, 0x45, 0xff, 0xfe, 0x00, 0x00, 0x02
};

void copyipv6addr (unsigned char *source, unsigned char *dest)
{
	int cnt;

	for (cnt = 0; cnt < 16; cnt++)
		*dest++ = *source++;
}

/* 14Feb2023, Maiko, zero address IPV6 */
void zeroipv6addr (unsigned char *addr)
{
	int cnt;

	for (cnt = 0; cnt < 16; cnt++)
		*addr++ = 0;
}

/*
 * for starters, I am choosing to store my IPV6 address as an
 * array of int16 types, it may not be the most efficient, but
 * I just want to get basic ipv6 functionality into place, no
 * routing tables at first, just direct to JNOS, that's it !
 * 
 * Since there is no routing table, no point looping through
 * all the interfaces as in the original iface.c code, do it
 * later, just pass the preconfigured ipv6 iface for now !
 *
 * 12Feb2023, Maiko, Bob (VE3TOK) confirms a working PING exchange
 *  (he's been running this code and playing around with it)
 *
 * 21Mar2023, Maiko, Added linklocal argument, since with IPV6 we
 * are required to have link-local address as well, for ND(P) and
 * possible DHCP V6 (which I hope not to have to get into) ...
 *
 * 23Mar2023, Maiko, Changed to return 1 or 0, ifp is greif!
 */

int ismyipv6addr (struct ipv6 *ipv6, int linklocal)
{
	int cnt;

	unsigned char *ptr;

	/*
	 * 26Mar2023, Maiko, We don't need linklocal at this stage,
	 * but leave the code in place anyways, might remove yet ?
	 */
	if (linklocal)
		ptr = mylinklocal;
	else
 		ptr = myipv6addr;

	/* get a complete address check first */

	for (cnt = 0; cnt < 16; cnt++)
	{
		/* break out on the first mismatch */

		if (ipv6->dest[cnt] != ptr[cnt] /* myipv6addr[cnt] */)
			break;
	}

	/* return valid iface if all 16 match */
	return (cnt == 16);
}

