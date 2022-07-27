
/*
 * APRS Services for JNOS 111x, TNOS 2.30 & TNOS 2.4x
 *
 * April,2001-June,2004 - Release (C-)1.16+
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 */

#include "global.h"

#ifdef	APRSD

#include "mbuf.h"

/* 01Feb2002, Maiko, DOS compile for JNOS will complain without these */
#ifdef	MSDOS
#include "iface.h"
#include "arp.h"
#include "slip.h"
#endif

#include "ax25.h"

#include <time.h>

#include "aprs.h"	/* 16May2001, VE4KLM, Finally added prototypes */

static int maxaprshrd = 0;

typedef struct {

  char callsign[AXALEN];
  char dti;
  int packets;
  time_t last;
  /* 16Aug2001, Maiko, Now track which port heard on */
  const char *iface_name;

  /* 20Aug2001, VE4KLM, Playing with digis, etc */
  int ndigis;
  /* 21Aug2001, VE4KLM, Added digi calls (would be nice to see) */
  char digis[MAXDIGIS][AXALEN];

} APRSHRD;

/* static APRSHRD aprshrd[MAXAPRSHRD]; */
static APRSHRD *aprshrd;

static int topcnt = 0;

/*
 * 31May2001, VE4KLM, Heard list can now be dynamically resized by the
 * sysop while TNOS is running, and is now initialized in autoexec.nos
 * file. Previously I had a hardcoded array of 20 entries max.
 */
void init_aprshrd (int hsize)
{
	if (maxaprshrd)
	{
		free (aprshrd);

		maxaprshrd = 0;

		topcnt = 0;
	}

	/* 28Aug2001, VE4KLM, User should be allowed to
	 * disable the heard table on the fly.
	 */
	if (hsize == 0)
		return;

	aprshrd = (APRSHRD*)malloc (sizeof(APRSHRD) * hsize);

	if (aprshrd == (APRSHRD*)0)
	{
		tprintf ("not enough memory for APRS heard list\n");
		return;
	}

	maxaprshrd = hsize;
}

#ifdef	APRS_14501

/*
 * 03Aug2001, VE4KLM, To calculate the amount of memory to allocate
 * for the 14501 page, we need to know the current number of entries
 * in the APRS heard list.
 */
int get_topcnt (void)
{
	return (topcnt);
}

#endif

/*
 * 17Oct2001, Maiko, New Function returns a pointer to a string
 * that identifies the name of the interface on which the passed
 * call was last heard on. The passed call is in internal format.
 */

const char *iface_hrd_on (char *callsign)
{
	register APRSHRD *ptr = aprshrd;
	register int cnt;

	for (cnt = 0; cnt < topcnt; cnt++, ptr++)
	{
		if (addreq (ptr->callsign, callsign))
			return ptr->iface_name;
	}

	return NULL;
}

/*
 * Function : display_aprshrd
 *
 * DISPLAY the APRS Heard table
 */

