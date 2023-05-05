/*
 * Client for the HFDD (HF Digital Device) interface
 * Designed and Coded by Maiko Langelaar / VE4KLM
 * Got the Prototype working October 30, 2004
 *
 * For Amateur Radio use only (please).
 *
 * This my attempt at writing a NOS client for HF digital modems
 * like the Halcomm DXP38 (which I based the prototype client on).
 *
 * $Author: ve4klm $
 *
 * $Date: 2009/05/18 16:36:38 $
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

/* prototypes */  
static void dxp_init (int dev, int pactor);
static void dxp_mycall (int dev);
static void dxp_connect (int dev, char *callsign, int pactor);
static void dxp_cmd_call (int dev, char *callsign, int setmycall, int pactor);
static void dxp_data (int dev, struct session *sp);
static void dxp_hw_reset (int dev);

/* 14Feb2005, Maiko, New function to allow me to set mode */
static void dxp_setmode (int dev, int pactor);

/* state machine stuff */
static enum { idle, init, call, called, connected, finished } hfdd_state = idle;

extern int hfdd_fwdsock;	/* for forwarding support */

static int mbxs[2] = { -1, -1 };

extern int hfdd_conn_flag;	/* connected or not connected flag */

/*
 * 05Mar2005, Maiko, New function to read incoming callsign from
 * the DXP when we get the connect (link) status (ie, 0x8020).
 */
static int dxp_get_incall (int dev, char *incall)
{
	int c;

	while (1)
	{
		if ((c = get_asy (dev)) == -1)
			break;

		if (c == 0x80)
			continue;

		*incall++ = c;

		if (c == 0x00)
			break;
	}

	return c;	/* return value of 0x00 means success ! */
}

void dxp_machine (int dev, HFDD_PARAMS *hfddp)
{
	int c, last_status = -1;

	hfdd_state = init;

	hfdd_conn_flag = 0;	/* default no connected */ 
 
    while (hfdd_state != finished)
	{
		if (pwait (NULL))
			break;

		/* move init and connect into the machine now */
		switch (hfdd_state)
		{
			case init:
				dxp_init (dev, hfddp->pactor);
				hfdd_state = call;
				break;

			case call:

			/* 19Feb2005, Maiko, Okay DXP now has bbs mode as well */
				if (!dxp_bbs_mode)
					dxp_connect (dev, hfddp->call, hfddp->pactor);

				hfdd_state = called;	
				break;

			case idle:
			case called:
			case connected:
			case finished:
				break;
		}

		if ((c = get_asy (dev)) == -1)
			break;

#ifdef	DEBUG
		if (isprint (c))
			log (-1, "M1 > [%c]", c); 
		else if (c != 0x80)
			log (-1, "M1 > [%02x]", c);
#endif
		if (c == 0x80)	/* flags status data */
		{
    		if ((c = get_asy (dev)) == -1)
				break;

			/*
			 * 16Feb2005, Maiko, Watch out for status bytes that have data
			 * following, ie: if we get a $807A, then the 2 bytes following
			 * are actually status information, and should not be treated
			 * as a command or response. We can just ignore most of the
			 * status bytes for now (maybe later on for monitoring).
			 */
			if (last_status == 0x7a)
			{
#ifdef	DEBUG
				log (-1, "Status [807A] [80%02x]", c);
#endif
				last_status = -1;
				continue;
			}

			if (c == 0x7a)
			{
				last_status = c;
				continue;
			}

			/* 16Feb2005, Maiko, Moved this here, since I don't want to
			 * have all those $807A status messages filling up the log.
			 */
			log (-1, "Status [80] [%02x]", c);

			if (c == 0x23)
			{
				if (hfddp->keyboard)
					tprintf ("*** disconnected from %s\n", hfddp->call);

				hfdd_state = finished;

				break;
			}
			else if (c == 0x24)
			{
				if (hfddp->keyboard)
					tprintf ("*** failure with %s\n", hfddp->call);

				hfdd_state = finished;

				break;
			}
			/* 22Jan2005, Maiko, Stumbled across this new one */
			else if (c == 0x09)
			{
				if (hfddp->keyboard)
					tprintf ("*** modem reset\n");

				hfdd_state = finished;

				break;
			}
			/*
			 * 21Jan2005, Maiko, Added 'signal lost' conditional
			 * 14Feb2005, Maiko, Oops, HalComm says I should ignore this,
			 * since the modem will send me the link failed status (8024)
			 * when the link actually fails !!! I should NOT be terminating
			 * session on 'signal lost' conditions. Just warn the user !
			 */
			else if (c == 0x25)
			{
				if (hfddp->keyboard)
					tprintf ("*** signal lost with %s\n", hfddp->call);

				/* hfdd_state = finished; */

				break;
			}
			else if (dxp_bbs_mode || (hfdd_state == call) || (hfdd_state == called))
			{
				if (c == 0x10)
				{
					if (hfddp->keyboard)
						tputs ("Trying... Escape sequence is: +++<CR>\n");
				}
				/* 16Feb2005, Maiko, Added P-Mode connected flag */
				else if ((c == 0x20) || (c == 0x2b))
				{
					if (dxp_bbs_mode)
					{
						/* 05Mar2005, Maiko, Get incoming callsign */
						if (dxp_get_incall (dev, hfddp->call) == -1)
						{
							log (-1, "dxp_get_incall () failed !!!");
							hfdd_state = finished;
							break;
						}

						/* 06Mar2005, Maiko, Now a generic function */
   						hfdd_mbox (dev, mbxs,
							(void*)TTY_LINK, (void*)(hfddp->call));
					}
					else
					{
						if (hfddp->keyboard)
							tprintf ("*** connected to %s\n", hfddp->call);

						hfdd_state = connected;
					}

					/* 18Jan2005, Maiko, integrated into fwding now */
					/* 28Feb2005, Maiko, Set this either way !! */
					hfdd_conn_flag = 1;
				}
			}

			if (hfddp->keyboard)
				usflush (Curproc->output);
		}
		else
		{
			/* 18Jan2005, Maiko, Now doing fwd support for Halcomm */
			if (hfdd_fwdsock != -1)
			{
				usputc (hfdd_fwdsock, c);

				usflush (hfdd_fwdsock);
			}
			else if (dxp_bbs_mode)
			{
	            usputc (mbxs[0], c);

				usflush (mbxs[0]);
			}
			else
			{
				if (hfddp->keyboard)
				{
					/* Map to NL or else we loose data when we display it */
					if (c == 0x0d)
						c = '\n';

					tputc (c); /* assuming this is actual data (for now) */

	    			tflush ();
				}
			}
		}
	}

	if (hfddp->keyboard)
		usflush (Curproc->output);
}

