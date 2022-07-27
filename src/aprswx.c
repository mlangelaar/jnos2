
/*
 * APRS Services for JNOS 111x, TNOS 2.30 & TNOS 2.4x
 *
 * April,2001-June,2004 - Release (C-)1.16+
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 * APRS Server - Status/Position BC routines
 * 
 * 01Jul2004, Maiko, More or less a copy of aprsbc module,
 * but suited for WX broadcasts using data read from files.
 * 
 * This is somewhat a rushed idea just so that people can use
 * it for their WX purposes. I really have to give this some
 * more thought (ie the aprsbc and aprswx stuff).
 *
 */

#include "global.h"

#ifdef	APRSD

#include "aprs.h"

#define	MAXWXSTRS	4

#define	WX_INETSTAT	0
#define	WX_INETPOS	1
#define	WX_RFSTAT	2
#define	WX_RFPOS	3

#define	MAXWXTIMERS	2

#define	WX_INETTIMER	0
#define	WX_RFTIMER		1

static int32 wx_timers[MAXWXTIMERS] = { 0, 0 };

static char *wx_strs[MAXWXSTRS] = { (char*)0, (char*)0, (char*)0, (char*)0 };

static char *wx_callsign = (char*)0;
static char *wx_filename = (char*)0;

/* Scrap the VERSION stuff - I never did like the feature */

static void usagewxcfg (void)
{
	tprintf ("Usage: aprs wx call [callsign]\n");
	tprintf ("       aprs wx [stat|pos] [data]\n");
	tprintf ("       aprs wx [timer] [minutes]\n");
	tprintf ("       aprs wx data [filename]\n\n");
	tprintf ("NOTE : Wx to RF not implemented (yet) !!!\n");
}

int doaprswx (int argc, char *argv[], void *p OPTIONAL)
{
	if (argc != 3)
		usagewxcfg ();

	/* The WX broadcasts have their own callsign !!! */
	else if (!stricmp (argv[1], "call"))
	{
		wx_callsign = j2strdup (argv[2]);
		strupr (wx_callsign);
	}
	/* The WX data is read from a file !!! */
	else if (!stricmp (argv[1], "data"))
		wx_filename = j2strdup (argv[2]);
	/*
	 * Both RF and INET can have separate strings
	 * for Status and Position information
	 */
#ifndef	APRSC
	else if (!stricmp (argv[1], "stat"))
		wx_strs[WX_INETSTAT] = j2strdup (argv[2]);
	else if (!stricmp (argv[1], "pos"))
		wx_strs[WX_INETPOS] = j2strdup (argv[2]);
#endif
	else if (!stricmp (argv[1], "rfstat"))
		wx_strs[WX_RFSTAT] = j2strdup (argv[2]);
	else if (!stricmp (argv[1], "rfpos"))
		wx_strs[WX_RFPOS] = j2strdup (argv[2]);

	/*
	 * Both RF and INET can have separate BC timers,
	 * entered in minutes, adjusted internally for the
	 * set_timer calls (in milliseconds).
	 */
#ifndef	APRSC
	else if (!stricmp (argv[1], "timer"))
		wx_timers[WX_INETTIMER] = atoi (argv[2]) * 60000;
#endif

	/* 01Jul2004, Maiko, No WX to rf broadcasts at this time !!! */

#ifdef	DONT_COMPILE
	else if (!stricmp (argv[1], "rftimer"))
		wx_timers[WX_RFTIMER] = atoi (argv[2]) * 60000;
#endif

	return 0;
}

static struct timer wx_inetid_t;

#ifndef	APRSC

/*
 * 27Nov2001, Maiko, Separate functions now for INET and RF
 * broadcasts, used to be just 'igateident()', now we have
 * the 'inetident' and 'rfident' functions.
 *
 * 01Jul2004, Maiko, Switch it so that POSIT goes out first,
 * then the STAT, some clients don't recognize object or station
 * unless they get the POSIT first. Initial STAT was getting
 * ignored before.
 *
 */

