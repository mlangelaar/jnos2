/* AX25 control commands
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Mods by G1EMM
 * Mods by PA0GRI
 * Mods by N1BEE
 */
/*
** FILE: ax25cmd.c
**
** AX.25 command handler.
**
** 09/24/90 Bob Applegate, wa2zzx
**    Added BCTEXT, BC, and BCINTERVAL commands for broadcasting an id
**    string using UI frames.
**
** 27/09/91 Mike Bilow, N1BEE
**    Added Filter command for axheard control
*/
  
#ifdef MSDOS
#include <dos.h>
#endif
#include "global.h"
#ifdef AX25
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "ax25.h"
#include "lapb.h"
#include "cmdparse.h"
#include "socket.h"
#include "mailbox.h"
#include "session.h"
#include "tty.h"
#include "nr4.h"
#include "commands.h"
#include "pktdrvr.h"
#include "netrom.h"

#ifdef	HFDD
#include "hfdd.h"
#endif

#include "j2strings.h"	/* 12Apr2021, Maiko */
  
int axheard __ARGS((struct iface *ifp));
static int doaxfilter __ARGS((int argc,char *argv[],void *p));
static int doaxflush __ARGS((int argc,char *argv[],void *p));
static int doaxirtt __ARGS((int argc,char *argv[],void *p));
static int doaxkick __ARGS((int argc,char *argv[],void *p));
static int doaxreset __ARGS((int argc,char *argv[],void *p));
static int doaxroute __ARGS((int argc,char *argv[],void *p));
int doaxstat __ARGS((int argc,char *argv[],void *p));
static int dobc __ARGS((int argc,char *argv[],void *p));
static int dobcint __ARGS((int argc,char *argv[],void *p));
static int dobcport __ARGS((int argc,char *argv[],void *p));
static int dobctext __ARGS((int argc,char *argv[],void *p));
static int doaxhport __ARGS((int argc,char *argv[],void *p));
static int doaxhsize __ARGS((int argc,char *argv[],void *p));
static int dodigipeat __ARGS((int argc,char *argv[],void *p));
static int domycall __ARGS((int argc,char *argv[],void *p));
#ifdef	AX25_XDIGI
static int doax25xdigi (int argc, char **argv, void *p);
#endif
static int dobbscall __ARGS((int argc,char *argv[],void *p));
int donralias __ARGS((int argc,char *argv[],void *p));
static void ax_bc __ARGS((struct iface *axif));
static int axdest __ARGS((struct iface *ifp));
static int avdest __ARGS((struct iface *ifp));	/* 05Apr2021, Maiko (VE4KLM) new digi'd calls */
#ifdef TTYCALL
static int dottycall __ARGS((int argc, char *argv[], void *p));
#endif
#ifdef TNCALL
static int dotncall __ARGS((int argc, char *argv[], void *p));
#endif

#ifdef	BACKUP_AXHEARD
/* 27Jan2020, Maiko, Moved into ax25heard.c, since I need al_create() function */
/* 12Apr2021, Maiko (VE4KLM), sysop can now specify a filename, used to be void */
extern int doaxhsave (char*);
extern int doaxhload (char*);
#endif

static int doaxwindow __ARGS((int argc,char *argv[],void *p));
static int doblimit __ARGS((int argc,char *argv[],void *p));
static int domaxframe __ARGS((int argc,char *argv[],void *p));
static int don2 __ARGS((int argc,char *argv[],void *p));
static int dopaclen __ARGS((int argc,char *argv[],void *p));
static int dopthresh __ARGS((int argc,char *argv[],void *p));
static int dot2 __ARGS((int argc,char *argv[],void *p));      /* K5JB */
static int dot3 __ARGS((int argc,char *argv[],void *p));
static int doaxtype __ARGS((int argc,char *argv[],void *p));
static int dot4 __ARGS((int argc,char *argv[],void *p));
static int doversion __ARGS((int argc,char *argv[],void *p));
static int doaxmaxwait __ARGS((int argc,char *argv[],void *p));
static int doax25irtt  __ARGS((int argc,char *argv[],void *p));
 
int32 Axmaxwait;

extern char Myalias[AXALEN];    /* the NETROM alias in 'call' form */
extern char Nralias[ALEN+1];      /* the NETROM alias in 'alias' form */
#ifdef TTYCALL
extern char Ttycall[AXALEN];  /* the ttylink call in 'call' form */
#endif
#ifdef TNCALL
extern char tncall[AXALEN];  /* the telnet link call in 'call' form */
#endif
extern int axheard_filter_flag;     /* in axheard.c */
/* Defaults for IDing. */
char *axbctext;     /* Text to send */
static struct timer Broadtimer; /* timer for broadcasts */
  
#ifdef	AX25_XDIGI
extern int ax25_addxdigi (char*, char*, char*, char*);
extern void ax25_showxdigi (void);
#endif

char *Ax25states[] = {
    "",
    "Disconnected",
    "Listening",
    "Conn pending",
    "Disc pending",
    "Connected",
    "Recovery",
};
  
/* Ascii explanations for the disconnect reasons listed in lapb.h under
 * "reason" in ax25_cb
 */
char *Axreasons[] = {
    "Normal",
    "DM received",
    "Timeout"
};
  
