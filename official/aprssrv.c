
/*
 * APRS Services for JNOS 2.0x, JNOS 111x, TNOS 2.30 & TNOS 2.4x
 *
 * April,2001-May,2005 - Release 2.0
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
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

#include "socket.h"

/* 01Feb2002, Maiko, DOS compile for JNOS will complain without this */
#ifdef  MSDOS
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

/*
 * 19Jun2020, Maiko (VE4KLM), breaking out callsigns lists functionality, and
 * moving the code into a new source file - j2KLMlists.c, since it's not just
 * used for APRS stuff anymore. It's also used for IP access restrictions, and
 * for my most recent download of messages from multiple winlink accounts.
 */

#include "j2KLMlists.h"

#define	ALLOC_BODY	/* This is so that malloc is used to allocate memory
					 * for the body of the HTML 14501 page. I started off
					 * with a static char [] but felt that was wastefull
					 * and not flexible enough. If you don't like the use
					 * of the malloc call, then undefine this. That will
					 * force the use of the static char [], just make sure
					 * it's big enough to hold max aprs heard and the max
					 * number of igate servers in the server list, etc.
					 */

#define	MAXTPRINTF	(SOBUF - 5)

/* 20Jun2001, VE4KLM, The destination call is now defined in aprs.h */
static char *aprsdestcall = APRSDC;

#ifndef	APRSC
static int Hsocket = -1;	/* tcpip connection to aprs.net */
#endif

static int Asocket = -1;	/* udp listener */

static int monitor_aprsnet = 0;	/* monitoring flag Added 18Apr2001 */

extern char Mycall[AXALEN];	/* TNOS mycall identifier */

static char Aprscall[AXALEN];	/* now use logoncallsign, not Mycall in
								 * our broadcasts and ID, etc */

static int bconlyifhrd = 1;		/* only broadcast if recipient heard flag,
								 * think I'll default this to on actually */

int dtiheard = 1;	/* 02Apr2002, Enable or disable DTI tracking in aprs heard table */

/* 14Nov2002, Maiko, New flag to enable/disable APRS digi feature */
/* 29Mar2003, Maiko, Default any APRS digipeating to NO !!! */
int aprsdigi = 0;
/*
 * 23Mar2006, Maiko, Added a flag to turn on debugging for digi code, cause
 * the latest stuff I sent Janusz is compile time DEBUG (ugly). This one is
 * more user friendly for him then.
 */
int debugdigi = 0;

int aprs_debug = 0;		/* debugging flag */

int aprs_poe = 1;	/* 05Sep2001, VE4KLM, Inject POE call flag */

int aprs_badpackets = 0;	/* 09Jan2002, New flag to keep APRS log small :-) */

int32 aprs_is_timeout = 600;			/* every 10 minutes for now */

/* 25Oct2001, Maiko, APRS specific events now have their own logfile */
static FILE *aprsLogfp = NULLFILE;

/*
 * 10Jul2001, Message Handler definitions
 */

#define	MSG_VIA_RF		1
#define	MSG_VIA_IGATE	0

#define	MAXANETSRV	4

#define	MAXANETCLI	10	/* 02Jan2002, New, 22Jan2002, Bumped up to 10 */

#define IGATE_ONLINE 1

typedef struct {

	char *server;	/* 16Jun2002, Maiko, No more length restrictions (alloc) */

	int	port;

	int info_port;	/* 11Dec2001, Maiko, New for HTTP link */

	time_t last;	/* 14May2001, Time Connected */

	time_t since;	/* 07Feb2002, Last heard since */

	char flags;

	/* 02Aug2001, VE4KLM, Try incoming Statistics first */
	long in_bytes;
	long in_packets;
	long out_bytes;
	long out_packets;

	struct timer inactive;	/* 20Jun2002, Inactivity timer */

	long last_packets_in;	/* 22Jun2002, Scrapped, then put back :-) */

	int talive;		/* 29Mar2003, New keep alive timerr interval */

} ANETSRV;

/* 07Jan2002, Need more info for client connections */

typedef struct {

	ANETSRV	srv;

	char *user;

	struct timer t;	/* 08Jan2002, timer and socket info */

	int s;

} ANETCLI;

static ANETSRV	anetsrv[MAXANETSRV];
static ANETCLI	anetcli[MAXANETCLI];	/* 02Jan2001, New, 07Jan changed to ANETCLI */

static	int numcli = 0;

static ANETSRV *nxtsrv;

int32 internal_hostid = 0;	/* the only kludge for now */

/* 08Nov2001, Maiko, Now a true representation of the email address
 *
static char *aprs_email = (char*)0;
 *
 * 21Jun2003, maiko, Now holds any type of information, I've decided
 * to give sysop some flexibility, can be plain text, or a mailto:,
 * or an html link, etc, based on flags passed to 'aprs contact',
 * which was formerly the 'aprs email' function.
 */
static char *aprs_contact = (char*)0;

/*
 * 21Jun2003, maiko, New system where we identify the type of contact,
 * default is 0=mailto:, others presently are 1=text and 2=html link.
 */
static char aprs_contact_type = 0;

/* 03Apr2002, Maiko, Now alloc'd pointers, not fixed size array */
char *aprs_port = (char*)0;
char *logon_callsign = (char*)0;

/* 25Feb2005, Maiko, Now have a logon (APRS IS) filter */
char *logon_filter = (char*)0;

static const char *notdef = "not defined";

static char *lwrlogoncall = (char*)0;	/* 12Apr2002, Cosmetics only */

/* 10Apr2002, Maiko, Locator path (if one wants to click on callsigns
 * and have a quick URL to a site like findu.com or canaprs.net
 */
char *locator = (char*)0;

/*
 * 08Nov2001, VE4KLM, New mode for the APRS Email Server
 *
 * 0 means pass through to APRS IS
 * 1 means handle locally and don't pass onto APRS IS
 * 2 means block any EMAIL requests
 *
 */
static int emailmode = 1;	/* default handle locally */

/*
 * 02Jul2003, Maiko, New modes for MIC-E handing, This variable
 * is reference in the aprs.c and aprsmice.c modules.
 *
 * 0 means ignore MIC-E packets completely
 * 1 means passthru to the APRS IS un-altered
 * 2 means convert before passing to APRS IS
 *
 */
int micemode = 1;	/* default is to pass as is (raw) */

static struct proc *CurConsoleAPRSC = NULL; /* 24May2003 */

/*
 * APRS Server Commands
 */

/* 31May2001, VE4KLM, hsize for configuring APRS heard table size */
static int dohsize (int argc, char *argv[], void *p);

static int doaprslog (int argc, char *argv[], void *p);
static int doaprserver (int argc, char *argv[], void *p);

#ifndef	APRSC
static int dosrvcfg (int argc, char *argv[], void *p);
static int doclicfg (int argc, char *argv[], void *p);
static int dointmsg (int argc, char *argv[], void *p);
static int dolocator (int argc, char *argv[], void *p);
#endif

static int donetlogon (int argc, char *argv[], void *p);

static int dointerface (int argc, char *argv[], void *p);
static int dointernal (int argc, char *argv[], void *p);
static int dolocmsg (int argc, char *argv[], void *p);

/* 09Jan2002, Replaced dodebug and a few others with doflags */
static int doflags (int argc, char *argv[], void *p);

/*
 * 07May2002, 'dofwdtorf' changed to 'docallslist', for multiple
 * callsign lists for different purposes (fwdtorf, bantorf, etc).
 */
extern int docallslist (int argc, char *argv[], void *p);

static void aprsx (int, void *, void *);
static ANETSRV *next_server (int);

/* 04Aug2001, VE4KLM, Added email for the 14501 page */
/* 21Jun2003, Maiko, Changed 'doemail' to 'docontact' */
#ifndef	APRSC
static int docontact (int argc, char *argv[], void *p);
#endif

/* 08Nov2001, Maiko, New APRS Email Server modes */
static int doemailmode (int argc, char *argv[], void *p);

/* 02Jul2003, Maiko, New mode to handle Mic-E processing */
static int domicemode (int argc, char *argv[], void *p);

#ifdef	APRS_14501
/* 22Aug2001, VE4KLM, New function to better alloc body memory */
static int get_srvcnt (void);
#endif

/* 28Nov2001, Maiko, Need a declaration for this function in the
 * smtpserv.c module. Does not seem to be prototyped in any of
 * the standard NOS header files.
 * 04Dec2001, Maiko, In JNOS, this function is declared in mailutil.h
 */
#ifndef	JNOSAPRS
extern int mailuser (FILE *data,const char *from,const char *to,const char *origto);
#endif

/*
 * 27Nov2001, Maiko, Added "BC" and "DUP" subcommands. Moved the
 * bcstat and bcpos into their own module for more flexibility.
 */

/* 09Jun2003, 'email' changed to contact, 'emailmode' changed to 'email' */

#ifdef	JNOSAPRS

static struct cmds DFAR Acmds[] = {

	{ "bc", doaprsbc, 0, 0, NULLCHAR },
#ifdef	DEVELOPMENT
	{ "dup", doaprsdup, 0, 0, NULLCHAR },
#endif
	{ "flags", doflags, 0, 0, NULLCHAR },
	{ "calls", docallslist, 0, 0, NULLCHAR },
#ifndef	APRSC
	{ "client", doclicfg, 0, 0, NULLCHAR },
	{ "contact", docontact, 0, 0, NULLCHAR },
#endif
	{ "hsize", dohsize, 0, 0, NULLCHAR },
	{ "interface", dointerface, 0, 0, NULLCHAR },
	{ "internal", dointernal, 0, 0, NULLCHAR },
	{ "listen", doaprserver, 0, 0, NULLCHAR },
#ifdef	DEVELOPMENT
	{ "lmsg", dolocmsg, 0, 0, NULLCHAR },
#endif
#ifndef	APRSC
	{ "locator", dolocator, 0, 0, NULLCHAR },
#endif
	{ "log", doaprslog, 0, 0, NULLCHAR },
	{ "logon", donetlogon, 0, 0, NULLCHAR },
#ifdef	DEVELOPMENT
	{ "imsg", dointmsg, 0, 0, NULLCHAR },
#endif
#ifndef	APRSC
	{ "server", dosrvcfg, 0, 0, NULLCHAR },
	{ "email", doemailmode, 0, 0, NULLCHAR },
	{ "mice", domicemode, 0, 0, NULLCHAR },
#endif
	{ "status", doaprsstat, 0, 0, NULLCHAR },
	{ "wx", doaprswx, 0, 0, NULLCHAR },
	{ NULLCHAR, NULL, 0, 0, NULLCHAR }
};

#else

static struct cmds Acmds[] =
{
	{ "bc", doaprsbc, 0, 0, NULLCHAR },
#ifdef	DEVELOPMENT
	{ "dup", doaprsdup, 0, 0, NULLCHAR },
#endif
	{ "flags", doflags, 0, 0, NULLCHAR },
	{ "calls", docallslist, 0, 0, NULLCHAR },
	{ "client", doclicfg, 0, 0, NULLCHAR },
	{ "contact", docontact, 0, 0, NULLCHAR },
	{ "hsize", dohsize, 0, 0, NULLCHAR },
	{ "interface", dointerface, 0, 0, NULLCHAR },
	{ "internal", dointernal, 0, 0, NULLCHAR },
	{ "listen", doaprserver, 0, 0, NULLCHAR },
#ifdef	DEVELOPMENT
	{ "lmsg", dolocmsg, 0, 0, NULLCHAR },
#endif
	{ "locator", dolocator, 0, 0, NULLCHAR },
	{ "log", doaprslog, 0, 0, NULLCHAR },
	{ "logon", donetlogon, 0, 0, NULLCHAR },
#ifdef	DEVELOPMENT
	{ "imsg", dointmsg, 0, 0, NULLCHAR },
#endif
	{ "server", dosrvcfg, 0, 0, NULLCHAR },
	{ "email", doemailmode, 0, 0, NULLCHAR },
	{ "mice", domicemode, 0, 0, NULLCHAR },
	{ "status", doaprsstat, 0, 0, NULLCHAR },
	{ "wx", doaprswx, 0, 0, NULLCHAR },
	{ NULLCHAR, NULL, 0, 0, NULLCHAR }
};

#endif

extern int vhttpget (int, char**);

extern int callcheck (char*);   /* 10Mar2009, Maiko, added prototype */

/*
 * 28Nov2001, Maiko, Need some functions to return some of the
 * static variables in this module, needed in aprsbc module. I
 * don't really like doing this (need to structure the globals
 * alot better actually). That can be future cleanup project.
 */

const char *getaprsdestcall (void)
{
	return aprsdestcall;
}

char *getlogoncall (void)
{
	return logon_callsign;
}

int getAsocket (void)
{
	return Asocket;
}

#ifndef	APRSC
int getHsocket (void)
{
	return Hsocket;
}
#endif

/* Send to tnos link level */

int sendrf (char *data, int len)
{
	struct sockaddr_in server_in;

	int s = j2socket (AF_INET, SOCK_DGRAM, 0);

	server_in.sin_family = AF_INET;
	server_in.sin_port = 16161;

	/*
	 * 30May2001, VE4KLM, If this is not set, then set it to the address
	 * of this TNOS box. This basically means that the 'aprs internal'
	 * autoexec.nos entry is no longer required except in special cases.
	 * ALOT of TNOS systems now have linux doing the encap and tunnel, so
	 * for those systems there is no longer any need for 'aprs internal'.
	 */
	if (internal_hostid == 0)
		internal_hostid = Ip_addr;

	server_in.sin_addr.s_addr = internal_hostid;

	if (j2sendto (s, data, len, 0, (char*)&server_in, sizeof(server_in)) == -1)
		return -1;

	close_s (s);	/* 15May2001, Oops !!! Not having this is probably the
				 	 * source of my TNOS SIGSEGV problems around sendrf */
	return 0;
}

/* 09Jul2001, Maiko, Log (with time stamp) data to mail/aprs.txt file */
/* 13Jul2001, Maiko, Now have a new APRS directory under Spool area */
/* 16Oct2001, Maiko, Added few more params for more detail */
/* 27May2005, Maiko, Spool/aprs no longer exists, now using APRSdir */
static void log_aprs_msg (char *ptr, int sending, char *callsign)
{
	char fname[100];
	time_t nowtime;
	FILE *fp;

	/* 27May2005, Maiko, Use the new APRSdir variable now */
	sprintf (fname, "%s/aprs.txt", APRSdir);

	time (&nowtime);

	if ((fp = fopen (fname, "a+")) != NULLFILE)
	{
		fprintf (fp, "%s", ctime (&nowtime));
		fprintf (fp, "%s %s %s\n", callsign, sending ? "sent" : "rcvd", ptr);
		fclose (fp);
	}
}

static void bad_msg (int rf, const char *desc, char *baddata)
{
	if (aprs_badpackets)
	{
		aprslog (-1, "Bad data (%s) from %s - %s",
			desc, rf ? "RF" : "APRS IS", baddata);
	}
}

/*
 * 03Oct2001, Maiko, Separate function to check for a message ID, and
 * then format the proper ack response. I made this a separate function
 * because there are now multiple places in the code where I may be
 * processing incoming messages (ie, the PBBS APRS message center).
 */
static int needmsgack (char *tptr, char *ackmsg)
{
	int len, retval = 0;

	*ackmsg = 0;

	/* search for Message ID identifier */
	while (*tptr && *tptr != '{')
		tptr++;

	/* if one is found, then format the ack message */
	if (*tptr)
	{
		tptr++;	/* skip the identifier */

		/*
		 * make sure we cap the length of the msg id to maximum
		 * of 5 bytes, just incase something is wrong with the data
		 * that we get from the APRS IS - as per the APRS spec.
		 */
		sprintf (ackmsg, "ack%.5s", tptr);

		/* 03Dec2001, Maiko, There could very well be a NEWLINE
		 * character at the end of the incoming message, due to
		 * my code in the aprs.c module, which I should really
		 * change the way I use newlines. Get rid of NEWLINE,
		 * don't let it go out RF in particular.
		 */
		len = strlen (ackmsg);
		if (*(ackmsg+len-1) == '\n')
			*(ackmsg+len-1) = 0;

		retval = 1;
	}

	return retval;
}

