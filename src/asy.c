/*
 * JNOS 2.0
 *
 * $Id: asy.c,v 1.2 2012/03/20 15:51:44 ve4klm Exp $
 *
 * Generic serial line interface routines
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Mods by G1EMM and WG7J and VE4KLM
 */
#include "global.h"
#include "proc.h"
#include "iface.h"
#include "netuser.h"
#include "slhc.h"
#ifdef UNIX
#include "unixasy.h"
#else
#include "i8250.h"
#endif
#include "asy.h"
#include "ax25.h"
#include "kiss.h"
#include "pktdrvr.h"
#include "ppp.h"
#include "slip.h"
#include "nrs.h"
#include "commands.h"
#include "mbuf.h"
  
static int asy_detach __ARGS((struct iface *ifp));

#ifdef JNOSPTY98

#include <ctype.h>  

/* 16Dec2011, Maiko, New function to better support /dev/ptmx (PTY98) devs */

static char *getptmxslave (char *iarg)
{
	char fname[128], *wsptr;

	FILE *fptmx;

	char *newarg = (char*)0;

	sprintf (fname, "spool/%s.cfg", iarg);

	if ((fptmx = fopen (fname, "r")) == (FILE*)0)
	{
		log (-1, "unable to open file [%s]", fname);

		return newarg;
	}

	while (fgets (fname, sizeof(fname)-1, fptmx))
	{
		rip (fname); /* make sure to strip out newline */

		/* 17Dec2011, Maiko, I should really look out for whitespace */
		for (wsptr = fname; *wsptr && isspace ((int)*wsptr); wsptr++);

		if (*wsptr && !strncmp (wsptr, "/dev/", 5))
		{
			newarg = j2strdup (wsptr + 5);

			log (-1, "ptmx device [%s] slave [%s]", iarg, newarg);

			break;
		}
	}

	if (newarg == (char*)0)
		log (-1, "ptmx device [%s] can't identify slave device", iarg);

	fclose (fptmx);

	return newarg;
}

#endif	/* end of JNOSPTY98 */
  
/* Attach a serial interface to the system
 * argv[0]: hardware type, must be "asy"
 * argv[1]: I/O address, e.g., "0x3f8"
 * argv[2]: vector, e.g., "4".  In hex, 0x prefix optional.  Suffix with _c to chain.
 * argv[3]: mode, may be:
 *      "slip" (point-to-point SLIP)
 *      "ax25" (AX.25 frame format in SLIP for raw TNC)
 *      "nrs" (NET/ROM format serial protocol)
 *      "ppp" (Point-to-Point Protocol, RFC1171, RFC1172)
 *      "pkiss" (Polled KISS ala G8BPQ)
 * argv[4]: interface label, e.g., "sl0"
 * argv[5]: receiver ring buffer size in bytes
 * argv[6]: maximum transmission unit, bytes
 * argv[7]: interface speed, e.g, "9600"
 * argv[8]: optional flags,
 *      'c' BPQ-style checksum used in kiss mode - WG7J
 *      'v' for Van Jacobson TCP header compression (SLIP only,
 *          use ppp command for VJ compression with PPP);
 *      'f' for forced use of the 16550 fifo's - WG7J
 *      'dd' to set 16550 trigger level to dd (an integer) - WG7J
 *      'n' to use NRS-CTS protocol hardware handshaking - WZ2B
 *	'r' to indicate device can assert DCD (so Unix CLOCAL is not needed) - N5KNX
  
 */
int
asy_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct iface *ifp;
    struct asy *asyp;
    char *ifn;
    int dev;
    int xdev;
    int trigchar = -1;
    char monitor = FALSE;
    int triglevel = 0;
    int force = 0;
    int polled = 0;
    char *cp;
#if defined(SLIP) || defined(AX25)
    struct slip *sp;
#endif
#ifdef  NRS
    struct nrs *np;
#endif

	char *argv1 = (char*)0;	/* 16Dec2011, Maiko, New arg mapping */

