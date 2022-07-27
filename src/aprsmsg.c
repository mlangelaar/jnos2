
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

/*
 * 04Apr2004, Maiko, Beginnings of new browser based message center
 */

#include "global.h"

#ifdef	APRSD

#include "ctype.h"
#ifdef	JNOSAPRS
#include "cmdparse.h"
#endif
#include "commands.h"

#ifndef MSDOS
#include <time.h>
#endif

#include <sys/stat.h>
#include "mbuf.h"

#ifdef  MSDOS
#include "socket.h"
#include "internet.h"
#endif

#include "netuser.h"
#include "ip.h"
#include "files.h"
#include "session.h"
#include "iface.h"
#include "udp.h"
#include "mailbox.h"
#include "usock.h"
#include "mailutil.h"
#include "smtp.h"

#include "aprs.h"	/* 16May2001, VE4KLM, Added prototypes finally */

#include "j2KLMlists.h"	/* 19Jun2020, Maiko (VE4KLM), new header file */

/*
#include <stdio.h>
#include <stdlib.h>
*/

#define       MAXTPRINTF      (SOBUF - 5)

/* ---------- get_Host ----------*/

static int get_Host (char *bp, char *host_cp)
{
	char *tmp_cp = host_cp;

	if (aprs_debug)
		aprslog (-1, "bp [%.30s]", bp);

	while (*bp)
	{
		if (!memcmp ("Host", bp, 4))
		{
			bp += 6;	/* skip ': ' that follows */

			while (*bp && *bp != '\r' && *bp != '\n') /* get host name */
			{
				*tmp_cp++ = *bp++;
			}	

			*tmp_cp = 0;

			return 1;
		}

		bp++;
	}

	return 0;
}

/* ---------- get_Host_URL ----------*/

static int get_Host_URL (char *bp, char *url_cp, char *host_cp)
{
	char *tmp_cp = url_cp;

	int host_len_i;

	if (!get_Host (bp, host_cp))
	{
		aprslog (-1, "No HOST defined in HTTP Header");
		return 0;
	}

	host_len_i = strlen (host_cp);

	while (*bp)
	{
		if (!memcmp ("GET", bp, 3))
		{
			bp += 4;	/* skip ' ' that follows */

			while (*bp && *bp != ' ') /* get URL path */
			{
				*tmp_cp++ = *bp++;
			}	

			*tmp_cp = 0;

			tmp_cp = url_cp;

			/* now strip out the host name from URL */
			while (*tmp_cp)
			{
				if (!memcmp (host_cp, tmp_cp, host_len_i))
				{
					/* skip the '/' at end of host name */
					strcpy (url_cp, tmp_cp + host_len_i + 1);
					return 1;
				}
				tmp_cp++;
			}
			return 0;
		}
		bp++;
	}

	return 0;
}

static char httpdata_ca[2000], URL_ca[100], Host_ca[40];

extern int build_response (char*, char*);

extern void set_amm (int);	/* 14Jun2004, Maiko */

char glob_url_ca[50];

void serv45845 (int s, void *unused OPTIONAL, void *p OPTIONAL)
{
	struct sockaddr_in fsocket;
	struct mbuf *bp;
	char *ptr;
	int len_i = sizeof(fsocket);

#ifndef	JNOSAPRS
	server_disconnect_io ();
#endif
	Curproc->output = s;

	aprslog (s, "connected");

	/*
	 * 14Jun2004, Maiko, Now have IP Access list for the 45845
	 * service, unless in the 'aprs calls ip45845 ...' list, all
	 * clients that connect are only able to monitor. If you don't
	 * want to allow access at all, then use ip tables on top of
	 * this. The 'set_amm ()' function is in aprsmsgr.c module.
	 */

	/* need to get some information of the client connecting */
	if (j2getpeername (s, (char *) &fsocket, &len_i) == -1)
	{
		aprslog (s, "disconnect, unable to get client information");
		close_s (s);
		return;
	}

	set_amm (ip_allow_45845 (inet_ntoa (fsocket.sin_addr.s_addr)));

	while (recv_mbuf (s, &bp, 0, NULLCHAR, NULL) > 0)
	{
		KLMWAIT (NULL);	/* give other processes a chance */

		len_i = len_p (bp);

		/* protection against buffer overflow perhaps (shud never happen) */
		if (len_i > 1995)
			len_i = 1995;

		(void) pullup (&bp, httpdata_ca, len_i);

		free_p (bp);

		get_Host_URL (httpdata_ca, URL_ca, Host_ca);

		if (aprs_debug)
		{
			aprslog (s, "URL [%.100s]", URL_ca);
			aprslog (s, "Host [%.100s]", Host_ca);
		}

		sprintf (glob_url_ca, "http://%s", Host_ca);

		len_i = build_response (httpdata_ca, URL_ca);

		/* 01Aug2001, VE4KLM, Arrggg !!! - the tprintf call uses vsprintf
		 * which has a maximum buf size of SOBUF, which itself can vary
		 * depending on whether CONVERSE or POPSERVERS are defined. If
		 * we exceed SOBUF, TNOS is forcefully restarted !!! Sooooo...,
		 * we will have to tprintf the body in chunks of SOBUF or less.
		 *
		tprintf ("%s", body);
		 */

		if (aprs_debug)
			aprslog (s, "response %d bytes", len_i);

		ptr = httpdata_ca;

		while (len_i > 0)
		{
			KLMWAIT (NULL);	/* give other processes a chance */

			if (len_i < MAXTPRINTF)
			{
				tprintf ("%s", ptr);
				len_i = 0;
			}
			else
			{
				tprintf ("%.*s", MAXTPRINTF, ptr);
				len_i -= MAXTPRINTF;
				ptr += MAXTPRINTF;
			}
		}

		break;	/* force termination of the connection */
	}

	aprslog (s, "disconnected");
}

#endif

