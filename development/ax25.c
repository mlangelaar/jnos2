/*
 * JNOS 2.0
 *
 * $Id: ax25.c,v 1.6 2012/03/20 15:55:31 ve4klm Exp ve4klm $
 *
 * Low level AX.25 code:
 * incoming frame processing (including digipeating)
 * IP encapsulation
 * digipeater routing
 *
 * Copyright 1991 Phil Karn, KA9Q / 1991 Kevin Hil, G1EMM
 *
 * Mods by PA0GRI and VE4KLM
 */
#include "global.h"
#ifdef AX25
#include "mbuf.h"
#include "iface.h"
#include "arp.h"
#include "slip.h"
#include "ax25.h"
#include "lapb.h"
#include "netrom.h"
#include "ip.h"
#include "devparam.h"
#include "trace.h"
#include "pktdrvr.h"
#include "netuser.h"
#include "commands.h"
#include <ctype.h>

#ifdef	APRSD
#include "aprs.h"
#endif

#ifdef	AXIP
#include "udp.h"
#include "axip.h"
#endif

#ifdef	IPV6
#include  "ipv6.h"	/* 28Apr2023, Maiko (VE4KLM) */
#endif
  
static int axsend __ARGS((struct iface *iface,char *dest,char *source,
int cmdrsp,int ctl,struct mbuf *data));

#ifdef AXIP
static int axip_stop __ARGS((struct iface *iface));
static int axip_raw __ARGS((struct iface *iface,struct mbuf *bp));
#ifdef DONT_COMPILE
static void startaxresolve (AXIPCFG *axcfg, int32 interval);
#endif
static void axrtick (void*);
#endif

#ifdef	AX25_XDIGI
extern struct iface *ax25_xdigi (struct iface *iface,
						char *idest, struct ax25 *hdr);
#endif

/* List of AX.25 multicast addresses in network format (shifted ascii).
 * Only the first entry is used for transmissions, but any incoming
 * packet with any one of these destination addresses is recognized
 * as a multicast
 */
/* NOTE: IF you CHANGE the order of these, also change the codes in ax25.h !!!
 * mailfor.c, nr3, and ax25cmd.c depend on this to get broadcast addresses !!!
 * 920306 - WG7J
 */
char Ax25multi[][AXALEN] = {
    { 'Q'<<1, 'S'<<1, 'T'<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1 }, /* QST */
    { 'N'<<1, 'O'<<1, 'D'<<1, 'E'<<1, 'S'<<1, ' '<<1, '0'<<1 }, /* NODES */
    { 'M'<<1, 'A'<<1, 'I'<<1, 'L'<<1, ' '<<1, ' '<<1, '0'<<1 }, /* MAIL */
    { 'I'<<1, 'D'<<1, ' '<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1 }, /* ID */
    { '\0', '\0', '\0', '\0', '\0', '\0', '\0' }
};
/* n5knx: MFJ 1.2.8 f/w bug can yield SABM to NUL call ... we prevent any surprises: */
char Mycall[AXALEN] = "\000JNOS";
char Myalias[AXALEN] = "\000JNOS";    /* the NETROM alias in 'call' form */
#ifdef MAILBOX
char Bbscall[AXALEN] = "\000JNOS";
#endif
#ifdef NETROM
char Nralias[ALEN+1] = "\000JNOS";      /* the NETROM alias in 'alias' form */
#endif
#ifdef CONVERS
char Ccall[AXALEN] = "\000JNOS";
#endif
#ifdef TTYCALL
char Ttycall[AXALEN] = "\000JNOS";
#endif
#ifdef TNCALL
char tncall[AXALEN] = "\000JNOS";
#endif
  
struct ax_route *Ax_routes; /* Routing table header */

#ifdef AXIP

// static AXIPCFG  *Axip;   /* table for AXIP/AXUDP interface info */
AXIPCFG  *Axip;   /* table for AXIP/AXUDP interface info */
static int max_ax25 = NAX25;

#ifdef PPP
extern int16 DFAR fcstab[];      /* table used when calculating FCS */
#else
int16 fcstab[256] = {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
    0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
    0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
    0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
    0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
    0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
    0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
    0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
    0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
    0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
    0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
    0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
    0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
    0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
    0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
    0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
    0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
    0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
    0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
    0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
    0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
    0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
    0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
    0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
    0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
    0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
    0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
    0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
    0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};
#endif	/* end of PPP */

/*
 * 20Oct2004, Maiko, Axip structure is now allocated, allowing the
 * user to conserve memory if it becomes an issue. This function is
 * called from main (). Pass the '-a' option to jnos to customize
 * the maximum size of the Axip structure. Default is NAX25.
 */
void init_max_ax25 (int howmany)
{
	max_ax25 = NAX25;	/* set a default value */

	if (howmany > 0)
		max_ax25 = howmany;

	log (-1, "allocating maximum of %d Axip (axudp + axip) devices", max_ax25);

	Axip = (AXIPCFG*)calloc ((unsigned) max_ax25, sizeof (AXIPCFG));

	if (Axip == (AXIPCFG*)0)
		exit (-1);
}

#endif	/* end of AXIP */
  
