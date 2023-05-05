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
 * $Date: 2009/12/06 01:21:59 $
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
static void dxp_hw_reset (int dev);

/* 14Feb2005, Maiko, New function to allow me to set mode */
static void dxp_setmode (int dev, int pactor);

/* state machine stuff */
static enum { idle, init, call, called, connected, finished } hfdd_state = idle;

extern int hfdd_fwdsock;	/* for forwarding support */

static int mbxs[2] = { -1, -1 };

extern int hfdd_conn_flag;	/* connected or not connected flag */

/*
 * 20Sep2006, Maiko, New code to deal with data loss that I have
 * noticed. I think the loss is because I do not WAIT for the DXP
 * to tell me we are ISS, before I send it the data to go out.
 */

#define HFDD_STDBY 0
#define HFDD_ISS   1
#define HFDD_IRS   2

static int session_mode = HFDD_STDBY;   /* 20Sep2006, Maiko */

/*
 * 06Feb2007, Maiko, A couple of functions to set the ISS / IRS flags,
 * these functions should really be removed from this DXP specific module,
 * and put into it's own. Later, I just want the KAM to work right now.
 */

void set_hfdd_iss ()
{
	session_mode = HFDD_ISS;
}

void set_hfdd_irs ()
{
	session_mode = HFDD_IRS;
}

int hfdd_iss ()
{
    return (session_mode == HFDD_ISS);
}

int hfdd_irs ()
{
    return (session_mode == HFDD_IRS);
}

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

/* 04Oct2006, Maiko, Eat the $8000 (END) of some messages */
static int dxp_eat_the_END (int dev)
{
	int status_i;

	if ((status_i = get_asy (dev)) == -1 || status_i != 0x80)
		return -1;

	if ((status_i = get_asy (dev)) == -1 || status_i != 0x00)
		return -1;

	return 0;
}


/* 04Oct2006, Maiko, Improved regular status logging */
static int dxp_status (int dev, int status_i)
{
	static char *statstr[8] = {
		"link disc normal",
		"link failed",
		"signal lost",
		"link pactor",
		"monitor fsk",
		"modem reset",
		"mycall set",
		"fsk mode",
	};

	static char *modestr[6] = {
		"clover",
		"amtor",
		"amtor fec",
		"pactor",
		"pactor fec",
		"amtor sel fec"
	};

	int offset_i = -1;

	switch (status_i)
	{
		case 0x23:
			offset_i = 0;
			break;
		case 0x24:
			offset_i = 1;
			break;
		case 0x25:
			offset_i = 2;
			break;
		case 0x2b:
			offset_i = 3;
			break;
		case 0x2e:
			offset_i = 4;
			break;
		case 0x09:
			offset_i = 5;
			break;
		case 0x13:
			offset_i = 6;
			break;
		case 0x84:
			offset_i = 7;
			break;
	}

	if (offset_i == -1)
		log (-1, "status [80%02X]", status_i);
	else
		log (-1, "%s", statstr[offset_i]);

	/* 05Oct2006, Some status codes require additional processing */

	if (status_i == 0x2e)
	{
		if ((status_i = get_asy (dev)) == -1 || status_i != 0x80)
			return -1;

  		if ((status_i = get_asy (dev)) == -1)
			return -1;

		if (dxp_eat_the_END (dev) == -1)
			return -1;

		if (status_i < 1 || status_i > 6)
			log (-1, "[80%02X]", status_i);
		else
			log (-1, "%s", modestr[status_i-1]);
	}

	return 0;
}

/*
 * 04Oct06, Maiko, Better monitoring functions, restructuring
 * of dxp code, found some implementation errors, and I should
 * really learn to read the engineering manuals alot better !
 */