/* 05Nov2001, VE4KLM, New email server */

static char DAEMONSTR[] = "%sAPRSSVR(%s)@%s\n";

static int aprsmail (const char *from, const char *data, const char *pathinfo, int pathinfolen)
{
	char tmpfrom[70], to[55], *toptr = to;
	int msglen, cnt_i;
	const char *ptr;
	FILE *tfile;
	long msgid;
	time_t t;

	/* Extract the email recipient from the APRS data */
	/* 05Jun2003, Maiko, Protect from buffer overflows !!! */
	cnt_i = 0;
	while (*data && *data != ' ' && cnt_i < 50)
	{
		*toptr++ = *data++;
		cnt_i++;
	}
	*toptr = 0;

	if (!(*data))
	{
		aprslog (-1, "bad email data [%s]", data);
		return 0;
	}

	data++;	/* skip the space, data now points to body of email */
	
	if ((tfile = tmpfile ()) == NULLFILE)
	{
		aprslog (-1, "can't get a temporary file for email");
		return 0;
	}

	aprslog (-1, "APRS Station %s sending email to %s", from, to);
	aprslog (-1, "Message : %s", data);

	/* count length of the message, excluding msg id at end if one exists */
	for (msglen = 0, ptr = data; *ptr && *ptr != '{'; ptr++, msglen++);

	(void) time (&t);

	fprintf (tfile, "%s%s", Hdrs[DATE], ptime (&t));

	/* 04Dec2001, Maiko, JNOS version has no passed argument */
#ifdef	JNOSAPRS
	msgid = get_msgid ();
#else
	msgid = get_msgid (1);
#endif

	fprintf (tfile, "%s<%ld@%s>\n", Hdrs[MSGID], msgid, Hostname);

	fprintf (tfile, DAEMONSTR, Hdrs[FROM], from, Hostname);
	fprintf (tfile, "%s%s\n", Hdrs[TO], to);
	fprintf (tfile, "%sMessage from APRS station %s\n\n", Hdrs[SUBJECT], from);

	/* 04Dec2001, Maiko, Put in length parameter so that msd id is not sent
	 * to the receipient of this email. Improves cosmetic appearance
	 */
	fprintf (tfile, "%.*s\n---\n", msglen, data);

	fprintf (tfile, "Please DO NOT reply to this email. The above message was received\n");
	fprintf (tfile, "by the %s Server, Amateur Radio Callsign %s.\n", TASVER, logon_callsign);

	/* 21Jun2003, Maiko, No more aprs_email, more options available now */
	fprintf (tfile, "Contact information: ");

	if (aprs_contact)
		fprintf (tfile, "%s", aprs_contact);
	else
		fprintf (tfile, "sysop@%s", Hostname);

	fprintf (tfile, "\nRF Path: %.*s\n", pathinfolen, pathinfo);

	fseek (tfile, 0L, 0);

	/*
	 * 04Dec2001, Maiko, JNOS version has one less passed argument,
	 * and we simply send a NULL string for the FROM information,
	 * since we are logging all details in the APRS.LOG anyways.
	 *
	 * 05Jun2003, Maiko, for the main NOS.log, it would help if we
	 * logged the FROM information, even if it's only the callsign
	 * of the APRS station sending and not a real EMAIL address. I
	 * did this at the request of a couple of sysops that only use
	 * the main NOS logs, and don't have the APRS log configured.
	 * 04Jul2003, Maiko, Oops, needs to have a valid domain name
	 * or else non-local mail will get bounced by external ISP.
	 *
	 * 21Dec2007, Maiko, Took me this long to figure it out, Janusz
	 * actually stumbled across this. The ( and ) characters are not
	 * legal characters in the local part of the email address.
	 *
	 *     sprintf (tmpfrom, "APRSSVR(%s)@%s", from, Hostname);
	 */

	sprintf (tmpfrom, "aprs-%s@%s", from, Hostname);

#ifdef	JNOSAPRS
	(void) mailuser (tfile, tmpfrom, to);
#else
	(void) mailuser (tfile, tmpfrom, to, to);
#endif
	(void) fclose (tfile);

	return 1;
}

/*
 * 27May2005 - Maiko, Beginning to rewrite this function !!!
 *
 * Now that we have the option of completely parsing an APRS data
 * string for the position or whatever, we MUST MAKE SURE that we
 * do NOT reference PAST the end of the data string. Doing so will
 * most certainly bring on some type of system CRASH situation.
 */
int valid_dti_data (char dti, char *data)
{
	char *bp = data;
	int offset = 0;
	int retval = 0;
	int cnt = 0;

	switch (dti)
	{
		case ':':	/* Message */

			while (!retval && *bp)
			{
				if ((cnt == 9) && (*bp == ':'))
					retval = 1;

				cnt++;
				bp++;
			}

			break;

		case '/':	/* Posit - with time stamp, no messaging */
		case '@':	/* Posit - with time stamp, messaging */

			offset = 7;		/* account for time stamp and pass thru */

		case '!':	/* Posit - no time stamp, no messaging */
		case '=':	/* Posit - no time stamp, messaging */

		/*
		 * 29Oct2005, Maiko, Oops - I forgot about compressed data.
		 * 20Dec2008, Maiko, Oops - I forgot the offset, causing '/'
		 * and '@' dti's to get trashed - caught by Ron (VE3CGR).
		 */
			if (*(bp+offset) == '/')
				retval = 2;

			while ((retval < 2) && *bp)
			{
				if ((cnt == (4 + offset)) && (*bp == '.'))
					retval++;

				else if ((cnt == (14 + offset)) && (*bp == '.'))
					retval++;

				cnt++;
				bp++;
			}

			/* here we need a score of 2 to be valid */
			if (retval < 2)
				retval = 0;

			break;

		case 'T':		/* APRS Telemetry Data */

			while ((retval < 4) && *bp)
			{
				if ((cnt == 0) && (*bp == '#'))
					retval++;

				else if ((cnt < 4) && isdigit(*bp))
					retval++;

				cnt++;
				bp++;
			}

			/* here we need a score of 4 to be valid */
			if (retval < 4)
				retval = 0;

			break;

	/* Be forgiving for those DTI that are not validated yet */
		default:
			retval = 1;
			break;
	}

	if (!retval && aprs_badpackets)
		aprslog (-1, "bad - dti [%c] data [%s]", dti, data);

	return retval;
}

/* 08Jan2002, New function. This one will replace
 * the one in aprs.c (but not quite yet).
 */
int valid_dti (char dti)
{
	int retval = 0;	/* default to invalid */

	switch (dti)
	{
		case 0x60:
		case 0x27:
		case 0x1c:
		case 0x1d:
			/* 07Jul2003, Maiko, If we passthru MIC-E, these are valid */
			if (micemode != 1)
				break;

		case 'T':   /* 10Jan2002, Maiko, Oops forgot about APRS telemetry */
		case '@':
		case '!':
		case '#':
		case '$':
		case '%':
		case '*':
		case '/':
		case ':':
		case ';':
		case '<':
		case '=':
		case '>':
		case '?':
		case '_':
			retval = 1;
			break;
	}

	return retval;
}

/* 23Sep2002, Maiko, New function indicate pos or stat frame */
/* 29Mar2003, Maiko, Oops - I forgot the '=' value */
static int dti_pos_or_stat (const char dti)
{
	return (dti == '!' || dti == '@' || dti == '>' || dti == '/' || dti == '=');
}

/* 26Apr2001, Experimental Message Handler */
/* 10Jul2001, Added RF flag so that I can use this function for RF side */
/* 16Jul2001, Igate mode added. If the mode is set to 'manual' then do not
 * send messages from RF to internet, unless the IGATE alias is in the path.
 */
