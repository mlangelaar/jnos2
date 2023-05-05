/*
 * JNOS 2.0
 *
 * $Id: wpages.c,v 1.1 2015/04/22 01:51:45 root Exp root $
 *
 * 01Feb2012, Maiko (VE4KLM), Incorporate the Lantz TNOS WP code, and try
 * and do it in one day, basically throwing it into one big file - subtle
 * differences between JNOS and TNOS micro functions and such - arggg !!!
 *
 * Depends also on TNOS 'merge.c' source file to be included in Make
 *
 * 15Mar2012, Maiko (VE4KLM), develop some user preference system specific
 * to the white page features, for now create a <user>.wpp, that seems to
 * be the easiest and quickest way to do this. I have no more room in the
 * ftpusers flags and/or m->sid fields to do this. See END of this file.
 *
 * 23Apr2012, Maiko, Serious bug fixed, change the 17 in the call to sortit()
 * to a value of 16 instead, rest of the bug is fixed in the sortit() module.
 *
 */

#include "global.h"

#ifdef WPAGES

#include "cmdparse.h"
#include "timer.h"
#include "bm.h"

#include "mailutil.h"
#include "unix.h"

#include "files.h"	/* 15Mar2012, Maiko */
#include "telnet.h"	/* 16Mar2012, Maiko */

static char DAEMONSTR[] = "%sREQSVR@%s (Mail Delivery Subsystem)\n";

/* 06Apr2012, Maiko, These should not be LONG constants */
#define MSPMINUTE (1000*60)
#define MSPHOUR (1000*60*60)
#define MSPDAY  (1000*60*60*24)

#define WPDEST  10

/* 06Apr2012, Maiko, These should not be LONG constants */
#define MSPMINUTE (1000*60)
#define MSPHOUR (1000*60*60)
#define MSPDAY  (1000*60*60*24)

#define WPAGEENTRYSIZE    29
#define WPAGEBBSENTRYSIZE 48

#define DIGI_IDS "+?#-"

static int WPClient, WPServer, WPtempAge = 30, Oldwpages_age = 30;
static struct timer WPUpdatetimer, Oldwpagestimer;
static char *WPcall, *WPdestinations[WPDEST];

static char wp_syntax[] = "Syntax: WP user%s\n";
static char sorry[] = "Command is only available to the SYSOP!\n";
static char notinwp[] = "User not in whitepages! If local user, try \"ML user\"...\n";
/* static char wplists2[] = "White Pages lists this person as '%s'\n"; */
static char wp_sysopsyntax[] = "Syntax: WP user @bbsname\n";
static char updating[] = "Updating database....\n";

int MbWpages = 1;

extern char *WhitePages;          /* White Pages user log file */
extern char *WhitePagesBBS;       /* White Pages BBS log file */
extern char *WPUpdateFile;        /* White Pages update temp file */

extern char *Mbhaddress, Mycall[];

static char *wpageCheck (char *string, int bbs, int updateit, char which);

void wpageAdd (char *entry, int bbs, int updateit, char which);
void RenewWPages ();

/* File : merge.c */
extern FILE *fopennew (const char *fname, const char *mode);
extern FILE *fopentmp (const char *fname, const char *mode);
extern int merge (const char *fname);

/* File : sort.c */
extern void sortit (const char *fname,int entrysize,
	int searchsize, int strsize,time_t age);

int wpage_options (char *user, int mode);

/* 02Feb2012, More code from Lantz TNOS 2.40 source */
char *wpage_exp (char *to, int hier, int exphome)
{
	char origbbs[40], buf[12], *cp2, *cp = (char*) -1;

	if (strchr (to, '%'))
		return (to);

	/* This section looks up the destination bbs in the White Pages file,
	 * if user not a BBS. If found, the complete address is substituted.
	 */

	if (cp == (char *) -1)
	{
		cp2 = strchr (to, '@');

		if (cp2 == NULLCHAR)	/* no dest BBS given */
		{
			if ((cp2 = wpageCheck (to, 0, 0, 'X')) != NULLCHAR)	/* found bbs */
			{
				free (to);
				strlwr (cp2);
				to = cp2;
			}
			cp2 = strchr (to, '@');
		}
		if (hier && cp2 != NULLCHAR)
		{
 			/* we have a dest bbs, get correct, full haddress */
			cp = cp2++;
			if ((cp2 = wpageCheck (cp2, 1, 0, 'X')) != NULLCHAR)
			{
				*cp = '\0';
				sprintf (origbbs, "%s@%s", to, cp2);
				free (to);
				free (cp2);
				strlwr (origbbs);
				to = strdup (origbbs);
			}
		}
	}
	else if (exphome)
	{
		pax25 (buf, Mycall);
		if ((cp = strpbrk (buf, DIGI_IDS)) != NULLCHAR)
			*cp = '\0';	/* remove SSID */
		sprintf (origbbs, "%s@%s%s%s", to, buf,
			 (Mbhaddress != NULLCHAR) ? "." : "",
			 (Mbhaddress != NULLCHAR) ? Mbhaddress : "");
		free (to);
		to = strdup (origbbs);
	}

	pwait (NULL);

	return (to);
}

