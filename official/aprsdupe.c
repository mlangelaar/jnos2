
/*
 * APRS Services for JNOS 111x, TNOS 2.30 & TNOS 2.4x
 *
 * April,2001-June,2004 - Release (C-)1.16+
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 * 14Nov2001, VE4KLM, MY attempt at tracking and blocking duplicate
 * packets. I need to prevent duplicates in cases like EMAIL server,
 * and other possible services down the road.
 *
 * 14Nov2002, Maiko, added parameter to aprs_dup () function that
 * let's us specifiy the *age limit* (so to speak) of the packet we
 * are analysing. Before it was hardcoded to 5 minutes for email, but
 * now that I am playing with APRS digipeating, I'd like the ability
 * to configure it as needed (or on the fly).
 */

#include "global.h"

#ifdef	APRSD

#include "ctype.h"

#ifndef MSDOS
#include <time.h>
#endif

#include "aprs.h"

#define	MAXDUPS	20

typedef struct {

	int16	checksum;
	time_t	last;

} DUPTBL;


static DUPTBL duptbl[MAXDUPS];

static int topcnt = 0;

/*
 * 14Nov2001, Maiko, Taken from smtpserv.c and modified for
 * use in my APRS server code. Compute CCITT CRC-16 of the
 * passed data.  This is 16 bit CRC-CCITT stuff, extracted
 * from Bill Simpson's PPP code.
 */

extern int16 fcstab[];

#define FCS(fcs, c) (((fcs) >> 8) ^ fcstab[((fcs) ^ (c)) & 0x00ff])

static int16 calc_aprs_crc (char *callsign, char *payload)
{
	int16 crc, len;

	crc = 0xffff;

	len = (int16) strlen (callsign);
	while (len--)
		crc = FCS(crc, *callsign++);

	len = (int16) strlen (payload);
	while (len--)
		crc = FCS(crc, *payload++);

	crc ^= 0xffff;

	return crc;
}

int aprs_dup (char *callsign, char *payload, int age_seconds)
{
 	int16 checksum = calc_aprs_crc (callsign, payload);
	time_t curtime = time (&curtime);
	int cnt;

	for (cnt = 0; cnt < topcnt; cnt++)
	{
		/* check if this checksum already exists in our dup table */
		if (duptbl[cnt].checksum == checksum)
		{
			/*
			 * make sure it isn't old, if so discard it !!! right now
			 * I consider old to mean anything longer than 5 minutes.
			 * 14Nov2002, Maiko, the age is now a function argument.
			 */
			if ((curtime - duptbl[cnt].last) < age_seconds)
				return 1;

			duptbl[cnt].checksum = 0;	/* free the old record for reuse */
		}
	}

	/*
	 * At this point, it's not a dupe, but a first time packet,
	 * so look for an empty slot to put it in if any exist. If
	 * not, put it at the end of the list for now.
	 */

	for (cnt = 0; cnt < topcnt; cnt++)
	{
		/* insert new dup table entry in free slot */
		if (duptbl[cnt].checksum == 0)
		{
			duptbl[cnt].checksum = checksum;	
			duptbl[cnt].last = curtime;

			break;
		}
	}


	/* I should really do this like I did the APRS Heard
	 * table but this is still just an experiment. I just
	 * want some basic protection again duplicate emails
	 * right now, nothing more. This function will get
	 * written better in a subsequent release, it will do
	 * for now. I don't think we will get a full table
	 * on email requests for now :-)
	 */

	if (cnt == topcnt)
	{
		if (topcnt == MAXDUPS)
		{
			aprslog (-1, "warning - can't track any more dupes !!!");
		}
		else
		{
			duptbl[cnt].checksum = checksum;

			duptbl[cnt].last = curtime;

			topcnt++;
		}
	}

	return 0;
}

/* 11Jun2003, Maiko, USER really doesn't need this function */

#ifdef	DEVELOPMENT

/* 27Nov2001, Maiko, Some commands to manage packet dups */

static void usagedupcfg (void)
{
	tprintf ("Usage: aprs dup show | test\n");
}

