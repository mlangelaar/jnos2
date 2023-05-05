/*
 * Support for the TUN device - now we can use TUN instead of SLIP to
 * interface NOS to linux or unix - this should dramatically increase
 * the network throughput since SLIP was async and very slow !
 *
 * Designed and Coded by Maiko Langelaar / VE4KLM
 * Got the Prototype working on November 22, 2004
 *
 * 24Sep2022, Maiko (VE4KLM), Now supporting multiple tun interfaces. It
 * turns out this code only worked with a single tun interface. So after
 * getting a message from Mark Herson (N2MH), wanting to use a couple of
 * interfaces (ampr and mesh), the code was fixed - seems to be working.
 *
 * 12Mar2023, Maiko (VE4KLM), Adding TAP option, since I may just have to
 * use a tap interface in order to receive any Node Solicitation messages
 * needed if I am to get IPV6 working with public addresses - Bob's idea.
 *
 * 18Mar2023, Maiko, There is no choice, IPV6 requires link layer, since
 * the Network Discovery is 'only available' at link layer (Ethernet) :|
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

#include "enet.h"	/* 17Mar2023, Maiko */

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
  
/*
 * 24Sep2022, Maiko (VE4KLM), support up to 3 TUN ifaces (for now) ...
 */

#define MAXTUNIFACES 3

// static int tunfd = -1;

// static int tunfd[MAXTUNIFACES] = { -1, -1, -1 };

/*
 * 12Mar2023, Maiko (VE4KLM), Now supporting TUN or TAP mode,
 * so need a structure to contain not just the fd anymore.
 */
struct tuntapdevs {
	int	fd;
	int	mode;
};

