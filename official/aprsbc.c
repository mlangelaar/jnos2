
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
 * 27Nov2001, VE4KLM, Moved the Broadcast functions like
 * the old 'bcstat' and 'bcpos' into this module. I wanted
 * to add more flexibility to the way the broadcasts were
 * done. Instead of clogging up the original aprssrv.c
 * module, might as well isolate the BC stuff to it's own
 * source module. Easier to maintain it that way ...
 *
 */

#include "global.h"

#ifdef	APRSD

#include "aprs.h"

#define	MAXBCSTRS	4

#define	BC_INETSTAT	0
#define	BC_INETPOS	1
#define	BC_RFSTAT	2
#define	BC_RFPOS	3

#define	MAXBCTIMERS	2

#define	BC_INETTIMER	0
#define	BC_RFTIMER		1

/*
 * 16May2001, VE4KLM, Added code so that this IGATE itself will start
 * sending status/position reports to the world wide APRS network.
 */

static int32 bc_timers[MAXBCTIMERS] = { 0, 0 };

static char *bc_strs[MAXBCSTRS] = { (char*)0, (char*)0, (char*)0, (char*)0 };

/* 30Nov2001, Maiko, Let's give option to specify TNOSaprs version in
 * status broadcasts.
 */
static char bc_version[MAXBCTIMERS] = { 0x01, 0x01 };

/* 27Nov2001, Maiko, Some commands to manage broadcasts */

static void usagebccfg (void)
{
#ifdef	APRSC
	tprintf ("Usage: aprs bc [rfver] [on|off]\n");
	tprintf ("       aprs bc [rfstat|rfpos] [data]\n");
	tprintf ("       aprs bc [rftimer] [minutes]\n");
#else
	tprintf ("Usage: aprs bc [ver|rfver] [on|off]\n");
	tprintf ("       aprs bc [stat|pos|rfstat|rfpos] [data]\n");
	tprintf ("       aprs bc [timer|rftimer] [minutes]\n");
#endif
}

int doaprsbc (int argc, char *argv[], void *p OPTIONAL)
{
	if (argc != 3)
		usagebccfg ();
	/*
	 * Both RF and INET can have separate strings
	 * for Status and Position information
	 */
#ifndef	APRSC
	else if (!stricmp (argv[1], "stat"))
		bc_strs[BC_INETSTAT] = j2strdup (argv[2]);
	else if (!stricmp (argv[1], "pos"))
		bc_strs[BC_INETPOS] = j2strdup (argv[2]);
#endif
	else if (!stricmp (argv[1], "rfstat"))
		bc_strs[BC_RFSTAT] = j2strdup (argv[2]);
	else if (!stricmp (argv[1], "rfpos"))
		bc_strs[BC_RFPOS] = j2strdup (argv[2]);

	/*
	 * Both RF and INET can have separate BC timers,
	 * entered in minutes, adjusted internally for the
	 * set_timer calls (in milliseconds).
	 */
#ifndef	APRSC
	else if (!stricmp (argv[1], "timer"))
		bc_timers[BC_INETTIMER] = atoi (argv[2]) * 60000;
#endif
	else if (!stricmp (argv[1], "rftimer"))
		bc_timers[BC_RFTIMER] = atoi (argv[2]) * 60000;

	/*
	 * 30Nov2001, Now able to toggle inclusion of the APRS
	 * server version in the status broadcasts.
	 */
#ifndef	APRSC
	else if (!stricmp (argv[1], "ver"))
	{
		if (!stricmp (argv[2], "on"))
			bc_version[BC_INETTIMER] = 1;
		else
			bc_version[BC_INETTIMER] = 0;
	}
#endif
	else if (!stricmp (argv[1], "rfver"))
	{
		if (!stricmp (argv[2], "on"))
			bc_version[BC_RFTIMER] = 1;
		else
			bc_version[BC_RFTIMER] = 0;
	}

	return 0;
}

/* 28Nov2001, Maiko, Moved all the broadcast functions into
 * this module now, including timer setup, etc.
 */

