/* $Id: scc.c,v 1.2 2009/07/04 02:30:03 ve4klm Exp $ */

/* Software DCD operation is experimental. Ever so experimental....
 * I'd be interested to hear how you get on with it (e-mail to
 * G8FSL@GB7MHD.#22.GBR.EU or g8fsl@g8fsl.ampr.org or adsb@bnr.co.uk)
 *
 * This compiler switch adds support to the code - the feature can
 * then be enabled on a per channel basis by appending an "s" to the
 * channel attach line
 */

#define SCC_SOFTWARE_DCD


/* This controls what to do in AX25 mode when one can't get a transmit
 * opportunity before the defer timer ("param idle") times out.
 * If you #define the following, it throws frames away, otherwise it
 * just goes ahead and transmits anyway.
 */

#undef THROW_AWAY_AFTER_DEFER_TIMEOUT


/* Generic driver for Z8530 boards, modified from the PE1CHL
 * driver for use with NOS. This version also supports the NRS
 * mode when used as an asynch port. Device setup is similar to
 * that of the PE1CHL version, with the addition of user specification
 * of buffer size (bufsize). See the file "scc.txt" for general
 * information on the use of this driver and setup procedures.
 *
 * General differences between this driver and the original version:
 *
 * 1)	Slip encoding and decoding is not done in the driver, but
 *	using the routines in slip.c, and these routines are supported
 *	in a manner similar to the asynch routines for the 8250. The
 *	input is handled via fifo buffer, while output is direct. The
 *	routines scc_send and get_scc are called via pointers in the
 *	Slip and Nrs structs for the particular channel.
 *
 * 2)	The timer routine, scctim, is not installed directly in the
 *	timer interrupt chain, but is called through the systick routine
 *	in pc.c.
 *
 *	3) Facilities of nos are used whenever possible in place of direct
 *	structure or variable manipulation. Mbuf management is handled
 *	this way, along with interface initialization.
 *
 * 4)	Nrs mode support is added in a manner similar to that of the
 *	Slip support. I have not had an opportunity to test this, but
 *	it is essentially identical to the way the 8250 version works.
 *
 * 5)	Callsign specification on radio modes (kiss,nrs,ax25) is an
 *	option. If not supplied, the value of Mycall will be used.
 *
 * 6)	Bufsize specification is now a parameter on setup of each channel.
 *	This is the size of the fifo on asynch input, and the size of
 *	mbuf buffers for sdlc mode. Since the fifo buffer can fill up,
 *	this value should be reasonably large for asynch mode. Mbufs
 *	are chained when they fill up, so having a small bufsize with
 *	sdlc modes (ax25) does not result in loss of characters.
 *
 * 7)	Because slip and nrs decoding is handled outside the driver,
 *	sccstat cannot be used to report sent and receive packet counts
 *	in asynch mode, and these fields are blanked on display in asynch
 *	modes.
 *
 *
 * I am interested in setting up some default initializations for
 * the popular Z8530 boards, to minimize user problems in constructing
 * the proper attach init entries. These would allow for shortened
 * entries to use the defaults, such as "attach scc 1 init drsi" to
 * attach a DRSI board in standard configuration at its default address.
 * Since I do not have complete technical information on all such boards,
 * I would very much appreciate any information that users can provide
 * me regarding particular boards.
 *
 * 1/25/90
 *
 * Modifications:
 *
 * 2/17/90:
 *
 * 1) Added mods from PE1CHL which reflect relevent changes to the
 *    scc driver in his version of net between 10/89 and 1/90. Changes
 *    incorporated include additional delays in sccvec.asm, addition
 *    of external clock mode, and initialization for the 8536 as a
 *    clock divider on the DRSI board. "INLINE" is a slight delay
 *    for register access incorporated for use with the inline i/o
 *    code in MSC. This may not be useful or necessary with TURBO.
 *    Changes making "TPS" a variable were not added, since the
 *    scc timer does not install itself on the hardware interrupt
 *    in this version.
 *
 *
 * Ken Mitchum, KY3B       km@cs.pitt.edu  km@dsl.pitt.edu
 *                             or mail to the tcpip group
 * 5/07/92:
 *
 * 1) Changes needed for BayCom-USCC card were made. CTS-Interrupt
 *    was disabled, because it keeps the system hanging. Clock sources
 *    will be changed when transmitter is switched on/off, if internal
 *    clock is used. Now DF9ICs G3RUH compatible modem is supported,
 *    which needs NRZ-signals and provides external clocks.
 *
 * Rene Stange, DG0FT @DB0KG.DEU.EU
 *
 */

/* Added ANSI-style prototypes, reformatted source, minor delinting.
 * Integrated into standard 900201 NOS by KA9Q.
 */

/* Updated Jan/Feb/Apr 1995 by Andrew Benham, G8FSL.
 * Added support for:
 *	multiple boards (they must share one interrupt, and share the
 *	  same intack method)
 *	"easy attach" lines
 *	10ms timing reference using a spare SCC channel
 *	ESCC chips (Z85230)
 *	NRZ AX25 operation (for DF9IC modem)
 *	User-supplied R11 value
 *	Open Squelch operation
 */

/*
 * Generic driver for Z8530 SCC chip in SLIP, KISS or AX.25 mode.
 *
 * Written by R.E. Janssen (PE1CHL) using material from earlier
 * EAGLE and PC100 drivers in this package.
 *
 * The driver has initially been written for my own Atari SCC interface
 * board, but it could eventually replace the other SCC drivers.
 *
 * Unfortunately, there is little consistency between the different interface
 * boards, as to the use of a clock source, the solution for the fullduplex
 * clocking problem, and most important of all: the generation of the INTACK
 * signal.  Most designs do not even support the generation of an INTACK and
 * the read of the interrupt vector provided by the chip.
 * This results in lots of configuration parameters, and a fuzzy
 * polltable to be able to support multiple chips connected at one interrupt
 * line...
 *
 */

#include <ctype.h>
#include <time.h>
#include <dos.h>
#include "global.h"
#include "config.h"
#ifdef SCC
#include "mbuf.h"
#include "netuser.h"
#include "proc.h"
#include "iface.h"
#include "pktdrvr.h"
#include "slip.h"
#include "nrs.h"
#include "i8250.h"
#include "scc.h"
#include "z8530.h"
#include "z8536.h"
#include "ax25.h"
#include "trace.h"
#include "pc.h"
#include "kiss.h"
#include "devparam.h"

/* interrupt handlers */
extern INTERRUPT sccvec();
extern INTERRUPT sccnovec();

struct sccboard *SccBoards;		/* pointer to a linked list of SCC boards */

int interrupt_number = -1;		/* The IRQ to use. All boards must use the same IRQ */

int highest_device_attached = -1;	/* The highest device number attached */

/* variables used by the SCC interrupt handler in sccvec.asm */
static INTERRUPT (*Orgivec)();		/* original interrupt vector */
struct sccchan *Sccchan[2 * MAXSCC] = {0};	/* information per channel */
ioaddr Sccvecloc = {0};			/* location to access for SCC vector */
unsigned char Sccmaxvec = {0};		/* maximum legal vector from SCC */
ioaddr Sccpolltab[2*MAXSCC+1] = {0};	/* polling table when no vectoring */

#if defined(INLINE)
static unsigned scc_delay __ARGS((unsigned v));

static unsigned scc_delay (v)		/* delay for about 5 PCLK cycles */
unsigned v;				/* pass-through used for input */

{
	register int i,j;		/* it takes time to save them */

	return v;			/* return the passed parameter */
}
#endif

unsigned char Random = 0;		/* random number for p-persist */
int ticker_channel = -1;

static int scc_call __ARGS((struct iface *ifp,char *call));
static int scc_init __ARGS((char *label,int nchips,ioaddr iobase,int space,
	int aoff,int boff,int doff,ioaddr intack,int ivec,long clk,int pclk,
	int hwtype,int hwparam,int ticker));
static int scc_raw __ARGS((struct iface *ifp,struct mbuf *bp));
static int scc_stop __ARGS((struct iface *ifp));
static int get_scc __ARGS((int dev));
static int scc_send __ARGS((int dev,struct mbuf *bp));
static int scc_async __ARGS((struct sccchan *scc));

static void scc_sdlc __ARGS((struct sccchan *scc));
static void scc_tossb __ARGS((struct sccchan *scc));
static void scc_txon __ARGS((struct sccchan *scc));
static void scc_txoff __ARGS((struct sccchan *scc));
static int32 scc_aioctl __ARGS((struct iface *ifp,int cmd,int set,int32 val));
static int32 scc_sioctl __ARGS((struct iface *ifp,int cmd,int set,int32 val));
static void scc_sstart __ARGS((struct sccchan *scc));
static unsigned int scc_speed __ARGS((struct sccchan *scc,
	unsigned int clkmode,long speed));
static void scc_asytx __ARGS((struct sccchan *scc));
static void scc_asyex __ARGS((struct sccchan *scc));
static void scc_asyrx __ARGS((struct sccchan *scc));
static void scc_asysp __ARGS((struct sccchan *scc));
static void scc_sdlctx __ARGS((struct sccchan *scc));
static void scc_sdlcex __ARGS((struct sccchan *scc));
static void scc_sdlcrx __ARGS((struct sccchan *scc));
static void scc_sdlcsp __ARGS((struct sccchan *scc));
void scc_timer_tick __ARGS((void));
void dummy_int __ARGS((struct sccchan *scc));
void scc_timer_interrupt __ARGS((struct sccchan *scc));
static void scc_ticker __ARGS((int device,int ticker,struct sccboard *SccBoard));
static void sccchannelstat __ARGS((struct sccchan *scc));


