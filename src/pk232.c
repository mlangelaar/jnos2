/*
 * Main PK232 hostmode code, designed/written by Maiko Langelaar (VE4KLM)
 *
 * September 9, 2008 - The first decent working prototype, bidirectional
 *                     FBB and B2F forwarding working like a charm, using
 *                     the Airmail software + DXP38 as a pactor client.
 *
 * This driver is using a new queue method of delivering data to the HF
 * modem, as opposed to the other drivers that have asy_send() calls all
 * over the place. Required a new module 'hfddq.c' for queue routines.
 *
 * For Amateur radio use only (please). Not for profit !
 *
 */

#include "global.h"

#include <string.h>

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

#ifndef	SOH
#define	SOH 0x01
#endif

#define DLE 0x10
#define ETB 0x17

int wait_4_ack = 0;	/* 30May08, Maiko, New _XX flag */
int wait_4_echo = 0;	/* 03Sep08, Maiko, should we wait for echo 2 snd mor */
int echo_bytes = 0;	/* 21Sep08, Maiko, Make sure ALL bytes are echoed */
int chk_4_irs = 0;	/* 26Sep08, Maiko, post changeover not always taking */

static int pk232_we_called = 0;	/* 23Feb2007, New */

extern int hfdd_conn_flag;	/* 18Jan2005, Maiko, Now in hfddrtns module */

extern int hfdd_fwdsock;	/* 18Jan2005, Now in hfddrtns module */

static int mbxs[2] = { -1, -1 };

void pk232_exit_hostmode (int dev)
{
	char command[7];

	sprintf (command, "%cOHON%c", SOH, ETB);

	NEW_hfdd_send (dev, command, 6);
}

/*
 * 12Sep2008, Maiko (VE4KLM), Poll OPMODE to find out whether
 * we are IRS or ISS, and possible other info. This is never
 * a priority, and only sent when nothing else has to be.
 */
void pk232_opmode_poll (int dev)
{
	char command[6];

	sprintf (command, "%c%cOP%c", SOH, 0x40, ETB);

	NEW_hfdd_send (dev, command, 5);
}

void pk232_data_poll (int dev, int priority)
{
	char command[6];

	sprintf (command, "%c%cGG%c", SOH, 0x40, ETB);

	if (priority)
		priority_send (dev, command, 5);
	else
		NEW_hfdd_send (dev, command, 5);
}

void pk232_changeover (int dev, char *command, int force)
{
	if (force)
	{
//		log (-1, "force changeover");

		sprintf (command, "%c%cAG%c", SOH, 0x40, ETB);
	}
	else
	{
//		log (-1, "signal changeover");

		sprintf (command, "%c%cOV%c", SOH, 0x40, ETB);
	}

	NEW_hfdd_send (dev, command, 5);
}

static char Hcommand[300];

static int16 dump_hash, last_dump_hash = 0, last_dump_cnt = 0;	/* 27Aug08 */

extern int16 dhash (unsigned char*);

void pk232_dump (int len, unsigned char *ptr)
{
	static char dumpbuffer[300];

	char *sptr = dumpbuffer;

	int cnt = 0;

	if (!hfdd_debug || !len) return;

	*sptr = 0;	/* start with NULL string */

	while (cnt < len)
	{
		if (*ptr == SOH)
			sptr += sprintf (sptr, "<SOH>");
		else if (*ptr == ETB)
			sptr += sprintf (sptr, "<ETB>");
		else if (*ptr == DLE)
			sptr += sprintf (sptr, "<DLE>");
		else if (*ptr == 0)
			sptr += sprintf (sptr, "<NULL>");
		else if (!isprint (*ptr))
			sptr += sprintf (sptr, "[%02x]", *ptr);
		else
			*sptr++ = *ptr;
		cnt++;
		ptr++;
	}

	*sptr = 0;

#ifdef	DONT_COMPILE
	if (last_dump_hash && last_dump_cnt == 10)
	{
		log (-1, "pk232_dump - prev msg repeated %d times", last_dump_cnt);
		last_dump_cnt = 0;
	}
#endif

/*
 * 27Aug08, Maiko, Only display if dump content changes, saves space, easier
 * to read the log file. At least I say how many times the message showed up.
 */
	dump_hash = dhash ((unsigned char*)dumpbuffer);

	if (dump_hash != last_dump_hash)
	{
		log (-1, "pk232_dump - (%d) [%s]", len, dumpbuffer);

		last_dump_hash = dump_hash;

		last_dump_cnt = 0;
	}
	else last_dump_cnt++;
}

