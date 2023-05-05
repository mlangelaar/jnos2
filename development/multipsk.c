/*
 * Support for MULTIPSK (tcp/ip control) as a digital modem
 * Designed and Coded (February 2009) by Maiko Langelaar, VE4KLM
 * First official prototype source checked in March 01, 2009 
 *
 * (C)opyright 2009 Maiko Langelaar, VE4KLM
 *
 * For Amateur Radio use only (please) !
 */

#include "global.h"

#ifdef	MULTIPSK

#include "mbuf.h"
#include "iface.h"
#include "slip.h"
#include "trace.h"
#include "pktdrvr.h"
#include "devparam.h"

#include "mailbox.h"

#include "netuser.h"	/* 29Sep2019, Maiko (VE4KLM), struct breakouts */

typedef struct mpskcparm {
	char *hostname;
	int portnum;
} IFPMPSK;

/* 27Feb2009, Maiko, Use an existing function from slip.c module  */
extern struct mbuf *slip_decode (register struct slip *sp, char c);

/* 28Feb2009, Maiko, Use an existing function from slip.c module  */
extern struct mbuf *slip_encode (struct mbuf *bp, int usecrc);

static int local_sock = -1, psock = -1;

static int txt_xmitted = 0;		/* 02Nov2013, Maiko, for TX switch off */

static int connected = 0;

static int connectmpsk (IFPMPSK *ifpp)
{
	static int logcount = 0, logstage = 1;	/* 10May2021, Maiko */

    struct sockaddr_in fsocket;

    int s = -1;
  
	if ((fsocket.sin_addr.s_addr = resolve (ifpp->hostname)) == 0L)
	{
		log (-1, "multipsk - host (%s) not found", ifpp->hostname);
        return -1;
    }
  
    fsocket.sin_family = AF_INET;
    fsocket.sin_port = ifpp->portnum;
 
	if ((s = j2socket (AF_INET, SOCK_STREAM, 0)) == -1)
	{
		log (-1, "multipsk - no socket");
        return -1;
	}

	sockmode (s, SOCK_BINARY);
  
	if (j2connect (s, (char*)&fsocket, SOCKSIZE) == -1)
	{
		/*
		 * 10May2021, Maiko (VE4KLM), Thanks to Bob (VE3TOK) and John (VE1JOT)
		 * who both noticed a growing amount of agwpe sockets - way too many of
		 * them ! It's very important to close the socket if we can't connect.
		 * originally reported for winrpr, but used in several JNOS drivers.
		 *
		 * Also, I might as well put in the new 'minimum logging' code that
		 * I wrote for the winrpr.c module - backing off the log entries.
		 *
		 */
		close_s (s);

		if (!(logcount % logstage))
		{
			log (-1, "multipsk - connect failed, errno %d", errno);
			logstage *= 2;
			logcount = 0;
		}

		logcount++;

        return -1;
	}
  
	log (s, "multipsk [%s] - connected", ifpp->hostname);

	/* reset on connect or we may not see subsequent failures logged for some time */
	logcount = 0;
	logstage = 1;

	return s;
}

int mpsk_send (struct iface *iface, struct mbuf *bp)
{
	static char *iobuf = NULL;
	static int iomtu = -1;
	struct mbuf *tbp;
	int len;

	if (iomtu == -1 || iface->mtu > iomtu)
	{
		if (iomtu != -1)
			free (iobuf);

		iomtu = iface->mtu;
		iobuf = mallocw (iomtu);
	}

	if (psock != -1)	/* tcp/ip socket handle */
	{
		len = len_p (bp);

		/* this should never happen, but just to be safe !!! */
		if (len > iomtu)
			log (-1, "mpsk_send - packet (%d) larger then mtu", len);

		else if (dup_p (&tbp, bp, 0, len) != len)
			log (-1, "mpsk_send - dup_p failed");

		else
		{
			char *ptr, *ptr2, *iobuf2 = mallocw (len * 2);
			int cnt = 0;

			(void)pullup (&tbp, iobuf, len);

			ptr = iobuf2;
			ptr2 = iobuf;
		
			while (cnt < len)
			{
				*ptr++ = 22;
				*ptr++ = *ptr2++;
				cnt++;
			}	

			if (j2send (psock, iobuf2, 2*len, 0) < 1)
				log (-1, "mpsk_send - write errno %d", errno);

			free_p (tbp);
		}

	    free_p (bp);
	}

    return 0;
}

