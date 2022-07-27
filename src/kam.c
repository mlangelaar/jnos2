/*
 * These are HFDD routines for the Kantronics KAM modem. Now that I got
 * the SCS and DXP units working quite well, time to move on to the KAM
 * support. Initial KAM version, 08Feb2005, Maiko Langelaar, VE4KLM.
 *
 * For Amateur radio use only (please). Not for profit !
 *
 * $Author: ve4klm $
 *
 * $Date: 2010/11/04 15:58:49 $
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

#define	KAM_FEND	0xc0
#define	KAM_FESC	0xdb
#define	KAM_TFEND	0xdc
#define	KAM_TFESC	0xdd

static int kam_disc_flag = 0;

/* 17Feb2007, Maiko, New conn request variables */
static char kam_conn_call[30];
static int kam_conn_flag = 0;

static int kam_fec_flag = 0;	/* 20Mar2007, New unproto */

static int kam_we_called = 0;	/* 23Feb2007, New */

extern int hfdd_conn_flag;	/* 18Jan2005, Maiko, Now in hfddrtns module */

extern int hfdd_fwdsock;	/* 18Jan2005, Now in hfddrtns module */

static int mbxs[2] = { -1, -1 };

/*
 * 03Aug2005, Maiko, These are now Globals
 * 03Jan2007, Maiko, HF_MODE now points to Pactor for later KAM firmware
 */
static int hf_mode = 2;

/* 02Feb2005, Maiko, New for common tnc_out module */
void set_kam_disc_flag ()
{
	kam_disc_flag = 1;
}

/* 17Feb2007, Maiko, new function to request outgoing connect */
void kam_FEC ()
{
	kam_fec_flag = 1;
}

/* 17Feb2007, Maiko, new function to request outgoing connect */
void kam_make_call (char *callsign)
{
	strcpy (kam_conn_call, callsign);
	kam_conn_flag = 1;
}

/* 14Mar2007, Maiko, a handy utility when tnc hangs on us */
void kam_exit_hostmode (int dev)
{
	char command[4];
	sprintf (command, "%cQ%c", KAM_FEND, KAM_FEND);
	hfdd_send (dev, command, 3);
}

void kam_changeover (int dev, char *command, int force)
{
	char flag_c = 'E';

	/* 06Feb2007, Maiko, I think I got it right */
	if (force)
		flag_c = 'T';

	sprintf (command, "%c%c%c", KAM_FEND, flag_c, KAM_FEND);

	hfdd_send (dev, command, 3);
}

static char Hcommand[300];