/* Attach an SCC channel to the system, or initialize SCC driver.
 * operation depends on argv[2] and argv[3]:
 * when "init", the SCC driver is initialized, and global information about
 * the hardware is set up.
 * argv[0]: hardware type, must be "scc"
 * argv[1]: board name
 * argv[2]: number of SCC chips we will support
 * argv[3]: mode, must be: "init" in this case
 * argv[4]: base address of SCC chip #0 (hex)
 * argv[5]: spacing between SCC chip base addresses
 * argv[6]: offset from chip base address to channel A control register
 * argv[7]: offset from chip base address to channel B control register
 * argv[8]: offset from each channel's control register to data register
 * argv[9]: address of INTACK/Read Vector port. 0 to read from RR3A/RR2B
 * argv[10]: CPU interrupt vector number for all connected SCCs
 * argv[11]: clock frequency (PCLK/RTxC) of all SCCs in cycles per second
 *		prefix with "p" for PCLK, "r" for RTxC clock (for baudrate gen)
 * argv[12]: optional hardware type (for special features)
 * argv[13]: optional extra parameter for special hardware
 * argv[14]: optional timing channel using spare SCC
 *
 * =OR= the "easy" form of the above init is acceptable:
 * argv[0]: hardware type, must be "scc"
 * argv[1]: board name
 * argv[2]: style of scc board: drsi, baycom, opto
 * argv[3]: base address of SCC chip #0 (hex)
 * argv[4]: CPU interrupt vector number for all SCCs on the board
 * argv[5]: optional timing channel using spare SCC
 *
 * otherwise, a single channel is attached using the specified parameters:
 * argv[0]: hardware type, must be "scc"
 * argv[1]: board name
 * argv[2]: SCC channel number to attach, 0/1 for first chip A/B, 2/3 for 2nd...
 * argv[3]: mode, can be:
 *		"slip", "kiss", "ax25"
 * argv[4]: interface label, e.g., "sl0"
 * argv[5]: maximum transmission unit, bytes
 * argv[6]: interface speed, e.g, "1200". prefix with "d" when an external
 *		divider is available to generate the TX clock. When the clock
 *		source is PCLK, this can be a /32 divider between TRxC and RTxC.
 *		When the clock is at RTxC, the TX rate must be supplied at TRxC.
 *		This is needed only for AX.25 fullduplex.
 *		When this arg is given as "ext", the transmit and receive clock
 *		are external, and the BRG and DPLL are not used.
 *		On the BayCom-USCC card "ext" enables NRZ-mode for DF9IC-modem.
 * argv[7]: buffer size
 * argv[8]: callsign used on the radio channels (optional)
 * argv[9]: optional parameter(s). Currently "s" sets software DCD
 */

int
scc_attach(argc,argv)
int argc;
char *argv[];
{
	register struct iface *ifp;
	struct sccchan *scc;
	unsigned int chan,brgrate;
	int pclk = 0,hwtype = 0,hwparam = 0, ticker = -1;
	int xdev;
	struct sccboard *SccBoard;
	int device = 0;
	char *cp;
	int i_state;

	/* first see if this is an "easy-attach" line */

	if (stricmp(argv[2],"baycom") == 0) {
		if (argc > 5)				/* optional ticker channel */
			if (ticker_channel == -1) {
				if (*argv[5] == 't') {
					argv[5]++;
					ticker = atoi(argv[5]);
				}
			} else {
				tprintf("t%d ignored, ticker channel already setup on %d\n",
				  atoi(argv[5]),ticker_channel);
			}

		return scc_init(argv[1],2,(ioaddr) htol(argv[3]),
		  2,4,5,-4,0,atoi(argv[4]),4915200L,1,0x10,0,ticker);
	}

	if (stricmp(argv[2],"drsi") == 0) {
		if (argc > 5)				/* optional ticker channel */
			if (ticker_channel == -1) {
				if (*argv[5] == 't') {
					argv[5]++;
					ticker = atoi(argv[5]);
				}
			} else {
				tprintf("t%d ignored, ticker channel already setup on %d\n",
				  atoi(argv[5]),ticker_channel);
			}

		return scc_init(argv[1],1,(ioaddr) htol(argv[3]),
		  16,2,0,1,0,atoi(argv[4]),4915200L,1,0x08,0,ticker);
	}

	if (stricmp(argv[2],"opto") == 0) {
		if (argc > 5)				/* optional ticker channel */
			if (ticker_channel == -1) {
				if (*argv[5] == 't') {
					argv[5]++;
					ticker = atoi(argv[5]);
				}
			} else {
				tprintf("t%d ignored, ticker channel already setup on %d\n",
				  atoi(argv[5]),ticker_channel);
			}

		return scc_init(argv[1],2,(ioaddr) htol(argv[3]),
		  4,2,0,1,(ioaddr)(htol(argv[3]) + 0x18),atoi(argv[4]),
		  4915200L,1,0,0,ticker);
	}



	/* not an "easy-attach", is it the special "init" mode to initialize global stuff ?? */

	if (stricmp(argv[3],"init") == 0) {
		if (argc < 12)			/* need at least argv[1]..argv[11] */
			return -1;

		if (isupper(argv[11][0]))
			argv[11][0] = tolower(argv[11][0]);

		if (argv[11][0] == 'p') {		/* wants to use PCLK as clock? */
			pclk = 1;
			argv[11]++;
		} else {
			if (argv[11][0] == 'r')	/* wants to use RTxC? */
				argv[11]++;	/* that's the default */
		}
		if (argc > 12)			/* optional hardware type */
			hwtype = htoi(argv[12]);	/* it is given in hex */

		if (argc > 13)			/* optional hardware param */
			hwparam = htoi(argv[13]);	/* also in hex */

		if (argc > 14)				/* optional ticker channel */
			if (ticker_channel == -1) {
				if (*argv[14] == 't') {
					argv[14]++;
					ticker = atoi(argv[14]);
				}
			} else {
				tprintf("t%d ignored, ticker channel already setup on %d\n",
				  atoi(argv[14]),ticker_channel);
			}

		return scc_init(argv[1],atoi(argv[2]),(ioaddr) htol(argv[4]),
		  atoi(argv[5]),atoi(argv[6]),atoi(argv[7]),atoi(argv[8]),
		  (ioaddr) htol(argv[9]),atoi(argv[10]),
		  atol(argv[11]),pclk,hwtype,hwparam,ticker);
	}


	/* not "init", so it must be a valid mode to attach a channel */

        if(argc < 8)  /* need at least argv[1] .. argv[7] */
            return -1;

	/* Check for existing interface name ! - WG7J */
	if (if_lookup(argv[4]) != NULLIF) {
		tprintf(Existingiface,argv[4]);
		return -1;
	}

	if (
#ifdef AX25
	  strcmp(argv[3],"ax25")
#if defined(KISS) || defined(SLIP) || defined(NRS)
	  &&
#endif
#endif
#ifdef KISS
	  strcmp(argv[3],"kiss")
#if defined(SLIP) || defined(NRS)
	  &&
#endif
#endif
#ifdef SLIP
	  strcmp(argv[3],"slip")
#ifdef NRS
	  &&
#endif
#endif
#ifdef NRS
	  strcmp(argv[3],"nrs")
#endif
	  ) {
		tprintf("Mode %s unknown for SCC\n",argv[3]);
		return -1;
	}

#if defined(SLIP) || defined(KISS)
	if (
#ifdef SLIP
	  strcmp(argv[3],"slip") == 0
#ifdef KISS
	  ||
#endif
#endif
#ifdef KISS
	  strcmp(argv[3],"kiss") == 0
#endif
	  ) {
		for (xdev = 0;xdev < SLIP_MAX;xdev++)
			if (Slip[xdev].iface == NULLIF)
				break;
		if (xdev >= SLIP_MAX) {
			j2tputs("Too many slip devices\n");
			return -1;
		}
	}
#endif	/* SLIP || KISS */

#ifdef NRS
	if (strcmp(argv[3],"nrs") == 0) {
		for (xdev = 0;xdev < NRS_MAX;xdev++)
			if (Nrs[xdev].iface == NULLIF)
				break;

		if (xdev >= NRS_MAX) {
			j2tputs("Too many nrs devices\n");
			return -1;
		}
	}
#endif
	for (SccBoard = SccBoards; SccBoard != NULL; SccBoard = SccBoard->next) {
		if (strcmp(SccBoard->name,argv[1]) == 0)
			break;
		device += 2 * SccBoard->nchips;
	}

	/* 'device' is an index for the start of Sccchan[] and Sccpolltab[]
	 * entries for this board - adding 'chan' makes it the index
	 * for this channel - see a few lines on
	 */

	if (SccBoard == NULL) {
		tprintf("First init SCC driver for %s\n",argv[1]);
		return -1;
	}
	if ((chan = atoi(argv[2])) > SccBoard->maxchan) {
		tprintf("SCC channel %d out of range for %s [0-%u]\n",chan,SccBoard->name,SccBoard->maxchan);
		return -1;
	}

	device += chan;

	if (device == ticker_channel) {
		tprintf("SCC channel %d on %s is being used for the ticker\n",chan,SccBoard->name);
		return -1;
	}

	if (Sccchan[device] != NULLCHAN) {
		tprintf("SCC channel %d on %s is already attached\n",chan,SccBoard->name);
		return -1;
	}

	/* create interface structure and fill in details */
	ifp = (struct iface *) callocw(1,sizeof(struct iface));
	ifp->name = mallocw(strlen(argv[4]) + 1);
	strcpy(ifp->name,argv[4]);

	ifp->mtu = atoi(argv[5]);
	ifp->dev = device;
	ifp->stop = scc_stop;

	scc = (struct sccchan *) callocw(1,sizeof(struct sccchan));
	scc->ctrl = SccBoard->iobase + (chan / 2) * SccBoard->space + SccBoard->off[chan % 2];
	scc->data = scc->ctrl + SccBoard->doff;
	scc->iface = ifp;

	scc->card = SccBoard;

	/* See if this is an "attach escc" line */
	if (argv[0][0] == 'e') {
		scc->escc = 1;
	}

#ifdef SCC_SOFTWARE_DCD
	if (argc > 9)
		if (argv[9][0] == 's')
			scc->a.flags |= SCC_FLAG_SWDCD;
#endif

	if (isupper(argv[6][0]))
		argv[6][0] = tolower(argv[6][0]);

	switch (argv[6][0]) {
		case 'd':			/* fulldup divider installed? */
			if (scc->escc == 0)
				/* ESCC has no need for the fulldup mod, so
				 * only do the fulldup mod if using an SCC.
				 */
				scc->fulldup = 1;	/* set appropriate flag */
			argv[6]++;		/* skip the 'd' */
			break;

		case 'e':			/* external clocking? */
			scc->extclock = 1;	/* set the flag */
			break;
	}

	/* If the speed field (argv[6]) includes the sub-string "/n",
	 * use NRZ rather than NRZI encoding
	 */

	if (strstr(argv[6],"/n") != NULLCHAR) {
		scc->nrz = 1;
	}

	/* Allow the (hopefully technically competent!) user to specify
	 * the hexadecimal value to write into R11, by including
	 * ":<value>" in the speed field (argv[6]).
	 * e.g. "d1200:66"
	 */

	if ((cp=strchr(argv[6],':')) != NULLCHAR) {
		scc->reg_11 = htol(++cp);
	}

	scc->bufsiz = atoi(argv[7]);
	ifp->addr = Ip_addr;

	switch (argv[3][0]) {		/* mode already checked above */
#ifdef AX25
		case 'a':		/* AX.25 */
			scc_sdlc(scc);	/* init SCC in SDLC mode */

			if (!scc->extclock) {
				brgrate = scc_speed(scc,32,atol(argv[6]));/* init SCC speed */
				scc->speed = SccBoard->clk / (64L * (brgrate + 2));/* calc real speed */
			}

			setencap(ifp,"AX25");
			scc_call(ifp,argc > 8 ? argv[8] : (char *) 0);	/* set the callsign */
			ifp->ioctl = scc_sioctl;
			ifp->raw = scc_raw;

			/* default KISS Params */
			if (ticker_channel == -1) {
				/* Use 'TPS' for timing */
				scc->a.txdelay = 36*TPS/100;	/* 360 ms */
				scc->a.slottime = 16*TPS/100;	/* 160 ms */
#if TPS > 67
				scc->a.tailtime = 3*TPS/100;	/* 30 ms */
#else
				scc->a.tailtime = 2;		/* minimal reasonable value */
#endif
				scc->a.waittime = 50*TPS/100;	/* 500 ms */
			} else {
				/* Use 10ms ticker for timing */
				scc->a.txdelay = 36;		/* 360 ms */
				scc->a.slottime = 16;		/* 160 ms */
				scc->a.tailtime = 3;		/* 30 ms */
				scc->a.waittime = 50;		/* 500 ms */
			}
			scc->a.persist = 25;		/* 10% persistence */
			scc->a.fulldup = 0;		/* CSMA */
			scc->a.maxkeyup = 7;		/* 7 s */
			scc->a.mintime = 3;		/* 3 s */
			scc->a.idletime = 120;		/* 120 s */
			break;
#endif
#ifdef KISS
		case 'k':		/* kiss */
			scc_async(scc);	/* init SCC in async mode */
			brgrate = scc_speed(scc,16,atol(argv[6]));
			scc->speed = SccBoard->clk / (32L * (brgrate + 2));

			setencap(ifp,"AX25");
			scc_call(ifp,argc > 8 ? argv[8] : (char *) 0);	/* set the callsign */

			ifp->ioctl = kiss_ioctl;
			ifp->raw = kiss_raw;

			for (xdev = 0;xdev < SLIP_MAX;xdev++) {
				if (Slip[xdev].iface == NULLIF)
					break;
			}
			ifp->xdev = xdev;
			Slip[xdev].iface = ifp;
			Slip[xdev].type = CL_KISS;
			Slip[xdev].send = scc_send;
			Slip[xdev].get = get_scc;
			ifp->rxproc = newproc("ascc rx",256,asy_rx,xdev,NULL,NULL,0);
			break;
#endif
#ifdef SLIP
		case 's':		/* slip */
			scc_async(scc);	/* init SCC in async mode */
			brgrate = scc_speed(scc,16,atol(argv[6]));
			scc->speed = SccBoard->clk / (32L * (brgrate + 2));
			setencap(ifp,"SLIP");
			ifp->ioctl = scc_aioctl;
			ifp->raw = slip_raw;
			for (xdev = 0;xdev < SLIP_MAX;xdev++) {
				if (Slip[xdev].iface == NULLIF)
					break;
			}
			ifp->xdev = xdev;
			Slip[xdev].iface = ifp;
			Slip[xdev].type = CL_SERIAL_LINE;
			Slip[xdev].send = scc_send;
			Slip[xdev].get = get_scc;
			ifp->rxproc = newproc("ascc rx",256,asy_rx,xdev,NULL,NULL,0);
			break;
#endif
#ifdef NRS
		case 'n':		/* nrs */
			scc_async(scc);	/* init SCC in async mode */
			brgrate = scc_speed(scc,16,atol(argv[6]));
			scc->speed = SccBoard->clk / (32L * (brgrate + 2));
			setencap(ifp,"AX25");
			scc_call(ifp,argc > 8 ? argv[8] : (char *) 0);	/* set the callsign */
			ifp->ioctl = scc_aioctl;
			ifp->raw = nrs_raw;

			for (xdev = 0;xdev < NRS_MAX;xdev++)
				if (Nrs[xdev].iface == NULLIF)
					break;

			ifp->xdev = xdev;
			Nrs[xdev].iface = ifp;
			Nrs[xdev].send = scc_send;
			Nrs[xdev].get = get_scc;
			ifp->rxproc = newproc("nscc rx",256,nrs_recv,xdev,NULL,NULL,0);
			break;
#endif
	}
	ifp->next = Ifaces;	/* link interface in list */
	Ifaces = ifp;

	Sccchan[device] = scc;		/* put addr in table for interrupts */
	if (device > highest_device_attached)
		highest_device_attached = device;

	if (argv[3][0] == 'a') {
		if (
#ifdef SCC_SOFTWARE_DCD
		  (scc->a.flags & SCC_FLAG_SWDCD)
		  ||
#endif
		  (RDREG(scc->ctrl) & DCD)) {		/* DCD is now ON */
			/* If we're not using open squelch mode, then the receiver is only
			 * enabled when the DCD line is active. If we are using open squelch
			 * mode, then the receive is enabled all the time (except whilst
			 * transmitting, if we're not in full-duplex mode)
			 */

			i_state = dirps();	/* because of 2-step accesses */
			if (!scc->extclock)
				WRSCC(scc->ctrl,R14,SEARCH|scc->wreg[R14]);	/* DPLL: enter search mode */
			or(scc,R3,ENT_HM|RxENABLE);	/* enable the receiver, hunt mode */
			restore(i_state);
		}
	}
	return 0;
}

