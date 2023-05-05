
/*
 * 18Jan2005, Time to move some of the common functions into their own
 * source module. This is my attempt to better organize and structure
 * the HF Digital Device (HFDD) source code. Maiko Langelaar, VE4KLM
 *
 * 02Feb2005 - only one keyboard handler now to handle traffic from
 * the keyboard (or the forwarding mailbox) to the HFDD (tnc), it is
 * the function called keyboard_to_hfdd (). New HFDD_PARAMS struct.
 *
 */

#include "global.h"

#ifdef	HFDD

#include "proc.h"
#include "socket.h"
#include "iface.h"
#include "devparam.h"
#include "unixasy.h"
#include "asy.h"
#include "session.h"
#include "usock.h"
#include "cmdparse.h"

#include "hfdd.h"

static void keyboard_to_hfdd (int dev, void *n1, void *n2);

/* socket used for forwarding to other systems (forward.bbs) */
int hfdd_fwdsock = -1;

/* flag to indicate whether the HFDD device is connected or not */
int hfdd_conn_flag = 0;

/* 17Mar2007, Maiko, New global debug flag for HFDD stuff */
int hfdd_debug = 0;

/* 20Jan2005, Maiko, functions to tell us HFDD iface type */
int hfdd_is_ptc (char *iface)
{
	return (memcmp ("ptc", iface, 3) == 0);
}

int hfdd_is_dxp (char *iface)
{
	return (memcmp ("dxp", iface, 3) == 0);
}

int hfdd_is_kam (char *iface)
{
	return (memcmp ("kam", iface, 3) == 0);
}

/* 22Apr2008, Maiko, Now trying to support the AEA PK232 modem */
int hfdd_is_pk232 (char *iface)
{
	return (memcmp ("232", iface, 3) == 0);
}

#ifdef WINMOR
/* 07Apr2010, Maiko, Now trying to support the WINMOR Sound Card modem */
int hfdd_is_winmor (char *iface)
{
	return (memcmp ("mor", iface, 3) == 0);
}
#endif

int hfdd_iface (char *iface)
{
	/* 23Apr08, Maiko, Added pk232 */
	/* Apr2010, Maiko, Added WINMOR sound card tnc support */
	return (hfdd_is_ptc (iface) ||
		hfdd_is_dxp (iface) ||
		hfdd_is_pk232 (iface) ||
#ifdef WINMOR
		hfdd_is_winmor (iface) ||
#endif
		hfdd_is_kam (iface));
}

/*
 * 07Jan2005, Maiko, The idea is that when JNOS wants to forward to a remote
 * system using an HFDD, we will assign a plain old local socket and use it
 * to talk between the HFDD modules and the forwarding processes. This idea
 * is based on the original JNOS tipmail server (a great way to do it).
 */

static int fwds[2] = { -1, -1 };