static void inetident (void *t OPTIONAL)
{
    char msgbuf[150], wx_data[100];
	char *savptr, *strptr, *ptr = msgbuf;
	int wx_ret, anythingsent = 0;

	strptr = wx_strs[WX_INETPOS];

	/* only try if string is allocated and valid Hsocket exists */
	if (strptr && getHsocket () != -1)
	{
    	sprintf (msgbuf, "%s>%s,%s,I:!%s\n", wx_callsign,
			getaprsdestcall (), getlogoncall (), strptr);

		/* 14Sep2003, Maiko, Posit length can not exceed 62 bytes */
		if (strlen (strptr) > 62)
			aprslog (-1, "Inet position not sent, exceeds 62 characters");
		else
		{
			if (aprs_debug)
				aprslog (-1, "Sending WX INET Position [%s]", msgbuf);

    		if (j2send (getHsocket (), msgbuf, strlen (msgbuf) /*+ 1*/, 0) < 0)
        		tprintf ("internet - can't send igate position\n");

			update_outstats (strlen (msgbuf) /*+ 1*/);

			anythingsent = 1;
		}
	}

	/* 23May2001, VE4KLM, Only broadcast if one is configured */
	/* 27Nov2001, Maiko, Now using separate BC module calls */
	strptr = wx_strs[WX_INETSTAT];

	/* only try if string is allocated and valid Hsocket exists */
	if (strptr && getHsocket () != -1)
	{
    	ptr += sprintf (ptr, "%s>%s,%s,I:>", wx_callsign,
				getaprsdestcall (), getlogoncall ());

		savptr = ptr;

		ptr += sprintf (ptr, "%s\n", strptr);

		/* 14Sep2003, Maiko, Status length can not exceed 62 bytes */
		if ((ptr - savptr) > 62)
			aprslog (-1, "Inet status not sent, exceeds 62 characters");
		else
		{
			if (aprs_debug)
				aprslog (-1, "Sending WX INET Status [%s]", msgbuf);

    		if (j2send (getHsocket (), msgbuf, strlen (msgbuf) /*+ 1*/, 0) < 0)
    	    	tprintf ("internet - can't send igate status\n");

			update_outstats (strlen (msgbuf) /*+ 1*/);

			anythingsent = 1;
		}
	}

	/*
	 * now read the Wx data file (provided status and posit sent)
	 * and send the DATA to the APRS internet system
	 */
	if (anythingsent)
	{
		FILE *fp;

		if ((fp = fopen (wx_filename, "r")))
		{
			wx_ret = fscanf (fp, "%s", wx_data);

			aprslog (-1, "data [%s] ret %d", wx_data, wx_ret);

			fclose (fp);

			if (wx_ret == 1 && getHsocket () != -1)
			{
				ptr = msgbuf;	/* VERY Important !!! */

    			ptr += sprintf (ptr, "%s>%s,%s,I:", wx_callsign,
					getaprsdestcall (), getlogoncall ());

				savptr = ptr;

				ptr += sprintf (ptr, "%s\n", wx_data);

				if ((ptr - savptr) > 62)
					aprslog (-1, "Wx data not sent, exceeds 62 characters");
				else
				{
					if (aprs_debug)
						aprslog (-1, "Sending WX data [%s]", msgbuf);

    				if (j2send (getHsocket (), msgbuf, strlen (msgbuf)/* + 1*/, 0) < 0)
    	    			tprintf ("internet - can't send wx data\n");

					update_outstats (strlen (msgbuf) /*+ 1*/);
				}
			}
		}
	}

#ifdef	JNOSAPRS
	start_timer (&wx_inetid_t);	/* restart the timer */
#else
#ifdef	TNOS241
	start_detached_timer (__FILE__, __LINE__, &wx_inetid_t);
#else
	start_detached_timer (&wx_inetid_t);
#endif
#endif

}

#endif	/* end of APRSC */

#ifdef	DONT_COMPILE

/*
 * 01Jul2004, Maiko, For RF I'll have to send this as a 3rd party
 * because really it's not the callsign of the IGATE. Will have to
 * think about this one for a while, so NO wx to rf right now !
 */

/*
 * 27Nov2001, Maiko, Separate functions now for INET and RF
 * broadcasts, used to be just 'igateident()', now we have
 * the 'inetident' and 'rfident' functions.
 *
 * 01Jul2004, Maiko, Switch it so that POSIT goes out first,
 * then the STAT, some clients don't recognize object or station
 * unless they get the POSIT first. Initial STAT was getting
 * ignored before.
 *
 */