static struct cmds DFAR Axcmds[] = {
    { "alias",    donralias,  0, 0, NULLCHAR },
#ifdef MAILBOX
    { "bbscall",  dobbscall,  0, 0, NULLCHAR },
#endif
    { "bc",           dobc,      0, 0, NULLCHAR },
    { "bcinterval",   dobcint,   0, 0, NULLCHAR },
    { "bcport",   dobcport,   0, 0, NULLCHAR },
    { "blimit",       doblimit,       0, 0, NULLCHAR },
    { "bctext",       dobctext,       0, 0, NULLCHAR },
    { "dest",         doaxheard,      0, 0, NULLCHAR },		/* 12Apr2021, Maiko, Put it back for historic reasons */
    { "digipeat", dodigipeat, 0, 0, NULLCHAR },
    { "filter",       doaxfilter,     0, 0, NULLCHAR },
    { "flush",        doaxflush,      0, 0, NULLCHAR },
    { "heard",        doaxheard,      0, 0, NULLCHAR },
    { "hearddest",    doaxheard,      0, 0, NULLCHAR },		/* 12Apr2021, Maiko, Put it back for historic reasons */
    { "hport",    doaxhport,  0, 0, NULLCHAR },
    { "hsize",    doaxhsize,  0, 0, NULLCHAR },
    { "irtt",     doaxirtt,   0, 0, NULLCHAR },
    { "kick",     doaxkick,       0, 2, "ax25 kick <axcb> | <remote call>" },
    { "maxframe",     domaxframe,     0, 0, NULLCHAR },
    { "maxwait",      doaxmaxwait,    0, 0, NULLCHAR },
    { "mycall",       domycall,       0, 0, NULLCHAR },
    { "paclen",       dopaclen,       0, 0, NULLCHAR },
    { "pthresh",      dopthresh,      0, 0, NULLCHAR },
    { "reset",    doaxreset,      0, 2, "ax25 reset <axcb> | <remote call>" },
    { "retries",      don2,           0, 0, NULLCHAR },
    { "route",        doaxroute,      0, 0, NULLCHAR },
    { "status",       doaxstat,       0, 0, NULLCHAR },
    { "t2",           dot2,           0, 0, NULLCHAR },	/* K5JB */
    { "t3",           dot3,           0, 0, NULLCHAR },
    { "t4",           dot4,           0, 0, NULLCHAR },
    { "timertype",    doaxtype,       0, 0, NULLCHAR },
#ifdef TTYCALL
    { "ttycall",      dottycall,  0, 0, NULLCHAR },
#endif
#ifdef TNCALL
    { "tncall",      dotncall,  0, 0, NULLCHAR },
#endif
    { "version",      doversion,      0, 0, NULLCHAR },
    { "window",       doaxwindow,     0, 0, NULLCHAR },
/* 31May2005, Maiko, New custom cross port digipeating rules */
#ifdef	AX25_XDIGI
    { "xdigi",		doax25xdigi,	0, 0, NULLCHAR },
#endif
    { NULLCHAR,		NULL,			0, 0, NULLCHAR }
};

/* Multiplexer for top-level ax25 command */
int
doax25(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Axcmds,argc,argv,p);
}
  
/* define the system's alias callsign ,
 * if netrom is used, this is also the netrom alias ! - WG7J
 */
  
int
donralias(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    char tmp[AXBUF];
  
    if(argc < 2) {
        tprintf("%s\n",pax25(tmp,Myalias));
        return 0;
    }
    if( (setcall(Myalias,argv[1]) == -1)
#ifdef NETROM
        || (putalias(Nralias,argv[1],1) == -1)
#endif
    ){
        j2tputs("can't set alias\n");
        Myalias[0] = '\0';
#ifdef NETROM
        Nralias[0] = '\0';
#endif
        return 0;
    }
#ifdef MAILBOX
    setmbnrid();
#endif
    return 0;
}
  
/*
** This function is called to send the current broadcast message
** and reset the timer.
*/
  
static int dobc(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifa;
  
    if (argc < 2)
    {
        tprintf("you need to specify an interface\n");
        return 1;
    }
  
    if((ifa=if_lookup(argv[1])) == NULLIF)
        tprintf(Badinterface,argv[1]);
    else if (ifa->type != CL_AX25)
        j2tputs("not an AX.25 interface\n");
    else
    {
        ax_bc(ifa);
        stop_timer(&Broadtimer) ;       /* in case it's already running */
        start_timer(&Broadtimer);               /* and fire it up */
    }
    return 0;
}
  
  
  
/*
** View/Change the message we broadcast.
*/
  
static int dobctext(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp;
  
    if (argc < 2) {
        if(axbctext)
            tprintf("Broadcast text: %s\n",axbctext);
        else
            j2tputs("not set\n");
    } else {
        if (axbctext != NULL)
            free(axbctext);
        axbctext = j2strdup(argv[1]);
        /* Set all ax.25 interfaces with no bc text */
        for(ifp=Ifaces;ifp->next;ifp=ifp->next)
            if(ifp->type == CL_AX25 && ifp->ax25->bctext == NULL)
                ifp->ax25->bctext = j2strdup(axbctext);
    }
    return 0;
}
  
/*
** Examine/change the broadcast interval.
*/
  
static int dobcint(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    void dobroadtick __ARGS((void));
  
    if(argc < 2)
    {
		/* 12Oct2009, Maiko, Use "%u" for uint32 vars */
        tprintf("Broadcast timer %d/%d seconds\n",
        	read_timer(&Broadtimer)/1000,
        		dur_timer(&Broadtimer)/1000);
        return 0;
    }
    stop_timer(&Broadtimer) ;       /* in case it's already running */
    Broadtimer.func = (void (*)__ARGS((void*)))dobroadtick;/* what to call on timeout */
    Broadtimer.arg = NULLCHAR;              /* dummy value */
    set_timer(&Broadtimer,(uint32)atoi(argv[1])*1000);     /* set timer duration */
    start_timer(&Broadtimer);               /* and fire it up */
    return 0;
}
  
/* Configure a port to do ax.25 beacon broadcasting */
static int
dobcport(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setflag(argc,argv[1],AX25_BEACON,argv[2]);
}
  
int Maxax25heard;
int numal,numad;        /* to limit heard and dest table entries - K5JB */
  
/* Set the size of the ax.25 heard list */
int doaxhsize(int argc,char *argv[],void *p) {
    if(argc > 1)                 /* if setting new size ... */
        doaxflush(argc,argv,p);  /* we flush to avoid memory problems - K5JB */
    return setint(&Maxax25heard,"Max ax-heard",argc,argv);
}
  
/* Configure a port to do ax.25 heard logging */
static int
doaxhport(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setflag(argc,argv[1],LOG_AXHEARD,argv[2]);
}
  
void
dobroadtick()
{
    struct iface *ifa;
  
    ifa = Ifaces;
  
    while (ifa != NULL)
    {
        if (ifa->flags & AX25_BEACON)
            ax_bc(ifa);
        ifa = ifa->next;
    }
  
    /* Restart timer */
    start_timer(&Broadtimer) ;
}
  
  
  
/*
** This is the low-level broadcast function.
*/
  
static void ax_bc(axiface)
struct iface *axiface;
{
    struct mbuf *hbp;
    int i;
  
    /* prepare the header */
    i = 0;
    if(axiface->ax25->bctext)
        i = strlen(axiface->ax25->bctext);
    if((hbp = alloc_mbuf(i)) == NULLBUF)
        return;
  
    hbp->cnt = i;
    if(i)
        memcpy(hbp->data,axiface->ax25->bctext,i);
  
    (*axiface->output)(axiface, Ax25multi[IDCALL],
#ifdef MAILBOX
    (axiface->ax25->bbscall[0] ? axiface->ax25->bbscall : axiface->hwaddr),
#else
    axiface->hwaddr,
#endif
    PID_NO_L3, hbp);        /* send it */
  
    /*
    ** Call another function to reset the timer...
    reset_bc_timer();
    */
}