/*
 * Initialization Codes
 */
static int tinit[17] = {

	/*
	 * Set MARK/SPACE values (is this necessary, defaults) ?
	 */

	/* center freq 2210 recommended for PK232 and MFJ1278
	10, 0x80, 0xec, 0x80, 0x08, 0x80, 0x3e, 0x80, 0x09,
		0x80, 0x06, 0, 0, 0, 0, 0, 0
	 */

	10, 0x80, 0xec, 0x80, 0x08, 0x80, 0x66, 0x80, 0x09,
		0x80, 0x2e, 0, 0, 0, 0, 0, 0

	/* set to be compatible with SCS tone 0
	10, 0x80, 0xec, 0x80, 0x05, 0x80, 0x78, 0x80, 0x06,
		0x80, 0x40, 0, 0, 0, 0, 0, 0
 	*/
};

/*
 * Init Function
 */
static void dxp_init (int dev, int pactor)
{
	struct mbuf *bp;

	int cnt, dnt;

	log (-1, "dxp_init - starting");

	/* 14Feb2005, Maiko, Only 1 now, reset moved into dxp_setmode () */
	bp = pushdown (NULLBUF, tinit[0]);

	for (dnt = 0; dnt < tinit[0]; dnt++)
		bp->data[dnt] = tinit[dnt+1];

	asy_send(dev,bp);

	pause (1000L);

	dxp_mycall (dev);

	pause (1000L);

	dxp_setmode (dev, pactor);

	pause (1000L);

	log (-1, "dxp_init - finished");
}

/*
 * Set mode (Clover, Pmode, etc) of the DXP modem
 * 14Feb2005, Maiko, New function, I would like to do
 * pactor with the DXP38 as well, wider audience then.
 */