/*
 * 12Mar2012, Maiko (VE4KLM), Split out lookup to a new function
 * 15Mar2012, Maiko, added 'SP use' flag, and now returns the call
 * in case the user decides to change to WP routing recommendation,
 * and by late night 15Mar2012, include user options (new function).
 * 16Mar2012, Maiko, Okay, I think I got this FINALLY the way I want
 * it to work. Good grief !!! How much trouble can a '\n' be ???
 */

int wpage_lookup (char **src, char *name, struct mbx *m)
{
	char *cp, *cp2;

	int c, wpopt = 0;

	if (!WPClient)
		return 0;

	if (name)
	{
		wpopt = wpage_options (name, 0);	/* 15Mar2012, Maiko, SP options */

		if (wpopt < 2)	/* 0 means no option file, 1 means do not check */
			return 0;
	}

	cp = wpageCheck (*src, 0, 0, 'X');

	if (!cp)
	{
		if (!name)
			j2tputs (notinwp);
	}
	else
	{
		cp2 = wpage_exp (strdup (*src), 1, 1);
		strupr (cp2);

		if (wpopt < 4)
			tprintf ("wp routing '%s'", cp2);

		if (name)
		{
			if (wpopt < 3)
			{
				c = mykeywait (" - use ? (Y/n) ", m);
			}
			else
			{
				c = 'Y';

				if (wpopt < 4)
					j2tputs ("\n");
			}

			if (c == 'Y' || c == 'y' || c == 0 /* '\n' */)
			{
/*
				if (wpopt < 4)
					j2tputs ("wp routing will be used ...\n");
*/

				free (*src);
				*src = strdup (cp2);
			}
		}
		else if (wpopt < 4) j2tputs ("\n");

		free (cp2);
		free (cp);
	}

	return 1;
}

int dombwpages (int argc, char **argv, void *p)
{
	struct mbx *m;
	char buf[80], *cp, origchar = 0;

	extern void tflush ();	/* 01Oct2019, Maiko (VE4KLM), compiler complaining implicit declaration */

	m = (struct mbx *) p;

	if (argc < 2) {
		tprintf (wp_syntax, m->privs & SYSOP_CMD ? " [@bbsname]" : "");
		return 0;
	}
	if (!(m->privs & SYSOP_CMD) && argc > 2) {
		j2tputs (sorry);
		return 0;
	}
	if (argc == 2)		/* lookup */
	{
		/* 12Mar2012, Maiko, Original code put into separate function */
		wpage_lookup (&argv[1], (char*)0, (struct mbx*)0);
	}
	else
	{		/* else, setting it */
		if (argv[2][0] != '@')
			j2tputs (wp_sysopsyntax);
		else {
			if ((cp = strchr (argv[2], '.')) != NULLCHAR) {
				sprintf (buf, "%s", &argv[2][1]); /* 05Jul2016, Maiko, cmplr */
				(void) strupr (buf);
				wpageAdd (buf, 1, 1, 'U');
				origchar = *cp;
				*cp = 0;
			}
			sprintf (buf, "%s%s", argv[1], argv[2]);
			(void) strlwr (buf);
			wpageAdd (buf, 0, 1, 'U');
			if (cp && origchar)
				*cp = origchar;
			j2tputs (updating);
			tflush ();
			pwait (NULL);
			RenewWPages ();
		}
	}
	return 0;
}

void trimright (char *cp)
{
	while (cp[strlen (cp) - 1] == ' ')
		cp[strlen (cp) - 1] = 0;
}

