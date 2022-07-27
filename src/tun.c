/*
 * Support for the TUN device - now we can use TUN instead of SLIP to
 * interface NOS to linux or unix - this should dramatically increase
 * the network throughput since SLIP was async and very slow !
 *
 * Designed and Coded by Maiko Langelaar / VE4KLM
 * Got the Prototype working on November 22, 2004
 *
 * For Amateur Radio use only (please).
 *
 * This my attempt at writing a TUN driver for NOS.
 *
 * $Author: root $
 *
 * $Date: 2015/04/22 01:51:45 $
 *
 */

#include <signal.h>

#include "global.h"

#ifdef	TUN	/* 16Aug2010, Maiko, Oops forgot this */

#include "mbuf.h"
#include "iface.h"
#include "asy.h"
#include "slip.h"
#include "trace.h"
#include "pktdrvr.h"
#include "unix.h"

#include <stdio.h>
#include <fcntl.h>
#include <sys/time.h>

#include <sys/types.h>

/*
 * 29Sep2019, Maiko (VE4KLM), THIS is the reason I want to rename the sockaddr,
 * and SOCK_STREAM and related definitions - like I did the socket functions a
 * while ago - I need to include the system socket definitions, in conjunction
 * with me breaking iftcp and ifax25 out of the big tcp.h and ax25.h header
 * files, both included by iface.h - HUGE socket related conflicts, uggg !
 *
 * add slhc.h (included by slip.h) to that comment, it does NOT need to
 * include the whole entire tcp.h file just for a structure definition.
 *
 * this comment refers to the <sys/socket.h> include just added below :)
 */
#include <sys/socket.h>
 
#include <linux/if.h>
#include <linux/if_tun.h> 
#include <sys/ioctl.h>

/*
 * Check the kernel version, since TUN device naming convention differs
 * between version 2.2 and version 2.4 kernels.
 */
#include <linux/version.h>

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
#define	TUNDEVNAME	"/dev/net/tun"
#define	LINUX_2_4
#else
#warning "using /dev/tun0 device for older kernel"
#define	TUNDEVNAME	"/dev/tun0"
#define	LINUX_OLD
#endif

static struct slip Tun;
  
static int tunfd = -1;

/* 29Sep2019, Maiko (VE4KLM), not including netuser.h just for this */
extern int32 Ip_addr;

int tun_send (bp, iface, gateway, prec, del, tput, rel)
	struct mbuf *bp;
	struct iface *iface;
	int32 gateway;
	int prec;
	int del;
	int tput;
	int rel;
{
	if (iface == NULLIF)
	{
		free_p (bp);
		return -1;
	}

    return (*iface->raw)(iface, bp);
}

#ifndef J2_SNMPD_VARS
int32 tun_rawsndcnt = 0, tun_rawrecvcnt = 0;
#endif

int tun_raw (struct iface *iface, struct mbuf *bp)
{
	static char *iobuf = NULL;
	static int iomtu = -1;
	struct mbuf *tbp;
	int len;

    dump (iface, IF_TRACE_OUT, CL_NONE, bp);

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

	if (tunfd != -1)
	{
		len = len_p (bp);

		/* this should never happen, but just to be safe !!! */
		if (len > iomtu)
			log (-1, "tun_raw - packet larger then mtu");

		else if (dup_p (&tbp, bp, 0, len) != len)
			log (-1, "tun_raw - dup_p failed");

		else
		{
			(void)pullup (&tbp, iobuf, len);

			write (tunfd, iobuf, (size_t)len);
#ifndef J2_SNMPD_VARS
			tun_rawsndcnt += len;
#else
			iface->rawsndbytes += len;
#endif
			free_p (tbp);
		}

	    free_p (bp);
	}

    return 0;
}