extern int getusage (char*, char*); /* 28May2020, Maiko (VE4KLM), new usage function */

static int doaxhusage ()
{
	// tprintf ("Usage : ax heard *[ dest | digid ] all | <iface>\n        ax heard save | load *[ filename ]\n  * denotes optional arguments\n");

	getusage ("ax25", j2str_heard);	/* 12Apr2021, Maiko, customize if you like */

	return 1;
}
  
int doaxheard (int argc, char **argv, void *p)
{
    struct iface *ifp;

	int digid; /* 05Apr2021, Maiko (VE4KLM), new */

	char *fileptr;	/* 12Apr2021, Maiko (VE4KLM), now specify backup file */

	if (argc < 2)
		return doaxhusage ();

	digid = (strcmp (argv[1], "digid") == 0);	/* 05Apr2021, Maiko (VE4KLM), new */

	/*
	 * 12Apr2021, Maiko (VE4KLM), Got rid of the 'show' subcommand, keep the
	 * original syntax since I'm sure so many people, including myself, are
 	 * so used to just typing in 'ax25 h ax0', not 'ax25 h show ax0'.
	 *
	 * The dest and newly added digi'd subcommands can stay as is ...
	 *
	 * While I am @ it - add file name or path to BACKUP functions !
	 *
	 */
#ifdef  BACKUP_AXHEARD
	if (!strcmp (argv[1], "save"))
	{
		if (argc > 2)
			fileptr = argv[2];
		else
			fileptr = NULL;

		doaxhsave (fileptr);	/* 28Jan2020, Maiko, in axheard.c */
	}
	else if (!strcmp (argv[1], "load"))
	{
		if (argc > 2)
			fileptr = argv[2];
		else
			fileptr = NULL;

		doaxhload (fileptr);	/* 28Jan2020, Maiko, in axheard.c */
	}
	else	/* 21May2021, Maiko, OOPS, compile fails if BACKUP_AXHEARD not defined */
#endif
	/*
	 * 04Mar2022, Maiko, multiple heard lists for the same interface / port !
	 *
	 * I only have one radio on RPR, but what if I want to change frequencies
	 * and bands throughout the course of the day ? It would be nice to be able
 	 * to switch to a dedicated heard list for a particular frequency or band.
	 *
	 * For this prototype, an example syntax is 'ax25 h new=17m rp0'
	 *
	 * Which would create a heardlist called rp0_17m, and you can create as
	 * many new heardlists as you like for the same interface. To switch to
	 * any of the heardlists, you would use the same command again. They're
	 * created the first time IF they don't exist already, that's the idea.
	 *
	 */
	if (!strcmp (argv[1], "new="))
	{
		if (argc > 2)
		{
			if ((ifp = if_lookup (argv[2])) == NULLIF)
			{
				tprintf (Badinterface, argv[2]);
				return 1;
			}

			tprintf ("new command coming - multiple heard lists for a single port\n"); 

			/* this is just a frame work, feature is in the works */
		}
		else return doaxhusage ();
	}

	else if (!strcmp (argv[1], "dest") || digid)	/* 05Apr2021, Maiko, looking for digi'd calls */
	{
		if (argc > 2)
		{
			if (strcmp (argv[2], "all"))
			{
				if ((ifp = if_lookup (argv[2])) == NULLIF)
				{
					tprintf (Badinterface, argv[2]);
					return 1;
				}

				if (ifp->output != ax_output)
				{
					tprintf ("Interface %s not AX.25\n", argv[2]);
					return 1;
				}

				/* 05Apr2021, Maiko (VE4KLM), new digi'd source list feature */
				if (digid)
					avdest(ifp);
				else
					axdest(ifp);

				return 0;
			}
			else
			{
				for (ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
				{
					if (ifp->output != ax_output)
						continue;	/* Not an ax.25 interface */

					/* 05Apr2021, Maiko (VE4KLM), new digi'd source list feature */
					if (digid)
					{
						if(avdest(ifp) == EOF)
							break;
					}
					else
					{
						if(axdest(ifp) == EOF)
							break;
					}
				}
			}
		}
		else return doaxhusage ();
	}
	else
	{
		/*
		 * 12Apr2021, Maiko (VE4KLM), moved the original heard syntax down here to
		 * the bottom, and decided we don't need the 'show' subcommand, it bugs me
		 * having it there (sorry) - stick to original historic heard syntax, the
		 * only new thing is you must specify the iface name or the word 'all'.
		 */
		if (strcmp (argv[1], "all"))
		{
			if ((ifp = if_lookup (argv[1])) == NULLIF)
			{
				tprintf (Badinterface, argv[1]);
				return 1;
			}

			if (ifp->type != CL_AX25)
			{
				tprintf ("Interface %s not AX.25\n", argv[1]);
				return 1;
			}

			if (ifp->flags & LOG_AXHEARD)
				axheard (ifp);
			else
				j2tputs ("not active\n");
		}
		else
		{
			for (ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
			{
				if (ifp->type != CL_AX25 || !(ifp->flags & LOG_AXHEARD))
					continue;	/* Not an ax.25 interface */

				if (axheard (ifp) == EOF)
					break;
			}
		}
	}

    return 0;
}

int
axheard(ifp)
struct iface *ifp;
{
    int col = 0;
    struct lq *lp;
    char tmp[AXBUF];

    if(ifp->hwaddr == NULLCHAR)
        return 0;
  
    j2tputs("Interface  Station   Time since send  Pkts sent\n");
    tprintf("%-9s  %-9s   %12s    %7d\n",ifp->name,pax25(tmp,ifp->hwaddr),
    tformat(secclock() - ifp->lastsent),ifp->rawsndcnt);
  
    j2tputs("Station   Time since heard Pkts rcvd : ");
    j2tputs("Station   Time since heard Pkts rcvd\n");
    for(lp = Lq;lp != NULLLQ;lp = lp->next){
        if(lp->iface != ifp)
            continue;
        if(col)
            j2tputs("  : ");

        if(tprintf("%-9s   %12s    %7d",pax25(tmp,lp->addr),
            tformat(secclock() - lp->time),lp->currxcnt) == EOF)
            return EOF;

        if(col){
            if(tputc('\n') == EOF){
                return EOF;
            } else {
                col = 0;
            }
        } else {
            col = 1;
        }
    }
    if(col)
        tputc('\n');
    return 0;
}

static int
axdest(ifp)
struct iface *ifp;
{
    struct ld *lp;
    struct lq *lq;
    char tmp[AXBUF];
  
    if(ifp->hwaddr == NULLCHAR)
        return 0;
    tprintf("%s:\n",ifp->name);
    j2tputs("Station   Last ref         Last heard           Pkts\n");
    for(lp = Ld;lp != NULLLD;lp = lp->next){
        if(lp->iface != ifp)
            continue;
  
        tprintf("%-10s%-17s",
        pax25(tmp,lp->addr),tformat(secclock() - lp->time));
  
        if(addreq(lp->addr,ifp->hwaddr)){
            /* Special case; it's our address */
            tprintf("%-17s",tformat(secclock() - ifp->lastsent));
        } else if((lq = al_lookup(ifp,lp->addr,0)) == NULLLQ){
            tprintf("%-17s","");
        } else {
            tprintf("%-17s",tformat(secclock() - lq->time));
        }
        if(tprintf("%8d\n",lp->currxcnt) == EOF)
            return EOF;
    }
    return 0;
}

/*
 * 05Apr2021, Maiko (VE4KLM), shows digipeated source calls list
 */
static int avdest (struct iface *ifp)
{
    struct lv *lv;
    struct lq *lq;
    char tmp[AXBUF];
  
    if(ifp->hwaddr == NULLCHAR)
        return 0;
    tprintf("%s:\n",ifp->name);
    j2tputs("Station   Last Digipeated  Last heard Direct    Pkts  Digipeater\n");
  /*
   * 06Apr2021, Maiko, Figuring out new spacing to accomodate digi call
   *
             VE4KLM-12 123456789012345671234567890123456700000000  1234567890
   */
    for(lv = Lv;lv != NULLLV;lv = lv->next){
        if(lv->iface != ifp)
            continue;
  
        tprintf("%-10s%-17s",
        pax25(tmp,lv->addr),tformat(secclock() - lv->time));
  
        if(addreq(lv->addr,ifp->hwaddr)){
            /* Special case; it's our address */
            tprintf("%-17s",tformat(secclock() - ifp->lastsent));
        } else if((lq = al_lookup(ifp,lv->addr,0)) == NULLLQ){
            tprintf("%-17s","");
        } else {
            tprintf("%-17s",tformat(secclock() - lq->time));
        }
	/* 06Apr2021, Maiko, Added digi call, nice to know who it is */
        tprintf("%8d",lv->currxcnt);
        if (tprintf("  %-10s\n", pax25(tmp,lv->digi)) == EOF)
            return EOF;
    }
    return 0;
}
  
static int
doaxfilter(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc >= 2){
        setint(&axheard_filter_flag,"ax25 heard filter",argc,argv);
    } else {
        j2tputs("Usage: ax25 filter <0|1|2|3>\n");
        return 1;
    }
  
    j2tputs("Callsign logging by source ");
    if(axheard_filter_flag & AXHEARD_NOSRC)
        j2tputs("disabled, ");
    else
        j2tputs("enabled, ");
    j2tputs("by destination ");
    if(axheard_filter_flag & AXHEARD_NODST)
        j2tputs("disabled\n");
    else
        j2tputs("enabled\n");
    return 0;
}

static int
doaxflush(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct lq *lp,*lp1;
    struct ld *ld,*ld1;
  
    for(lp = Lq;lp != NULLLQ;lp = lp1){
        lp1 = lp->next;
        free((char *)lp);
    }
    Lq = NULLLQ;
    for(ld = Ld;ld != NULLLD;ld = ld1){
        ld1 = ld->next;
        free((char *)ld);
    }
    Ld = NULLLD;
    numad = numal = 0;  /* K5JB */
    return 0;
}
  
static int
doaxreset(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ax25_cb *axp;
 
	/* 31May2007, Maiko (VE4KLM), new getax25cb() function */
	if ((axp = getax25cb (argv[1])) == NULLAX25)
		j2tputs (Notval);
	else
		reset_ax25 (axp);

	return 0;
}
  
/* Display AX.25 link level control blocks */
int
doaxstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ax25_cb *axp;
    char tmp[AXBUF];
    char tmp2[AXBUF];
  
    if(argc < 2){
#ifdef UNIX
        /* 09Mar2021, Maiko, AXB is now up to 12 in width, give Iface 5 more spaces */
        tprintf("&AXB         Snd-Q   Rcv-Q   Remote    Local     Iface       State\n");
#else
        j2tputs("&AXB Snd-Q   Rcv-Q   Remote    Local     Iface  State\n");
#endif
        for(axp = Ax25_cb;axp != NULLAX25; axp = axp->next){
#ifdef UNIX
            /* 09Mar2021, Maiko, AXB is now up to 12 in width */
            if(tprintf("%-12lx %5d   %5d   %-10s%-10s%-12s%s\n",
#else
                if(tprintf("%4.4x %-8d%-8d%-10s%-10s%-7s%s\n",
#endif
                    FP_SEG(axp),
                    len_q(axp->txq),len_p(axp->rxq),
                    pax25(tmp,axp->remote),
                    pax25(tmp2,axp->local),
                    axp->iface?axp->iface->name:"",
                    Ax25states[axp->state]) == EOF)
                    return 0;
        }
        return 0;
    }

	/* 31May2007, Maiko (VE4KLM), new getax25cb() function */
	if ((axp = getax25cb (argv[1])) == NULLAX25)
		j2tputs (Notval);
	else
		st_ax25 (axp);

    return 0;
}

/* Dump one control block */
void
st_ax25(axp)
register struct ax25_cb *axp;
{
    char tmp[AXBUF];
    char tmp2[AXBUF];
  
    if(axp == NULLAX25)
        return;
#ifdef UNIX
    /* 09Mar2021, Maiko, AXB is now up to 12 in width */
    tprintf("&AXB         Local     Remote    Iface  RB V(S) V(R) Unack P Retry State\n");
    tprintf("%-12lx %-9s %-9s %-6s %c%c",FP_SEG(axp),
#else
    j2tputs("&AXB Local     Remote    Iface  RB V(S) V(R) Unack P Retry State\n");
    tprintf("%4.4x %-9s %-9s %-6s %c%c",FP_SEG(axp),
#endif
    pax25(tmp,axp->local),
    pax25(tmp2,axp->remote),
    axp->iface?axp->iface->name:"",
    axp->flags.rejsent ? 'R' : ' ',
    axp->flags.remotebusy ? 'B' : ' ');
    tprintf(" %4d %4d",axp->vs,axp->vr);
    tprintf(" %02u/%02u %u",axp->unack,axp->maxframe,axp->proto);
    tprintf(" %02u/%02u",axp->retries,axp->n2);
    tprintf(" %s\n",Ax25states[axp->state]);
	/* 12Oct2009, Maiko, Use "%u" for uint32 vars */
    tprintf("srtt = %u mdev = %u ",axp->srt,axp->mdev);
    j2tputs("T1: ");
    if(run_timer(&axp->t1))
        tprintf("%d",read_timer(&axp->t1));
    else
        j2tputs("stop");
    tprintf("/%d ms; ",dur_timer(&axp->t1));
  
    j2tputs("T3: ");
    if(run_timer(&axp->t3))
        tprintf("%d",read_timer(&axp->t3));
    else
        j2tputs("stop");
    tprintf("/%d ms; ",dur_timer(&axp->t3));
  
    j2tputs("T4: ");
    if(run_timer(&axp->t4))
        tprintf("%d",(read_timer(&axp->t4)/1000));
    else
        j2tputs("stop");
    tprintf("/%d sec\n",(dur_timer(&axp->t4)/1000));
}
  
/* Set limit on retransmission backoff */
int doax25blimit (int argc, char **argv, void *p)
{
    return setint32 (p, "blimit", argc, argv);
}

static int
doblimit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doax25blimit(argc,argv,(void *)&Blimit);
}
  
/* Set limit on retransmission in ms */
int doax25maxwait (int argc, char **argv, void *p)
{
    return setint32 (p, "retry maxwait", argc, argv);
}
  
static int
doaxmaxwait(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doax25maxwait(argc,argv,(void *)&Axmaxwait);
}
  
/* Display or change our AX.25 address */
static int
domycall(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char tmp[AXBUF];
  
    if(argc < 2){
        tprintf("%s\n",pax25(tmp,Mycall));
        return 0;
    }
    if(setcall(Mycall,argv[1]) == -1)
        return -1;
#ifdef MAILBOX
    if(Bbscall[0] == 0)
        memcpy(Bbscall,Mycall,AXALEN);
    setmbnrid();
#endif
    return 0;
}

#ifdef	AX25_XDIGI

/* 31May2005, Maiko, Finished up the ax25xdigi NOS subcommands */
/* 05Jun2005, Maiko, Added callsign substitution feature */

static int doax25xdigi (int argc, char **argv, void *p)
{
	int retval = 0;

	char *argptr = (char*)0;

	if (argc < 4)
	{
		ax25_showxdigi ();
	}
	else
	{
		if (argc == 5)
			argptr = argv[4];

		retval = ax25_addxdigi (argv[1], argv[2], argv[3], argptr);
	}

    return retval;
}
#endif
  
#ifdef MAILBOX
/* Display or change our BBS address */
static int
dobbscall(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char tmp[AXBUF];
    struct iface *ifp;
  
    if(argc < 2){
        tprintf("%s\n",pax25(tmp,Bbscall));
        return 0;
    }
    if(setcall(Bbscall,argv[1]) == -1)
        return -1;
    /* Set the bbs call on all appropriate interfaces
     * that don't have a bbs call set yet.
     */
    for(ifp = Ifaces;ifp != NULLIF;ifp = ifp->next)
        if(ifp->type == CL_AX25 && ifp->ax25->bbscall[0] == 0)
            memcpy(ifp->ax25->bbscall,Bbscall,AXALEN);
    return 0;
}
#endif
  
#ifdef TTYCALL
/* Display or change ttylink AX.25 address */
static int
dottycall(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char tmp[AXBUF];
  
    if(argc < 2){
        tprintf("%s\n",pax25(tmp,Ttycall));
        return 0;
    }
    if(setcall(Ttycall,argv[1]) == -1)
        return -1;
    return 0;
}
#endif

#ifdef TNCALL

/* 01Aug2019, add some parameters */

char *tnlink_host = (char*)0;
int	tnlink_port;
int	tnlink_cronly = 0;

/* Display or change Winlink RMS AX.25 address
 * 30Jul2019, not a generic telnet jumpstart call
 */
static int dotncall (int argc, char **argv, void *p)
{
    char tmp[AXBUF];
  
	if ((argc == 2) && !strncmp (argv[1], "off", 3))
	{
		free (tnlink_host);
		tnlink_host = (char*)0;
		tprintf ("unconfigured\n");
		return 0;
	}

    if (argc < 4)
	{
		tprintf ("Setup an AX25 <-> TELNET conduit (supports Packet Winlink clients)\n");
		tprintf ("Usage : ax25 tncall [callsign] [telnet host] [telnet port] *[cronly]\n");
		tprintf ("\t * required for winlink, otherwise optional\n");
		tprintf ("\t use 'ax25 tncall off' to unconfigure.\n");
		if (tnlink_host)
		{
			tprintf ("configured : %s <-> %s:%d", pax25 (tmp, tncall), tnlink_host, tnlink_port);

			if (tnlink_cronly) tprintf (" (cronly)");

			tprintf ("\n");
		}
		else tprintf ("not configured\n");
        return 0;
    }

    if (setcall (tncall, argv[1]) == -1)
	{
		tprintf ("error setting ax25 callsign\n");
		return -1;
	}

	tnlink_host = j2strdup (argv[2]);

	if (!(tnlink_port = atoi (argv[3])))
	{
		tprintf ("error setting telnet port\n");
		return -1;
	}

	tnlink_cronly = 0;

	if ((argc == 5) && !strncmp (argv[4], "cronly", 6))
		tnlink_cronly = 1;

    return 0;
}

#endif
  
/* Control AX.25 digipeating */
static int
dodigipeat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setflag(argc,argv[1],AX25_DIGI,argv[2]);
}
  
int
doax25version(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setshort((unsigned short *)p,"AX25 version",argc,argv);
}
  
static int
doversion(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doax25version(argc,argv,(void *)&Axversion);
}
  
static int doax25irtt (int argc, char **argv, void *p)
{
    return setint32 (p, "Initial RTT (ms)", argc, argv);
}
  
static int
doaxirtt(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doax25irtt(argc,argv,(void *)&Axirtt);
}
  
/* Set T2 timer - K5JB */
int doax25t2 (int argc, char **argv, void *p)
{
    return setint32 (p, "Resptime, T2 (ms)", argc, argv);
}

/* Set T2 timer - K5JB */
static int
dot2(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doax25t2(argc,argv,(void*)&T2init);
}

/* Set idle timer */
int doax25t3 (int argc, char **argv, void *p)
{
    return setint32 (p, "Idle poll timer (ms)", argc, argv);
}

/* Set idle timer */
static int
dot3(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doax25t3(argc,argv,(void*)&T3init);
}
  
/* Set link redundancy timer */
int doax25t4 (int argc, char **argv, void *p)
{
    return setint32 (p, "Link redundancy timer (sec)", argc, argv);
}
  
/* Set link redundancy timer */
static int
dot4(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doax25t4(argc,argv,(void*)&T4init);
}
  
/* Set retry limit count */
int
doax25n2(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setshort((unsigned short *)p,"Retry limit",argc,argv);
}
  
static int
don2(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doax25n2(argc,argv,(void *)&N2);
}
  
/* Force a retransmission */
static int
doaxkick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ax25_cb *axp;

	/* 31May2007, Maiko (VE4KLM), new getax25cb() function */
	if ((axp = getax25cb (argv[1])) == NULLAX25)
		j2tputs (Notval);
	else
		recover (axp);	/* call recover() direct, since we know for sure
						 * that axp is valid at this point. No point calling
						 * kick_ax25() which simply loops through the whole
						 * AX25 callback link list to make sure axp is in
						 * the list, and then calls recover().
						 */
    return 0;
}
  
