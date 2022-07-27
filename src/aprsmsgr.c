
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

#include <time.h>

#include "aprs.h"

#include "aprsmsgdb.h"

/* extern char *glob_url_ca; gcc does not like this, crash city ! */

extern char glob_url_ca[50];	/* GCC wants an exact definition !!! */

static char body_ca[5000], lcall[10];

/* screen colors - BLUE, YELLOW, RED */
static char *bgcolors[] = {

	"#00EEDD", "#00DDDD",
	"#EEEE00", "#DDDD00",
	"#FF1100", "#EE0000"
};

static int err_level_i = 0;
static int msgmode_i = 0;

static int allow_msgmode_i = 0;	/* 23May2004, New for demo, monitor only */

static int refresh_i = 60;	/* default refresh is 1 minute */
static int maxlines_i = 7;	/* 01May2004, Maiko, Max lines to display */
static int userf_i = 0;		/* default to internet system */
static int usemsgid_i = 1;	/* default to use message ids */

static int do_the_refresh_i = 0;

static char *checked_str = "CHECKED";
static char *space_str = " ";

/* 14Jun2004, Maiko, function to toggle allowance of message mode */
void set_amm (int mode_i)
{
	allow_msgmode_i = mode_i;
}

static int build_options_form (char *bp)
{
	char *savebp = bp;

	bp += sprintf (bp, "<HTML><HEAD>");

	bp += sprintf (bp, "<style type=\"text/css\"> BODY { font-family: Arial, Helvetica; font-size: 14px; } TABLE { font-size:12px; } </style>");

	bp += sprintf (bp, "<TITLE>Change Options</TITLE></HEAD>");

	bp += sprintf (bp, "<BODY bgcolor=\"%s\"><CENTER><br><br>", bgcolors[err_level_i]);
	bp += sprintf (bp, "<FORM action=\"%s/msg.cmd\" method=\"GET\">", glob_url_ca);
	/* 05May2004, Maiko, Combine options into ONE submit command */

	bp += sprintf (bp, "<h4>** Some Options You Can Change **</h4>\r\n");
	bp += sprintf (bp, "Refresh Interval (seconds) : <INPUT type=\"TEXT\" name=\"Opt.Refresh\" value=\"%d\" size=4>", refresh_i);
	bp += sprintf (bp, "<br><br>Maximum Lines Displayed : <INPUT type=\"TEXT\" name=\"Opt.MaxLines\" value=\"%d\" size=4>", maxlines_i);

	bp += sprintf (bp, "<br><br>Use Message IDs : <INPUT type=\"RADIO\" name=\"Opt.MsgIds\" value=\"no\" %s>no&nbsp;", usemsgid_i ? space_str : checked_str);
	bp += sprintf (bp, "<INPUT type=\"RADIO\" name=\"Opt.MsgIds\" value=\"yes\" %s>yes", usemsgid_i ? checked_str: space_str);

	bp += sprintf (bp, "<br><br>Destination : <INPUT type=\"RADIO\" name=\"Opt.Destiny\" value=\"rf\" %s>RF&nbsp;", userf_i ? checked_str : space_str);
	bp += sprintf (bp, "<INPUT type=\"RADIO\" name=\"Opt.Destiny\" value=\"internet\" %s>Internet", userf_i ? space_str : checked_str);

	bp += sprintf (bp, "<br><br><INPUT type=\"submit\" value=\"Save Changes\">");
	bp += sprintf (bp, "</FORM></CENTER></BODY></HTML>\r\n");

	return (bp - savebp);
}

static char *create_header_display (char *bp)
{
	long timenow = time ((long*)0);

	bp += sprintf (bp, "<center><strong>VE4KLM-%s Message Center</strong><br>", TASVER);

	bp += sprintf (bp, "<font size=2><i>last refreshed on %.24s</i></font></center>", ctime (&timenow));

	return bp;
}