static int message_handler (char *data, int rf)
{
    char callsign[15], *csp = callsign, dti;
	char srccall[15], *ssp = srccall, *ptr;
    int cnt, mymsg, retval, srclen = 0;

	/* Directed Queries and Route Trace support, 29May2001 */
	char tracebuf[100], *savebp = data;
	/* 03Oct2001, Maiko, Bump up mymsgargv size, mods to dointmsg, dolocmsg */
	char *mymsgargv[4], tbuf[300];
	int rfallowed, sendack, dupe;
	void *vp = (void*)0;

	/*
	 * The return value of this function has different meanings depending
	 * on whether the incoming data is from the Internet or local RF port.
	 */
	if (rf)
		retval = 1;
	else
		retval = 0;
        /*
         * 28May2004, Maiko, Working on dedicated APRS Client code
         */
#ifdef  APRSC
        if (*data == '}')
        {
                aprslog (-1, "this is a 3rd party message");
                *data++;
        }
#else
	/*
	 * 25May2001, VE4KLM, Unfortunately, we get a fair amount of garbage
	 * from the internet stream. The easiest way to keep alot of it out, is
	 * to check if the data starts with an alphanumeric character. This will
	 * also screen out the REMARKS and COMMENTS that some servers put out.
	 */
	if (!isalnum (*data))
		return retval;
#endif
	/* Extract the source callsign of this packet */
	srclen = 0;
	while (*data && *data != '>')
	{
		if (srclen > 9)	/* safety feature, in case we get garbage calls */
		{
			bad_msg (rf, "Source Callsign", savebp);
			return retval;
		}
		*ssp++ = *data++;
		srclen++;
	}
	*ssp = 0;	/* make sure you terminate source callsign string */

	/*
	 * 08May2002, Maiko, If this is from the RF side, then check if the
	 * source call is in the 'ignorerf' list. If so, then stop here.
	 */
	if (rf && callsign_ignored (srccall))
	{
		if (aprs_debug)
			aprslog (-1, "source [%s] ignored", srccall);

		return retval;
	}

	/* 03Oct2001, Maiko, Moved this up here, it's used for more stuff now */
	mymsgargv[1] = srccall;

	/* 25Sep2001, Skip delimiter */
	if (*data == '>')
		data++;
	/*
	 * 13Jul2001, VE4KLM, Ignore calls beginning with APRS because they are
	 * nothing more than server stats. Drop them now, so we don't waste CPU
	 * time, processing a bunch of stuff we'll never use for anything !
	 */
	if (strnicmp (srccall, "aprs", 4) == 0)
		return retval;

	/* skip to end of source path header */
	while (*data && *data != ':')
	{
		/*
		 * 25Sep2001, Maiko, Better checks put in to make sure we don't
		 * get clobbered with bad data from the APRS internet system. This
		 * replaces my old check for soley the '}' characters.
		 *
		 * 26Sep2001, Maiko, Forgot about '/' for dual port tncs. Fixed.
		 *
		 * 14Sep2001, VE4KLM, Put in a check to discard '}' characters
		 * that suddenly appeared in the source path header from the APRS
		 * IS, corrupted packets were coming in, crashing my system.
		 *
		 * 03Dec2001, Maiko, Looks like KH2Z is using '@' for igate POE now.
		 */
		if (!isalnum (*data) && (*data != '-') && (*data != '/') &&
			(*data != '*') && (*data != ',') && (*data != '@'))
		{
			/* 25Sep2001, replaced with a common function */
			bad_msg (rf, "Source Path Header", savebp);
			return retval;
		}

		data++;
	}

	/* just to be on the safe side */
	if (!(*data))
	{
		/* 25Sep2001, replaced with a common function */
		bad_msg (rf, "Truncated Packet", savebp);
		return retval;
	}

	data++;	/* skip the source path header delimiter */

	dti = *data++; /* Get the DTI (Data Type Identifier) */

	/* 08Jan2002, Maiko, Now that I'm accepting client connections, it's
	 * time to make sure we stick with VALID DTI characters as per the latest
	 * specification. I've already noticed one of my clients (APRS+SA) sending
	 * me the old style messages, where there is no DTI character, or the
	 * source path termination character is missing.
	 */
	if (!valid_dti (dti))
	{
		bad_msg (rf, "Bad or missing DTI", savebp);
		return retval;
	}

	/*
	 * 11Jan2002, Maiko, Now make sure the DTI data is at least somewhat
	 * good. New function started to validate stuff particular to the DTI
	 * character in use for this APRS packet.
	 */
	if (!valid_dti_data (dti, data))
		return retval;

	retval = 0;	/* reset, cause the return value of this function
				 * is different if message comes from IGATE or RF.
				 */

#ifdef	APRS_POSIT_DB
	/* 24Jun2005, Maiko, New tracking of positions for display later */
	if (dti == '!')
		aprs_db_add_position (srccall, data);
#endif

	if (dti == ':') /* Direct or 3rd Party Message */
    {
		/* 29May2001, VE4KLM, Route trace support */
		/* 02Sep2010, Maiko, You must use (int) for the field precision */
		sprintf (tracebuf, "PATH= %.*s", (int)(data - savebp - 2), savebp);

        /* extract the callsign of the 3rd party */
		srclen = 0;
        while (*data && *data != ' ' && *data != ':')
		{
			/* 28Aug2001, Better check the recipient length as well */

			if (srclen > 9)	/* safety feature, incase we get garbage */
			{
				/* 25Sep2001, replaced with a common function */
				bad_msg (rf, "Recipient Callsign", savebp);

				if (rf)
					return (1);
				else
					return (0);
			}

			*csp++ = *data++;

			srclen++;
		}

        *csp = 0;   /* terminate callsign string */

		/*
		 * Check if this message is destined for this NOSaprs server, and
		 * set a flag to indicate this. This flag is used in several places
		 * further on in the code.
	 	 */
		mymsg = 0;
		if (strcmp (callsign, logon_callsign) == 0)
			mymsg = 1;

        /* 28May2004, Maiko, Working on dedicated APRS client */
#ifndef APRSC
		/*
		 * 24Aug2002, Maiko, Check if this message is destined for any of
		 * my connected clients. If so, we should forward it to them.
		 */
		for (cnt = 0; cnt < MAXANETCLI; cnt++)
		{
			ANETCLI *cptr = &anetcli[cnt];

			ANETSRV *aptr = &cptr->srv;

			/* only forward to configured clients that are online */
			if (aptr->server && aptr->flags == IGATE_ONLINE)
			{
				/* see if recipient callsign matches a particular client */
				if (stricmp (callsign, cptr->user) == 0)
				{
					if (aprs_debug)
						aprslog (-1, "forwarding msg to [%s] client", callsign);

					strcat (data, "\n");	/* need to add this, since it
											 * was stripped to begin with
											 */
					if (aprs_debug)
						aprslog (-1, "data [%d] [%s]", strlen (data), data);

					if (j2send (cptr->s, data, strlen (data), 0) < 0)
						aprslog (-1, "unable to send forward to client");

					/* That's it, nothing else to do !!! */
					if (rf)
						return (1);
					else
						return (0);
				}
			}
		}

		/*
		 * If this is from the APRS IS and is NOT destined for this NOSaprs
		 * server, then it will have to go out the RF port. Before we allow
		 * that, we have to screen the recipient callsign against a couple of
		 * callsign lists (ie, the 'fwdtorf' and 'bantorf' lists) to make sure
		 * that this message is indeed allowed to go out the RF port.
		 */
		rfallowed = 1;
		if (!rf && !mymsg)
		{
			if (!callsign_allowed (callsign))
				rfallowed = 0;

			/* 08May2002, Maiko, Now have a banned callsign list too */
			else if (callsign_banned (callsign))
			{
				if (aprs_debug)
					aprslog (-1, "recipient [%s] banned", callsign);

				rfallowed = 0;
			}
		}
#else
		rfallowed = 1;
#endif
        /* skip remaining white space */
        while (*data && *data != ':')
            data++;

        /* skip the message text delimiter */
        data++;

        /* at this point we have the message text */
		/* 24May2001, VE4KLM, Added source call, nice to know who it is */
		/* 18Oct2001, Maiko, Don't show this if call is not allowed on RF */
		if (rfallowed && (aprs_debug || mymsg))
           	aprslog (-1, "[%s] to [%s] %s", srccall, callsign, data);

#ifndef APRSC
		/*
		 * 05Nov2001, VE4KLM, Now intercept EMAIL requests, from RF only,
		 * we sure as heck don't want to be processing stuff we get from
		 * the internet side (not yet anyways).
		 * 08Nov2001, Maiko, Added email modes.
	 	 */
		if (emailmode && rf && strcmp (callsign, "EMAIL") == 0)
		{
			if (emailmode == 1)
			{
				aprslog (-1, "email handled locally.");

				/* 20Nov2001, Maiko, Check for duplicate email packet and
				 * only send the email once. IF it's a dupe, it means that
				 * the ack we sent back probably didn't make it, so just
				 * send another one back and see if that one makes it.
				 * 14Nov2002, Maiko, Now pass maximum AGE allowed for
				 * duplicate email requests.
			 	 */
				dupe = aprs_dup (srccall, data, 300);

				if (!dupe)
					sendack = aprsmail (srccall, data, savebp, (data - savebp - 2));
				else
				{
					aprslog (-1, "duplicate email, just ack, nothing more");
					sendack = 1;
				}

				if (sendack)
				{
					/* 09Nov2001, Maiko, Reply with a fake 3rd party message,
					 * this is the only way to make sure the ACK is properly
					 * processed at the originating APRS client. The usual
					 * direct message will not work, since we are not the
					 * station with call EMAIL, we are the station with call
					 * VE4UMR-5. First construct the start of a possible ACK
					 * message, so that if a message id is found, we can
					 * append the ack and complete the message.
					 */
					ptr = tbuf;
    				ptr += sprintf (ptr, "}%s>%s::%-9.9s:", callsign, aprsdestcall, srccall);

					/* 08Nov2001, Maiko, Check for message id, if one is found, then
					 * complete the prebuilt message above, and send it out RF. The only
					 * thing we have to do now is check for duplicate EMAIL requests.
					 */
					if (needmsgack (data, ptr))
					{
						if (sendrf (tbuf, strlen (tbuf)) == -1)
       						aprslog (-1, "sendrf - can't send email ack");
					}

					if (!dupe)
						smtptick (NULL);  /* wake SMTP to send that mail */
				}

				return (1);	/* don't let this go out to the internet */
			}

			if (emailmode == 2)
			{
				aprslog (-1, "email blocked");
				return (1);
			}
		}

		/*
		 * 02Oct2001, VE4KLM, See if the recipient of the message is
		 * currently logged into the PBBS, if so, send it there, and
		 * leave. This code will currently not work for JNOS, sorry.
		 *
		 * 18Oct2001, Maiko, Time to try this on a JNOS system, so
		 * that once again, both TNOS and JNOS will have matching
		 * functionality from the NOSaprs perspective.
		 */

		if (!mymsg)
		{
			/* this is VERY experimental, it's not perfect by a long shot */

			struct mbx *mbxptr = NULLMBX;

#ifdef	JNOSAPRS

        	for (mbxptr = Mbox; mbxptr; mbxptr = mbxptr->next)
			{
			/* name is lower case, callsign is uppercase, use strcasecmp */
				if (!stricmp (mbxptr->name, callsign))
					break;
        	}
#else
			for (cnt = 0; cnt < NUMMBX; cnt++)
			{
				if (Mbox[cnt] != NULLMBX)
				{
				/* name is lower case, callsign is uppercase, use strcasecmp */
					if (!stricmp (Mbox[cnt]->name, callsign))
						break;
				}
			}

			if (cnt != NUMMBX)
				mbxptr = Mbox[cnt];
#endif
			if (mbxptr != NULLMBX)
			{
				/* 03Oct2001, Maiko, Make sure we ack this message
				 * for the user of the PBBS APRS message center.
				 */
				if (needmsgack (data, tbuf))
				{
					mymsgargv[2] = tbuf;

					/* make sure source call is the PBBS user !!! */
					mymsgargv[3] = mbxptr->name;

					/* 10Jul2001, Check if IGATE or RF route */
					if (rf)
						dolocmsg (4, mymsgargv, vp);
					else
						dointmsg (4, mymsgargv, vp);
				}

				/* format the message for PBBS user */
				sprintf (tbuf, "%s: %s\n", srccall, data);
				/* 13Oct2001, Maiko, Log PBBS messages from the internet */
				/* 16Oct2001, Maiko, Added few more params for more detail */
				(void) log_aprs_msg (tbuf, 0, callsign);

				usprintf (mbxptr->user, tbuf);
				usflush (mbxptr->user);

				/* kpause (5000); */

				return retval;	/* message delivered, no point going further */
			}
		}
#endif
		/* 18Oct2001, Maiko, We now do this after the PBBS 'see if the user
		 * is using the APRS message center' check above.
		 */
		if (!rfallowed)
			return retval;

		/* At this point, the message is allowed to go out to the RF port,
		 * or it belongs to 'myself'.
		 */
		if (!mymsg)
		{
			if (!rf)
				retval = 1;
		}
		else	
		{
			if (rf)
				retval = 1;

			/* 29May2001, VE4KLM, Respond to some Directed Queries */

			msgdb_save (srccall, callsign, data);

			/*
			 * 09Jul2001, Now ack any messages that have a message id. We
			 * should really fork something out to deal with the ack cause
			 * we should send several acks over a period of a few minutes
			 * to ensure the receipient will get it. This is just a test
			 * to see if the following code will work out or not.
			 *
			 * 03Oct2001, Maiko, Put the ack processing in it's own
			 * function (reduces code duplication) because I now have
			 * to do ack processing in several places.
			 */

			if (needmsgack (data, tbuf))
			{
				mymsgargv[2] = tbuf;
#ifdef	APRSC
                dolocmsg (3, mymsgargv, vp);
#else
				/* 10Jul2001, Check if IGATE or RF route */
				if (rf)
					dolocmsg (3, mymsgargv, vp);
				else
					dointmsg (3, mymsgargv, vp);
#endif
				msgdb_save (callsign, srccall, tbuf);
			}

			/*
			 * 09Jul2001, VE4KLM, Log messages (with time stamp) to a
			 * file now, as well, send to the console so that the sysop
			 * can finally do some real-time messaging.
			 */

			/* format the console message */
			sprintf (tbuf, "%s: %s\n", srccall, data);

			/* 09Jul2001, Maiko, Log messages from the internet */
			/* 16Oct2001, Maiko, Added few more params for more detail */
			(void) log_aprs_msg (tbuf, 0, callsign);

			/*
			 * 10Oct2001, VE4KLM, No more SYSOP mode for messaging, now
			 * that I have the APRS message console put in. I ran into
			 * complications allowing the APRS Message Center from the
			 * SYSOP mode. I think having the Center from the TNOS console
			 * and the PBBS prompt is sufficient.
			 *
			 * 24May2003, Maiko, The APRS message center invoked at the TNOS
			 * console (not the PBBS) is now allocated a new session, so that
			 * the TNOS console is always free (the way it should be). Because
			 * of that, I'll now have to keep a variable to hold the current
			 * process (session) in use for the APRS message center invoked
			 * at the TNOS console.
			 */
			if (CurConsoleAPRSC)
				usprintf (CurConsoleAPRSC->output, tbuf);	/* 24May2003 */
			else
				usprintf (Command->output, tbuf);

			/* 30Jan2006, Maiko, Prepare to support all query types */
			if (*data == '?')
			{
				data++;

				if (!strnicmp (data, "PING?", 5) ||
					!strnicmp (data, "APRST", 5))
				{
					mymsgargv[2] = tracebuf;
				}
				else
					mymsgargv[2] = "Not supported (yet)";
#ifdef  APRSC
				dolocmsg (3, mymsgargv, vp);
#else
				if (rf)
					dolocmsg (3, mymsgargv, vp);
				else
					dointmsg (3, mymsgargv, vp);
#endif
				msgdb_save (callsign, srccall, tracebuf);
			}
		}
    }

#ifndef	APRSC

	/* 22Sept2002, Maiko, New feature - Gate POSIT and STAT info to RF */
	else if (dti_pos_or_stat (dti))
	{
		/* This is only done for stuff coming from the APRS IS to RF */
		if (!rf)
		{
			/*
			 * Check the source call against the appropriate callsign
			 * list and ONLY gate the stat or posit information to RF
			 * if the callsign or callsign pattern is in the list.
			 */
			if (dti == '>' && callsign_can_stat (srccall))
				retval = 1;

			else if (callsign_can_pos (srccall))
				retval = 1;
		}
	}
	/* 12Sep2004, Maiko, New feature - Gate WX info to RF */
	else if (dti == '_')
	{
		/* This is only done for stuff coming from the APRS IS to RF */
		if (!rf && callsign_can_wx (srccall))
			retval = 1;
	}
	/* 21Dec2008, Maiko, New feature - Gate Objects to RF */
	else if (dti == ';')
	{
		/* This is only done for stuff coming from the APRS IS to RF */
		if (!rf && callsign_can_obj (srccall))
			retval = 1;
	}
#ifdef	GATE_MICE_RF
	/*
	 * 17Sep2005, Maiko, Request from Janusz, Gate MICE to RF ...
	 */
	else if (mice_dti (dti))
	{
		/* This is only done for stuff coming from the APRS IS to RF */
		if (!rf && callsign_can_mice (srccall))
			retval = 1;
	}
#endif

#endif
	return retval;
}

/*
 * The following function is called from config.c, used when parsing
 * the CONSOLE command line or the AUTOEXEC.NOS commands
 */

int doaprs (int argc, char *argv[], void *p)
{
	return subcmd (Acmds, argc, argv, p);
}

#ifndef	APRSC

/*
 * As of April 11 2000 Steve Dimse has released this following doHash code
 * to the open source aprs community. This allows me to logon to the APRSD
 * system down in Miami, or whoever else is running an APRSD server.
 */

#define kKey 0x73e2	/* This is the key for the data */

static short doHash (const char *theCall)
{
	short hash, i, len;
	char rootCall[10];	/* need to copy call to remove ssid from parse */
	char *ptr, *p1 = rootCall;
	
	while ((*theCall != '-') && (*theCall != 0)) *p1++ = toupper(*theCall++);
	*p1 = 0;
	
	hash = kKey;	/* Initialize with the key value */
	i = 0;
	len = strlen(rootCall);
	ptr = rootCall;
	while (i<len)		/* Loop through the string two bytes at a time */
	{
		hash ^= (*ptr++)<<8;	/* xor high byte with accumulated hash */
		hash ^= (*ptr++);		/* xor low byte with accumulated hash */
		i += 2;
	}

	return hash & 0x7fff; /* mask off high bit so number is always positive */
}

/* Connect to World Wide APRS network */

static int connect_aprs_net (ANETSRV *anetsrvp)
{
	struct sockaddr_in fsocket;
	int comms_socket;
	char logonstr[150], *lptr = logonstr;

	fsocket.sin_family = AF_INET;

	aprslog (-1, "Connecting Hostname [%s]", anetsrvp->server);

	if ((fsocket.sin_addr.s_addr = resolve (anetsrvp->server)) == 0)
	{
		aprslog (-1, "Not resolvable");
		return -1;
	}

	fsocket.sin_port = anetsrvp->port;

	comms_socket = j2socket (AF_INET, SOCK_STREAM, 0);

	if (j2connect (comms_socket, (char*)&fsocket, SOCKSIZE) != 0)
	{
		aprslog (-1, "Connect failed");
		close_s (comms_socket);
		return -1;
	}

	lptr += sprintf (lptr, "user %s pass %u vers %s",
		logon_callsign, doHash (logon_callsign), TASVER);

	/* 24Feb2005, Maiko, Now pass custom port options */
	if (logon_filter)
		lptr += sprintf (lptr, " filter %s", logon_filter);

	lptr += sprintf (lptr, "\n");	/* 27May2005, Should terminate this */

	if (aprs_debug)
		aprslog (-1, "Logon [%s]", logonstr);

	if (j2send (comms_socket, logonstr, strlen (logonstr), 0) < 1)
	{
		aprslog (-1, "unable to send logon string");
		close_s (comms_socket);
		return -1;
	}

	aprslog (-1, "we are now connected and logged in");

	return comms_socket;
}

/*
 * 01May2001, VE4KLM, Convert a message from the internet to
 * the proper format for delivery over the local RF network
 */

static int convert_3rd_party (unsigned char *buffer)
{
    unsigned char orgmsg[300], *orgptr = orgmsg;

	/*
	 * 02Sep2010, Maiko, Pain in the neck, compiler warnings, need to cast,
	 * should be fine since we're only copying stuff, not interpreting it,
	 * you may see this throughout the code, I'd rather not have to do it.
	 */
    strcpy ((char*)orgmsg, (char*)buffer);

    *buffer++ = '}';    /* 3rd Party Data Type Identifier */

	/* 25Oct2002, Maiko, See comments further down, USE_OLD_WAY */

#ifdef	USE_OLD_WAY
    /*
     * copy from incoming source path header until the '*' or if
     * there is no '*', then do the entire source path header, up
     * to the ':' delimiter. Do not include the '*' or ':' chars.
     */
    while (*orgptr && *orgptr != '*' && *orgptr != ':')
        *buffer++ = *orgptr++;

    if (!(*orgptr)) /* just to be safe */
    {
        aprslog (-1, "bad packet, discard");
        return -1;
    }

    /*
     * If there is a '*' at this point we need to skip over it and
     * any other digi callsigns, right up to the ':' character. If
     * there is a ':' at this point, then just skip over it.
     */
    if (*orgptr++ == '*')
    {
        while (*orgptr && *orgptr != ':')   /* skip any digis after '*' */
            orgptr++;

        if (!(*orgptr)) /* just to be safe */
        {
            aprslog (-1, "bad packet, discard");
            return -1;
        }
        orgptr++;   /* skip the ':' delimiter */
    }
#else
	/*
	 * 25Oct2002, Maiko, Just include the FROMCALL and TOCALL for now, as
	 * per the latest 'standard' of choice. No point sending ALL other path
	 * information to an RF client at this point. There has been talk by some
	 * people, of the APRS-IS supporting so-called KISS routing (I forget the
	 * exact terminology used), which could let APRS rf clients make use of
	 * the complete path information for routing purposes, but that's not
	 * currently supported by the APRS-IS at this point in time.
	 */

	/*
	 * This should cover the FROM>TOCALL just fine, copy until you hit the
	 * first ',' character.
	 * 04Nov2002, oops, should also check for ':', cause there might not be
	 * any ',' characters if you think about it (ie, No digi or other calls
	 * following the TOCALL). Stumbled across this *bug* when I had a QSO
	 * with Bob (N1UAN) the other day, couldn't figure out why his messages
	 * to me were showing as 'bad packet, discard'. This is now fixed !
	 */
    while (*orgptr && *orgptr != ',' && *orgptr != ':')
        *buffer++ = *orgptr++;

    if (!(*orgptr)) /* just to be safe */
    {
        aprslog (-1, "bad packet, discard");
        return -1;
    }

	/* now skip over ALL other path information which we don't need */
    while (*orgptr && *orgptr != ':')
        orgptr++;

    if (!(*orgptr)) /* just to be safe */
    {
        aprslog (-1, "bad packet, discard");
        return -1;
    }
    orgptr++;   /* skip the ':' delimiter */

#endif

    /* the 'orgptr' now points at the APRS message itself ! */

    /*
     * Now insert 3rd party network identifier and gateway callsign,
     * don't forget to prepend the comma, and put '*' at end of 3rd
     * party header, oh yes, and the final ':' character.
     */
    buffer += sprintf ((char*)buffer, ",TCPIP,%s*:", logon_callsign);

    /* Finally put in the original APRS message, and we're done */
    strcat ((char*)buffer, (char*)orgptr);

	return 0;
}