void add_WPUpdate (char *origcall, char *home, char *name, char which)
{
	char here[80], *call, *bbshier, *cp, *who = NULLCHAR;
	struct tm *t;
	time_t now;
	FILE *fp;

	/*
	log (-1, "add_WPUpdate origcall [%s] home [%s] name [%s] which [%c]",
		origcall, home, name, which);
	 */
 
	if (!WPClient)
		return;

	if (which == 'X' || which == 'T')
		return;

	if ((fp = fopen (WPUpdateFile, APPEND_TEXT)) == 0)
		return;

	time (&now); t = localtime (&now);

	call = strdup (origcall);
	strupr (call);
	cp = strpbrk (call, ".#@[");
	if (cp)
		*cp = 0;

#ifdef TNOS_240
	if (mbxRCall)
		strncpy (here, mbxRCall, 80);
	else
#endif
		pax25 (here, Mycall);

	if (!home || !stricmp (here, home))
	{
		if (Mbhaddress)
		{
			strcat (here, ".");
			strcat (here, Mbhaddress);
		}
		bbshier = strdup (here);
	}
	else bbshier = wpageCheck (home, 1, 0, 'X');

	if (bbshier)
	{
		if (name)
		{
			who = strdup (&name[1]);
			cp = strpbrk (who, " )");
			if (cp)
				*cp = 0;
		}
		fprintf (fp, "On %02d%02d%02d %s/%c @ %s zip ? %s ?\n",
				 (t->tm_year % 100), t->tm_mon + 1, t->tm_mday, call, which,
				 bbshier, (who && *who) ? who : "?");
		if (who)
			free (who);
		free (bbshier);
	}
	free (call);
	fclose (fp);
}

static void wpageUpdate (
FILE *fp,
char *string,
char *entry,
char *oldtime,
int bbs,
int updateit,
char which
) {
time_t now;
char buf[LINELEN], *cp;
char compare[40];
int thesize, changed = 0;
int thelen;
long offset;

	/* parameters string and oldtime are not being used, yet */

	thesize = (bbs) ? 32 : 13;
	(void) time (&now);

	/* make a comparison string from 'string' */
	strncpy (buf, string, (unsigned) thesize);
	buf[thesize] = 0;
	if ((cp = strchr (buf, ' ')) != NULLCHAR)
		*cp = 0;

	/* make a comparison string from 'entry' */
	strncpy (compare, entry, (unsigned) thesize);
	compare[thesize] = 0;
	if ((cp = strchr (compare, ' ')) != NULLCHAR)
		*cp = 0;

	if (!bbs && (cp = strchr (buf, '.')) != NULLCHAR)
		*cp = 0;

	/* has this entry changed ? */
	if (stricmp (compare, buf))
		changed = 1;

	/* if we are updating it, take the NEW entry */
	if (updateit && which != 'G')
		entry = buf;

	/* write the old or new entry, with the new timestamp and close file */
	fprintf (fp, "%-*s %-14ld\n", thesize, entry, now);
	(void) fclose (fp);
	pwait (NULL);

	/* If we were updating the entry and the entry DID change..... */
	if (changed && which != 'T') {
		if (updateit) {
			/* now we update the temporary wpages */
			if (!bbs) {	/* no temp for wpagebbs */
				char buf2[LINELEN];

				if ((cp = strchr (buf, '@')) != NULLCHAR)
					thelen = (int) (cp - buf);
				else
					thelen = (int) strlen (buf);
				if ((fp = fopentmp (WhitePages, READ_TEXT)) != NULLFILE) {
					while (offset = ftell (fp), fgets (buf2, LINELEN - 1, fp)) {
						if (!strnicmp (buf2, string, (unsigned) thelen)) {
							fseek (fp, offset, SEEK_SET);
							break;
						}
					}
				} else
					fp = fopentmp (WhitePages, APPEND_TEXT);
				fprintf (fp, "%-*s %-14ld\n", thesize, buf, now);
				(void) fclose (fp);
				pwait (NULL);
			}

			// log (-1, "WP buf [%s]", buf);

			/* record it in the daily WP Update temp file */
			if (bbs)
				add_WPUpdate (buf, buf, NULLCHAR, which);
			else {
				cp = strchr (buf, '@');
				if (cp)
					*cp++ = 0;
				add_WPUpdate (buf, cp, NULLCHAR, which);
			}
		}
	}
}