void pk232_send_data (int dev, char *data, int len)
{ 
	Hcommand[0] = SOH;

	Hcommand[1] = 0x20;

	memcpy (Hcommand + 2, data, len);

	Hcommand[2 + len] = ETB;

//	log (-1, "pk232_send_data (%d) [%.*s]", len, len, data);

	NEW_hfdd_send (dev, Hcommand, 3 + len);
}

/*
static void pk232_pactor_standbye (int dev, unsigned char *command)
{
	sprintf (command, "%c%cPT%c", SOH, 0x40, ETB);

	NEW_hfdd_send (dev, command, 5);
}
*/

static unsigned char response[300];

/* 20Sep2008, Maiko, Function to send FEC (unproto) */
void pk232_FEC (int dev)
{
	char command[70], tmp[AXBUF];

	char *bcall = pax25 (tmp, Bbscall);

	/* now try and establish the HF connection */
	sprintf (command, "%c%cPD%c", SOH, 0x40, ETB);
	NEW_hfdd_send (dev, command, 5);

	sprintf (command, "CQ CQ CQ DE %s %s %s\r", bcall, bcall, bcall);
	pk232_send_data (dev, command, strlen (command));

	sprintf (command, "%c%cRC%c", SOH, 0x40, ETB);
	NEW_hfdd_send (dev, command, 5);
}

/* 10Sep2008, Maiko, New queue system, no more use of flags like
 * the other modem drivers, direct call now, parameters changed since
 * the 'dev' is not known within hfddrtns.c. pk232_make_call() is no
 * longer in existence since we don't use flags anymore for this.
 *
static void pk232_connect (int dev, char *command, char *callsign)
 *
 */
void pk232_connect (char *ifname, char *callsign)
{
	char command[20];

	struct iface *ifp;

	if ((ifp = if_lookup (ifname)) == NULLIF)
	{
		tprintf (Badinterface, ifname);
		return;
	}

	/* now try and establish the HF connection */
	sprintf (command, "%c%cPG%s%c", SOH, 0x40, callsign, ETB);

	NEW_hfdd_send (ifp->dev, command, strlen (command));

	pk232_we_called = 1;
}

/* 08Feb2007, Maiko, Function to check for disconnected status */
static int disconnected (char *ptr)
{
	if (memcmp ("DISCONNECTED", ptr, 12) == 0)
	{
		log (-1, "PK232 - disconnected");

		close_s (mbxs[0]);

		/*
		 * 23Jun07, Maiko, Oops ! We need to make sure we reset this
		 * value, or else any incoming connects will never see a mbox
		 * greet them (ie, VA3RSA has been trying a few times). This
		 * can happen if I first try to connect out. It was not reset
		 * after a failed connect (or a connect for that matter).
		 */
		pk232_we_called = 0;

		return 1;
	}

	return 0;
}

/* 05Feb2007, Maiko, Okay, now able to properly handle incoming calls */
static int pk232_incoming (char *ptr, char *callsign)
{
	char *cptr = callsign;

	if (memcmp ("CONNECTED to", ptr, 11) == 0)
	{
		ptr += 13;	/* skip to callsign */

		/* 06May2008, Maiko, ' VIA LONGPATH' should be discarded (space) */
		while (*ptr && *ptr != ' ' && *ptr != 0x0d)
			*cptr++ = *ptr++;

		*cptr = 0;

		log (-1, "PK232 - connected to [%s]", callsign);

		return 1;
	}

	return 0;
}

/* 23Apr2008, Maiko (VE4KLM), Read a SOH/ETB delimited block of data */