static int dxp_fsk_status (int dev)
{
	static char *fskstatstr[23] = {
		"idle",
		"traffic",
		"request",
		"error",
		"phasing",
		"over (n/i)",
		"fsk tx (rtty)",
		"fsk rx (rtty)",
		"100 baud",
		"200 baud",
		"huffman on",
		"huffman off",
		"standby and listening",
		"standby",
		"iss",
		"irs",
		"standby (amtor) and listening",
		"standby (amtor)",
		"fec tx (amtor)",
		"fec rx (amtor)",
		"fec tx",
		"free sig tx (amtor)",
		"free sig tx (amtor) t/o"
	};

	int status_i;

	if ((status_i = get_asy (dev)) == -1 || status_i != 0x80)
		return -1;

  	if ((status_i = get_asy (dev)) == -1)
		return -1;

	if (status_i > 22)
		log (-1, "fsk status [80%02X]", status_i);
	else
		log (-1, "%s", fskstatstr[status_i]);

	/* Some status codes require additional processing */

	switch (status_i)
	{
		case 0x0e:
			session_mode = HFDD_ISS;
			break;

		case 0x0f:
			session_mode = HFDD_IRS;
			break;
	}

	return 0;
}

#ifdef	DEBUG

static char logbuffer[104];
static char *logptr = logbuffer;
static int logcnt = 0;

static void log_jheard ()
{
	char callsign[50], *cptr = callsign, *ptr;

	/* 13Oct06, Maiko, Grab the first callsign heard */

	if (!(ptr = strstr (logbuffer, "de ")))
		if (!(ptr = strstr (logbuffer, "DE ")))
			return;

	ptr += 3;	/* skip ahead to possible callsign */

	while (*ptr)
	{
		if (!isalnum (*ptr))
			break;

		*cptr++ = *ptr++;
	}

	*cptr = 0;

	/* at this point we might have a valid callsign - call 'logsrc()' */

	log (-1, "heard station [%s]", callsign);
}

static void log_flush ()
{
	if (!logcnt)
		return;

	log (-1, "[%s]", logbuffer);

	log_jheard ();	/* 13Oct2006, Maiko, Attempt to jheard */

	logptr = logbuffer;

	*logptr = 0;

	logcnt = 0;
}

static void log_data (int c)
{
	if (isprint (c))
		logptr += sprintf (logptr, "%c", c);
	else if (c != 0x80)
		logptr += sprintf (logptr, ".");

	if (logcnt++ > 100)
		log_flush ();
}

#endif