void tun_rx (int xdev, void *p1, void *p2)
{
	/* NOTE : should really malloc the rcv buffer based on mtu of iface */
	static char buf[1600];
	struct mbuf *bp;
	extern int errno;
	int len;

	/* we pass the interface parameters into here */
	struct iface *ifp = (struct iface*)p1;

	log (-1, "tun_rx - listening for packets");

	while (1)
	{
		/* wait for an event, let others run if there is no event */
		if (pwait (&tunfd) != 0)
			return;

		if ((len = read (tunfd, buf, (size_t)sizeof(buf))) == 0)
			continue;

		if (len == -1 && errno == EWOULDBLOCK)
			continue;

		if (len == -1)
		{
			/* timeout, retry */
			if (errno == EINTR)
				continue;

			tprintf ("tun_rx : read errno %d, shutting down\n", errno);

			return;
		}

#ifdef	DONT_COMPILE

		log (-1, "tun_rx - %d bytes", len);

		{
			static char logbuf[400], *lptr;

			int cnt;

			for (lptr = logbuf, cnt = 0; cnt < len; cnt++)
				lptr += sprintf (lptr, " %02X", (int)buf[cnt]);

			log (-1, "tun_rx data -%s", logbuf);
		}
#endif
		bp = qdata (buf, len);

    	dump (ifp, IF_TRACE_IN, CL_NONE, bp);

#ifndef J2_SNMPD_VARS
		tun_rawrecvcnt += len_p (bp);
#endif
        if (net_route (ifp, CL_NONE, bp) != 0)
		{
			log (-1, "tun_rx - net_route failed !!!");

			free_p (bp);
		}
	}
}

static void tun_status (struct iface *iface)
{
    struct slip *sp;
  
    sp = &Tun;

    if (sp->iface != iface)
        /* Must not be a SLIP device */
        return;
  
    tprintf("  IN:\t%d pkts\n", iface->rawrecvcnt);
    tprintf("  OUT:\t%d pkts\n", iface->rawsndcnt);
}

static int tun_detach (struct iface *ifp)
{
    // tun_stop (ifp);
  
	Tun.iface = NULLIF;

    return 0;
}

int tun_attach (int argc, char *argv[], void *p)
{
	struct iface *ifp;
  
	if (if_lookup (argv[1]) != NULLIF)
	{
		tprintf (Existingiface, argv[4]);
		return -1;
	}

	/* Create interface structure and fill in details */

	ifp = (struct iface*)callocw (1, sizeof(struct iface));

	ifp->addr = Ip_addr;
	ifp->name = j2strdup (argv[1]);
	ifp->mtu = atoi (argv[2]);
	ifp->dev = atoi (argv[3]);
	ifp->stop = tun_detach;

	setencap (ifp, "TUN");

	ifp->ioctl = NULL;
	ifp->raw = tun_raw;
	ifp->show = tun_status;
	ifp->flags = 0;
	ifp->xdev = 0;

	/* Link in the interface - important part !!! */
	ifp->next = Ifaces;
	Ifaces = ifp;

	/* Now physically open TUN device to get things going */

	{
  		struct ifreq ifr;
 
	    if ((tunfd = open(TUNDEVNAME, O_RDWR|O_NONBLOCK|O_NOCTTY, 0644)) == -1)
		{
			log (-1, "unable to open [%s]", TUNDEVNAME);
			return -1;
		}
 
#ifdef	LINUX_2_4

  		memset (&ifr, 0, sizeof(ifr));

  		ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

		/*
		 * 15Jan2018, Added KB8OJH security contribution, so you can run JNOS
		 * as non-root user, attaching to a preconfigured (persistent) iface.
		 */
		if (argc == 5)
			strncpy (ifr.ifr_name, argv[4], IFNAMSIZ);

  		if (ioctl (tunfd, TUNSETIFF, &ifr) < 0)
		{
			log (-1, "unable to setup tunnel");
			close (tunfd);
			return -1;
		}
 
  		log (-1, "using new [%s] device", ifr.ifr_name);
#else

		if (ioctl (tunfd, TUNSETNOCSUM, 1) < 0)
		{
			log (-1, "unable to setup tunnel");
			close (tunfd);
			return -1;
		}

  		log (-1, "using [%s] device", TUNDEVNAME);
#endif

	}

	ifp->rxproc = newproc ("tun_rx", 1024, tun_rx, 0, (void*)ifp, NULL, 0);

	/* pwait() will never return if we don't register the fd !!! */
	register_io(tunfd, &tunfd);

    return 0;
}

#endif	/* end of TUN */
