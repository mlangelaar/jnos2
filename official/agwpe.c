/*
 * Support for AGWPE (tcp/ip control) as a digital modem
 *
 * January 29, 2023, by Maiko (VE4KLM), now supports additional ports
 *                   using standard 'attach kiss' command, this after
 *                   getting it working for winrpr and QtSoundModem.
 *
 * Designed and Coded (23 February 2012) by Maiko Langelaar, VE4KLM
 *
 * by 11:30 pm, already have packets going out sound card, next step is to
 * have packets coming into sound card, and passed on to JNOS for proc.
 *
 * by 12:00 noon, 24Feb, I have packets coming into JNOS and decoded, good
 * old desktop speakers and a microphone for the *radio channel*, incoming
 * test packets compliments of KI4MCW - found his packet2.wav on the web :
 * 
    Fri Feb 24 11:24:32 2012 - agwpe sent:
    AX25: VE4NOD->VE4BBS-3 SABM(P)
    0000  ac 8a 68 84 84 a6 e6 ac 8a 68 9c 9e 88 61 3f     ,.h..&f,.h...a?

    Fri Feb 24 11:24:37 2012 - agwpe recv:
    AX25: KI4MCW-1->APAGW v WIDE2-2 UI pid=Text
    0000  82 a0 82 8e ae 40 60 96 92 68 9a 86 ae 62 ae 92  . ...@`..h...b..
    0010  88 8a 64 40 65 03 f0 3d 33 37 33 30 2e 35 30 4e  ..d@e.p=3730.50N
    0020  2f 30 37 37 33 30 2e 35 30 57 2d 41 47 57 74 72  /07730.50W-AGWtr
    0030  61 63 6b 65 72                                   acker

    Fri Feb 24 11:41:43 2012 - agwpe sent:
    AX25: VE4NOD->ID UI pid=Text
    0000  92 88 40 40 40 40 e0 ac 8a 68 9c 9e 88 61 03 f0  ..@@@@`,.h...a.p
 *
 * Alot of this code was drawn from my 'multipsk.c' code written about two
 * years ago - man, where has all the time gone :(
 *
 * (C)opyright 2023 Maiko Langelaar, VE4KLM
 *
 * For Amateur Radio use only (please) !
 */

#include "global.h"

#ifdef	AGWPE

#include "mbuf.h"
#include "iface.h"
#include "slip.h"
#include "trace.h"
#include "pktdrvr.h"
#include "devparam.h"

/*
 * 29Sep2019, Maiko (VE4KLM), struct breakouts
 */
#include "ax25.h"
#include "netuser.h"

#define UNUSED(x) ((void)(x))   /* 15Apr2016, VE4KLM, trick to suppress any
                                 * unused variable warning, why are there so
                                 * many MACROs in this code, should convert
                                 * them to actual functions at some point. */
typedef struct agwpecparm {
	char *hostname;
	int portnum;
} IFPAGWPE;

/* 27Feb2009, Maiko, Use an existing function from slip.c module  */
extern struct mbuf *slip_decode (register struct slip *sp, char c);

/* 28Feb2009, Maiko, Use an existing function from slip.c module  */
extern struct mbuf *slip_encode (struct mbuf *bp, int usecrc);

/*
 * 14Dec2022, Maiko (VE4KLM), Multiple interfaces does not work
 * as is, we need to have unique psocks per interface, so change
 * this to an array of psocks, everything else is fine I think.
 *
 * This array must be properly initialized in agwpe_attach() !
 *
static int psock = -1;
 */

#define MAXAGWPORTS 5
static int psock[MAXAGWPORTS];
static int next_psock = 0;