/* SCC driver initialisation. called on "attach scc <num> init ..." */
static int
scc_init(label,nchips,iobase,space,aoff,boff,doff,intack,ivec,clk,pclk,hwtype,hwparam,ticker)
char *label;		/* board label */
int nchips;		/* number of chips */
ioaddr iobase;		/* base of first chip */
int space,aoff,boff,doff;
ioaddr intack;		/* INTACK ioport or 0 for no INTACK */
int ivec;		/* interrupt vector number */
long clk;		/* clock frequency */
int pclk;		/* PCLK or RTxC for clock */
int hwtype;		/* selection of special hardware types */
int hwparam;		/* extra parameter for special hardware */
int ticker;		/* ticker channel */
{
	int chip,chan;
	ioaddr chipbase;
	register ioaddr ctrl;
	int i_state,d;
	int dum = 1;
	struct sccboard *SccBoard,*SccPrevBoard=NULL;
	struct sccchan *scc;
	/* unsigned int baudratedivide; */
	int device = 0;

#define z 0

	for (SccBoard = SccBoards; SccBoard != NULL;
	  SccPrevBoard=SccBoard, SccBoard = SccBoard->next) {
		device += 2 * SccBoard->nchips;
		if (strcmp(SccBoard->name,label) == 0) {
			tprintf("SCC board %s already attached\n",label);
			return 1;
		}
	}

	/* 'device' is the first free entry in the Sccpolltab[] and Sccchan[] */

	if ((device >= (2*MAXSCC)) || (device + (2 * nchips) >= (2*MAXSCC))) {
		j2tputs("Too many SCCs\n");
		return 1;
	}

	if ((interrupt_number != -1) && (interrupt_number != ivec)) {
		tprintf("Not attached - a previously attached board uses IRQ %u"
		 " - all other boards must use this IRQ\n");
		return 1;
	}

	SccBoard = (struct sccboard *)callocw(1,sizeof(struct sccboard));

	if (SccPrevBoard != NULL)
		SccPrevBoard->next = SccBoard;
	else
		SccBoards = SccBoard;

	SccBoard->name = j2strdup(label);
	SccBoard->nchips = nchips;
	SccBoard->maxchan = (2 * nchips) - 1;
	SccBoard->iobase = iobase;
	SccBoard->space = space;
	SccBoard->off[0] = aoff;
	SccBoard->off[1] = boff;
	SccBoard->doff = doff;
	SccBoard->clk = clk;
	SccBoard->pclk = pclk;
	SccBoard->hwtype = hwtype;
	SccBoard->hwparam = hwparam;

	/* reset and pre-init all chips in the system */
	for (chip = 0; chip < nchips; chip++) {
		chipbase = iobase + chip * space;
		ctrl = chipbase + SccBoard->off[0];
		i_state = dirps();	/* because of 2-step accesses */
		VOID(RDREG(ctrl));	/* make sure pointer is written */
		WRSCC(ctrl,R9,FHWRES);	/* force hardware reset */
		for (d = 0; d < 1000; d++)	/* wait a while to be sure */
			dum *= 10;
		for (chan = 0; chan < 2; chan++) {
			ctrl = chipbase + SccBoard->off[chan];

			/* initialize a single channel to no-op */
			VOID(RDREG(ctrl));	/* make sure pointer is written */
			WRSCC(ctrl,R4,z);	/* no mode selected yet */
			WRSCC(ctrl,R1,z);	/* no W/REQ operation */
			WRSCC(ctrl,R2,16 * ((device-chan)/2));	/* chip# in upper 4 bits of vector */
			WRSCC(ctrl,R3,z);	/* disable rx */
			WRSCC(ctrl,R5,z);	/* disable tx */
			WRSCC(ctrl,R9,VIS);	/* vector includes status, MIE off */
			Sccpolltab[device] = ctrl; /* store ctrl addr for polling */

			device++;
		}
		if (hwtype & HWEAGLE)			/* this is an EAGLE card */
			WRREG(chipbase + 4,0x08);	/* enable interrupt on the board */

		if (hwtype & HWPC100)			/* this is a PC100 card */
			WRREG(chipbase,hwparam);	/* set the MODEM mode (22H normally) */

		if (hwtype & HWPRIMUS)			/* this is a PRIMUS-PC */
			WRREG(chipbase + 4,hwparam);	/* set the MODEM mode (02H normally) */

		if (hwtype & HWDRSI) {			/* this is a DRSI PC*Packet card */
			ioaddr z8536 = chipbase + 7;	/* point to 8536 master ctrl reg */

			/* Initialize 8536 to perform its divide-by-32 function */
			/* This part copied from N6TTO DRSI-driver */

			/* Start by forcing chip into known state */

			VOID(RDREG(z8536));		/* make sure pointer is written */
			WRSCC(z8536,CIO_MICR,0x01);	/* force hardware reset */

			for (d = 0; d < 1000; d++)	/* wait a while to be sure */
				dum *= 10;

			WRSCC(z8536,CIO_MICR,0x00);	/* Clear reset and start */

			/* Wait for chip to come ready */

			while (RDSCC(z8536,CIO_MICR) != 0x02)
				dum *= 10;

			WRSCC(z8536,CIO_MICR,0x26);	/* NV|CT_VIS|RJA */
			WRSCC(z8536,CIO_MCCR,0xf4);	/* PBE|CT1E|CT2E|CT3E|PAE */

			WRSCC(z8536,CIO_CTMS1,0xe2);	/* Continuous, EOE, ECE, Pulse output */
			WRSCC(z8536,CIO_CTMS2,0xe2);	/* Continuous, EOE, ECE, Pulse output */

			WRSCC(z8536,CIO_CT1MSB,0x00);	/* Load time constant CTC #1 */
			WRSCC(z8536,CIO_CT1LSB,0x10);
			WRSCC(z8536,CIO_CT2MSB,0x00);	/* Load time constant CTC #2 */
			WRSCC(z8536,CIO_CT2LSB,0x10);

			WRSCC(z8536,CIO_IVR,0x06);

			/* Set port direction bits in port A and B
			 * Data is input on bits d1 and d5, output on d0 and d4.
			 * The direction is set by 1 for input and 0 for output
			 */

			WRSCC(z8536,CIO_PDCA,0x22);
			WRSCC(z8536,CIO_PDCB,0x22);

			WRSCC(z8536,CIO_CSR1,CIO_GCB|CIO_TCB);	/* Start CTC #1 running */
			WRSCC(z8536,CIO_CSR2,CIO_GCB|CIO_TCB);	/* Start CTC #2 running */
		}
		restore(i_state);
	}

	Sccpolltab[device] = 0;		/* terminate the polling table */
	Sccvecloc = intack;		/* location of INTACK/vector read */
	Sccmaxvec = 16 * (device/2);	/* upper limit on valid vector */

	if (interrupt_number == -1) {

		/* Only do this once, for the first board */

		interrupt_number = ivec;

		/* save original interrupt vector */
		Orgivec = getirq(ivec);

		if (intack) {	/* INTACK method selected? */
			/* set interrupt vector to INTACK-generating routine */
			setirq(ivec,sccvec);
		} else {
			/* set interrupt vector to polling routine */
			setirq(ivec,sccnovec);
		}
		/* enable the interrupt */
		maskon(ivec);
	}

	if (ticker != -1) {
		scc_ticker(device,ticker,SccBoard);
	}

	return 0;
}

