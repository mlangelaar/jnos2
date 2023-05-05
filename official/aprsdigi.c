
/*
 * APRS Services for JNOS 111x, TNOS 2.30 & TNOS 2.4x
 *
 * April,2001-June,2004 - Release (C-)1.16+
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 * This module contains my own APRS digi code, and is a
 * brand new file, I started writing in November, 2002.
 *
 */

#include "global.h"

#ifdef	APRSD

#include "ctype.h"

#include "aprs.h"

#ifdef	APRS_DIGI

#include "ax25.h"

/* 23Mar2006, Maiko, SSn-N routing - testing for Poland (Janusz) */
static char ss_dest[2] = { 'S' << 1, 'P' << 1 };
static char ss_dest2[2] = { 'D' << 1, 'L' << 1 };

static char wide_dest[4] = { 'W' << 1, 'I' << 1, 'D' << 1, 'E' << 1 };

/* 30Mar2006, Maiko, Better support RELAY for backwards compatibility */
static char relay_dest[5] = {
	'R' << 1, 'E' << 1, 'L' << 1, 'A' << 1, 'Y' << 1
};

static char space_char = ' ' << 1;	/* 23Mar2006, Maiko, callsign space */

extern int aprsdigi;	/* 14Nov2002, Maiko, Declared in aprssrv.c module */

extern int debugdigi;	/* 23Mar2006, Maiko, New debug flag for aprs flags */

/*
 * 07/08 November 2002 - Finished and tested preliminary WIDEn-N
 *
 * 13November, added a very basic duplicate packet check, so that
 * the WIDEn-N frames don't get into a ping-pong match with other
 * digipeaters in the area, etc. Not sure how well it will work,
 * but I'm sure it will be better than absolutely nothing.
 *
 * 21Mar2006, Maiko (VE4KLM), Updated to comply with the new-N
 * paradigm that is being touted in the APRS world. Plain WIDE
 * as well as RELAY is to be completely phased out. It's now
 * exclusively WIDEn-N, where n and N should be no bigger
 * than the value 3 (at worst case for open spaces).
 */

int aprs_digi (struct ax25 *hdr, char *idest, int *nomark)
{
	char tmp[AXBUF], chksum[20], n_char, N_char;

	int ssnN = 0, retval = 0;

	*nomark = 0;

	/* If digipeating is disabled, then leave now ! */
	if (!aprsdigi)
	{
		if (debugdigi)
			aprslog (-1, "digipeating disabled");

		return 0;
	}

	/*
	 * if repeated already, then drop it - should never happen actually
	 * since if it is repeated, the ax25.c code would never let it get
	 * here, since it would not be considered an immediate destination.
	 */
	if (*(idest+ALEN) & REPEATED)
	{
		if (debugdigi)
			aprslog (-1, "repeated already");

		return 0;
	}

	/*
	 * 30Mar2006, Maiko, Easiest way for me to process a RELAY is to
	 * simply convert it to a WIDE1-1 to fool the code into treating
	 * it exactly as a WIDE1-1 (ie, substitute our call and mark it
	 * as a digipeated frame.
	 */
	if (memcmp (idest, relay_dest, 5) == 0)
	{
		if (debugdigi)
			aprslog (-1, "got RELAY, convert to WIDE1-1");
		
		setcall (idest, "WIDE1-1");
	}

	/* must start off as a WIDE and 6th position of call must be space */
	if (memcmp (idest, wide_dest, 4) || (*(idest+5) != space_char))
	{
		if (debugdigi)
			aprslog (-1, "not WIDEn-N");
		/*
		 * 23Mar2006, Maiko, Experimental - Janusz wants SSn-N routing
		 * for Poland (SPn-N) and Germany (DLn-N). Let's see how well
		 * this works for him ...
		 */

		if (memcmp (idest, ss_dest, 2) || (*(idest+3) != space_char))
		{
			if (debugdigi)
				aprslog (-1, "not SPn-N");

			if (memcmp (idest, ss_dest2, 2) || (*(idest+3) != space_char))
			{
				if (debugdigi)
					aprslog (-1, "not DLn-N");

				return 0;
			}
		}

		n_char = *(idest+2);	/* Grab the Maximum hops value */

		ssnN = 1;	/* flag this as an SSn-N routing */
	}
	else
	{
		n_char = *(idest+4);	/* Grab the Maximum hops value */
	}

	if (n_char == space_char)
	{
		if (debugdigi)
			aprslog (-1, "plain WIDE or SS not supported");

		return 0;
	}

	/* convert n_char to integer value */
	n_char = (n_char >> 1) - '0';

	/* 21Mar2006, The 'n' in WIDEn-N or SSn-N must be constrained */
	if (n_char < 1 || n_char > 3)
	{
		if (debugdigi)
			aprslog (-1, "max hops must be within 1 to 3 digipeaters");

		return 0;
	}

	/* Which hop are we on ? Use the SSID of the callsign */
	N_char = *(idest+6);

	if ((N_char & SSID) != 0)
	{
		N_char = (N_char >> 1) & 0xf;

		if (debugdigi)
		{
			if (ssnN)
				aprslog (-1, "got SS%d-%d", n_char, N_char);
			else
				aprslog (-1, "got WIDE%d-%d", n_char, N_char);
		}

		/* build hash (key) to create *payload* checksum value */
		if (ssnN)
			sprintf (chksum, "%sSS%d", pax25 (tmp, hdr->dest), n_char);
		else
			sprintf (chksum, "%sWIDE%d", pax25 (tmp, hdr->dest), n_char);

		if (aprs_dup (pax25 (tmp, hdr->source), chksum, 28))
		{
			if (debugdigi)
				aprslog (-1, "duplicate packet, do NOT digipeat");

			return 0;
		}

		if (N_char == 1)
		{
			/* 30Mar2006, Maiko, Fill in if we get WIDE1-1 */
			if (n_char == 1)
			{
				setcall (idest, logon_callsign);
			    //memcpy (idest, Aprscall, AXALEN);
			}
			else
			{
				*(idest+6) = 0x60;	/* wipe out SSID, set reserved bit */
			}

			/* Now let NOS digi and mark it */
		}
		else
		{
			/* adjust the SSID, let NOS digi, but do not mark as repeated */

 			N_char--;

			*(idest+6) = 0x60 | (N_char << 1);

			*nomark = 1;
		}

		retval = 1;
	}
	else if (debugdigi)
	{
		aprslog (-1, "impossible WIDE%d or SS%d not repeated", n_char, n_char);
	}

	return retval;
}

#endif

#endif

