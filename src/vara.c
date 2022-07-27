/*
 * Support for EA5HVK VARA (tcp/ip control) as a digital modem
 *
 * January 5, 2021, The beginnings of a prototype interface.
 *
 * January 16, 2021, Now designed as an IP Bridge using PPP code :)
 *
 * January 28, 2021, Added remote RTS PTT control using UDP, use
 * it in conjunction with the new udpPTT.txt (perl) script, note
 * the script is hardcoded for COM1, edit if you need too ...
 *
 * September 1, 2021, Looks like access mode is finally working to
 * a reasonable degree - did 4 kicks in a row to a winlink express
 * running VARA HF P2P mode, 2 empty, 1 message pickup, followed by
 * another empty, seems to be working, and smooth for a change.
 *  (using full blast licensed VARA - a big thank you to Jose)
 *
 * AND there is now a global flag 'vara_ip_bridge_mode' which
 * will be 0 (off) by default, such that vara access mode will
 * be the default behaviour. You must toggle this flag using
 * the new 'vara ipbridge' before using it with PPP ...
 *
 * The PPP over VARA is working reasonably well, latency is not
 * great, but to be honest, PPP, IP, any stop and start traffic
 * where you're prompted for login and password, these scenarios
 * aren't really suitable for VARA - but it certainly works !
 *
 * (C)opyright 2021 Maiko Langelaar, VE4KLM
 *
 * For Amateur Radio use only (please) !
 *
 */

#include "global.h"

#ifdef	EA5HVK_VARA

#include "mbuf.h"
#include "iface.h"
#include "trace.h"
#include "pktdrvr.h"
#include "devparam.h"
#include "udp.h"

extern int getusage (char*, char*);

/*
 * Maiko, new function in main.c, modified 24Jan2021 so I can
 * output direct to the JNOS log instead of the usual console,
 * so now has an extra argument, if -1L stick with console.
 */
extern int j2directsource (int, char*);

/*
 * 29Sep2019, Maiko (VE4KLM), struct breakouts
 */
#include "ax25.h"
#include "netuser.h"

#include <ctype.h>	/* for isprint() */

#include "usock.h"	/* 20Apr2021, maiko */

#define UNUSED(x) ((void)(x))   /* 15Apr2016, VE4KLM, trick to suppress any
                                 * unused variable warning, why are there so
                                 * many MACROs in this code, should convert
                                 * them to actual functions at some point. */
typedef struct varaparm {
	char *hostname;
	int port;
} VARAPARAM;

static int vara_fwd_connected = 0;	/* 20Apr2021, Maiko (VE4KLM), trying hfdd style again */

static int psock = -1;

static int dsock = -1;

int vara_ptt = 0;

int vara_buffer = -1;	/* 29May2021, Maiko */

static int vara_cmd_debug = 0;

static int vara_ip_bridge_mode = 0;		/* 01Sep2021, Maiko - toggle between access mode and ip bridge */

static int connectvara (VARAPARAM *ifpp)
{
    static int logcount = 0, logstage = 1;

    struct sockaddr_in fsocket;

    int s = -1;
/*
 * 31Aug2021, Maiko (VE4KLM), Interesting, forwarding with Winlink Express
 * came to life (it completed this time). I keep forgetting I put in NOIAC
 * option on sockets, but you have to tell JNOS to do that, which I failed
 * to do in the connect code below. General telnet does it already. That's
 * what I think is going on here anyways - seems to be working now ?
 */
#ifdef  JNOS_DEFAULT_NOIAC
/* #warning "default NO IAC processing in affect" */
        struct usock *up;
#endif
	if ((fsocket.sin_addr.s_addr = resolve (ifpp->hostname)) == 0L)
	{
		log (-1, "vara - host (%s) not found", ifpp->hostname);
        return -1;
    }
  
    fsocket.sin_family = AF_INET;
    fsocket.sin_port = ifpp->port;

	if ((s = j2socket (AF_INET, SOCK_STREAM, 0)) == -1)
	{
		log (-1, "vara - no socket");
        return -1;
	}
/*
 * 31Aug2021, Maiko (VE4KLM), This seems very important when talking
 * with the VARA modem over tcp/ip, I keep forgetting about the NOIAC
 * option that I put in 5 or so years ago, hopefully this is it !!!
 */
#ifdef  JNOS_DEFAULT_NOIAC
	if (!vara_ip_bridge_mode)
	{
    if ((up = itop (s)) == NULLUSOCK)
      log (-1, "telnet (itop failed) unable to set options");
	else
		up->flag |= SOCK_NO_IAC;
	}
#endif
	if (j2connect (s, (char*)&fsocket, SOCKSIZE) == -1)
	{
		/*
		 * 04May2021, Maiko (VE4KLM), Thanks to Bob (VE3TOK) who noticed a
		 * growing amount of winrpr_rx sockets - way too many of them ! So
		 * it's important to close the socket if we are unable to connect,
		 * same applies to agwpe and this new vara code for that matter.
		 */
		close_s (s);

		/*
		 * 26Nov2020, Maiko (VE4KLM), an idea to back off on the number of
		 * log entries created if the device is offline for long periods.
		 */
		if (!(logcount % logstage))
		{
			log (-1, "vara - connect failed, errno %d", errno);
			logstage *= 2;
            logcount = 0;
        }

		logcount++;

        return -1;
	}
  
	log (s, "vara [%s] - connected", ifpp->hostname);

	/*
	 * 26Nov2020, Maiko (VE4KLM), reset on connect, or we may not
	 * see subsequent connect failures in the logs for some time.
	 */
	logcount = 0;
	logstage = 1;

	return s;
}