#endif

/* 28Nov2001, Maiko, New function to update outgoing stats. Needed
 * because of references in the new aprsbc.o module
 */

void update_outstats (int length)
{
	/* 22Jan2002, Maiko, Carefull, next server may not be available */
	if (nxtsrv == (ANETSRV*)0)
		return;

	/* 02Aug2001, Maiko, Update outgoing stats */
	nxtsrv->out_bytes += length;
	nxtsrv->out_packets++;
}

/* Read messages coming from ourselves (RF) */

static unsigned char UDPbuf[305];

static void aprs_net (int unused OPTIONAL, void *sp, void *p OPTIONAL)
{
	struct sockaddr_in from;
	struct mbuf *bp;
	int fromlen, foo;
	const char *cp;

	struct session *asp = (struct session*)sp;

#ifndef	JNOSAPRS
	server_disconnect_io ();
#endif

	while (1)
	{
		KLMWAIT (NULL);	/* give other processes a chance */

		fromlen = sizeof (from);

		if ((foo = recv_mbuf (Asocket, &bp, 0, (char *) &from, &fromlen)) == -1)
		{
			if ((cp = sockerr (Asocket)) != NULLCHAR)
				aprslog (-1, "recv (Asocket) - sockerr [%s]", cp);
			else
				aprslog (-1, "recv (Asocket) closed normally");

			break;	/* Server closing */
		}

		if (foo == 0)
		{
			if (aprs_debug)
				aprslog (-1, "recv_mbuf - zero length message");

			continue;
		}

		foo = len_p (bp);

		/* protection against buffer overflow perhaps (shud never happen) */
		if (foo > 300)
			foo = 300;

		pullup (&bp, (char*)UDPbuf, foo);

		if (aprs_debug)
			aprslog (Asocket, "Got RF, [%d] %s", foo, UDPbuf);
        /*
         * 28May2004, Maiko, Working on dedicated APRS Client code
         */
#ifdef  APRSC
        message_handler ((char*)UDPbuf, MSG_VIA_RF);
#else
		/* 10Jul2001, VE4KLM, Now handle messages from RF side */
		if (!message_handler ((char*)UDPbuf, MSG_VIA_RF))
		{
			KLMWAIT (NULL);	/* give other processes a chance */

			if (aprs_debug)
				aprslog (Asocket, "sending to internet");

			/* dont try to send if internet is offline */
			if (Hsocket == -1)
			{
				if (aprs_debug)
					aprslog (-1, "internet offline");

				continue;
			}

			/* send Packet off to the internet APRS server */
			if (j2send (Hsocket, (char*)UDPbuf, foo, 0) < 0)
			{
				aprslog (-1, "send internet failed");
				continue;
			}

			/* 22Jan2002, Maiko, Carefull, next server may not be available */
			if (nxtsrv != (ANETSRV*)0)
			{
				/* 02Aug2001, Maiko, Update outgoing stats */
				nxtsrv->out_bytes += foo;
				nxtsrv->out_packets++;
			}
		}
#endif
	}

	asp->proc1 = NULLPROC;

	close_s (Asocket);

	Asocket = -1;
}

#ifndef	APRSC

/*
 * 20Jun2002, Maiko, New way to handle inactive clients
 * 22Jun2002, Use last_packets_in instead to MINIMIZE
 * the timer restarts that MBXTDISC style uses. With all
 * the data coming in from the APRS IS, the timer restarts
 * would be going off the scale if I was to use the standard
 * MBXTDISC method of inactivity. This way is ALOT more
 * resource friendly in my opinion.
 */ 
static void inactive_APRSsrv (void *t OPTIONAL)
{
	if (aprs_debug)
		aprslog (Hsocket, "checking for inactivity");

	if ((nxtsrv->last_packets_in == -1) ||
		(nxtsrv->in_packets == nxtsrv->last_packets_in))
	{
		aprslog (Hsocket, "disconnect, inactive server");

		if (Hsocket != -1)
		{
			close_s (Hsocket);
			Hsocket = -1;
		}

		nxtsrv->last_packets_in = -1;	/* make sure we reset this */

		/* no point restarting timer, since connection will be toast */
	}
	else
	{
		nxtsrv->last_packets_in = nxtsrv->in_packets;

		/* make sure we restart the timer */
#ifdef  TNOS241
		start_timer (__FILE__, __LINE__, &nxtsrv->inactive);
#else
		start_timer (&nxtsrv->inactive);
#endif
	}
}

#endif

/* Process to receive all APRS related messages */
static void aprsx (int unused OPTIONAL, void *u OPTIONAL, void *p OPTIONAL)
{
	unsigned char buffer[300];
	struct sockaddr_in sock;
	struct session sp;
	time_t curtime;
	const char *cp;
	int len;

#ifndef	JNOSAPRS
	server_disconnect_io ();
#endif
	aprslog (-1, "%s startup", TASVER);

	/* You must define a logon callsign and a default port */

	if (logon_callsign == (char*)0)
	{
		aprslog (-1, "abort - logon callsign %s !", notdef);
		return;
	}

	if (aprs_port == (char*)0)
	{
		aprslog (-1, "abort - interface %s !", notdef);
		return;
	}

	/* setup the UDP listener to listen to 'myself' for APRS ax25 frames */

	Asocket = j2socket (AF_INET, SOCK_DGRAM, 0);
	sock.sin_family = AF_INET;
	sock.sin_addr.s_addr = INADDR_ANY;
	sock.sin_port = 93;

	if (j2bind (Asocket, (char *) &sock, sizeof (sock)) == -1)
	{
		aprslog (-1, "APRSD: Can't j2bind");
		Asocket = -1;
		return;
	}

	/* Fork off process to listen to aprs stuff from 'ourselves' - rf */
	sp.proc1 = newproc ("aprs_net", 2048, aprs_net, 0, &sp, NULL, 0);

	KLMWAIT (NULL);	/* give other processes a chance */

	/* Start status and position broadcasts to RF side */
	startrfbc ();

#ifdef  APRSC

    while (1)
    {
        if (Asocket == -1)
        {
            aprslog (-1, "UDP listener is dead, kick out");
            break;
        }

        KLMWAIT (NULL); /* give other processes a chance */

#ifdef  JNOSAPRS
        j2pause (30000);
#else
#ifdef  TNOS241
        (void) kpause (__FILE__, __LINE__, 30000L); /* 30 seconds */
#else
        (void) kpause (30000L);
#endif
#endif
    }

#else

	/* Infinite loop, always try to stay connected to an Inet server */

	while (1)
	{
		/*
		 * 01Oct2002, VE4KLM (Maiko), If the UDP listener is dead,
		 * then someone likely did an 'aprs listen off', or just by
		 * chance it died for some other strange reason. In any event,
		 * there is no point trying a host connection. Just leave !
		 */
		if (Asocket == -1)
		{
			aprslog (-1, "UDP listener is dead, kick out");
			break;
		}

		KLMWAIT (NULL);	/* give other processes a chance */

		/* get the next server in this list */
		if ((nxtsrv = next_server (0)) == (ANETSRV*)0)
		{
#ifdef	JNOSAPRS
			j2pause (30000);
#else
#ifdef	TNOS241
			(void) kpause (__FILE__, __LINE__, 30000L);	/* 30 seconds */
#else
			(void) kpause (30000L);
#endif
#endif
			continue;
		}

		/* try to connect to it */
		if ((Hsocket = connect_aprs_net (nxtsrv)) == -1)
		{
#ifdef	JNOSAPRS
			j2pause (30000);
#else
#ifdef	TNOS241
			(void) kpause (__FILE__, __LINE__, 30000L);	/* 30 seconds */
#else
			(void) kpause (30000L);
#endif
#endif
			continue;
		}

		/*
		 * 01Nov2002, VE4KLM (Maiko), If the UDP listener is dead,
		 * then someone likely did an 'aprs listen off', or just by
		 * chance it died for some other strange reason. In any event,
		 * there is no point continuing, kill the host connection,
		 * and leave the loop !
		 */
		if (Asocket == -1)
		{
			close_s (Hsocket);

			Hsocket = -1;

			aprslog (-1, "UDP listener is dead, kick out");

			break;
		}

		KLMWAIT (NULL);	/* give other processes a chance */

		/* at this point we have a connection, start tracking connect time */
		nxtsrv->last = time (&curtime);
		nxtsrv->since = nxtsrv->last;	/* 07Feb2002, Maiko, track last heard */

		nxtsrv->flags = IGATE_ONLINE;	/* indicate that it is online (stats) */

		nxtsrv->in_bytes = 0;	/* 28Jan2002, Maiko, Reset - new connection */
		nxtsrv->in_packets = 0;
		nxtsrv->out_bytes = 0;
		nxtsrv->out_packets = 0;

		nxtsrv->last_packets_in = -1;	/* 22Jun2002, Maiko, inactivity */

		KLMWAIT (NULL);	/* give other processes a chance */

		/* 23Jan2002, Maiko, Start stat and posit broadcasts to INET side */
		startinetbc ();

		/* 01Jul2004, Maiko, Start WX broadcasts to INET side */
		startinetwx ();

		/*
		 * 20Jun2002, Maiko, Now using MBXTDISC style code to check
		 * for APRS server inactivity (inactivity value set uptop,
		 * and is currently set to check every 10 minutes).
		 */
		set_timer (&nxtsrv->inactive, aprs_is_timeout * 1000);
		nxtsrv->inactive.func = (void (*)(void *)) inactive_APRSsrv;
		nxtsrv->inactive.arg = NULL;
#ifdef  TNOS241
		start_timer (__FILE__, __LINE__, &nxtsrv->inactive);
#else
		start_timer (&nxtsrv->inactive);
#endif
		/* Now loop till socket get's busted, processing Inet traffic */
		while (1)
		{
			KLMWAIT (NULL);	/* give other processes a chance */

			if ((len = recvline (Hsocket, (char*)buffer, sizeof(buffer) - 2)) == -1)
			{
				if ((cp = sockerr (Hsocket)) != NULLCHAR)
					aprslog (-1, "recv (Hsocket) - sockerr [%s]", cp);
				else
					aprslog (-1, "recv (Hsocket) closed normally");

				break;	/* Inet Server connection lost or closing */
			}

			if (len == 0)
			{
				if (aprs_debug)
					aprslog (-1, "recvline - zero length message");

				continue;
			}

			/* 07Feb2002, Maiko, Now tracking last heard time */
			nxtsrv->since = time (&curtime);

			/* 02Aug2001, Maiko, Update incoming stats */
			nxtsrv->in_bytes += len;	
			nxtsrv->in_packets++;

			/* 24Jun2002, Maiko, Make sure we set this !!! */
			if (nxtsrv->last_packets_in == -1)
				nxtsrv->last_packets_in = nxtsrv->in_packets;

			/* 19Jul2001, VE4KLM, Get rid of the newline (if there) */
			if (buffer[len-1] == '\n')
				len--;

			buffer[len] = 0;

			/* 16Apr2001, VE4KLM, experimental message handler */
			/* 10Jul2001, VE4KLM, Added flag for RF or IGATE */
			if (message_handler ((char*)buffer, MSG_VIA_IGATE))
			{
				KLMWAIT (NULL);	/* give other processes a chance */

            	/* 01May2001, VE4KLM, convert message for bc to rf */
            	if (convert_3rd_party (buffer) != -1)
				{
					len = strlen ((char*)buffer); /* MUST recalc buffer len */

					/* 19Jul2001, VE4KLM, Don't go sending the NULL out RF */
					if (sendrf ((char*)buffer, len) == -1)
						aprslog (-1, "sendrf failed");
				}
			}

			if (monitor_aprsnet)
				aprslog (Hsocket, "APRSN: [%.*s]", len, buffer);
		}

		stop_timer (&nxtsrv->inactive);	/* 20Jun2002, Maiko, Stop inactivity */

		nxtsrv->last = time (&curtime) - nxtsrv->last;	/* end connected time */

		nxtsrv->flags = 0;

		if (Hsocket != -1)	/* 20Jun2002, Maiko, Added this, important */
		{
			close_s (Hsocket);	/* close the socket and free resources */

			Hsocket = -1;	/* 28Jan2002, Maiko, important */
		}

		KLMWAIT (NULL);	/* give other processes a chance */

		/*
		 * 23Jan2002, Maiko, Stop status and position broadcasts
		 * to the Internet side
		 */
		stopinetbc ();

		/* 01Jul2004, Maiko, Stop WX broadcasts to Internet side */
		stopinetwx ();
	}

#endif	/* end of APRSC */

	/*
	 * Stop status and position broadcasts to RF side, will
	 * never get here right now
	 */
	stoprfbc ();
}

#if defined (APRS_44825) && !defined(APRSC)

/*
 * 28Dec2001, Maiko, New monitor only port for low bandwidth links or
 * for those stations that simply want to monitor traffic, not handle
 * it, ie, PCSAT listeners.
 * 08Jan2002, Maiko, Added send function (currently only for keep alive
 * status messages since some clients kill connection if they don't
 * receive anything after some time.
 */

static void send44825 (void *ptr)
{
	char obuf[100];
	int len;

	ANETCLI	*cptr = (ANETCLI*)ptr;

	ANETSRV *aptr = &cptr->srv;

	sprintf (obuf, "%s>%s:>keepalive\n", getlogoncall (), getaprsdestcall ());

	len = strlen (obuf); /* + 1;*/	/* include string terminator */

	/* 07Feb2002, Maiko, Update client OUT counters */

	if (j2send (cptr->s, obuf, len, 0) < 0)
	{
		aprslog (-1, "unable to send keepalive");
	}
	else
	{
		aptr->out_bytes += len;
		aptr->out_packets++;
	}

	/* 17Jun2002, Maiko, Should be using attached timers */
#ifdef  TNOS241
    start_timer (__FILE__, __LINE__, &cptr->t);
#else
    start_timer (&cptr->t);
#endif
}

/*
 * 20Jun2002, Maiko, New way to handle inactive clients
 * 23Jun2002, Maiko, Make this just like the APRS IS timer
 */ 
static void inactive_44825 (void *ptr)
{
	ANETCLI	*cptr = (ANETCLI*)ptr;

	ANETSRV *aptr = &cptr->srv;

	if (aprs_debug)
	{
		aprslog (cptr->s, "checking for inactivity");
		aprslog (-1, "last in %ld, in %ld",
			aptr->last_packets_in, aptr->in_packets);
	}

	if ((aptr->last_packets_in == -1) ||
		(aptr->in_packets == aptr->last_packets_in))
	{
		aprslog (cptr->s, "disconnect, inactive client");

		close_s (cptr->s);

		aptr->last_packets_in = -1;	/* make sure we reset this */

		/* no point restarting timer, since connection will be toast */
	}
	else
	{
		aptr->last_packets_in = aptr->in_packets;

		/* make sure we restart the timer */
#ifdef  TNOS241
		start_timer (__FILE__, __LINE__, &aptr->inactive);
#else
		start_timer (&aptr->inactive);
#endif
	}
}

/*
 * 17May2004, Maiko, No longer static to this module, called from the
 * new aprscfg.c module.
 */