static int build_msg_center_form (char *bp)
{
	char *savebp = bp;

	int cnt;
	
	bp += sprintf (bp, "<HTML><HEAD>");

	bp += sprintf (bp, "<style type=\"text/css\"> BODY { font-family: Arial, Helvetica; font-size: 14px; } TABLE { font-size:12px; } </style>");

/*
BODY
{
background-color: #8FFDF8;
font-family: Arial, Helvetica;
font-size: 10px;
}
TABLE
{
font-size:20px;
}
</style>
*/
	if (msgmode_i)
	{
		bp += sprintf (bp, "<TITLE>VE4KLM-%s Messaging Center - waiting for user to send message</TITLE>", TASVER);
	}
	else
	{
		bp += sprintf (bp, "<TITLE>VE4KLM-%s Messaging Center - refresh %d seconds</TITLE>", TASVER, refresh_i);

		// HTTP 4.01 says not to do the client side anymore, use the
		// server side refresh in the HTTP response header !!!
		// bp += sprintf (bp, "<meta http-equiv=\"refresh\" content=\"%d; url=%s/msg.display\">", refresh_i, glob_url_ca);
	}

	bp += sprintf (bp, "</HEAD>\r\n");
	bp += sprintf (bp, "<BODY bgcolor=\"%s\"><center>\r\n", bgcolors[err_level_i]);

	bp += sprintf (bp, "<table bgcolor=\"%s\" border=1 cellpadding=10><tr><td>", bgcolors[err_level_i+1]);

	bp = create_header_display (bp);

	bp += sprintf (bp, "</td></tr></table><br>");

	bp += sprintf (bp, "<table bgcolor=\"%s\" border=1 cellpadding=5>",
		bgcolors[err_level_i+1]);

	bp += sprintf (bp, "<tr align=\"center\"><td>Time</td><td>From</td><td>To</td><td>Status</td><td width=50>Message Text</td></tr>");

	{
		MSGDBREC *msgdbrec = msgdb_tailrecs (maxlines_i);

		for (cnt = 0; cnt < maxlines_i; msgdbrec++, cnt++)
		{
			if (!msgdbrec->time)
				continue;

			bp += sprintf (bp, "<tr><td>%s</td>", ctime (&(msgdbrec->time)));
			bp += sprintf (bp, "<td>%s</td>", msgdbrec->from);
			bp += sprintf (bp, "<td>%s</td>", msgdbrec->to);
			bp += sprintf (bp, "<td>%s</td>", msgdbrec->status);
			bp += sprintf (bp, "<td>%s</td><tr>", msgdbrec->msg);

			/* 01May2004, Maiko, Keep track of last call incase of reply */
			if (!strcmp (msgdbrec->from, logon_callsign))
				strcpy (lcall, msgdbrec->to);
			else
				strcpy (lcall, msgdbrec->from);
		}
	}

	bp += sprintf (bp, "</table><br>");

	if (msgmode_i)
	{
		bp += sprintf (bp, "<FORM action=\"%s/msg.cmd\" method=\"GET\">",
			glob_url_ca);

		bp += sprintf (bp,
				"<table><tr><td><INPUT type=\"TEXT\" name=\"Msg.To\"");

		/* 01May2004, Maiko, Now always fill in value of last station */
		bp += sprintf (bp, " value=\"%s\">", lcall);

		bp += sprintf (bp, "</td><td><INPUT type=\"TEXT\" name=\"Msg.Data\" size=63 maxlength=63></td></tr></table>");

		bp += sprintf (bp, "<br><INPUT type=\"submit\" name=\"Cmd.Send\" value=\"Send the Message\"></form>");
	}

	if (aprs_debug)
	{
		aprslog (-1, "f - glob_url_ca [%.10s], len %d",
			glob_url_ca, (bp - savebp));
	}

	/* second form */
	bp += sprintf (bp, "<FORM action=\"%s/msg.cmd\" method=\"GET\">", glob_url_ca);

	if (!msgmode_i)
	{
		/* 05Jun2004, Maiko, Use the disabled flag on the button element */
		bp += sprintf (bp, "<INPUT type=\"submit\" name=\"Cmd.ReadyToSend\" value=\"Send a Message\"");

		if (!allow_msgmode_i)
			bp += sprintf (bp, " disabled");

		bp += sprintf (bp, ">&nbsp;");

		bp += sprintf (bp, "<INPUT type=\"submit\" name=\"Cmd.ChangeOptions\" value=\"Change Options\"");

		if (!allow_msgmode_i)
			bp += sprintf (bp, " disabled");

		bp += sprintf (bp, ">&nbsp;");
	}
	else
	{
		bp += sprintf (bp, "<INPUT type=\"submit\" name=\"Cmd.Cancel\" value=\"Cancel and Return to Monitoring\">&nbsp;");
	}

	bp += sprintf (bp, "<INPUT type=\"submit\" name=\"Cmd.Refresh\" value=\"Refresh now !\"></form>");

	bp += sprintf (bp, "</center></BODY>\r\n");
	bp += sprintf (bp, "</HTML>\r\n");

	aprslog (-1, "length %d", bp - savebp);

	return (bp - savebp);
}