int hfdd_connect (char *cc)
{
	char cbuf[20], *cptr, *ccptr, **pargv;

	int cnt;

	pargv = (char **)callocw(3,sizeof(char *));

	/* just to get this working, it needs better structure */
	for (ccptr = cc, cnt = 0; cnt < 3; cnt++)
	{
		cptr = cbuf;

		while (*ccptr && *ccptr != ' ')
			*cptr++ = *ccptr++;

		*cptr = 0;

		/* 20Mar2006, Maiko, Remove any EOL (end of line) chars */
		if (cnt == 2)
			rip (cbuf);

		if (hfdd_debug)
			log (-1, "cbuf [%s]", cbuf);

		pargv[cnt] = j2strdup (cbuf);

		if (*ccptr == ' ')
			ccptr++;
	}

	hfdd_conn_flag = 0; /* 03Feb2005, MAKE SURE this is set !!! */

	/*
	 * 16Apr2007, Maiko (VE4KLM), Identify which device interface is, now
	 * that I have the KAM connect working nicely, I need to add the other
	 * devices, PTC now, the DXP will be later.
	 */

	/* 17Feb2007, Maiko, New way to conn */
	if (hfdd_is_kam (pargv[1]))
		kam_make_call (pargv[2]);
	else if (hfdd_is_ptc (pargv[1]))
		ptc_make_call (pargv[2]);
	/* 23Apr2008, Maiko, Now support AEA PK232 modem */
	/* 10Sep2008, Maiko, New queue system for PK232, different function */
	else if (hfdd_is_pk232 (pargv[1]))
		pk232_connect (pargv[1], pargv[2]);
	else if (hfdd_is_dxp (pargv[1]))
		log (-1, "dxp not supported YET ...");
#ifdef WINMOR
	/* 07Apr2010, Maiko, Support the WINMOR Sound Card modem */
	else if (hfdd_is_winmor (pargv[1]))
		winmor_make_call (pargv[2]);
#endif
	/*
	 * 16Feb2007, Maiko, Need a function to request connect from hfddsrv
	 * used to be hfdd_console here - no more, since hfddsrv fulltime now
	 */

	if (hfdd_debug)
		log (-1, "trying [%s]", pargv[2]);

	/* 04Mar2006, Maiko, Fix up this wait for connection ... */

	while (!hfdd_conn_flag)
		j2pause (100);

	if (hfdd_conn_flag == -1)
	{
		if (hfdd_debug)
			log (-1, "[%s] no connection", pargv[2]);

		return -1;
	}

	if (hfdd_debug)
		log (-1, "[%s] connected - assign socket pair", pargv[2]);

	/* use socket pair like bbs side, 08Jan2005 */
	if (j2socketpair (AF_LOCAL, SOCK_STREAM, 0, fwds) == -1)
	{
		log (-1, "socketpair failed, errno %d", errno);
		return -1;
	}

	seteol (fwds[0], "\r");
	seteol (fwds[1], "\r");
/*
	seteol (fwds[0], "\r\n");
	seteol (fwds[1], "\r\n");
*/
	hfdd_fwdsock = fwds[0]; /* this is for our local access */

	sockmode (fwds[1], SOCK_ASCII);

	return (fwds[1]);	/* this one becomes m->user in forward.c */
}

#ifndef HASH_DUMP_CODE

static int16 dump_hash, last_dump_hash = 0, last_dump_cnt = 0;  /* 27Aug08 */

int16 dhash (unsigned char *ptr)
{
	int16 retval = MAXINT16;

	while (*ptr)
	{
		/* xor high byte with accumulated hash */
		retval ^= (*ptr++) << 8;

		/* xor low byte with accumulated hash */
		retval ^= (*ptr++);
	}

	return retval;
}

#endif

void asy_dump (int len, unsigned char *ptr)
{
	static char dumpbuffer[300];

	char *sptr = dumpbuffer;

	int cnt = 0;

	if (!hfdd_debug || !len) return;

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


#ifndef	HASH_DUMP_CODE
/*
	if (last_dump_hash && last_dump_cnt == 10)
	{
		log (-1, "asy_dump - prev msg repeated %d times", last_dump_cnt);
		last_dump_cnt = 0;
	}
*/

/*
 * 27Aug08, Maiko, Only display if dump content changes, saves space, easier
 * to read the log file. At least I say how many times the message showed up.
 */
	dump_hash = dhash ((unsigned char*)dumpbuffer);
/*
	log (-1, "asy_dump DH %d LDH %d CNT %d LEN %d STRLEN %d",
		dump_hash, last_dump_hash, cnt, len, strlen(dumpbuffer));
*/

	/* always show data out */
	if (dump_hash != last_dump_hash)
	{
		log (-1, "asy_dump (%d) [%s]", len, dumpbuffer);

		last_dump_hash = dump_hash;

		last_dump_cnt = 0;
	}
	else last_dump_cnt++;

#endif

}

/*
 * 20Jan2005, Maiko, Moved into this module, since it's common.
 *
 * 09Sep2008, Maiko, This function scheduled to be deleted at some
 * point. The new PK232 now uses a QUEUE version (NEW_hfdd_send),
 * which I will convert the other HF modems to at some point.
 */