int mpsk_raw (struct iface *iface, struct mbuf *bp)
{
    struct mbuf *bp2, *bp3;

	struct slip *sp = &Slip[iface->xdev];
  
    bp2 = pushdown (bp, 1);
    bp2->data[0] = PARAM_DATA;

	dump (iface, IF_TRACE_OUT, sp->type, bp2);

    iface->rawsndcnt++;
    iface->lastsent = secclock();
  
    if ((bp3 = slip_encode (bp2, sp->usecrc)) == NULLBUF)
        return -1;

	mpsk_send (iface, bp3);

    return 0;
}

#define	TBUFLEN	80

/* 11Aug2013, Maiko, Made src point to pointer */
/* 10Aug2013, Maiko, new function to ignore spaces in string comparisons */ 
int j2_str_ws_case_cmp (/* unsigned */ char **src_p, char *key)
{
	/* unsigned */ char *src = *src_p;

    int startlooking = 0, mismatch = 0;

	while (*src && *key)
	{
		if (*src == ' ')
		{
			src++;
			continue;
		}

		if (*key == ' ')
		{
			key++;
			continue;
		}

		if (!startlooking)	/* 22Aug2013, Maiko */
		{
			/* jump ahead to a possible key */
			if (*src != *key)
			{
				src++;
				continue;
			}
			else startlooking = 1;
		}

		if (*src != *key)
			mismatch ++;

		src++;
		key++;
	}

	*src_p = src;

	return (mismatch);
}

/* 02Nov2013, Maiko, Send command to Multipsk */
static void send_command (char *cmdstr)
{
	char outbuf[5];

	sprintf (outbuf, "%c%s%c", 0x1a, cmdstr, 0x1b);

	j2send (psock, outbuf, strlen (outbuf), 0);
}

/* 31Oct2013, mailbox out function */

static void mbox_mpsk_out (int dev, void *n1, void *n2)
{
	int c, save_errno, len, maxlen = 20;

	char outdata[100], *ptr = outdata;

	while (1)
	{
		/*
		 * Same idea as in the hfdd routines, if we don't get anything from
		 * the keyboard (mailbox) side after 2 seconds, it's safe to assume
		 * there is nothing more to send to the remote side.
		 */
		j2alarm (2000);
		c = recvchar (local_sock);
		j2alarm (0);

		/*
		 * An EOF tells us that we did not receive anything from
		 * the keyboard or the forwarding mailbox for 2 seconds,
		 * or it tells us that we need to terminate this proc !
		 */
		if (c == EOF)
		{
			save_errno = errno;

			/* 10Feb2007, Maiko, Actually if the following happens,
			 * then very likely a mailbox or session died, in which
			 * case (now that we want this to not die), we need to
			 * do cleanup - .....
			 */
			if (save_errno != EALARM)
			{
				log (-1, "errno %d, mailbox probably shutdown", save_errno);
				break;
			}
		}
		else
		{
			*ptr++ = 0x19;	/* Patrick preceeds TEXT bytes with CHR(25) */

			*ptr++ = c &0xff;
		}			

		/* Calculate the length of data in the outgoing buffer */
		len = (int)(ptr - outdata);

		if (len && ((c == EOF) || (len > maxlen)))
		{
			// send the data to MULTIPSK system !!! (todo)

			if (j2send (psock, outdata, len, 0) < 1)
			{
				save_errno = errno;

				log (-1, "mbox_mpsk_out, j2send errno %d", save_errno);
			}

			ptr = outdata;	/* make sure we start from beginning again */

			/* 02Nov2013, Put it in TX */
			if (c == EOF)
			{
				log (-1, "turn on transmitter");
				send_command ("TX");
			}

			continue;
		}
	}

	connected = 0;	/* important for us to take future connects */
}