static char glob_Msg_To[20], glob_Msg_Data[70], glob_Cmd_What[20];

static char glob_Opt_Refresh[5];
static char glob_Opt_MaxLines[5];
static char glob_Opt_Destiny[10];
static char glob_Opt_MsgIds[5];

static void parse_msg_center_cmd (char *ptr)
{
	int matched;
	char *ptr2;

	*glob_Msg_To = 0;
	*glob_Msg_Data = 0;
	*glob_Cmd_What = 0;
	*glob_Opt_Refresh = 0;
	*glob_Opt_MaxLines = 0;
	*glob_Opt_Destiny = 0;
	*glob_Opt_MsgIds = 0;

	while (*ptr)
	{
		/*
		 * 05May2004, Maiko, Cleaned this up to match the code
		 * style used by the Options newly added today further
		 * below - this is more flexible, cleaner to work with
		 */
		if (!memcmp ("Msg.", ptr, 4))
		{
			/* strcpy (glob_Cmd_What, "Msg"); */
			matched = 1;
			ptr += 4;

			if (!memcmp ("To", ptr, 2))
			{
				ptr2 = glob_Msg_To;
				ptr += 3;
			}
			else if (!memcmp ("Data", ptr, 4))
			{
				ptr2 = glob_Msg_Data;
				ptr += 5;
			}
			else matched = 0;

			if (matched)
			{
				/* make sure to watch for end of string ! */
				while (*ptr && *ptr != '&')
					*ptr2++ = *ptr++;

				*ptr2 = 0;
			}
			else ptr++;
		}
		else if (!memcmp ("Cmd.", ptr, 4))
		{
			ptr2 = glob_Cmd_What;
			ptr += 4;

			while (*ptr != '&' && *ptr != '=')
				*ptr2++ = *ptr++;

			*ptr2 = 0;
		}
		/*
		 * 05May2004, Maiko, New way to process options !!!
		 */
		else if (!memcmp ("Opt.", ptr, 4))
		{
			strcpy (glob_Cmd_What, "SetOptions");
			matched = 1;
			ptr += 4;

			if (!memcmp ("Refresh", ptr, 7))
			{
				ptr2 = glob_Opt_Refresh;
				ptr += 8;
			}
			else if (!memcmp ("MaxLines", ptr, 8))
			{
				ptr2 = glob_Opt_MaxLines;
				ptr += 9;
			}
			else if (!memcmp ("Destiny", ptr, 7))
			{
				ptr2 = glob_Opt_Destiny;
				ptr += 8;
			}
			else if (!memcmp ("MsgIds", ptr, 6))
			{
				ptr2 = glob_Opt_MsgIds;
				ptr += 7;
			}
			else matched = 0;

			if (matched)
			{
				/* make sure to watch for end of string ! */
				while (*ptr && *ptr != '&')
					*ptr2++ = *ptr++;

				*ptr2 = 0;
			}
			else ptr++;
		}
		else ptr++;
	}
}

static void url_decode_data (char *ptr)
{
	unsigned char high, low;

	char *outptr = ptr;

	while (*ptr)
	{
		if (*ptr == '+')
			*outptr = ' ';

		else if (*ptr == '%')
		{
			ptr++;
			if (*ptr >= 'A' && *ptr <= 'F')
				high = *ptr - 'A' + 10;
			else if (*ptr >= '0' && *ptr <= '9')
				high = *ptr - '0';
			ptr++;
			if (*ptr >= 'A' && *ptr <= 'F')
				low = *ptr - 'A' + 10;
			else if (*ptr >= '0' && *ptr <= '9')
				low = *ptr - '0';

			*outptr = high * 16 + low;
		}
		else
			*outptr = *ptr;

		outptr++;
		ptr++;
	}

	*outptr = 0;	/* make sure we terminate this */
}

