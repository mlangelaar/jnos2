/*
 * JNOS 2.0
 *
 * $Id: merge.c,v 1.1 2012/03/20 16:51:27 ve4klm Exp $
 *
 * 01Feb2012, Maiko, Taken from Lantz TNOS code - need for WPAGES feature
 */

#include "global.h"

#ifdef	WPAGES

#include "commands.h"
#include "proc.h"

/*
static char rcsid[] = "$Id: merge.c,v 1.1 2012/03/20 16:51:27 ve4klm Exp $";
 */

FILE *fopennew (const char *fname, const char *mode)
{
	char newname[256];

	sprintf (newname, "%s.new", fname);
	return (fopen (newname, mode));
}


FILE *fopentmp (const char *fname, const char *mode)
{
	char newname[256];

	sprintf (newname, "%s.tmp", fname);
	return (fopen (newname, mode));
}


int merge (const char *fname)
{
	FILE *fp, *fpnew;
	char buf[128], name[128];

	sprintf (name, "%s.new", fname);
	if ((fpnew = fopen(name, "rt")) == (FILE *)NULL)
		return 0;
	if ((fp = fopen(fname, "r+t")) == (FILE *)NULL)		{
		(void) fclose (fpnew);
		(void) rename(name, fname);
		return 1;
	}
	pwait (NULL);
	fseek (fp, 0, 2);
	while (fgets (buf, 128, fpnew))		{
		pwait (NULL);
		fputs (buf, fp);
	}
	(void) fclose (fp);
	(void) fclose (fpnew);
	(void) remove (name);
	return 1;
}

#endif	/* end of WPAGES */