void serv44825 (int s, void *unused OPTIONAL, void *p OPTIONAL)
{
	struct sockaddr_in fsocket;
	char outdata[300], *optr;
	char tempuser[20], *tptr;
	time_t curtime;
	ANETSRV *aptr;
	ANETCLI *cptr;
	int len = sizeof(fsocket), cnt;

	int user_verified = 0;

#ifndef	JNOSAPRS
	server_disconnect_io ();
#endif
	seteol (s, "\r");

	aprslog (s, "connected");

	/* need to get some information of the client connecting */
	if (j2getpeername (s, (char *) &fsocket, &len) == -1)
	{
		aprslog (s, "disconnect, unable to get client information");
		close_s (s);
		return;
	}

	/* 16Jun2002, Maiko, Is this a configured client ? */
	for (cnt = 0; cnt < MAXANETCLI; cnt++)
	{
		if (anetcli[cnt].srv.server)
		{
			if (!strcmp (anetcli[cnt].srv.server,
				inet_ntoa (fsocket.sin_addr.s_addr)))
			{
				break;
			}
		}
	}

	if (cnt == MAXANETCLI)
	{
		aprslog (s, "disconnect, not a configured client");
		close_s (s);
		return;
	}

	cptr = &anetcli[cnt];	/* 07Jan */

	aptr = &cptr->srv;

	/* 23Jun2002, Maiko, Should check if connected already !!! */
	if (aptr->flags == IGATE_ONLINE)
	{
		/*
		 * hmmm ... connect request while we still seem connected,
		 * have to think that the session got invalidated somehow,
		 * meaning we should close the *existing* connection, and
		 * let this one take it's place !!! Closing the *existing*
		 * socket should do it (ie, terminate *existing* process,
		 * stopping it's timers, etc, etc. This needs work !!!
		 */
		aprslog (s, "disconnect, old session being terminated");
		close_s (cptr->s);	/* socket of old session, NOT this one !!! */
		close_s (s);	/* close this one too (for now) just to be safe */
		return;
	}

	aptr->last = time (&curtime);
	aptr->since = aptr->last;	/* 07Feb2002, Maiko, Track last heard */
	aptr->in_bytes = 0;
	aptr->in_packets = 0;
	aptr->out_bytes = 0;
	aptr->out_packets = 0;

	aptr->last_packets_in = -1;	/* 23Jun2002, Maiko, Scrapped, then put back */

	aptr->flags = IGATE_ONLINE;	/* indicate that it is online (stats) */

	cptr->s = s;	/* important for timer function */
	cptr->t.state = 0;	/* probably a good idea */

	/* 20Jun2002, Maiko, New way to handle inactive connections */
	set_timer (&aptr->inactive, 1800000);	/* 30 minutes for now */
	aptr->inactive.func = (void (*)(void *)) inactive_44825;
	aptr->inactive.arg = (void*)cptr;
#ifdef  TNOS241
	start_timer (__FILE__, __LINE__, &aptr->inactive);
#else
	start_timer (&aptr->inactive);
#endif

	while (1)
	{
		KLMWAIT (NULL);	/* give other processes a chance */

		if ((len = recvline (s,outdata, sizeof(outdata) - 2)) == -1)
			break;

		if (aprs_debug)
		{
			aprslog (s, "MON B1 (%d) B2 (%d) L1 (%d) L2 (%d)",
				*outdata, *(outdata+1), *(outdata+len-1), *(outdata+len-2));

			aprslog (s, "len %d [%s]", len, outdata);
		}

		/* Why not just write it out the Hsocket ? Well, I should
		 * really validate the incoming data before I simply go shooting
		 * it out to the APRS internet system. Using aprs_send () will
		 * forward this packet to the main APRS server which contains
		 * the data validation routines, etc.
		 */

		if (len > 1)
		{
			/* 07Feb2002, Maiko, Track since last heard */
			aptr->since = time (&curtime);

			/* update incoming stats */
			aptr->in_bytes += len;
			aptr->in_packets++;

			/* 24Jun2002, Maiko, Make sure we do this !!! */
			if (aptr->last_packets_in == -1)
				aptr->last_packets_in = aptr->in_packets;
			
			/* 07Jan2002, Maiko, User info and authentication */
			if (!user_verified)
			{
				optr = outdata;

				/* seems we get leading CR from UI-View logon string */
				if (*optr == '\r' || *optr == '\n')
					optr++;

				if (!memcmp (optr, "user", 4) || !memcmp (optr, "USER", 4))
				{
					optr = skipwhite (optr + 4);

					tptr = tempuser;

					while (*optr && *optr != ' ')
						*tptr++ = *optr++;

					*tptr = 0;	/* terminate string */

					aprslog (s, "User [%s] logging in", tempuser);

					/*
					 * 01Oct2002, Maiko, Oops - let's not waste memory. Do
					 * it here instead of when client disconnects - as was
					 * my original thought - that way the aprsstat and 14501
					 * information has a 'history' of sorts, telling us the
					 * callsign that was last connected for this client.
					 */
					if (cptr->user)
					{
						/* debugging */
						aprslog (s, "freeing user [%s] memory", cptr->user);

						free (cptr->user);
					}

					cptr->user = j2strdup (tempuser);

					/* find out which software version they are using */
					tptr = strstr (optr, "vers");

					if (*tptr == 0)
						tptr = strstr (optr, "VERS");

					if (*tptr)
						tptr = skipwhite (tptr + 4);

					if (*tptr == 0)
					{
						aprslog (s, "software not specified");
						break;
					}

					aprslog (s, "Software [%s]", tptr);

					/* User is verified and connected for sure only after
					 * we confirm that some type of software is specified.
					 */
					user_verified = 1;

					/*
					 * 15Nov2002, Maiko, I no longer hardcode for which
					 * software keepalive broadcasts are done for. Now, each
					 * client is configured with it's own keepalive value.
					 * 29Mar2003, Maiko, New 'talive' variable to hold this,
					 * instead of trying to 'recycle' the unused port value
					 * which actually I should leave alone since it is used
					 * in both the html and finger info pages.
					 */
					if (aptr->talive == 0)
					{
						aprslog (s, "client does NOT want keepalive msgs");
						continue;
					}

					aprslog (s, "keepalive set for %d seconds", aptr->port);

					KLMWAIT (NULL);	/* give other processes a chance */

					/* 08Jan2002, Maiko, Keep alive timer for connection */
					cptr->t.func = (void (*)(void*)) send44825;
					cptr->t.arg = (void*)cptr;

					/*
					 * 15Nov2002, Maiko, interval is now configurable using
					 * the 'port' paramter of 'aprs client ...'. No longer
					 * hardcoded anymore. If 'port' = 0, timer disabled.
					 * 29Mar2003, Maiko, using new 'talive' var instead of
					 * the port variable since it's needed for stats.
					 */
					set_timer (&cptr->t, aptr->talive * 1000);

					/* send a keep alive right now to get timer going */
					send44825 ((void*)cptr);

					continue;
				}
				else
				{
					aprslog (s, "No user authentication");

					break;
				}
			}

			len++;	/* include NULL terminator */

			/* for piggy backed messages, that have a NULL terminator
			 * on the first message (only noticed with my software so
			 * far) need to look at this sending of the NULL somemore
			 */
			if (*outdata == 0 || *outdata == '\n' || *outdata == '\r')
				aprs_send (outdata + 1, len - 1);
			else
				aprs_send (outdata, len);
		}
	}

	stop_timer (&aptr->inactive);	/* 20Jun2002, Maiko, stop inactivity */

	stop_timer (&cptr->t);	/* 08Jan2002 */

	/* 16Jun2002, Maiko, Added the online flags and time tracking */
	aptr->last = time (&curtime) - aptr->last;	/* end connected time */
	aptr->flags = 0;	/* indicate that it is now offline (stats) */

	aprslog (s, "disconnected");

	close_s (s);
}

#endif	/* end of APRS_44825 */

#if defined(APRS_14501) && !defined(APRSC)

/*
 * 07Jan2002, Maiko, Modified to include user info if Server Connection
 * banner, I have one function to handle the banner for Host and User
 * connections (saves code). Both are almost the same (were, until I
 * added more info for the Client connections).
 */
static char *dsp14501hdr (const char contype, char *ptr)
{
	/* show igate connection status list */
	ptr += sprintf (ptr, "<br><table border=1 cellpadding=3 bgcolor=\"#00ffdd\"><tr>");

	if (contype == 'C')
		ptr += sprintf (ptr, "<td>User</td>");

	/* 07Feb2002, Maiko, Added 'Since last heard' header column */
	ptr += sprintf (ptr, "<td align=\"center\">");

	if (contype == 'C')
		ptr += sprintf (ptr, "Client");
	else
		ptr += sprintf (ptr, "Server");

	ptr += sprintf (ptr, "</td><td>Port</td><td>Connected</td><td>Pkts In</td><td>Bytes</td><td>Pkts Out</td><td>Bytes</td><td align=\"center\">Since</td></tr>");

	return ptr;
}

/*
 * 31Jul2001, VE4KLM, New 14501 Information Server, so finally we can go
 * direct to aprsstat from any of the APRSD 14501 info pages on the web.
 * 17May2004, Maiko, No longer static to this module, called from the
 * new aprscfg.c module.
 */

/* Jun2005, Maiko, New HTML form functions (ax25 heard, users, past) */
extern int http_obj_jh_form (char*, char*), http_obj_usr_form (char*);
extern int http_obj_sizeof_forms (void);

void serv14501 (int s, void *unused OPTIONAL, void *p OPTIONAL)
{

#ifndef	ALLOC_BODY
	static char body[2000];	/* this 14501 sucker can take alot of memory :-( */
	char *ptr = body;
#else
	char *body, *ptr;
#endif

	char *tptr, tindstr[25];
	time_t curtime, nowtime;
	int needed, len, cnt;
	int server, maxnodes;
	ANETSRV *nodeptr;
	char tmp[AXBUF];

	int hdrflg = 0; /* only display IGATE Con header if servers are present */

	char *hptr, hiface[20];	/* 14Mar2006, Maiko, New common code */

#ifndef	JNOSAPRS
	server_disconnect_io ();
#endif

	while (1)
	{
		if (!vhttpget (s, &hptr))
			break;

		*hiface = 0;	/* important */

		if (hptr)
			strcpy (hiface, hptr);

#ifdef	ALLOC_BODY

		/* 07Jun2003, Maiko, Revamped the way I calculate my memory needs */
		/* 20Jun2005, Maiko, Revamped again for the newer 2.0c5a look */

		needed = 400;	/* start with basic form, no filled in data */

		if (aprs_contact)
			needed += (2 * strlen (aprs_contact));

		if (locator)
			needed += (strlen (locator) + strlen (lwrlogoncall));

		/* 20Jun2005, Maiko, New to know memory needed for new forms */
		needed += http_obj_sizeof_forms ();

		/* Server Section */
		if (get_srvcnt())
			needed += (250 + get_srvcnt() * 230);

		/* Client Section */
		if (numcli)
			needed += (250 + numcli * 230);

		/* Heard Section */
		if (get_topcnt ())
		{
			needed += 200;

			if (locator)
				needed += (get_topcnt () * (77 + 105 + strlen (locator)));
			else
				needed += (get_topcnt () * (70 + 90));
		}

		aprslog (-1, "Body needed %d bytes", needed);

		body = malloc (needed);

/* this is better approach. I don't use the
	mallocw - there is absolutely no reason
	why TNOS should shutdown simply cause
	there's no memory to do a 14501 page. */

		if (body == (char*)0)
		{
			aprslog (s, "No memory available");
			break;
		}

		ptr = body;
#endif
		KLMWAIT (NULL);	/* give other processes a chance */

		/* put general information first */

		ptr += sprintf (ptr, "<html><center><br><b>Status of %s's system - contact : </b>", pax25 (tmp, Mycall));

		if (aprs_contact)
		{
			switch (aprs_contact_type)
			{
				case 't':
				case 'T':
					ptr += sprintf (ptr, "%s", aprs_contact);
					break;
				case 'm':
				case 'M':
					ptr += sprintf (ptr, "<a href=\"mailto:%s\">%s</a>",
						aprs_contact, aprs_contact);
					break;
				case 'h':
				case 'H':
					ptr += sprintf (ptr, "<a href=\"%s\">%s</a>",
						aprs_contact, aprs_contact);
					break;
			}
		}
		else ptr += sprintf (ptr, "<i>not available</i>");

		ptr += sprintf (ptr, "<br><font size=2>powered by VE4KLM-JNOS %s - KA9Q NOS derivative</font><br><br>", Version);

	ptr += http_obj_jh_form (ptr, hiface);

	ptr += sprintf (ptr, "<br>");

	ptr += http_obj_usr_form (ptr);

	ptr += sprintf (ptr, "<br><table border=1 cellpadding=5 bgcolor=\"#00ffdd\"><tr><td>VE4KLM-%s</td><td>Callsign : ", TASVER);

	if (locator)
	{
		ptr += sprintf (ptr, "<a href=\"%s%s\">%s</a>",
			locator, lwrlogoncall, lwrlogoncall);
	}
	else
		ptr += sprintf (ptr, "%s", lwrlogoncall);

	ptr += sprintf (ptr, "</td><td>Default Port : %s</td></tr></table>",
			aprs_port);

	/*
	 * 16Jun2002, The 44825 client information used to be a separate
	 * for (;;) loop, which has been removed. I now use the original
	 * code for the server info, for the client info as well, since
	 * creating this NODEcfg () system of functions. This cuts down
	 * on the amount of code duplication.
	 */
	server = 1;

	while (server >= 0)
	{
		if (server)
			maxnodes = MAXANETSRV;
		else
			maxnodes = MAXANETCLI;

		for (hdrflg = cnt = 0; cnt < maxnodes; cnt++)
		{
			if (server)
				nodeptr = &anetsrv[cnt];
			else
				nodeptr = &anetcli[cnt].srv;

			if (nodeptr->port)
			{
				nowtime = time (&curtime);

				if (nodeptr->flags == IGATE_ONLINE)
				{
					/* 23Aug2001, Maiko, Now shade the row, instead of using
					 * an asterick symbol like was done for the text version
					 */
					tptr = tformat ((int32)(nowtime - nodeptr->last));

					strcpy (tindstr, "<tr bgcolor=\"#ffff00\">");
				}
				else
				{
					/* 08Feb2002, If never connected before, then indicate it */
					if (nodeptr->last)
						tptr = tformat ((int32)nodeptr->last);
					else
						tptr = (char*)"-";

					strcpy (tindstr, "<tr>");
				}

				/* 29Aug2001, Maiko, Make this like the text based on, if no
				 * servers are configured, then don't bother putting up this
				 * header. Instead, indicate later that no servers present.
				 */
				if (!hdrflg)
				{
					if (server)
						ptr = dsp14501hdr ('S', ptr);
					else
						ptr = dsp14501hdr ('C', ptr);

					hdrflg = 1;
				}

				ptr += sprintf (ptr, "%s", tindstr);

				/* 16Jun2002, Maiko, New client info code now here */
				if (!server)
				{
					if (anetcli[cnt].user)
						ptr += sprintf (ptr, "<td>%s</td>", anetcli[cnt].user);
					else
						ptr += sprintf (ptr, "<td>-</td>");
				}
				/*
				 * 12Apr2002, Maiko, if info port is defined as 0, then
				 * remove html link
				 */ 
				if (nodeptr->info_port)
				{
					/*
					 * 11Dec2001, Maiko, Added configurable link for host
					 * information
					 */ 
					ptr += sprintf (ptr,
						"<td><a href=\"http://%s:%d\">%s</a></td>",
							nodeptr->server, nodeptr->info_port,
								nodeptr->server);
				}
				else
					ptr += sprintf (ptr, "<td>%s</td>", nodeptr->server);

				ptr += sprintf (ptr, "<td>%d</td><td>%s</td><td>%ld</td><td>%ld</td><td>%ld</td><td>%ld</td>", nodeptr->port, tptr, nodeptr->in_packets, nodeptr->in_bytes, nodeptr->out_packets, nodeptr->out_bytes);

				/* 08Feb2002, If never connected before, then indicate it */
				if (nodeptr->since)
						tptr = tformat ((int32)(nowtime - nodeptr->since));
				else
						tptr = (char*)"-";

				/* 07Feb2002, Maiko, Now show 'since last heard' info */
				ptr += sprintf (ptr, "<td>%s</td></tr>", tptr);
			}
		}

		KLMWAIT (NULL);	/* give other processes a chance */

		if (hdrflg)
			ptr += sprintf (ptr, "</table>");

		server--;
	}
		/* put in the users heard information */
		ptr += build_14501_aprshrd (ptr);

		/* end the page (20Nov2001, Maiko, Added Version of TNOS itself) */
		/* 11Dec2001, Maiko, Moved version to the top instead */
		ptr += sprintf (ptr, "</center></html>\r\n");

		KLMWAIT (NULL);	/* give other processes a chance */

		len = ptr - body;

		aprslog (-1, "Body length %d", len);

		/* write the HEADER record */
		/* 03Dec2001, Maiko, Change to 1.1 from 1.0 */
		/* 04Dec2001, Maiko, Change back to 1.0, take out close cmd */
		tprintf ("HTTP/1.0 200 OK\r\n");

		/* 03Dec2001, Maiko, Force client to close session when done
		tprintf ("Connection: close\r\n");
		*/
		tprintf ("Content-Length: %d\r\n", len);
		tprintf ("Content-Type: text/html\r\n");
		tprintf ("Server: %s\r\n", TASVER);

		/* use a BLANK line to separate the BODY from the HEADER */
		tprintf ("\r\n");

		/* 01Aug2001, VE4KLM, Arrggg !!! - the tprintf call uses vsprintf
		 * which has a maximum buf size of SOBUF, which itself can vary
		 * depending on whether CONVERSE or POPSERVERS are defined. If
		 * we exceed SOBUF, TNOS is forcefully restarted !!! Sooooo...,
		 * we will have to tprintf the body in chunks of SOBUF or less.
		 *
		tprintf ("%s", body);
		 */

		ptr = body;

		while (len > 0)
		{
			KLMWAIT (NULL);	/* give other processes a chance */

			if (len < MAXTPRINTF)
			{
				tprintf ("%s", ptr);
				len = 0;
			}
			else
			{
				tprintf ("%.*s", MAXTPRINTF, ptr);
				len -= MAXTPRINTF;
				ptr += MAXTPRINTF;
			}
		}

#ifdef	ALLOC_BODY
		/* aprslog (s, "free up body space"); */
		free (body);
#endif
		break;	/* FORCE termination of connection */
	}

	if (hptr)
		free (hptr);

	aprslog (s, "disconnected");

	close_s (s);
}


