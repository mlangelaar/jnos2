
/*
 *
 * 28May2020, Maiko (VE4KLM), no more hardcoded usage() functions !
 *  (users can customize the usage files to their own needs or wants)
 *
 * I just got tired of hardcoding them. The recompiles, restarts, wasting
 * my time and energy trying to nail down exact words, enough already ...
 *
 * 25Jun2020, Maiko, Added prefix argument to getusage() function.
 *
 */

#include <stdio.h>

#define	MAXUSAGEFILEENTRYLEN 80

#ifdef	DONT_COMPILE
char *UsageFile = "/jnos/rte/usage";	/* temporary, put it in files.c later */
#endif

extern char *Usagedir;	/* defined in files.c */

extern void nos_log (int, char*, ...);

extern int tprintf (char *fmt, ...);

/*
 * 03Jun2020, Maiko, now has a return value, so the calling
 * routine can pick alternative usage method if it has one.
 *
 * 25Jun2020, Maiko, Added prefix argument, allows to provide a unique
 * help file for each subcommand of commands such as 'aprs calls ...'
 */
int getusage (char *prefix, char *item)
{
	char buffer[MAXUSAGEFILEENTRYLEN+3];	/* watch for LF, CR, EOL, terminator ? */

	FILE *fp;

	sprintf (buffer, "%s/%s/%s.txt", Usagedir, prefix, item);	/* should check length BEFORE I do this !!! */

	if (!(fp = fopen (buffer, "r")))
	{
		/* 12Apr2021, Maiko (VE4KLM), Should probably tell people if nothing shows up :] */
		tprintf ("can't open usage file [%s]\n", buffer);

		nos_log (-1, "can't open usage file [%s]", buffer);

		return 0;
	}

	while (fgets (buffer, MAXUSAGEFILEENTRYLEN, fp))
	{
		if (*buffer == '#' )
			continue;

		tprintf ("%s", buffer);
	}

 	fclose(fp);

	return 1;
}