static int searchfileFP (searchfor, fp, buf, entrysize, passes, searchlen)
register char *searchfor;
register FILE *fp;
register char *buf;
int entrysize, *passes, searchlen;
{
register int low = 0, high;
long size;
register int start;
register int result, found = 0;
int theindex = 0;
char *cp, origchar = 0;

	if (passes)
		*passes = 0;
	if (!searchlen)
		searchlen = (int) strlen (searchfor);
	size = (long) filelength (fileno(fp));
	if (size < entrysize)
		return (-1);
	size /= entrysize;
	high = size - 1;
	theindex = start = size / 2;

	for ( ; ; )	{
		pwait (NULL);
		fseek (fp, (long) ((long)theindex * (long)entrysize), 0);
		(void) fread (buf, 1, (unsigned)entrysize, fp);
		if (feof(fp))
			continue;
		if (passes)
			(*passes)++;
		cp = strpbrk (buf, ".@ \t");
		if (cp)	{
			origchar = *cp;
			*cp = 0;
		}
		result = strnicmp (searchfor, buf, (unsigned)searchlen);
		if (!result)	{
			if (searchlen == (int)strlen(buf))	{
				found = 1;
				if (cp)
					*cp = origchar;
				break;
			}
		}
		if (start != 1)
			start /= 2;
		if (result < 0)	{
			high = theindex - 1;
			theindex -= start;
		} else	{
			low = theindex + 1;
			theindex += start;
		}
		if (theindex < low || theindex > high)
			break;
	}
	if (found)	{		/* refresh it */
		fseek (fp, (long) ((long)theindex * (long)entrysize), 0);
		(void) fread (buf, 1, (unsigned) entrysize, fp);
	}
	return ((found) ? theindex : -1);
}

int searchfile (searchfor, fname, buf, entrysize, passes, searchlen)
register char *searchfor;
const char *fname;
register char *buf;
int entrysize, *passes, searchlen;
{
int result;
FILE *fp;

	if ((fp = fopen (fname, "rb")) == NULL)
		return (-1);
	result = searchfileFP (searchfor, fp, buf, entrysize, passes, searchlen);
	(void) fclose (fp);
	return (result);	
}

/* Returns a copy (strdup'ed) of existing entry if string is in WP file */
char *wpageCheck (char *string, int bbs, int updateit, char which)
{
/* note: updateit is NEVER set except sometimes when called from wpageAdd() */
register FILE *fp;
char buf[LINELEN], *retval = NULLCHAR;
register char *cp;
int result, thelen;
int entrysize;
char const *fname;
int skipupdate = 0;

	if (!MbWpages || string == NULLCHAR || !*string)
		return (retval);
	if (!strnicmp (string, "sysop", 5))
		return (retval);
	entrysize = (bbs) ? WPAGEBBSENTRYSIZE : WPAGEENTRYSIZE;
	fname = (bbs) ? WhitePagesBBS : WhitePages;
	if ((cp = strchr (string, (bbs) ? '.' : '@')) != NULLCHAR)
		thelen = (int) (cp - string);
	else
		thelen = (int) strlen (string);

/* 14Mar2012, Maiko (VE4KLM), this search file is VERY MESSY/UGLY, if any
   of the entries in any of the WP files is off by one byte in length, it
   really messes up !!! This cost me an entire day of work, and I'm still
   trying to figure out how the one entry was written to file.

   Using entrysize, size, high, theentry is ugly. I think I might just
   rewrite the search function or even just change to using another one.
 */
	result = searchfile (string, fname, buf, entrysize, 0, thelen);
	if (result == -1 && !updateit) {
		if ((fp = fopennew (fname, READ_TEXT)) != NULLFILE) {
			while (fgets (buf, LINELEN - 1, fp)) {
				if (!strnicmp (buf, string, (unsigned) thelen)) {
					result = 0;
					skipupdate = 1;
					break;
				}
			}
			(void) fclose (fp);
		}
	}
	if (result != -1) {	/* found it */
		if (((cp = strpbrk (buf, " \t")) != NULLCHAR))
			*cp = '\0';
		rip (buf);
		retval = strdup (buf);
		if (!skipupdate) {
			fp = fopen (fname, "r+");
			if (fp != NULLFILE)	{
				fseek (fp, (long) (long) result * (long) entrysize, 0);
				wpageUpdate (fp, string, buf, &buf[(bbs) ? 33 : 14], bbs, updateit, (which != 'G') ? which : 'X');
				/* wpageUpdate does the (void) fclose(fp); */
			}
		}
	}

	return retval;
}