#ifdef JNOSPTY98
	/*
	 * 16Dec2011, Maiko (VE4KLM), First of all my apologies to Bob (VE3TOK)
	 * for dragging my butt on this for so long. I now support the ability
	 * to read a temporary file containing the SLAVE tty created when one
	 * opens the /dev/ptmx using kissattach or some other program.
	 */
	if (!strncmp (argv[1], "ptmx_", 5))
	{
		if ((argv1 = getptmxslave (argv[1])) == (char*)0)
			return -1;
	}
	else
#endif
		argv1 = j2strdup (argv[1]);

    if(if_lookup(argv[4]) != NULLIF){
        tprintf(Existingiface,argv[4]);
        return -1;
    }
    /* Find unused asy control block */
    for(dev=0;dev < ASY_MAX;dev++){
        asyp = &Asy[dev];
        if(asyp->iface == NULLIF)
            break;
    }
    if(dev >= ASY_MAX){
        j2tputs("Too many async controllers\n");
        return -1;
    }
#ifdef UNIX
    asyp->flags=0;
#endif
  
    /* Create interface structure and fill in details */
    ifp = (struct iface *)callocw(1,sizeof(struct iface));
    ifp->addr = Ip_addr;
    ifp->name = j2strdup(argv[4]);
    ifp->mtu = atoi(argv[6]);
    ifp->dev = dev;
    ifp->stop = asy_detach;
  
#ifdef UNIX
    /* check for 'r' => rlsd (or DCD) can be asserted */
    if((argc > 8) && ((cp = strchr(argv[8],'r')) != NULLCHAR)) {
        asyp->flags |= ASY_CARR;
        *cp = ' ';   /* mark flag char as consumed */
    }
#endif /* UNIX */

    /*Check for forced 16550 fifo usage - WG7J */
    if((argc > 8) && ((cp = strchr(argv[8],'f')) != NULLCHAR)) {
        force = 1;
        *cp = ' ';   /* mark flag char as consumed */
        if((triglevel = atoi(++cp)) == 0)  /* is there an additional arg ? */
            if(argc > 9)
                triglevel = atoi(argv[9]);
    }
    strupr(argv[3]);

#ifdef	HFDD
	/*
	 * 03Mar2006, Maiko (VE4KLM), Should have a proper attach command for
	 * the HFDD (HF Digital Device). Since I started this, I've been using
	 * the asy 'ax25' interface for convenience, but that causes certain
	 * issues and adds to overhead. Let's do this right once and for all.
	 *
	 * Pass the modem type (dxp38, ptcpro, etc) using argv[8] ...
	 */
    if(strcmp(argv[3],"HFDD") == 0)
	{
		log (-1, "attaching HF digital device ...");
        ifp->ioctl = asy_ioctl;
		ifp->type = CL_HFDD;	/* 18Mar2006 */

		/* 05Dec08, Maiko, You MUST set tcp params, else tcpuser crashes */
        setencap (ifp, NULL);
	}
	else
#endif  

#ifdef  SLIP
    if(strcmp(argv[3],"SLIP") == 0) {
        for(xdev = 0;xdev < SLIP_MAX;xdev++){
            sp = &Slip[xdev];
            if(sp->iface == NULLIF)
                break;
        }
        if(xdev >= SLIP_MAX) {
            j2tputs("Too many slip devices\n");
            free(ifp->name);
            free((char *)ifp);
            return -1;
        }
        setencap(ifp,"SLIP");
        ifp->ioctl = asy_ioctl;
        ifp->raw = slip_raw;
        ifp->show = slip_status;
        ifp->flags = 0;
        ifp->xdev = xdev;
  
        sp->iface = ifp;
        sp->send = asy_send;
        sp->get = get_asy;
        sp->type = CL_SERIAL_LINE;
        trigchar = FR_END;
#ifdef VJCOMPRESS
        if((argc > 8) && ((cp=strchr(argv[8],'v')) != NULLCHAR)) {
            sp->escaped |= SLIP_VJCOMPR;
            sp->slcomp = slhc_init(16,16);
            *cp = ' ';   /* mark flag char as consumed */
        }
#else
        sp->slcomp = NULL;
#endif  /* VJCOMPRESS */
        ifp->rxproc = newproc( ifn = if_name( ifp, " rx" ),
        256,asy_rx,xdev,NULL,NULL,0);
        free(ifn);
    } else