#if defined(SLIP) || defined(KISS) || defined(NRS)

/* initialize an SCC channel in asynchronous mode */
static int
scc_async(scc)
register struct sccchan *scc;
{
	int i_state;
	register struct fifo *fp = &(scc->fifo);

	if ((fp->buf = malloc(scc->bufsiz)) == NULLCHAR) {
		tprintf("scc%d: No space for rx buffer\n",scc->iface->dev);
		return -1;
	}
	fp->bufsize = scc->bufsiz;
	fp->wp = fp->rp = fp->buf;
	fp->cnt = 0;

	scc->int_transmit = scc_asytx;	/* set interrupt handlers */
	scc->int_extstat = scc_asyex;
	scc->int_receive = scc_asyrx;
	scc->int_special = scc_asysp;

	i_state = dirps();

	wr(scc,R4,X16CLK|SB1);		/* *16 clock, 1 stopbit, no parity */
	wr(scc,R1,z);			/* no W/REQ operation */
	wr(scc,R3,Rx8);			/* RX 8 bits/char, disabled */
	wr(scc,R5,Tx8|DTR|RTS);		/* TX 8 bits/char, disabled, DTR RTS */
	wr(scc,R9,VIS);			/* vector includes status */
	wr(scc,R10,NRZ|z);		/* select NRZ */
	if (scc->reg_11) {
		wr(scc,R11,scc->reg_11);
	} else {
		wr(scc,R11,RCBR|TCBR);		/* clocks are BR generator */
	}
	wr(scc,R14,scc->card->pclk? BRSRC:z);	/* brg source = PCLK/RTxC */
	wr(scc,R15,BRKIE);		/* enable BREAK ext/status int */

	or(scc,R3,RxENABLE);		/* enable receiver */
	or(scc,R5,TxENAB);		/* enable transmitter */

	WRREG(scc->ctrl,RES_EXT_INT);	/* reset ext/status interrupts */
	WRREG(scc->ctrl,RES_EXT_INT);	/* must be done twice */
	scc->status = (RDREG(scc->ctrl) & ~ZCOUNT);	/* read initial status */

	or(scc,R1,INT_ALL_Rx|TxINT_ENAB|EXT_INT_ENAB);	/* enable interrupts */
	or(scc,R9,MIE);					/* master interrupt enable */

	restore(i_state);
	return 0;
}

#endif

#ifdef AX25

/* initialize an SCC channel in SDLC mode ; new BayCom version G8FSL */
static void
scc_sdlc(scc)
register struct sccchan *scc;
{
	int i_state;

	scc->int_transmit = scc_sdlctx;	/* set interrupt handlers */
	scc->int_extstat = scc_sdlcex;
	scc->int_receive = scc_sdlcrx;
	scc->int_special = scc_sdlcsp;

	i_state = dirps();

	if (scc->escc) {
		/* 85230 code. Needs to be done before writing to R7 or R15 */

		wr(scc,R15,0x01);	/* enable the ESCC extended register R7'*/

		/* Set up Tx FIFO to interrupt when there's any space in the fifo
		 * If this is an 8530, then R7 gets corrupted here but is overwritten
		 * a few lines further on.
		 * If this is an 85230, then the write is to R7' for fifo control
		 */
		wr(scc,R7,0x00);

		/* Restore bit 0 of R15	in case this is a 8530 */
		wr(scc,R15,0x00);

		/* End of 85230 code */
	}

	wr(scc,R4,X1CLK|SDLC);		/* *1 clock, SDLC mode */
	wr(scc,R1,z);			/* no W/REQ operation */
	wr(scc,R3,Rx8|RxCRC_ENAB);	/* RX 8 bits/char, CRC, disabled */
	wr(scc,R5,Tx8|DTR|TxCRC_ENAB);	/* TX 8 bits/char, disabled, DTR */
	wr(scc,R6,z);			/* SDLC address zero (not used) */
	wr(scc,R7,FLAG);		/* SDLC flag value */
	wr(scc,R9,VIS);			/* vector includes status */

	if ((scc->extclock) && (scc->card->hwtype & HWBAYCOM)) {
		/* If this is a Baycom card, and asked for external
		 * clocks, then it is to drive the DF9IC-modem 9k6
		 * modem. This needs NRZ data (not NRZI).
		 */
		wr(scc,R10,CRCPS|NRZ|ABUNDER);	/* CRC preset 1, select NRZ, ABORT on underrun */
	} else {
		/* Use NRZ if scc->nrz is 1, or NRZI if it
		 * is 0.
		 */
		if (scc->nrz) {
			wr(scc,R10,CRCPS|NRZ|ABUNDER);	/* CRC preset 1, select NRZ, ABORT on underrun */
		} else {
			wr(scc,R10,CRCPS|NRZI|ABUNDER);	/* CRC preset 1, select NRZI, ABORT on underrun */
		}
	}

	/* Now for an amazingly complex piece of code to write to R11.
	 * The trouble is that there are so many options.
	 * It's easy if the user has specified the value to write to R11!
	 */

	if (scc->reg_11) {
		wr(scc,R11,scc->reg_11);
	} else {
		if (scc->extclock) {		/* when using external clocks */
			if (scc->card->hwtype & HWBAYCOM) {
				/* If this is a Baycom card, and asked for external
				 * clocks, then the external clocks are:
				 * Rx clk on TRxC pin, and Tx clk on RTxC.
				 */
				wr(scc,R11,RCTRxCP|TCRTxCP);	/* RXclk TRxC, TXclk RTxC. */
			} else {
				/* If it's not a Baycom card, but want external
				 * clocks, then the external clocks are: Rx clk
				 * on RTxC pin, and Tx clock on TRxC pin.
				 */
				wr(scc,R11,RCRTxCP|TCTRxCP);	/* RXclk RTxC, TXclk TRxC. */
			}
		} else {
			/* Not using external clocks */
			if (scc->fulldup) {		/* when external clock divider */
				if (scc->card->pclk) {	/* when using PCLK as clock source */
					if (scc->card->hwtype & HWBAYCOM) {
						/* RXclk DPLL, TXclk RTxC, out=DPLL (Echo duplex) */
						wr(scc,R11,RCDPLL|TCRTxCP|TRxCOI|TRxCDP);
					} else {
						/* RXclk DPLL, TXclk RTxC, out=BRG. external /32 TRxC->RTxC */
						wr(scc,R11,RCDPLL|TCRTxCP|TRxCOI|TRxCBR);
					}
				} else {
					/* RXclk DPLL, TXclk TRxC, external TX clock to TRxC */
					wr(scc,R11,RCDPLL|TCTRxCP);
				}
			} else {
				/* Either half-duplex operation with an SCC
				 * OR this is an ESCC
				 */
#if 0	/* KA9Q - for PSK modem */
				/* RXclk DPLL, TXclk BRG. BRG reprogrammed at every TX/RX switch */
				wr(scc,R11,RCDPLL|TCBR);
#else
				if (scc->escc) {
					/* If this is an ESCC, then there's an internal /32
					 * counter between the DPLL CLK input and the DPLL
					 * output to the transmitter
					 */
					wr(scc,R11,RCDPLL|TCDPLL);
				} else {
					/* If this is an SCC, then the BRG must be reprogrammed
					 * when switching between Tx and Rx
					 */
					/* DPLL -> Rx clk, (DPLL -> Tx CLK), DPLL -> TRxC pin */
					wr(scc,R11,RCDPLL|TCDPLL|TRxCOI|TRxCDP);
				}
#endif
			}
		}
	}

	if (scc->extclock) {		/* when using external clocks */
		wr(scc,R14,z);					/* No BRG options */
		WRSCC(scc->ctrl,R14,DISDPLL|scc->wreg[R14]);	/* No DPLL operation */
	} else {
		wr(scc,R14,scc->card->pclk? BRSRC:z);		/* BRG source = PCLK/RTxC */
		WRSCC(scc->ctrl,R14,SSBR|scc->wreg[R14]);	/* DPLL source = BRG */
		WRSCC(scc->ctrl,R14,SNRZI|scc->wreg[R14]);	/* DPLL NRZI mode */
	}

	if (scc->card->hwtype & HWBAYCOM) {	/* If it's a BayCom-USCC card then we
						 * mustn't enable CTS interrupts (well,
						 * not on ports 1 and 2).
						 */
#ifdef SCC_SOFTWARE_DCD
		if (scc->a.flags & SCC_FLAG_SWDCD) {
			wr(scc,R15,BRKIE|SYNCIE);	/* enable ABORT & SYNC/HUNT interrupts */
		} else
#endif
			wr(scc,R15,BRKIE|DCDIE);	/* enable ABORT	& DCD interrupts */
	} else {
#ifdef SCC_SOFTWARE_DCD
		if (scc->a.flags & SCC_FLAG_SWDCD) {
			wr(scc,R15,BRKIE|CTSIE|SYNCIE);	/* enable ABORT, CTS & SYNC/HUNT interrupts */
		} else
#endif
			wr(scc,R15,BRKIE|CTSIE|DCDIE);	/* enable ABORT, CTS & DCD interrupts */
	}

	WRREG(scc->ctrl,RES_EXT_INT);	/* reset ext/status interrupts */
	WRREG(scc->ctrl,RES_EXT_INT);	/* must be done twice */
	scc->status = (RDREG(scc->ctrl) & ~ZCOUNT);	/* read initial status */

	or(scc,R1,INT_ALL_Rx|TxINT_ENAB|EXT_INT_ENAB);	/* enable interrupts */
	or(scc,R9,MIE);			/* master interrupt enable */

	/* Enabling the receiver here seems to cause the SCC driver problems,
	 * so I've moved the code to the end of scc_attach()	G8FSL
	 */

	restore(i_state);
}

