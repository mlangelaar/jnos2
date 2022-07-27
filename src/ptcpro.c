/*
 * These are HFDD routines for the SCS PTC modem. Now that I got the KAM
 * support working quite well, it's time to go back to the SCS code I worked
 * on back in early 2005 and get it structured the same way as the KAM code.
 *
 * For Amateur radio use only (please). Not for profit !
 *
 * $Id: ptcpro.c,v 1.3 2010/11/04 15:58:49 ve4klm Exp $
 *
 */

#include "global.h"

#ifdef	HFDD

#include "mbuf.h"
#include "proc.h"
#include "iface.h"

#ifdef UNIX
#include "unixasy.h"
#else
#include "i8250.h"
#endif

#include "asy.h"
#include "tty.h"
#include "session.h"
#include "socket.h"
#include "commands.h"
#include "devparam.h"
#include "usock.h"

#include "hfdd.h"	/* 21Jan2005, Maiko */

static int ptc_disc_flag = 0;

/* 16Apr2007, Maiko (VE4KLM), New functionality to match KAM structure */
static char ptc_conn_call[30];
static int ptc_conn_flag = 0;
static int ptc_fec_flag = 0;
static int ptc_we_called = 0;

extern int hfdd_conn_flag;	/* 18Jan2005, Maiko, Now in hfddrtns module */

static int mbxs[2] = { -1, -1 };

static int gchannel = 0;

extern int hfdd_fwdsock;	/* 18Jan2005, Now in hfddrtns module */

/* 02Feb2005, Maiko, New for common tnc_out module */
void set_ptc_disc_flag ()
{
	ptc_disc_flag = 1;
}

/* 16Apr2007, Maiko (VE4KLM), New functions matching KAM structure */
/* 17Feb2007, Maiko, new function to request outgoing connect */
void ptc_FEC ()
{
        ptc_fec_flag = 1;
}

/* 17Feb2007, Maiko, new function to request outgoing connect */
void ptc_make_call (char *callsign)
{
        strcpy (ptc_conn_call, callsign);
        ptc_conn_flag = 1;
}

/* 15May2007, Maiko, a handy utility when tnc hangs on us */
void ptc_exit_hostmode (int dev)
{
	char command[10], *ptr = command;

	*ptr++ = 0x04;	/* channel */
	*ptr++ = 0x01;	/* info = 0, cmd = 1 */
	*ptr++ = 0x05;	/* length - 1 */

	ptr += sprintf (ptr, "JHOST0");

	hfdd_send (dev, command, 9);
}

void ptc_changeover (int dev, char *command, int force)
{
	char *ptr = command;

	*ptr++ = 0x04;	/* channel */
	*ptr++ = 0x01;	/* info = 0, cmd = 1 */
	*ptr++ = 0x01;	/* length - 1 */
	*ptr++ = '%';

	log (-1, "%s PTC changeover", force ? "forced" : "normal");
	if (force)
		*ptr++ = 'I';	/* break in */
	else
		*ptr++ = 'O';

	hfdd_send (dev, command, 5);
}

static void ptc_disconnect (int dev, char *command)
{
	char *ptr = command;

	*ptr++ = 0x04;	/* channel */
	*ptr++ = 0x01;	/* info = 0, cmd = 1 */
	*ptr++ = 0x00;	/* length - 1 */
	*ptr++ = 'D';

	hfdd_send (dev, command, 4);
}

static void ptc_connect (int dev, char *command, char *callsign)
{
	char *ptr = command;

	*ptr++ = 0x04;	/* channel */

	*ptr++ = 0x01;	/* info = 0, cmd = 1 */

	*ptr++ = (3 + strlen (callsign));	/* length - 1 */

	ptr += sprintf (ptr, "C %s", callsign);

	hfdd_send (dev, command, 7 + strlen (callsign));
}

void ptc_send_data (int dev, char *data, int len)
{ 
	char command[300], *ptr = command;

	*ptr++ = 0x04;	/* channel */

	*ptr++ = 0x00;	/* info = 0, cmd = 1 */

	*ptr++ = len - 1;	/* length - 1 */

	memcpy (ptr, data, len);

	hfdd_send (dev, command, 3 + len);
}

/* 25Jan2005, Maiko, Now put into a function, used multiple places */
static void ptc_get_status (int dev, char *command)
{
	char *ptr = command;

	int datalen = 4;

	/* 26Feb2005, Maiko, Check PTC status (traffic led, etc) */
	if (gchannel > 4)
		gchannel = 0xfe;

	*ptr++ = gchannel;	/* channel */

	*ptr++ = 0x01;	/* info = 0, cmd = 1 */

	if (gchannel == 0xfe)
		*ptr++ = 0x01;	/* length - 1 */
	else
		*ptr++ = 0x00;	/* length - 1 */

	*ptr++ = 'G';	/* command */

	/* 26Feb2005, Maiko, Check PTC status (3 of the 4 status bytes) */
	if (gchannel == 0xfe)
	{
		*ptr++ = '2';
		datalen++;
		gchannel = 0;
	}
	else
		gchannel++;

	hfdd_send (dev, command, datalen);
}