void wpageAdd (char *entry, int bbs, int updateit, char which)
{
	time_t now;
	FILE *fp;
	char *last, buf[14];

	if (!MbWpages || entry == NULLCHAR)
		return;
	if (!strnicmp (entry, "sysop", 5))
		return;

	// log (-1, "wpageAdd [%s]", entry);

	last = wpageCheck (entry, bbs, updateit, which);
	if (!last) {
		(void) time (&now);
		if (!bbs) {
			strncpy (buf, entry, 13);
			buf[13] = 0;
			if ((last = strchr (buf, '.')) != NULLCHAR)
				*last = 0;
			entry = buf;
		}
		if ((fp = fopennew ((bbs) ? WhitePagesBBS : WhitePages, APPEND_TEXT)) != NULLFILE) {
			fprintf (fp, "%-*s %-14ld\n", (bbs) ? 32 : 13, entry, now);	/* Save h_addr in whitepages file */
			pwait (NULL);
			(void) fclose (fp);
		}

		// log (-1, "WP entry [%s]", entry);

		if (bbs)
			add_WPUpdate (entry, entry, NULLCHAR, which);
		else {
			last = strchr (entry, '@');
			if (last)
				*last++ = 0;
			add_WPUpdate (entry, last, NULLCHAR, which);
		}
	} else
		free (last);
}

static int dotempage (int argc, char **argv, void *p)
{
	if (argc < 2)
		tprintf ("WP Temporary Assignment age: %d days\n", WPtempAge);
	else
		WPtempAge = atoi (argv[1]);
	return 0;
}

void mark_forwarded (FILE *fp, long ind, char thetype)
{
	long save;

	clearerr (fp);
	
	save = ftell (fp);
	fseek (fp, ind, SEEK_SET);
	fputc (thetype, fp);    /* mark as done! */
	fflush (fp);
	fseek (fp, save, SEEK_SET);
}

static void TempWPages ()
{
	FILE *fp;
	long offset, validentries = 0;
	char buf[LINELEN], *cp;
	time_t then, now;

	(void) time (&now);
	if ((fp = fopentmp (WhitePages, READ_TEXT)) != NULLFILE) {
		while (offset = ftell (fp), fgets (buf, LINELEN - 1, fp)) {
			pwait (NULL);
			if (*buf == '$')
				continue;
			cp = skipnonwhite (buf);
			*cp++ = 0;
			cp = skipwhite (cp);
			then = atol (cp);
			if ((then + (WPtempAge * MSPDAY)) > now) {
				validentries++;
#if 0
				tcmdprintf ("temp: entry '%s' to remain %ld days\n", buf,
					    ((then + (WPtempAge * MSPDAY)) - now) / MSPDAY);
#endif
				continue;
			}
			wpageAdd (buf, 0, 1, 'T');
			mark_forwarded (fp, offset, '$');
		}
		(void) fclose (fp);
		if (!validentries) {
			sprintf (buf, "%s.tmp", WhitePages);
			unlink (buf);
		}
	}
	pwait (NULL);
}

static void
exp_function (filename, fname, theage, thetimer, strsize, sortsize)
const char *filename;
const char *fname;
int theage;
struct timer *thetimer;
int strsize, sortsize;
{
	time_t age;

	extern int j2wp_expired;

	stop_timer (thetimer);

	log (-1, "wp processing");

	pwait (NULL);

	if (!merge (filename))	/* add in contents of '.new' file */
	{
		log (-1, "no new entries");
		return;
	}

	age = (time_t) (theage * 86400L);

	/* sort entire file */
	sortit (filename, strsize + 16, sortsize, strsize, age);

	if (j2wp_expired)
		log (-1, "%d expired", j2wp_expired);

	start_timer (thetimer);

	return;
}

void RenewWPages ()
{
	exp_function (WhitePages, "wpages", Oldwpages_age, &Oldwpagestimer, 13, 6);
}

static void Oldwpagesprocess (int a, void *v1, void *v2)
{
#ifdef TNOS_240
	setMaintenance ();
#endif
	exp_function (WhitePagesBBS, "wpagebbs", Oldwpages_age, &Oldwpagestimer, 32, 6);
	RenewWPages ();
	TempWPages ();
#ifdef TNOS_240
	clearMaintenance ();
#endif
}

static void Oldwpagestick (void *p)
{
	if (newproc ("Oldwpages", 2048, Oldwpagesprocess, 0, NULL, NULL, 0) == NULLPROC)
		log (-1, "Couldn't start Oldwpages process");
}