/* Set maximum number of frames that will be allowed in flight */
int
doax25maxframe(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setshort((unsigned short *)p,"Window size (frames)",argc,argv);
}
  
static int
domaxframe(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doax25maxframe(argc,argv,(void *)&Maxframe);
}
  
/* Set maximum length of I-frame data field */
int
doax25paclen(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setshort((unsigned short *)p,"Max frame length (bytes)",argc,argv);
}
  
static int
dopaclen(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doax25paclen(argc,argv,(void *)&Paclen);
}
  
/* Set size of I-frame above which polls will be sent after a timeout */
int
doax25pthresh(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setshort((unsigned short *)p,"Poll threshold (bytes)",argc,argv);
}
  
static int
dopthresh(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doax25pthresh(argc,argv,(void *)&Pthresh);
}
  
/* Set high water mark on receive queue that triggers RNR */
int
doax25window(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setshort((unsigned short *)p,"AX25 receive window (bytes)",argc,argv);
}
  
static int
doaxwindow(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doax25window(argc,argv,(void *)&Axwindow);
}
/* End of ax25 subcommands */
  
#if defined(AX25SESSION) || defined(MAILBOX) || defined(AXUISESSION)
/* Consolidate into one place the parsing of a connect path containing digipeaters.
 * This is for gateway connects (mboxgate.c), by console connect cmd, by the
 * axui command, and by the BBS forwarding process.
 * We will place the derived path into the ax25 route table.
 * Input: tokenized cmd line argc/argv: cmd iface target [via] digi1 [,] digi2 ... digiN
 *        interface *ifp.
 *        target, the node reached via digipeating.
 * Output: ax25 route table is updated.
 * Return code: 0 => error, else number of digis in path.  -- K5JB
*/
int
connect_filt(int argc,char *argv[],char target[],struct iface *ifp)
{
    int i, rtn_argc, ndigis,dreg_flag = 0;
	int get_dreg_flag = 1, skip_flag;
    char digis[MAXDIGIS][AXALEN];
    char *nargv[MAXDIGIS + 1];
    char *cp,*dreg;

    ndigis = argc - 3;  /* worst case */

    for(i=0,rtn_argc=0; i<ndigis && rtn_argc < MAXDIGIS + 1; i++)
	{
        cp = argv[i+3];

		get_dreg_flag = 1;

		while (get_dreg_flag)	/* replaces GOTO get_dreg label */
		{

		skip_flag = 0;

        switch(*cp)
		{
            case ',':
                if(!*(cp + 1))  /* lone comma */
					skip_flag = 1;
                else
				{ /* leading comma, a faux pas */
                    nargv[rtn_argc++] = ++cp;
                    if(*cp == ',')	/* sequential commas are illegal */
					{
                        rtn_argc = MAXDIGIS + 1;  /* force for() loop exit */
                        skip_flag = 1;
                    }
                }
				break;
            case 'v':
                if(!strncmp("via",cp,strlen(cp)))  /* should get 'em all */
				{
                    skip_flag = 1;
					break;
				}
				/* flow thru */
            default:
                nargv[rtn_argc++] = cp;
                break;
        }
		if (skip_flag)	/* break out of get_dreg_flag while loop */
			break;
        for(; *cp; cp++)	/* remove any trailing commas */
		{
            if(*cp == ',')
			{
                *cp = '\0';
                if(*(cp + 1) != '\0')	/* something dangling */
				{
                    dreg = cp + 1;   /* restart scan here */
                    dreg_flag = 1;
                }
                break;
            }
		}
        if(dreg_flag)
		{
            dreg_flag = 0;
            cp = dreg;      /* go back and get the dreg */
            get_dreg_flag = 1;
        }
		else get_dreg_flag = 0;

		}	/* end of get_dreg_flag while loop */
    }

    if(rtn_argc > MAXDIGIS || ndigis < 1 || ifp==NULLIF)
	{
        j2tputs("Too many digipeaters, or badly formed command.\n");
        return 0;
    }
    for(i=0;i<rtn_argc;i++)
	{
        if(setcall(digis[i],nargv[i]) == -1)
		{
            tprintf("Bad digipeater %s\n",nargv[i]);
            return 0;
        }
    }
    if(ax_add(target,AX_AUTO,digis,rtn_argc,ifp) == NULLAXR)
	{
        j2tputs("AX25 route add failed\n");
        return 0;
    }
    return(rtn_argc);
}
#endif

