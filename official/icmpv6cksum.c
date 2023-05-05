/*
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 *
 * 07Feb2023, Maiko (VE4KLM), create an ipv6 version of cksum.
 *
 * I need this if I am to get ICMP V6 working properly !
 *  (forget this pseudo structure, just use the real ipv6 info)
 */

#include "global.h"
#include "mbuf.h"
#include "ip.h"
#include "ipv6.h"

/*
 * 09Feb2023, Maiko, Saves a ton of code, broke it out of the
 * ipv4 cksum() function in iphdr.c, can use it here too ...
 */
extern int16 cksum_mbuf_chain (struct mbuf*, int32*, int16);

int16 icmpv6cksum (struct ipv6 *ipv6, struct mbuf *m, int16 len)
{
	int16 cnt, csum;

    	int32 sum = 0L;
  
    /* Sum pseudo-header, if present */
    if (ipv6)
	{
		for (cnt = 0; cnt < 16; cnt+=2 /* cnt++ */)
		{
			// log (-1, "%x", ipv6->source[cnt] & 0x00ff);
			// sum += (ipv6->source[cnt] & 0x00ff);

	/* 08Feb2023, Maiko (VE4KLM), And SUCCESS !!! key is you have to use WORDS not BYTES */
			/* 22Mar2023, Why am I doing the 00ff crap, they're bytes now
			csum = ((ipv6->source[cnt] & 0x00ff) << 8) + (ipv6->source[cnt+1] & 0x00ff);
 */
			csum = (ipv6->source[cnt] << 8) + (ipv6->source[cnt+1]);
			// log (-1, "csum source [%x]", csum);
			sum += csum;
		}

		for (cnt = 0; cnt < 16; cnt+=2 /* cnt++ */)
		{
			// log (-1, "%x", ipv6->dest[cnt] & 0x0ff);
			// sum += (ipv6->dest[cnt] & 0x00ff);

	/* 08Feb2023, Maiko (VE4KLM), And SUCCESS !!! key is you have to use WORDS not BYTES */

			/* 22Mar2023, Why am I doing the 00ff crap, they're bytes now
			csum = ((ipv6->dest[cnt] & 0x00ff) << 8) + (ipv6->dest[cnt+1] & 0x00ff);
*/
			csum = (ipv6->dest[cnt] << 8) + (ipv6->dest[cnt+1]);
			// log (-1, "csum dest [%x]", csum);
			sum += csum;

		}

	// log (-1, "%x", (unsigned char)(ipv6->next_header));

        sum += (unsigned char)(ipv6->next_header);

	// log (-1, "%x", ipv6->payload_len);
        // sum += ipv6->payload_len;

	// log (-1, "%x", len);
        sum += len;
    }

/* 08Feb2023, Maiko, The beauty of this is the JNOS existing cksum is mostly reused */

	return cksum_mbuf_chain (m, &sum, len);	/* 09Feb2023, Maiko */

}