static void dxp_setmode (int dev, int pactor)
{
	struct mbuf *bp;

	char *data;

	if (!pactor)	/* clover */
	{
		bp = pushdown (NULLBUF, 12);

		data = bp->data;
	/*
	 * Set Clover mode
	 */
		*data++ = 0x80;
		*data++ = 0x80;
	/*
	 * Set the Robust Link Retry value (10)
	 */
		*data++ = 0x80;
		*data++ = 0x60;
		*data++ = 0x80;
		*data++ = 0x0a;
	/*
	 * Set the CCB Retry Maximum value (40)
	 *
	 * 14Feb2005, Maiko, HalComm suggests to increase CCB retry maximum
	 * for HF deep fading. I find alot of that actually when trying to hit
	 * my coastal *test* stations (VE1 or VE7). So let's try to double it.
	 */
		*data++ = 0x80;
		*data++ = 0x62;
		*data++ = 0x80;
		*data++ = 0x28;
	/*
	 * 01Jun2005 - Set Listen Mode
	 */
		*data++ = 0x80;
		*data++ = 0x58;
	}
	else		/* pactor */
	{
		bp = pushdown (NULLBUF, 16);

		data = bp->data;
	/*
	 * Switch to FSK Modes
	 */
		*data++ = 0x80;
		*data++ = 0x84;
	/*
	 * Set Filter BW
	 */
		*data++ = 0x80;
		*data++ = 0xeb;
		*data++ = 0x80;
		*data++ = 0x02;
	/*
	 * Set CS Delay
	 */
		*data++ = 0x80;
		*data++ = 0xf1;
		*data++ = 0x80;
		*data++ = 0x19;
	/*
	 * 26May2005 - Set Listen Mode
	 */
		*data++ = 0x80;
		*data++ = 0x58;

	/* 03Jul2005 - Monitor mode as well */

		*data++ = 0x80;
		*data++ = 0x5a;
	/*
	 * Set P-Mode standby
	 */
		*data++ = 0x80;
		*data++ = 0x83;
	}

	asy_send (dev, bp);
}

/*
 * Set MYCALL in the TNC/Modem
 */
static void dxp_mycall (int dev)
{
	char tmp[AXBUF];

	dxp_cmd_call (dev, pax25 (tmp, Mycall), 1, 0);
}

/*
 * Connect to a remote station
 */
static void dxp_connect (int dev, char *callsign, int pactor)
{
	/* 15Feb2005, Maiko, Allow operator to hardware reset modem !!! */
	if (!memcmp ("reset", callsign, 5))
		dxp_hw_reset (dev);
	else
		dxp_cmd_call (dev, callsign, 0, pactor);
}

/* 02Feb2005, Maiko, New disconnect and changeover functions */
void dxp_disconnect (int dev)
{
	struct mbuf *bp;

	log (-1, "dxp_disconnect");

	bp = pushdown (NULLBUF, 2);

	bp->data[0] = 0x80;
	bp->data[1] = 0x07;

	asy_send (dev, bp);
}

/* 15Feb2005, Maiko, Hardware reset option (connect to reset) */
static void dxp_hw_reset (int dev)
{
	struct mbuf *bp;

	log (-1, "dxp hardware reset");

	bp = pushdown (NULLBUF, 2);

	bp->data[0] = 0x80;
	bp->data[1] = 0x09;

	asy_send (dev, bp);
}

/* 25May2005 - can't believe I forgot to code this function */

void dxp_changeover (int dev)
{
	struct mbuf *bp;

	log (-1, "dxp_changeover");

	bp = pushdown (NULLBUF, 2);

	bp->data[0] = 0x80;
	bp->data[1] = 0x87;

	asy_send (dev, bp);
}

/*
 * Function to save code, used twice - first to set
 * the MYCALL callsign in the TNC/MODEM, secondly to
 * actually make a call to a remote station.
 */
static void dxp_cmd_call (int dev, char *callsign, int setmycall, int pactor)
{
    struct mbuf *bp;

	char *ptr;

	int cnt, len = strlen (callsign);

	bp = pushdown (NULLBUF, ((2 * len) + 4));

	ptr = bp->data;

	log (-1, "dxp_connect - call %s", callsign);

	/* robust connect */
	*ptr++ = 0x80;

	if (setmycall)
		*ptr++ = 0x13;
	else
	{
		if (pactor)
			*ptr++ = 0x19;
		else
			*ptr++ = 0x10;
	}

	for (cnt = 0; cnt < len; cnt++)
	{
		*ptr++ = 0x80;
		*ptr++ = callsign[cnt];
	}

	/* terminate connect command */
	*ptr++ = 0x80;
	*ptr++ = 0x00;

	asy_send (dev, bp);
}

#endif	/* end of HFDD */

