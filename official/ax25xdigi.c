
/*
 * Customized cross port (interface) digipeating code
 *
 * For both APRS and Conventional Packet routing
 *
 * New for May, 2005 - JNOS 2.0c5
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 * This module contains my own cross digipeating code, and is a brand
 * new file I started writing in mid May of 2005. This code will let NOS
 * take packets received on one interface and digipeat them to another
 * interface IF the packet contains a specific digi call. The source,
 * destination, and digi calls to trigger on are all customizeable.
 *
 * You can configure as many cross digi rules as you wish. This was
 * originally thought of specific to APRS since a couple of people
 * had asked for it, but then I thought - wait this is generic and
 * can be used for all of the AX25 based stuff.
 */

#include "global.h"

#include "ctype.h"

#ifdef	AX25_XDIGI

/* 11Oct2005, Maiko, DOS compile will fail without this */
#ifdef	MSDOS
#include "mailbox.h"
#endif

#include "ax25.h"

/* Type definitions */

typedef struct rules_xdigi {

	struct rules_xdigi *next;

	struct iface *src_iface;
	struct iface *dst_iface;

	char cdigi[AXALEN];

	char sdigi[AXALEN];

} XDIGI_RULES;

/* static variables */

static XDIGI_RULES	*xdigi_rules = (XDIGI_RULES*)0;

/*
 * Function : displays current Cross Port Digipeater Rules
 */

void ax25_showxdigi (void)
{
	XDIGI_RULES *ptr = xdigi_rules;

	char itmp[20];

	if (ptr == (XDIGI_RULES*)0)
	{
		tprintf ("no cross port digi rules defined. To add one, use :\n");
		tprintf (" ax25 xdigi [src port] [dst port] [digicall] [subcall]\n");
	}
	else
	{
		tprintf ("current cross port digi rules :\n");
		while (ptr != (XDIGI_RULES*)0)
		{
			tprintf ("from [%s] to [%s] on call [%s]",
				ptr->src_iface->name, ptr->dst_iface->name,
					pax25 (itmp, ptr->cdigi));

			if (ptr->sdigi)
				tprintf (" substitute call [%s]", pax25 (itmp, ptr->sdigi));

			tprintf ("\n");

			ptr = ptr->next;
		}
	}
}

/*
 * Function : adds a rule to the Cross Port Digipeater Rules table
 *
 * Parameters are passed in *user readable* format. In other words,
 * the source and destination ports are by name, as is the callsign
 * that we want to use as the cross port digipeater.
 */
int ax25_addxdigi (char *srcifc, char *dstifc, char *cdigicall, char *sdigicall)
{
	XDIGI_RULES *ptr;

	log (-1, "xdigi rule - from port [%s] to port [%s] on call [%s]",
		srcifc, dstifc, cdigicall);

	ptr = (XDIGI_RULES*)calloc (1, sizeof(XDIGI_RULES));

	if (ptr == (XDIGI_RULES*)0)
	{
		log (-1, "ax25_addxdigi : no memory, errno %d", errno);
		return 0;
	}

	if ((ptr->src_iface = if_lookup (srcifc)) == NULLIF)
	{
		log (-1, "ax25_addxdigi : no such port [%s]", srcifc);
		return 0;
	}

	if ((ptr->dst_iface = if_lookup (dstifc)) == NULLIF)
	{
		log (-1, "ax25_addxdigi : no such port [%s]", dstifc);
		return 0;
	}

	if (setcall (ptr->cdigi, cdigicall) == -1)
	{
		log (-1, "ax25_addxdigi : setcall failed [%s]", cdigicall);
		return 0;
	}

	if (sdigicall)
	{
		if (setcall (ptr->sdigi, sdigicall) == -1)
		{
			log (-1, "ax25_addxdigi : setcall failed [%s]", sdigicall);
			return 0;
		}
	}
	else *ptr->sdigi = 0;

	ptr->next = xdigi_rules;

	xdigi_rules = ptr;

	return 1;
}

/*
 * Function : determines if the passed digipeater callsign qualifies
 *            as a crossport digipeater as per our rules table.
 *
 * The parameter, 'idest', is the digipeater call (internal format).
 */
struct iface *ax25_xdigi (struct iface *iface, char *idest, struct ax25 *hdr)
{
	static struct iface *dst_iface;

	XDIGI_RULES *ptr = xdigi_rules;

	/* char itmp[20]; for debugging only, 05Dec2020, Maiko */

	/*
	log (-1, "search xdigi [%s] incoming iface [%s]",
		pax25 (itmp, idest), iface->name);
	*/

	dst_iface = NULLIF;

	ptr = xdigi_rules;

	while (ptr != (XDIGI_RULES*)0)
	{
		if (iface == ptr->src_iface)
		{
			/* log (-1, "match xdigi [%s]", pax25 (itmp, ptr->cdigi)); */

			if (addreq (idest, ptr->cdigi))
			{
				/* log (-1, "dst iface [%s]", ptr->dst_iface->name); */

				if (ptr->dst_iface != iface)
				{
					dst_iface = ptr->dst_iface;
					break;
				}
			}
		}

		ptr = ptr->next;
	}

	if (dst_iface != NULLIF)
	{
		/* if a substitute call is defined, use it instead */
		if (ptr->sdigi)
			memcpy (hdr->digis[hdr->ndigis], ptr->sdigi, AXALEN);
		else
			memcpy (hdr->digis[hdr->ndigis], idest, AXALEN);

		hdr->ndigis++;
	}

	return dst_iface;
}

#endif