/* 10Sep2008, Maiko (VE4KLM), Not static anymore, need for common routines */
void kam_dump (int len, unsigned char *ptr)
{
	static char dumpbuffer[300];

	char *sptr = dumpbuffer;

	int cnt = 0;

	if (!hfdd_debug || !len) return;

	*sptr = 0;	/* start with NULL string */

	while (cnt < len)
	{
		if (*ptr == KAM_FEND)
			sptr += sprintf (sptr, "<FEND>");
		else if (*ptr == KAM_TFEND)
			sptr += sprintf (sptr, "<TFEND>");
		else if (*ptr == KAM_FESC)
			sptr += sprintf (sptr, "<FESC>");
		else if (*ptr == KAM_TFESC)
			sptr += sprintf (sptr, "<TFESC>");
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

	log (-1, "DATA (%d) [%s]", len, dumpbuffer);
}

/* 12Jul2005, Maiko, Send function redone */
void kam_send_data (int dev, char *data, int len)
{ 
	Hcommand[0] = KAM_FEND;
	Hcommand[1] = 'D';
	Hcommand[2] = '2';
	Hcommand[3] = '0';

	memcpy (Hcommand + 4, data, len);

	Hcommand[4 + len] = KAM_FEND;
/*
	sprintf (Hcommand, "%cD%d0%.*s%c", KAM_FEND,
		hf_port, len, data, KAM_FEND);
*/
	kam_dump (5 + len, (unsigned char*)Hcommand);

	hfdd_send (dev, Hcommand, 5 + len);
}

static void kam_pactor_standbye (int dev, unsigned char *command)
{
	sprintf ((char*)command, "%cC20PACTOR%c", KAM_FEND, KAM_FEND);

	hfdd_send (dev, (char*)command, 11);
}

/*
static void kam_get_status (int dev, unsigned char *command)
{
	sprintf (command, "%cC20STATUS%c", KAM_FEND, KAM_FEND);

	hfdd_send (dev, command, 11);
}
*/

static unsigned char Kcommand[300], response[300];

/* 20Mar2007, Maiko, New function to send out FEC (unproto beacon) */
static void kam_unproto (int dev, char *command)
{
	char tmp[AXBUF];
  
	char *bcall = pax25 (tmp, Bbscall);

	sprintf (command, "%cT%c", KAM_FEND, KAM_FEND);
	hfdd_send (dev, command, 3);
	j2pause (2000);
	hfdd_empty_tnc (dev);
	j2pause (2000);

	sprintf (command, "%cD20de %s %s %s\r%c",
		KAM_FEND, bcall, bcall, bcall, KAM_FEND);

	hfdd_send (dev, command, strlen (command));
	j2pause (2000);
	hfdd_empty_tnc (dev);
	j2pause (2000);

	sprintf (command, "%cE%c", KAM_FEND, KAM_FEND);
	hfdd_send (dev, command, 3);
	j2pause (2000);
	hfdd_empty_tnc (dev);
	j2pause (2000);
}

static void kam_connect (int dev, char *command, char *callsign)
{
	static char *conmode[3] = { "AMTOR", "GTOR", "PACTOR" };

	/*
	 * 13Jul2005, Maiko, Use AMTOR for initial development, since all
	 * I have to work with is a KAM (ver 5.00). On 30Jul2005, I finally
	 * got hold of a KAM (ver 4.00) - hooked up them back to back, not
	 * much to work with, but better then nothing I suppose :-(
	 *
	 * 23Feb2007, Maiko, Finally got a KAM (ver 6.1) for pactor :-)
	 *
	int use_hf = 1, vhf_port = 1, hf_port = 2, hf_mode = 2;
	 */

	/*
	 * 23Feb2007, We have to break out of Standbye mode to get
	 * back to Cmd mode, so that we can establish HF connection.
	 */
	sprintf (command, "%cX%c", KAM_FEND, KAM_FEND);
	hfdd_send (dev, command, 3);
	j2pause (2000);
	hfdd_empty_tnc (dev);
	j2pause (2000);

	/* 10Jul2007, Maiko, This bothers me, but when I BREAK OUT of
	 * the HF mode to go back to packet(cmd) mode, the TNC sends me
	 * a single FEND - not sure if this is normal. I've been trying
	 * to debug this, so far it seems to not cause problems ...
	 */

	/* now try and establish the HF connection */
	sprintf (command, "%cC20%s %s%c", KAM_FEND,
		conmode[hf_mode], callsign, KAM_FEND);

	hfdd_send (dev, command, strlen (command));
}

/* 08Feb2007, Maiko, Function to check for disconnected status */
static int kam_disconnected (char *ptr)
{
	if (memcmp ("<PACTOR STANDBY", ptr, 15) == 0)
	{
			log (-1, "KAM - disconnected");

			/*
			 * 23Jun07, Maiko, Oops ! We need to make sure we reset this
			 * value, or else any incoming connects will never see a mbox
			 * greet them (ie, VA3RSA has been trying a few times). This
			 * can happen if I first try to connect out. It was not reset
			 * after a failed connect (or a connect for that matter).
			 */
			kam_we_called = 0;

			return 1;
	}
	return 0;
}

/* 05Feb2007, Maiko, Okay, now able to properly handle incoming calls */
static int kam_incoming (char *ptr, char *callsign)
{
	char *cptr = callsign;

	if (memcmp ("<LINKED TO", ptr, 10) == 0)
	{
		ptr += 11;	/* skip to callsign */

		while (*ptr && *ptr != '>')
			*cptr++ = *ptr++;

		*cptr = 0;

		log (-1, "KAM - connected to [%s]", callsign);

		return 1;
	}

	return 0;
}

/* 20Feb2007, Maiko, Read a FEND delimited block of data */
static int read_tnc_block (int dev, unsigned char *tncdata)
{
	unsigned char *ptr = tncdata;

	int len, c, last_c = -1, ft = 1;

	j2alarm (5000);	/* 5 seconds or bust !!! */

	while (1)
	{
		if ((c = get_asy (dev)) == -1)
			break;

		*ptr = c & 0xff;

		// log (-1, "BLK (%d)", (int)*ptr);

		/* first character MUST be a FEND character */
		if (ft && (*ptr != KAM_FEND))
		{
			len = -2;	/* indicate possible loss of HOST MODE */
			break;
		}

		/* next FEND character denotes end of data */
		if (!ft && (*ptr == KAM_FEND) && last_c != KAM_FEND)
		{
			ptr++;	/* include the trailing FEND in returned data */
			break;
		}

		last_c = c;

		ft = 0;

		ptr++;
	}

	j2alarm (0);

	if (len != -2)
	{
		len = (int)(ptr - tncdata);
		kam_dump (len, tncdata);
	}

	return len;
}

/*
 * 11Feb2007, Maiko, Disconnect function redone (again)
 * this works remarkably well - CTRL-C followed by X, then
 * wait a second or so, put back in STANDBY mode. This *might*
 * require some tweaking down the road, but it's working 100
 * percent right now !!!
 */
static void kam_disconnect (int dev, char *command)
{
	sprintf (command, "%cX%c", KAM_FEND, KAM_FEND);
	hfdd_send (dev, command, 3);
	j2pause (2000);
	hfdd_empty_tnc (dev);
	j2pause (2000);
	kam_pactor_standbye (dev, (unsigned char*)command);
	j2pause (2000);
	hfdd_empty_tnc (dev);
}

void kam_machine (int dev, HFDD_PARAMS *hfddp)
{
	unsigned char *ptr, cmd, port, stream;

	int len, loop = 1;

	kam_disc_flag = 0;

	hfdd_conn_flag = 0;

	while (loop)
	{
		/*
		 * Before we bug the controller, let JNOS have a chance to do
		 * the other things it needs to do, and run the other drivers
		 * that it needs to process, etc, etc ...
		 */
		pwait (0);

		/* 20Mar2007, Maiko, Feable attempt at FEC unproto beacon */
		if (kam_fec_flag)
		{
			kam_unproto (dev, (char*)Kcommand);
			kam_fec_flag = 0;
		}
		/*
		 * 17Feb2007, Maiko, Add a few new options (conn req, etc)
		 */
		if (kam_conn_flag)
		{
			kam_connect (dev, (char*)Kcommand, kam_conn_call);
			kam_conn_flag = 0;
			kam_we_called = 1;
		}

		/* current mailbox can force disconnect by setting this flag */
		if (kam_disc_flag)
		{
			kam_disconnect (dev, (char*)Kcommand);
			kam_disc_flag = 0;
		}

		/*
		 * Used to ask for the overall status if we are not busy
		 * 
			kam_get_status (dev, Kcommand);
		 *
		 * 11Feb2007, Maiko, Turns out we DONT have to poll !
		 */

		/* 20Feb2007, Maiko, New FEND delimited read function */
		if ((len = read_tnc_block (dev, response)) == 0)
			continue;

		if (len == -2)
		{
			/* 24May2007, Maiko, New init function in use */
			hfdd_init_tnc (dev);

			kam_pactor_standbye (dev, Kcommand);
			hfdd_empty_tnc (dev);

			hfdd_empty_tnc (dev);
			hfdd_empty_tnc (dev);
			hfdd_empty_tnc (dev);

			continue;
		}

		ptr = response;

		ptr++;	/* skip leading FEND */

		cmd = *ptr++;
		port = *ptr++;
		stream = *ptr++;

		len -= 4;	/* adjust length by 4 bytes just read */

		/* 24Jul2005, Maiko, IRS and ISS status */
		if (cmd == 'I')
		{
			/* 24Feb2007, Maiko, Oops, functions reversed ! */
			if (stream == '0')
			{
				log (-1, "KAM - we are IRS");
				set_hfdd_irs ();
			}
			else if (stream == '1')
			{
				log (-1, "KAM - we are ISS");
				set_hfdd_iss ();
			}
		}			
		else if (cmd == 'C' || cmd == 'S')
		{
			if (len == 1 && *ptr == KAM_FEND)
				continue;

				/* 07Feb07, Maiko, skip any non printable characters */
			while (*ptr <= 0x0d)
				ptr++;

			/* 05Feb2007, let's try incoming connects */
			if (kam_incoming ((char*)ptr, hfddp->call))
			{
				if (hfdd_debug)
					log (-1, "port %c stream %c", port, stream);

				strlwr (hfddp->call);

				if (!kam_we_called)
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
				kam_we_called = 0;

				hfdd_conn_flag = 1;

				continue;
			}

			if (kam_disconnected ((char*)ptr))
			{
				hfdd_conn_flag = -1;
				continue;
			}

			if (hfdd_debug)
			{
				log (-1, "NS [%c] [%c] [%c] [%s]",
					cmd, port, stream, ptr);
			}
		}
		else if (cmd == 'D')
		{
			int kdsoc = mbxs[0];

			if (hfdd_fwdsock != -1)
				kdsoc = hfdd_fwdsock;

			while (len > 1)	 /* skip the last FEND !!! */
			{
				/* 20Feb2007, Maiko, Fix up escape sequences */
				if (*ptr == KAM_FESC)
				{
					ptr++;
					if (*ptr == KAM_TFEND)
					{
						ptr++;
						len--;
						usputc (kdsoc, KAM_FEND);
					}
					else if (*ptr == KAM_TFESC)
					{
						ptr++;
						len--;
						usputc (kdsoc, KAM_FESC);
					}
					else usputc (kdsoc, KAM_FESC);
				}
				else
				{
					usputc (kdsoc, (char)(*ptr));
					ptr++;
				}

				usflush (kdsoc);

				len--;
			}
		}
	}
}

#endif	/* end of HFDD */