void hfdd_send (int dev, char *data, int len)
{
	struct mbuf *bp;

	bp = pushdown (NULLBUF, len);

	memcpy (bp->data, data, len);

	/* debugging output -> Aug 26, 2008, Maiko */
	asy_dump (len, (unsigned char*)data);

	asy_send (dev, bp);
}

/* 16Feb2007, Maiko, Null functions for now */

void hfdd_console (int argc, void *n1, void *n2)
{
	log (-1, "null function at this time");
}

int hfdd_client (int argc, char **argv, void *p)
{
	log (-1, "null function at this time");

	return 0;
}

/*
 * 20Jan2005, Maiko, Only one client for all now with hooks for
 * the different interfaces. No point duplicating code, and this
 * also standardizes specific aspects of the HFDD functionality,
 * regardless of the type of interface (ie, Clover or Pactor).
 */

static HFDD_PARAMS hfddp;	/* 11Mar2005, Maiko, Static, not local */

#ifdef WINMOR
static struct iface ifdummy;
#endif

int hfddsrv (int argc, char **argv, void *p)
{
	struct session spdummy, *sp = &spdummy;
	struct iface *ifp;
	struct asy *ap;
	char *ifn;

	/* 07Apr2010, Maiko, Winmor sound card tnc support */
	int notwinmor = 1;

#ifdef WINMOR
	ifp = &ifdummy;
	ifdummy.name = j2strdup ("winmor");
#endif
	/*
	 *  HFDD_PARAMS	hfddp;
	 *
	 * wonder if this getting destroyed when the client terminates is
	 * causing crashes, since a process might still be alive that expects
	 * this. Make it a static for now to see if that solves crashing !
	 */

	if (p != (void*)0)
		sp = (struct session*)p;

#ifdef WINMOR
	/* 07Apr2010, Maiko, No physical TTY for the WINMOR device */
	notwinmor = strcmp ("mor", argv[1]);
#endif

	if (notwinmor)
	{
		if (hfdd_debug)
			log (-1, "argc %d iface %s call %s", argc, argv[1], argv[2]);

    	if ((ifp = if_lookup (argv[1])) == NULLIF)
		{
        	tprintf (Badinterface, argv[1]);
        	return 1;
    	}

    	ap = &Asy[ifp->dev];
    	if (ifp->dev >= ASY_MAX || ap->iface != ifp)
		{
        	tprintf ("Interface %s not asy port\n", argv[1]);
        	return 1;
    	}

    	if (ifp->raw == bitbucket)
		{
        	tprintf ("hfdd session already active on %s\n", argv[1]);
        	return 1;
    	}

    	/* Save output handler and temporarily redirect output to null */
    	ifp->raw = bitbucket;
	}

   	/* Put tty into raw mode */
   	sp->ttystate.echo = 0;
   	sp->ttystate.edit = 0;
   	sockmode (sp->output, SOCK_BINARY);

	/*
	 * 03Jun2007, Maiko, No more keyboard mode for now. This will all be
	 * for mail forwarding only. The keyboard mode never worked very well,
	 * never got developed to it's fullest potential, and introduced some
	 * technical and logistical challenges just not worth doing (yet) !
	 */

	hfddp.keyboard = 0;	/* always from mailbox now, no keyboard user mode */

	hfddp.call = j2strdup (argv[2]);
	hfddp.iface = j2strdup (argv[1]);

	if (notwinmor)
	{
 	  /* 15Feb2005, Maiko, now using device params to configure this */
	  hfddp.pactor = (ap->flags & ASY_PACTOR);
	}
	else hfddp.pactor = 1;	/* 09Apr2010, Maiko, Automatic apparently */

	hfdd_fwdsock = -1;	/* 12Mar2005, Maiko, Make sure of this !!! */

	/*
	 * start up the keyboard handler (as of 02Feb2005, there is only
	 * only one keyboard handler now for all devices, the device specific
	 * stuff is now contained within the handler, instead of having a
	 * separate keyboard handler for each device. More standardized !
	 */
	ifn = if_name (ifp, " keybd");
	sp->proc1 = newproc (ifn, 1024, keyboard_to_hfdd, ifp->dev, sp, &hfddp, 0);
	free (ifn);

    ifn = if_name (ifp, " modem");
    chname (Curproc, ifn);
    free (ifn);

    sp->proc = Curproc;	/* important for alert */
  
    /* bring the line up (just in case) */
    if (ifp->ioctl != NULL)
        (*ifp->ioctl)(ifp, PARAM_UP, TRUE, 0L);

	if (hfdd_debug)
		log (-1, "start the modem machine");

	/*
	 * 22Jan2005, Maiko, Added 'realuser' parameter to the xxx_machine
	 * functions, so that tprintf()'s are suppressed, and the j2alarm()
	 * calls are invoked. Forwarding and having a real user at the
	 * keyboard have slightly different input/output requirements.
	 */
	if (hfdd_is_ptc (hfddp.iface))
		ptc_machine (ifp->dev, hfddp.call);
	else if (hfdd_is_dxp (hfddp.iface))
		dxp_machine (ifp->dev, &hfddp);
	/* 05Feb2007, Maiko, Switched parameters passed to function */
	else if (hfdd_is_kam (hfddp.iface))
		kam_machine (ifp->dev, &hfddp);
	/* 22Apr2008, Maiko, Now support AEA PK232 modem */
	else if (hfdd_is_pk232 (hfddp.iface))
		pk232_machine (ifp->dev, &hfddp);
#ifdef WINMOR
	/* 07Apr2010, Maiko, Support the WINMOR Sound Card modem */
	else if (hfdd_is_winmor (hfddp.iface))
		winmor_machine (ifp->dev, &hfddp);
#endif

	/* 08Feb2007, Maiko, MODEM machine will now never die !!! */

	if (hfdd_debug)
		log (-1, "left the modem machine");

	return 0;
}