#endif	/* end of APRS_14501 */

/* Start or Stop  the APRS daemon */

static int doaprserver (int argc, char *argv[], void *p OPTIONAL)
{
	if (argc == 2)
	{
		if (!stricmp (argv[1], "on"))
		{
			if (Asocket == -1)
			{
				/* Start domain server task */
				/* 11Apr06, Maiko, Increase stack by 1024 bytes */
				(void) newproc ("APRS Server", 2048, aprsx, 0, NULL, NULL, 0);
			}
		}
		else
		{
			if (Asocket != -1)
			{
				close_s (Asocket);
				Asocket = -1;
			}
#ifndef	APRSC
			if (Hsocket != -1)
			{
				close_s (Hsocket);
				Hsocket = -1;
			}
#endif
		}
	}
	else
	{
		tprintf ("use: aprs listen [on|off]\n"); 
		tprintf ("currently: O%s\n", (Asocket != -1) ? "n" : "ff");
	}

	return 0;
}

/* 31May2001, VE4KLM, Configure size of (and init) APRS heard table */

static int dohsize (int argc, char *argv[], void *p OPTIONAL)
{
	static int hsize = 0;

	/* 18Jun2003, Maiko, Make this a bit more user friendly */
	if (argc == 2)
	{
		if ((hsize = atoi (argv[1])) < 1)
			hsize = 0;

		init_aprshrd (hsize);
	}
	else
	{
		tprintf ("use: aprs hsize [max entries in aprs heard table]\n");
		tprintf ("currently: %d\n", hsize);
	}

	return 0;
}

/*
 * 09Jan2002, Maiko, New single function for general flags. The idea
 * is to do away with each function dedicated to a single flag. Might
 * as well have it all done by one single function. This replaces the
 * dodebug, domonitor, and dopoe functions. New badpackets flag.
 *
 * 02Apr2002, Maiko, Added dtiheard option so that users can switch off
 * the tracking of DTI character in APRS heard table if they want.
 *
 */

static int doflags (int argc, char *argv[], void *p OPTIONAL)
{
	char *ptr;
	int state;

	if (argc == 1)
	{
		tprintf ("%cdebug %cbadpackets %cmonitor %cpoe %cdtiheard %cbconlyifhrd %cdigi %cdebugdigi\n",
			aprs_debug ? '+' : '-', aprs_badpackets ? '+' : '-',
			monitor_aprsnet ? '+' : '-', aprs_poe ? '+' : '-',
			dtiheard ? '+' : '-', bconlyifhrd ? '+' : '-',
			aprsdigi ? '+' : '-', debugdigi ? '+' : '-');
	}
	else
	{
		while (argc > 1)
		{
			ptr = argv[argc-1];

			state = 0;

			if (*ptr++ == '+')
				state = 1;

			if (!stricmp (ptr, "debug"))
				aprs_debug = state;

			if (!stricmp (ptr, "badpackets"))
				aprs_badpackets = state;

			if (!stricmp (ptr, "monitor"))
				monitor_aprsnet = state;

			if (!stricmp (ptr, "poe"))
				aprs_poe = state;

			if (!stricmp (ptr, "dtiheard"))
				dtiheard = state;

			/* 06May2002, Maiko, New bc flag for messaging */
			if (!stricmp (ptr, "bconlyifhrd"))
				bconlyifhrd = state;

			/* 14Nov2002, Maiko, New enable/disable APRS digipeating */
			if (!stricmp (ptr, "digi"))
				aprsdigi = state;

			/* 23Mar2006, Maiko, New debug flag for aprs digi code */
			if (!stricmp (ptr, "debugdigi"))
				debugdigi = state;

			argc--;
		}
	}

	return 0;
}

#ifndef	APRSC

/* Return the next available APRSNET server */

static ANETSRV *next_server (int beginning)
{
	static int current_server = 0;

	int cnt;

	if (beginning)
		current_server = 0;

	for (cnt = current_server; cnt < MAXANETSRV; cnt++)
	{
		if (anetsrv[cnt].port)
		{
			current_server = cnt + 1;	/* point to the next one */
			return &anetsrv[cnt];
		}
	}

	if (cnt >= MAXANETSRV)
		current_server = 0;

	return (ANETSRV*)0;
}

#ifdef	APRS_14501

/*
 * 22Aug2001, Maiko, Get the number of servers in our list, so that
 * I can better allocate the amount of memory needed for the 14501
 * body statistics
 */
static int get_srvcnt (void)
{
	int cnt = 0, count = 0;

	while (cnt < MAXANETSRV)
	{
		if (anetsrv[cnt].port)
			count++;

		cnt++;
	}

	/* aprslog (-1, "%d servers in the list", count); */

	return count;
}

#endif

/* Add or Delete from list of APRSNET servers */
/* 16Jun2002, Maiko, Added client support (again), but this time
 * I'm going to configure the clients like servers - changed the
 * original server config code to reuse it for clients as well,
 * that will save alot of duplicate code and space. The dosrvcfg
 * and doclicfg functions are now stub functions to the original
 * code that has been renamed to the doNODEcfg function.
 */

static void pnodetype (int server)
{
	if (server)
		tprintf ("server");
	else
		tprintf ("client");
}

static void usageNODEcfg (int server)
{
	tprintf ("usage: aprs ");
	pnodetype (server);
	tprintf (" [add|delete|list|kick] *[parameters]\n");
}

static int doNODEcfg (int server, int argc, char *argv[], void *p OPTIONAL)
{
	int cnt, port, maxnodes;

	ANETSRV *nodeptr;

	if (server)
		maxnodes = MAXANETSRV;
	else
		maxnodes = MAXANETCLI;

	if (argc == 1)
		usageNODEcfg (server);

	else if (!stricmp (argv[1], "add"))
	{
		/* 24May2001, VE4KLM, Check number of args, or it could crash */
		/* 13Dec2001, Maiko, Possibility of 5 args now, make it less than */
		if (argc < 4)
		{
			usageNODEcfg (server);
			return 0;
		}

		for (cnt = 0; cnt < maxnodes; cnt++)
		{
			if (server)
				nodeptr = &anetsrv[cnt];
			else
				nodeptr = &anetcli[cnt].srv;

			if (!nodeptr->port)
			{
				/* note for client, port is not used */
				/* 15Nov2002, Maiko, port for client is now keepalive timer */
				/* 29Mar2003, Maiko, value of 0 means NO keep alive, and
				 * new 'talive' variable to hold keepalive interval.
				 */
				if (server)
				{
					nodeptr->port = atoi (argv[3]);
					nodeptr->talive = 0;	/* unused */
				}
				else
				{
					nodeptr->port = 14825;	/* client always used 14825 */
					nodeptr->talive = atoi (argv[3]);
				}

				/* length of server info no longer important (alloced) */
				nodeptr->server = j2strdup (argv[2]);
				/* 11Dec2001, Maiko, New info port for HTTP info link */
				nodeptr->info_port = 14501;

				/* 13Dec2001, Maiko, Info port can now be customized */
				if (argc == 5)
				{
					if ((port = atoi (argv[4])) > 0)
						nodeptr->info_port = port;
					else
						nodeptr->info_port = 0;
				}

				/* 02Aug2001, VE4KLM, Init statistics counters */
				nodeptr->in_bytes = 0;
				nodeptr->in_packets = 0;

				/* 16Jun2002, Maiko, Should probably init these too */
				nodeptr->out_bytes = 0;
				nodeptr->out_packets = 0;

				/* 07Jun2003, Maiko, oops, only do this for clients ! */
				if (!server)
					numcli++;

				break;
			}
		}

		if (cnt == maxnodes)
		{
			tprintf ("no more room for new entries.\n");
		}
	}
	else if (!stricmp (argv[1], "delete"))
	{
		/* 24May2001, VE4KLM, Check number of args, or it could crash */
		if (argc != 3)
		{
			usageNODEcfg (server);
			return 0;
		}

		for (cnt = 0; cnt < maxnodes; cnt++)
		{
			if (server)
				nodeptr = &anetsrv[cnt];
			else
				nodeptr = &anetcli[cnt].srv;

			if (strcmp (nodeptr->server, argv[2]) == 0)
			{
				nodeptr->port = 0;

				/* 07Jun2003, Maiko, oops, only do this for clients ! */
				if (!server)
					numcli--;

				break;
			}
		}

		if (cnt == maxnodes)
			tprintf ("could not find entry to delete.\n");
	}
	else if (!stricmp (argv[1], "list"))
	{
		tprintf ("\nCurrent ");
		pnodetype (server);
		tprintf (" list\n");

		for (cnt = 0; cnt < maxnodes; cnt++)
		{
			if (server)
				nodeptr = &anetsrv[cnt];
			else
				nodeptr = &anetcli[cnt].srv;

			if (nodeptr->port)
			{
				tprintf ("%-20.20s %5d\n", nodeptr->server, nodeptr->port);
			}
		}
	}
	/*
	 * 07Feb2002, Maiko, Added a kick option so we can force rotation to next
	 * server in the list. Wish I had put this in months ago, that way if the
	 * current APRS internet system server is acting up, we can bounce them,
	 * and keep TNOS running (no restart or reordering of server list).
	 * 16Jun2002, Maiko, Only kick socket for server connection !
	 * 29Mar2003, Maiko, Should be '&& server', not '&& !server', meaning
	 * this feature never worked unless user went 'aprs client kick'.
	 */
	else if (!stricmp (argv[1], "kick") && server)
	{
		if (Hsocket != -1)
		{
			close_s (Hsocket);
			Hsocket = -1;
		}
	}

	return 0;
}

static int doclicfg (int argc, char *argv[], void *p OPTIONAL)
{
	return (doNODEcfg (0, argc, argv, p));
}

static int dosrvcfg (int argc, char *argv[], void *p OPTIONAL)
{
	return (doNODEcfg (1, argc, argv, p));
}

#endif	/* end of APRSC */

static int dointernal (int argc, char *argv[], void *p OPTIONAL)
{
	if (argc == 2)
	{
		if ((internal_hostid = resolve (argv[1])) == 0)
		{
			tprintf ("internal hostid not resolvable\n"); 
			internal_hostid = 0;
		}
	}
	else
	{
		tprintf ("*** For NOS systems that have 2 commercial IP addresses ***\n");
		tprintf ("*** or other unusual IP configurations. Ask me for help ***\n");
		tprintf ("use: aprs internal [IP Address of NOS system]\n"); 
		tprintf ("currently: %s\n", inet_ntoa (internal_hostid));
	}

	return 0;
}

#ifndef	APRSC

static int doemailmode (int argc, char *argv[], void *p OPTIONAL)
{
	static const char *mmode[] = { "passthru", "local", "block" };

	int cnt = -1;

	/* 09Jun2003, On the fly now, show current value, more user friendly */

	if (argc == 2)
	{
#ifdef	DEVELOPMENT
		if (!stricmp ("testack", argv[1]))
		{
			aprsmail ("VE4XXX", "maiko@pcs.mb.ca Test ACK Msg 73 de Maiko{01",
				"VE4XXX>APRS,VE4RAG*", 19);
			return 0;
		}
		if (!stricmp ("testnoack", argv[1]))
		{
			aprsmail ("VE4XXX", "maiko@pcs.mb.ca Test NOACK Msg 73 de Maiko",
				"VE4XXX>APRS,VE4RAG*", 19);
			return 0;
		}
#endif
		for (cnt = 0; cnt < 3; cnt++)
		{
			if (stricmp (mmode[cnt], argv[1]) == 0)
				break;
		}
		if (cnt != 3)
			emailmode = cnt;
	}

	if (cnt == -1 || cnt == 3)
	{
		tprintf ("use: aprs email [%s|%s|%s]\n", mmode[0], mmode[1], mmode[2]);
		tprintf ("currently: %s\n", mmode[emailmode]);
	}

	return 0;
}

/* 02Jul2003, Maiko, New Mic-E processing options */

static int domicemode (int argc, char *argv[], void *p OPTIONAL)
{
	static const char *mmode[] = { "ignore", "passthru", "convert" };

	int cnt = -1;

	if (argc == 2)
	{
		for (cnt = 0; cnt < 3; cnt++)
		{
			if (stricmp (mmode[cnt], argv[1]) == 0)
				break;
		}
		if (cnt != 3)
			micemode = cnt;
	}

	if (cnt == -1 || cnt == 3)
	{
		tprintf ("use: aprs mice [%s|%s|%s]\n", mmode[0], mmode[1], mmode[2]);
		tprintf ("currently: %s\n", mmode[micemode]);
	}

	return 0;
}

static int docontact (int argc, char *argv[], void *p OPTIONAL)
{
	/* 09Jun2003, On the fly now, show current value, more user friendly */

	if (argc == 3)
	{
		if (aprs_contact)
			free (aprs_contact);

		aprs_contact = j2strdup (argv[2]);

		aprs_contact_type = *argv[1];
	}
	else
	{
		tprintf ("use: aprs contact [t|m|h] [contact information]\n");

		if (aprs_contact)
			tprintf ("currently: %c %s\n", aprs_contact_type, aprs_contact);
		else
			tprintf ("currently: %s\n", notdef);
	}

	return 0;
}

#endif	/* end of APRSC */