#ifdef	REMOTE_RTS_PTT
/*
 * 28Jan2021, Maiko (VE4KLM), function to send UDP frame to a perl
 * listener I wrote that runs on the windows machine running VARA
 * modem, and triggers the RTS pin on that windows machine serial
 * port for PTT without needing Signal Link or any DRA board.
 *
 * Snippets taken from the fldigi.c code I wrote in 2015 !
 *
 * 29Jan2021, Added ifpp, so ip address is simply taken from
 * the attach vara command, most likely (as in my case), any
 * serial port will be on the same PC as VARA modem uses.
 *  (had hardcoded it before, call it lazy development)
 */
int trigger_RTS_via_perl_listener (VARAPARAM *ifpp, char *cmdstr)
{
	struct mbuf *bp;
	struct socket lsock, rsock;
    int len = strlen (cmdstr);

	lsock.address = INADDR_ANY;
	lsock.port = 8400;

	rsock.address = resolve (ifpp->hostname);
	// rsock.address = resolve ("10.8.10.6");
	rsock.port = 8400;

	if ((bp = alloc_mbuf (len)) == NULLBUF)
		return 0;

	/* do NOT use string copy, string terminator will corrupt memory */
    memcpy ((char*)bp->data, cmdstr, len);

    bp->cnt = (int16)len;

    return send_udp (&lsock, &rsock, 0, 0, bp, 0, 0, 0);
}
#endif	/* end of REMOTE_RTS_PTT */

static char *varacmdpending = (char*)0;

