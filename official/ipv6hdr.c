/*
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 *
 * 30Jan2023 - I must be nuts, the beginning of an ipv6 stack for JNOS 2.0
 *
 * Thanks to Bob Tenty (VE3TOK) for pushing me on this ? Sigh ...
 *
 * Don't be surprised if the code kind of looks like ipv4 :]
 *
 * References used so far :
 *
 *  https://www.geeksforgeeks.org/internet-protocol-version-6-ipv6-header
 *
 */

#include "global.h"
#include "mbuf.h"
#include "ip.h"
#include "internet.h"

#include "ipv6.h"	/* 01Feb2023, Maiko */  

#include "enet.h"	/* 19Mar2023, Maiko */

/*
 * 02Feb2023, Maiko, Need something to give me human readable IPV6 address
 *
 * Reference -> https://stackoverflow.com/questions/3727421/
 *               expand-an-ipv6-address-so-i-can-print-it-to-stdout
 *
 * Leading zeros in each 16-bit field are suppressed, but each group must retain at least one digit.
 *  (the above sentence comes from the ARIN site with regard to visually displaying addresses)
 *
 * 21Feb2023, Maiko, Try to write a short form IPV6 addr function from scratch
 *   (argggg, I have to do this on paper, just can't do it in my head)
 */
char *human_readable_ipv6 (unsigned char *addr)
{
	static char str[100];
	char *ptr = str;
	int cnt;

#ifdef	DONT_COMPILE
        static char sfstr[60];
   char *sfptr = sfstr;
   char leadingchar = 0, allzeros = 0, coloncount = 0;
#endif
	for (cnt = 0; cnt < 16; cnt++, addr++)
	{
		ptr += sprintf (ptr, "%x", (*addr & 0xf0) >> 4);
		ptr += sprintf (ptr, "%x", *addr & 0x0f);

		if ((cnt % 2) && cnt != 15)
			ptr += sprintf (ptr, ":");
	}

#ifdef	DONT_COMPILE
	/*
	 * 21Feb2023, Maiko (VE4KLM), An attempt at short form IPV6
	     (not today, my head hurts already, try again later)
	 */
	for (ptr = str, sfptr = sfstr; *ptr; ptr++)
	{
		if (*ptr == '0')
		{
			allzeros++;

			if (leadingchar)
				*sfptr++ = *ptr;

			continue;
		}
		else if (*ptr == ':')
		{
			if (allzeros != 4)
				  *sfptr++ = *ptr;

			leadingchar = 0;

			allzeros = 0;
		}
		else
		{	if (!leadingchar)
				leadingchar = 1;

			*sfptr++ = *ptr;
		}
	}

	*sfptr = 0;
#endif

	return str;
}

/*
		sprintf (str, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                 (int)addr->s6_addr[0], (int)addr->s6_addr[1],
                 (int)addr->s6_addr[2], (int)addr->s6_addr[3],
                 (int)addr->s6_addr[4], (int)addr->s6_addr[5],
                 (int)addr->s6_addr[6], (int)addr->s6_addr[7],
                 (int)addr->s6_addr[8], (int)addr->s6_addr[9],
                 (int)addr->s6_addr[10], (int)addr->s6_addr[11],
                 (int)addr->s6_addr[12], (int)addr->s6_addr[13],
                 (int)addr->s6_addr[14], (int)addr->s6_addr[15]);
*/

#define PLAY_AROUND_WITH_ETHERNET_HEADER_TAP

/*
 * 26Mar2023, Maiko, The MAC address of the linux tap interface.
 * This is set when the tap interface is created in tun.c file.
 *  (before this I was actually hardcoding it further down)
 */
unsigned char ipv6_ethernet_mac[6];

/*
 * Convert IPV6 header in host format to network mbuf, there is no checksum
 */
struct mbuf *htonipv6 (struct ipv6 *ipv6, struct mbuf *data)
{
    struct mbuf *bp;
    char *cp;
    int cnt;
  
#ifdef PLAY_AROUND_WITH_ETHERNET_HEADER_TAP
	struct ether ether;