static int dotimer (int argc, char **argv, void *p)
{
	if (argc < 2)
	{
		tprintf ("WP timer = %d/%d minutes\n",
			read_timer (&Oldwpagestimer) / MSPMINUTE,
				dur_timer (&Oldwpagestimer) / MSPMINUTE);

		return 0;
	}

	stop_timer (&Oldwpagestimer);	/* just in case */

	Oldwpagestimer.func = Oldwpagestick;	/* what to call on timeout */

	Oldwpagestimer.arg = NULL;	/* dummy value */

	/* set duration */
	set_timer (&Oldwpagestimer, (uint32) atoi (argv[1]) * MSPMINUTE);

	start_timer (&Oldwpagestimer);	/* fire it up */

	return 0;
}

static int doexpkick (int argc, char **argv, void *p)
{
	Oldwpagestick ((void*)0);

	return 0;
}

static int doage (int argc, char **argv, void *p)
{
	if (argc < 2)
		tprintf ("WP age: %d days\n", Oldwpages_age);
	else
		Oldwpages_age = atoi (argv[1]);

	return 0;
}

static int dowpclient (int argc, char **argv, void *p)
{
	return setbool (&WPClient, "Enable WP Client - process WP updates", argc, argv);
}

static int dowpsupport (int argc, char **argv, void *p)
{
	return setbool (&MbWpages, "Use White Pages dbase", argc, argv);
}

static int dowpserver (int argc, char **argv, void *p)
{
	return setbool (&WPServer, "Enable WP Server - process incoming WP messages", argc, argv);
}

static int dowpclicall (int argc, char **argv, void *p)
{
	char buf[128], *cp;

	strncpy (buf, Hostname, 128);

	if ((cp = strchr (buf, '.')) != NULLCHAR)
		*cp = 0;

	if (argc > 1)
	{
		free (WPcall);
		WPcall = strdup (argv[1]);
	}
		
	j2tputs ("WP updates will be sent from ");

	if (!WPcall)
		tprintf ("WP@%s - no clientcall set\n", buf);
	else
		tprintf ("%s@%s\n", WPcall, buf);

	return 0;
}

static int dowpdest (int argc, char **argv, void *p)
{
	int k;

	if (argc == 1)
	{
		if (!WPdestinations[0])
			j2tputs ("No WP update destinations are defined\n");
		else
		{
			j2tputs ("WP updates will be sent to the following PBBSs...\n\n");
			for (k = 0; k < WPDEST; k++)
				if (WPdestinations[k])
					tprintf ("%s\n", WPdestinations[k]);
		}
		return 0;
	}

	for (k = 0; k < WPDEST; k++)
		if (!WPdestinations[k])
		{
			WPdestinations[k] = strdup (argv[1]);
			break;
		}

	if (k == WPDEST)
		tprintf ("Sorry, max of %d update dests already defined!\n", WPDEST);

	return 0;
}

int rdaemon(data, replyto, from, to, msg, msgtype)
FILE *data;		/* pointer to rewound data file */
const char *replyto;
char *from;
char *to;
const char *msg, msgtype;
{
time_t t;
FILE *tfile;
char buf[LINELEN], *newaddr, *orgto;
long mid;

	if((tfile = tmpfile()) == NULLFILE)
		return -1;

	to = strdup ((to == NULLCHAR) ? "sysop" : to);
		
	orgto = strdup (to);

	if((newaddr = rewrite_address(to, 0)) != NULLCHAR)	{
		free (to);
		to = newaddr;
	}
	time(&t);
	fprintf(tfile, "%s", Hdrs[RECEIVED]); /* 05Jul2016, Maiko, compiler */
	fprintf(tfile,"from %s ",Hostname);
#ifdef TNOS_240
	mid = get_msgid(0);
#else
	mid = get_msgid();
#endif
#ifdef MBFWD
	fprintf(tfile,"by %s (%s) with SMTP\n\tid AA%ld ; %s",
		Hostname, (Mbfwdinfo != NULLCHAR) ? Mbfwdinfo : shortversion, \
		mid, ptime(&t));
#else
	fprintf(tfile,"by %s (%s) with SMTP\n\tid AA%ld ; %s",
		Hostname, shortversion, mid, ptime(&t));
#endif
	fprintf(tfile,"%s%c\n",Hdrs[BBSTYPE], msgtype);
	fprintf(tfile,"%s%s",Hdrs[DATE],ptime(&t));
#ifdef TNOS_240
	mid = get_msgid(1);
#else
	mid = get_msgid();
#endif
	fprintf(tfile,"%s<%ld@%s>\n",Hdrs[MSGID],mid,Hostname);
	if (from == NULLCHAR)
		fprintf(tfile,DAEMONSTR,Hdrs[FROM],Hostname);
	else
		fprintf(tfile,"%s%s\n",Hdrs[FROM],from);
	fprintf(tfile,"%s%s\n",Hdrs[TO], strlwr(orgto));
	if (replyto != NULLCHAR)
		fprintf(tfile, "%s%s\n", Hdrs[REPLYTO], replyto);
	fprintf(tfile,"%s%s\n\n",Hdrs[SUBJECT], msg);
	pwait (NULL);
	
	if (data != NULLFILE)
	{
		while(fgets(buf,sizeof(buf),data) != NULLCHAR)
		{
			pwait (NULL);
			fputs(buf,tfile);
		}
	} else
		fputc ('\n', tfile);
	rewind(tfile);

	sprintf (buf, "REQSVR@%s", Hostname);
	mailuser(tfile, (from) ? from : buf, to);
	free (to);
	free (orgto);
	fclose(tfile);
	pwait (NULL);
	return 0;
}

