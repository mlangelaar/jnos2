/*
 * Support for NRR (Netrom Record Route) packets. Prototype was completed
 * on September 23, 2005 - I am able to receive NRR frames from a remote
 * system, respond to them, and the remote system shows proper output :
 *
    [JNOS-2.0c5b-BFHIM$]
    You have 22 messages  -  0 new.
    Area: ve4klm (#1) >
    c w1uu-2
    Trying...  The escape character is: CTRL-T
    *** connected to ANDNET:W1UU-2
    (X)NET/LINUX V1.36
    =>n ve4klm

    routing MBGATE:VE4KLM v WA7V-2

    > VE4KLM    WA7V-2    248/6   1.88s  4 hops
      VE4KLM    ON6DP     234/6   5.57s  7 hops
      VE4KLM    VK3KQU-12 132/6  36.76s 10 hops

    =>
    *** route: W1UU-2 WA7V-2 VE4WSC VE4KLM VE4WSC WA7V-2 W1UU-2

    =>nrr ve4klm

    =>
    *** route: W1UU-2 WA7V-2 VE4WSC VE4KLM VE4WSC WA7V-2 W1UU-2

    =>
 *
 * This code was written from scratch, based on observations I made
 * of traces between Xnet <-> Xnet and Xnet <-> Jnos 2.0 systems.
 *
 * Mid September, 2005, by Maiko Langelaar / VE4KLM
 *
 * 03Mar2006, Maiko, Separated out of INP3 code, renamed NRR functions.
 *
 */

#include <ctype.h>
#include "global.h"

#if defined (NETROM) && defined (NRR)

#include "nrr.h"

#include "socket.h"	/* 29Sep2019, Maiko (VE4KLM), struct breakouts */

/* Check NETROM transport header to see if this is a NRR frame */

int nrr_frame (unsigned char *data)
{
	 /* take a sneak peak at the transport header */

	return (data[0] == 0x00 &&
			data[1] == 0x01 &&
			data[2] == 0x00 &&
			data[3] == 0x00 &&
			data[4] == 0x00);
}

static int nrr_mbx_user = -1;

/* 23Sep2005, Maiko (VE4KLM), Send out an NRR to a remote system */

static void nrr_send (char *dest)
{
	struct mbuf *bp;

   	if ((bp = alloc_mbuf (6 + AXALEN)) == NULLBUF)
	{
		log (-1, "inp3_send_nrr - no memory");
   		return;
	}

	/* 05Oct05, Maiko, Figuring out how to display nrr response */
	nrr_mbx_user = Curproc->output;

	bp->cnt = 6 + AXALEN;	/* 03Oct2005, Oops, forgot this one ! */

	bp->data[0] = 0x00;
	bp->data[1] = 0x01;
	bp->data[2] = 0x00;
	bp->data[3] = 0x00;
	bp->data[4] = 0x00;

	memcpy (&bp->data[5], Nr_iface->hwaddr, AXALEN);

	bp->data[12] = 0x7f;

	nr3output (dest, bp);
}

/* Process an NRR frame sent to us by a remote system */

int nrr_proc (struct nr3hdr *n3hdr, struct mbuf **bp)
{
	char tmp[AXBUF], nrrcalls[10][AXALEN], nrrflags[10], *ptr;

	int cnt, len, nrrcount = 0;

	/* pullup (bp, NULLCHAR, 5); now done in ntohnr4 () function */

	log (-1, "received NRR from [%s]", pax25 (tmp, n3hdr->source));

	while (len_p (*bp) > 0)
	{
		if (nrrcount >= 10)
		{
			log (-1, "warning - exceeded max NRR calls");

			pullup (bp, NULLCHAR, AXALEN + 1);
		}
		else
		{
			pullup (bp, nrrcalls[nrrcount], AXALEN);
			pullup (bp, &nrrflags[nrrcount], 1);

			log (-1, "node %s %d", pax25 (tmp, nrrcalls[nrrcount]),
				(int)nrrflags[nrrcount]);
		}

		nrrcount++;
	}

	/*
	 * 23Sep2005, This is important !!! If our call is the first
	 * one in the list, then it means WE sent it to begin with.
	 *
	 * 05Oct2005, Maiko, Experimental NRR response to mbx user.
	 */

	if (addreq (nrrcalls[0], Nr_iface->hwaddr))
	{
		char *ptr, *nrrstr = malloc (30 + (12 * nrrcount));

		/* format the NRR path result for display to user */
		for (ptr = nrrstr, cnt = 0; cnt < nrrcount; cnt++)
		{
			if (cnt == 0)
				ptr += sprintf (ptr, "*** route:");

			ptr += sprintf (ptr, " %s", pax25 (tmp, nrrcalls[cnt]));

			if (nrrflags[cnt] & 0x80)
				*ptr++ = '*';
		}

		/* 12Oct2005, Maiko, finish up proper formatting */
		ptr += sprintf (ptr, " %s\n>\n", pax25 (tmp, Nr_iface->hwaddr));

		*ptr = 0;	/* terminate the string */

		if (nrr_mbx_user != -1)
		{
			/*
			 * 12Oct2005, Maiko, We really should not be
			 * doing I/O from within a network call should
			 * we ? This is just experimental, seems ok
			 */
			usprintf (nrr_mbx_user, nrrstr);
			usflush (nrr_mbx_user);
		}

		free (nrrstr);

		log (-1, "this was requested by us - stop and display");

		return 0;
	}

	/*
	 * Make sure it goes back to where it came from !
	 */
	memcpy (n3hdr->dest, n3hdr->source, AXALEN);
	memcpy (n3hdr->source, Nr_iface->hwaddr, AXALEN);

	/* need space for transport header + calls + flags */
	len = 5 + (nrrcount + 1) * (AXALEN + 1);

   	if ((*bp = alloc_mbuf (len)) == NULLBUF)
   		return 0;
  
	(*bp)->cnt = len;

	ptr = (*bp)->data;

	*ptr++ = 0x00;
	*ptr++ = 0x01;
	*ptr++ = 0x00;
	*ptr++ = 0x00;
	*ptr++ = 0x00;

	for (cnt = 0; cnt < nrrcount; cnt++)
	{
		memcpy (ptr, nrrcalls[cnt], AXALEN);
		ptr += AXALEN;

		*ptr++ = nrrflags[cnt];
	}

	/* Add our node to the end of the list */
	memcpy (ptr, Nr_iface->hwaddr, AXALEN);
	ptr += AXALEN;

	/*
	 * 21Sep2005, Maiko (VE4KLM), Based on observations of the
	 * logfiles and traces, I am guessing we should take the flag
	 * value (looks like a sequence number actually) of the last
	 * call in the list, decrement it, then send that decremented
	 * value with our call when we append our call to the list
	 * that we send back to the originator.
	 */

	*ptr = (nrrflags[cnt-1] - 0x01);

	/*
	 * 22Sep2005, Maiko, I believe the most significant bit of the
	 * sequence number is used to flag the 'end point node' in the
	 * path of the NRR sequence. The originator usually see's the
	 * asterick (*) character displayed beside the callsign of the
	 * 'end point node', so I think this bit takes care of that.
	 */

	*ptr++ |= 0x80;

	/* That's it ! send it off */

	return 1;
}

/* 03Mar2006, Maiko, Function pulled out of inp3rtns.c */

int donrr (int argc, char **argv, void *p)
{
	char remote[AXALEN];

	setcall (remote, argv[1]);

	nrr_send (remote);

	return 0;
}

#endif