/* initialize an SCC channel in ticker mode */
static void
scc_ticker(device,ticker,SccBoard)
int device;
int ticker;
struct sccboard *SccBoard;
{
	struct sccchan *scc;
	int i_state;

	/* 'device' as passed here is set to one higher than the highest
	 * channel on the card (because it's called after all the channels
	 * on the card have been initialised). Fix 'device' up first.
	 */

	device = device - (2 * SccBoard->nchips) + ticker;

	ticker_channel = device;

	/* configure channel for ticker */
	scc = (struct sccchan *) callocw(1,sizeof(struct sccchan));
	scc->card = SccBoard;

	scc->ctrl = SccBoard->iobase + ((ticker / 2) * SccBoard->space) + SccBoard->off[ticker % 2];
	scc->data = scc->ctrl + SccBoard->doff;

	/* set interrupt handlers */
	scc->int_transmit = dummy_int;
	scc->int_receive = dummy_int;
	scc->int_special = dummy_int;
	scc->int_extstat = scc_timer_interrupt;

	i_state = dirps();

	wr(scc,R4,X1CLK|SB1);		/* *1 clock, 1 stopbit, no parity */
	wr(scc,R1,z);			/* no W/REQ operation */
	wr(scc,R3,Rx8);			/* RX 8 bits/char, disabled */
	wr(scc,R5,Tx8|DTR|RTS);		/* TX 8 bits/char, disabled, DTR RTS */
	wr(scc,R9,VIS);			/* vector includes status */
	wr(scc,R10,NRZ|z);		/* select NRZ */
	wr(scc,R11,RCBR|TCBR);		/* clocks are BR generator */
	wr(scc,R14,scc->card->pclk? BRSRC:z);	/* brg source = PCLK/RTxC */
	wr(scc,R15,ZCIE);		/* enable ZERO COUNT ext/status int */

	WRREG(scc->ctrl,RES_EXT_INT);	/* reset ext/status interrupts */
	WRREG(scc->ctrl,RES_EXT_INT);	/* must be done twice */
	scc->status = (RDREG(scc->ctrl) & ~ZCOUNT);	/* read initial status */

	or(scc,R1,EXT_INT_ENAB);	/* enable external interrupts */
	or(scc,R9,MIE);			/* master interrupt enable */

	restore(i_state);

	scc_speed(scc,1,50);	/* want ZC interrupt every 10ms, so ask for
				 * 50 baud bit rate generator - before the
				 * clock_mode division (if clock_mode <> 1)
				 */

	Sccchan[device] = scc;			/* put addr in table for interrupts */
	if (device > highest_device_attached)
		highest_device_attached = device;
}

#endif	/* AX25 */

/* set SCC channel speed
 * clkmode specifies the division rate (1,16,32) inside the SCC
 * returns the selected brgrate for "real speed" calculation
 */
static unsigned int
scc_speed(scc,clkmode,speed)
register struct sccchan *scc;
unsigned int clkmode;
long speed;		/* the desired baudrate */
{
	unsigned int brgrate;
	long spdclkm;
	int i_state;

	/* calculate baudrate generator value */

	if ((spdclkm = speed * clkmode) == 0)
		return 65000U;		/* avoid divide-by-zero */

	/* The formula for the division ratio is:
	 *
	 * ( clock / (2 * clock_mode * baud_rate) ) - 2
	 *
	 * By adding (clock_mode * baud_rate) to clock, we are adding
	 * one-half to the division ratio - to round the ratio to the
	 * nearest integer
	 */
	brgrate = (unsigned) ((scc->card->clk + spdclkm) / (spdclkm * 2)) - 2;

	i_state = dirps();		/* 2-step register accesses... */

	cl(scc,R14,BRENABL);		/* disable baudrate generator */
	wr(scc,R12,brgrate);		/* brg rate LOW */
	wr(scc,R13,brgrate >> 8);	/* brg rate HIGH */
	or(scc,R14,BRENABL);		/* enable baudrate generator */

	restore(i_state);
	return brgrate;
}

/* de-activate SCC channel */
static int
scc_stop(ifp)
struct iface *ifp;
{
	struct sccchan *scc = Sccchan[ifp->dev];
	int i_state,d,dum=1;

	i_state = dirps();

	VOID(RDREG(scc->ctrl));		/* make sure pointer is written */
	or(scc,R9,(ifp->dev % 2)? CHRB : CHRA); /* reset the channel */

	for (d = 0; d < 1000; d++)	/* wait - needs 5 PCLK cycles before next SCC access */
		dum *= 10;

	switch (ifp->type) {
		case CL_SERIAL_LINE:
		case CL_KISS:
			free(scc->fifo.buf);
		default:
			break;
	}
	free(scc);
	Sccchan[ifp->dev] = NULLCHAN;
	restore(i_state);
	return 0;
}

/* de-activate SCC driver on program exit */
void
sccstop()
{
	struct sccboard *SccBoard,*sccnext;
	int i,device = 0;

	for (SccBoard = SccBoards; SccBoard != NULL; SccBoard = sccnext) {
		sccnext = SccBoard->next;
		if (interrupt_number != -1) {
			maskoff(interrupt_number);		/* disable the interrupt */
			setirq(interrupt_number,Orgivec);	/* restore original interrupt vector */
			interrupt_number = -1;
		}
		for (i=0 ; i <= SccBoard->maxchan; i++) {
			free(Sccchan[device]);
			device++;
		}
		free(SccBoard->name);
		free(SccBoard);
	}
}

#if defined(SLIP) || defined(NRS)

/* perform ioctl on SCC (async) channel
 * this will only read/set the line speed
 */
static int32
scc_aioctl(ifp,cmd,set,val)
struct iface *ifp;
int cmd;
int set;
int32 val;
{
	struct sccchan *scc;
	unsigned int brgrate;

	scc = Sccchan[ifp->dev];

	switch (cmd) {
		case PARAM_SPEED:
			if (set) {
				brgrate = scc_speed(scc,16,val);
				scc->speed = scc->card->clk / (32L * (brgrate + 2));
			}
			return scc->speed;
	}
	return 0;
}

#endif

#ifdef AX25

/* perform ioctl on SCC (sdlc) channel
 * this is used for AX.25 mode only, and will set the "kiss" parameters
 */
static int32
scc_sioctl(ifp,cmd,set,val)
struct iface *ifp;
int cmd;
int set;
int32 val;
{
	struct sccchan *scc;
	int i_state;
	unsigned int brgrate;

	scc = Sccchan[ifp->dev];

	switch (cmd) {
		case PARAM_SPEED:
			if (set) {
				if (val == 0)
					scc->extclock = 1;
				else {
					brgrate = scc_speed(scc,32,val);/* init SCC speed */
					scc->speed = scc->card->clk / (64L * (brgrate + 2));/* calc real speed */
				}
			}
			return scc->speed;
		case PARAM_TXDELAY:
			if (set)
				scc->a.txdelay = val;
			return scc->a.txdelay;
		case PARAM_PERSIST:
			if (set)
				scc->a.persist = val;
			return scc->a.persist;
		case PARAM_SLOTTIME:
			if (set)
				scc->a.slottime = val;
			return scc->a.slottime;
		case PARAM_TXTAIL:
			if (set)
				scc->a.tailtime = val;
			return scc->a.tailtime;
		case PARAM_FULLDUP:
			if (set)
				scc->a.fulldup = val;
			return scc->a.fulldup;
		case PARAM_WAIT:
			if (set)
				scc->a.waittime = val;
			return scc->a.waittime;
		case PARAM_MAXKEY:
			if (set)
				scc->a.maxkeyup = val;
			return scc->a.maxkeyup;
		case PARAM_MIN:
			if (set)
				scc->a.mintime = val;
			return scc->a.mintime;
		case PARAM_IDLE:
			if (set)
				scc->a.idletime = val;
			return scc->a.idletime;
		case PARAM_DTR:
			if (set) {
				if (val)
					scc->wreg[R5] |= DTR;
				else
					scc->wreg[R5] &= ~DTR;

				i_state = dirps();
				if (scc->a.tstate == IDLE && scc->timercount == 0)
					scc->timercount = 1;	/* force an update */
				restore(i_state);
			}
			return (scc->wreg[R5] & DTR) ? 1 : 0;
		case PARAM_GROUP:
			if (set)
				scc->group = (int) val;
			return scc->group;
	}
	return -1;
}

