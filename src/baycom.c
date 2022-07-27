/*
 * This my attempt at writing a BAYCOM interface for JNOS 2.0 (linux) ...
 *
 * Support for the BAYCOM device (ser12) - In linux one has usually had to
 * load the baycom_ser_fdx KERNEL module, then run the net2kiss program which
 * creates a PSEUDO-TTY for programs like JNOS to attach to, like a kiss tnc.
 *
 * This source now lets JNOS attach directly to the linux KERNEL module. No
 * more net2kiss required, no more PSEUDO-TTY support required. Now it's just
 * a simple matter of doing an 'attach baycom <iface> <mtu> <devname>'.
 *
 * Designed and Coded by Maiko Langelaar / VE4KLM - I got the first version
 * of this driver working quite nicely on November 15, 2008, using a board
 * based on the XR 2206 and XR 2211 chips.
 *
 * For Amateur Radio use only (please).
 *
 * =========================================================================
 *
 * Some of the code from the net2kiss program has been used here, so to keep
 * with the *legal requirements* of using parts of the software, below is the
 * comments found at the very beginning of the net2kiss.c source code :
 *
 *      net2kiss.c - convert a network interface to KISS data stream
 *
 *	Copyright (C) 1996  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Please note that the GPL allows you to use the driver, NOT the radio.
 *  In order to use the radio, you need a license from the communications
 *  authority of your country.
 */

#include "config.h"	/* 20May2009, Maiko, Important to include this */

#ifdef	BAYCOM_SER_FDX

#define	FROM_NET2KISS	/* indicates any code borrowed from net2kiss.c */

/*
 * 29Sep2019, Maiko (VE4KLM), get rid of this (rediculous in hindsite)
 *
 * 05Dec08, Maiko, tell iface.h of exclusions
 *  #define BAYCOM_MODULE
 */

#include "iface.h"

#include "kiss.h"
#include "devparam.h"

#ifdef	FROM_NET2KISS	/* from NET2KISS.C - 0.1 18.09.96 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <endian.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <grp.h>
#include <string.h>
#include <termios.h>

#include <sys/socket.h>
#include <net/if.h>

#ifdef __GLIBC__
#include <net/ethernet.h>
#else
#include <linux/if_ether.h>
#endif

static int fdif = -1;
static struct ifreq ifr;

static void die(char *func) 
{
	fprintf(stderr, "%s (%i)%s%s\n", strerror(errno),
		errno, func ? " in " : "", func ? func : "");
	exit(-1);
}

#endif	/* End of NET2KISS */

static int baycom_send (struct iface*, struct mbuf*);

extern void dump (struct iface*, int, unsigned int, struct mbuf*);

extern void register_io (int, void*);

/* baycom_ioctl - Added November 11, 2008 - Maiko (VE4KLM) */
int32 baycom_ioctl (struct iface *iface, int cmd, int set, int32 val)
{
    struct mbuf *hbp;
    char *cp;
    int32 rval = 0;

	switch (cmd)
	{
		case PARAM_TXDELAY:
		case PARAM_PERSIST:
		case PARAM_SLOTTIME:
		case PARAM_TXTAIL:
		case PARAM_FULLDUP:
			break;

		default:
			log (-1, "baycom - [%d] cmd not supported",  cmd);
			return rval;
	}

	/* Allocate space for cmd and arg */
	if ((hbp = alloc_mbuf(2)) != NULLBUF)
	{
		cp = hbp->data;

		*cp++ = cmd;

		*cp = (char)val;

		hbp->cnt = 2;

		if (hbp->data[0] != (char)PARAM_RETURN)
			hbp->data[0] |= (iface->port << 4);

		if (iface->port)
		{
			iface->rawsndcnt++;
			iface->lastsent = secclock();
		}

		baycom_send (iface, hbp);
	}

	return rval;
}