int doaprsdup (int argc, char *argv[], void *p OPTIONAL)
{
	time_t curtime = time (&curtime);

	int cnt;

	if (argc == 1)
		usagedupcfg ();

	else if (!stricmp (argv[1], "show"))
	{
		tprintf ("APRS Dupe Table (for debugging)\nChecksum  Age\n");

		for (cnt = 0; cnt < topcnt; cnt++)
		{
			tprintf ("%-10.8d %ld\n", duptbl[cnt].checksum,
				curtime - duptbl[cnt].last);
		}
	}

	/* 20Mar2006, Maiko, Testing routines for me to debug aprsdigi */
	else if (!stricmp (argv[1], "test"))
	{
		static char wide_dest[7] = { 'W' << 1, 'I' << 1, 'D' << 1, 'E' << 1, '3' << 1, ' ' << 1, '1' << 1 };

		static char wide_dest2[7] = { 'W' << 1, 'I' << 1, 'D' << 1, 'E' << 1, '2' << 1, ' ' << 1, '2' << 1 };

		static char source[7] = { 'V' << 1, 'E' << 1, '4' << 1, 'R' << 1, 'A' << 1, 'G' << 1, '0' << 1 };

		static char ss_dest[7] = { 'S' << 1, 'P' << 1, '3' << 1, ' ' << 1, ' ' << 1, ' ' << 1, '1' << 1 };

		static char ss_dest2[7] = { 'S' << 1, 'P' << 1, '2' << 1, ' ' << 1, ' ' << 1, ' ' << 1, '2' << 1 };

		char tmp[AXBUF], idest[AXALEN];
		struct ax25 hdr;
		int nomark, ret;

		tprintf ("Testing aprsdigi routine (for debugging)\n");

		memcpy (hdr.source, source, AXALEN);

		memcpy (idest, wide_dest, AXALEN);
		tprintf ("incoming digi call [%s]\n", pax25 (tmp, idest));

		ret = aprs_digi (&hdr, idest, &nomark);
		tprintf ("ret %d, outgoing digi call [%s], nomark %d\n",
			ret, pax25 (tmp, idest), nomark);

		memcpy (idest, wide_dest2, AXALEN);
		tprintf ("incoming digi call [%s]\n", pax25 (tmp, idest));

		ret = aprs_digi (&hdr, idest, &nomark);
		tprintf ("ret %d, outgoing digi call [%s], nomark %d\n",
			ret, pax25 (tmp, idest), nomark);

		ret = aprs_digi (&hdr, idest, &nomark);
		tprintf ("ret %d, outgoing digi call [%s], nomark %d\n",
			ret, pax25 (tmp, idest), nomark);

		ret = aprs_digi (&hdr, idest, &nomark);
		tprintf ("ret %d, outgoing digi call [%s], nomark %d\n",
			ret, pax25 (tmp, idest), nomark);

		memcpy (idest, ss_dest, AXALEN);
		tprintf ("incoming digi call [%s]\n", pax25 (tmp, idest));

		ret = aprs_digi (&hdr, idest, &nomark);
		tprintf ("ret %d, outgoing digi call [%s], nomark %d\n",
			ret, pax25 (tmp, idest), nomark);

		memcpy (idest, ss_dest2, AXALEN);
		tprintf ("incoming digi call [%s]\n", pax25 (tmp, idest));

		ret = aprs_digi (&hdr, idest, &nomark);
		tprintf ("ret %d, outgoing digi call [%s], nomark %d\n",
			ret, pax25 (tmp, idest), nomark);

		ret = aprs_digi (&hdr, idest, &nomark);
		tprintf ("ret %d, outgoing digi call [%s], nomark %d\n",
			ret, pax25 (tmp, idest), nomark);

		ret = aprs_digi (&hdr, idest, &nomark);
		tprintf ("ret %d, outgoing digi call [%s], nomark %d\n",
			ret, pax25 (tmp, idest), nomark);
	}

	return 0;
}

#endif

#endif