static int connectagwpe (IFPAGWPE *ifpp)
{
	static int logcount = 0, logstage = 1;	/* 04May2021, Maiko */

    struct sockaddr_in fsocket;

    int s = -1;
  
	if ((fsocket.sin_addr.s_addr = resolve (ifpp->hostname)) == 0L)
	{
		log (-1, "agwpe - host (%s) not found", ifpp->hostname);
        return -1;
    }
  
    fsocket.sin_family = AF_INET;
    fsocket.sin_port = ifpp->portnum;
 
	if ((s = j2socket (AF_INET, SOCK_STREAM, 0)) == -1)
	{
		log (-1, "agwpe - no socket");
        return -1;
	}

	sockmode (s, SOCK_BINARY);
  
	if (j2connect (s, (char*)&fsocket, SOCKSIZE) == -1)
	{
		/*
		 * 04May2021, Maiko (VE4KLM), Thanks to Bob (VE3TOK) who noticed a
		 * growing amount of winrpr_rx sockets - way too many of them ! So
		 * it's important to close the socket if we are unable to connect,
		 * originally reported for winrpr, but it was based on this code.
		 *
		 * Looking back on my emails, John (VE1JOT) actually reported it
		 * for agwpe on 10Apr2021, so he should get credit for this one.
		 *
		 * Also, I might as well put in the new 'minimum logging' code that
		 * I wrote for the winrpr.c module - backing off the log entries.
		 * .
		 */
		close_s (s);

		if (!(logcount % logstage))
		{
			log (-1, "agwpe - connect failed, errno %d", errno);
			logstage *= 2;
			logcount = 0;
		}

		logcount++;

        return -1;
	}
  
	log (s, "agwpe [%s] - connected", ifpp->hostname);

	/* reset on connect or we may not see subsequent failures logged for some time */
	logcount = 0;
	logstage = 1;

	return s;
}

/*
 * 29Jan2023, Maiko (VE4KLM), need this for any kiss attachments
 *
 * Note - the additional kiss interfaces made assumptions that the
 * devices were physical ASY (serial) devices, therefore the stub.
 */
int agwpe_send_stub (int dev, struct mbuf *bp)
{
	struct iface dummy;

	agwpe_send (&dummy, bp);
}

int agwpe_send (struct iface *iface, struct mbuf *bp)
{
	static char *iobuf = NULL;
	static int iomtu = -1;
	struct mbuf *tbp;
	char *ptr;
	int len;

	if (iomtu == -1 || iface->mtu > iomtu)
	{
		if (iomtu != -1)
			free (iobuf);

		iomtu = iface->mtu;
		iobuf = mallocw (iomtu);
	}

	/* 14Dec2022, Maiko (VE4KLM), psock is now an array !! */

	if (psock[iface->dev] != -1)	/* tcp/ip socket handle */
	{
		len = len_p (bp);

		ptr = iobuf;

		/* this should never happen, but just to be safe !!! */
		if (len > iomtu)
			log (-1, "agwpe_send - packet (%d) larger then mtu", len);

		else if (dup_p (&tbp, bp, 0, len) != len)
			log (-1, "agwpe_send - dup_p failed");

		else
		{
			/* Send Data in raw AX.25 format (K frame) */

			memset (ptr, 0x00, 4); ptr += 4;
			*ptr++ = 'K';
			memset (ptr, 0x00, 23); ptr += 23;

			len++;	/* for the KISS flag we put in later (04Apr2014) */

			memcpy (ptr, (char*)&len, 4); ptr += 4;

			memset (ptr, 0x00, 4); ptr += 4;

			/* 24Feb2012, Maiko, AGW PE expects Flag
			 * 04Apr2014, Maiko, KISS delimiter I think (docs are not clear)
			 */

			*ptr++ = 0x00;

			pullup (&tbp, ptr, len - 1);

			if (j2send (psock[iface->dev], iobuf, len + 36, 0) < 1)
				log (-1, "agwpe_send - write errno %d", errno);

			free_p (tbp);
		}

	    free_p (bp);
	}

    return 0;
}