static int read_tnc_block (int dev, unsigned char *tncdata)
{
	unsigned char *ptr = tncdata, last_c = 0xff;

	int len, c, ft = 1;

	j2alarm (5000);	/* 5 seconds or bust !!! */

	while (1)
	{
		if ((c = get_asy (dev)) == -1)
			break;

		*ptr = c & 0xff;

		// log (-1, "BLK (%d)", (int)*ptr);

		/* first character MUST be a FEND character */
		if (ft && (*ptr != SOH))
		{
			len = -2;	/* indicate possible loss of HOST MODE */
			break;
		}

		/* next FEND character denotes end of data */
		if (!ft && (*ptr == ETB))
		{
			/*
			 * 11Jul2008, Maiko, IF a DLE is in front of the ETB, then
			 * this particular ETB is actual DATA, and not the end of the
			 * block. Didn't think of this originally - jamming things up.
			 */
			if (last_c != DLE)
			{
				/* include the trailing FEND in returned data */
				ptr++;
				break;
			}
		}

		last_c = c;

		ft = 0;

		ptr++;
	}

	j2alarm (0);

	if (len != -2)
	{
		len = (int)(ptr - tncdata);
		pk232_dump (len, tncdata);
	}

	return len;
}

/*
 * 09Sep08, with the new queue system, hfddrtns can now call
 * the disconnect function directly, no more use of the disc
 * flags like in the older version of the HF modem drivers.
 */
void pk232_disconnect (int dev)
{
	char command[6];
	sprintf (command, "%c%cRC%c", SOH, 0x40, ETB);
	NEW_hfdd_send (dev, command, 5);
}