/* 31Jul2001, VE4KLM, New function for HTML and non-HTML statistics */
static int show_aprshrdlst (char *dptr, int html)
{
	register APRSHRD *ptr = aprshrd;
	char tmp[AXBUF], *sptr = dptr;
	register int cnt, idx;
	time_t curtime;
	int dspflg;
	char *tptr;

	for (cnt = 0; cnt < topcnt; cnt++, ptr++)
	{
		dspflg = 0;

		/* 17Aug2001, Display interface if different from main port */
		if (strcmp (ptr->iface_name, aprs_port))
			dspflg |= 0x01;

		/* 20Aug2001, VE4KLM, Playing with digis */
		if (ptr->ndigis)
			dspflg |= 0x02;

		/* 21Aug2001, Lowercase the callsigns (it takes up alot less room)
		 * when viewing an HTML page
		 */

		tptr = strlwr (pax25 (tmp, ptr->callsign));

#ifdef	APRS_14501

		if (html)
		{
			dptr += sprintf (dptr, "<tr><td>");

			if (locator)
				dptr += sprintf (dptr, "<a href=\"%s%s\">%s</a>", locator, tptr, tptr);
			else
				dptr += sprintf (dptr, "%s", tptr);

			dptr += sprintf (dptr, "</td><td>");

			/* 12Apr2002, Maiko, Cosmetics */
			if (dtiheard)
				dptr += sprintf (dptr, "%c", ptr->dti);
			else
				dptr += sprintf (dptr, "n/a");

			dptr += sprintf (dptr, "</td><td>%s</td><td>%d</td><td>",
				tformat ((int32)(time (&curtime) - ptr->last)), ptr->packets);

			if (dspflg & 0x01)
				dptr += sprintf (dptr, "I %s", ptr->iface_name);

			if (dspflg & 0x02)
			{
				if (dspflg & 0x01)
					dptr += sprintf (dptr, ", ");

				dptr += sprintf (dptr, "P ");

				for (idx = 0; idx < ptr->ndigis; idx++)
				{
					if (idx)
						dptr += sprintf (dptr, " ");

				/* 05Sept2001, Maiko, Added '*' repeated flags if present */
					dptr += sprintf (dptr, "%s%s",
						strlwr (pax25 (tmp, ptr->digis[idx])),
                    		(ptr->digis[idx][ALEN] & REPEATED) ? "*" : "");
				}
			}

			if (!dspflg)
				*dptr++ = '-';

			dptr += sprintf (dptr, "</td></tr>");
		}
		else
#else
		if (1)
#endif
		{
			/* 12Apr2002, Maiko, More cosmetics */
			if (dtiheard)
				tprintf ("%-15.15s%c    ", tptr, ptr->dti);
			else
				tprintf ("%-14.14sn/a   ", tptr);

			tprintf ("%s  %-4d  ", tformat ((int32)(time (&curtime) - ptr->last)), ptr->packets);

			if (dspflg & 0x01)
				tprintf ("I %s", ptr->iface_name);

			if (dspflg & 0x02)
			{
				if (dspflg & 0x01)
					tprintf (", ");

				tprintf ("P ");

				for (idx = 0; idx < ptr->ndigis; idx++)
				{
					if (idx)
						tprintf (" ");

				/* 05Sept2001, Maiko, Added '*' repeated flags if present */
					tprintf ("%s%s", strlwr (pax25 (tmp, ptr->digis[idx])),
                   		(ptr->digis[idx][ALEN] & REPEATED) ? "*" : "");
				}
			}

			if (!dspflg)
				tprintf ("-");

			tprintf ("\n");
		}
	}

	if (html)
		return (dptr - sptr);
	else
		return 1;
}

#ifdef	APRS_14501

/* 31Jul2001, VE4KLM, new function for the http 14501 port statistics */
int build_14501_aprshrd (char *dptr)
{
	char *sptr = dptr;

	if (maxaprshrd == 0)
	{
		dptr += sprintf (dptr, "<h4>APRS heard table not configured</h4>");
		return (dptr - sptr);
	}


	/* 03Jan2001, Maiko, Might as well put this instead of empty table */
	if (topcnt == 0)
	{
		dptr += sprintf (dptr, "<h4>No APRS traffic heard so far</h4>");
		return (dptr - sptr);
	}

	/* 16Aug2001, Maiko, We now track the port user was heard on */
	dptr += sprintf (dptr, "<h4>APRS Traffic Heard</h4><table border=1 cellpadding=3 bgcolor=\"#00ddee\"><tr><td>Callsign</td><td>DTI</td><td>Since</td><td>Pkts</td><td>Other Info (I=Interface, P=Path)</td></tr>");

	dptr += show_aprshrdlst (dptr, 1);

	dptr += sprintf (dptr, "</table><br>");

	return (dptr - sptr);
}

#endif

/* this function is the regular aprsstat function, modified 31Jul2001
 * to use above function
 */
void display_aprshrd (void)
{
	if (maxaprshrd == 0)
	{
		tprintf ("\nAPRS heard table not configured\n\n");
		return;
	}

	/* 03Jan2001, Maiko, Might as well put this instead of empty table */
	if (topcnt == 0)
	{
		tprintf ("\nNo APRS traffic heard so far\n\n");
		return;
	}

	/* 16Aug2001, Maiko, We now track the port user was heard on */
	tprintf ("\nAPRS traffic heard:\nCallsign      DTI     Since     Pkts  Other Info (I=Interface, P=Path)\n");

	show_aprshrdlst (NULL, 0);

	tprintf ("\n");
}

