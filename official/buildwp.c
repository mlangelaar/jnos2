
#include "global.h"

#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "dirutil.h"

/*
static char rcsid[] = "$Id: buildwp.c,v 1.6 1996/08/29 12:11:16 root Exp $";
 */

char *WhitePages = "spool/wpagebbs";
#define NULLFILE (FILE *) 0
#define NULLCHAR (char *) 0
#define LINELEN 128
#define	READ_TEXT	"rt"
#define	APPEND_TEXT	"at+"

void
wpageUpdate(fp, string, entry, oldtime)
FILE *fp;
char *string, *entry, *oldtime;
{
time_t now;

	time(&now);
	fprintf(fp,"%-32s %-14ld\n",entry,now); // update time stamp
}


/* Returns a copy (strdup'ed) of existing entry if string is in whitepages file */
char *
wpageCheck(string)
char *string;
{
char buf[LINELEN], *cp, *retval = NULLCHAR;
FILE *fp;

	if (string && ((fp = fopen(WhitePages,"r+t")) != NULLFILE))	{
		while(fgets(buf, LINELEN, fp) != NULLCHAR)	{
			/* Get rid of following spaces or tabs, to separate the
			 * h_addr and it's time stamp entry
			 */
			if( ((cp=strchr(buf,' ')) != NULLCHAR) || \
				((cp=strchr(buf,'\t')) != NULLCHAR) )
					*cp = '\0';
			if(!strnicmp(string,buf, strlen(string)))	{
				retval = strdup (buf);
				fseek (fp, (long) -49, SEEK_CUR);
				wpageUpdate (fp, string, buf, &buf[33]);
				break;
			}
		}
		fclose(fp);
	}
	return retval;
}
     

void
wpageAdd (entry)
char *entry;
{
time_t now;
FILE *fp;
char *last;

	if (entry == NULLCHAR)
		return;
	last = wpageCheck (entry);
	free (last);
	if (!last)	{
		time(&now);
		if((fp = fopen(WhitePages,APPEND_TEXT)) != NULLFILE) {
			fprintf(fp,"%s %-14ld\n",entry,now); /* Save h_addr in whitepages file */
			fclose(fp);
		}
	}

}


main ()
{
FILE *tmpfile;
struct ffblk ff;
char buf[LINELEN], *cp, *cp2;
char fname[32];

	strcpy(buf,"spool/mail/*.txt");
	if (findfirst(buf, &ff, 0) == 0) {
		do	{
			sprintf (fname, "spool/mail/%s", ff.ff_name);
			if ((tmpfile = fopen (fname,READ_TEXT)) != NULLFILE)	{
				while(fgets(buf,LINELEN,tmpfile) != NULLCHAR)	{
					if(!strnicmp(buf,"R:",2)) { /*found one*/
						/* Find the '@[:]CALL.STATE.COUNTRY'or
						 * or the '?[:]CALL.STATE.COUNTRY' string
						 * The : is optional.
						 */
						if( ((cp=strchr(buf,'@')) != NULLCHAR) ||
							((cp=strchr(buf,'?')) != NULLCHAR) ) {
								if((cp2=strchr(cp,' ')) != NULLCHAR)
									*cp2 = '\0';
								if((cp2=strchr(cp,'\n')) != NULLCHAR)
									*cp2 = '\0';
								if((cp2=strchr(cp,'\t')) != NULLCHAR)
									*cp2 = '\0';
								/* Some bbs's send @bbs instead of @:bbs*/
								if (*++cp == ':')
									cp++;
							    	wpageAdd (cp);
							    	printf("%s: Adding '%s'\n", ff.ff_name, cp);
							}
					}
				}
				fclose (tmpfile);
			}

		} while (findnext(&ff) == 0);
	}
}