/* 17Feb2007, Maiko, Wrapper function so we can use the newproc() call */
void hfddsrv2 (int argc, void *n1, void *n2)
{
	char **iface = (char**)n1;

	hfddsrv (argc, iface, n2);
}

/* 10Jun2005, Maiko, One changeover function to avoid duplicate code */
static void hfdd_changeover (int dev, char *iface, int force)
{
	char command[10];

#ifdef WINMOR
	if (hfdd_is_winmor (iface))
		winmor_changeover (force);
	else
#endif
    if (hfdd_is_ptc (iface))
		ptc_changeover (dev, command, force);

	else if (hfdd_is_dxp (iface))
		dxp_changeover (dev, force);

	else if (hfdd_is_kam (iface))
		kam_changeover (dev, command, force);

	/* 23Apr08, Maiko (VE4KLM), Now supporting pk232 */
	else if (hfdd_is_pk232 (iface))
		pk232_changeover (dev, command, force);
}

/*
 * 02Feb2005, Maiko, Make this one function for all modem types,
 * no need to have a dxp_out, or ptc_out, etc - this will be easier
 * to maintain and we have a standard way of doing it then.
 */
static void keyboard_to_hfdd (int dev, void *n1, void *n2)
{
	struct session *sp = (struct session*)n1;

	HFDD_PARAMS *hfdd_params = (HFDD_PARAMS*)n2;

	int soc, save_errno, c, maxlen, len;

	char outdata[300], *ptr = outdata;

	/* initialize the changeover control variable */
	int changeover = hfdd_params->pactor;

	int changeoverdone = 0;	/* 25May2005 */

	if (hfdd_debug)
		log (-1, "entry into keyboard_to_hfdd");

	while (1)
	{
		if (hfdd_debug)
			log (-1, "waiting for forwarding socket to be assigned");

		while (hfdd_fwdsock == -1)
		{
			/*
			 * We might be told to die nicely through an ALERT, note
			 * that pause returns a zero after normal wait is over, but
			 * will return a -1 for other exceptions (like ALERTS).
			 */
			if (j2pause (1000) == -1)
			{
				log (-1, "keyboard handler alerted - die nicely");
				sp->proc1 = NULLPROC;
				return;
			}
		}

		soc = hfdd_fwdsock;

	if (hfdd_debug)
		log (-1, "entering the read-from-keyboard loop");

	/* 03Jun07, Maiko, Oops, these need to be reset with Xtra while loop */
	changeoverdone = 0;
	ptr = outdata;


	/* 21Sep08, Maiko, Don't think we need this minimum anymore */
#ifdef	DONT_COMPILE
	/*
	 * 24Jul08, Maiko, The 'maxlen' was initially put in to help me debug
	 * a problem with the new PK232 driver code. However, I should probably
	 * be setting the limit based on the type of hardware, instead of using
	 * a generic value like 64 - which was the maximum regardless of modem.
	 */
    if (hfdd_is_pk232 (hfdd_params->iface))
        maxlen = 16;
    else
#endif
        maxlen = 250;	/* let's try 128 instead */

	while (1)
	{
		/*
		 * The whole idea here is that if we don't get anything from
		 * the keyboard or our forwarding mailbox after 2 seconds, I
		 * think it's safe to assume there is nothing more to send to
		 * the remote side. In the case of Pactor, we use this to make
		 * the changeover so that the remote end can then send to us.
		 */

		j2alarm (2000);
    	c = recvchar (soc);
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
				if (hfdd_debug)
				{
					log (-1, "errno %d reading from mailbox or keyboard",
						save_errno);

					log (-1, "mailbox probably shutdown !!!");
				}

				/* 10Feb2007, Maiko, Force disconnect just to be safe */

#ifdef WINMOR
			/* 12Apr2010, maiko */
				if (hfdd_is_winmor (hfdd_params->iface))
					set_winmor_disc_flag ();
				else
#endif
				if (hfdd_is_ptc (hfdd_params->iface))
					set_ptc_disc_flag ();
				else if (hfdd_is_dxp (hfdd_params->iface))
					dxp_disconnect (dev);
				else if (hfdd_is_kam (hfdd_params->iface))
					set_kam_disc_flag ();
				/* 23Apr08, Maiko (VE4KLM), Added support for pk232 */
				else if (hfdd_is_pk232 (hfdd_params->iface))
					pk232_disconnect (dev);	/* 09Sep08, Maiko, New way */

				hfdd_fwdsock = -1;	/* socket no longer valid */

				hfdd_conn_flag = 0;		/* Set as disconnected !!! */

				break;	/* break out 10Feb2007 to wait for new socket */
			}
		}

		/*
		 * 03Jun07, Maiko, If not connected, then we should not
		 * even be in this while loop anymore. Leave and wait for
		 * a new mailbox to be created.
	 	 */

		if (!hfdd_conn_flag)
		{
			if (hfdd_debug)
				log (-1, "no longer connected, no point reading mailbox");

			hfdd_fwdsock = -1;	/* socket no longer valid */

			hfdd_conn_flag = 0;		/* Set as disconnected !!! */

			break;
		}

		/* Do not put the EOF into the outgoing buffer */
		if (c != EOF)
		{
			/* log (-1, "(%c)", (char)c); 13Apr2010, Maiko, Debugging */

			/* 05Oct06, Maiko, Forgot about escape chars - DXP only */
			if (hfdd_is_dxp (hfdd_params->iface))
			{
				/*
				 * DXP sees 0x80 and 0x81 as special characters, so we
				 * must first send $81 as an escape, then $80 or $81.
				 */
				if (c == 0x80 || c == 0x81)
				{
					if (hfdd_debug)
						log (-1, "escape character");

					*ptr++ = 0x81;
				}

				*ptr++ = c & 0xff;
			}

			/* 10Feb07, Maiko, Escape characters for Kantronics */
			else if (hfdd_is_kam (hfdd_params->iface))
			{
				/*
				 * KAM sees FEND or FESC as special characters, so
				 * we actually replace them with sequences instead.
				 */
				if (c == 0xc0)
				{
					if (hfdd_debug)
						log (-1, "escape (fend) character");

					*ptr++ = 0xdb;
					*ptr++ = 0xdc;
				}
				else if (c == 0xdb)
				{
					if (hfdd_debug)
						log (-1, "escape (esc) character");

					*ptr++ = 0xdb;
					*ptr++ = 0xdd;
				}
				else *ptr++ = c & 0xff;
			}
			/* 30May08, Maiko, Escape character for AEA */
			else if (hfdd_is_pk232 (hfdd_params->iface))
			{
				/* 17Jun08, Maiko, Corrected - 3 characters require escape */
				if (c == 0x01 || c == 0x10 || c == 0x17)
				{
					if (hfdd_debug)
						log (-1, "escape (dle) character");

					*ptr++ = 0x10;
				}

				*ptr++ = c & 0xff;
			}

			/* 10Feb07, Maiko, I don't believe SCS has escape chars */
			else *ptr++ = c & 0xff;
		}

		/* Calculate the length of data in the outgoing buffer */
		len = (int)(ptr - outdata);

		/*
		 * 24Jul08, Do not send any more than 'maxlen' bytes at a time
		 * to the tnc. Before this, the maximum length was hardcoded to
		 * a value of 64 bytes (regardless of the type of modem).
		 */
        if (len > maxlen)
		{
			if (!changeoverdone)
			{
				hfdd_changeover (dev, hfdd_params->iface, 1);
				changeoverdone = 1;
			}

			if (hfdd_debug)
				log (-1, "waiting for us to be ISS ...");
           /*
            * 20Sep2006, Maiko, Don't send out any data until the
            * modem tells us that we are indeed the ISS (sender). I
			* found out that failure to wait results in lost data.
            */

            while (!hfdd_iss ())
			{
				j2pause (100);	/* putting this in will free up jnos */
              	pwait (NULL);
			}

			if (hfdd_debug)
				log (-1, "we are now ISS - sending %d bytes", len);

#ifdef WINMOR
		/* 09Apr2010, Maiko, Finally got back to backs hooked up, play time */
			if (hfdd_is_winmor (hfdd_params->iface))
				winmor_send_data (outdata, len);

			else
#endif
			if (hfdd_is_ptc (hfdd_params->iface))
				ptc_send_data (dev, outdata, len);

			else if (hfdd_is_dxp (hfdd_params->iface))
				hfdd_send (dev, outdata, len);

			else if (hfdd_is_kam (hfdd_params->iface))
				kam_send_data (dev, outdata, len);

			/* 29Apr2008, Maiko, Finally doing the pk232 modem */
			else if (hfdd_is_pk232 (hfdd_params->iface))
				pk232_send_data (dev, outdata, len);

			ptr = outdata;	/* make sure we start from beginning again */

			continue;
		}

		/*
		 * If forwarding, send the data if we get an EOF indicating that
		 * we have not received any data within 2 seconds, OR if there is
		 * a real user at the keyboard, send if a Carriage Return has been
		 * hit by the user, OR if we get an EOF indicating this process
		 * is about to be terminated (in which case we have flush any
		 * remaining data in the output buffer.
		 */
		if (c == EOF)
		{
			if (len)
			{
				if (!changeoverdone)
				{
					hfdd_changeover (dev, hfdd_params->iface, 1);
					changeoverdone = 1;
				}

				if (hfdd_debug)
					log (-1, "waiting for us to be ISS ...");
				/*
				 * 20Sep2006, Maiko, Don't send out any data until the
				 * modem tells us that we are indeed the ISS (sender). I
				 * found out that failure to wait results in lost data.
				 */

            	while (!hfdd_iss ())
				{
					j2pause (100);	/* putting this in will free up jnos */
              		pwait (NULL);
				}

				if (hfdd_debug)
					log (-1, "we are now ISS - sending %d bytes", len);

#ifdef WINMOR
			/* 09Apr2010, Maiko, got back to backs hooked up, play time */
				if (hfdd_is_winmor (hfdd_params->iface))
					winmor_send_data (outdata, len);

				else
#endif
				if (hfdd_is_ptc (hfdd_params->iface))
					ptc_send_data (dev, outdata, len);

				else if (hfdd_is_dxp (hfdd_params->iface))
					hfdd_send (dev, outdata, len);

				else if (hfdd_is_kam (hfdd_params->iface))
					kam_send_data (dev, outdata, len);

				/* 29Apr2008, Maiko, Finally doing the pk232 modem */
				else if (hfdd_is_pk232 (hfdd_params->iface))
					pk232_send_data (dev, outdata, len);

				ptr = outdata;	/* reset output buffer pointer !!! */
			}

			/* 30May2008, Maiko, We need to do this whether len is 0 or not,
			 * ran into a case where len = 0, so previously we would never let
			 * the final changeover to the remote occur, and the remote would
			 * just sit there waiting and waiting. Discovered this by accident
			 * while testing my pk232 hfdd code -> chance happening len = 0.
			 * NOTE : only do this once on the first len = 0, use the change
			 * overdone flag to make sure we did the forced one first !
			 */
			if (changeoverdone)
			{	
				if (changeover)
					hfdd_changeover (dev, hfdd_params->iface, 0);
			}

			changeoverdone = 0;	/* 25May2005 */
		}
	}

	}	/* NEW WHILE(1) LOOP - 10Feb2007, Maiko */

	/* 10Feb2007, Maiko, Let's try staying inside keyboard loop now */

	log (-1, "left the read-from-keyboard loop, errno %d, save_errno %d",
		errno, save_errno);
}