/*
 * Function : update_aprshrd
 *
 * Update the APRS Heard table
 *
 * 16Aug2001, Maiko, Changed parameters for this function, need more
 * information for statistic purposes and for future enhancements.
 */

void update_aprshrd (struct ax25 *hdr, struct iface *iface, char dti)
{
	register APRSHRD *ptr = aprshrd, *repptr = (APRSHRD*)0;
	char *callsign = hdr->source;
	time_t curtime, oldest;
	register int cnt;

	/* 31May2001, VE4KLM, If heard was not configured, then leave now !!! */
	if (maxaprshrd == 0)
		return;

	/* first go through the entire list see if we can update */

	for (cnt = 0; cnt < topcnt; cnt++, ptr++)
	{
		/*
		 * 02Apr2002, Maiko, now have option to include/exclude the DTI
		 * character from the tracking index, the heard table can get
		 * quite large, especially if one station is using a variety
		 * of different DTI types. New global variable, 'dtiheard'.
		 */
		if (addreq (ptr->callsign, callsign) && (!dtiheard || (dtiheard && ptr->dti == dti)))
		{
			ptr->packets++;		/* increment # of packets received */
			ptr->last = time (&curtime);	/* save time heard */

			/* 16Aug2001, Maiko, Now track which port it was heard on */
			ptr->iface_name = iface->name;

			ptr->ndigis = hdr->ndigis;	/* 20Aug2001, Playing with digis */
			/* 21Aug2001, VE4KLM, Okay, let's just save the digi info */
			memcpy (ptr->digis, hdr->digis, hdr->ndigis * AXALEN);

			return;		/* that's it, let's get out of here */
		}
	}

	if (topcnt < maxaprshrd)
	{
		memcpy (ptr->callsign, callsign, AXALEN);

		/* 02Apr2002, Maiko, DTI tracking can now be excluded */
		if (dtiheard)
			ptr->dti = dti;
		else
			ptr->dti = ' ';

		ptr->packets = 1;
		ptr->last = time (&curtime);

		/* 16Aug2001, Maiko, Now track which port it was heard on */
		ptr->iface_name = iface->name;

		ptr->ndigis = hdr->ndigis;	/* 20Aug2001, Playing with digis */
		/* 21Aug2001, VE4KLM, Okay, let's just save the digi info */
		memcpy (ptr->digis, hdr->digis, hdr->ndigis * AXALEN);

		topcnt++;
	}
	else
	{
		/*
	 	 * If we get here it means the table is full. At this point we
		 * have no choice but to start overwriting the oldest data with
		 * the newest stuff coming in. 31May2001, VE4KLM, completed !!!
		 */

		repptr = (APRSHRD*)0;
		ptr = aprshrd;

		/* locate the oldest record, save a pointer to it */
		for (oldest = 0, cnt = 0; cnt < topcnt; cnt++, ptr++)
		{
			if (ptr->last > oldest)
			{
				oldest = ptr->last;

				repptr = ptr;	/* replacement pointer */
			}
		}

		/* replace this record with the latest information */

		if (repptr != (APRSHRD*)0)
		{
			memcpy (repptr->callsign, callsign, AXALEN);

			/* 02Apr2002, Maiko, DTI tracking can now be excluded */
			if (dtiheard)
				repptr->dti = dti;
			else
				repptr->dti = ' ';

			repptr->packets = 1;
			repptr->last = time (&curtime);

			/* 16Aug2001, Maiko, Now track which port it was heard on */
			repptr->iface_name = iface->name;

			repptr->ndigis = hdr->ndigis; /* 20Aug2001, Playing with digis */
			/* 21Aug2001, VE4KLM, Okay, let's just save the digi info */
			memcpy (repptr->digis, hdr->digis, hdr->ndigis * AXALEN);
		}
	}

	return;
}

#endif