#ifdef AX25SESSION
/* Initiate interactive AX.25 connect to remote station */
int
doconnect(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct sockaddr_ax fsocket;
    struct session *sp;
    struct iface *ifp;
    char target[AXALEN];
    int split = 0;
  
    /*Make sure this comes from console - WG7J*/
    if(Curproc->input != Command->input)
        return 0;

#ifdef SPLITSCREEN
    if(argv[0][0] == 's')   /* use split screen */
        split  = 1;
#endif

#ifdef	HFDD

	/* 05Jan2005, Maiko, HFDD should be console accessible */
	if (hfdd_iface (argv[1]))
	{
		char **pargv;

		int cnt;

    	if ((sp = newsession ("hfdd", TELNET, split)) == NULLSESSION)
		{
        	j2tputs (TooManySessions);
        	return 1;
    	}

		/*
		 * 06Jan2005, Maiko, Man !!! this got me good. You MUST duplicate
		 * your ARGC and ARGV values that you pass onto your process !!!
		 * 
		 * Failure to do this will catch you really good when you get deep
		 * into your processes and long after these were passed.
		 *
		 */

		pargv = (char**)callocw (4, sizeof(char*));

		for (cnt = 0; cnt < 3; cnt++)
			pargv[cnt] = j2strdup (argv[cnt]);

		pargv[3] = j2strdup ("keyboard");	/* 25Jan2005, trigger kybrd display */

		newproc ("hfddcon", 1024, hfdd_console, argc, pargv, sp, 1);

		return 0;
	}

#endif
  
    if(((ifp = if_lookup(argv[1])) == NULLIF) || (ifp->type != CL_AX25)) {
        tprintf("Iface %s not defined or not an AX25 type interface\n",argv[1]);
        return 1;
    }
  
    if(setcall(target,argv[2]) == -1){
        tprintf("Bad callsign %s\n", argv[2]);
        return 1;
    }
  
    /* If digipeaters are given, put them in the routing table */
    if(argc > 3){
        if(connect_filt(argc,argv,target,ifp) == 0)
            return 1;
    }
    /* Allocate a session descriptor */
    if((sp = newsession(argv[2],AX25TNC,split)) == NULLSESSION){
        j2tputs(TooManySessions);
        return 1;
    }
    if((sp->s = j2socket(AF_AX25,SOCK_STREAM,0)) == -1){
        j2tputs(Nosock);
        freesession(sp);
        keywait(NULLCHAR,1);
        return 1;
    }
    fsocket.sax_family = AF_AX25;
    setcall(fsocket.ax25_addr,argv[2]);

#ifdef JNOS20_SOCKADDR_AX
	/* 30Aug2010, Maiko, New way to reference interface information */
	memset (fsocket.filler, 0, ILEN);
	fsocket.iface_index = if_indexbyname (argv[1]);
#else
    strncpy(fsocket.iface,argv[1],ILEN);
#endif

    return tel_connect(sp, (char *)&fsocket, sizeof(struct sockaddr_ax));
}
#endif
  