static int donetlogon (int argc, char *argv[], void *p OPTIONAL)
{
	int err = -1;

	/*
	 * 24Feb2005, Maiko, I've wanted to use the JavaAPRS server filters for
	 * a long time, even knew how they worked a long time ago, just never got
	 * to implementing the filter. So now we need to shift the args around a
	 * bit. New subcommands 'call' and 'filter'. Logon code stays the same.
	 */
	if (argc == 3)
	{
		if (memcmp ("call", argv[1], 4) == 0)
		{
			if (logon_callsign)
			{
				free (logon_callsign);
				free (lwrlogoncall);
			}

			logon_callsign = j2strdup (argv[2]);
			strupr (logon_callsign);

			lwrlogoncall = j2strdup (argv[2]);
			strlwr (lwrlogoncall);

			if (setcall (Aprscall, logon_callsign) == -1)
				tprintf ("APRS setcall failed [%s]\n", logon_callsign);
			else
				err = 0;
		}
		/*
		 * 24Feb2005, Maiko, New APRS IS filter code for logon
		 */
		else if (memcmp ("filter", argv[1], 6) == 0)
		{
			if (logon_filter)
				free (logon_filter);

			logon_filter = j2strdup (argv[2]);

			err = 0;
		}
	}

	if (err == -1)
	{
		/* 24Feb2005, Maiko, Updated to reflect new subcommands */
		tprintf ("use: aprs logon call | filter [callsign | filter string]\n");
		tprintf ("currently: call [%s] filter [%s]\n",
			lwrlogoncall, logon_filter);
	}

#ifndef	MSDOS
	mkdir (APRSdir, 0777);	/* 27May2005, New APRSdir variable */
#endif
	return 0;
}

/* 09Jul2001, This one let's me send messages to users on local RF port */

static int dolocmsg (int argc, char *argv[], void *p OPTIONAL)
{
    char msgbuf[200], srccall[20], *ptr = msgbuf;

    if (argc < 3)
    {
        tprintf ("usage : aprs lmsg <callsign> <message>\n");
        return 0;
    }

	/* 24May2004, Maiko, Oops - forgot about 3rd party messages !!! */
	if (argc == 4)
	{
		strcpy (srccall, argv[3]);
		(void) strupr (srccall);	/* force uppercase */

		if (strcmp (srccall, logon_callsign))
		    ptr += sprintf (msgbuf, "}%s>%s:", srccall, aprsdestcall);
	}

	(void) strupr (argv[1]);	/* 19Jul2001, VE4KLM, Force uppercase */

	sprintf (ptr, ":%-9.9s:%s", argv[1], argv[2]);

	if (aprs_debug)
    	aprslog (-1, "sending msg [%s]", msgbuf);

	if (sendrf (msgbuf, strlen (msgbuf)) == -1)
       	aprslog (-1, "sendrf - can't send local message\n");

	/* 09Jul2001, Maiko, Log messages to the internet */
	sprintf (msgbuf, "%s: %s\n", argv[1], argv[2]);
	/* 16Oct2001, Maiko, Added few more params for more detail */
	(void) log_aprs_msg (msgbuf, 1, logon_callsign);

    return 0;
}

#ifndef	APRSC

/* Internal test function that lets me send messages over the internet */

static int dointmsg (int argc, char *argv[], void *p OPTIONAL)
{
    char msgbuf[200], srccall[20], *srccall_cp;

    if (argc < 3)
    {
        tprintf ("usage : aprs msg <callsign> <message>\n");
        return 0;
    }

	/*
	 * 03Oct2001, Maiko, Need a way to override the src call. I never
	 * anticipated this originally, but I need it now for the new APRS
	 * message center available at the PBBS prompt. This is for internal
	 * use only, and should be hidden from the usage phrase above.
	 */
	if (argc == 4)
	{
		strcpy (srccall, argv[3]);
		(void) strupr (srccall);	/* force uppercase */
		srccall_cp = srccall;
	}
	else
		srccall_cp = logon_callsign;

	(void) strupr (argv[1]);	/* 19Jul2001, VE4KLM, Force uppercase */

    sprintf (msgbuf, "%s>%s::%-9.9s:%s\n",
		srccall_cp, aprsdestcall, argv[1], argv[2]);

	if (aprs_debug)
    	aprslog (-1, "sending msg [%s]", msgbuf);

    if (j2send (Hsocket, msgbuf, strlen (msgbuf)/* + 1*/, 0) < 0)
        tprintf ("unable to send message\n");

	/* 22Jan2002, Maiko, Carefull, next server may not be available */
	if (nxtsrv != (ANETSRV*)0)
	{
		/* 02Aug2001, Maiko, Update outgoing stats */
		nxtsrv->out_bytes += (strlen (msgbuf)/* + 1*/);
		nxtsrv->out_packets++;
	}

	/* 09Jul2001, Maiko, Log messages to the internet */
	sprintf (msgbuf, "%s: %s\n", argv[1], argv[2]);
	/* 16Oct2001, Maiko, Added few more params for more detail */
	(void) log_aprs_msg (msgbuf, 1, srccall_cp);

    return 0;
}

static int dolocator (int argc, char *argv[], void *p OPTIONAL)
{
	/* 09Jun2003, On the fly now, show current value, more user friendly */

	if (argc == 2)
	{
		if (locator)
			free (locator);

		locator = j2strdup (argv[1]);
	}
	else
	{
		tprintf ("use: aprs locator [url]\n");
		tprintf ("currently: %s\n", locator ? locator : notdef);
	}

	return 0;
}

#endif	/* end of APRSC */

static int dointerface (int argc, char *argv[], void *p OPTIONAL)
{
	/* 11Jun2003, On the fly now, show current value, more user friendly */

	if (argc == 2)
	{
		if (aprs_port)
			free (aprs_port);

    	aprs_port = j2strdup (argv[1]);
	}
	else
	{
		tprintf ("use: aprs interface [port]\n");
		tprintf ("currently: %s\n", aprs_port);
	}
    return 0;
}

/*
 * The following 2 functions used to be in the file, aprstorf.c, when I
 * originally released my prototypes. That file is no longer present, all
 * that code is now merged into this source file (aprssrv.c). I did it to
 * say in sync with what Brian Lantz did when he incorporated the APRS stuff
 * into his latest release of TNOS 2.40 on May 14, 2001.
 *
 * aprsd_frame ()
 * aprsd_torf ()
 *
 */

int aprsd_frame (udp)
	struct udp *udp;
{
    return (udp->dest == 16161);
}

void aprsd_torf (iface, bp, ip)
      struct iface *iface;
      struct mbuf *bp;
      struct ip *ip OPTIONAL;
{
	struct mbuf *hbp, *data;
	char recipient[AXALEN];
	struct ax_route *axr;
	char dest[AXALEN];
	// char const *idest;
	struct ax25 addr;
	char *rptr, *ptr;
	char rtmp[15];
	char dti_c;		/* 22Sep2002, New */

	const char *hrdifacename = NULL;	/* important initialization !!! */

	/* 24Oct2001, Maiko, I used to get the iface parameters for the
	 * main APRS port at this point, but now that I'm checking the
	 * heard table, there's no point looping through the iface list
	 * twice. So the code has been moved further down after we find
	 * out who the destination is (recipient or aprsdest call).
	 */

	ptr = bp->data;

	/* 17May2001, VE4KLM, I want to use the sendrf function for more
	 * than just 3rd party messaging now. I want to broadcast a status
	 * and position for this IGATE server, and possibly other stuff down
	 * the road - so now we have to look at the first character of the
	 * message to distinguish whether it is 3rd party or not.
	 * 10Jul2001, VE4KLM, Sysop can now send local aprs messages to users
	 * on the RF port, so we'll have to route those by recipient as well,
	 * meaning we'll have to add the check for the ':' DTI below.
	 */

	if (*ptr == '}' || *ptr == ':')
	{
		/*
		 * Extract the recipient from the APRS text message, because
		 * we need to setup routing based on the recipient. I know, I
		 * know, that goes against the concepts of WIDE, RELAY, etc,
		 * but I designed this TNOS implementation for a bit more
		 * flexibility. Why should people 3 netrom nodes away not
		 * be able to participate in the APRS network ???
		 */
	
		if (*ptr == '}')	/* 11Jul2001, only do this for 3rd party */
		{	
			/* look for message datatype id */
			while (*ptr != ':')
				ptr++;

			ptr++;	/* skip it, check next one */
		}

		dti_c = *ptr;	/* 22Sep2002, New */

		/* 15Sep2004, Maiko, Now want to broadcast WX frames too */
		/* 17Sep2005, Maiko, AND mic-e frames (for Janusz) */
		if (dti_pos_or_stat (dti_c) ||
#ifdef	GATE_MICE_RF
			mice_dti (dti_c) ||
#endif
		/* 21Dec2008, Maiko, Add objects to the stuff we igate */
			dti_c == ';' ||
			dti_c == '_')
		{
			strcpy (rtmp, aprsdestcall);
		}
		else if (dti_c == ':')
		{
			ptr++;	/* skip it */

			/* extract the message recipient */
			rptr = rtmp;
			while (*ptr && *ptr != ' ' && *ptr != ':')
				*rptr++ = *ptr++;

			/* terminate the recipient string */
			*rptr = 0;
		}
		else
		{
			aprslog (-1, "no delimiter, descarding");
			return;
		}
	}
	else
	{
		/* non 3rd party frames are always destined for 'APZ000' */
		strcpy (rtmp, aprsdestcall);
	}

	/*
	 * 21Dec2008, Maiko, 'setcall' is too simplistic to give a definitive
	 * answer to whether the recipient is an actual HAM callsign, or just
	 * another message, wx notice, or bulletin. The 'callcheck' function
	 * I borrowed from Barry (K2MF) is alot better, so I will now use it
	 * instead to choose whether to try and 'route by recipient'. Thanks
	 * to Josh (AB9FT) for pushing me on better igating of bulletins.
	 */
	if (!callcheck (rtmp))
	{
		aprslog (-1, "callcheck (%s) - probable bulletin", rtmp);

		strcpy (rtmp, aprsdestcall);

		if (setcall (recipient, rtmp) == -1)
		{
			aprslog (-1, "setcall failed [%s]", rtmp);
			return;
		}
	}
	else if (setcall (recipient, rtmp) == -1)
	{
		aprslog (-1, "setcall failed [%s]", rtmp);
		return;
	}

	if (setcall (dest, (char*)aprsdestcall) == -1)
	{
		aprslog (-1, "setcall failed [%s]", aprsdestcall);
		return;
	}

	/* 24Oct2001, Maiko, New way to process the interface parameters. Only
	 * one loop is required. This new system incorporates the APRS heard table
	 * so that we should technically now be able to handle APRS messaging on
	 * ANY of the TNOS ax.25 interfaces, not just the default one anymore.
	 */

    if (strcmp (rtmp, aprsdestcall))
	{
		hrdifacename = iface_hrd_on (recipient);
		if (hrdifacename && aprs_debug)
		{
			aprslog (-1, "recipient [%s] last heard on iface [%s]",
				rtmp, hrdifacename);
		}

		/*
		 * 06May2002, Maiko, If we have '+bconlyifhrd' flagged, and
		 * the recipient was not heard, then drop out now !!! Time to
		 * make this system a bit more smart (maybe). If a 3rd party
		 * message appears from the APRS IS, in most cases, there is
		 * no point broadcasting it out to RF if the station was never
		 * heard (this is experimental, may not appeal to all sysops).
		 * 22Sep2002, Maiko, debugging to make sure this is *working*.
		 */
		if (hrdifacename == NULL && bconlyifhrd)
		{
			if (aprs_debug)
				aprslog (-1, "recipient [%s] not heard - msg dropped", rtmp);

			return;
		}
	}

	/* if not in heard table, or if aprsdestcall, then use the default port */
	if (hrdifacename == NULL)
		hrdifacename = aprs_port;

	for (iface = Ifaces; iface != NULLIF; iface = iface->next)
	{
		if (strcmp (iface->name, hrdifacename) == 0)
			break;
	}

	if (iface == NULLIF)
	{
        aprslog (-1, "No APRS interface available !!!");
		return;
	}

	/*
	 * If there's a digipeater route, get it. Please note, I originally
	 * wanted to use the TNOS 'axsend' function here to save code space,
	 * but the digipeater route needs to be setup based on the dest call
	 * of the message receipient, NOT the APRS dest call of APZ000. So I
	 * have to essentially write my own version of axsend to do this.
	 *
	axsend (iface, dest, Mycall, LAPB_COMMAND, UI, data);
	 */

#ifdef	JNOSAPRS
	axr = ax_lookup (recipient, iface);
#else
	axr = ax_lookup (Aprscall, recipient, iface, AX_NONSETUP);
#endif

	/*
	 * 05Jun2001, VE4KLM, OOPS !!! Even though I had wanted 3rd party msgs
	 * to be routed based on recipient, we still need to be in compliance
	 * with APRS routing concepts. If there is no route defined for the
	 * recipient, then route based on the aprs dest call. Why ? The nice
	 * thing about NOS is that you can use the command, 'ax25 route add',
	 * to configure the APRS unproto path (nice eh ???). For example :
	 * 
	 *    ax25 route add APZ000 ax1 WIDE WIDE'
	 */

    if (strcmp (rtmp, aprsdestcall) && axr == NULLAXR)
	{
#ifdef	JNOSAPRS
		axr = ax_lookup (dest, iface);
#else
		axr = ax_lookup (Aprscall, dest, iface, AX_NONSETUP);
#endif
	}

	memcpy (addr.dest, dest, AXALEN);
	memcpy (addr.source, Aprscall, AXALEN);

	addr.cmdrsp = LAPB_COMMAND;

    if (axr != NULLAXR)
	{
        memcpy (addr.digis, axr->digis, (size_t)axr->ndigis * AXALEN);
        addr.ndigis = axr->ndigis;
        // idest = addr.digis[0];
    }
	else
	{
        addr.ndigis = 0;
        // idest = dest;
    }

    addr.nextdigi = 0;

	/* allocate memory for new message (include 2 bytes for CTL & PID) */
    if ((hbp = alloc_mbuf ((int16)(bp->cnt + 2))) == NULLBUF)
	{
		aprslog (-1, "no memory for new message");
	    return;
	}

	/* set control and pid fields */
	hbp->data[0] = uchar (UI);
	hbp->data[1] = uchar (PID_NO_L3);

	/* copy the APRS data to buffer */
    hbp->cnt = (int16)(bp->cnt + 2);
    memcpy (hbp->data + 2, bp->data, bp->cnt);

	/* now put in the ax25 header */
    if ((data = htonax25 (&addr, hbp)) == NULLBUF)
	{
		aprslog (-1, "could not put in the ax25 header");
        free_p (hbp);
        return;
    }

	if (aprs_debug)
	{
    	aprslog (-1, "sending recipient [%s] port [%s] %d digis",
			rtmp, aprs_port, addr.ndigis);
	}

	/* send the packet out !!! */
	(*iface->raw)(iface, data);

	return;
}

#ifndef	APRSC

static int dspigatehdr (void)
{
	tprintf ("\nServer connection:\n     Server            Port  Connected   Pkts In   Bytes     Pkts Out  Bytes\n");

	return 1;
}

static int dspclihdr (void)
{
	tprintf ("\nClient connections:\n    User       Server       Port  Connected   Pkts  Bytes In   Pkts  Bytes Out\n");

	return 1;
}

#endif	/* end of APRSC */

/* 11May2001, VE4KLM, Fingerd statistics so we can put on our website */

