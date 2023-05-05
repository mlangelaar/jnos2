/*
 * JNOS 2.0
 *
 * $Id: sort.c,v 1.2 2012/04/26 13:47:01 ve4klm Exp $
 *
 * 02Feb2012, Maiko (VE4KLM), From Lantz TNOS code - need for WPAGES feature
 *
 * 23Apr2012, Maiko, Serious bug fixed (see notes further down) where JNOS
 * was crashing on some systems but not others - array OOB conditions !
 *
 */

#include "global.h"

#ifdef	WPAGES

#include "commands.h"
#include "unix.h"
#include "proc.h"

#include <time.h>
#include <stdlib.h>

/*
static char rcsid[] = "$Id: sort.c,v 1.2 2012/04/26 13:47:01 ve4klm Exp $";
 */

#define ENDING ((size_t) -1)

int j2wp_expired; /* 23Apr2012, Maiko, Renamed to something more unique */

struct entries {
	char *name;
	long theindex;
	size_t nextindex;
};

int glob_searchsize;

static struct entries *e = ((struct entries *)0);
static int count;
static size_t current;
static char name[16];
static int result, same;
static size_t theindex;
static long currentindex;
static int sortdups = 0, wassorted = 0;
static size_t sorthead;
static size_t lastindex;

static struct entries *temp = (struct entries *) 0;
static struct entries *temp2 = (struct entries *) 0;

static void search (void);
static void insert (void);
static void makesortname (char *buf, const char *origname);

/*
 * 23Apr2012, After running for several days on KQ6UP (Chris) site, which I was
 * using as a sandbox, the crashes that a few people were reporting when ever a
 * 'wpage kick' was done, *seems* to be resolved. This took a couple of weeks
 * to fix - actually caused me great stress - bottom line was that 'entrysize'
 * (once you got into the sort function) was 1 byte too big, caused too little
 * memory to be allocated, but in order for fgets() to read an entry you have
 * to tell it to read 1 more then what you want. Anyways, the fix was to change
 * the 17 offset (passed to sortit from within wpages.c) to 16, and AFTER one
 * allocates memory, bump it up again so that fgets() works.
 *
 * This *bug* still exists in the original TNOS source.
 *
 * This whole thing had to do with Array Out Of Bound conditions, something
 * the earlier glibc and compilers seemed to be very forgiving of, while the
 * latest GCC 4.5.2 on Slackware 13.37 makes it jump out (crash) very fast.
 *
 * After doing some reading, I guess it was my turn to be the unlucky guy
 * who's system functioned perfectly fine, while others didn't. That's just
 * the nature of the array OOB beast when you program in the 'C' language,
 * still bites you even after doing this for 20 years or more ...
 */

static long global_size = 0;

int oob (char *desc, long item)
{
	if (item < 0 || item >= global_size)
	{
		log (-1, "%s (%ld) OOB !!!", desc, item);
		return 1;
	}

	return 0;
}

static void search ()
{
	same = 0;
	lastindex = ENDING;
	for (theindex = sorthead; theindex != ENDING; theindex = e[theindex].nextindex)		{
		if (oob ("search", theindex))
			return;

		result = strnicmp (name, e[theindex].name, strlen(name));
		if (!result)	{
			same = 1;
			sortdups++;
			return;
		}
		if (result < 0)
			return;
		lastindex = theindex;
	}
	return;
}

static void insert ()
{
	if (oob ("insert", current))
		return;
	temp = &e[current];
	if (lastindex == ENDING)	{	/* at head of list */
		temp->nextindex = sorthead;
		sorthead = current;
	} else 	{	/* in the midst or tail of file */
		if (oob ("insert2", lastindex))
			return;
		temp2 = &e[lastindex];
		if (theindex)		/* in midst of list */
			temp->nextindex = temp2->nextindex;
		temp2->nextindex = current;
		if (!wassorted && !same && !theindex)
			wassorted = 1;
	}

	if (strlen (name) > glob_searchsize)
	{
		log (-1, "insert3 name [%.*s] !!!", glob_searchsize, name); 
		return;
	}
	strcpy (temp->name, name);

	if (!same)
		temp->theindex = currentindex;
	else
		temp->theindex = -1;
	current++;
	count++;
}

static void makesortname (char *buf, const char *origname)
{
	char *cp;

	strcpy (buf, origname);
	cp = strchr (buf, '.');
	if (!cp)
		cp = &buf[strlen(buf)];
	strcpy (cp, ".srt");
}