/* Display and modify AX.25 routing table */
static int
doaxroute(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char tmp[AXBUF];
    int i,ndigis;
    register struct ax_route *axr;
    char target[AXALEN],digis[MAXDIGIS][AXALEN];
    struct iface *iface;
  
    if(argc < 2){
        j2tputs("Target    Iface  Type  Mode Digipeaters\n");
        for(axr = Ax_routes;axr != NULLAXR;axr = axr->next){
            tprintf("%-10s%-7s%-6s",pax25(tmp,axr->target),
            axr->iface->name,axr->type == AX_LOCAL ? "Local" :
            axr->type == AX_AUTO ?  "Auto" : "Perm" );
            switch(axr->mode){
                case AX_VC_MODE:
                    j2tputs(" VC ");
                    break;
                case AX_DATMODE:
                    j2tputs(" DG ");
                    break;
                case AX_DEFMODE:
                    j2tputs(" IF ");
                    break;
                default:
                    j2tputs(" ?? ");
                    break;
            }
  
            for(i=0;i<axr->ndigis;i++){
                tprintf(" %s",pax25(tmp,axr->digis[i]));
            }
            if(tputc('\n') == EOF)
                return 0;
        }
        return 0;
    }
    if(argc < 4){
        j2tputs("Usage: ax25 route add <target> <iface> [digis...]\n");
        j2tputs("       ax25 route drop <target> <iface>\n");
        j2tputs("       ax25 route mode <target> <iface> [mode]\n");
        j2tputs("       ax25 route perm <target> <iface> [digis...]\n");
        return 1;
    }
    if(setcall(target,argv[2]) == -1){
        tprintf("Bad target %s\n",argv[2]);
        return 1;
    }
    if((iface = if_lookup(argv[3])) == NULLIF){
        tprintf(Badinterface,argv[3]);
        return 1;
    }
    switch(argv[1][0]){
        case 'a':       /* Add route */
        case 'p':       /* add a permanent route - K5JB */
            ndigis = argc - 4;
            if(ndigis > MAXDIGIS){
                j2tputs("Too many digipeaters\n");
                return 1;
            }
            for(i=0;i<ndigis;i++){
                if(setcall(digis[i],argv[i+4]) == -1){
                    tprintf("Bad digipeater %s\n",argv[i+4]);
                    return 1;
                }
            }
            if(ax_add(target,argv[1][0] == 'a' ? AX_LOCAL : AX_PERM,
               digis,ndigis,iface) == NULLAXR){        /* K5JB */
                j2tputs("Failed\n");
                return 1;
            }
            break;
        case 'd':       /* Drop route */
            if(ax_drop(target,iface,1) == -1){
                j2tputs("Not in table\n");
                return 1;
            }
            break;
        case 'm':       /* Alter route mode */
            if((axr = ax_lookup(target,iface)) == NULLAXR){
                j2tputs("Not in table\n");
                return 1;
            }
            switch(argv[4][0]){
                case 'i':       /* use default interface mode */
                    axr->mode = AX_DEFMODE;
                    break;
                case 'v':       /* use virtual circuit mode */
                    axr->mode = AX_VC_MODE;
                    break;
                case 'd':       /* use datagram mode */
                    axr->mode = AX_DATMODE;
                    break;
                default:
                    tprintf("Unknown mode %s\n", argv[4]);
                    return 1;
            }
            break;
        default:
            tprintf("Unknown command %s\n",argv[1]);
            return 1;
    }
    return 0;
}
  