/*
 * 06Mar2005, Maiko, The 'ptc_mbox' and 'dxp_mbox' functions
 * are the same, no point duplicating code, make it a generic
 * function used by all HF devices. Extra argument added to
 * pass the integer socket values setup for the mailbox.
 */

void hfdd_mbox (int dev, int *mbxs, void *n1, void *n2)
{
	if (hfdd_debug)
		log (-1, "incoming connection made - assign socket pair");

	/* use socket pair like bbs side, 08Jan2005 */
	if (j2socketpair (AF_LOCAL, SOCK_STREAM, 0, mbxs) == -1)
	{
		log (-1, "socketpair failed, errno %d", errno);
		return;
	}

	seteol (mbxs[0], "\r");
	seteol (mbxs[1], "\r");
/*
	seteol (mbxs[0], "\r\n");
	seteol (mbxs[1], "\r\n");
*/
	sockmode (mbxs[1], SOCK_ASCII);

	/*
	 * 09Oct2006, Maiko, Ouch !!! This process can easily use up 2000
	 * bytes of stack space if you can believe it - I observed this from
	 * live testing - had alot of crashing going on, until I realized just
	 * how much stack space this process uses when forwarding over HFDD !
	 *
	 * Forget 1024 or 2048 - At minimum, start with 4096 stack !!!
	 */
	newproc ("MBOX HF Server", 4096, mbx_incom, mbxs[1], n1, n2, 0);

	/*
	 * give the mailbox time to come up, then assign hfdd_fwdsock. This
	 * is very important, assigning hfdd_fwdsock before this, results
	 * in the keyboard handler (which waits for the assignment of the
	 * hfdd_fwdsock) to prematurely die with an invalid socket.
	 */
	hfdd_fwdsock = mbxs[0]; /* this is for our local access */
}