static void ptc_dump (int len, unsigned char *ptr)
{
	static char dumpbuffer[300];

	char *sptr = dumpbuffer;

	int cnt = 0;

	/* 20May07, Maiko, Don't dump the tons of status messages */
	if (!hfdd_debug || len < 7) return;

	*sptr = 0;	/* start with NULL string */

	while (cnt < len)
	{
		if (!isprint (*ptr))
			sptr += sprintf (sptr, "[%02x]", *ptr);
		else
			*sptr++ = *ptr;
		cnt++;

		ptr++;
	}

	*sptr = 0;

	log (-1, "DATA (%d) [%s]", len, dumpbuffer);
}

void ptc_machine (int dev, char *callsign)
{
	static unsigned char *ptr, command[300], response[300];

	register int c, len = 0;

	ptc_disc_flag = 0;
	hfdd_conn_flag = 0;

	gchannel = 0;

	while (1)
	{
		/*
		 * Before we bug the controller, let JNOS have a chance to do
		 * the other things it needs to do, and run the other drivers
		 * that it needs to process, etc, etc ...
		 */
		pwait (0);

		/* MASTER - Ask the Controller for it's information */

		if (ptc_conn_flag)
		{
			ptc_connect (dev, (char*)command, ptc_conn_call);
			ptc_conn_flag = 0;
			ptc_we_called = 1;
		}
		else if (ptc_disc_flag)
		{
			ptc_disconnect (dev, (char*)command);
			ptc_disc_flag = 0;
		}
		else
		{
			ptc_get_status (dev, (char*)command);
		}

		/*
		 * SLAVE - Read in the Controller information. According to the
		 * documentation, the standard is 250 ms, but the PTC will respond
		 * in a matter of several ms ! For development purposes I will leave
		 * it set to 100 ms, until this is done, then it can be tweaked !
		 */

		ptr = response;

		j2alarm (100);
		while ((c = get_asy (dev)) != -1)
			*ptr++ = c & 0xff;
		j2alarm (0);

		len = (int)(ptr - response);

		/* Now figure out what to do with the information */

		if (len)
		{
			int channel, code;

			ptr = response;

			channel = *ptr++;

			/* 24May07, Maiko, Plug and Play - init modem if not in hostmode */
			if (channel > 4 && channel < 254)
			{
				hfdd_init_tnc (dev);
				continue;
			}

			code = *ptr++;

			/* 20May07, Maiko, Moved into here ! */
			if (code)
				ptc_dump (len, response);

			if (code == 1) /* 1 - success, message follows */
			{
				if (memcmp ("CHANNEL NOT", ptr, 11) == 0)
				{
					hfdd_conn_flag = -1;
				}
			}
			else if (code == 3)	/* link status */
			{
				if (memcmp ("CONN", ptr + 4, 4) == 0)
				{
					if (!ptc_we_called)
					{
						hfdd_mbox (dev, mbxs, (void*)TTY_LINK,
							(void*)(ptr + 17));
					}

					hfdd_conn_flag = 1;
				}
				/*
				 * 27Apr07, Maiko, If we are NOT connected, it really does
				 * not matter why, we need to indicate that we are DISC !
				 */
				else hfdd_conn_flag = -1;
			}
			else if (code == 6 || code == 7)
			{
				len = *ptr;
				len++;
				ptr++;

				/* 26Feb2005, Maiko, Check PTC status (traffic led, etc) */
				if (channel == 254)
				{
					static unsigned char lastbusy = 255;
					static unsigned char lastconl = 255;
					static unsigned char lastcons = 255;

					unsigned char statbyte;

					int cnt;

					for (cnt = 0; cnt < len; cnt++, ptr++)
					{
						statbyte = *ptr;

						if (cnt == 0)
						{
							if (statbyte == 247)
							{	
								if (lastbusy != statbyte)
								{
									log (-1, "PTC - channel busy");
									lastbusy = statbyte;
								}
							}
							/* 20May07, Maiko, Too many channel clear msgs */
							else
								lastbusy = 255;

							/* 20May07, Maiko, oops should be 0x08, not 0x04 */
							if (statbyte & 0x08)
							{
								if (!hfdd_iss ())
									log (-1, "PTC - we are ISS");

								set_hfdd_iss ();
							}
							else
							{
								if (!hfdd_irs ())
									log (-1, "PTC - we are IRS");

								set_hfdd_irs ();
							}
						}
						else if (cnt == 1)
						{
							if (lastconl != statbyte)
							{
								if (!statbyte)
									log (-1, "no pactor connection");
								else
									log (-1, "pactor %d connection",
										(int)statbyte);

								lastconl = statbyte;
							}
						}
						else if (cnt == 2)
						{
							if (lastcons != statbyte)
							{
								log (-1, "pactor speed level %d",
									(int)statbyte);

								lastcons = statbyte;
							}
						}
					}
			/*
			 * Byte 2: PACTOR connect level (0 is not connected)
			 * 1: PACTOR-I,
			 * 2: PACTOR-II,
			 * 3: PACTOR-III.
			 *
			 * Byte 3: Speedlevel (submode of a PACTOR level)
			 * 0-1 on PACTOR-I,
			 * 0-3 on PACTOR-II,
			 * 0-5 on PACTOR-III.
			 */
					continue;
				}

				if (code == 7)
				{
					int scssoc = mbxs[0];

					if (hfdd_fwdsock != -1)
						scssoc = hfdd_fwdsock;

					while (len > 0)
					{
		            	usputc (scssoc, (char)(*ptr));
						ptr++;

						usflush (scssoc);

						len--;
					}
				}
			}
		}
	}
}

#endif	/* end of HFDD */