int doaprsstat (int argc OPTIONAL, char *argv[] OPTIONAL, void *p OPTIONAL)
{
	char tind, *tptr;
	register int cnt;
	time_t curtime;
	int hdrflg = 0;

	int server = 1, maxnodes;

	ANETSRV *nodeptr;

	/*
	 * 17May2001, VE4KLM, Added the version information. It could be
	 * very useful down the road, since I am still doing development
	 * on this stuff and probably will be for the next little while.
	 * 20Jun2001, VE4KLM, Added uptime.
	 * 31Jul2001, VE4KLM, Changed the look again.
	 */
	tprintf ("Ver: %s, Uptime: %s, Call: %s, Interface: %s\n",
		TASVER, tformat (secclock ()), logon_callsign, aprs_port);

#ifndef	APRSC

	while (server >= 0)
	{
			/* 18Jun2002, Maiko, Now the client info is in the same
			 * loop as the server info stuff, no point duplicating
			 * code.
			 */
		if (server)
			maxnodes = MAXANETSRV;
		else
			maxnodes = MAXANETCLI;

	for (hdrflg = cnt = 0; cnt < maxnodes; cnt++)
	{
		if (server)
			nodeptr = &anetsrv[cnt];
		else
			nodeptr = &anetcli[cnt].srv;

		if (nodeptr->port)
		{
			if (nodeptr->flags == IGATE_ONLINE)
			{
				tptr = tformat ((int32)(time (&curtime) - nodeptr->last));
				tind = '*';
			}
			else
			{
				tptr = tformat ((int32)nodeptr->last);
				tind = ' ';
			}

			if (!hdrflg)
			{
				if (server)
					hdrflg = dspigatehdr ();
				else
					hdrflg = dspclihdr ();
			}

			if (server)
			{
				tprintf ("%c %-20.20s %-5d %s  %-9ld %-9ld %-9ld %ld\n",
					tind, nodeptr->server, nodeptr->port, tptr,
					nodeptr->in_packets, nodeptr->in_bytes,
					nodeptr->out_packets, nodeptr->out_bytes);
			}
			else
			{
				tprintf ("%c %-10.10s%-16.16s%-6d%s  %-6ld%-11ld%-6ld%ld\n",
					tind, anetcli[cnt].user, nodeptr->server, nodeptr->port,
					tptr, nodeptr->in_packets, nodeptr->in_bytes,
					nodeptr->out_packets, nodeptr->out_bytes);
			}
		}
	}

	server--;

	}

#endif	/* end of APRSC */

    /* 14May2001, VE4KLM, More user stats */
    display_aprshrd ();

	return 0;
}

/*
 * NEW for start of October. The start of an APRS message center for the
 * general PBBS user, this is under development and is VERY experimental,
 * so don't expect anything real fancy yet - I'm just playing around.
 * 10Oct2001, Can also be used from the console now.
 * 18Oct2001, Maiko, Time to try this with a JNOS system, should work
 * fine now that I've figured out a few things.
 */
int dombaprs (int argc OPTIONAL, char *argv[] OPTIONAL, void *p)
{
	static long msgidpool = 1L;	/* very simple msgid pool for starters */
	struct mbx *m = (struct mbx *) p;
	char tmpbuf[20], srccall[20], recipient[20], cmd_c;
	char *huhstr = "Huh ?\n", *pingstr = "?APRST";
	char *mymsgargv[4], *cp;
	void *vp = (void*)0;
	int usemsgid = 1;	/* 03Oct2001, Auto message ID for outgoing messages */
	/* 10Oct2001, VE4KLM, Now useable from the console */
	int isconsole = (Curproc->input == Command->input);
	struct mbuf *bp = NULLBUF;	/* MUST do this, or else memory fault */
	int userfmsg = 1;	/* 30May2002, Maiko, RF or APRS is routing option */
	const char *hrdifacename = NULL;	/* 22Sep2002, Maiko, New */

	/* 24May2003, Should have a new session if from console */
	struct session *sp = NULL;
	int usesession = 0;

	/* 24May2003, Use a session if this comes from console */
	if (isconsole)
	{
		/* 25May2003, Right now I'm only allowing one console center,
		 * if I don't put in this check, replies back to us may wind
		 * up going to wrong or invalid sessions or windows
		 */
		if (CurConsoleAPRSC)
		{
			j2tputs ("The APRS message center is already active.\n");
			return 0;
		}

		usesession = 1;
		if ((sp = newsession ("Local APRS message center", TELNET, 0)) == NULLSESSION)
			return 0;

		CurConsoleAPRSC = Curproc;	/* messages back to the sysop */
	}

	/* 10Oct2001, Maiko, Permission not required on TNOS console */
	if (!isconsole)
	{
		/* 11Oct2001, Maiko, Do not allow this from sysop mode, it get's
		 * too complicated trying to redirect the acks back there. Having
		 * the APRS message center available from the PBBS prompt and the
		 * TNOS console should be more than enough for now.
		 *
		 * 13Oct2001, The MBX state is not bitmapped, use '==', not '&'.
		 *
		 * 17Oct2001, Moved this conditional up here, and added a check
		 * to see if the 'm' structure pointer is NULL. Funny why this
		 * would only show up in TNOS 2.41. If someone tries to access
		 * the APRS message center from sysop mode, it seems that 'm' is
		 * set to NULL (normal actually), but why is it not happening in
		 * my TNOS 2.30 release ? Anyways, bottom line, need to check for
		 * 'm' pointer value == NULL, or else it faults on us ...
		 */
		if (m == NULL || m->state == MBX_SYSOP)
		{
			j2tputs ("Sorry, can only be used from PBBS or TNOS console.\n");
			return 0;
		}

		/* 02Oct2001, Maiko, Must have telnet permission (security) */
		if (!(m->privs & TELNET_CMD))
		{
			j2tputs (Noperm);
			return 0;
		}

	}

	/* 10Oct2001, VE4KLM, For TNOS console, use the logon_callsign */
	if (!isconsole)
	{
		strcpy (srccall, m->name);
		(void) strupr (srccall); /* Force uppercase */

	/* 11Oct2001, Maiko, Another snag. If the PBBS user callsign is the
	 * same as the LOGON_CALLSIGN, then tell the user to use the TNOS console
	 * instead (assumption made that it is probably the sysop of the TNOS
	 * system himself (or herself) trying to do this. That's reasonable.
	 */
		if (strcmp (srccall, logon_callsign) == 0)
		{
			j2tputs ("System callsign detected. Please use the TNOS console.\n");
			return 0;
		}

		/* 25Oct2001, Maiko, Wouldn't hurt to log usage */
		aprslog (-1, "%s using APRS msg center", m->name);
	}
	else
		strcpy (srccall, logon_callsign);

#ifdef	DONT_COMPILE
	/* 02Oct2001, Maiko, If our connection to APRS IS is down, no point */
	if (Hsocket == -1)
	{
		j2tputs ("The Internet System is OFFLINE - please try again later.\n");
		return 0;
	}
#endif

#ifndef	APRSC
	/* 30May2002, Maiko, Now that we allow RF messages, the above should
	 * be limited to a simple warning without exiting.
	 */
	if (Hsocket == -1)
		j2tputs ("The Internet System is OFFLINE - Only RF messaging is available.\n");
#endif

	tprintf ("Welcome to the %s Message Center. Use '/h' for help.\n", TASVER);
	j2tputs ("Don't forget to set the msg recipient - use '/r callsign'.\n");
#ifndef	APRSC
	/* 30May2002, Maiko, Toggle message routing */
	j2tputs ("Messages will go out RF - use '/f' to toggle to APRS IS.\n");
#endif

	*recipient = 0;

	while (1)
	{
		usflush (Curproc->output);

		/* 10Oct2001, The PBBS prompt uses the mbxrecvline () call, while
		 * the TNOS console uses the recv_mbuf on the Command input stream.
		 */
		if (!isconsole)
		{
			if (mbxrecvline (m) < 0)
				break;

			cp = m->line;
		}
		else
		{
			free_p (bp);

			/* 24May2003, Use Curproc->input now, not Command->input */
			if (recv_mbuf (Curproc->input, &bp, 0, NULLCHAR, 0) < 0)
				break;

			cp = bp->data;

			rip (cp);	/* need to do this or we get trailing junk */
		}

#ifdef MBXTDISC
		/* Only if not on TNOS console */
		if (!isconsole)
			stop_timer (&m->tdisc);
#endif
		/* no point doing anything if nothing was entered */
		if (*cp == '\0')
		{
			j2tputs ((char*)huhstr);
			continue;
		}

		/* Now parse out special message center commands */
		if (*cp == '/')
		{
			cp++;

			cmd_c = *cp++;

			if (cmd_c == 'q')
			{
				j2tputs ("73!\n");
				break;
			}
			else if (cmd_c == 'r' && *cp == ' ')
			{
				sprintf (recipient, "%.9s", skipwhite (cp));
				(void) strupr (recipient); /* Force uppercase */
		/*
		 * 12Oct2001, Maiko, Validate the AX25 callsign of the recipient. So
		 * far this is really the only function that I can find in TNOS to do
		 * this. It's not perfect, but it's better than nothing.
		 */
				if (setcall (tmpbuf, recipient) == -1)
				{
					j2tputs ("Invalid callsign, try it again.\n");
					*recipient = 0;
				}
				else
				{
					/*
					 * 22Sep2002, Maiko, Let's make this a bit more
					 * user friendly, so the client at least has some
					 * idea if the recipient is available on RF or not.
					 */
					if (userfmsg)
					{
						hrdifacename = iface_hrd_on (tmpbuf);

						/* 22Sep2002, Maiko, Warn client if bconlyifhrd set */
						if (hrdifacename == NULL && bconlyifhrd)
						{
							/* right now just force NO, possibly an
							 * option down the road.
							 */
							tprintf ("callsign not heard, try again.\n");
							*recipient = 0;
						}
					}

					if (*recipient)
					{
						tprintf ("Recipient is now %s", recipient);

						if (userfmsg)
						{
							if (hrdifacename)
							{
								tprintf (" - last heard on port %s",
									hrdifacename);
							}
							else
								tprintf (" - not heard on any port");
						}

						tprintf (".\n");
					}
				}
			}
			else if (cmd_c == 'p')
			{
				cp = (char*)pingstr;
			}
			else if (cmd_c == 'm')
			{
				j2tputs ("Msg ids are now on.\n");
				usemsgid = 1;
			}
			else if (cmd_c == 'n')
			{
				j2tputs ("Msg ids are now off.\n");
				usemsgid = 0;
			}
#ifndef	APRSC
			else if (cmd_c == 'f')
			{
				/* 22Sep2002, Maiko, clear out recipient to be safe */
				*recipient = 0;

				if (!userfmsg)
				{	
					j2tputs ("Msgs will now go out the RF port.\n");
					userfmsg = 1;
				}
				else
				{	
					j2tputs ("Msgs will now go out to the APRS IS.\n");
					userfmsg = 0;
				}
			}
#endif
			else if (cmd_c == 'h')
			{
				j2tputs ("/m - attach msg id to msgs - expect acks.\n");
				j2tputs ("/n - do not use msg ids - no acks.\n");
				j2tputs ("/p - send an APRS ping to current recipient.\n");
				j2tputs ("/q - quit and return to PBBS prompt.\n");
				j2tputs ("/r callsign - set new msg recipient.\n");
#ifndef	APRSC
				/* 30May2002, VE4KLM, Option to force msgs out RF !!! */
				j2tputs ("\n/f - toggle msg routing - RF or APRS IS.\n");
#endif
			}	
			else j2tputs ((char*)huhstr);

			/* we want the ping command to go through */
			if (cmd_c != 'p')
				continue;
		}

		/* Recipient MUST be defined if we want to send out a message */
		if (*recipient == 0)
		{
			j2tputs ("You didn't set the msg recipient - use '/r callsign'.\n");
			continue;
		}

		/*
		 * 15Sep2003, The message content must be no bigger than 67 bytes,
		 * as per the current APRS specification. I don't know why I waited
		 * for 2 years to do this ...
		 */
		if (strlen (cp) > 67)
		{
			tprintf ("* warning - msg cutoff at 67 (max) characters\n");
			*(cp+67) = 0;
		}

		/* 03Oct2001, Concatonate a msg ID to end of message if enabled */
		/* Oops, don't do this for query type messages (APRS Spec says no) */
		if (usemsgid && *cp != '?')
		{
			tprintf ("* sending with msgid %ld *\n", msgidpool);

			sprintf (tmpbuf, "{%ld", msgidpool++);		
			strcat (cp, tmpbuf);
			/* Can't let this go over 5 digits, as if it will ever happen */
			if (msgidpool > 99999L)
				msgidpool = 0;
		}

		/* 03Oct2001, Use existing functions to send to the APRS IS */
		mymsgargv[1] = recipient;
		mymsgargv[2] = cp;
		mymsgargv[3] = srccall;

#ifdef	APRSC
        dolocmsg (4, mymsgargv, vp);
#else
		/* 30May2002, Maiko, Now allow msgs to RF from bbs */
		if (userfmsg)
			dolocmsg (4, mymsgargv, vp);
		else
			dointmsg (4, mymsgargv, vp);
#endif

		/* 05May2004, Maiko, Make sure our web interface db is updated */
		msgdb_save (srccall, recipient, cp);

#ifdef MBXTDISC
		/* Only if not on TNOS console */
		if (!isconsole)
		{
#ifdef	TNOS241
			start_detached_timer (__FILE__, __LINE__, &m->tdisc);
#else
			start_detached_timer (&m->tdisc);
#endif
		}
#endif
	}

	if (isconsole)
		free_p (bp);

	/* 24May2003, Maiko */
	if (usesession)
	{
		if (sp)
			freesession (sp);

		CurConsoleAPRSC = NULL;
	}

	return 0;
}

void send45845msg (int userfmsg, char *from, char *recipient, char *data)
{
	char *mymsgargv[4];

	void *vp = (void*)0;

	mymsgargv[1] = recipient;
	mymsgargv[2] = data;
	mymsgargv[3] = from;

#ifdef  APRSC
    dolocmsg (4, mymsgargv, vp);
#else
	if (userfmsg)
		dolocmsg (4, mymsgargv, vp);
	else
		dointmsg (4, mymsgargv, vp);
#endif

	msgdb_save (from, recipient, data);
}

/*
 * 25Oct2001, Maiko, Time to put APRS logging to it's own
 * file. I've had several people *complain* about this, and
 * rightfully so. I shouldn't be using the main log, it just
 * happened that way (it was convenient at the time) The two
 * functions below are adaptations of the main logging functions
 * that are defined in the TNOS 'main.c' source file.
 */

int doaprslog (int argc, char *argv[], void *p OPTIONAL)
{
	static char *aprslogname = NULLCHAR;

	char fname[128];

	/* 21Jun2003, Maiko, More user friendly now */

	if (argc == 2)
	{
		if (!strcmp (argv[1], "off"))
		{
			if (aprsLogfp)
			{
				fclose (aprsLogfp);
				aprsLogfp = NULLFILE;
				free (aprslogname);
				aprslogname = NULLCHAR;
			}
			else tprintf ("already off\n");
		}
		else
		{
			if (aprsLogfp)
				tprintf ("already on, turn it off first\n");
			else
			{
				strncpy (fname,
					make_fname (Command->curdirs->dir, argv[1]), 128);
				aprslogname = j2strdup (fname);
				aprsLogfp = fopen (aprslogname, APPEND_TEXT);
			}
		}
	}
	else
	{
		tprintf ("use: aprs log [path or filename | off]\n");

		if (aprsLogfp && aprslogname != NULLCHAR)
			tprintf ("currently: %s\n", aprslogname);
		else
			tprintf ("currently: off\n");
	}

	return 0;
}

#ifdef LOG_GMT_TZ
#define TZTIME(x) asctime(gmtime(x))
#else
#define TZTIME(x) ctime(x)
#endif

void aprslog (int s, const char *fmt,...)
{
	va_list ap;
	char *cp;
	time_t t;
	int i;
	struct sockaddr fsocket;
	char buf[SOBUF];

#ifdef	MSDOS
	int fd;
#endif

	if (aprsLogfp == NULLFILE)
		return;

	va_start (ap, fmt);
	(void) vsprintf (buf, fmt, ap);
	va_end (ap);

	(void) time (&t);
	cp = TZTIME (&t);
	rip (cp);
	i = MAXSOCKSIZE;
	fprintf (aprsLogfp, "%s", cp);
	if (j2getpeername (s, (char *) &fsocket, &i) != -1)
		fprintf (aprsLogfp, " %s", psocket (&fsocket));

	fprintf (aprsLogfp, " - %s\n", buf);
	(void) fflush (aprsLogfp);

#ifdef	MSDOS
	/* MS-DOS doesn't really flush files until they're closed */
	fd = fileno (aprsLogfp);
	if ((fd = dup (fd)) != -1)
		close (fd);
#endif

}

#endif