static struct tuntapdevs tuntapdev[MAXTUNIFACES] = {
	{ -1, 0 }, { -1, 0 }, { -1, 0 }
};

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
	int written, len, mode;

	mode = tuntapdev[iface->dev].mode; /* 13Mar2023, Maiko */

    dump (iface, IF_TRACE_OUT, (mode == IFF_TUN) ? CL_NONE : CL_ETHERNET, bp);

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

	/*
	 * 12Mar2023, Maiko, Now a structure, since we support TUN and TAP
	 *  if (tunfd[iface->dev] != -1)
 	 */
	if (tuntapdev[iface->dev].fd != -1)
	{
		len = len_p (bp);

		/* this should never happen, but just to be safe !!! */
		if (len > iomtu)
			log (-1, "tun_raw - packet [%d] larger then mtu [%d]", len, iomtu);

		else if (dup_p (&tbp, bp, 0, len) != len)
			log (-1, "tun_raw - dup_p failed");

		else
		{

#ifdef	CHANGED_MY_MIND_FOR_NOW    // 18Mar2023, Maiko (VE4KLM)

			/*
			 * I have no access to IPV6 data at this point, so putting in
			 * the ethernet headers is a challenge, or I am simply having
			 * trouble wrapping my head around this. It just might be a lot
			 * easier to insert an ethernet header in the ipv6_send() func,
			 * where I have direct access to IPV6 info, and set the type
			 * to CL_ETHERNET when the frame is passed to the hopper.
			 *
			 * This minimizes changes to tun.c, meaning our so called tap
			 * driver is simply a different device type, and it expects to
			 * receive an ethernet frame, not try and insert a frame.
			 *
			 * This will at least get things functional for now. I suppose
			 * one can revisit this at a later date to design it 'better'.
			 */

			if (mode == IFF_TAP)
			{
				struct ether ep;

			/*
			 * 17Mar2023, Maiko (VE4KLM), Insert ethernet header (pushdown operation)
			 * this whole ethernet level is throwing me for a loop, where should I be
			 * grabbing this info from ? or storing it when it comes in ? I am having
			 * one of my clueless moments right at this time :( HOW DO WE DO THIS ?
			 */

    			/*
			memcpy (ep.dest, dest, EADDR_LEN);
    			memcpy (ep.source, source, EADDR_LEN);
			*/
    			ep.type = IPV6_TYPE;

    			if ((tbp = htonether (&ep, tbp)) == NULLBUF)
				{
        			free_p (tbp);
        			return -1;
				}

				len = len_p (tbp);	/* this is important */
    		}

#endif		/* CHANGED_MY_MIND_FOR_NOW */

			(void)pullup (&tbp, iobuf, len);

			written = write (tuntapdev[iface->dev].fd, iobuf, (size_t)len);

			if (written == -1)
				log (-1, "write errno %d", errno);
			/* else
				log (-1, "wrote %d of %d bytes", written, len);
			 */

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
	int len, mode;

	/* we pass the interface parameters into here */
	struct iface *ifp = (struct iface*)p1;

	log (-1, "tun_rx - listening for packets");

	mode = tuntapdev[ifp->dev].mode; /* 12Mar2023, Maiko */

	while (1)
	{
		/* wait for an event, let others run if there is no event */
		if (pwait (&(tuntapdev[ifp->dev].fd)) != 0)
			return;

		if ((len = read (tuntapdev[ifp->dev].fd, buf, (size_t)sizeof(buf))) == 0)
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

/*
 * 18Mar2023, Maiko, I just realized, perhaps this is why we are
 * seeing 'duplicate' trace information, config.c already does this
 * for us, no ?
 *
    	 dump (ifp, IF_TRACE_IN, (mode == IFF_TUN) ? CL_NONE : CL_ETHERNET, bp);
 */

#ifndef J2_SNMPD_VARS
		tun_rawrecvcnt += len_p (bp);
#endif
        if (net_route (ifp, (mode == IFF_TUN) ? CL_NONE : CL_ETHERNET, bp) != 0)
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

	int dev;  /* 25Sep2022, Maiko (VE4KLM), check for MAXTUNIFACES */

	extern unsigned char ipv6_ethernet_mac[6];	/* in ipv6hdr.c, added 26Mar2023, Maiko */
	char outbuf[20];
  
	if (if_lookup (argv[1]) != NULLIF)
	{
		/* 04May2023, Michael Ford (WZ0C), should be argv[1], not argv[4] */
		tprintf (Existingiface, argv[1]);
		return -1;
	}

	/* 25Sep2022, Maiko (VE4KLM), This is important, or crash */
	if ((dev = atoi (argv[3])) >= MAXTUNIFACES)
	{
		tprintf ("no iface created - MAXTUNIFACES exceeded\n");
		return -1;
	}

	/* Create interface structure and fill in details */

	ifp = (struct iface*)callocw (1, sizeof(struct iface));

	ifp->addr = Ip_addr;
	ifp->name = j2strdup (argv[1]);
	ifp->mtu = atoi (argv[2]);
	ifp->dev = dev;
	ifp->stop = tun_detach;

	setencap (ifp, "TUN");

	ifp->ioctl = NULL;
	ifp->raw = tun_raw;
	ifp->show = tun_status;
	ifp->flags = 0;
	ifp->xdev = 0;

	/*
	 * 18Mar2023, Maiko, For TAP we need a MAC address
	 *  (looks like 54:4e:45 is private internal)
	 */

	ifp->hwaddr = mallocw(6);

	ifp->hwaddr[0] = 0x54; ifp->hwaddr[1] = 0x4e; ifp->hwaddr[2] = 0x45;
	ifp->hwaddr[3] = 0x00; ifp->hwaddr[4] = 0x00; ifp->hwaddr[5] = 0x00;

	/* Link in the interface - important part !!! */
	ifp->next = Ifaces;
	Ifaces = ifp;

	/* Now physically open TUN device to get things going */

	{
  		struct ifreq ifr;
 
	    if ((tuntapdev[ifp->dev].fd = open(TUNDEVNAME, O_RDWR|O_NONBLOCK|O_NOCTTY, 0644)) == -1)
		{
			log (-1, "unable to open [%s]", TUNDEVNAME);
			return -1;
		}

	/* 12Mar2023, Maiko (VE4KLM), We can now use TUN or TAP with this driver */
		if (strstr (ifp->name, "tap"))
			tuntapdev[ifp->dev].mode = IFF_TAP;
		else
			tuntapdev[ifp->dev].mode = IFF_TUN;

#ifdef	LINUX_2_4

  		memset (&ifr, 0, sizeof(ifr));

  		// ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

  		ifr.ifr_flags = tuntapdev[ifp->dev].mode | IFF_NO_PI;

		/*
		 * 15Jan2018, Added KB8OJH security contribution, so you can run JNOS
		 * as non-root user, attaching to a preconfigured (persistent) iface.
		 */
		if (argc == 5)
			strncpy (ifr.ifr_name, argv[4], IFNAMSIZ);

  		if (ioctl (tuntapdev[ifp->dev].fd, TUNSETIFF, &ifr) < 0)
		{
			log (-1, "unable to setup tunnel");
			close (tuntapdev[ifp->dev].fd);
			return -1;
		}
 
  		log (-1, "using new [%s] device", ifr.ifr_name);

		/*
		 * 26Mar2023, Maiko, for TAP interface, I need the MAC address,
		 * which is used when building ethernet header in my IPV6 code,
		 * okay it's all zeros (cause I did not read above).
		 */
		if (strstr (ifp->name, "tap"))
		{
			if (ioctl (tuntapdev[ifp->dev].fd, SIOCGIFHWADDR, &ifr) < 0)
				log (-1, "error grabbing IFHWADDR");
			else
				memcpy (ipv6_ethernet_mac, ifr.ifr_hwaddr.sa_data, 6);

  			log (-1, "ethernet MAC [%s]", pether (outbuf, ipv6_ethernet_mac));
		}
#else

		if (ioctl (tuntapdev[ifp->dev].fd, TUNSETNOCSUM, 1) < 0)
		{
			log (-1, "unable to setup tunnel");
			close (tuntapdev[ifp->dev].fd);
			return -1;
		}

  		log (-1, "using [%s] device", TUNDEVNAME);
#endif

	}

	ifp->rxproc = newproc ("tun_rx", 1024, tun_rx, 0, (void*)ifp, NULL, 0);

	/* pwait() will never return if we don't register the fd !!! */
	register_io(tuntapdev[ifp->dev].fd, &(tuntapdev[ifp->dev].fd));

    return 0;
}

#endif	/* end of TUN */
