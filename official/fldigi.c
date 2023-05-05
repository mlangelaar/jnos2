/*
 * JNOS 2.0 support functions for FLDIGI interface
 *
 * (C)opyright 2015 By Maiko Langelaar / VE4KLM - first written 17Apr2015
 *
 * The beauty of the FLDIGI interface is that we simply just have to attach
 * to it like we do any AXUDP wormhole, running a subsequent 'attach fldigi'
 * command to ensure the FEND and PORT|HWID bytes are added on the way out,
 * and stripped off on the way in. By mid day, already have FLDIGI sending
 * out AX25 connect requests, pretty cool I think.
 *
 * 20Apr2015, Maiko, Added proper fldigi_command(), replaces perl script :)
 *
 */

#include "global.h"

#ifdef	FLDIGI_KISS

#include "mbuf.h"
#include "iface.h"
#include "ax25.h"
#include "netuser.h"
#include "udp.h"
#include "axip.h"

static char *fldigiport = NULL;

int fldigi_iface (struct iface *ifp)
{
	if (!fldigiport)
		return 0;

	return (strcmp (ifp->name, fldigiport) == 0);
}

/* 20Apr2015, Maiko, send HWID command, return 0 on error */

extern AXIPCFG *Axip;	/* have to make this global now in ax25.c */

int fldigi_command (struct iface *ifp, char *cmdstr)
{
	struct mbuf *bp;

	struct socket lsock, rsock;

	int len = strlen (cmdstr);

	lsock.address = INADDR_ANY;
	lsock.port = 10000;

	rsock.address = Axip[ifp->dev].ipaddr;
	rsock.port = 10000;
	
	log (-1, "fldigi_command [%s]", cmdstr);

	if ((bp = alloc_mbuf (len)) == NULLBUF)
		return 0;

	strncpy ((char*)bp->data, cmdstr, len);

	bp->cnt = (int16)len;

	return send_udp (&lsock, &rsock, 0, 0, bp, 0, 0, 0);
}

int fldigi_attach (int argc, char **argv, void *p)
{
	struct iface *ifp;

	if ((ifp = if_lookup (argv[1])) == NULLIF)
	{
		tprintf (Badinterface,argv[1]);
		return 1;
	}

	fldigiport = j2strdup (argv[1]);

	fldigi_command (ifp, "\xC0\x06KISSRAW:ON\xC0");

	// system ("/jnos/rte/send.pl");

	return 0;
}

#endif
