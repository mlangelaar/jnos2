/*
 * Support WINMOR Sound Card TNC (over tcp/ip) as a digital modem
 *
 * Designed and Coded (April 2010) by Maiko Langelaar, VE4KLM
 * First official prototype source checked in April 27, 2010 
 *
 * (C)opyright 2010 Maiko Langelaar, VE4KLM
 *
 * For Amateur Radio use only (please) !
 *
 * Note : the WINMOR legal stuff says NOT for commercial use !!!
 *
 */

#include "global.h"

#ifdef	WINMOR

#include "netuser.h"
#include "hfdd.h"
#include "winmor.h"
#include "lapb.h"

#include <ctype.h>

static char connect_call[30];
static int connect_requested = 0;
static int disconnect_requested = 0;
static int we_called = 0;
static int ctrl_socket = -1;
static int data_socket = -1;
static IWMP wmp;

static int mbxs[2] = { -1, -1 };

extern int hfdd_conn_flag;	/* In the 'hfddrtns.c' module */

extern int hfdd_fwdsock;

/*
 * 17Apr2014, Maiko (VE4KLM), Let's write a basic heard function specific
 * to the WINMOR driver, includes some basic structure definitions.
 */
struct wh {
	struct wh *next;
	char *callsign;
	int32 time;
};

#define NULLWH (struct wh*)0

static struct wh *Wh = NULLWH;

static void log_winmor_station (char *callsign)
{
	register struct wh *wh;

	for (wh = Wh; wh != NULLWH; wh = wh->next)
	{
		/* if it's already in there, just update the time heard */
		if (!strcmp (callsign, wh->callsign))
		{
			wh->time = secclock ();
			break;
		}
	}

	/* if we end the list with nothing, then add a new entry */
	if (wh == NULLWH)
	{
		wh = (struct wh*)callocw (1, sizeof(struct wh));
		wh->callsign = j2strdup (callsign);
		wh->time = secclock ();
		wh->next = Wh;
		Wh = wh;
	}
}

void winmor_heard ()
{
	register struct wh *wh;

    j2tputs ("Station   Time since heard\n");

	for (wh = Wh; wh != NULLWH; wh = wh->next)
	{
		tprintf ("%-9s   %12s\n", wh->callsign,
			tformat (secclock () - wh->time));
	}
}

/* 17Apr2014, Maiko (VE4KLM), End of WINMOR heard list functions */

static void logit (int s, char *host, int port, int ctrl, int err, int err2)
{
	char logbuf[100], *ptr = logbuf;

	ptr += sprintf (logbuf, "winmor (%s) ", ctrl ? "CTRL" : "DATA");

	if (err)
		ptr += sprintf (ptr, "host [%s] port [%d] - ", host, port);

	switch (err)
	{
		case 0:
			ptr += sprintf (ptr, "connected");
			break;
		case 1:
			ptr += sprintf (ptr, "host not found");
			break;
		case 2:
			ptr += sprintf (ptr, "no sockets, errno %d", err2);
			break;
		case 3:
			ptr += sprintf (ptr, "connect failed, errno %d", err2);
			break;
	}

	log (s, "%s", logbuf);
}

int connect_winmor (IWMP *wmp)
{
    struct sockaddr_in fsocket;

    int s = -1;
 
	if ((fsocket.sin_addr.s_addr = resolve (wmp->host)) == 0)
		logit (s, wmp->host, wmp->port, wmp->ctrl, 1, 0);
	else
	{
	    fsocket.sin_family = AF_INET;
   		fsocket.sin_port = wmp->port;
 
		if ((s = j2socket (AF_INET, SOCK_STREAM, 0)) == -1)
			logit (s, wmp->host, wmp->port, wmp->ctrl, 2, errno);
		else
		{
			if (wmp->ctrl)
				sockmode (s, SOCK_ASCII);
			else
				sockmode (s, SOCK_BINARY);
  
			if (j2connect (s, (char*)&fsocket, SOCKSIZE) == -1)
			{
				logit (s, wmp->host, wmp->port, wmp->ctrl, 3, errno);
				close_s (s);
				s = -1;
			}
			else logit (s, wmp->host, wmp->port, wmp->ctrl, 0, 0);
		}  
	}

	return s;
}

static void winmor_data_rx (int xdev, void *p1, void *p2)
{
	int save_errno, count = 0, c;

	while (1)
	{
		j2alarm (200);
		c = recvchar (data_socket);
		save_errno = errno;
		j2alarm (0);

		if (c == -1)
		{
			if (save_errno == EALARM)
			{
				if (count)
				{
					if (hfdd_fwdsock != -1)
						usflush (hfdd_fwdsock);
					else if (mbxs[0] != -1)
						usflush (mbxs[0]);
					else
						log (-1, "winmore_rx - usflush (no socket)");

					count = 0;	/* make sure we reset this */
				}
			}
			else
			{
				log (-1, "winmore_rx - ABORT - errno %d", errno);
				break;
			}
		}
		else
		{
			count++;

			if (hfdd_debug)
				log (-1, "[%c]", (char)c);

			if (hfdd_fwdsock != -1)
				usputc (hfdd_fwdsock, (char)c);
			else if (mbxs[0] != -1)
				usputc (mbxs[0], (char)c);
			else
				log (-1, "winmore_rx - usputc (no socket)");
		}
	}
}