#endif

#if defined(SLIP) || defined(KISS) || defined(NRS)

/* start SCC transmitter when it is idle (SLIP/KISS mode only) */
static void
scc_sstart(scc)
register struct sccchan *scc;
{
	if (scc->tbp != NULLBUF		/* busy */
	  || scc->sndq == NULLBUF)	/* no work */
		return;

	scc->tbp = dequeue(&scc->sndq);
	WRREG(scc->data,FR_END);
}

#endif

/* show SCC status */
int
dosccstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct sccchan *scc;
	int i,j;
	struct sccboard *SccBoard;
	int device = 0;

	if (argc > 1) {
		for (i=0 ; i <= highest_device_attached ; i++) {
			scc = Sccchan[i];
			if (scc == NULLCHAN)
				continue;
			if (stricmp(argv[1],scc->iface->name) == 0) {
				j2tputs("Iface    Sent   Rcvd   Error Space Overr   Rxints   Txints   Exints   Spints\n");
				sccchannelstat(scc);
				tputc('\n');
				if (scc->escc)
					j2tputs("ESCC  ");
				tprintf("Ctrl=%x Data=%x  LastStatus=%x\n",
				  scc->ctrl,scc->data,scc->status);
				for (j=0 ; j < 16; j++)
					tprintf("WR%d=%2.2x  ",j,scc->wreg[j]);
				tprintf("\nTxState=%u TxTimer=%u RxOvr=%u TxUndr=%u\n",
				  scc->a.tstate,scc->timercount,scc->rovers,scc->tunders);
				tprintf("RR3 = %2.2x\n",rd(scc,R3));
				if (scc->int_receive == scc_sdlcrx) {
					j2tputs("AX25 options:");
					if (!scc->a.flags)
						j2tputs(" (none)");
					if (scc->a.flags & SCC_FLAG_SWDCD)
						j2tputs("  Software DCD");
					tputc('\n');
				}
				return 0;
			}
		}
		tprintf("%s is not an SCC interface\n",argv[1]);
		return 1;
	}

	if (SccBoards == NULL) {
		j2tputs("No SCC board attached\n");
		return 1;
	}

	for (SccBoard = SccBoards; SccBoard != NULL; SccBoard = SccBoard->next) {
		tprintf("Board: %s\n",SccBoard->name);
		j2tputs("Ch Iface    Sent   Rcvd   Error Space Overr   Rxints   Txints   Exints   Spints\n");

		for (i = 0; i <= SccBoard->maxchan; i++) {
			if ((scc = Sccchan[device+i]) != NULLCHAN) {
				tprintf("%2d ",i);
				sccchannelstat(scc);
			}
		}
		device += 2 * SccBoard->nchips;
	}
	return 0;
}

static void
sccchannelstat(scc)
struct sccchan *scc;
{
#if defined(SLIP) || defined(KISS) || defined(NRS)
	if (scc->int_receive == scc_asyrx)
		tprintf("%-6s  ** asynch ** %7lu %5u %5u %8lu %8lu %8lu %8lu\n",scc->iface->name,
		  scc->rxerrs,scc->nospace,scc->rovers + scc->tunders,
		  scc->rxints,scc->txints,scc->exints,scc->spints);
#ifdef AX25
	else
#endif
#endif

#ifdef AX25
		tprintf("%-6s %6lu %6lu %7lu %5u %5u %8lu %8lu %8lu %8lu\n",
		  (scc->iface) ? scc->iface->name : "ticker",
		  scc->enqueued,scc->rxframes,scc->rxerrs,scc->nospace,
		  scc->rovers + scc->tunders,scc->rxints,scc->txints,scc->exints,
		  scc->spints);
#endif
}

#ifdef AX25

/* send raw frame to SCC. used for AX.25 */
static int
scc_raw(ifp,bp)
struct iface *ifp;
struct mbuf *bp;
{
	struct sccchan *scc;
	int i_state;

	dump(ifp,IF_TRACE_OUT,CL_AX25,bp);
	ifp->rawsndcnt++;
	ifp->lastsent = secclock();

	scc = Sccchan[ifp->dev];

	if (scc->tx_inhibit) {	/* transmitter inhibit */
		free_p(bp);
		return -1;
	}

	enqueue(&scc->sndq,bp);	/* enqueue packet */
	scc->enqueued++;

	i_state = dirps();

	if (scc->a.tstate == IDLE) {	/* when transmitter is idle */
		scc->a.tstate = DEFER;	/* start the key-up sequence */

		/* maxdefer is the number of ticks for which to defer */
		if (ticker_channel == -1)
			scc->a.maxdefer = TPS * scc->a.idletime / scc->a.slottime;
		else
			scc->a.maxdefer = 100 * scc->a.idletime / scc->a.slottime;

		scc->timercount = scc->a.waittime;
	}
	restore(i_state);
	return 0;
}

#endif

#if defined(SLIP) || defined(KISS) || defined(NRS)

static int
scc_send(dev,bp)
int dev;
struct mbuf *bp;
{
	struct sccchan *scc;

	scc = Sccchan[dev];
	enqueue(&scc->sndq,bp);

	if (scc->tbp == NULLBUF)
		scc_sstart(scc);
	return(0);
}

#endif

#ifdef AX25

/* initialize interface for AX.25 use */
static int
scc_call(ifp,call)
register struct iface *ifp;
char *call;
{
	char out[AXALEN];

	ifp->hwaddr = mallocw(AXALEN);
	if (setcall(out,call) == 0)
		memcpy(ifp->hwaddr,out,AXALEN);
	else
		memcpy(ifp->hwaddr,Mycall,AXALEN);
	return 0;
}

#endif

#if defined(SLIP) || defined(KISS) || defined(NRS)

/* Interrupt handlers for asynchronous modes (kiss, slip) */

/* Transmitter interrupt handler */
/* This routine sends data from mbufs in SLIP format */
static void
scc_asytx(scc)
register struct sccchan *scc;
{
	register struct mbuf *bp;

	scc->txints++;

	if (scc->txchar != 0) {	/* a character pending for transmit? */
		WRREG(scc->data,scc->txchar);	/* send it now */
		scc->txchar = 0;	/* next time, ignore it */
		return;
	}

	if (scc->tbp == NULLBUF) {	/* nothing to send? */
		if ((scc->tbp = scc->sndq) != NULLBUF) { /* dequeue next frame */
			scc->sndq = scc->sndq->anext;
			WRREG(scc->data,FR_END);	/* send FR_END to flush line garbage */
		} else {
			WRREG(scc->ctrl,RES_Tx_P);	/* else only reset pending int */
		}
		return;
	}
	while ((bp = scc->tbp)->cnt == 0) {	/* nothing left in this mbuf? */
		bp = bp->next;			/* save link to next */

		free_mbuf(scc->tbp);

		if ((scc->tbp = bp) == NULLBUF) {		/* see if more mbufs follow */
			WRREG(scc->data,FR_END);	/* frame complete, send FR_END */
			return;
		}
	}
	/* now bp = scc->tbp (either from while or from if stmt above) */

	WRREG(scc->data,*(bp->data));	/* just send the character */
	bp->cnt--;			/* decrease mbuf byte count */
	bp->data++;			/* and increment the data pointer */
}

/* External/Status interrupt handler */
static void
scc_asyex(scc)
register struct sccchan *scc;
{
	register unsigned char status,changes;

	scc->exints++;
	status = RDREG(scc->ctrl);
	changes = status ^ scc->status;

	if (changes & BRK_ABRT) {		/* BREAK? */
		if ((status & BRK_ABRT) == 0)	/* BREAK now over? */
			VOID(RDREG(scc->data)); /* read the NUL character */
	}
	scc->status = status & ~ZCOUNT;
	WRREG(scc->ctrl,RES_EXT_INT);
}

/* Receiver interrupt handler under NOS.
 * Since the higher serial protocol routines are all written to work
 * well with the routines in 8250.c, it makes sense to handle
 * asynch i/o with the 8530 in a similar manner. Therefore, these
 * routines are as close to their counterparts in 8250.c as possible.
 */

static void
scc_asyrx(scc)
register struct sccchan *scc;
{
	register struct fifo *fp;
	char c;

	scc->rxints++;

	fp = &(scc->fifo);
	do {
		c = RDREG(scc->data);
		if (fp->cnt != fp->bufsize) {
			*fp->wp++ = c;
			if (fp->wp >= &fp->buf[fp->bufsize])
				fp->wp = fp->buf;
			fp->cnt++;
		} else
			scc->nospace++;
	} while (RDREG(scc->ctrl) & Rx_CH_AV);
	j2psignal(fp,1);	/* eventually move this to timer routine */
}

/* Blocking read from asynch input.
 * Essentially the same as get_asy() in 8250.c
 * See comments in asy_rxint().
 */
static int
get_scc(dev)
int dev;
{
	int i_state;
	register struct fifo *fp;
	char c;

	fp = &(Sccchan[dev]->fifo);

	i_state = dirps();
	while (fp->cnt == 0)
		pwait(fp);
	fp->cnt--;
	restore(i_state);

	c = *fp->rp++;
	if (fp->rp >= &fp->buf[fp->bufsize])
		fp->rp = fp->buf;

	return uchar(c);
}

int
scc_frameup(dev)
int dev;
{
	Sccchan[dev]->rxframes++;
	return 0;
}