int agwpe_raw (struct iface *iface, struct mbuf *bp)
{
	dump (iface, IF_TRACE_OUT, CL_AX25, bp);

    iface->rawsndcnt++;
    iface->lastsent = secclock();
  
	agwpe_send (iface, bp);

    return 0;
}

/*

struct agwpehdr {
	int port;
	char reserved1[3];
	int kind;
	char reserved2[3];
	char src[10];
	char dst[10];
	int32 len;
	char reserved3[4];
};

agwpe_ntohdr (struct agwpehdr *hdr, struct mbuf **bpp)
{
	hdr->port = pullchar (bpp);
	pullup (bpp, hdr->reserved1, 3);
	hdr->kind = pullchar (bpp);
	pullup (bpp, hdr->reserved2, 3);
	pullup (bpp, hdr->src, 10);
	pullup (bpp, hdr->dst, 10);
	hdr->len = pull32 (bpp);
	pullup (bpp, hdr->reserved3, 4);
}

*/

void agwpe_rx (int xdev, void *p1, void *p2)
{
	struct iface *ifp = (struct iface*)p1;
	IFPAGWPE *ifpp = (IFPAGWPE*)p2;
    struct sockaddr_in from;
	char initbuf[40], *ptr;
    struct mbuf *bp;
	char tmp[AXBUF];
	int fromlen;

	log (-1, "agwpe listener [%s:%d]", ifpp->hostname, ifpp->portnum);

	while (1)
	{
		/* Connect to the agwpe tcp/ip server, retry every minute */
		/* 14Dec2022, Maiko (VE4KLM), psock is now an array !! */

		if (psock[ifp->dev] == -1)
		{
			while ((psock[ifp->dev] = connectagwpe (ifpp)) == -1)
				j2pause (60000);
		}

		/* Register CallSign (X frame) */

		ptr = initbuf;
		memset (ptr, 0, 4); ptr += 4;
		*ptr++ = 'X';
		memset (ptr, 0, 3); ptr += 3;

		ptr += sprintf (ptr, "%-10.10s", pax25 (tmp, Bbscall));

		memset (ptr, 0, 18); ptr += 18;

		/* 14Dec2022, Maiko (VE4KLM), psock is now an array !! */
		if (j2send (psock[ifp->dev], initbuf, 36, 0) < 1)
			log (-1, "Register Callsign 'X' - write errno %d", errno);

		/* Activate reception of Frames in raw format (k Frame) */

		ptr = initbuf;
		memset (ptr, 0x00, 4); ptr += 4;
		*ptr++ = 'k';
		memset (ptr, 0x00, 31); ptr += 31;

		/* 14Dec2022, Maiko (VE4KLM), psock is now an array !! */
		if (j2send (psock[ifp->dev], initbuf, 36, 0) < 1)
			log (-1, "Activate Raw Recv 'k' - write errno %d", errno);

		/* Handle incoming data stream */

		while (1)
		{
			int32 agwpedk, agwpedl;
			int32 agwpeport;

			char agwpesrc[11], agwpedst[11];

			pwait (NULL);	/* give other processes a chance */

			/* 14Dec2022, Maiko (VE4KLM), psock is now an array !! */
			if (recv_mbuf (psock[ifp->dev], &bp, 0, (char*)&from, &fromlen) == -1)
				break;

			/* Parse out the header */
			
			agwpeport = pullchar (&bp);
			pullup (&bp, NULL, 3);
			agwpedk = pullchar (&bp);
			pullup (&bp, NULL, 3);
			pullup (&bp, agwpesrc, 10);
			pullup (&bp, agwpedst, 10);
			pullup (&bp, (char*)&agwpedl, 4);
			pullup (&bp, NULL, 4);

       UNUSED (agwpeport);   /* suppress warning, Maiko (VE4KLM), 23Apr2016 */

/*
	log (psock, "P [%d] DK [%d] DL [%d]", agwpeport, agwpedk, agwpedl);
*/
			/* end of header */

			if (agwpedk != 'K')		/* we only handle RAW frames for now */
				continue;

			/* this is a flag put in by AGWPE, not part of raw ax25 data
			log (psock, "Flag [%02x]", pullchar (&bp));
			*/

			/* 05Apr2014, Maiko, I think it's a KISS delimiter */
			pullchar (&bp);
			agwpedl--;

			// pullup (&bp, rawbuf, agwpedl);

			/* NICE - just pass the rest of BP direct to net_route :) */

			if (net_route (ifp, CL_AX25,  bp) != 0)
			{
				free_p (bp);
				bp = NULL;
			}
		}

		log (-1, "agwpe disconnected");

	/* 14Dec2022, Maiko (VE4KLM), psock is now an array !! */
		close_s (psock[ifp->dev]);
		psock[ifp->dev] = -1;
	}
}