/* 09Mar2005, Maiko, New HFDD commands, called from the config.c file */

static int dohfddsrv (int argc, char *argv[], void *p)
{
	static struct proc *hfsrvp = (struct proc*)0;

	if (argc == 3)
	{
		if (!stricmp (argv[2], "start"))
		{
			char **pargv = (char**)callocw (3, sizeof(char*));

			log (-1, "start hfdd server on port [%s]", argv[1]);

			pargv[0] = "c";
			pargv[1] = j2strdup (argv[1]);
			pargv[2] = "bbs";

			hfsrvp = newproc ("hfddsrv", 2048, hfddsrv2, 3, pargv, (void*)0, 1);
		}
		/* 09Mar2005, Maiko, Kick option incase server goes into limbo */
		else if (!stricmp (argv[2], "kick"))
		{
			log (-1, "kick hfdd server on port [%s]", argv[1]);
			alert (hfsrvp, EABORT);
		}
		/* 23Sep2006, Maiko, Unproto option for sysop to FEC at will,
		 * note this is a bit of a kludge in that we have to pass the
		 * name of the interface (will fix that later) ...
		 */
		else if (!stricmp (argv[2], "unproto"))
		{
			struct iface *ifp;

			log (-1, "sending unproto (FEC) beacon");

			/* 20Mar2007, Maiko, Added KAM hostmode */
   			if ((ifp = if_lookup (argv[1])) != NULLIF)
			{
				if (hfdd_is_dxp (ifp->name))
					dxp_FEC (ifp->dev, 1);
				else if (hfdd_is_kam (ifp->name))
					kam_FEC ();
				/* 16Apr2007, Maiko, Added FEC code for SCS modem */
				else if (hfdd_is_ptc (ifp->name))
					ptc_FEC ();
				/* 20Sepr2008, Maiko, Added FEC code for PK232 modem */
				else if (hfdd_is_pk232 (ifp->name))
					pk232_FEC (ifp->dev);
			}
		}
		/*
		 * 14Mar2007, Maiko, Utility to EXIT hostmode incase the
		 * TNC seems to get into the occasional I AM IN HOSTMODE
		 * but I AM NOT RESPONDING type of situation. Doing this
		 * kick will force the code to recognize NOT IN HOSTMODE
		 * status, and reinitialize the TNC (KAM).
		 */
		else if (!stricmp (argv[2], "exit"))
		{
			struct iface *ifp;

   			if ((ifp = if_lookup (argv[1])) != NULLIF)
			{
				/* 15May07, Now have hostmode exit for PTC-IIusb modem */
				if (hfdd_is_ptc (ifp->name))
					ptc_exit_hostmode (ifp->dev);
				/* 23Apr08, Maiko, Supporting PK232 now */
				else if (hfdd_is_pk232 (ifp->name))
					pk232_exit_hostmode (ifp->dev);
				else
					kam_exit_hostmode (ifp->dev);
			}
		}
	}
	else
		tprintf ("use: hfdd server [iface] [start|stop]\n"); 

	return 0;
}