void dxp_machine (int dev, HFDD_PARAMS *hfddp)
{
	int c, bbs_mode = 0;

	hfdd_state = init;

	hfdd_conn_flag = 0;	/* default no connected */ 
 
	/* 19Feb2005, Maiko, Okay DXP now has bbs mode as well */
	if (!memcmp ("bbs", hfddp->call, 3))
		bbs_mode = 1;

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
				if (!bbs_mode)
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

		if (c == 0x80)
		{
#ifdef	DEBUG
			log_flush ();
#endif
    		if ((c = get_asy (dev)) == -1)
				break;

			/* 04Oct2006, Maiko, More restructuring, etc */

			if (c == 0x7a)
			{
				if (dxp_fsk_status (dev) == -1)
					break;

				continue;
			}

			if (dxp_status (dev, c) == -1)
				break;

			if (c == 0x23 || c == 0x24)
			{
				if (dxp_eat_the_END (dev) == -1)
					break;

				if (hfddp->keyboard)
				{
					if (c == 0x23)
						tprintf ("*** disconnected from %s\n", hfddp->call);
					else
						tprintf ("*** failure with %s\n", hfddp->call);
				}

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
				if (dxp_eat_the_END (dev) == -1)
					break;

				if (hfddp->keyboard)
					tprintf ("*** signal lost with %s\n", hfddp->call);

				break;
			}
			else if (bbs_mode || (hfdd_state == call) || (hfdd_state == called))
			{
				if (c == 0x10)
				{
					if (hfddp->keyboard)
						j2tputs ("Trying... Escape sequence is: +++<CR>\n");
				}
				/* 16Feb2005, Maiko, Added P-Mode connected flag */
				else if ((c == 0x20) || (c == 0x2b))
				{
					if (bbs_mode)
					{
						/* 05Mar2005, Maiko, Get incoming callsign */
						if (dxp_get_incall (dev, hfddp->call) == -1)
						{
							log (-1, "dxp_get_incall () failed !!!");
							hfdd_state = finished;
							break;
						}

						/* 09Oct2006, Maiko, Lower case the call */
	                    strlwr (hfddp->call);

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

#ifdef	DONT_COMPILE
			if (hfddp->keyboard)
				usflush (Curproc->output);
#endif
			continue;
		}

		/*
		 * 04Oct2006, Maiko, Forgot about the *other* ESC character. If we
		 * get one of these, then the byte that follows is actual DATA !!!
		 */
		if (c == 0x81)
		{
			log (-1, "escape character");

    		if ((c = get_asy (dev)) == -1)
				break;
		}
		
#ifdef	DEBUG
		log_data (c);
#endif
		/* 18Jan2005, Maiko, Now doing fwd support for Halcomm */

		if (hfdd_fwdsock != -1)
		{
			usputc (hfdd_fwdsock, c);

			usflush (hfdd_fwdsock);
		}
		else if (bbs_mode)
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

	if (hfddp->keyboard)
		usflush (Curproc->output);
}

#ifdef	DONT_COMPILE
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

#endif

/*
 * 23Sep2006, Maiko, A beacon function of sorts, transmit a
 * pactor FEC message to let users know of our presence.
 */
void dxp_FEC (int dev, int pactor)
{
	char tmp[AXBUF], *data, *mycallp;

	struct mbuf *bp;

	log (-1, "dxp_FEC - send beacon");
	/*
	 * Select P-MODE FEC transmit
	 */

	bp = pushdown (NULLBUF, 4);
	data = bp->data;

	*data++ = 0x80;
	*data++ = 0x1c;
	*data++ = 0x80;
	*data++ = 0x00;

	asy_send (dev, bp);

	j2pause (1000);		/* give it time */

	/*
	 * Create beacon content, then request normal disconnect
	 */

	mycallp = pax25 (tmp, Bbscall);

	bp = pushdown (NULLBUF, 3 * strlen (mycallp) + 4);
	data = bp->data;

	data += sprintf (data, "%s %s %s", mycallp, mycallp, mycallp);

	*data++ = 0x80;
	*data++ = 0x07;

	asy_send (dev, bp);

#ifdef	DONT_COMPILE
	j2pause (5000);		/* give it time */

	/*
	 * Make sure we're back in P-Mode standby
	 */
	bp = pushdown (NULLBUF, 2);
	data = bp->data;

	*data++ = 0x80;
	*data++ = 0x83;

	asy_send (dev, bp);
#endif
}

/*
 * Init Function
 */
static void dxp_init (int dev, int pactor)
{
	int c;

	/* 23Sep2006, Maiko, I really should have left this in
	 * a long time ago. We certainly don't need any stray stuff
	 * coming from past sessions that didn't terminate properly,
	 * it's actually more important to do this as well on the
	 * keyboard side. That part has got me good several times.
	 */
	log (-1, "dxp_init - flushing");

	while (1)
	{
		j2alarm (2000);
   		c = get_asy (dev);
		j2alarm (0);

		if (c == EOF)
			break;

		log (-1, "garbage [%02x]", c);
	}

	log (-1, "dxp_init - starting");

#ifdef	DONT_COMPILE

	/* 14Feb2005, Maiko, Only 1 now, reset moved into dxp_setmode () */
	bp = pushdown (NULLBUF, tinit[0]);

	for (dnt = 0; dnt < tinit[0]; dnt++)
		bp->data[dnt] = tinit[dnt+1];

	asy_send(dev,bp);

#endif
	j2pause (1000L);

	dxp_mycall (dev);

	j2pause (1000L);

	dxp_setmode (dev, pactor);

	j2pause (1000L);

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

	dxp_cmd_call (dev, pax25 (tmp, Bbscall), 1, 0);
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

void dxp_changeover (int dev, int force)
{
	struct mbuf *bp;

	log (-1, "dxp_changeover");

	bp = pushdown (NULLBUF, 2);

	bp->data[0] = 0x80;

	if (force)
		bp->data[1] = 0x87;
	else
		bp->data[1] = 0x0c;

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