void vara_cmd (int xdev, void *p1, void *p2)
{
	struct iface *ifp = (struct iface*)p1;

	VARAPARAM *ifpp = (VARAPARAM*)p2;

	char buf[100];

	int len, save_errno;

#ifdef	DEBUG
        intlast_len = -2;
#endif
	log (-1, "vara command [%s:%d]", ifpp->hostname, ifpp->port);

	while (1)
	{
		/* Connect to control port of VARA tcp/ip server, retry every minute */

		if (psock == -1)
		{
			while ((psock = connectvara (ifpp)) == -1)
				j2pause (60000);

			sockmode (psock, SOCK_ASCII);
  
			seteol (psock, "\r");	/* Windows VARA HF v4.3.1 requires this */
		}

		j2alarm (1000);
		len = recvline (psock, buf, sizeof(buf) - 1);
		save_errno = errno;
		j2alarm(0);
#ifdef DEBUG
		if (last_len != len)
		{
			log (psock, "recvline %d %d", len, save_errno);
			last_len = len;
		}
#endif
		if (len > 0)
		{
			rip (buf);

			if (vara_cmd_debug)
				log (psock, "vara [%s]", buf);

			if (!strncmp (buf, "PTT ON", 6))
			{
#ifdef	REMOTE_RTS_PTT
				trigger_RTS_via_perl_listener (ifpp, buf); /* 28Jan2021, Maiko */
#endif
				vara_ptt = 1;
			}

			if (!strncmp (buf, "PTT OFF", 7))
			{
#ifdef	REMOTE_RTS_PTT
				trigger_RTS_via_perl_listener (ifpp, buf); /* 28Jan2021, Maiko */
#endif
				vara_ptt = 0;
			}

			/* 29May2021, Maiko (VE4KLM), Forgot Jose mentioned to me this back
			 * in January I think, it's an important number to keep an eye on,
			 * and since I'm stuck on the lzhuf portion of forwarding, perhaps
			 * this needs to be incorporated in the lzhuf portion ? We'll see.
			 *  (presently I am NOT actually using it - 01Sep2021, Maiko)
			 */
			if (!strncmp (buf, "BUFFER", 6))
				vara_buffer = atoi (buf+7);

			/*
			 * 16Jan2021, Maiko (VE4KLM), I should probably indicate the
			 * physical layer is UP when we are actually connected, not
			 * when the data stream is connected, elsewhere in the code.
			 *
			 * As well, source the ppp commands, so that I don't have to
			 * manually run a 'source ppp.nos' after I see the connect.
			 *
			 * Lastly, indicate physical layer is DOWN on disconnect.
			 *
			 */
			if (!strncmp (buf, "CONNECTED", 9))
			{
				/* 
				 * 01Sep2021, Maiko (VE4KLM), no more hard coding, one can now configure
				 * the VARA interface as an IP Bridge (PPP), or to simply use for direct
				 * access for forwarding of messages with other VARA based BBS systems.
				 */
				if (vara_ip_bridge_mode)
				{
					log (psock, "physical layer (PPP) is up");

					/* 15Jan2021, Maiko (VE4KLM), you gotta be kidding me :| */
					if (ifp->iostatus) (*ifp->iostatus)(ifp, PARAM_UP, 0);

					log (psock, "sourcing PPP configuration");

					/* Maiko, new breakout function in main.c */
					j2directsource (psock, "ppp.nos");

				}
				else
				{
					vara_fwd_connected = 1;	/* 20Apr2021, Maiko (VE4KLM), trying hfdd style again */

					log (psock, "vara link up");
				}
			}

			if (!strncmp (buf, "DISCONNECTED", 12))
			{
				/* 
				 * 01Sep2021, Maiko (VE4KLM), no more hard coding, one can now configure
				 * the VARA interface as an IP Bridge (PPP), or to simply use for direct
				 * access for forwarding of messages with other VARA based BBS systems.
				 */
				if (vara_ip_bridge_mode)
				{
					log (psock, "physical layer (PPP) is down");

					if (ifp->iostatus) (*ifp->iostatus)(ifp, PARAM_DOWN, 0);
				}
				else
				{
					vara_fwd_connected = 0;	/* 20Apr2021, Maiko (VE4KLM), trying hfdd style again */

					log (psock, "vara link down");
				}
			}
		}
		else if (len == -1)
		{
			if (save_errno != EALARM)
			{
				// log (-1, "vara command disconnected, errno %d", save_errno);
				log (-1, "resetting vara modem network interfaces");	/* 01Sep2021, Maiko, more informative */

				close_s (psock);
				psock = -1;

				/*
				 * 31Aug2021, Maiko (VE4KLM), Seems I have to reset both channels
				 * to the modem, I don't know why, but the behaviour suggests it
			 	 * is necessary, otherwise only 1 channel seems to come up ? AND
				 * it now appears I can consistently kick a session and reverse
				 * forward with Winlink Express works EVERY time now, unreal !
				 *  (this could be the working prototype now - very excited)
				 */
				close_s (dsock);
				dsock = -1;

				vara_fwd_connected = 0;	/* 20Apr2021, Maiko (VE4KLM), trying hfdd style again */
			}
			else if (varacmdpending)	/* An opportunity to issue a command */
			{
				j2send (psock, varacmdpending, strlen (varacmdpending), 0);
				j2send (psock, "\r", 1, 0);
				free (varacmdpending);
				varacmdpending = (char*)0;
			}
		}
	}
}

int dovaracmd (int argc, char **argv, void *p)
{
	if (argc < 2)
		getusage ("", argv[0]);

	/*
	 * 01Feb2021, Maiko (VE4KLM), Strange behavior more so on windows
	 * 10, give user option to reset entire TCP/IP interface, saves from
	 * having to restart JNOS, I don't like having to add this command,
	 * but it's the only way right now to 'reset stuff', sigh ...
 	 */
	else if (!strcmp (argv[1], "reset"))
	{
		close_s (psock);
		psock = -1;

		close_s (dsock);
		dsock = -1;
	}

	/*
	 * 21Jan2021, Maiko, Added debug toggle, the log can fill up.
	 */
	else if (!strcmp (argv[1], "debug"))
		vara_cmd_debug = !vara_cmd_debug;
	/*
	 * 01Sep2021, Maiko (VE4KLM), toggle vara mode, IP BRIDGE (PPP)
	 * versus my newly developed DIRECT ACCESS (using forward.bbs)
	 */
	else if (!strcmp (argv[1], "ipbridge"))
		vara_ip_bridge_mode = !vara_ip_bridge_mode;

	else if (psock == -1)
		tprintf ("not possible - vara modem is offline\n");

	else
	{
		varacmdpending = j2strdup (argv[1]);
		strupr (varacmdpending);
	}

	return 0;
}