static void launch_mbox (char *callsign)
{
	int mbxs[2];

	if (j2socketpair (AF_LOCAL, SOCK_STREAM, 0, mbxs) == -1)
	{
		log (-1, "socketpair failed, errno %d", errno);
		return;
	}

	seteol (mbxs[0], "\n");
	seteol (mbxs[1], "\n");

	sockmode (mbxs[1], SOCK_ASCII);

	strlwr (callsign);	/* 15Jul2009, Maiko, Keep it lower case please ! */

	newproc ("mpskmbx", 8192, mbx_incom, mbxs[1],
		(void*)TTY_LINK, (void*)callsign, 0);

    local_sock = mbxs[0];

	// create keyboard handler (noted 30Oct2013) !!!

    newproc ("mpskmbx_out", 1024, mbox_mpsk_out, 0, 0, 0, 0);
}


void mpsk_rx (int xdev, void *p1, void *p2)
{
	struct iface *ifp = (struct iface*)p1;

	IFPMPSK *ifpp = (IFPMPSK*)p2;

	struct slip *sp;
    struct mbuf *bp;

    int c;

	/* 01Oct2019, Maiko, compiler format overflow warning, so changed keystr from 20 to TBUFLEN+1+10 */
    char tbuf[TBUFLEN+1], keystr[TBUFLEN+1+10], *sptr, *cptr;
    int save_errno, eol = 0;

    sp = &Slip[ifp->xdev];

	log (-1, "multipsk listener [%s:%d]", ifpp->hostname, ifpp->portnum);

	/* 08Aug2013, Maiko, Build a key string to help find a connect attempt ! */
	pax25 (tbuf, Mycall);
	for (sptr = tbuf; *sptr && *sptr != '-'; sptr++);
	*sptr = '\0';
	sprintf (keystr, "JNOS %s DE ", tbuf);
	log (-1, "key string [%s]", keystr);

	while (1)
	{
		/* Connect to the multipsk tcp/ip server, retry every minute */

		if (psock == -1)
		{
			while ((psock = connectmpsk (ifpp)) == -1)
				j2pause (60000);
		}

		/* Handle incoming data stream */

		bp = NULL;

		while (1)
		{

		/* 22Aug2013, Maiko, Use inactivity timer to determine when we should
		 * try and parse the text buffer for possible connects and/or data
		 */
			j2alarm (2 * 1000);	
		    c = recvchar (psock);
			save_errno = errno;
			j2alarm (0);

			/* see if this is a timeout, if so examine the text buffer */
			if (c == -1)
			{
				if (save_errno == EALARM)
				{
					if (eol)
					{
						log (-1, "text buffer [%s]", tbuf);

						eol = 0;	/* don't need it anymore, start over */

						sptr = tbuf;

						/* look for the key string constructed earlier */
						if (!connected && !j2_str_ws_case_cmp (&sptr, keystr))
						{
							/* 08Aug2013, Maiko, get remote callsign */
							if (*sptr)
							{
								for (cptr = sptr; *cptr && *cptr != ' '; cptr++); *cptr = '\0';

								log (-1, "[%s] wants to connect", sptr);

								launch_mbox (sptr);

								connected = 1;	/* 30Oct2013 */
							}
						}
						else if (connected)
						{
							usputs (local_sock, tbuf);

							/* 02Nov2013, Maiko, need this or mailbox does not
							 * take the command */
							usputs (local_sock, "\n");

							usflush (local_sock);	
						}
					}
					/* 02Nov2013, Use this to switch it back to RX */
					else if (txt_xmitted)
					{
						log (-1, "switch back to receiver");
						send_command ("RX");
						txt_xmitted = 0;
					}
				}
				else break;		/* break out of the loop, and terminate */
			}
			else

			if (c == 0x16)	/* Patrick preceeds KISS bytes with CHR(22) */
			{
				if ((c = recvchar (psock)) == -1)
					break;

				/*
				 * 27Feb2009, Maiko, Use existing functions, no point
				 * reinventing the wheel, it's there already. Just need
				 * to make it non-static in slip.c, so that we can call
				 * it from here. This should work nicely, simple code.
				 */

				if ((bp = slip_decode (sp, (char)c)) == NULLBUF)
					continue;   /* More to come */

				if (net_route (sp->iface, sp->type, bp) != 0)
				{
					free_p (bp);
					bp = NULL;
				}

			}
			else if (c == 0x1d)	/* Patrick preceeds TEXT bytes with CHR(29) */
			{
				if ((c = recvchar (psock)) == -1)
					break;

				if (eol == TBUFLEN)
				{
					log (-1, "text buffer full, start from scratch");
					eol = 0;	/* reset the buffer */
				}
				else
				{
					tbuf[eol] = (unsigned char)c;
					eol++;
					tbuf[eol] = '\0';	/* perpetual string terminator */
				}
			}
			else if (c == 0x1c)	/* 02Nov2013, CHR(28) means it got transmitted */
			{
				if ((c = recvchar (psock)) == -1)
					break;

				txt_xmitted++;
			}
			else log (-1, "ctrl byte %d [%c]", c, c);
		}

		log (-1, "multipsk disconnected");
		close_s (psock);
		psock = -1;
	}
}