    bp = pushdown (data, IPV6LEN + sizeof(struct ether));
#else
    bp = pushdown (data, IPV6LEN);
#endif
    cp = bp->data;
 
#ifdef PLAY_AROUND_WITH_ETHERNET_HEADER_TAP
	
/*
 * 22Mar2023, Maiko, Another stupid hack, the only way I know when to use
 * the ethernet broadcast MAC is to look at the destination IPV6 address.
 */
	if ((ipv6->dest[0] == 0xff) && (ipv6->dest[1] == 0x02))
	{
		ether.dest[0] = 0x33; ether.dest[1] = 0x33; ether.dest[2] = 0x00;
		ether.dest[3] = 0x00; ether.dest[4] = 0x00; ether.dest[5] = 0x02;
	}
	else
	{
		/*
		 * 26Mar2023, Maiko, No more hardcoding, taken directly from IRF
		 * structure opened when creating the TAP interface in tun.c ...
		 */
		memcpy (&ether, ipv6_ethernet_mac, 6);
/*
		ether.dest[0] = 0xae; ether.dest[1] = 0x0e; ether.dest[2] = 0xff;
		ether.dest[3] = 0xf5; ether.dest[4] = 0x57; ether.dest[5] = 0x8e;
*/
/*
		ether.dest[0] = 0xf2; ether.dest[1] = 0x67; ether.dest[2] = 0x26;
		ether.dest[3] = 0x11; ether.dest[4] = 0xd9; ether.dest[5] = 0xfd;
*/
	}

	memcpy (ether.source, myipv6iface()->hwaddr, EADDR_LEN);
	ether.type = IPV6_TYPE;

	memcpy (cp, ether.dest, EADDR_LEN);
	cp += EADDR_LEN;
	memcpy (cp, ether.source, EADDR_LEN);
	cp += EADDR_LEN;
	cp = put16 (cp, ether.type);

#endif
 
	/*
	 * The top 12 bits of traffic class are not used right now,
	 * so there is nothing to OR the version information with.
	 */
    *cp++ = ipv6->version << 4;

	/*
	 * Just put in the bottom 4 bits of traffic class right now,
	 * and indicate that we don't support function of flow label
	 * field (for now anyways), fill the 20 bits with 0s only.
	 */
    *cp++ = ipv6->traffic_class << 4;
    cp = put16 (cp, 0);

	/*
	 * payload length
	 */
    cp = put16 (cp, ipv6->payload_len);

	/*
	 * next header
	 */
    *cp++ = ipv6->next_header;

	/*
	 * hop limit
	 */
    *cp++ = ipv6->hop_limit;

	/*
	 * source address
	 */
	for (cnt = 0; cnt < 16; cnt++)
		*cp++ = ipv6->source[cnt];

/* terrible of me 06Feb2023, Maiko, VE4KLM
      that is why it was crashing on me
	for (cnt = 0; cnt < 16; cnt++)
		cp = put16 (cp, ipv6->source[cnt]);
 */
	/*
	 * destination address
	 */
	for (cnt = 0; cnt < 16; cnt++)
		*cp++ = ipv6->dest[cnt];

/*same diff 06Feb2023, Maiko
	for (cnt = 0; cnt < 16; cnt++)
		cp = put16 (cp, ipv6->dest[cnt]);
*/
  
    return bp;
}

/*
 * Extract an IPV6 header from mbuf
 */
int ntohipv6 (struct ipv6 *ipv6, struct mbuf **bpp)
{
	char ipv6buf[IPV6LEN], *ptr;

	int cnt;
  
	if (pullup (bpp, ipv6buf, IPV6LEN) != IPV6LEN)
		return -1;
  
	ipv6->version = (ipv6buf[0] >> 4) & 0xf;

	/*
	 * not supporting function of flow label field (for now anyways)
	 */
	ipv6->traffic_class = 0;

	/*
	 * payload length
	 */
	ipv6->payload_len = get16(&ipv6buf[4]);

	ptr = &ipv6buf[6];	/* 02Feb2023, Maiko (VE4KLM) */

	/*
	 * next header
	 */
	ipv6->next_header = *ptr++;

	/*
	 * hop limit
	 */
	ipv6->hop_limit = *ptr++;

	/*
	 * source address
	 */
	for (cnt = 0; cnt < 16; cnt++)
		ipv6->source[cnt] = *ptr++;

	/*
	 * destination address
	 */

	for (cnt = 0; cnt < 16; cnt++)
	    ipv6->dest[cnt] = *ptr++;

    return 0;
}