static void WPUpdatetick (void *p)
{
	char here[128], there[128];
	FILE *fp;
	int k;

	start_timer (&WPUpdatetimer);

	if (WPClient)
	{
		if ((fp = fopen (WPUpdateFile, READ_TEXT)) == 0)
			return;

		sprintf (here, "%s@%s", (WPcall) ? WPcall : "WP", Hostname);

		for (k = 0; k < WPDEST; k++)
		{
			if (WPdestinations[k])
			{
				log (-1, "WP Update sent to: %s", WPdestinations[k]);
				rewind (fp);
				sprintf (there, "WP@%s", WPdestinations[k]);
				rdaemon (fp, NULLCHAR, here, there, "WP Update", 'P');
			}
		}

		fclose (fp);
	}

	unlink (WPUpdateFile);
}

static int dowpupdate (int argc, char **argv, void *p)
{
	if (argc < 2)
	{
		tprintf ("WP update timer = %d/%d hours\n",
			read_timer (&WPUpdatetimer) / MSPHOUR,
				dur_timer (&WPUpdatetimer) / MSPHOUR);

		return 0;
	}

	if (*argv[1] == 'n')
	{
		WPUpdatetick (NULL);
		return 0;
	}

	/* set the timer - just in case */
	stop_timer (&WPUpdatetimer);

	/* what to call on timeout */
	WPUpdatetimer.func = (void (*)(void *)) WPUpdatetick;

	WPUpdatetimer.arg = NULL;	/* dummy value */

	/* set timer duration */
	set_timer (&WPUpdatetimer, (uint32) atoi (argv[1]) * MSPHOUR);

	start_timer (&WPUpdatetimer);

	return 0;
}

/* White Page Server */

static void getfield (char *into, char *from, int fieldtype)
{
	char *cp, *temp;

	cp = &from[strlen(Hdrs[fieldtype])];
	if (*cp == '"')	{
		cp++;
		if ((temp = strchr (cp, '"')) != NULLCHAR)
			cp = temp+1;
	}
	if ((temp = strchr (cp, '<')) != NULLCHAR)	{
		cp = temp+1;
		if ((temp = strchr (cp, '>')) != NULLCHAR)
			*temp = 0;
	}
	if ((temp = strpbrk (cp, " \t")) != NULLCHAR)
		*temp = 0;
	strcpy (into, cp);
}

void parseheader (fp, from, subject, to, bid, buf, startat)
FILE *fp;
char *from, *subject, *to, *bid, *buf;
long *startat;
{
	char *cp, *temp;
	int fieldtype, prevtype;

	rewind (fp);
	subject[0] = from[0] = 0;
	while(fgets(buf,128,fp) != NULLCHAR)	{
		if(buf[0] == '\n')
			break;
		rip (buf);
		fieldtype = htype(buf, &prevtype);
		if (fieldtype == FROM)	{
			trimright (buf);
			getfield (from, buf, fieldtype);
		}
		if (fieldtype == SUBJECT)	{
			trimright (buf);
			cp = &buf[strlen(Hdrs[SUBJECT])];
			cp = skipwhite(cp);
			if (*cp == '"')	{
				cp++;
				if ((temp = strchr (cp, '"')) != NULLCHAR)
					*temp = 0;
			}
			strcpy (subject, cp);
		}
		if ((to != NULLCHAR) && (fieldtype == TO))	{
			trimright (buf);
			getfield (to, buf, fieldtype);
		}
		if ((bid != NULLCHAR) && (fieldtype == MSGID))	{
			trimright (buf);
			getfield (bid, buf, fieldtype);
		}
	}
	*startat = ftell (fp);
	if (fgets(buf,128,fp) != NULLCHAR)	{
		if(!strnicmp (buf, "R:", 2))	{
			while(fgets(buf,128,fp) != NULLCHAR)	{
				if(buf[0] == '\n')
					break;
			}
			*startat = ftell (fp);
		}
	}
	pwait (NULL);
}