extern int lapbtimertype;
  
/* ax25 timers type - linear v exponential */
int
doax25timertype(argc,argv,p)
int argc ;
char *argv[] ;
void *p ;
{
    if (argc < 2) {
        j2tputs("AX25 timer type is ");
        switch(*(int *)p){
            case 2:
                j2tputs("original\n");
                break;
            case 1:
                j2tputs("linear\n");
                break;
            case 0:
                j2tputs("exponential\n");
                break;
        }
        return 0 ;
    }
  
    switch (argv[1][0]) {
        case 'o':
        case 'O':
            *(int *)p = 2 ;
            break ;
        case 'l':
        case 'L':
            *(int *)p = 1 ;
            break ;
        case 'e':
        case 'E':
            *(int *)p = 0 ;
            break ;
        default:
            j2tputs("usage: ax25 timertype [original|linear|exponential]\n") ;
            return -1 ;
    }
  
    return 0 ;
}
  
/* ax25 timers type - linear v exponential */
static int
doaxtype(argc,argv,p)
int argc ;
char *argv[] ;
void *p ;
{
    return doax25timertype(argc,argv,(void *)&lapbtimertype);
}
  
  
void init_ifax25(struct ifax25 *ax25) {
  
    if(!ax25)
        return;
    /* Set interface to the defaults */
    ax25->paclen = Paclen;
    ax25->lapbtimertype = lapbtimertype;
    ax25->irtt = Axirtt;
    ax25->version = Axversion;
    ax25->t2 = T2init;	/* K5JB */
    ax25->t3 = T3init;
    ax25->t4 = T4init;
    ax25->n2 = N2;
    ax25->maxframe = Maxframe;
    ax25->pthresh = Pthresh;
    ax25->window = Axwindow;
    ax25->blimit = Blimit;
    ax25->maxwait = Axmaxwait;
    if(axbctext)
        ax25->bctext = j2strdup(axbctext);
#ifdef MAILBOX
    memcpy(ax25->bbscall,Bbscall,AXALEN);
#endif
}
  
