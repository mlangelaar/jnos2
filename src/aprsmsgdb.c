
/*
 * APRS Services for JNOS 111x, TNOS 2.30 & TNOS 2.4x
 *
 * April,2001-June,2004 - Release (C-)1.16+
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 */

#include "global.h"

#ifdef	APRSD

/*
 * MSGDB.C
 *
 * Routines to manage the APRS message database
 *
 * 04Apr2004, Maiko, Now have a proper APRS message database,
 * created to accomodate the browser based Message Center.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#include "files.h"
#include "aprs.h"
#include "aprsmsgdb.h"

/* for quicker file retrieval I use a unique message id in the
 * file name. So in fact, my message database is really a directory
 * of files, rather than one big file of records. Each file in the
 * directory will have a unique message id to tell me what order
 * it came in. This will really speed up retrievals I think, and
 * keeps the code very very simple. Tailing a giant file of entries
 * for the last N records is a bit more complicated and cpu intensive
 * then simply looking for filenames that contain the last N ids.
 */

static int last_msg_idx = -1;

static void msgdb_save_index (void)
{
	char filename[100];

	FILE *fp;

	/* 27May2005, Added proper path (ie, new APRSdir variable) */
	sprintf (filename, "%s/msgdb/msgdb.idx", APRSdir);

	if ((fp = fopen (filename, "w")))
	{
		fputc (last_msg_idx, fp);
		fclose (fp);
	}
	else log (-1, "fopen (%s) failed", filename);	/* 10Mar2009, Maiko */
}

void msgdb_load_index (void)
{
	char filename[100];

	FILE *fp;

	/* 27May2005, Added proper path (ie, new APRSdir variable) */
	sprintf (filename, "%s/msgdb/msgdb.idx", APRSdir);

	fp = fopen (filename, "r");

	last_msg_idx = 333;	/* 14Apr2004, Maiko, Default 333, NOT -1 ! */

	if (fp)
	{
		last_msg_idx = getc (fp);

		aprslog (-1, "loaded %d as last index", last_msg_idx);

		fclose (fp);
	}
	else
		aprslog (-1, "no messages in the database yet");
}

int msgdb_get_last_idx (void)
{
	/* if last msg index is not set, make sure we load it from file */
	if (last_msg_idx == -1)
		msgdb_load_index ();

	return last_msg_idx;
}

/*
 * 16Apr2004, Maiko, Now put in a function, since I need
 * to use this same code to read when updating msgdb. Also
 * extra status field now in the msgdb structure.
 */
static void msgdb_get_record (FILE *fp, MSGDBREC *rec)
{
	char *ptr = rec->msg;

	fscanf (fp, "%ld %s %s %s %s", &rec->time, rec->from,
		rec->to, rec->status, ptr);

	if (aprs_debug)
	{
		aprslog (-1, "GETREC %ld %s %s %s %s", rec->time,
			rec->from, rec->to, rec->status, ptr);
	}

	/*
	 * kinda messy, but it will work. Need to create a type
	 * of fscanf that will let you include whitespace in the
	 * data. Heck, if this works, then just leave it !
	 */
	while (1)
	{
		ptr += strlen (ptr);

		*ptr++ = ' ';
		*ptr = 0;

		if (fscanf (fp, "%s", ptr) != 1)
			break;
	}

	ptr--;		/* take out any trailing space */

	*ptr = 0;
}