/* 12Apr2010, Maiko, Finally forwarding to rms express (peer2peer mode) */
static void dumpit (int len, unsigned char *ptr)
{
	static char dumpbuffer[300];

	char *sptr = dumpbuffer;

	int cnt = 0;

	if (!hfdd_debug || !len) return;

	*sptr = 0;	/* start with NULL string */

	while (cnt < len)
	{
		if (*ptr == 0)
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

void winmor_changeover (int force)
{
	if (!force)
		return;

	log (-1, "winmor - request changeover");

	if (j2send (ctrl_socket, "BREAK\r\n", 7, 0) < 1)
		log (-1, "winmor - write errno %d", errno);
}

void winmor_send_data (char *data, int len)
{ 
	/* 02Sep2010, Maiko (VE4KLM), Compiler warnings arggg */
	dumpit (len, (unsigned char*)data);

	if (j2send (data_socket, data, len, 0) < 1)
		log (-1, "winmor - write errno %d", errno);

	j2pause (5000);	/* wait 5 seconds just to test this out */
}

void set_winmor_disc_flag ()
{
//	log (-1, "actually, let the *system* disconnect naturally ...");
//	disconnect_requested = 1;
}

void winmor_make_call (char *callsign)
{
	strcpy (connect_call, callsign);

	connect_requested = 1;

	we_called = 1;
}

#ifdef	DONT_NEED_CONNECT_TIMEOUT

static void ctofunc (void *t)
{
	char buf[50];
	log (-1, "winmor - connect timer expired");
	sprintf (buf, "DirtyDisconnect\r\n");
	if (j2send (ctrl_socket, buf, strlen (buf), 0) < 1)
		log (-1, "winmor - write errno %d", errno);
}

static struct timer cto;	/* 08Apr2010, connect timeout */

#endif

void initiate_disconnect ()
{
	char buf[50];

	sprintf (buf, "Disconnect\r\n");

	if (j2send (ctrl_socket, buf, strlen (buf), 0) < 1)
		log (-1, "winmor - write errno %d", errno);
}

static void send_bw ()
{
	char buf[10];

	int speed = 500;	/* 12May2010, Maiko, Leave at 500 for now, I
				 * wonder if the 1600 speed is hanging stuff
				 */

	sprintf (buf, "BW %d\r\n", speed);

	if (j2send (ctrl_socket, buf, strlen (buf), 0) < 1)
		log (-1, "winmor - write errno %d", errno);
}

void initiate_connect ()
{
	char buf[50];

	sprintf (buf, "Connect %s\r\n", connect_call);

	if (j2send (ctrl_socket, buf, strlen (buf), 0) < 1)
		log (-1, "winmor - write errno %d", errno);

#ifdef	DONT_NEED_CONNECT_TIMEOUT

	log (-1, "winmor - allow 60 seconds for a connect");

	/* set timer to stop after so many retries */
	cto.func = (void (*)(void*)) ctofunc;
	cto.arg = NULL;
	set_timer (&cto, 60000);
	start_timer (&cto);

#endif

}

void initialize_tnc ()
{
	char buf[50];

	int cnt = 3;

	while (cnt > 0)
	{
		switch (cnt)
		{
			case 3:
				sprintf (buf, "Codec True\r\n");
				break;
			case 2:
				sprintf (buf, "DriveLevel 90\r\n");
				break;
			case 1:
				sprintf (buf, "AutoBreak False\r\n");
				break;
		}

		cnt--;

		if (j2send (ctrl_socket, buf, strlen (buf), 0) < 1)
			log (-1, "winmor - write errno %d", errno);
	}
}

static int incoming_connect (char *ptr, char *callsign)
{
	char *cptr = callsign;

	if (memcmp ("CONNECTED ", ptr, 10) == 0)
	{
		ptr += 10;	/* skip to callsign */

		while (*ptr && *ptr != '>')
			*cptr++ = *ptr++;

		*cptr = 0;

		log (-1, "winmor - connected to [%s]", callsign);

		return 1;
	}

	return 0;
}

void winmor_machine (int dev, HFDD_PARAMS *hfddp)
{
	char buf[100], *bptr;

	int len, loop = 1;

#ifdef	DONT_COMPILE
	int keepalive = 600;	/* 11May2010, Maiko, Keepalive */
#endif

    extern char *hfddip;	/* 27Apr2010, Maiko, in hfddrtns.c */

	hfdd_conn_flag = 0;

	/* wmp.host = strdup ("130.179.72.6"); */

	if (!hfddip)
	{
		log (-1, "ip address of tnc not set !!!");
		return;
	}

	wmp.host = hfddip;	/* 27Apr2010, Maiko, no longer hardcoded */

	while (loop)
	{
		if (ctrl_socket == -1)
		{
			if (hfdd_debug)
				log (-1, "winmor (CTRL) is not connected, trying ...");

			wmp.port = 8500;
			wmp.ctrl = 1;

			ctrl_socket = connect_winmor (&wmp);

			if (ctrl_socket == -1)
			{
				j2pause (60000);
				continue;
			}

			initialize_tnc ();

			j2pause (2000);
		}

		if (data_socket == -1)
		{
			if (hfdd_debug)
				log (-1, "winmor (DATA) is not connected, trying ...");

			wmp.port = 8501;
			wmp.ctrl = 0;

			data_socket = connect_winmor (&wmp);

			if (data_socket == -1)
			{
				j2pause (60000);
				continue;
			}

			newproc ("winmor_rx", 1024, winmor_data_rx, 0,
				(void*)0, (void*)0, 0);

			j2pause (2000);
		}

		/*
		 * Before we bug the controller, let JNOS have a chance to do
		 * the other things it needs to do, and run the other drivers
		 * that it needs to process, etc, etc ...
		pwait (0);
		 */

		if (disconnect_requested)	/* 12Apr2010 */
		{
			disconnect_requested = 0;
			initiate_disconnect ();
		}

		if (connect_requested)
		{
			connect_requested = 0;
			initiate_connect ();
		}

   		j2alarm (100);
		len = recvline (ctrl_socket, buf, sizeof(buf) - 1);
   		j2alarm(0);

		if (len == -1)
		{
 			if (errno != EALARM)
			{
				/* connection probably lost, close down tcp/ip, try again */

				close_s (ctrl_socket);
				ctrl_socket = -1;

				close_s (data_socket);
				data_socket = -1;
			}

#ifdef	DONT_COMPILE

		/*
		 * 18May2010, Maiko, Don't think I will need this anymore for now ...
		 *
		 * 11May2010, Maiko, new keep alive code. The tnc *seems* to disappear
		 * off the tcp/ip map if not used after a while, so let's see if this
		 * does anything. Just send an enter or something every 600 seconds.
	 	 */
			else if (--keepalive < 0)
			{
				keepalive = 600;

				if (j2send (ctrl_socket, "BUFFERS\r\n", 9, 0) < 1)
					log (-1, "winmor - write errno %d", errno);
			}
#endif
			continue;
		}

		rip (buf);

		/* 07May2010, Maiko, PTT and OFFSET just fill up the log */
		if (strncmp ("PTT", buf, 3) && strncmp ("OFF", buf, 3))
			log (ctrl_socket, "%s", buf);

		/* 09Apr2010, Maiko, Two systems connected back to back */
		if (incoming_connect (buf, hfddp->call))
		{
			strlwr (hfddp->call);

			if (!we_called)
			{
				/*
				 * 13Apr2010, Maiko, Must send 'BW' to ack the connect, or
				 * nothing else (ie, BREAK, STATE, etc) will work, and the
				 * remote station will just continue to try connecting.
				 */
				send_bw ();

				j2pause (1000);	/* give it a couple of seconds to steady */

  				hfdd_mbox (dev, mbxs, (void*)TTY_LINK, (void*)(hfddp->call));
			}

			we_called = 0;	/* important to set this, or incoming is ignored */

			hfdd_conn_flag = 1;

			continue;
		}
	/*	
	 * 10Apr2014, Maiko, Try and keep a HEARD list for the HFDD port.
	 * For example I've seen data such as 'MONCALL W5HCS (EM12PU)'.
	 */
		if (strncmp ("MONCALL ", buf, 8) == 0)
		{
			log (-1, "winmor heard [%s]", buf + 8);

			/* 18Apr2014, Maiko, Don't want Grid Square in the heard data */
			for (bptr = buf + 8; *bptr && *bptr != ' '; bptr++); *bptr = 0;

			/*
		 	 * 17Apr2014, Maiko (VE4KLM), I was thinking of using the axheard
			 * functions, but then I need to make changes to several files and
			 * then I have to link this dummy interface into the list of ifaces
			 * and it starts to get confusing - better just leave AX25 functions
			 * out of this, and just write WINMOR specific log functions 4 now.
			 */

			log_winmor_station (buf + 8);
		}

		else if (strncmp ("NEWSTATE ", buf, 9) == 0)
		{
			if (strncmp ("ISS", buf + 9, 3) == 0)
			{
				if (hfdd_irs ())
					log (-1, "we are now ISS");

				set_hfdd_iss ();
			}
			else if (strncmp ("IRS", buf + 9, 3) == 0)
			{
				if (hfdd_iss ())
					log (-1, "we are now IRS");

				set_hfdd_irs ();
			}
			else if (strncmp ("DISCONNECTED", buf + 9, 12) == 0)
			{
				/* 12May2010, Maiko, Oops, should be set to 0 */
				/* 23Mar2014, Maiko, Actually it should be -1 */
				hfdd_conn_flag = -1;
				//hfdd_conn_flag = 0;

				/* 18May2010, Maiko, Socket needs to be closed properly or
				 * else it stays open and they accumulate filling up table.
				 */
				close_s (mbxs[0]);
				mbxs[0] = -1;
			}
		}
	}
}

#endif	/* End of WINMOR */