int mpsk_attach (int argc, char *argv[], void *p)
{
	struct iface *ifp;
	struct slip *sp;
	IFPMPSK *ifpp;
	int xdev;
 
	if (if_lookup (argv[1]) != NULLIF)
	{
		/* 04May2023, Michael Ford (WZ0C), should be argv[1], not argv[4] */
		tprintf (Existingiface, argv[1]);
		return -1;
	}

	/* Create structure of Multipsk connection  parameters */

	ifpp = (IFPMPSK*)callocw (1, sizeof(IFPMPSK));

	ifpp->hostname = j2strdup (argv[2]);
	ifpp->portnum = atoi (argv[3]);

	/* Create interface structure and fill in details */

	ifp = (struct iface*)callocw (1, sizeof(struct iface));

	ifp->addr = Ip_addr;
	ifp->name = j2strdup (argv[1]);
	ifp->mtu = 256;
	ifp->dev = 0;
	ifp->stop = NULL;

	if (ifp->hwaddr == NULLCHAR)
		ifp->hwaddr = mallocw (AXALEN);

	memcpy (ifp->hwaddr, Mycall, AXALEN);

	setencap (ifp, "AX25");

	ifp->ioctl = NULL;
	ifp->raw = mpsk_raw;
	ifp->show = NULL;
	ifp->flags = 0;

	/*
	 * 28Feb2009, Maiko, Crap, it looks like the Slip[] and xdev and dev are
	 * so interlocked into the trace (dump) routines, that dump will actually
	 * crash (and probably other stuff) if I just calloc a SLIP structure, as
	 * I tried to do before. Looks like I'll have to stick with looping thru
	 * the Slip[] array for an empty slot, until I figure out another way to
	 * do this. It works ! BUT I don't like how it's tied into everything.
	 */
	for (xdev = 0; xdev < SLIP_MAX; xdev++)
	{
		sp = &Slip[xdev];
		if (sp->iface == NULLIF)
			break;
	}
	if (xdev >= SLIP_MAX)
	{
		j2tputs ("Too many slip devices\n");
		return -1;
	}

	ifp->xdev = xdev;
	sp->iface = ifp;
	sp->kiss[ifp->port] = ifp;
	sp->type = CL_KISS;
	sp->polled = 0;
	sp->usecrc = 0;

	/* Link in the interface - important part !!! */
	ifp->next = Ifaces;
	Ifaces = ifp;

	/*
	 * Create a listener process
	 *
	 * The connect to the multipsk tcp/ip server is inside the
	 * listener process. If we do it here, JNOS will hang on the
	 * attach command - remember 'attach' must always return.
	 */
	ifp->rxproc = newproc ("mpsk_rx", 1024, mpsk_rx,
			0, (void*)ifp, (void*)ifpp, 0);

	return 0;
}

#endif	/* End of MULTIPSK */