/*
 * 16Nov08, Maiko, Pass the kernel device name in the attach, that way we
 * can use this driver for several of the different baycom kernel modules
 * that were written by Thomas Sailer and others. All of them were based
 * on the net2kiss stuff, so they *should* work with this JNOS interface.
 *
 * static char *name_iface = "bcsf0";  I've only used the 'ser12' myself.
 */
static char *name_iface = NULL;

static int bc_debug = 0;

#define	PARAM_DATA 0
#define	CL_AX25	9
#define AXALEN 7

int baycom_raw (struct iface *iface, struct mbuf *bp)
{
	struct mbuf *bp2;

	dump (iface, IF_TRACE_OUT, CL_AX25, bp);

	bp2 = pushdown (bp, 1);

	bp2->data[0] = PARAM_DATA;

	baycom_send (iface, bp2);

	return 0;
}

static int baycom_send (struct iface *iface, struct mbuf *bp)
{
	static char *iobuf = NULL;
	static int iomtu = -1;
	struct mbuf *tbp;
	int len;

        struct sockaddr to;

	/*
	 * This code was written based on my stripping down of the asy_input()
	 * function used by the SLIP interface (asy.c, unixasy.c, slip.c).
	 *
	 * I don't see the point of allocating memory before a read, just
	 * to free it again right after the read, and so on. All that does
	 * is fragment memory. Only allocate the io buffer once at startup,
	 * and only re-allocate if the MTU of the interface is changed to a
	 * larger value than what it started at (why that would be the case
	 * is beyond me, but we should check for that to be safe).
 	 *
	 * 26Nov2004, Maiko Langelaar, VE4KLM
	 */

	if (iomtu == -1 || iface->mtu > iomtu)
	{
		if (iomtu != -1)
			free (iobuf);

		iomtu = iface->mtu;
		iobuf = mallocw (iomtu);
	}

    iface->lastsent = secclock();

    iface->rawsndcnt++;

	if (fdif != -1)
	{
		len = len_p (bp);

#ifdef J2_SNMPD_VARS
   		iface->rawsndbytes += len;
#endif
		/* this should never happen, but just to be safe !!! */
		if (len > iomtu)
			log (-1, "baycom_raw - packet (%d) larger then mtu", len);

		else if (dup_p (&tbp, bp, 0, len) != len)
			log (-1, "baycom_raw - dup_p failed");

		else
		{
			(void)pullup (&tbp, iobuf, len);

			if (bc_debug)
				log (-1, "baycom_send - %d bytes", len);

			strncpy (to.sa_data, name_iface, sizeof(to.sa_data));

			if (sendto (fdif, iobuf, len, 0, &to, sizeof(to)) < 1)
				log (-1, "baycom_send - write errno %d", errno);

			free_p (tbp);
		}

	    free_p (bp);
	}

    return 0;
}

static char buf[1600];

extern char Mycall[AXALEN];

void baycom_rx (int xdev, void *p1, void *p2)
{
	struct mbuf *bp;
	char kisstype;
	// int port;
	int len;

	/* we pass the interface parameters into here */
	struct iface *ifp = (struct iface*)p1;

	log (-1, "baycom_rx - listening for packets");

	while (1)
	{
		/* wait for an event, let others run if there is no event */
		if (pwait (&fdif) != 0)
			return;

		if ((len = read (fdif, buf, sizeof(buf))) == 0)
			continue;

		if (len == -1 && errno == EWOULDBLOCK)
			continue;

		if (len == -1)
		{
			/* timeout, retry */
			if (errno == EINTR)
				continue;

			tprintf ("baycom_rx : read errno %d, shutting down\n", errno);

			return;
		}

		if (bc_debug)
			log (-1, "baycom_rx - %d bytes", len);

		kisstype = *buf;

		// port = kisstype >> 4;

		if ((kisstype & 0xf) == PARAM_DATA)
		{
			len--;

			bp = qdata (buf + 1, len);

			if (net_route (ifp, CL_AX25, bp) != 0)
			{
				log (-1, "baycom_rx - net_route (CL_AX25) failed !!!");
				free_p (bp);
			}
		}
	}
}