#endif
#ifdef  AX25
        if(strcmp(argv[3],"AX25") == 0
#ifdef POLLEDKISS
            || strcmp(argv[3],"PKISS") == 0
#endif
        ) {
        /* Set up a SLIP link to use AX.25 */
            for(xdev = 0;xdev < SLIP_MAX;xdev++){
                sp = &Slip[xdev];
                if(sp->iface == NULLIF)
                    break;
            }
            if(xdev >= SLIP_MAX) {
                j2tputs("Too many ax25"
#ifdef POLLEDKISS
                "or pkiss"
#endif
                " devices\n");
                free(ifp->name);
                free((char *)ifp);
                return -1;
            }
            setencap(ifp,"AX25");
            ifp->ioctl = kiss_ioctl;
            ifp->raw = kiss_raw;
            ifp->show = slip_status;
            ifp->port = 0;          /* G1EMM */
            if(ifp->hwaddr == NULLCHAR)
                ifp->hwaddr = mallocw(AXALEN);
            memcpy(ifp->hwaddr,Mycall,AXALEN);
            ifp->xdev = xdev;
  
            sp->iface = ifp;
            sp->send = asy_send;
            sp->kiss[ifp->port] = ifp;  /* G1EMM */
            sp->get = get_asy;
            sp->type = CL_KISS;
            sp->polled = sp->usecrc = 0;
            trigchar = FR_END;
            ifp->rxproc = newproc( ifn = if_name( ifp, " rx" ),
            256,asy_rx,xdev,NULL,NULL,0);
#ifdef POLLEDKISS
            if(*argv[3] == 'P') {
                polled = 1;
                sp->polled = sp->usecrc = 1;  /* PKISS => usecrc (for compatibility) */
            }
            if((argc > 8) && ((cp=strchr(argv[8],'c')) != NULLCHAR)) {
                sp->usecrc = 1;
                *cp = ' ';   /* mark flag char as consumed */
            }
#endif
            free(ifn);
        } else
#endif
#ifdef  NRS
            if(strcmp(argv[3],"NRS") == 0) {
        /* Set up a net/rom serial iface */
                for(xdev = 0;xdev < SLIP_MAX;xdev++){
                    np = &Nrs[xdev];
                    if(np->iface == NULLIF)
                        break;
                }
                if(xdev >= SLIP_MAX) {
                    j2tputs("Too many nrs devices\n");
                    free(ifp->name);
                    free((char *)ifp);
                    return -1;
                }
        /* no call supplied? */
                setencap(ifp,"AX25");
                ifp->ioctl = asy_ioctl;
                ifp->raw = nrs_raw;
/*      ifp->show = nrs_status; */
                ifp->hwaddr = mallocw(AXALEN);
                memcpy(ifp->hwaddr,Mycall,AXALEN);
                ifp->xdev = xdev;
                np->iface = ifp;
                np->send = asy_send;
                np->get = get_asy;
                trigchar = ETX;
                ifp->rxproc = newproc( ifn = if_name( ifp, " nrs" ),
                256,nrs_recv,xdev,NULL,NULL,0);
                free(ifn);
            } else