void vara_rx (int xdev, void *p1, void *p2)
{
	struct iface *ifp = (struct iface*)p1;

	VARAPARAM *ifpp = (VARAPARAM*)p2;

	extern int ppp_init (struct iface*);

	log (-1, "vara data rx [%s:%d]", ifpp->hostname, ifpp->port);

	while (1)
	{
		/* Connect to the vara tcp/ip server, retry every minute */

		if (dsock == -1)
		{
			while ((dsock = connectvara (ifpp)) == -1)
				j2pause (60000);

			sockmode (dsock, SOCK_ASCII);

			seteol (dsock, "\r");	/* Windows VARA HF v4.3.1 requires this */

			/* 
			 * 01Sep2021, Maiko (VE4KLM), no more hard coding, one can now configure
			 * the VARA interface as an IP Bridge (PPP), or to simply use for direct
			 * access for forwarding of messages with other VARA based BBS systems.
			 */
			if (vara_ip_bridge_mode)
			{
				/*
				 * 14Jan2021, Maiko (VE4KLM), pass the socket to the ppp
				 * routines, which I modified to work with sockets now.
				 */
				ifp->dev = dsock;

				log (dsock, "initializing ppp interface");

				if (ppp_init (ifp))
				{
					log (-1, "ppp init failed");
					break;
				}
			}

			/*
			 * 17Jan2021, Maiko, Might as well automate initial
			 * commands like "mycall xxx", "listen on", whatever,
			 * when we see the control and data streams come up.
			 *
			 * That way there is no need to manually run them as
			 * I have been doing before I decided to add this :]
			 */
			log (psock, "sourcing vara configuration");

			/* Maiko, new breakout function in main.c */
			j2directsource (psock, "vara.nos");
		}
		else j2pause (10000);
	}

	log (-1, "vara data rx disconnected");
	close_s (dsock);
	dsock = -1;
}

int vara_attach (int argc, char *argv[], void *p)
{
	struct iface *ifp;

	VARAPARAM *ifpp, *ifpp2;
 
	if (if_lookup (argv[1]) != NULLIF)
	{
		tprintf (Existingiface, argv[4]);
		return -1;
	}

	/* Create structure of AGWPE connection parameters */

	ifpp = (VARAPARAM*)callocw (1, sizeof(VARAPARAM));

	ifpp->hostname = j2strdup (argv[3]);
	ifpp->port = atoi (argv[4]);

	ifpp2 = (VARAPARAM*)callocw (1, sizeof(VARAPARAM));

	ifpp2->hostname = j2strdup (argv[3]);
	ifpp2->port = ifpp->port + 1;

	/* Create interface structure and fill in details */

	ifp = (struct iface*)callocw (1, sizeof(struct iface));

	ifp->addr = Ip_addr;
	ifp->name = j2strdup (argv[1]);

	/* 08Jan2021, Maiko, Oops, would help to allocate hwaddr :| */
        ifp->hwaddr = mallocw(AXALEN);
        memcpy(ifp->hwaddr,Mycall,AXALEN);

        ifp->mtu = atoi(argv[2]);
	ifp->dev = 0;
	ifp->stop = NULL;

	setencap (ifp, "PPP");

	ifp->ioctl = NULL;
	ifp->show = NULL;
	ifp->flags = 0;
	ifp->xdev = 0;

	/* Link in the interface - important part !!! */
	ifp->next = Ifaces;
	Ifaces = ifp;

	/*
	 * Command Processor
	 */

	newproc ("vara_cmd", 1024, vara_cmd, 0, (void*)ifp, (void*)ifpp, 0);

	/*
	 * Data (Rx) Processor
 	 */

	ifp->rxproc = newproc ("vara_rx", 1024, vara_rx, 0, (void*)ifp, (void*)ifpp2, 0);

	return 0;
}

/*
 * 20Apr2021, Maiko (VE4KLM), a new vara_conn() function for forwarding code, this
 * is like the hfdd_conn() that I tried to make work, but keeping it separate for
 * the initial development, I may want to do some of this stuff different now ?
 *
 * Oh wow - successfully picked off a message waiting for me on a Winlink Express
 * system, using VARA HF P2P mode @ 10:30 pm tonight, really really awesome !
 *
 * Trying to send to Winlink Express however is not quite working, it keeps
 * on complaining about message not in correct format, giving a ByteOffset,
 * so not quite there yet, but this is VERY good news, I'm stoked !
 *
 * 01Sep2021, Maiko (VE4KLM), function args change - now passing src and dest calls
 */

int vara_connect (char *src, char *dst)
{
	/* 01Sep2021, Maiko, 9 cmd + 9 src call + 9 dst call + terminator = 28, make it 30 then */
	varacmdpending = malloc (30);

	sprintf (varacmdpending, "connect %s %s", src, dst);

	strupr (varacmdpending);

	/* wait for 'modem' to say there is a connection */
	while (!vara_fwd_connected)
		j2pause (1000);

  	/* use this as m->user in forward.c - will this work ? */
	return (dsock);
}

#endif	/* End of EA5HVK_VARA */