void msgdb_save (char *from, char *to, char *data)
{
	int orgid, incoming_msg_idx = -1;
	char filename[100], *ptr;
	MSGDBREC rec;
	FILE *fp;

	long timenow = time ((long*)0);

	/* if last msg index is not set, make sure we load it from file */
	if (last_msg_idx == -1)
		msgdb_load_index ();

	/* 05May2004, Maiko, Use this to keep filename unique with msgid */
	orgid = strcmp (logon_callsign, from);

	if (strcmp (logon_callsign, to) && orgid)
	{
		aprslog (-1, "msgdb_save - not from us or for us");
		return;
	}

	/* 14Apr2004, Maiko, if this is an ack from them to us, check
	 * for the original file that we would have saved when we sent
	 * them the message, then update the status flag to indicate
	 * that we got an acknowledgement of the message we sent.
	 */
	if (!memcmp ("ack", data, 3))
	{
		aprslog (-1, "got an ACK - update original record");

		incoming_msg_idx = atoi (data+3);

	/* 27May2005, Added proper path (ie, new APRSdir variable) */
		sprintf (filename, "%s/msgdb/msgdb.%c%d.txt",
			APRSdir, orgid ? 'U':'T', incoming_msg_idx);

		if ((fp = fopen (filename, "r")))
		{
			msgdb_get_record (fp, &rec);

			fclose (fp);

			remove (filename);

			if ((fp = fopen (filename, "w")))
			{
				if (aprs_debug)
				{
					/* 18Apr2004, Maiko, Use A(msgid) in display now */
					aprslog (-1, "WRITEREC %ld %s %s A%d %s", timenow,
						rec.from, rec.to, incoming_msg_idx, rec.msg);
				}

			/* rewrite entry with new timestamp and ack msg as status */

				fprintf (fp, "%ld %s %s A%d %s\n", timenow, rec.from,
					rec.to, incoming_msg_idx, rec.msg);

				fclose (fp);
			}	
		}
	}
	else
	{
		/* check and see if this message has a trailing msg id */
		for (ptr = data; *ptr && *ptr != '{'; ptr++);

		if (*ptr)
		{
			incoming_msg_idx = atoi (ptr+1);

			/* 27May2005, Added proper path (ie, new APRSdir variable) */
			sprintf (filename, "%s/msgdb/msgdb.%c%d.txt",
				APRSdir, orgid ? 'T':'U', incoming_msg_idx);
		}
		else
		{
			/* 27May2005, Added proper path (ie, new APRSdir variable) */
			sprintf (filename, "%s/msgdb/msgdb.%c%d.txt",
				APRSdir, orgid ? 'T':'U', last_msg_idx);
		}

		if ((fp = fopen (filename, "w")))
		{
			if (*ptr)
			{
				fprintf (fp, "%ld %s %s %d %s\n", timenow,
					from, to, incoming_msg_idx, data);
			}
			else
				fprintf (fp, "%ld %s %s - %s\n", timenow, from, to, data);

			fclose (fp);

			last_msg_idx++;

			msgdb_save_index ();	/* save it to file */
		}
	}
}

static char *msgdbtmpfile = "/tmp/msgdbfiles.txt";

static MSGDBREC msgdbrecs[20];

MSGDBREC *msgdb_tailrecs (int numrecs)
{
	char filename[100];
	FILE *fp, *fpr;
	int cnt;

	/* 27May2005, Added proper path (ie, new APRSdir variable) */
#ifdef	MOST_RECENT_FIRST
	sprintf (filename, "ls -lt %s/msgdb/*.txt | tr -s \" \" | cut -d \" \" -f9 | head -n %d > %s", APRSdir, numrecs, msgdbtmpfile);
#else
	sprintf (filename, "ls -ltr %s/msgdb/*.txt | tr -s \" \" | cut -d \" \" -f9 | tail -n %d > %s", APRSdir, numrecs, msgdbtmpfile);
#endif

	if (aprs_debug)
		aprslog (-1, "calling system with %d bytes", strlen (filename));

	system (filename);

	if ((fp = fopen (msgdbtmpfile, "r")))
	{
		for (cnt = 0; cnt < numrecs; cnt++)
		{
			fgets (filename, sizeof(filename)-2, fp);

			/* take off the CR/LF at the end !!! */
			filename[strlen(filename)-1] = 0;

			if (aprs_debug)
				aprslog (-1, "open [%s]", filename);

			msgdbrecs[cnt].time = 0L;	/* flag if record is valid */

			if ((fpr = fopen (filename, "r")))
			{
				msgdb_get_record (fpr, &msgdbrecs[cnt]);

				fclose (fpr);
			}
		}

		fclose (fp);
	}

	return msgdbrecs;
}

#endif

