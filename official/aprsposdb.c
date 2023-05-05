
#include "global.h"

#ifdef	APRSD

#include <stdio.h>

#include "files.h"

void aprs_db_add_position (char *srccall, char *data)
{
	char filename[100];

	FILE *fp;

	sprintf (filename, "%s/db/positions/%s", APRSdir, srccall);

	if ((fp = fopen (filename, "w")) != NULLFILE)
	{
		fprintf (fp, "%s %.*s", srccall, 18, data);

		fclose (fp);
	}
}

#endif