#endif
#ifdef  PPP
                if(strcmp(argv[3],"PPP") == 0) {
        /* Setup for Point-to-Point Protocol */
                    trigchar = HDLC_FLAG;
                    monitor = TRUE;
                    setencap(ifp,"PPP");
                    ifp->ioctl = asy_ioctl;
                    ifp->flags = FALSE;
        /* Initialize parameters for various PPP phases/protocols */
                    if (ppp_init(ifp) != 0) {
                        j2tputs("Cannot allocate PPP control block\n");
                        free(ifp->name);
                        free((char *)ifp);
                        return -1;
                    }
                } else
#endif /* PPP */
                {
                    tprintf("Mode %s unknown for interface %s\n",argv[3],argv[4]);
                    free(ifp->name);
                    free((char *)ifp);
                    return -1;
                }
  
#ifdef NRS
        if((argc > 8) && ((cp=strchr(argv[8],'n')) != NULLCHAR)) {  /* WZ2B */
            asyp->nrs_cts = 1;
            *cp = ' ';   /* mark flag char as consumed */
        } else {
            asyp->nrs_cts = 0;
        }
#else
#ifndef UNIX
        asyp->nrs_cts = 0;
#endif
#endif /* NRS */

    if((argc > 8) && (strtok(argv[8]," 1234567890") != NULLCHAR)) {  /* N5KNX */
        tprintf("attach: invalid flag (%s) ignored.\n", argv[8]);
    }

    /* Link in the interface */
    ifp->next = Ifaces;
    Ifaces = ifp;
#ifdef UNIX
    /* *ix version can fail (e.g. "device locked"). Detach if it does. */
    if (asy_init(dev,ifp,argv1,argv[2],(int16)atol(argv[5]),
        trigchar,monitor,atol(argv[7]),force,triglevel,polled) == -1)
            if_detach(ifp);
#else
    asy_init(dev,ifp,argv[1],argv[2],(int16)atol(argv[5]),
    trigchar,monitor,atol(argv[7]),force,triglevel,polled);
#endif
    return 0;
}
  
static int
asy_detach(ifp)
struct iface *ifp;
{
    asy_stop(ifp);
  
#ifdef  SLIP
    if(stricmp(ifp->iftype->name,"SLIP") == 0) {
        Slip[ifp->xdev].iface = NULLIF;
#ifdef VJCOMPRESS
        slhc_free( Slip[ifp->xdev].slcomp );
        Slip[ifp->xdev].slcomp = NULL;
#endif  /* VJCOMPRESS */
    } else
#endif
#ifdef  AX25
        if(stricmp(ifp->iftype->name,"AX25") == 0
        && Slip[ifp->xdev].iface == ifp ) {
            Slip[ifp->xdev].iface = NULLIF;
        } else
#endif
#ifdef  NRS
            if(stricmp(ifp->iftype->name,"AX25") == 0
            && Nrs[ifp->xdev].iface == ifp ) {
                Nrs[ifp->xdev].iface = NULLIF;
            } else
#endif
#ifdef  PPP
                if(stricmp(ifp->iftype->name,"PPP") == 0) {
                    ppp_free(ifp);
                } else
#endif
                {
                    tprintf("invalid type %s for interface %s\n",
                    ifp->iftype->name, ifp->name);
                    free(ifp->name);
                    free(ifp);
                    return -1;
                }
    return 0;
}
  
/* Execute user comm command */
int
doasycomm(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct iface *ifp;
    register struct asy *ap;
    int dev;
    struct mbuf *bp;
  
    if((ifp = if_lookup(argv[1])) == NULLIF){
        tprintf(Badinterface,argv[1]);
        return 1;
    }
    for(dev=0,ap = Asy;dev < ASY_MAX;dev++,ap++)
        if(ap->iface == ifp)
            break;
    if(dev == ASY_MAX){
        tprintf("Interface %s not asy port\n",argv[1]);
        return 1;
    }
  
    bp = pushdown(NULLBUF,(int16)strlen(argv[2]) + 2 );
    strcpy(bp->data,argv[2]);
    strcat(bp->data,"\r");
    bp->cnt = strlen(argv[2]) + 1;
    asy_send(dev,bp);
    return 0;
}
  