int build_response (char *data, char *URL_sp)
{
	char ackmsgid[7], *ptr = URL_sp, *bp = data;

	int body_len_i;

	/* create BODY ahead of time, so we know the content length */

	/* first check for GET action requests */

	do_the_refresh_i = 1;	/* default is to always have browser refresh */

	msgmode_i = 0;	/* default is to monitor */

	if (aprs_debug)
		aprslog (-1, "checking url [%.40s]", ptr);

	if (memcmp (ptr, "/msg.cmd?", 9) == 0)
	{
		ptr += 9;

		parse_msg_center_cmd (ptr);

		if (aprs_debug)
		{
			aprslog (-1, "TO [%s] DATA [%s] CMD [%s]",
				glob_Msg_To, glob_Msg_Data, glob_Cmd_What);

			aprslog (-1, "REFRESH [%s] MAXLINES [%s]",
				glob_Opt_Refresh, glob_Opt_MaxLines);

			aprslog (-1, "DESTINY [%s] MSGIDS [%s]",
				glob_Opt_Destiny, glob_Opt_MsgIds);
		}

		/* 06May2004, Maiko, Final options for now */
		if (!memcmp (glob_Cmd_What, "SetOptions", 10))
		{
			refresh_i = atoi (glob_Opt_Refresh);

			maxlines_i = atoi (glob_Opt_MaxLines);

		/* 15Jun2004, Maiko, Cap the value to 7 for development copy */
			if (maxlines_i < 1 || maxlines_i > 7)
				maxlines_i = 7;

			if (!strcmp ("rf", glob_Opt_Destiny))
				userf_i = 1;
			else
				userf_i = 0;

			if (!strcmp ("no", glob_Opt_MsgIds))
				usemsgid_i = 0;
			else
				usemsgid_i = 1;
		}
		else if (!memcmp (glob_Cmd_What, "ReadyToSend", 11))
		{
			do_the_refresh_i = 0;	/* browser should not refresh for this */

			msgmode_i = 1;	/* setup up response to prompt for message */
		}

		else if (!memcmp (glob_Cmd_What, "Send", 4))
		{
			/* 08Apr2004, Maiko, Browser data needs to be URL decoded !!! */
			url_decode_data (glob_Msg_Data);

			/* 12Apr2004, Maiko, Only call if form is filled in properly */
			/* 15Apr2016, Maiko (VE4KLM), Oops, believe this should be looking
			 * at the contents of the glob_XXX variables, not the values of the
			 * variables themselves, oh boy, it's been like that for 12 years.
			if (glob_Msg_To && glob_Msg_Data)
			 */

			if (*glob_Msg_To && *glob_Msg_Data)
			{
				if (aprs_debug)
					aprslog (-1, "URL DECODE [%s]", glob_Msg_Data);

				/* 14Apr2004, Maiko, concatonate msg id before sending */
				/* 18Apr2004, Maiko, Do not assign for queries ! */
				/* 06May2004, Maiko, Now can use or not use msg ids */
				if (usemsgid_i && *glob_Msg_Data != '?')
				{
					sprintf (ackmsgid, "{%d", msgdb_get_last_idx ());

					strcat (glob_Msg_Data, ackmsgid);
				}

				/* 07Apr2004, Maiko, Now we can msg - 1rst milestone */
				/* 06May2004, Maiko, Now we can go rf or internet system */
				send45845msg (userf_i, logon_callsign, glob_Msg_To,
					glob_Msg_Data);
			}
			else
				aprslog (-1, "abort - recipient and/or message not filled in");
		}

		if (!memcmp (glob_Cmd_What, "ChangeOptions", 13))
		{
			body_len_i = build_options_form (body_ca);

			do_the_refresh_i = 0;	/* browser should not refresh for this */
		}
		else
			body_len_i = build_msg_center_form (body_ca);
	}
	else	/* default response */
	{
		body_len_i = build_msg_center_form (body_ca);
	}

	/* write the HEADER record */

	bp += sprintf (bp, "HTTP/1.1 200 OK\r\n");
	bp += sprintf (bp, "Server: VE4KLM-%s\r\n", TASVER);

	bp += sprintf (bp, "Cache-Control: no-cache\r\n");

	// HTTP 4.01 says refresh should be server side in the header,
	// not on the client side anymore (previously using META tags).
	if (do_the_refresh_i)
		bp += sprintf (bp, "Refresh: %d; URL='%s'\r\n",
				refresh_i, glob_url_ca);

	bp += sprintf (bp, "Content-type: text/html\r\n");
	bp += sprintf (bp, "Content-length: %d\r\n", body_len_i);

	/* use a BLANK line to separate the BODY from the HEADER */
	bp += sprintf (bp, "\r\n");

	/* now write the BODY of the page */
	bp += sprintf (bp, "%s", body_ca);

	if (aprs_debug)
		aprslog (-1, "body len %d total len %d", body_len_i, strlen (data));

	/* make sure I set the length to be written out */
	return strlen (data);
}

#endif

