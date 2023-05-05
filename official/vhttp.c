/*
 * 14Mar2006, Maiko (VE4KLM), This is an effort to make the Virtual HTTP code
 * that I originally created for the APRS webpages a bit more robust, and more
 * bullet proof. For instance, the early code leaked mbuf memory, since I was
 * not doing pullup()'s on the entire incoming buffer. Also, this function I
 * can use in multiple servers, keeping the code size down, and consistent.
 */

#include "global.h"
#include "socket.h"
#include "proc.h"

#define ISCMDLEN 32

int vhttpget (int s, char **httpdata)
{
	int len, newreq, cmdreq, retval = 0;
	char important_stuff[ISCMDLEN+1];
	char *ptr, *hptr = (char*)0;
	struct mbuf *bp;

	close_s (Curproc->output);

	Curproc->output = s;

	log (s, "vhttp connected");

	while (recv_mbuf (s, &bp, 0, NULLCHAR, NULL) > 0)
	{
		if ((len = len_p (bp)) < 10)
		{
			log (s, "vhttp truncated request");
			free_p (bp);
			break;
		}

		/* only pullup enough to extract expected cmds */
		if (len > ISCMDLEN)
			len = ISCMDLEN;

		pullup (&bp, important_stuff, len);

		free_p (bp);

		important_stuff[len] = 0;	/* terminate string !!! */

		// log (-1, "vhttp sneak peak [%s]", important_stuff);

		/*
		 * Screen out the favicon.ico and other requests which will just
		 * consume unnecessary resources from NOS - we only use '/' ...
		 */
		newreq = (memcmp (important_stuff, "GET / HTTP", 10) == 0);

		cmdreq = (memcmp (important_stuff, "GET /ifc?", 9) == 0);

		if (!newreq && !cmdreq)
		{
			log (s, "vhttp unsupported request");
			break;
		}

		if (newreq)
			log (s, "vhttp generic request");

		else
		{
			log (s, "vhttp interface specified");

			ptr = important_stuff + 9;	/* skip cmd and trailing ? char */

			/*
			 * Parse till we hit the first FIELD delimiter (&) if there is
			 * one - the 14501 form will not have that, but 45845 form will
			 * have because of it's submit button. Replace any occurances of
			 * the SPACE delimiters (+) with blanks, so that when we pass this
			 * data back to the calling function, then it can be used as is.
			 * Also, watch for a space, we don't want HTTP header data !
			 */
			while (*ptr && *ptr != '&' && *ptr != ' ')
			{
				if (*ptr == '+')
					*ptr = ' ';

				ptr++;
			}

			*ptr = 0;   /* that's it - terminate the command */

			hptr = j2strdup (important_stuff + 9);	/* pass back cmd args */
/*
			log (s, "vhttp j2strdup [%d] bytes, command [%s]",
				strlen (hptr), hptr);
*/
		}

		retval = 1;		/* tell caller that request successfully obtained */

		break;	/* force breakout !!! */
	}

	*httpdata = hptr;	/* pass the pointer to data back to caller */

	return retval;
}