/* Send IP datagrams across an AX.25 link */
int
ax_send(bp,iface,gateway,prec,del,tput,rel)
struct mbuf *bp;
struct iface *iface;
int32 gateway;
int prec;
int del;
int tput;
int rel;
{
    char *hw_addr;
    struct ax25_cb *axp;
    struct mbuf *tbp;
  
    struct ax_route *axr;
    char mode;
  
    if(gateway == iface->broadcast) /* This is a broadcast IP datagram */
        return (*iface->output)(iface,Ax25multi[0],iface->hwaddr,PID_IP,bp);
  
    if((hw_addr = res_arp(iface,ARP_AX25,gateway,bp)) == NULLCHAR)
        return 0;   /* Wait for address resolution */
  
    /* If there's a defined route, get it */
    axr = ax_lookup(hw_addr,iface);
  
    if(axr == NULLAXR || ((mode = axr->mode) == AX_DEFMODE) ) {
        if(iface->flags & CONNECT_MODE){
            mode = AX_VC_MODE;
        } else {
            mode = AX_DATMODE;
        }
    }
  
    /* UI frames are used for any one of the following three conditions:
     * 1. The "low delay" bit is set in the type-of-service field.
     * 2. The "reliability" TOS bit is NOT set and the interface is in
     *    datagram mode.
     * 3. The destination is the broadcast address (this is helpful
     *    when broadcasting on an interface that's in connected mode).
     */
#ifdef NO_TOS_AX25_MAPPING
    /* Disobeying iface defaults is undesired by some users */
    if((mode == AX_DATMODE)
#else
    if(del || (!rel && (mode == AX_DATMODE))
#endif
    || addreq(hw_addr,Ax25multi[0])){
        /* Use UI frame */
        return (*iface->output)(iface,hw_addr,iface->hwaddr,PID_IP,bp);
    }
  
    /* Reliability is needed; use I-frames in AX.25 connection */
    if((axp = find_ax25(iface->hwaddr,hw_addr,iface)) == NULLAX25){
        /* Open a new connection */
        axp = open_ax25(iface,iface->hwaddr,hw_addr,
        AX_ACTIVE,0,s_arcall,s_atcall,s_ascall,-1);
        if(axp == NULLAX25){
            free_p(bp);
            return -1;
        }
    }
    if(axp->state == LAPB_DISCONNECTED){
        est_link(axp);
        lapbstate(axp,LAPB_SETUP);
    }
    /* Insert the PID */
    tbp = pushdown(bp,1);

    bp = tbp;
    bp->data[0] = PID_IP;
    if((tbp = segmenter(bp,axp->paclen)) == NULLBUF){
        free_p(bp);
        return -1;
    }
    return send_ax25(axp,tbp,-1);
}
/* Add header and send connectionless (UI) AX.25 packet.
 * Note that the calling order here must match enet_output
 * since ARP also uses it.
 */
int
ax_output(iface,dest,source,pid,data)
struct iface *iface;    /* Interface to use; overrides routing table */
char *dest;     /* Destination AX.25 address (7 bytes, shifted) */
char *source;       /* Source AX.25 address (7 bytes, shifted) */
int16 pid;      /* Protocol ID */
struct mbuf *data;  /* Data field (follows PID) */
{
    struct mbuf *bp;
  
    /* Prepend pid to data */
    bp = pushdown(data,1);
    bp->data[0] = (char)pid;
    return axsend(iface,dest,source,LAPB_COMMAND,UI,bp);
}
/* Common subroutine for sendframe() and ax_output() */
static int
axsend(iface,dest,source,cmdrsp,ctl,data)
struct iface *iface;    /* Interface to use; overrides routing table */
char *dest;     /* Destination AX.25 address (7 bytes, shifted) */
char *source;       /* Source AX.25 address (7 bytes, shifted) */
int cmdrsp;     /* Command/response indication */
int ctl;        /* Control field */
struct mbuf *data;  /* Data field (includes PID) */
{
    struct mbuf *cbp;
    struct ax25 addr;
    struct ax_route *axr;
    char *idest;
    int rval;
  
    /* If the source addr is unspecified, use the interface address */
    if(source[0] == '\0')
        source = iface->hwaddr;
  
    /* If there's a digipeater route, get it */
    axr = ax_lookup(dest,iface);
  
    memcpy(addr.dest,dest,AXALEN);
    memcpy(addr.source,source,AXALEN);
    addr.cmdrsp = cmdrsp;
  
    if(axr != NULLAXR){
        memcpy(addr.digis,axr->digis,axr->ndigis*AXALEN);
        addr.ndigis = axr->ndigis;
        idest = addr.digis[0];
    } else {
        addr.ndigis = 0;
        idest = dest;
    }
  
    addr.nextdigi = 0;
  
    /* Allocate mbuf for control field, and fill in */
    cbp = pushdown(data,1);
    cbp->data[0] = ctl;
  
    if((data = htonax25(&addr,cbp)) == NULLBUF){
        free_p(cbp);    /* Also frees data */
        return -1;
    }
    /* This shouldn't be necessary because redirection has already been
     * done at the IP router layer, but just to be safe...
     */
    if(iface->forw != NULLIF){
        logsrc(iface->forw,source);
        logdest(iface->forw,idest);
        rval = (*iface->forw->raw)(iface->forw,data);
    } else {
        logsrc(iface,source);
        logdest(iface,idest);
        rval = (*iface->raw)(iface,data);
    }
    return rval;
}

#ifdef  AXIP

#ifdef	FLDIGI_KISS
extern int fldigi_iface (struct iface*);	/* 15Apr2016, Maiko */
#endif

/*
 * 27Apr2023, Maiko, The bulk of the original axip_input()
 * function can be used for both the IPV4 and IPV6 stack.
 * The IP address is only used to locate the AX.25 pseudo
 * iface, nothing more, so create a breakout function.
 *  (rxbroadcast arg was never used, don't need ip)
 */
void axip_input_process (int i, struct iface *iface, struct mbuf *bp)
{
    int16 len, f, fcs = 0xffff;
    struct mbuf *tbp;

#ifdef	FLDIGI_KISS
	/* Not sure we will have FLDIGI running ipv6 ? */
	int fldigi = fldigi_iface (iface);
#endif

    if (i == max_ax25)
	{
         /* Here we could still try to pick a real AX.25 interface,
    	    but that would mean that we are accepting AX.25 frames
        	from unknown IP hosts, so we'd rather drop it.
          */
        free_p(bp);
        return;
    }

    iface = Ifaces;

    while (iface != NULLIF) {
        if(iface->raw == axip_raw && iface->dev == i)
          /* found the right AX.25 pseudo interface */
            break;
        iface = iface->next;
    }

    if(iface == NULLIF) {
        free_p(bp);
        return;
    }

#ifdef	FLDIGI_KISS
	if (fldigi)
	{
        if (PULLCHAR(&bp) != 0xC0)
			log (-1, "expecting FEND");

        if (PULLCHAR(&bp) != 0x07)
			log (-1, "expecting Port 0, HWID 7");
	}
#endif

    len = len_p(bp) - sizeof(fcs);

#ifdef	FLDIGI_KISS
	if (fldigi)	/* remember there is a FEND at the end we have to dump */
		len--;
#endif

#ifdef J2_SNMPD_VARS
	/* 27Sep2011, Maiko (VE4KLM), This really should be tracked !!! */
	iface->rawrecbytes += len;
	iface->rawrecvcnt++;
	iface->lastrecv = secclock();
#endif

    if(dup_p(&tbp,bp,0,len) != len) {
        free_p(bp);
        return;
    }
    while(len--)
        fcs = (fcs >> 8) ^ fcstab[(fcs ^ PULLCHAR(&bp)) & 0x00ff];
    fcs ^= 0xffff;
    f = PULLCHAR(&bp);
    f |= (PULLCHAR(&bp) << 8);

#ifdef	FLDIGI_KISS
	if (fldigi && (PULLCHAR(&bp) != 0xC0))
		log (-1, "expecting FEND");
#endif

    if(fcs == f){
         /* add some tracing - WG7J */
#ifdef TRACE
        dump(iface,IF_TRACE_IN,CL_AX25,tbp);
#endif
#ifdef RXECHO
        /* If configured, echo this packet before handling - WG7J */
        if(iface && iface->rxecho) {
            struct mbuf *bbp;

            dup_p(&bbp,tbp,0,len_p(tbp));     /* as it was */
            (*iface->rxecho->raw)(iface->rxecho, bbp);
        }
#endif
        ax_recv(iface,tbp);
    } else
		free_p(tbp);
}

#ifdef	IPV6

/* 27Apr2023, Maiko (VE4KLM), new axipv6_input() - AXIP over IPV6 */ 

void axipv6_input (iface, ipv6, bp, rxbroadcast)
struct iface *iface;   /* Input interface */
struct ipv6 *ipv6;   /* IP header */
struct mbuf *bp;  /* AX.25 frame with FCS */
int rxbroadcast;    /* Accepted for now */
{
    int i;

    /* Since the AX.25 frame arrived on an interface that does
       not necessarily support AX.25, we have to find a suitable
       AX.25 interface, or drop the packet.
     */
    /* Try to find a matching AX.25 pseudo interface */
    for(i=0; i < max_ax25; ++i)
	{
		/*
		 * 03May2023, Maiko (VE4KLM), Added this important check so that
		 * once can now run BOTH an AXIP and AXUDP to the same remote IP
		 * address. I never thought to do this 20 years ago when adding
		 * the axudp feature to JNOS back then, hopefully fixed now :]
		 */
		// log (-1, "%d) axtype %d next %d", i, Axip[i].axtype, ipv6->next_header);

		if (Axip[i].axtype != ipv6->next_header)
			continue;
/*
		if (Axip[i].ipaddr6)
			log (-1, "%d) [%s]", i, ipv6shortform (human_readable_ipv6 (Axip[i].ipaddr6), 0));
		else
			log (-1, "%d) [NULL IPV6]", i);
*/
		if (Axip[i].ipaddr6 && !memcmp (Axip[i].ipaddr6, ipv6->source, 16))
			break;
	}

	// log (-1, "incoming axipv6 iface %d axtype %d", i, ipv6->next_header);

	/* 27Apr2023, Maiko (VE4KLM), New breakout function, common code */
    axip_input_process (i, iface, bp);
}

#endif

/* Handle AX.25 frames received inside IP according to RFC-1226 */
void
axip_input(iface,ip,bp,rxbroadcast)
struct iface *iface;    /* Input interface */
struct ip *ip;          /* IP header */
struct mbuf *bp;        /* AX.25 frame with FCS */
int rxbroadcast;    /* Accepted for now */
{
    int i;

    /* Since the AX.25 frame arrived on an interface that does
       not necessarily support AX.25, we have to find a suitable
       AX.25 interface, or drop the packet.
     */
    /* Try to find a matching AX.25 pseudo interface */
    for(i=0; i < max_ax25; ++i)
	{
		/*
		 * 03May2023, Maiko (VE4KLM), Added this important check so that
		 * once can now run BOTH an AXIP and AXUDP to the same remote IP
		 * address. I never thought to do this 20 years ago when adding
		 * the axudp feature to JNOS back then, hopefully fixed now :]
		 */
		if (Axip[i].axtype != ip->protocol)
			continue;

        if (Axip[i].ipaddr == (uint32)(ip->source))
			break;
	}

	// log (-1, "incoming axip iface %d", i);

	/* 27Apr2023, Maiko (VE4KLM), New breakout function, common code */
    axip_input_process (i, iface, bp);
}

#endif	/* end of AXIP */

/* Process incoming AX.25 packets.
 * After optional tracing, the address field is examined. If it is
 * directed to us as a digipeater, repeat it.  If it is addressed to
 * us or to QST-0, kick it upstairs depending on the protocol ID.
 */
extern int32 CT4init;
  
void
ax_recv(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
    struct mbuf *hbp;
    char control;
    struct ax25 hdr;
    struct ax25_cb *axp = (struct ax25_cb*)0;
    struct ax_route *axr;
    struct iface *cross;
    char (*mpp)[AXALEN];
    int mcast,hwaddr,digiaddr = 0;
    char *isrc,*idest;  /* "immediate" source and destination */
    int To_us = 0;      /* Is this a link to us ? */
#ifdef AXUISESSION
    extern int Axui_sock;
#endif
#ifdef	APRS_DIGI
	int nomark = 0;
#endif
  
    /* Pull header off packet and convert to host structure */
    if(ntohax25(&hdr,&bp) < 0){
        /* Something wrong with the header */
        free_p(bp);
        return;
    }
  
    if(iface->flags & LOG_IPHEARD) {
        struct mbuf *nbp;
        struct ip ip;
        int len;
  
        len = len_p(bp);
        if(dup_p(&nbp, bp, 0, len) == len) {
            /* find higher proto, if any */
            (void) PULLCHAR(&nbp);  /* skip control byte */
            if(PULLCHAR(&nbp) == PID_IP) {
                ntohip(&ip,&nbp);
		/* 18Apr2023, Maiko, log_ipheard now uses (void*) pointer for
		 * first argument, to support IPV6 traffic
                    log_ipheard(ip.source,iface);
		 */
                if(ip.version == IPVERSION)
                    log_ipheard(&(ip.source),iface);
            }
            free_p(nbp);
        }
    }

#ifdef  APRSD

    /* 12Apr2001, Maiko (VE4KLM), NOSaprs began with this function call */

	if (bp == NULLBUF)	/* 13Dec2005, Maiko, NULLBUF sometimes appears */
	{
		log (-1, "warning : ax_recv NULLBUF");	/* debug this for now */
	}
	/* 13Dec2005, Maiko, NOTE : I should really use pullchar() here */
	else if ((uchar (bp->data[0]) & ~PF) == UI &&
				uchar (bp->data[1]) == PID_NO_L3)
	{
		/*
		 * 04Dec2001, should let it go through even if it is an APRS frame,
		 * cause what if someone wants to use NOS to digi the frame after ?
		 */
		aprs_processing (&hdr, iface, bp);
	}

#endif
  
    /* If there were digis in this packet and at least one has
     * been passed, then the last passed digi is the immediate source.
     * Otherwise it is the original source.
     */
    if(hdr.ndigis != 0 && hdr.nextdigi != 0)
        isrc = hdr.digis[hdr.nextdigi-1];
    else
        isrc = hdr.source;

    /*
     * 05Apr2021, Maiko (VE4KLM), Now that I am using Robust Packet on a regular
     * basis, especially with APRS frames on 30meters, it would actually be nice
     * to generate a report on calls heard via digis as well as direct, to give
     * a much better idea on how many people are actually 'on frequency' ...
     */
    if (hdr.ndigis != 0 && hdr.nextdigi != 0)
    {

	/*
	 * Oops - forgot to comment out debug 20Apr2021, Maiko
	 *

	char iitmp[20];

	int cnt;

 	log (-1, "src [%s] next digi %d num digis %d",
		pax25 (iitmp, hdr.source), hdr.nextdigi, hdr.ndigis);

	for (cnt = 0; cnt < hdr.ndigis; cnt++)
		log (-1, "[%d] digi [%s]", cnt, pax25 (iitmp, hdr.digis[cnt]));
 	*
	*/

	/* 05Apr2021, Maiko (VE4KLM) experimental - logging DIGI'd source calls now !!! */
	/* 06Apr2021, Maiko, now passing actual digi call to track, this will be cool */

            logDigisrc (iface, hdr.source, hdr.digis[0]);
    }

    /* If there are digis in this packet and not all have been passed,
     * then the immediate destination is the next digi. Otherwise it
     * is the final destination.
     */
    cross = NULLIF;
    if(hdr.ndigis != 0 && hdr.nextdigi != hdr.ndigis)
	{
        idest = hdr.digis[hdr.nextdigi];

	/*	log (-1, "idest [%s]", pax25 (itmp, idest)); */

        if(!addreq(idest,iface->hwaddr))
		{

#ifdef	APRS_DIGI

		/* 08Nov2002, Maiko, My first attempt at supporting WIDEn-N digi */

			digiaddr = aprs_digi (&hdr, idest, &nomark);

			if (digiaddr)
            	To_us = IFACE_LINK; /* need this, or packet is discarded */
			else
#endif

#ifdef	AX25_XDIGI

			/* 24May2005, New custom cross port digipeating code */

			if ((cross = ax25_xdigi (iface, idest, &hdr)) != NULLIF)
			{
				/*
				 * This section of code was put in so that I could get a
				 * snap shot of the digipeater list contained in the ax25
				 * header - helps me develop the code to manipulate it.
				 * 25May2005, Maiko, VE4KLM
				 *
				char iitmp[20];

				int cnt;

				log (-1, "ndigis %d, execute cross port digi", hdr.ndigis);

				for (cnt = 0; cnt < hdr.ndigis; cnt++)
				{
					log (-1, "[%d] digi [%s]", cnt,
						pax25 (iitmp, hdr.digis[cnt]));
				}
				 */

				/* This one confuses me a bit (come back to it) */
				memcpy (idest, iface->hwaddr, AXALEN);
			}
			else
			/*
			 * 24May2005, End of new code
			 */
#endif
            /* Check if digi matches callsign of any other
             * interface for crossband digipeating.
             */
            /* Modified to allow a separate crossband digi call
             * for each interface. This works ONLY cross band,
             * and works independent from the 'ax25 digiport' setting - WG7J
             */
            for(cross = Ifaces; cross != NULLIF; cross = cross->next){
                if(cross->type == CL_AX25) {
                    if((hwaddr = addreq(idest,cross->hwaddr)) == 0)
                        /* Only allow cdigi digipeating if BOTH interfaces
                         * have a cdigi call set ! - WG7J
                         */
                        if(cross != iface && iface->ax25->cdigi[0] != '\0')
                            digiaddr = addreq(idest,cross->ax25->cdigi);
                    if(hwaddr || digiaddr) {
                        /* Swap callsigns so that the reply
                         * can be crossband digipeated in
                         * the other direction.
                         */
                        if(hwaddr)
                            memcpy(idest,iface->hwaddr,AXALEN);
                        else
                            memcpy(idest,iface->ax25->cdigi,AXALEN);
                        break;
                    }
                }
            }
        }
    }
    else
        idest = hdr.dest;
  
    /* Don't log our own packets if we overhear them, as they're
     * already logged by axsend() and by the digipeater code.
     */
#ifdef NETROM
    if(!addreq(isrc,iface->hwaddr) &&
#ifdef MAILBOX
        !addreq(isrc,iface->ax25->bbscall) &&
#endif
    (!Nr_iface || !addreq(isrc,Nr_iface->hwaddr))) {
#else
        if(!addreq(isrc,iface->hwaddr)
#ifdef MAILBOX
            && !addreq(isrc,iface->ax25->bbscall)
#endif
        ) {
#endif
            logsrc(iface,isrc);
            logdest(iface,idest);
        }
    /* Examine immediate destination for a multicast address */
        mcast = 0;
        for(mpp = Ax25multi;(*mpp)[0] != '\0';mpp++){
            if(addreq(idest,*mpp)){
                mcast = 1;
                break;
            }
        }
  
    /* Now check for any connection already in the AX.25 cb list
     * This allows netrom user connects (with inverted ssid's) ,
     * connections already established etc.. to pass.
     * There should be no more digis needed!
     * Added 11/15/91 WG7J
     */
        if(hdr.nextdigi == hdr.ndigis)
        /* See if the source and destination address are in hash table */
            if((axp = find_ax25(hdr.dest,hdr.source,iface)) != NULLAX25)
                To_us = KNOWN_LINK;
  
    /* Check to see if this is the Netrom interface callsign
     * or the alias ! Accept any SSID on the alias.
     * NOTE: This also allows digipeating via those calls!
     * Added 11/15/91 WG7J
     */
        if(!To_us) {
            if(!memcmp(idest,Myalias,ALEN))
                To_us = ALIAS_LINK;     /* this is for the alias */
            else
#ifdef NETROM
                if((iface->flags & IS_NR_IFACE) && addreq(idest,Nr_iface->hwaddr))
                    To_us = NETROM_LINK;    /* this is for the netrom callsign! */
                else
#endif
                    if(addreq(idest,iface->hwaddr))
                        To_us = IFACE_LINK;     /* this is to the interface call */
#ifdef MAILBOX
                    else if(addreq(idest,iface->ax25->bbscall))
                        To_us = ALIAS_LINK;       /* This will jumpstart */
#endif
#ifdef CONVERS
                    else if((iface->flags & IS_CONV_IFACE) && addreq(idest,Ccall))
                        To_us = CONF_LINK;      /* this is for the conference call */
#endif
#ifdef TTYCALL
                    else if (addreq(idest, Ttycall))
                        To_us = TTY_LINK;
#endif
#ifdef TNCALL
                    else if (addreq(idest, tncall))
                        To_us = TN_LINK;
#endif
  
        }
  
#ifdef AXUISESSION	/* K5JB: for axui, mustn't discard other UI frames */
        if(Axui_sock != -1 && !mcast && !To_us){  /* UI Session active? */
            control = *bp->data & ~PF;
            if(uchar(control) == UI)
                mcast = 1;	/* will fake it */
        }
#endif
        if(!To_us && !mcast && !cross){
        /* Not a broadcast, and not addressed to us. Inhibit
         * transmitter to avoid colliding with addressed station's
         * response, and discard packet.
         */
            free_p(bp);
            return;
        }
  
    /* At this point, packet is either addressed to us, or is
     * a multicast.
     */
        if(hdr.nextdigi < hdr.ndigis){
        /* Packet requests digipeating. See if we can repeat it. */
            if(digiaddr || ((iface->flags & AX25_DIGI) && !mcast)){
            /* Yes, kick it back out. htonax25 will set the
             * repeated bit.
             */
#ifdef	APRS_DIGI
				if (!nomark)
#endif
                hdr.nextdigi++;
                if(cross)   /* Crossband digipeat */
                    iface = cross;
                if(digiaddr || iface->flags & AX25_DIGI) {
                    if((hbp = htonax25(&hdr,bp)) != NULLBUF){
                        if(digiaddr)
                            logsrc(iface,iface->ax25->cdigi);
                        else
                            logsrc(iface,iface->hwaddr);
                        if(iface->forw != NULLIF){
                            logdest(iface->forw,hdr.digis[hdr.nextdigi]);
                            (*iface->forw->raw)(iface->forw,hbp);
                        } else {
                            logdest(iface,hdr.digis[hdr.nextdigi]);
                            (*iface->raw)(iface,hbp);
                        }
                        bp = NULLBUF;
                    }
                }
            }
            free_p(bp); /* Dispose if not forwarded */
            return;
        }
    /* If we reach this point, then the packet has passed all digis,
     * and is either addressed to us or is a multicast
     */
        if(bp == NULLBUF){
            return;      /* Nothing left */
        }
  
    /* If this is a connect, then remove old ax route (if not perm) - K5JB */
       if(uchar(*bp->data & ~PF) == SABM)
           (void)ax_drop(hdr.source,iface,0);      /* K5JB */

    /* If there's no entry in the routing table and not a multicast pkt, and
     * this packet has digipeaters, create an entry.  -- n5knx/k5jb 1.10L
     */
        if(!mcast && ((axr = ax_lookup(hdr.source,iface)) == NULLAXR)
        && hdr.ndigis > 0){
            char digis[MAXDIGIS][AXALEN];
            int i,j;
  
        /* Construct reverse digipeater path */
            for(i=hdr.ndigis-1,j=0;i >= 0;i--,j++){
                memcpy(digis[j],hdr.digis[i],AXALEN);
                digis[j][ALEN] &= ~(E|REPEATED);
            }
            ax_add(hdr.source,AX_AUTO,digis,hdr.ndigis,iface);
        }
    /* Sneak a peek at the control field. This kludge is necessary because
     * AX.25 lacks a proper protocol ID field between the address and LAPB
     * sublayers; a control value of UI indicates that LAPB is to be
     * bypassed.
     */
        control = *bp->data & ~PF;
  
        if(uchar(control) == UI){
            int pid;
            struct axlink *ipp;
  
            (void) PULLCHAR(&bp);
            if((pid = PULLCHAR(&bp)) == -1)
                return;     /* No PID */
        /* Find network level protocol and hand it off */
            for(ipp = Axlink;ipp->funct != NULL;ipp++){
                if(ipp->pid == pid)
                    break;
            }
            if(ipp->funct != NULL)
                (*ipp->funct)(iface,NULLAX25,hdr.source,hdr.dest,bp,mcast);
            else
                free_p(bp);
            return;
        }
    /* Everything from here down is connected-mode LAPB, so ignore
     * multicasts
     */
        if(mcast){
            free_p(bp);
            return;
        }
    /* At this point, if we already have a connection on its way,
     * we already have found the control block !
     * 11/15/91 WG7J/PA3DIS
     */
        if(To_us !=KNOWN_LINK) {
        /* This is a new connection to either the interface call,
         * or to the netrom-interface call or system alias.
         * Create a new ax25 entry for this guy,
         * insert into hash table keyed on his address,
         * and initialize table entries
         */
            if((axp = cr_ax25(hdr.dest,hdr.source,iface)) == NULLAX25){
                free_p(bp);
                return;
            }
#ifdef CONVERS
        /* set a different T4 if conference link - WG7J */
            if(To_us == CONF_LINK)
                set_timer(&axp->t4,CT4init * 1000);
#endif
            axp->jumpstarted = To_us;
            if(hdr.cmdrsp == LAPB_UNKNOWN)
                axp->proto = V1;    /* Old protocol in use */
        }
        lapb_input(axp,hdr.cmdrsp,bp);
    }
  
/* General purpose AX.25 frame output */
    int
    sendframe(axp,cmdrsp,ctl,data)
    struct ax25_cb *axp;
    int cmdrsp;
    int ctl;
    struct mbuf *data;
    {
        return axsend(axp->iface,axp->remote,axp->local,cmdrsp,ctl,data);
    }
/* Find a route for an AX.25 address.
 * Code to remove SSID field C/R- and E-bits in ax_lookup(), ax_add() and
 * ax_drop() added by vk6zjm 4/5/92. This eliminates duplicate AX25 routes.
 * 1992-05-28 - Added interface -- sm6rpz
 */
  
    struct ax_route *
    ax_lookup(target,ifp)
    char *target;
    struct iface *ifp;
    {
        register struct ax_route *axr;
        struct ax_route *axlast = NULLAXR;
        char xtarget[AXALEN];
  
    /* Remove C/R and E bits in local copy only */
        memcpy(xtarget,target,AXALEN);
        xtarget[AXALEN-1] &= SSID;
  
        for(axr = Ax_routes; axr != NULLAXR; axlast = axr,axr = axr->next){
            if(memcmp(axr->target,xtarget,AXALEN) == 0 && axr->iface == ifp){
                if(axr != Ax_routes){
                /* Move entry to top of list to speed
                 * future searches
                 */
                    axlast->next = axr->next;
                    axr->next = Ax_routes;
                    Ax_routes = axr;
  
                }
                return axr;
            }
        }
        return axr;
    }
/* Add an entry to the AX.25 routing table */
    struct ax_route *
    ax_add(target,type,digis,ndigis,ifp)
    char *target;
    int type;
    char digis[][AXALEN];
    int ndigis;
    struct iface *ifp;
    {
        register struct ax_route *axr;
        char xtarget[AXALEN];
  
        if(ndigis < 0 || ndigis > MAXDIGIS || addreq(target,ifp->hwaddr))
            return NULLAXR;
  
    /* Remove C/R and E bits in local copy only */
        memcpy(xtarget,target,AXALEN);
        xtarget[AXALEN-1] &= SSID;
  
        if((axr = ax_lookup(xtarget,ifp)) == NULLAXR){
            axr = (struct ax_route *)callocw(1,sizeof(struct ax_route));
            axr->next = Ax_routes;
            Ax_routes = axr;
            memcpy(axr->target,xtarget,AXALEN);
            axr->ndigis = ndigis;
            axr->iface = ifp;
        }else   /* added with 1.10L - K5JB */
            if(axr->type == AX_PERM)
                return axr;
        axr->type = type;
        axr->mode = AX_DEFMODE;     /* set mode to default */
        if(axr->ndigis != ndigis)
            axr->ndigis = ndigis;
  
        memcpy(axr->digis,digis[0],ndigis*AXALEN);
        return axr;
    }
    int
    ax_drop(target,ifp,authority)   /* added authority - K5JB */
    char *target;
    struct iface *ifp;
    int authority;  /* 0 => retain PERMed routes, 1 => force drop of PERMed routes */
    {
        register struct ax_route *axr;
        struct ax_route *axlast = NULLAXR;
        char xtarget[AXALEN];
  
    /* Remove C/R and E bits in local copy only */
        memcpy(xtarget,target,AXALEN);
        xtarget[AXALEN-1] &= SSID;
  
        for(axr = Ax_routes;axr != NULLAXR;axlast=axr,axr = axr->next)
            if(memcmp(axr->target,xtarget,AXALEN) == 0 && axr->iface == ifp)
                break;
        if(axr == NULLAXR)
            return -1;  /* Not in table! */
        if(!authority && axr->type == AX_PERM)  /* K5JB */
            return 0;
        if(axlast != NULLAXR)
            axlast->next = axr->next;
        else
            Ax_routes = axr->next;
  
        free((char *)axr);
        return 0;
    }
/* Handle ordinary incoming data (no network protocol) */
    void
    axnl3(iface,axp,src,dest,bp,mcast)
    struct iface *iface;
    struct ax25_cb *axp;
    char *src;
    char *dest;
    struct mbuf *bp;
    int mcast;
    {
        if(axp == NULLAX25){
#ifdef AXUISESSION
            axui_input (iface, axp, src,dest,bp,mcast);
#else
            free_p(bp);
        /* beac_input(iface,src,bp); */
#endif
        } else {
            append(&axp->rxq,bp);
            if(axp->r_upcall != NULLVFP((struct ax25_cb*,int)))
                (*axp->r_upcall)(axp,len_p(axp->rxq));
        }
    }
  
#ifdef  AXIP

/*
 * Check if the UDP packet qualifies as a possible AXUDP frame
 *
 * Why the need to go through all interface information ? Well, because
 * I wanted to give the user the flexibilty to configure UDP source and
 * destination ports, instead of hardcoding them to the value of 93.
 *
 * This function is called from the udp.c module.
 *
 * 17Nov2013, Maiko, Allow from ANY source port (if configured), just make
 * darn sure you firewall the destination port to the remote system ipaddr,
 * hopefully this makes people who use NAT with port mapping, happy :)
 *
 * 10Dec2013, Maiko, Take a step further, some firewalls do really wierd port
 * mapping for UDP traffic, so use a negative Axip[i].source to indicate dest
 * port for outgoing traffic.
 */

int axudp_frame (udp)
	struct udp *udp;
{
	int i;

	for (i = 0; i < max_ax25; ++i)
	{
		/*
		 * 04May2023, Maiko, Good grief, this is why when I send out AXUDP
		 * the response comes back AXIP interface, forgot to check the new
		 * axtype field, this should fix this once and for all - uggg !
		 */
		if (Axip[i].axtype != UDP_PTCL)
			continue;

		if (Axip[i].dest == udp->dest && (Axip[i].source == -1 || Axip[i].source == udp->source))
			break;
	}

	return (i != max_ax25);
}


/*
 * Attach a fake AX.25 interface for AX.25 on top of IP or in UDP
 *
 * argv[0] == "axip" or "axudp"
 * argv[1] == name of new interface
 * argv[2] == MTU
 * argv[3] == hostname of remote end of wormhole
 * argv[4] == optional AX.25 callsign for this interface, must be specified
 *            and be different from any other interface callsign if crossband
 *            digipeating is going to work properly.
 *
 * For AXUDP, extra options as follows ...
 *
 * I suppose I could have left the UDP ports at 93 but I wanted to give
 * the user some flexibility to choose what UDP ports can be used.
 *
 * argv[5] = optional UDP source port.
 * argv[6] = optional UDP destination port.
 *
 * In order to specify the optional UDP ports you obviously have to
 * specify something for the optional AX25.callsign. Just use '-'
 * for argv[4], if you are not using it.
 *
 */
    int
    axip_attach(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        int i, port;
        struct iface *ifp;
 
/* 30Aug2010, Maiko (VE4KLM), Iface names can now be ANY length */
#ifndef JNOS20_SOCKADDR_AX 
     /* Check for too long iface names - WG7J */
        if(strlen(argv[1]) >= ILEN) {
            tprintf("interface max %d chars\n",ILEN-1);
            return -1;
        }
#endif
        if(if_lookup(argv[1]) != NULLIF) {
            tprintf("interface %s already attached\n",argv[1]);
            return -1;
        }
        for(i=0; i < max_ax25; ++i)
	{
#ifdef	IPV6
            if (!(Axip[i].ipaddr) && !(Axip[i].ipaddr6))
#else
            if(Axip[i].ipaddr == 0)
#endif
                break;
	}

        if(i == max_ax25) {
            j2tputs("too many AX25 interfaces attached\n");
            return -1;
        }

	/* 01Aug2004, Maiko, New built in resolve timer for dynamic links */
	Axip[i].axhost = j2strdup (argv[3]);

	/*
	 * 07Feb2011, Maiko, Attach the interface, even if JNOS can't resolve the
     * domain name at the time of attach. It might start working later on and
	 * come to think of it, this actually solves the issue of having to attach
	 * any dyndns type interfaces (using 'at' command) after JNOS has started,
	 * because now it will always try to resolve - so the 'at' solution is not
	 * technically needed anymore after this simple little fix.
	 *
     * However, the main reason I am doing this is that it keeps the order of
	 * all JNOS interfaces consistent - something my recent SNMPD & MRTG work
	 * depends quite heavily on. Post note, Apr 27, 2023, well not anymore.
	 *
	 *  Axip[i].ipaddr = resolve (argv[3]);
	 *
	 * Actually, resolve should NOT be called period while autoexec.nos is
	 * being parsed. Leave the ipaddr undefined, then let axresolv() do the
	 * job a bit later after the first expiry occurs of each of the timers.
	 *
	 * 27Apr2023, Maiko, Can now configure an IPV6 address for the wormhole
	 *
	 */

	Axip[i].ipver = IPVERSION;	/* default to IPV4, 28Apr2023, Maiko */

	if (isalpha (*argv[3]))
		Axip[i].ipaddr = -1; /* do NOT use 0, or you loose the Axip[] slot */
#ifdef	IPV6
	else if (strchr (argv[3], ':'))
	{
		struct j2sockaddr_in6 sock6;

		if (j2_ipv6_asc2ntwk (argv[3], &sock6) != 1)
		{
			tprintf("Resolver %s unknown\n", argv[3]);
			return -1;
		}

		Axip[i].ipaddr6 = malloc (16);	/* very important */

		copyipv6addr (sock6.sin6_addr.s6_addr, Axip[i].ipaddr6);

		Axip[i].ipver = IPV6VERSION;	/* 28Apr2023, Maiko */
	}
#endif
	else
		Axip[i].ipaddr = aton (argv[3]);

	ifp = (struct iface *) callocw (1, sizeof (struct iface));

        ifp->dev = i;
        ifp->addr = Ip_addr;
        ifp->name = j2strdup(argv[1]);
        ifp->hwaddr = mallocw(AXALEN);
        memcpy(ifp->hwaddr,Mycall,AXALEN);
        ifp->mtu = atoi(argv[2]);
        setencap(ifp,"AX25");

	log (-1, "attach [%s] iface %d", ifp->name, ifp->dev);

	if (argc > 4 && *argv[4] != '-')
	{
        free(ifp->hwaddr);
        ifp->hwaddr = mallocw(ifp->iftype->hwalen);
        (*ifp->iftype->scan)(ifp->hwaddr,argv[4]);
	}

	/* source and dest ports not used in AXIP, set to 0 */
	Axip[i].dest = Axip[i].source = 0;

	Axip[i].axtype = AX25_PTCL; /* 03May2023, Maiko, default Axip for address check for loop */

	/* if an AXUDP interface, get extra parameters if present */
	if (!memcmp ("axu", argv[0], 3))
	{
		/* default the source and dest UDP ports to 93 */
		Axip[i].dest = Axip[i].source = 93;

		Axip[i].axtype = UDP_PTCL;	/* 03May2023, Maiko, address check for loop */

		/* allow users to customize which UDP ports to use for AXUDP */
		if (argc > 5)
		{
			if ((port = atoi (argv[5])) != 0)
				Axip[i].source = port;
			else
			{
				j2tputs ("invalid source port\n");
				return -1;
			}
		}

		if (argc > 6)
		{
			if ((port = atoi (argv[6])) > 0)
				Axip[i].dest = port;
			else
			{
				j2tputs ("invalid destination port\n");
				return -1;
			}
		}
	}
        ifp->raw = axip_raw;
        ifp->stop = axip_stop;
        ifp->next = Ifaces;
        Ifaces = ifp;

		/*
		 * resolve the ip address of the remote host on a regular
		 * basis only IF it's a domain name, and not an ip address.
		 * 01Aug2004, Maiko, Every few minutes, check host name.
		 */
		if (isalpha (*argv[3]))
		{
			AXIPCFG *axcfg = &Axip[i];

			log (-1, "axudp [%s] start resolving", argv[3]);

			axcfg->resolv_t.func = (void (*)(void*))axrtick;
			axcfg->resolv_t.arg = (void*)axcfg;
			/* 07Feb2011, Maiko, Until it resolves, check every minute */
			set_timer (&axcfg->resolv_t, 60000);
			start_timer (&axcfg->resolv_t);
		}

        return 0;
    }

    static int
    axip_stop(iface)
    struct iface *iface;
    {
		Axip[iface->dev].source = Axip[iface->dev].dest = 0;
		Axip[iface->dev].ipaddr = 0;

        return 0;
    }
 
 
/* raw routine for sending AX.25 on top of IP or UDP now */
    static int
    axip_raw(iface,bp)
    struct iface *iface;    /* Pointer to interface control block */
    struct mbuf *bp;        /* Data field */
    {
        int16 len, fcs = 0xffff;
        struct mbuf *bp1;
		struct socket lsocket, rsocket;
#ifdef	IPV6
		struct j2socketv6 lsocket6, rsocket6;	/* 28Apr2023, Maiko */
#endif

#ifdef	FLDIGI_KISS
		struct mbuf *newbp;
		int fldigi = fldigi_iface (iface);
#endif

	/*
	 * 08Feb2011, Maiko, It is now possible for 'ipaddr' to have a
	 * value of -1, indicating waiting for domain name resolution,
	 * in which case we should not even try to send anything !
	 */
		if (Axip[iface->dev].ipaddr == -1)
			return -1;

        dump(iface,IF_TRACE_OUT,iface->type,bp);
        iface->rawsndcnt++;
        iface->lastsent = secclock();
        len = len_p(bp);
#ifdef J2_SNMPD_VARS
        iface->rawsndbytes += len;
#endif
        if(dup_p(&bp1,bp,0,len) != len) {
            free_p(bp);
            return -1;
        }
        while (len--)      /* calculate FCS */
            fcs = (fcs >> 8) ^ fcstab[(fcs ^ PULLCHAR(&bp1)) & 0x00ff];
  
        fcs ^= 0xffff; /* final FCS (is this right?) */

        if((bp1 = alloc_mbuf(sizeof (fcs)
#ifdef	FLDIGI_KISS
 + fldigi
#endif
)) == NULLBUF) {
            free_p(bp);
            return -1;
        }
        *bp1->data = fcs & 0xff;
        *(bp1->data+1) = (fcs >> 8) & 0xff;
        bp1->cnt += sizeof(fcs);

#ifdef	FLDIGI_KISS
		if (fldigi)
		{
			*(bp1->data+2) = 0xC0;	/* trailing FEND required by flgdigi */

        	bp1->cnt += 1;
		}
#endif
        append(&bp,bp1);

#ifdef	FLDIGI_KISS
		if (fldigi)
		{
			/* put in the starting FEND and PORT|HWID needed by flgdigi */
			newbp = pushdown (bp, 2);

			newbp->data[0] = 0xC0;
			newbp->data[1] = 0x07;

			bp = newbp;
		}
#endif
		/* 04May2023, Maiko, Now use axtype to determine the protocol */
		if (Axip[iface->dev].axtype == AX25_PTCL)
		{
			// log (-1, "sending axip ipver %d", Axip[iface->dev].ipver);

#ifdef	IPV6
	 		/* 28Apr2023, Maiko, new ipver struct member is helpful here */
			if (Axip[iface->dev].ipver == IPV6VERSION)
				return ipv6_send (myipv6addr, Axip[iface->dev].ipaddr6, AX25_PTCL, 0, 0, bp, 0, 0, 0);
			else
#endif
        	return ip_send (INADDR_ANY, Axip[iface->dev].ipaddr, AX25_PTCL, 0, 0, bp, 0, 0, 0);
		}
		else
		{
			// log (-1, "sending axudp ipver %d iface %d", Axip[iface->dev].ipver, iface->dev);

#ifdef	IPV6

	 		/* 28Apr2023, Maiko, new ipver struct member is helpful here */
			if (Axip[iface->dev].ipver == IPV6VERSION)
			{
				zeroipv6addr (lsocket6.address);	/* this is equiv to INADDR6ANY */

 				/* 17Nov2013, Maiko, if from ANY source port, use dest port */
				if (Axip[iface->dev].source == -1)
					lsocket6.port = Axip[iface->dev].dest;
				else
					lsocket6.port = Axip[iface->dev].source;

				copyipv6addr (Axip[iface->dev].ipaddr6, rsocket6.address);

				rsocket6.port = Axip[iface->dev].dest;

				/* We set the length to zero and let the function determine it */
				return send_udpv6 (&lsocket6, &rsocket6, 0, 0, bp, 0, 0, 0);
			}
			else
			{
#endif
			lsocket.address = 0;

 			/* 17Nov2013, Maiko, if from ANY source port, use dest port */
			if (Axip[iface->dev].source == -1)
				lsocket.port = Axip[iface->dev].dest;
			else
				lsocket.port = Axip[iface->dev].source;

			rsocket.address = Axip[iface->dev].ipaddr;

			rsocket.port = Axip[iface->dev].dest;

			/* We set the length to zero and let the function determine it */
			return send_udp (&lsocket, &rsocket, 0, 0, bp, 0, 0, 0);
#ifdef	IPV6
			}
#endif
		}	
    }

    void paxipdest(iface)
    struct iface *iface;
    {
        if (iface && iface->stop == axip_stop)
	{
            tprintf("         AXIP dest %s\n",
		inet_ntoa(Axip[iface->dev].ipaddr));
	}
    }

/*
 * 01Aug2004, Maiko, New functions for dynamic axudp links.
 * 08Feb2011, Maiko, A few log mods for recent changes in that
 * we will continue to attach interface even if we can't resolve
 * the domain name of the remote host at the time.
 *
 * 27Apr2023, Maiko (VE4KLM), Now supporting IPV6 name resolution :]
 */

static void axresolve (int notused, void *ptr, void *ptr2)
{
	AXIPCFG *axcfg = (AXIPCFG*)ptr;
#ifdef	IPV6
	struct j2sockaddr_in6 fsocket6;
#endif
	uint32 ipaddr;

	char logmsg[100], *lmptr = logmsg;

	lmptr += sprintf (lmptr, "axudp [%s] ", axcfg->axhost);

#ifdef	IPV6

	/* 27Apr2023, Maiko, AXIP over IPV6 is now possible */

	if (ipv6iface)
		copyipv6addr (resolve6 (axcfg->axhost), fsocket6.sin6_addr.s6_addr);
	else
		zeroipv6addr (fsocket6.sin6_addr.s6_addr);

	if (fsocket6.sin6_addr.s6_addr[0] != 0x00)		/* if resolve fails, goto ipv4 */
	{
		set_timer (&axcfg->resolv_t, AXRESOLVTIMER);

		if (memcmp (fsocket6.sin6_addr.s6_addr, axcfg->ipaddr6, 16))
		{
			/*
			 * 28Apr2023, Maiko, Finish up this code, try and get something
			 * configured with Bob either today or tomorrow
			 */
			if (axcfg->ipaddr == -1)
				lmptr += sprintf (lmptr, "started at [%s]", ipv6shortform (human_readable_ipv6 (fsocket6.sin6_addr.s6_addr), 0));
			else
				lmptr += sprintf (lmptr, "changed to [%s]", ipv6shortform (human_readable_ipv6 (fsocket6.sin6_addr.s6_addr), 0));

			if (!(axcfg->ipaddr6))
				axcfg->ipaddr6 = malloc (16);	/* very important */

			copyipv6addr (fsocket6.sin6_addr.s6_addr, axcfg->ipaddr6);

			axcfg->ipaddr = 0;
		}
		else lmptr += sprintf (lmptr, "no change");
	}
	else

#endif

	if ((ipaddr = resolve (axcfg->axhost)))
	{
		/* 07Feb2011, Maiko, If it resolves, back off timer to every 1/2 hr */
		set_timer (&axcfg->resolv_t, AXRESOLVTIMER);

		if (ipaddr != axcfg->ipaddr)
		{
			if (axcfg->ipaddr == -1)
				lmptr += sprintf (lmptr, "started at [%s]", inet_ntoa (ipaddr));
			else
				lmptr += sprintf (lmptr, "changed to [%s]", inet_ntoa (ipaddr));

			axcfg->ipaddr = ipaddr;
		}
		else lmptr += sprintf (lmptr, "no change");
	}
	else lmptr += sprintf (lmptr, "unable to resolve");

	log (-1, "%s", logmsg);	/* 27Oct2009, changed the way this is logged */

	start_timer (&axcfg->resolv_t);	/* restart the timer */
}

/*
 * 11Nov2004, Maiko, New function - I've rewritten the way that the
 * domain name for axudp/axip remote hosts is resolved. The proper way
 * to do the name resolution is to spawn a separate process and do the
 * resolve() call within that new process. Calling resolve() within the
 * function pointed to by the 'timer.func' argument is *not* wise.
 *
 * The smtp_fwd code in smtpcli.c is done in a similar manner.
 *
 * I had to rewrite this because JNOS was suddenly going into a strange
 * state of very HIGH cpu usage, effectively shutting it down. Thanks to
 * Barry (K2MF) for lending credibility to my suspicions. Live & Learn.
 *
 */

static void axrtick (void *ptr)
{
	AXIPCFG *axcfg = (AXIPCFG*)ptr;

	if (newproc ("axr", 512, axresolve, 0, ptr, NULL,0) == NULLPROC)
	{
		log (-1, "axudp could not start a resolver process");

		start_timer (&axcfg->resolv_t);
	}
}

#ifdef	DONT_COMPILE
		
/* Main entry point for the resolver of remote host names for AXIP/AXUDP */

static void startaxresolve (AXIPCFG *axcfg, int32 interval)
{
	if (axcfg->ipaddr == -1)
		log (-1, "axudp [%s] started without resolution", axcfg->axhost);
	else
		log (-1, "axudp [%s] started at [%s]",
			axcfg->axhost, inet_ntoa (axcfg->ipaddr));

	axcfg->resolv_t.func = (void (*)(void*))axrtick; /* called on timeout */

	axcfg->resolv_t.arg = (void*)axcfg;

	set_timer (&axcfg->resolv_t, interval);

	start_timer (&axcfg->resolv_t);	/* start the timer */
}

#endif	/* end of DONT_COMPILE */

#endif	/* end of AXIP */
  
#endif	/* end of AX25 */

