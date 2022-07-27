/* dumpdate - display /spool/{user.dat,history} info
   Usage:  dumpdate [-v] [path_to_file] [hamcall ...]

   Written 4-9-93 by N5KNX for JNOS 1.08d.  Updated 1-22-95 for 1.10h.
   See mboxcmd.c, routines updatedefaults() and loguser().
   Intended for USERS.DAT, but works for HISTORY file too.
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#ifdef UNIX
#define stricmp strcasecmp
#endif

#define NULLFILE	(FILE *)0
#define NULLCHAR	(char *)0

extern int errno;
char UDefaults[64] = "/spool/users.dat";

int main (int argc, char *argv[])
{
    FILE *Ufile = NULLFILE;
    time_t t;
    int i, found=0, verbose=0;
    char *cp, *cp2, *dt;
    char buf[256];

    while (argc>1) {
	if (strcmp(argv[1], "-v") == 0) verbose++;
	else if (Ufile == NULLFILE) {		
		if((Ufile = fopen(argv[1],"r")) == NULLFILE) break;	/* must be call */
    		strncpy(UDefaults, argv[1], sizeof (UDefaults));
	}
	else break;
    	argc--; argv++;
    }

    if (Ufile == NULLFILE)
	if((Ufile = fopen(UDefaults,"r")) == NULLFILE) {
		perror(UDefaults);
		return(errno);
        }

    while(fgets(buf,sizeof(buf),Ufile) != NULLCHAR) {        /* Find user(s) in the default file */
	if((cp=(char *)strchr(buf,' ')) == NULLCHAR) continue;  /* bad syntax */
	*cp++ = '\0';	/* terminate callsign */

	if (argc>1) {	/* compare the name(s) */
		for (i=1; i<argc; i++)
			if(!stricmp(argv[i],buf)) break;  /* found match */
		if (i >= argc) continue;	/* no match, bypass this line in default file */
	}

	found++;
	t = (time_t)atol(cp);	/* convert time */
	dt = (t==0 ? "(never/disconnected)" : ctime(&t));
	printf ("%s\t%.24s",buf, dt);	/* Callsign TAB LastOnTime */

        if (verbose)
	  while(*cp != '\0') {
        	while(*cp == ' ') cp++;  /*skip blanks*/

		switch(*cp){
		case 'C':	/* ALWAYS the last user option flag */
                            cp++;
                            if (*cp == 'T') printf(" via_Telnet");
                            else if (*cp == 'N') printf(" via_NetRom");
                            else if (*cp == 'A') printf (" via_AX.25");
                            break;
                case 'M':
                            cp++;
                            i = atoi(cp);
                            printf(" More=%d", i);
                            break;
                case 'A':
			    printf(" AreaPrompt");
                            break;
                case 'X':
                            printf(" Xpert");
                            break;
                case 'N':
                            printf(" NetromPrompt");
                            break;
                case 'P':
                            printf(" LineMode");
                            break;
		case 'R':
			    printf(" ReplyTo");
			    break;
		case '-':		/* Registered user (NEVER the last flag */
			    cp2=++cp;
			    while(*cp && *cp != ' ') cp++;
			    *cp = '\0';
			    switch (*cp2++) {
				case 'h':	if (*cp2) printf (" Hbbs=%s", cp2); break;
				case 'e':	if (*cp2) printf (" ReplyTo=%s", cp2); break;
				case 'n':	if (*cp2) printf (" Name=%s", cp2); break;
			    }
			    *cp = ' ';  /* always a next option */
                }
                if (*cp) cp++;
	}
	printf("\n");
    }	/* ends while(fgets()) */

  fclose(Ufile);
  return (!found);
}