static void rfident (void *t OPTIONAL)
{
    char msgbuf[150], *savptr, *strptr, *ptr = msgbuf;

	/* 23May2001, VE4KLM, Only broadcast if one is configured */
	/* 27Nov2001, Maiko, Now using separate BC module calls */
	strptr = wx_strs[WX_RFPOS];

	/* if RF broadcast string is not defined, then try use
	 * the internet broadcast string if it is defined.
	 */
	if (!strptr)
		strptr = wx_strs[WX_INETPOS];

	/* only try if string is allocated */
	if (strptr)
	{
		/* 03Dec2001, Maiko, Do not put a NEWLINE on RF traffic */
		/* 02Nov2002, Maiko, Forgot that NOSaprs can message now */
    	sprintf (msgbuf, "=%s", strptr);

		/* 14Sep2003, Maiko, Posit length can not exceed 62 bytes */
		if (strlen (strptr) > 62)
			aprslog (-1, "RF position not sent, exceeds 62 characters");
		else
		{
			if (aprs_debug)
				aprslog (-1, "Sending RF Position [%s]", msgbuf);

			/* 16Jul2001, Do not send the NEWLINE and NULL out to RF */
			if (sendrf (msgbuf, strlen (msgbuf)) == -1)
        		tprintf ("sendrf - can't send igate position\n");
		}
	}

	/* 23May2001, VE4KLM, Only broadcast if one is configured */
	/* 27Nov2001, Maiko, Now using separate BC module calls */
	strptr = bc_strs[BC_RFSTAT];

	/* if RF broadcast string is not defined, then try use
	 * the internet broadcast string if it is defined.
	 */
	if (!strptr)
		strptr = bc_strs[BC_INETSTAT];

	/* only try if string is allocated */
	if (strptr)
	{
    	ptr += sprintf (ptr, ">");

		savptr = ptr;

		if (bc_version[BC_RFTIMER])
			ptr += sprintf (ptr, "%s ", TASVER);

		ptr += sprintf (ptr, "%s", strptr);

		/* 14Sep2003, Maiko, Status length can not exceed 62 bytes */
		if ((ptr - savptr) > 62)
			aprslog (-1, "RF status not sent, exceeds 62 characters");
		else
		{
			if (aprs_debug)
				aprslog (-1, "Sending RF Status [%s]", msgbuf);

			/* 16Jul2001, Do not send the NEWLINE and NULL out to RF */
			if (sendrf (msgbuf, strlen (msgbuf)) == -1)
        		tprintf ("sendrf - can't send igate status\n");
		}
	}

#ifdef	JNOSAPRS
	start_timer (&aprs_rfid_t);	/* restart the timer */
#else
#ifdef	TNOS241
	start_detached_timer (__FILE__, __LINE__, &aprs_rfid_t);
#else
	start_detached_timer (&aprs_rfid_t);
#endif
#endif

}

#endif	/* end of DONT_COMPILE */

#ifndef	APRSC

void startinetwx (void)
{
	/* 28Nov2001, Only start timers if intervals are defined (ie, non zero) */
	if (wx_timers[WX_INETTIMER])
	{
		wx_inetid_t.func = (void (*)(void*)) inetident; /* call when expired */

		wx_inetid_t.arg = NULL;

		/* 27Nov2001, Used to be hardcoded for every 30 min */
		set_timer (&wx_inetid_t, wx_timers[WX_INETTIMER]);

		inetident ((void*)0);	/* send one out now, don't wait for timer */
	}
}

/* 23Jan2002, Maiko, Function to stop bc timer */
void stopinetwx (void)
{
	stop_timer (&wx_inetid_t);
}

#endif	/* end of APRSC */

#ifdef	DONT_COMPILE

/* 01Jul2004, Maiko, No WX to rf right now due to complications */

void startrfbc (void)
{
	/* 28Nov2001, Only start timers if intervals are defined (ie, non zero) */
	if (bc_timers[BC_RFTIMER])
	{
		aprs_rfid_t.func = (void (*)(void*)) rfident; /* call when expired */

		aprs_rfid_t.arg = NULL;

		/* 27Nov2001, Used to be hardcoded for every 30 min */
		set_timer (&aprs_rfid_t, bc_timers[BC_RFTIMER]);

		rfident ((void*)0);	/* send one out now, don't wait for timer */
	}
}

/* 23Jan2002, Maiko, Function to stop bc timer */
void stoprfbc (void)
{
	stop_timer (&aprs_rfid_t);
}

#endif	/* end of DONT_COMPILE */

#endif