int wpserver (FILE *fp, const char *from)
{
	/* 01Oct2019, Maiko, compiler format overflow warning, bumped name from to 64 to 93 (call + bbs + null) */
	char call[12], bbs[80], zip[12], name[93], *qth, updatetype = 'I';
	char buf[512], subject[256], realfrom[128], *cp;
	long startat;
	int k;

	if (!WPServer)
		return 0;

	parseheader (fp, realfrom, subject, NULLCHAR, NULLCHAR, buf, &startat);
	cp = strchr (realfrom, '@');
	if (cp)
		*cp++ = 0;
	// log (-1, "WP Updates received from %s", (cp) ? cp : "unknown source");
	fseek (fp, startat, SEEK_SET);
	if (realfrom[0] && subject[0])
	{
		while (fgets (buf, 512, fp) != NULLCHAR)
		{
			if (!strnicmp ("On ", buf, 3)) /* this is a WP Update line */
			{
				rip (buf);
				sscanf (&buf[10], "%s @ %s zip %s %s",
					call, bbs, zip, name);
				qth = buf;
				for (k = 0; k < 8; k++)
				{
					qth = strchr (qth, ' ');
					if (qth)
						qth++;
					else
						break;
				}
				if (!qth)
					continue;
				cp = strchr (call, '/');
				if (cp) {
					*cp++ = 0;
					updatetype = *cp;
				}
#if 0
				tcmdprintf ("Received WP Update [%c]: %s @ %s (%s) zip=%s qth=%s\n", updatetype, call, bbs, name, zip, qth);
#endif
				if (updatetype != 'I')
				{
					sprintf (name, "%s@%s", call, bbs);
					wpageAdd (name, 0, (updatetype == 'G') ? 0 : 1, 'X');
				}
				else wpageAdd (bbs, 1, 1, 'X');
			}
		}
	}

	return 1;
}

/* structure of WPAGES commands */

static struct cmds OLDcmds[] =
{
    { "age",            doage,          0, 0, NULLCHAR },
    { "client",         dowpclient,     0, 0, NULLCHAR },
    { "clientcall",     dowpclicall,    0, 0, NULLCHAR },
    { "destinations",   dowpdest,       0, 0, NULLCHAR },
    { "kick",           doexpkick,   2048, 0, NULLCHAR },
    { "timer",          dotimer,        0, 0, NULLCHAR },
    { "server",         dowpserver,     0, 0, NULLCHAR },
    { "support",        dowpsupport,    0, 0, NULLCHAR },
    { "temporaryage",   dotempage,      0, 0, NULLCHAR },
    { "update",         dowpupdate,     0, 0, NULLCHAR },
    { NULLCHAR,         NULL,           0, 0, NULLCHAR }
};

int dooldwpages (int argc, char **argv, void *p)
{
        return (subcmd (OLDcmds, argc, argv, (void*)0));
}

/*
 * 15Mar2012, Maiko (VE4KLM), User specific white page preference functions
 *
 *  The following options will be available when user uses 'SP' command :
 *
 *    1. Do not check for WP entry
 *
 *    2. Check for WP entry, show it, prompt if user wants it
 *
 *    3. Check for WP entry, show it, no prompt, just use it
 *
 *    4. Check for WP entry, don't show it, no prompt, just use it
 *
 */

int wpage_options (char *user, int mode)
{
	char buf[FILE_PATH_SIZE];
	FILE *fp;
	int opt;

	sprintf (buf, "%s/%s.wpp", Mailspool, user);

    if (!(fp = fopen (buf, !mode ? "r" : "w")))
		return 0;

	if (!mode)
	{
		if (!fgets (buf, sizeof(buf)-2, fp))
			opt = 0;
		else
			opt = atoi (buf);

	}
	else opt = mode;

	if (opt < 1 || opt > 4)
	{
		log (-1, "user [%s] can't read wp option, invalid or corrupt", user);
		opt = 0;
	}

	if (mode)
	{
		if (fprintf (fp, "%d", mode) != 1)
		{
			log (-1, "user [%s] can't write wp option", user);
			opt = 0;
		}
	}

	fclose (fp);

	return opt;
}

#endif	/* end of WPAGES */