static void baycom_status (struct iface *iface)
{
	/* 12Oct2009, Maiko, Use "%u" for uint32 vars */
    tprintf("  IN:\t%u pkts\n", iface->rawrecvcnt);
    tprintf("  OUT:\t%u pkts\n", iface->rawsndcnt);
}

static int baycom_detach (struct iface *ifp)
{
    // baycom_stop (ifp);
  
    return 0;
}

#ifdef	FROM_NET2KISS	/* from NET2KISS.C - 0.1 18.09.96 */

static void restore_ifflags(int signum)
{
	if (ioctl(fdif, SIOCSIFFLAGS, &ifr) < 0)
		die("ioctl SIOCSIFFLAGS");
	close(fdif);
	exit(0);
}

#endif	/* End of NET2KISS */

extern int32 Ip_addr;   /* Our IP address */

int baycom_attach (int argc, char *argv[], void *p)
{
	struct ifreq ifr_new;
	struct sockaddr sa;
	short if_newflags = 0;
	struct iface *ifp;

	int proto = htons (ETH_P_AX25);

	if (if_lookup (argv[1]) != (struct iface*)0)
	{
		tprintf (Existingiface, argv[1]);
		return -1;
	}

	/* Create interface structure and fill in details */

	ifp = (struct iface*)callocw (1, sizeof(struct iface));

	ifp->addr = Ip_addr;
	ifp->name = j2strdup (argv[1]);
	ifp->mtu = atoi (argv[2]);
	ifp->dev = 0;
	ifp->stop = baycom_detach;

	/* 16Nov08, Maiko, Now pass kernel device name in the attach */
	name_iface = j2strdup (argv[3]);

	setencap (ifp, "AX25");

	ifp->ioctl = baycom_ioctl;	/* use KISS routines */
	ifp->raw = baycom_raw;
	ifp->show = baycom_status;

	/* 15Nov08, Maiko, ax_recv (addreq) crashes if we don't do this */
	if (ifp->hwaddr == NULLCHAR)
		ifp->hwaddr = mallocw (AXALEN);
	memcpy (ifp->hwaddr, Mycall, AXALEN);

	ifp->flags = 0;
	ifp->xdev = 0;

	/* Link in the interface - important part !!! */
	ifp->next = Ifaces;
	Ifaces = ifp;

#ifdef	FROM_NET2KISS	/* from NET2KISS.C - 0.1 18.09.96 */

	if ((fdif = socket(PF_INET, SOCK_PACKET, proto)) < 0) 
		die("socket");
	strcpy(sa.sa_data, name_iface);
	sa.sa_family = AF_INET;
	if (bind(fdif, &sa, sizeof(struct sockaddr)) < 0)
		die("bind"); 
	strcpy(ifr.ifr_name, name_iface);
	if (ioctl(fdif, SIOCGIFFLAGS, &ifr) < 0)
		die("ioctl SIOCGIFFLAGS");
       	ifr_new = ifr;	
	ifr_new.ifr_flags |= if_newflags;
	if (ioctl(fdif, SIOCSIFFLAGS, &ifr_new) < 0)
		die("ioctl SIOCSIFFLAGS");
	signal(SIGHUP, restore_ifflags);
	signal(SIGINT, restore_ifflags);
	signal(SIGTERM, restore_ifflags);
	signal(SIGQUIT, restore_ifflags);
	signal(SIGUSR1, restore_ifflags);
	signal(SIGUSR2, restore_ifflags);

#endif	/* End of NET2KISS */

	ifp->rxproc = newproc ("baycom_rx", 1024, baycom_rx, 0, (void*)ifp, NULL, 0);

	/* pwait() will never return if we don't register the fd !!! */
	register_io(fdif, &fdif);

    return 0;
}

#endif	/* End of BAYCOM_SER_FDX */