int agwpe_attach (int argc, char *argv[], void *p)
{
	struct iface *ifp;
	struct slip *sp;
	IFPAGWPE *ifpp;
	int xdev;
 
	if (if_lookup (argv[1]) != NULLIF)
	{
		tprintf (Existingiface, argv[4]);
		return -1;
	}

	/* 14Dec2022, Maiko (VE4KLM), properly support multiple ifaces */
	if (next_psock == MAXAGWPORTS)
	{
		tprintf ("max ports, increase MAXAGWPORTS\n");
		return -1;
	}

	/* Create structure of AGWPE connection parameters */

	ifpp = (IFPAGWPE*)callocw (1, sizeof(IFPAGWPE));

	ifpp->hostname = j2strdup (argv[2]);
	ifpp->portnum = atoi (argv[3]);


	/* Create interface structure and fill in details */

	ifp = (struct iface*)callocw (1, sizeof(struct iface));

	ifp->addr = Ip_addr;
	ifp->name = j2strdup (argv[1]);
	ifp->mtu = 256;

	/* 14Dec2022, Maiko (VE4KLM), properly support multiple ifaces */
	ifp->dev = next_psock++;
	psock[ifp->dev] = -1;

	ifp->stop = NULL;

	if (ifp->hwaddr == NULLCHAR)
		ifp->hwaddr = mallocw (AXALEN);

	memcpy (ifp->hwaddr, Mycall, AXALEN);

	setencap (ifp, "AX25");

	ifp->ioctl = NULL;
	ifp->raw = agwpe_raw;
	ifp->show = NULL;
	ifp->flags = 0;
	ifp->port = 0;  /* 29Jan2023, Maiko, Need this for proper slip function */

	/*
	 * 28Feb2009, Maiko, Crap, it looks like the Slip[] and xdev and dev are
	 * so interlocked into the trace (dump) routines, that dump will actually
	 * crash (and probably other stuff) if I just calloc a SLIP structure, as
	 * I tried to do before. Looks like I'll have to stick with looping thru
	 * the Slip[] array for an empty slot, until I figure out another way to
	 * do this. It works ! BUT I don't like how it's tied into everything.
	 *
	 * 29Jan2023, Maiko (VE4KLM), not change here, but just a note. We have
	 * to use the SLIP structures if we are to attach additiona kiss ports
	 * to the original device. Direwolf supports an additional kiss port.
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

   	/* 29Jan2023, Maiko, Oops, forgot these two - terrible of me !!! */
    	sp->send = agwpe_send_stub;
    	sp->get = NULL; // 29Jan2023, only used by asy serial, queue level ?

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
	 * The connect to the agwpe tcp/ip server is inside the
	 * listener process. If we do it here, JNOS will hang on the
	 * attach command - remember 'attach' must always return.
	 */
	ifp->rxproc = newproc ("agwpe_rx", 1024, agwpe_rx,
			0, (void*)ifp, (void*)ifpp, 0);

	return 0;
}

#endif	/* End of AGWPE */