/* 27Nov2001, Maiko, Separate timers now for RF and INET broadcasts */
static struct timer aprs_inetid_t, aprs_rfid_t;

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
    char msgbuf[150], *savptr, *strptr, *ptr = msgbuf;

	/* 23May2001, VE4KLM, Only broadcast if one is configured */
	/* 27Nov2001, Maiko, Now using separate BC module calls */
	strptr = bc_strs[BC_INETPOS];

	/* only try if string is allocated and valid Hsocket exists */
	if (strptr && getHsocket () != -1)
	{
		/* 02Nov2002, Maiko, Forgot that NOSaprs can message now */
    	sprintf (msgbuf, "%s>%s:=%s\n", getlogoncall (),
			getaprsdestcall (), strptr);

		/* 14Sep2003, Maiko, Posit length can not exceed 62 bytes */
		if (strlen (strptr) > 62)
			aprslog (-1, "Inet position not sent, exceeds 62 characters");
		else
		{
			if (aprs_debug)
				aprslog (-1, "Sending INET Position [%s]", msgbuf);

    		if (j2send (getHsocket (), msgbuf, strlen (msgbuf)/* + 1*/, 0) < 0)
        		tprintf ("internet - can't send igate position\n");

			update_outstats (strlen (msgbuf) /* + 1 */);
		}
	}

	/* 23May2001, VE4KLM, Only broadcast if one is configured */
	/* 27Nov2001, Maiko, Now using separate BC module calls */
	strptr = bc_strs[BC_INETSTAT];

	/* only try if string is allocated and valid Hsocket exists */
	if (strptr && getHsocket () != -1)
	{
    	ptr += sprintf (ptr, "%s>%s:>", getlogoncall (), getaprsdestcall ());

		savptr = ptr;

		if (bc_version[BC_INETTIMER])
			ptr += sprintf (ptr, "%s ", TASVER);

		ptr += sprintf (ptr, "%s\n", strptr);

		/* 14Sep2003, Maiko, Status length can not exceed 62 bytes */
		if ((ptr - savptr) > 62)
			aprslog (-1, "Inet status not sent, exceeds 62 characters");
		else
		{
			if (aprs_debug)
				aprslog (-1, "Sending INET Status [%s]", msgbuf);

    		if (j2send (getHsocket (), msgbuf, strlen (msgbuf)/* + 1*/, 0) < 0)
    	    	tprintf ("internet - can't send igate status\n");

			update_outstats (strlen (msgbuf)/* + 1*/);
		}
	}

#ifdef	JNOSAPRS
	start_timer (&aprs_inetid_t);	/* restart the timer */
#else
#ifdef	TNOS241
	start_detached_timer (__FILE__, __LINE__, &aprs_inetid_t);
#else
	start_detached_timer (&aprs_inetid_t);
#endif
#endif

}

#endif	/* end of APRSC */

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
	strptr = bc_strs[BC_RFPOS];

	/* if RF broadcast string is not defined, then try use
	 * the internet broadcast string if it is defined.
	 */
	if (!strptr)
		strptr = bc_strs[BC_INETPOS];

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

#ifndef	APRSC

/*
 * 27Nov2001, Maiko, Now starts up the two timers, one for
 * the RF side and one for the INET side. Timer intervals are
 * now configurable. Used to be hardcoded to 30 minutes.
 * 28Nov2001, Maiko, Now that it's in the aprsbc.o module,
 * this is no longer a static function anymore.
 * 23Jan2002, Maiko, Split the old startigateident () into
 * two separate functions (one for RF, one for INET). This
 * had to be done since Asocket has become permanent, and
 * the aprsx function was rewritten. Now, if we have no
 * connection to the APRS internet system, there is no
 * reason why we can't continue the RF broadcasts, since
 * the APRS server (Asocket) is always running.
 */

void startinetbc (void)
{
	/* 28Nov2001, Only start timers if intervals are defined (ie, non zero) */
	if (bc_timers[BC_INETTIMER])
	{
		aprs_inetid_t.func = (void (*)(void*)) inetident; /* call when expired */

		aprs_inetid_t.arg = NULL;

		/* 27Nov2001, Used to be hardcoded for every 30 min */
		set_timer (&aprs_inetid_t, bc_timers[BC_INETTIMER]);

		inetident ((void*)0);	/* send one out now, don't wait for timer */
	}
}

/* 23Jan2002, Maiko, Function to stop bc timer */
void stopinetbc (void)
{
	stop_timer (&aprs_inetid_t);
}

#endif	/* end of APRSC */

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

#endif