/*
 * 27Apr2010, Maiko, Applications using the WINMOR Sound Card TNC
 * connect to it using TCP/IP. We need to configure the IP address
 * of the TNC, so let's create a new hfdd subcommand to do so.
 */
char *hfddip = (char*)0;	/* ip address of ip based tnc */

static int dohfddip (int argc, char *argv[], void *p)
{
    if (argc < 2)
	{
		if (hfddip)
        	tprintf("hfdd IP address [%s]\n", hfddip);
		else
        	tprintf("not defined\n");
	}
    else
	{
		if (hfddip)
			free (hfddip);

		hfddip = j2strdup (argv[1]);
	}
	return 0;
}

/*
 * 17Mar2007, Maiko, With all the debugging that I do, I
 * should really have a debug flag that the user can toggle
 * so that the log file does not get too cluttered up.
 */
static int dohfdddbg (int argc, char *argv[], void *p)
{
	if (hfdd_debug)
		hfdd_debug = 0;
	else
		hfdd_debug = 1;

	log (-1, "HFDD debugging is now %s", hfdd_debug ? "ON":"OFF");

	return 0;
}

#ifdef	WINMOR

/*
 * 16Apr2016, Maiko, Should have a proper 'stub' function for this
 */

static int dowinmorheard (int argc, char *argv[], void *p)
{
	extern void winmor_heard ();	/* 17Apr2014, Maiko (VE4KLM) */

	winmor_heard ();

	return 0;
}

#endif

static struct cmds DFAR HFDDcmds[] = {

	{ "server", dohfddsrv, 0, 0, NULLCHAR },

	{ "debug", dohfdddbg, 0, 0, NULLCHAR },

	{ "ip", dohfddip, 0, 0, NULLCHAR },

#ifdef	WINMOR
	{ "heard", dowinmorheard, 0, 0, NULLCHAR },
#endif

	{ NULLCHAR, NULL, 0, 0, NULLCHAR }
};

int dohfdd (int argc, char *argv[], void *p)
{
	return subcmd (HFDDcmds, argc, argv, p);
}

#endif	/* end of HFDD */