void sortit (const char *fname, int entrysize,
		int searchsize, int strsize, time_t date)
{
	char *cp;
	unsigned int k;
	FILE *fp, *out;
	char buf[128];
	long size0, size;
	time_t now, stamptime;
	int newnum = 0;

	struct entries *ptr;

	if ((fp = fopen (fname, "rt")) == 0)
		return;

	makesortname (buf, fname);

	if ((out = fopen (buf, "wt")) == 0)
	{
		fclose (fp);
		return;
	}
	size0 = (long) filelength(fileno(fp));
	size = size0 / (long) entrysize;

	log (-1,"Sort '%s' - %ld Entries originally", fname, size);

	size += 1;	/* safety buffer */

	global_size = size;

	glob_searchsize = searchsize;

	entrysize++;	/* in order for fgets to get the NL bump this up */

	e = malloc (size * sizeof(struct entries));

	if (e == 0)
	{
		log (-1, "no memory, errno %d", errno);
		return;
	}

	for (ptr = e, k = 0; k < (unsigned int)size; k++, ptr++)
	{
		ptr->name = malloc (searchsize + 1);
		memset (ptr->name, 0, searchsize + 1);
		ptr->nextindex = ENDING;
		ptr->theindex = -1;
//		pwait (NULL);
	}

	sorthead = ENDING;
	current = count = 0;
	j2wp_expired = sortdups = wassorted = 0;
	now = time(&now);
	while (!feof (fp))
	{
		pwait (NULL);
		currentindex = ftell(fp);
		if ((long)current == size)
			break;

		fgets (buf, entrysize, fp);
		/* log (-1, "sort %s", buf); */

		if (feof (fp))
			continue;

		pwait (NULL);

		if (*buf > ' ' && ((int) strlen(buf) > (entrysize - 2))
			&& (*buf != '#'))
		{
			strncpy (name, buf, searchsize);
			name[searchsize] = 0;
			if ((cp = strpbrk (name, ".@ \t")) != 0)
				*cp = 0;
			search();
			pwait (NULL);
			insert ();
			pwait (NULL);
		} else	{	/* skip it! */

			log (-1, "skip [%.10s]", buf);

			current++;
			count++;
			continue;
		}
		/* now check for expired date stamp, if used */
		if (date)	{
			cp = strchr (buf, ' ');
			if (!cp)	{
				stamptime = 0;
			} else {
				cp = skipwhite (cp);
				stamptime = atol(cp);
			}
			if ((stamptime == 0) || (now - stamptime >= date))
			{
				j2wp_expired++;

				if (oob ("sortit", current))
					break;
 
				e[current].theindex = -2;
			}
		}
	}
	fflush (stdout);
	if (j2wp_expired || wassorted || sortdups)		{
		for (theindex = sorthead,k=0; k < (unsigned int)count && theindex != ENDING; k++,theindex = temp->nextindex)
		{
			if (oob ("sortit2", theindex))
				break;
			temp = &e[theindex];
			if (temp->theindex >= 0)	{
				pwait (NULL);
				fseek (fp, (long) temp->theindex, 0);
				fgets (buf, entrysize, fp);

				if (strsize)	{

					/* we now re-format it, just in case it is bad */
					if ((cp = strpbrk (buf, " \t")) == 0)
						continue;

					*cp++ = 0;
					cp = skipwhite (cp);
					stamptime = atol(cp);
					buf[strsize] = 0;	/* just in case */
					cp = buf;
					while (*cp)	{
						if (*cp & 0x80)	{
							*cp = 0;
							break;
						}
						cp++;
					}
					if (stamptime && *buf)
				                fprintf(out,"%-*s %-14ld\n",strsize,buf,stamptime);
			        } else
					fputs (buf, out);
				newnum++;
			}
		}
	}
	log(-1,"Sort '%s' - %d Entries at end", fname, newnum);
	j2pause (1000);
	fclose (fp);
	fclose (out);
	j2pause (1000);

	for (ptr = e, k = 0; k < (unsigned int)size; k++, ptr++)
		free (ptr->name);

	if (e)
	{
		free (e);

		e = (struct entries*)0;
	}

	makesortname (buf, fname);
	if (j2wp_expired || wassorted || sortdups)	{
		(void) remove (fname);
		(void) rename (buf, fname);
	} else
		(void) remove (buf);
}

#endif	/* end of WPAGES */