/* Receive Special Condition interrupt handler */
static void
scc_asysp(scc)
register struct sccchan *scc;
{
	register unsigned char status;

	scc->spints++;

	status = rd(scc,R1);		/* read receiver status */
	VOID(RDREG(scc->data));		/* flush offending character */

	if (status & (CRC_ERR | Rx_OVR))	/* did a framing error or overrun occur ? */
		scc->rovers++;		/* report as overrun */

	WRREG(scc->ctrl,ERR_RES);
}

#endif

#ifdef AX25

/* Interrupt handlers for sdlc mode (AX.25) */

/* Transmitter interrupt handler */
static void
scc_sdlctx(scc)
register struct sccchan *scc;
{
	register struct mbuf *bp;

	scc->txints++;

	switch (scc->a.tstate) {		/* look at transmitter state */
		case ACTIVE:		/* busy sending data bytes */
			while ((bp = scc->tbp)->cnt == 0) {	/* nothing left in this mbuf? */
				bp = bp->next;			/* save link to next */
				free_mbuf(scc->tbp);		/*KM*/
				if ((scc->tbp = bp) == NULLBUF) {		/* see if more mbufs follow */
					if (RDREG(scc->ctrl) & TxEOM) {	/* check tx underrun status */
						scc->tunders++;		/* oops, an underrun! count them */
						WRREG(scc->ctrl,SEND_ABORT);	/* send an abort to be sure */
						scc->a.tstate = TAIL;	/* key down tx after TAILTIME */
						scc->timercount = scc->a.tailtime;
						return;
					}
					cl(scc,R10,ABUNDER);		/* frame complete, allow CRC transmit */
					scc->a.tstate = FLUSH;
					WRREG(scc->ctrl,RES_Tx_P);	/* reset pending int */
					return;
				}
			}
			/* now bp = scc->tbp (either from while or from if stmt above) */
			WRREG(scc->data,*(bp->data++));	/* send the character */
			bp->cnt--;			/* decrease mbuf byte count */
			return;
		case FLUSH:			/* CRC just went out, more to send? */
			or(scc,R10,ABUNDER);	/* re-install underrun protection */
			/* verify that we are not exeeding max tx time (if defined) */
			if ((scc->timercount != 0 || scc->a.maxkeyup == 0)
			  && (scc->tbp = scc->sndq) != NULLBUF) {	/* dequeue a frame */
				scc->sndq = scc->sndq->anext;
				WRREG(scc->ctrl,RES_Tx_CRC);	/* reset the TX CRC generator */
				scc->a.tstate = ACTIVE;
				scc_sdlctx(scc);		/* write 1st byte */
				WRREG(scc->ctrl,RES_EOM_L);	/* reset the EOM latch */
				return;
			}
			scc->a.tstate = TAIL;			/* no more, key down tx after TAILTIME */
			scc->timercount = scc->a.tailtime;
			WRREG(scc->ctrl,RES_Tx_P);
			return;
		default:		/* another state */
			WRREG(scc->ctrl,RES_Tx_P);		/* then don't send anything */
			return;
	}
}

/* External/Status interrupt handler */
static void
scc_sdlcex(scc)
register struct sccchan *scc;
{
	register unsigned char status,changes;
	int i,limit;

	if (scc->escc)
		limit = 8;
	else
		limit = 3;

	scc->exints++;
	status = RDREG(scc->ctrl);
	changes = status ^ scc->status;

	/* Note that we ignore SYNC/HUNT changes, although changes in this
	 * signal cause external interrupts - only so that the status is
	 * kept up to date
	 */

	if (changes & BRK_ABRT) {		/* Received an ABORT */
		if (status & BRK_ABRT) {	/* is this the beginning? */
			if (scc->rbp != NULLBUF) {/* did we receive something? */
				/* check if a significant amount of data came in */
				/* this is because the drop of DCD tends to generate an ABORT */
				if (scc->rbp->next != NULLBUF || scc->rbp->cnt > sizeof(struct phdr))
					scc->rxerrs++;		/* then count it as an error */
				scc_tossb(scc);			/* throw away buffer */
			}
			/* flush the FIFO (3 chars for SCC, 8 chars for ESCC */
			for (i=0; i<limit ; i++)
				VOID(RDREG(scc->data));
		}
	}

	if (changes & CTS) {		/* CTS input changed state */
		if (!(scc->card->hwtype & HWBAYCOM)) {	/* If it's a BayCom-USCC card then we
							 * should ignore CTS changes
							 */
			if (status & CTS) {	/* CTS is now ON */
				if (scc->a.tstate == KEYWT
				  && scc->a.txdelay == 0)	/* zero TXDELAY = wait for CTS */
					scc->timercount = 1;	/* it will start within 10 ms */
			}
		}
	}

	if (
#ifdef SCC_SOFTWARE_DCD
	  !(scc->a.flags & SCC_FLAG_SWDCD)
	  &&
#endif
	  (changes & DCD)) {		/* DCD input changed state */
		if (status & DCD) {	/* DCD is now ON */
			if (!scc->extclock)
				WRSCC(scc->ctrl,R14,SEARCH|scc->wreg[R14]);	/* DPLL: enter search mode */
			or(scc,R3,ENT_HM|RxENABLE);	/* enable the receiver, hunt mode */
		} else {		/* DCD is now OFF */
			cl(scc,R3,ENT_HM|RxENABLE);	/* disable the receiver */
			/* flush the FIFO (3 chars for SCC, 8 chars for ESCC */
			for (i=0; i<limit ; i++)
				VOID(RDREG(scc->data));
			if (scc->rbp != NULLBUF) {/* did we receive something? */
				/* check if a significant amount of data came in */
				/* this is because some characters precede the drop of DCD */
				if (scc->rbp->next != NULLBUF || scc->rbp->cnt > sizeof(struct phdr))
					scc->rxerrs++;	/* then count it as an error */
				scc_tossb(scc);		/* throw away buffer */
			}
		}
	}

	scc->status = status & ~ZCOUNT;	/* Save without Zero Count bit's value */
	WRREG(scc->ctrl,RES_EXT_INT);
}

/* Receiver interrupt handler */
static void
scc_sdlcrx(scc)
register struct sccchan *scc;
{
	register struct mbuf *bp;

	scc->rxints++;

	if ((bp = scc->rbp1) == NULLBUF) {	/* no buffer available now */
		if (scc->rbp == NULLBUF) {
			if ((bp = alloc_mbuf(scc->bufsiz+sizeof(struct phdr))) != NULLBUF) {
				scc->rbp = scc->rbp1 = bp;
				bp->cnt = sizeof(struct phdr);	/* get past the header */
			}
		} else if ((bp = alloc_mbuf(scc->bufsiz)) != NULLBUF) {
			scc->rbp1 = bp;
			for (bp = scc->rbp; bp->next != NULLBUF; bp = bp->next);
			bp->next = scc->rbp1;
			bp = scc->rbp1;
		}
		if (bp == NULLBUF) {
			VOID(RDREG(scc->data));	/* so we have to discard the char */
			or(scc,R3,ENT_HM);	/* enter hunt mode for next flag */
			scc_tossb(scc);		/* put buffers back on pool */
			scc->nospace++;		/* count these events */
			return;
		}
	}

	/* now, we have a buffer (at bp). read character and store it */
	bp->data[bp->cnt++] = RDREG(scc->data);

	if (bp->cnt == bp->size)			/* buffer full? */
		scc->rbp1 = NULLBUF;		/* acquire a new one next time */
}

/* Receive Special Condition interrupt handler */
static void
scc_sdlcsp(scc)
register struct sccchan *scc;
{
	register unsigned char status;
	register struct mbuf *bp;
	struct phdr phdr;

	scc->spints++;

	status = rd(scc,R1);		/* read receiver status */
	VOID(RDREG(scc->data));		/* flush offending character */

	if (status & Rx_OVR) {		/* receiver overrun */
		scc->rovers++;		/* count them */
		or(scc,R3,ENT_HM);	/* enter hunt mode for next flag */
		scc_tossb(scc);		/* rewind the buffer and toss */
	}
	if (status & END_FR		/* end of frame */
	  && scc->rbp != NULLBUF) {		/* at least received something */
		if ((status & CRC_ERR) == 0	/* no CRC error is indicated */
		  && (status & 0xe) == RES8 	/* 8 bits in last byte */
		  && scc->rbp->cnt > sizeof(phdr)) {

			/* we seem to have a good frame. but the last byte received */
			/* from rx interrupt is in fact a CRC byte, so discard it */
			if (scc->rbp1 != NULLBUF) {
				scc->rbp1->cnt--;	/* current mbuf was not full */
			} else {
				for (bp = scc->rbp; bp->next != NULLBUF; bp = bp->next)
					;
					/* find last mbuf */

				bp->cnt--;		/* last byte is first CRC byte */
			}

			phdr.iface = scc->iface;
			phdr.type = CL_AX25;
			memcpy(&scc->rbp->data[0],(char *)&phdr,sizeof(phdr));
			enqueue(&Hopper,scc->rbp);

			scc->rbp = scc->rbp1 = NULLBUF;
			scc->rxframes++;
		} else {			/* a bad frame */
			scc_tossb(scc);		/* throw away frame */
			scc->rxerrs++;
		}
	}
	WRREG(scc->ctrl,ERR_RES);
}

/* Throw away receive mbuf(s) when an error occurred */
static void
scc_tossb (scc)
register struct sccchan *scc;
{
	register struct mbuf *bp;

	if ((bp = scc->rbp) != NULLBUF) {
		free_p(bp->next);
		free_p(bp->dup);	/* Should be NULLBUF */
		bp->next = NULLBUF;
		scc->rbp1 = bp;			/* Don't throw this one away */
		bp->cnt = sizeof(struct phdr);	/* Simply rewind it */
	}
}

/* Switch the SCC to "transmit" mode */
/* Only to be called from an interrupt handler, while in AX.25 mode */
static void
scc_txon(scc)
register struct sccchan *scc;
{
	if (!scc->escc && !scc->fulldup && !scc->extclock) {	/* no fulldup divider? */
		cl(scc,R3,RxENABLE);		/* then switch off receiver */
		cl(scc,R5,TxENAB);		/* transmitter off during switch */
		scc_speed(scc,1,scc->speed);	/* reprogram baudrate generator */
		if ((scc->card->hwtype & HWBAYCOM) && (scc->reg_11 == 0)) {
			/* DPLL -> Rx clk, BRG -> Tx CLK, TxCLK -> TRxC pin */
			wr(scc,R11,RCDPLL|TCBR|TRxCOI|TRxCBR);
		}
	}
	or(scc,R5,RTS|TxENAB);			/* set the RTS line and enable TX */
	if (scc->card->hwtype & HWPRIMUS)	/* PRIMUS has another PTT bit... */
		WRREG(scc->ctrl + 4,scc->card->hwparam | 0x80);	/* set that bit! */
}