static int doifax25paclen __ARGS((int argc, char *argv[], void *p));
static int doifax25timertype __ARGS((int argc, char *argv[], void *p));
static int doifax25irtt __ARGS((int argc, char *argv[], void *p));
static int doifax25version __ARGS((int argc, char *argv[], void *p));
static int doifax25t2 __ARGS((int argc, char *argv[], void *p));	/* K5JB */
static int doifax25t3 __ARGS((int argc, char *argv[], void *p));
static int doifax25t4 __ARGS((int argc, char *argv[], void *p));
static int doifax25n2 __ARGS((int argc, char *argv[], void *p));
static int doifax25maxframe __ARGS((int argc, char *argv[], void *p));
static int doifax25pthresh __ARGS((int argc, char *argv[], void *p));
static int doifax25window __ARGS((int argc, char *argv[], void *p));
static int doifax25blimit __ARGS((int argc, char *argv[], void *p));
static int doifax25maxwait __ARGS((int argc, char *argv[], void *p));
static int doifax25bctext __ARGS((int argc, char *argv[], void *p));
static int doifax25bbscall __ARGS((int argc, char *argv[], void *p));
static int doifax25cdigi __ARGS((int argc, char *argv[], void *p));
  
/* These are the command that set ax.25 parameters per interface */
static struct cmds DFAR Ifaxcmds[] = {
#ifdef MAILBOX
    { "bbscall",  doifax25bbscall,    0, 0, NULLCHAR },
#endif
    { "bctext",   doifax25bctext,     0, 0, NULLCHAR },
    { "blimit",   doifax25blimit,     0, 0, NULLCHAR },
    { "cdigi",    doifax25cdigi,      0, 0, NULLCHAR },
    { "irtt",     doifax25irtt,       0, 0, NULLCHAR },
    { "maxframe", doifax25maxframe,   0, 0, NULLCHAR },
    { "maxwait",  doifax25maxwait,    0, 0, NULLCHAR },
    { "paclen",   doifax25paclen,     0, 0, NULLCHAR },
    { "pthresh",  doifax25pthresh,    0, 0, NULLCHAR },
    { "retries",  doifax25n2,         0, 0, NULLCHAR },
    { "timertype",doifax25timertype,  0, 0, NULLCHAR },
    { "t2",       doifax25t2,         0, 0, NULLCHAR },	/* K5JB */
    { "t3",       doifax25t3,         0, 0, NULLCHAR },
    { "t4",       doifax25t4,         0, 0, NULLCHAR },
    { "version",  doifax25version,    0, 0, NULLCHAR },
    { "window",   doifax25window,     0, 0, NULLCHAR },
    { NULLCHAR,	NULL,		0, 0, NULLCHAR }
};
  
int ifax25(int argc,char *argv[],void *p) {
  
    if(((struct iface *)p)->type != CL_AX25)
        return j2tputs("not an AX.25 interface\n");
    return subcmd(Ifaxcmds,argc,argv,p);
};
  
  
  
#ifdef MAILBOX
/* Set bbs ax.25 address */
static int
doifax25bbscall(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
    char tmp[AXBUF];
  
    if(argc == 1)
        tprintf("Bbscall %s\n",pax25(tmp,ifp->ax25->bbscall));
    else
        setcall(ifp->ax25->bbscall,argv[1]);
    return 0;
}
#endif
  
/* Set ax.25 bctext */
static int
doifax25bctext(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    if(argc == 1)
        tprintf("Bctext : %s\n",ifp->ax25->bctext);
    else {
        if(ifp->ax25->bctext)
            free(ifp->ax25->bctext);
        if(strlen(argv[1]) == 0)    /* clearing the buffer */
            ifp->ax25->bctext = NULL;
        else
            ifp->ax25->bctext = j2strdup(argv[1]);
    }
    return 0;
}
  
static int
doifax25blimit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    return doax25blimit(argc,argv,(void *)&ifp->ax25->blimit);
}
  
/* Set cross band digi call address */
static int
doifax25cdigi(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
    char tmp[AXBUF];
  
    if(argc == 1)
        tprintf("Cdigi %s\n",pax25(tmp,ifp->ax25->cdigi));
    else
        setcall(ifp->ax25->cdigi,argv[1]);
    return 0;
}
  
static int
doifax25irtt(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    return doax25irtt(argc,argv,(void *)&ifp->ax25->irtt);
}
  
static int
doifax25maxframe(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    return doax25maxframe(argc,argv,(void *)&ifp->ax25->maxframe);
}
  
static int
doifax25maxwait(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    return doax25maxwait(argc,argv,(void *)&ifp->ax25->maxwait);
}
  
  
/* Set interface AX.25 Paclen, and adjust the NETROM mtu for the
 * smallest paclen. - WG7J
 */
static int
doifax25paclen(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
#ifdef NETROM
    int tmp;
#endif
  
    doax25paclen(argc,argv,(void *)&ifp->ax25->paclen);
#ifdef NETROM
    if(argc > 1 && ifp->flags & IS_NR_IFACE) {
        if((tmp=ifp->ax25->paclen - 20) < Nr_iface->mtu)
            Nr_iface->mtu = tmp;
    }
#endif
    return 0;
}
  
static int
doifax25pthresh(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    return doax25pthresh(argc,argv,(void *)&ifp->ax25->pthresh);
}
  
static int
doifax25n2(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    return doax25n2(argc,argv,(void *)&ifp->ax25->n2);
}
  
static int
doifax25timertype(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    return doax25timertype(argc,argv,(void *)&ifp->ax25->lapbtimertype);
}
  
static int	/* K5JB */
doifax25t2(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	return doax25t2(argc,argv,(void *)&ifp->ax25->t2);
}

static int
doifax25t3(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    return doax25t3(argc,argv,(void *)&ifp->ax25->t3);
}
  
static int
doifax25t4(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    return doax25t4(argc,argv,(void *)&ifp->ax25->t4);
}
  
static int
doifax25version(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    return doax25version(argc,argv,(void *)&ifp->ax25->version);
}
  
static int
doifax25window(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    return doax25window(argc,argv,(void *)&ifp->ax25->window);
}
  
#endif /* AX25 */