void pk232_machine (int dev, HFDD_PARAMS *hfddp)
{
	unsigned char *ptr, cmd;

	int len, which_poll = 0;

	hfdd_conn_flag = 0;

	while (1)
	{
		/*
	 	 * 28Aug2008, Maiko, Really, I should be using a queue system, and
		 * have only ONE point of entry when sending data to the tnc, not a
		 * bunch of asy_send () from all over the place like previous drivers
		 * which I've written for the HF modems. I have to stop being lazy
		 * when developing drivers, and get a bit more serious about how I
		 * do this, hence the NEW_hfdd_send() in the NEW hfddq.c module.
		 *
		 * 29Aug2008, We sent data, waiting for ack - just poll
		 *
		 * 03Sep2008, I think perhaps we need to wait for the echo as well,
		 * since it tells us the TNC successfully sent it out HF - that is
		 * why the 'EAS Y' init param in 232.cfg is so vital to this.
		 */

		/* if waiting for a serial ack or HF echo, then QUEUE up a poll */
		if (wait_4_ack || wait_4_echo)
			pk232_data_poll (dev, 1);

		if (!send_tnc_block ())
		{
			/* if nothing to send, then QUEUE up a poll */

			if (which_poll)
			{
				/*
				 * 12Sep08, Maiko (VE4KLM), Okay I need a reliable way
				 * to determine IRS or ISS, so this is it then.
				 */
				pk232_opmode_poll (dev);
				which_poll = 0;
			}
			else
			{
 				pk232_data_poll (dev, 0);
				which_poll = 1;
			}

			continue;
		}

		/* 20Feb2007, Maiko, New SOH/ETB delimited read function */
		if ((len = read_tnc_block (dev, response)) == 0)
			continue;

		ptr = response;
		ptr++;	/* skip the SOH */
		cmd = *ptr++;
		len -= 2;	/* adjust length by 2 bytes just read */

		/* 10Sep2008, Maiko, TNC init hostmode */
		if (cmd == '@')
		{
			/* if a command is simply echoed back, then not in hostmode */
			hfdd_init_tnc (dev);
			continue;
		}

		/* 03Sep08, Maiko, Wait for echo before sending more */
		if (cmd == '/')
		{
			/* 21Sep08, Maiko, Only clear for more when ALL bytes
			 * originally sent to the tnc are echoed back !!! */
			echo_bytes -= len;

			echo_bytes++;	/* ETB does not count, len contains it */

			if (echo_bytes > 0)
			{
				log (-1, "still waiting for %d echo bytes", echo_bytes);
				continue;
			}

		/*
		 * 24Sep08, Maiko, It seems the PK232 is echoing back an extra byte
		 * or so, when JNOS is sending messages out. I've noticed a '|' char
		 * in particular. I don't know why this is happening. I first noticed
		 * it because we can get NEGATIVE values of echo_bytes. This does not
		 * seem to harm things any (yet) ...
		 */
			if (echo_bytes < 0)
				log (-1, "got %d extra echo bytes", abs (echo_bytes));
			else
				log (-1, "got all echo bytes");

			delete_priority ();
			wait_4_echo = 0;
			echo_bytes = 0;
		}

		/* 30May2008, Maiko, Wait for Data to be acked flag */
		if (cmd == '_')
		{
			if (!strncmp ((char*)ptr, "XX", 2))
			{
				delete_priority ();
				wait_4_ack = 0;
				wait_4_echo = 1;
			}
		}

		if (cmd == 0x3f)
		{
			log (-1, "monitored [%.*s]", len-2, ptr);
			continue;
		}

		if (cmd == 0x4f)	/* 17Jun2008, Reorganized 0x4f handling */
		{
			char *rcmd = (char*)ptr;

			ptr += 2;

			/*
			 * 26Sep08, Maiko, Running into a strange situation where a
			 * changeover is sent to the PK232 but it stays in ISS mode,
			 * instead of switching to IRS - why ??? I don't know, unless
			 * the Airmail client is sending something and there's a clash,
			 * hard to say right now. Thanks Jan (PA3GJX) for catching it.
			 *
			 * New code (chk_4_irs) where I will monitor to make sure that
			 * we eventually get into IRS once we get the response for OV.
			 *
			 * 30Sep08, Maiko, After running ALOT of tests, I can confirm
			 * that this new code works for sure. Sending another OV after
			 * several seconds did indeed put the PK232 into IRS and the
			 * exchange continued like it should have. Fixed (for now).
			 */
			if (!strncmp (rcmd, "OV", 2))
				chk_4_irs = 100;

			chk_4_irs--;

			if (chk_4_irs == 1)
			{
				char tcmd[6];
				log (-1, "still waiting for IRS - send another changeover");
				pk232_changeover (dev, tcmd, 0);
				chk_4_irs = 0;
			}
	
			/* 12Sep2008, Maiko, Use opmode poll to check for IRS or ISS */

			if (!strncmp (rcmd, "OP", 2))
			{
				char iXs = *(ptr+3);

				if (iXs == 'S')
				{
					if (hfdd_irs ())
						log (-1, "we are now ISS");

					set_hfdd_iss ();
				}
				else if (iXs == 'R')
				{
					if (hfdd_iss ())
						log (-1, "we are now IRS");

					set_hfdd_irs ();

					chk_4_irs = 0;		/* 26Sep08, Maiko, New - clear */
				}
				else
					log (-1, "unknown %c opmode", iXs);
			}

			continue;
		}

		if (cmd == 'P')	/* Link messages */
		{
			if (pk232_incoming ((char*)ptr, hfddp->call))
			{
				strlwr (hfddp->call);

				if (!pk232_we_called)
				{
  					hfdd_mbox (dev, mbxs, (void*)TTY_LINK,
						(void*)(hfddp->call));
				}

				/*
				 * 23Jun07, Maiko, Oops ! We need to make sure we reset this
				 * value, or else any incoming connects will never see a mbox
				 * greet them (ie, VA3RSA has been trying a few times). This
				 * can happen if I first try to connect out. It was not reset
				 * after a failed connect (or a connect for that matter).
				 */
				pk232_we_called = 0;

				hfdd_conn_flag = 1;

				continue;
			}

			if (disconnected ((char*)ptr))
			{
				hfdd_conn_flag = -1;
				continue;
			}

			if (!strncmp ((char*)ptr, "Timeout", 7))
			{
				hfdd_conn_flag = -1;
				pk232_we_called = 0;
			}
		}

		if (cmd == 0x30)	/* data comes IN as $3x, where x = 0 */
		{
			int kdsoc = mbxs[0];

			if (hfdd_fwdsock != -1)
				kdsoc = hfdd_fwdsock;

		/*	log (-1, "data (%d) [%.*s]", len, len, ptr); */

			while (len > 1)	 /* skip the ETB */
			{
				/* 20Feb2007, Maiko, Fix up escape sequences */
				if (*ptr == DLE)
				{
					if (hfdd_debug)
						log (-1, "escape (esc) character");

					ptr++;
					len--;
				}

				/* log (-1, "[%02x] (%d)", (int)*ptr, (int)*ptr); */

				usputc (kdsoc, (char)(*ptr));
				usflush (kdsoc);
				ptr++;
				len--;
			}
		}
	}
}

#endif	/* end of HFDD */