/* Switch the SCC to "receive" mode (or: switch off transmitter)
 * Only to be called from an interrupt handler, while in AX.25 mode
 */
static void
scc_txoff(scc)
register struct sccchan *scc;
{
	cl(scc,R5,RTS);				/* turn off RTS line */
	if (scc->card->hwtype & HWPRIMUS)	/* PRIMUS has another PTT bit... */
		WRREG(scc->ctrl + 4,scc->card->hwparam);	/* clear that bit! */

	if (!scc->escc && !scc->fulldup && !scc->extclock) {	/* no fulldup divider? */
		cl(scc,R5,TxENAB);		/* then disable the transmitter */
		scc_speed(scc,32,scc->speed);	/* back to receiver baudrate */
		if ((scc->card->hwtype & HWBAYCOM) && (scc->reg_11 == 0)) {
			/* DPLL -> Rx clk, DPLL -> Tx CLK, RxCLK -> TRxC pin */
			wr(scc,R11,RCDPLL|TCDPLL|TRxCOI|TRxCDP);
			WRSCC(scc->ctrl,R14,SEARCH|scc->wreg[R14]);	/* DPLL: enter search mode */
			or(scc,R3,ENT_HM|RxENABLE);			/* enable the receiver, hunt mode */
		}
	}
}

#endif	/* AX25 */

/* SCC timer interrupt handler. Will be called every 1/TPS s by the
 * routine systick in pc.c
 *
 * If we've configured a spare SCC channel to provide the ticks, then
 * this handler isn't needed. G8FSL 950125
 *
 */
void scctimer()
{
#ifdef AX25
	if (ticker_channel == -1)	/* No ticker channel set up */
		scc_timer_tick();
#endif
}

#ifdef AX25

/* A dummy interrupt handler for the tx and rx interrupts from the ticker
 * channel. Should never be called, but let's be defensive...
 */
void dummy_int(scc)
register struct sccchan *scc;
{
	scc->spints++;
	return;
}


/* The interrupt routine called from the timer tick channel
 */
void
scc_timer_interrupt(scc)
	struct sccchan *scc;
{
	register unsigned char status,changes;

	scc->exints++;

	status = RDREG(scc->ctrl);
	changes = status ^ scc->status;

	if ((status & ZCOUNT) || (changes == 0)) {
		scc_timer_tick();
	} else {
		scc->rxerrs++;
	}
	scc->status = status & ~ZCOUNT;	/* Save without Zero Count bit's value */
	WRREG(scc->ctrl,RES_EXT_INT);
}

/* The real SCC timer interrupt handler.
 * Called either from the routine systick in pc.c, or from the SCC interrupt
 * routine.
 */
void scc_timer_tick()
{
	register struct sccchan *scc;
	int i_state,j, doitagain, defer_it;

	if (SccBoards == 0)
		return;

	i_state = dirps();

	for (j = highest_device_attached; j >= 0 ; j--) {
		if ((scc = Sccchan[j]) != NULLCHAN
		  && scc->timercount != 0
		  && --(scc->timercount) == 0) {
			/* handle an SCC timer event for this SCC channel
			 * this can only happen when the channel is AX.25 type
			 * (the SLIP/KISS driver does not use timers)
			 */

			doitagain = 0;	/* use to force us out of the while loop */
			defer_it = 0;	/* also for ridding of the GOTO stuff */

			while (1)	/* using a while loop to get rid of GOTO tags */
			{

			switch (scc->a.tstate)
			{
				case IDLE:	/* it was idle, this is FULLDUP2 timeout */
					scc_txoff(scc);	/* switch-off the transmitter */
					break;

				case DEFER:	/* trying to get the channel */
					/* operation is as follows:
					 *
					 * CSMA: when channel clear AND persistence randomgenerator
					 * wins, AND group restrictions allow it:
					 *	keyup the transmitter
					 * if not, delay one SLOTTIME and try again
					 *
					 * FULL: always keyup the transmitter
					 */
					if (defer_it || scc->a.fulldup == 0)
					{
						Random = 21 * Random + 53;
						if (defer_it ||
#ifdef SCC_SOFTWARE_DCD
						  ((scc->a.flags & SCC_FLAG_SWDCD) && !(scc->status & SYNC_HUNT))
						  ||
#endif
						  (!(scc->a.flags & SCC_FLAG_SWDCD) && (scc->status & DCD))
						  || (scc->a.persist < Random) ) {

							/* defer transmission again. check for limit */
/* defer_it: */
							if (--(scc->a.maxdefer) == 0) {
							/* deferred too long. choice is to:
							 * - throw away pending frames, or
							 * - smash-on the transmitter and send them.
							 * the first would be the choice in a clean
							 * environment, but in the amateur radio world
							 * a distant faulty station could tie us up
							 * forever, so the second may be better...
							 */
#ifdef THROW_AWAY_AFTER_DEFER_TIMEOUT
								struct mbuf *bp,*bp1;

									while ((bp = scc->sndq) != NULLBUF) {
									scc->sndq = scc->sndq->anext;
									free_p(bp);
								}
#else
							/* GOTO keyup; just keyup transmitter ... */
								scc->a.tstate = KEYUP;
								doitagain = 1;
								break;
#endif
							}
							scc->timercount = scc->a.slottime;
							break;
					}
					if (uchar(scc->group) != NOGROUP) {
						/* If this channel is part of a group */
						int i;
						struct sccchan *scc2;

						for (i = 0; i <= highest_device_attached; i++)
							/* Look through all the other channels */
							if (
							  ((scc2 = Sccchan[i]) != NULLCHAN)		/* An attached channel */
							  && (scc2 != scc)				/* Not the one we're processing */
							  && (uchar(scc2->group) & uchar(scc->group))	/* In the current group */
							  && (
							  /* We've found another channel in this group. Need to check TX status
							   * for a TXGROUP, or RX status for an RXGROUP, (or both for both!)
							   */

							  /* RTS line is used for PTT */
							  ((scc->group & TXGROUP) && (scc2->wreg[R5] & RTS))
#ifdef SCC_SOFTWARE_DCD
							  || ((scc->group & RXGROUP)
							    && (scc->a.flags & SCC_FLAG_SWDCD)
							    && !(scc2->status & SYNC_HUNT))
#endif
							  || ((scc->group & RXGROUP)
							    && !(scc->a.flags & SCC_FLAG_SWDCD)
							    && (scc2->status & DCD))

							  || ((scc->group & TX2GROUP) && (scc2->a.tstate > DEFER))
							  )) {

							/* replace 'GOTO defer_it' with this */
								scc->a.tstate = DEFER;
								doitagain = 1;
								defer_it = 1;
								break;
							}
						}
					}

				case KEYUP:	/* keyup transmitter (note fallthrough) */

					if ((scc->wreg[R5] & RTS) == 0) {		/* when not yet keyed */
						scc->a.tstate = KEYWT;
						scc->timercount = scc->a.txdelay;	/* 0 if CTSwait */
						scc_txon(scc);
						break;
					}
					/* when already keyed, directly fall through */
				case KEYWT:	/* waited for CTS or TXDELAY */
					/* when a frame is available (it should be...):
					 * - dequeue it from the send queue
					 * - reset the transmitter CRC generator
					 * - set a timeout on transmission length, if defined
					 * - send the first byte of the frame
					 * - reset the EOM latch
					 * when no frame available, proceed to TAIL handling
					 */
					if ((scc->tbp = scc->sndq) != NULLBUF) {
						scc->sndq = scc->sndq->anext;
						WRREG(scc->ctrl,RES_Tx_CRC);
						scc->a.tstate = ACTIVE;
						if (ticker_channel == -1)
							scc->timercount = TPS * scc->a.maxkeyup;
						else
							scc->timercount = 100 * scc->a.maxkeyup;
						scc_sdlctx(scc);
						WRREG(scc->ctrl,RES_EOM_L);
						break;
					}
					/* when no frame queued, fall through to TAIL case */
				case TAIL:	/* at end of frame */
					/* when fulldup is 0 or 1, switch off the transmitter.
					 * when frames are still queued (because of transmit time limit),
					 * restart the procedure to get the channel after MINTIME.
					 * when fulldup is 2, the transmitter remains keyed and we
					 * continue sending. IDLETIME is an idle timeout in this case.
					 */
					if (scc->a.fulldup < 2) {
						scc->a.tstate = IDLE;
						scc_txoff(scc);

						if (scc->sndq != NULLBUF) {
							scc->a.tstate = DEFER;
							if (ticker_channel == -1) {
								scc->a.maxdefer = TPS * scc->a.idletime /
								  scc->a.slottime;
								scc->timercount = TPS * scc->a.mintime;
							} else {
								scc->a.maxdefer = 100 * scc->a.idletime /
								  scc->a.slottime;
								scc->timercount = 100 * scc->a.mintime;
							}
						}
						break;
					}
					if (scc->sndq != NULLBUF) {	/* still frames on the queue? */
						scc->a.tstate = KEYWT;	/* continue sending */
						if (ticker_channel == -1)
							scc->timercount = TPS * scc->a.mintime;	/* after mintime */
						else
							scc->timercount = 100 * scc->a.mintime;	/* after mintime */
					} else {
						scc->a.tstate = IDLE;
						if (ticker_channel == -1)
							scc->timercount = TPS * scc->a.idletime;
						else
							scc->timercount = 100 * scc->a.idletime;
					}
					break;
				case ACTIVE:	/* max keyup time expired */
				case FLUSH:	/* same while in flush mode */
					break;	/* no action required yet */
				default:	/* unexpected state */
					scc->a.tstate = IDLE;	/* that should not happen, but... */
					scc_txoff(scc);		/* at least stop the transmitter */
					break;
			}

			if (!doitagain)	/* this required because of while loop */
				break;

			}	/* end of while loop used to get rid of GOTO tags */
		}
	}
	restore(i_state);
}
#endif

#endif /* SCC */

