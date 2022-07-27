/* TCP control and status routines
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Mods by G1EMM
 * Mods by WG7J
 * Mods by WA3DSP
 * Mods by PA0GRI
 * Copyright 1992 Gerard J van der Grinten, PA0GRI
 */
#ifdef MSDOS
#include <dos.h>
#endif
#include <ctype.h>
#include "global.h"
#include "timer.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "iface.h"
#include "tcp.h"
#include "cmdparse.h"
#include "commands.h"
#include "socket.h"
  
#ifdef  TCPACCESS

/*
 * 21Feb2015, Maiko (VE4KLM), Maybe an exercise in vanity or just to prove I
 * can still write code using pointers to functions ? Technically I am using
 * the same code to delete entries from the tcp access list in expiry process
 * and also when deleted via the 'tcp access' console command, only difference
 * being in the conditional which determines if an entry is deleted or not. So
 * let's have just one function to delete the entries, and pass this function
 * a pointer to a 'conditional function' to say yes or no to deletion. Makes
 * sense right ? Sure, yup, absolutely - enable with '#define PTR2CONDFUNC'.
 */

#ifdef	PTR2CONDFUNC	/* exercise in complicatedness (a new work maybe) */

int deltaccess ((int*) condfunc (int), int outfirstmatch)
{
    struct rtaccess *prev, *head, *tpacc;

	prev = NULLACCESS;

	head = tpacc = TCPaccess;

	while (tpacc != NULLACCESS)
	{
		head = tpacc; tpacc = tpacc->nxtbits;

		/* Only check if age is not -1, else default gets hit, and crash */
		if ((head->age != -1) && ((secclock () - head->age) > expired))
		{
			if (head == TCPaccess)	/* special case */
				TCPaccess = head->nxtbits;
			else
				prev->nxtbits = tpacc;

			log (-1, "tcp access [%s] expired", inet_ntoa (head->target));

			free (head);

			if (outfirstmatch)
				return 0;
		}
		else prev = head;
	}
}

#endif	/* end of PTR2CONDFUNC */

static int doaccess __ARGS((int argc,char *argv[],void *p));

#ifdef TCPACCESS_EXPIRY

#ifdef	BLACKLIST_BAD_LOGINS
extern int MbBlackList;		/* 16Feb2015, Maiko, Now holds expiry time */
#endif

/* 
 * 12Feb2015, Maiko (VE4KLM), get rid of expired tcp access rules
 *
 * 21Feb2015, Maiko (VE4KLM), fix up my delete code (crashing), went
 * over the 'real' delete code already in here - man am I stupid !!!
 */

static struct timer ExpireTA;

static void expiretaccess (void)
{
    struct rtaccess *prev, *head, *tpacc;

	int32 expired = MbBlackList;

	// log (-1, "expiring tcp access rules ...");

	prev = NULLACCESS;

	head = tpacc = TCPaccess;

	while (tpacc != NULLACCESS)
	{
		head = tpacc; tpacc = tpacc->nxtbits;

		/* Only check if age is not -1, else default gets hit, and crash */
		if ((head->age != -1) && ((secclock () - head->age) > expired))
		{
			if (head == TCPaccess)	/* special case */
				TCPaccess = head->nxtbits;
			else
				prev->nxtbits = tpacc;

			log (-1, "tcp access [%s] expired", inet_ntoa (head->target));

			free (head);
		}
		else prev = head;
	}

	start_timer (&ExpireTA);
}

int doaccessexpiry (int argc, char **argv, void *p)
{
	if (argc < 2)
	{
		tprintf ("TCP Access Entry Expiry timer = %d/%d mins\n",
			read_timer (&ExpireTA) / 60000, dur_timer (&ExpireTA) / 60000);

		return 0;
	}

	if (*argv[1] == 'n')
	{
		expiretaccess ();

		return 0;
	}

	stop_timer (&ExpireTA);

	ExpireTA.func = (void*)expiretaccess;

	ExpireTA.arg = NULL;

	set_timer (&ExpireTA, 60000 * atoi (argv[1]));	/* minutes */

	start_timer (&ExpireTA);

	return 0;
}

#endif	/* end of TCPACCESS_EXPIRY */

/*
 * add an entry to the access control list
 * not a lot of error checking 8-)
 *
 * 15Jan2015, Maiko (VE4KLM), Moved up here, then we don't have to
 * have that extra prototype at the top of the file anymore, one less
 * thing to have to make sure is consistent with the actual function.
 *
 * Also, I would like the ability to INSERT an entry somewhere within
 * the list, that way I can add 'tcp access deny' entry without having
 * to delete the 'tcp permit all' first, only to have to add it again
 * after I add the entry. I'm sure most of you know what I mean :)
 *
 * New 'index' parameter - list depth, 0 very top, - values from end, +
 * values from top, 99999 special value means use the original code !
 *
 * 25Feb2015, Maiko (VE4KLM), new special value 99998 specific to the
 * blacklist (mbox) calling of this function (enables the age value),
 * and - values not quite there yet (commented out).
 *
 */

void addtaccess (target,bits,low,high,permit,index)
int32 target;           /* Target IP address prefix */
unsigned int bits;      /* Size of target address prefix in bits (0-32) */
int16 low;
int16 high;
int16 permit;
int index;	/* 15Jan2015, new parameter (see above comments) */
{
    struct rtaccess *tpacc; /*temporary*/
    struct rtaccess *holder; /*for the new record*/
  
    holder = (struct rtaccess *)callocw(1,sizeof(struct rtaccess));
    holder->nxtiface = NULLACCESS;
    holder->nxtbits = NULLACCESS;
    holder->target = target;
    holder->bits = bits;
    holder->lowport = low;
    holder->highport = high;
    holder->status = permit;

#ifdef TCPACCESS_EXPIRY
	holder->age = -1;	/* 12Feb2015, Maiko (VE4KLM), default not used ! */
#endif

	if ((tpacc = TCPaccess) == NULLACCESS)
		TCPaccess = holder;

	/* 20Jan2015, Maiko (VE4KLM), New 'insert' feature */

	else
	{
		if (index == 99999)		/* regular operation, NO INDEX SPECIFIED */
		{
			while (tpacc->nxtbits != NULLACCESS)
				tpacc = tpacc->nxtbits;

			tpacc->nxtbits = holder;
		}
		else if ((index == 0) | (index == 99998))	/* put at the very top */
		{
			holder->nxtbits = TCPaccess;
			TCPaccess = holder;

#ifdef TCPACCESS_EXPIRY
			/*
			 * 25Feb2015, Maiko, Problem with using index=0 for the blacklist
			 * is that manual insertion of a record via 'tcp access' command
			 * will also result in AGE being set, and expiry possibly kicking
			 * in, so I'm going to have to assign yet another special value for
		 	 * the blacklist functionality, such that index=0 is without age,
			 * so lets just pick 99998 - make sure to change in mailbox.c !
			 */
			if (index == 99998)
				holder->age = secclock (); /* 12Feb2015, Maiko, expiry */
#endif
		}
		else if (index > 0)
		{
			index--;	/* drop down one, makes code simple */

			while (index && tpacc->nxtbits != NULLACCESS)
			{
				tpacc = tpacc->nxtbits;
				index--;
			}

			holder->nxtbits = tpacc->nxtbits;
			tpacc->nxtbits = holder;
		}
#ifdef	DONT_COMPILE
		else if (index < 0)
		{
			// index++;	/* bump up one, makes code simple */

			/* count entire list, so we have an index value from the start */
			while (tpacc->nxtbits != NULLACCESS)
			{
				tpacc = tpacc->nxtbits;
				index++;
			}

			tpacc = TCPaccess;	/* important to reset for second pass */

			while (index && tpacc->nxtbits != NULLACCESS)
			{
				tpacc = tpacc->nxtbits;
				index--;
			}

			holder->nxtbits = tpacc->nxtbits;
			tpacc->nxtbits = holder;
		}
#endif		/* end of DONT_COMPILE */
	}
}

#endif

static int tcpirtt __ARGS((int argc,char *argv[],void *p));
static int dotcpirtt __ARGS((int argc,char *argv[],void *p));
static int tcpmss __ARGS((int argc,char *argv[],void *p));
static int dotcpmss __ARGS((int argc,char *argv[],void *p));
static int dortt __ARGS((int argc,char *argv[],void *p));
static int dotcpkick __ARGS((int argc,char *argv[],void *p));
static int dotcpreset __ARGS((int argc,char *argv[],void *p));
static int dotcpclean __ARGS((int argc,char *argv[],void *p));
static int dotcpretries __ARGS((int argc,char *argv[],void *p));
static int tcpretries __ARGS((int argc,char *argv[],void *p));
static int dotcpstat __ARGS((int argc,char *argv[],void *p));
static int dotcptimer __ARGS((int argc,char *argv[],void *p));
static int tcptimer __ARGS((int argc,char *argv[],void *p));
static int dotcptr __ARGS((int argc,char *argv[],void *p));
static int dotcpwindow __ARGS((int argc,char *argv[],void *p));
static int tcpwindow __ARGS((int argc,char *argv[],void *p));
static int dotcpmaxwait __ARGS((int argc,char *argv[],void *p));
static int tcpmaxwait __ARGS((int argc,char *argv[],void *p));
static int dotcpsyndata __ARGS((int argc,char *argv[],void *p));
static int tcpsyndata __ARGS((int argc,char *argv[],void *p));
int doview __ARGS((int argc,char *argv[],void *p));
static int dotcpblimit __ARGS((int argc,char *argv[],void *p));
static int tcpblimit __ARGS((int argc,char *argv[],void *p));
void rxtx_data_compute __ARGS((struct tcb *tcb,int32 *sent,int32 *recvd));

#ifdef TCPACCESS
struct rtaccess *TCPaccess = NULLACCESS; /* access list */
/* 386 compiles using BC++3.1 can't do 32-bit left shifts correctly, so: */
#define NETBITS(bits) ((bits) ? (~0 << (32 - (bits))) : 0)
#endif

extern int dotcpwatch (int, char**, void*);	/* 12Apr2014, Maiko (VE4KLM) */
  
/* TCP subcommand table */
static struct cmds DFAR Tcpcmds[] = {
#ifdef  TCPACCESS
    { "access",       doaccess,       0,      0, NULLCHAR },
#endif
    { "blimit",       dotcpblimit,    0, 0,   NULLCHAR },
    { "clean",        dotcpclean,     0, 0,   NULLCHAR },
    { "irtt",         dotcpirtt,      0, 0,   NULLCHAR },
    { "kick",         dotcpkick,      0, 2,   "tcp kick <tcb>" },
    { "maxwait",      dotcpmaxwait,   0, 0,   NULLCHAR },
    { "mss",          dotcpmss,       0, 0,   NULLCHAR },
    { "reset",        dotcpreset,     0, 2,   "tcp reset <tcb>" },
    { "retries",      dotcpretries,   0,0,    NULLCHAR },
    { "rtt",          dortt,          0, 3,   "tcp rtt <tcb> <val>" },
    { "status",       dotcpstat,      0, 0,   NULLCHAR },
    { "syndata",      dotcpsyndata,   0, 0,   NULLCHAR },
    { "timertype",    dotcptimer,     0, 0,   NULLCHAR },
    { "trace",        dotcptr,        0, 0,   NULLCHAR },
    { "view",         doview,         0, 0,   NULLCHAR },

    /* 12Apr2014, Maiko (VE4KLM), New tcp watchdog utility */
    { "watch",        dotcpwatch,     0, 0,   NULLCHAR },

    { "window",       dotcpwindow,    0, 0,   NULLCHAR },
    { NULLCHAR,		NULL,			0, 0,	NULLCHAR }
};
int
dotcp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Tcpcmds,argc,argv,p);
}
static int
dotcptr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Tcp_trace,"TCP state tracing",argc,argv);
}
  
/* Eliminate a TCP connection */
static int dotcpreset (int argc, char **argv, void *p)
{
    register struct tcb *tcb;

    /* 09Mar20121, Maiko (VE4KLM), replace htoi with htol, and better way anyways */
    tcb = (void*)htol (argv[1]);

    if (!tcpval (tcb))
    {
        j2tputs (Notval);
        return 1;
    }
    reset_tcp (tcb);
    return 0;
}
  
/* Reset lingering FIN Wait 2 state connections. (Selcuk) */
static int
dotcpclean(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct tcb *tcb, *tcb1;

    for(tcb=Tcbs;tcb != NULLTCB;tcb = tcb1){
        tcb1 = tcb->next;
        if(tcb->state == TCP_FINWAIT2)
           reset_tcp(tcb);
    }
    return 0;
}
  
/* Set initial round trip time for new connections */
static int tcpirtt(int argc, char **argv, void *p)
{
    return setint32 (p, "TCP default irtt", argc, argv);
}

/*
 * 01Apr2008, Maiko (VE4KLM), Simple tracking of the RTT cache
 * utilization (spread) in graphical form. Skip (K8RRA) and me
 * have been discussing RTT cache algorithms lately, and I do
 * admit, I am now curious. This might prompt me to play around
 * with different cache models, or even self adapting models,
 * to get the best use (spread) of the cache space.
 */
#define SHOW_CACHE_UTILIZATION

static int dotcpirtt (int argc, char **argv, void *p)
{
    struct tcp_rtt *tp;

#ifdef	SHOW_CACHE_UTILIZATION
	int used[RTTCACHE];
	int ucnt = 0;
#endif

    tcpirtt(argc,argv,(void *)&Tcp_irtt);

    if (argc < 2)
	{
        for(tp = &Tcp_rtt[0];tp < &Tcp_rtt[RTTCACHE];tp++,ucnt++)
		{
#ifdef	SHOW_CACHE_UTILIZATION
			used[ucnt] = tp->mods;
#endif
            if(tp->addr != 0)
			{
                if(tprintf("%s: srtt %d mdev %d\n",
                    inet_ntoa(tp->addr),
                    tp->srtt,tp->mdev) == EOF)
                    break;
            }
        }
#ifdef	SHOW_CACHE_UTILIZATION
		tprintf ("RTT cache utilization:\n");
		for (ucnt = 0; ucnt < RTTCACHE; ucnt++)
		{
			if (used[ucnt])
				tprintf ("[%d]", used[ucnt]);
			else
				tprintf ("_");
		}
		tprintf ("\n");
#endif
    }
    return 0;
}
  
/* Set smoothed round trip time for specified TCB */
static int dortt (int argc, char **argv, void *p)
{
    register struct tcb *tcb;

    /* 09Mar20121, Maiko (VE4KLM), replace htoi with htol, and better way anyways */
    tcb = (void*)htol (argv[1]);

    if (!tcpval(tcb))
    {
        j2tputs (Notval);
        return 1;
    }
    tcb->srtt = atoi (argv[2]);		/* alarm bell */
    return 0;
}
 
/* Force a retransmission */
static int dotcpkick (int argc, char **argv, void *p)
{
    register struct tcb *tcb;

    /* 09Mar20121, Maiko (VE4KLM), replace htoi with htol, and better way anyways */
    tcb = (void*)htol (argv[1]);

    if (kick_tcp (tcb) == -1)
    {
        j2tputs (Notval);
        return 1;
    }
    return 0;
}
  
/* Set default maximum segment size */
static int
tcpmss(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setshort((unsigned short *)p,"TCP MSS",argc,argv);
}
static int
dotcpmss(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return tcpmss(argc,argv,(void *)&Tcp_mss);
}
  
/* Set default window size */
static int
tcpwindow(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setshort((unsigned short *)p,"TCP window",argc,argv);
}
static int
dotcpwindow(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return tcpwindow(argc,argv,(void *)&Tcp_window);
}
  
static int
tcpsyndata(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool((int *)p,"TCP syn+data piggybacking",argc,argv);
}
static int
dotcpsyndata(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return tcpsyndata(argc,argv,(void *)&Tcp_syndata);
}
  
extern int Tcp_retries;
  
/* Set maximum number of backoffs before resetting the connection */
static int
tcpretries(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint((int *)p,"max. retries",argc,argv);
}
static int
dotcpretries(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return tcpretries(argc,argv,(void *)&Tcp_retries);
}
  
static int32 Tcp_maxwait;
  
/* Set maximum retry waittime in ms. */
static int tcpmaxwait (int argc, char **argv, void *p)
{
    return setint32 (p, "max. retry wait (ms)", argc, argv);
}

static int dotcpmaxwait (int argc, char *argv[], void *p)
{
    return tcpmaxwait(argc,argv,(void *)&Tcp_maxwait);
}
  
extern int Tcp_blimit;
  
/* Set backoff limit on the connection; from N1BEE */
static int
tcpblimit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint((int *)p,"backoff limit",argc,argv);
}
static int
dotcpblimit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return tcpblimit(argc,argv,(void *)&Tcp_blimit);
}
  
/* New tcp stat by Doug Crompton, wa3dsp */
static int tstat __ARGS((int flag));
  
/* Display status of TCBs */
static int dotcpstat (int argc, char **argv, void *p)
{
    struct tcb *tcb;
  
    if (argc < 2)
	{
        tstat(0);
    }
	else
	{
        if (toupper(argv[1][0])=='A')
		{
            tstat(1);
        }
		else
		{
    		/* 09Mar20121, Maiko (VE4KLM), replace htoi with htol, and better way anyways */
    		tcb = (void*)htol (argv[1]);

            if (!tcpval (tcb))
                j2tputs (Notval);
            else
                st_tcp (tcb);
        }
    }
    return 0;
}
  
/* Dump TCP stats and summary of all TCBs
 *  &TCB Rcv-Q Snd-Q  Local socket           Remote socket          State
 *  1234     0     0  xxx.xxx.xxx.xxx:xxxxx  xxx.xxx.xxx.xxx:xxxxx  Established
 */
static int tstat(int flag)
{
    int i;
    struct tcb *tcb;
    int j;
  
    for(j=i=1;i<=NUMTCPMIB;i++){
        if(Tcp_mib[i].name == NULLCHAR)
            continue;
        tprintf("(%2d)%-20s%10d",i,Tcp_mib[i].name,
        Tcp_mib[i].value.integer);
        if(j++ % 2)
            j2tputs("     ");
        else
            j2tputs("\n");
    }
    if((j % 2) == 0)
        j2tputs("\n");
  
#ifdef UNIX
    /* 09Mar2021, Maiko (VE4KLM), need to now accomodate a 12 wide TCB */
    j2tputs("&TCB         RcvQ SndQ  Local socket           Remote socket        State\n");
#else
    j2tputs("&TCB Rcv-Q Snd-Q  Local socket           Remote socket          State\n");
#endif
    for(tcb=Tcbs;tcb != NULLTCB;tcb = tcb->next){
        if(tcb->state == TCP_LISTEN && !flag)
            continue;
#ifdef UNIX
        /* 09Mar2021, Maiko (VE4KLM), TCB is now 12 wide */
        tprintf("%-12lx%5u%5u  ", FP_SEG (tcb), tcb->rcvcnt, tcb->sndcnt);
#else
        tprintf("%4.4x%6u%6u  ",FP_SEG(tcb),tcb->rcvcnt,tcb->sndcnt);
#endif
        tprintf("%-22s",pinet(&tcb->conn.local));
        tprintf("%-22s",pinet(&tcb->conn.remote));
        tprintf("%-s",Tcpstates[(int)tcb->state]);
        if(tcb->state == TCP_LISTEN && tcb->flags.clone)
            j2tputs(" (S)");
        if(j2tputs("\n") == EOF)
            return 0;
    }
    return 0;
}
  
/* Dump a TCP control block in detail */
void
st_tcp(tcb)
struct tcb *tcb;
{
    int32 sent,recvd;
  
    if(tcb == NULLTCB)
        return;
  
    rxtx_data_compute(tcb,&sent,&recvd);
  
    tprintf("Local: %s",pinet(&tcb->conn.local));
    tprintf(" Remote: %s",pinet(&tcb->conn.remote));
    tprintf(" State: %s\n",Tcpstates[(int)tcb->state]);
    j2tputs("      Init seq    Unack     Next Resent CWind Thrsh  Wind  MSS Queue      Total\n"
    "Send:");
    tprintf("%9x",tcb->iss);
    tprintf("%9x",tcb->snd.una);
    tprintf("%9x",tcb->snd.nxt);
    tprintf("%7d",tcb->resent);
    tprintf("%6d",(int)tcb->cwind);
    tprintf("%6d",(int)tcb->ssthresh);
    tprintf("%6d",(int)tcb->snd.wnd);
    tprintf("%5d",(int)tcb->mss);
    tprintf("%6d",(int)tcb->sndcnt);
    tprintf("%11d\n",sent);
  
    j2tputs("Recv:");
    tprintf("%9x",tcb->irs);
    j2tputs("         ");
    tprintf("%9x",(int)tcb->rcv.nxt);
    tprintf("%7d",tcb->rerecv);
    j2tputs("      ");
    j2tputs("      ");
    tprintf("%6d",(int)tcb->rcv.wnd);
    j2tputs("     ");
    tprintf("%6d",(int)tcb->rcvcnt);
    tprintf("%11d\n",recvd);
  
    if(tcb->reseq != (struct reseq *)NULL){
        register struct reseq *rp;
  
        j2tputs("Reassembly queue:\n");
        for(rp = tcb->reseq;rp != (struct reseq *)NULL; rp = rp->next){
            if(tprintf("  seq x%x %d bytes\n",
                rp->seg.seq,rp->length) == EOF)
                return;
        }
    }
    if(tcb->backoff > 0)
        tprintf("Backoff %u ",tcb->backoff);
    if(tcb->flags.retran)
        j2tputs("Retrying ");
    switch(tcb->timer.state){
        case TIMER_STOP:
            j2tputs("Timer stopped ");
            break;
        case TIMER_RUN:
            tprintf("Timer running (%d/%d ms) ",
            	read_timer(&tcb->timer), dur_timer(&tcb->timer));
            break;
        case TIMER_EXPIRE:
            j2tputs("Timer expired ");
    }
    tprintf("SRTT %d ms Mean dev %d ms\n",tcb->srtt,tcb->mdev);
}
  
void
rxtx_data_compute(tcb,sent,recvd)
struct tcb *tcb;
int32 *sent;
int32 *recvd;
{
  
    /* Compute total data sent and received; take out SYN and FIN */
    *sent = tcb->snd.una - tcb->iss;        /* Acknowledged data only */
    *recvd = tcb->rcv.nxt - tcb->irs;
    switch(tcb->state){
        case TCP_LISTEN:
        case TCP_SYN_SENT:      /* Nothing received or acked yet */
            *sent = *recvd = 0;
            break;
        case TCP_SYN_RECEIVED:
            (*recvd)--;     /* Got SYN, no data acked yet */
            *sent = 0;
            break;
        case TCP_ESTABLISHED:   /* Got and sent SYN */
        case TCP_FINWAIT1:      /* FIN not acked yet */
            (*sent)--;
            (*recvd)--;
            break;
        case TCP_FINWAIT2:      /* Our SYN and FIN both acked */
            *sent -= 2;
            (*recvd)--;
            break;
        case TCP_CLOSE_WAIT:    /* Got SYN and FIN, our FIN not yet acked */
        case TCP_CLOSING:
        case TCP_LAST_ACK:
            (*sent)--;
            *recvd -= 2;
            break;
        case TCP_TIME_WAIT:     /* Sent and received SYN/FIN, all acked */
            *sent -= 2;
            *recvd -= 2;
            break;
    }
}
  
/* TCP View Command - D. Crompton 1/92 */
/* Modified for sorted display and     */
/* two views - tcp view b|t - 3/92     */
int
doview(argc,argv,p)
int argc;
char *argv[];
void *p;
  
{
    register struct tcb *tcb;
    int32 sent,recvd;
    int i,j,k=0,vtype;
    int nosort=0;
    char *buf;
    char temp[80];
  
    if(argc == 1)
        vtype = 1;
    else {
        switch (argv[1][0]) {
            case 'b':  vtype=1;
                break;
            case 't':  vtype=0;
                break;
            default:   j2tputs("Use: tcp view <bytes|timers>\n");
                return 0;
        }
    }
  
    for(tcb=Tcbs,i=0;tcb != NULLTCB;tcb = tcb->next){
        if(tcb->state == TCP_LISTEN)
            continue;
        i++;
    }
  
    if (i)
	{
		/* 22Dec2005, Maiko, Changed malloc() to mallocw() instead */
        buf = mallocw ((unsigned)(i * 80));
  
        if (vtype) {
            j2tputs("                                                   Send  Send   Receive Receive\n" \
            "Remote Socket:Port:Local Port/State   &TCB         Bytes Retries  Bytes Retries\n");
        } else {
            j2tputs("Remote Socket:Port:Local Port/State   &TCB    Boff State       Timer      SRTT\n");
        }
        for(tcb=Tcbs,j=0;tcb != NULLTCB;tcb = tcb->next){
            if(tcb->state == TCP_LISTEN)
                continue;
  
            strcpy(temp,pinet(&tcb->conn.remote));
            strcat(temp,strstr(pinet(&tcb->conn.local),":"));
            strcat(temp,"/");
            strcat(temp,Tcpstates[(int)tcb->state]);
            temp[37]=0;
            k=sprintf(&buf[j],"%-37s",temp);
#ifdef UNIX
            /*
             * 09Mar2021, Maiko (VE4KLM), need space for 12, and use pointer format
             * (is this going to spill over to the next line ? keep an eye on it)
             */
            k += sprintf(&buf[j + k], "%12lx", FP_SEG (tcb));
#else
            sprintf(temp,"%8lx",ptol(tcb));
            temp[4]=0;
            k+=sprintf(&buf[j+k]," %4s   ",temp);
#endif
            if (vtype) {
                rxtx_data_compute(tcb,&sent,&recvd);
                k+=sprintf(&buf[j+k],"%10d",sent);
                k+=sprintf(&buf[j+k],"%7d",tcb->resent);
                k+=sprintf(&buf[j+k],"%10d",recvd);
                sprintf(&buf[j+k],"%7d",tcb->rerecv);
            } else {
                k+=sprintf(&buf[j+k]," %4u",tcb->backoff);
                if(tcb->flags.retran)
                    k+=sprintf(&buf[j+k]," Retry ");
                else
                    k+=sprintf(&buf[j+k],"  Try  ");
                switch(tcb->timer.state) {
                    case TIMER_STOP:
                        k+=sprintf(&buf[j+k],"      Stopped");
                        break;
                    case TIMER_RUN:
                        k+=sprintf(&buf[j+k]," Run (");
                        if (dur_timer(&tcb->timer)<10000) {
                            k+=sprintf(&buf[j+k],"%d/%d)ms",
                            	read_timer(&tcb->timer), dur_timer(&tcb->timer));
                        } else {
                            if ((read_timer(&tcb->timer)/1000)>9999) {
                                k+=sprintf(&buf[j+k],">9999/9999)s");
                            } else {
                                k+=sprintf(&buf[j+k],"%d/%d)s",
                                	read_timer(&tcb->timer)/1000,
										dur_timer(&tcb->timer)/1000);
                            }
                        }
                        break;
                    case TIMER_EXPIRE:
                        k+=sprintf(&buf[j+k],"      Expired");
                }
#ifdef UNIX
                if(k>73) k=73;
#endif
                for (;k<73;k++)
                    buf[j+k]=' ';
                if ((tcb->srtt)<10000) {
                    sprintf(&buf[j+73],"%4dms",tcb->srtt);
                } else {
                    if ((tcb->srtt/1000)>9999) {
                        sprintf(&buf[j+73],">9999s");
                    } else {
                        sprintf(&buf[j+73],"%4ds",tcb->srtt/1000);
                    }
                }
            }
            if (nosort) {
                tprintf("%s\n", buf);
            } else
                j+=80;
        }
  
        if (!nosort) {
#ifdef UNIX
			/* log (-1, "calling j2qsort, %d entries", j); */

            j2qsort (buf, (size_t)i, 80,
				(int (*) (const void*,const void*)) strcmp);
#else
            qsort(buf,(size_t)i,80,(int (*) ()) strcmp);
#endif
  
            for (j=0,k=0;j<i;j++,k+=80) {
                j2tputs(&buf[k]);
                if(tputc('\n') == EOF)
                    break;
            }
        }
        free(buf);
    }
    return 0;
}
  
/* tcp timers type - linear v exponential */
static int
tcptimer(argc,argv,p)
int argc ;
char *argv[] ;
void *p ;
{
    if (argc < 2) {
        tprintf("Tcp timer type is %s\n", *(int *)p ? "linear" : "exponential" ) ;
        return 0 ;
    }
  
    switch (argv[1][0]) {
        case 'l':
        case 'L':
            *(int *)p = 1 ;
            break ;
        case 'e':
        case 'E':
            *(int *)p = 0 ;
            break ;
        default:
            j2tputs("use: tcp timertype [linear|exponential]\n") ;
            return -1 ;
    }
    return 0 ;
}
  
extern int tcptimertype;
  
static int
dotcptimer(argc,argv,p)
int argc ;
char *argv[] ;
void *p ;
{
  
    return tcptimer(argc,argv,(void *)&tcptimertype);
}
  
#ifdef  TCPACCESS

/* 29Dec2004, Maiko, Replaces GOTO 'usage_msg:' label */
/* 20Jan2015, Maiko (VE4KLM), Introduce line numbers to allow inserts */
static int do_usage_msg ()
{
	j2tputs(" Format: tcp access <permit|deny|delete> [L#] <dest addr>[/<bits>] [lowport [highport]]\n\n");
	return 1;
}

/*
 * 13Feb2015, Maiko (VE4KLM), Decided to make it so that 'tcp access' now has
 * its own subcommand list, since I want to be able to add features like expiry
 * of access entries. So I have cut out code from the original doaccess() func,
 * and pasted into some newer and smaller support functions. Doing it now will
 * make it easier to add features down the road (hopefully for others too).
 */

static int doaccesslist (int argc, char **argv, void *p)
{
	struct rtaccess *tpacc;

	char *cp;

	j2tputs("IP Address      Mask  Low Port High Port State  Age\n");

	for(tpacc = TCPaccess;tpacc != NULLACCESS;tpacc = tpacc->nxtbits)
	{
		if(tpacc->target != 0)
			cp = inet_ntoa(tpacc->target);
		else
			cp = "all";

		tprintf("%-16s",cp);
		tprintf("%4u ",tpacc->bits);
		tprintf("%9u",tpacc->lowport);
		tprintf("%10u ",tpacc->highport);

		if(tpacc->status)
			cp = "deny";
		else
			cp = "permit";

		tprintf("%-6s", cp);

		if (tpacc->age != -1)
			tprintf (" %d", secclock () - tpacc->age);

		tprintf ("\n");
	}

	return 0;
}

/* 13Feb2015, Maiko (VE4KLM), This used to be doaccess(), now a support func */

static int doaccessfunc (int argc, char **argv, void *p)
{
    int32 target;
    unsigned bits;
    char *bitp;
    int16 lport,hport,state;
    struct rtaccess *tpacc;
    struct rtaccess *head;
    struct rtaccess *prev;

	/* 20Jan2015, Maiko (VE4KLM), New insert feature / toggle orig syntax */
	int shiftargs = 0, offset = 99999;

	//log (-1, "argc %d argv[0] %s", argc, argv[0]);

	if(strcmp(argv[0],"permit") == 0)
        state = 0;

    else
	{
        if ((strcmp(argv[0],"deny") == 0) ||
            (strcmp(argv[0],"delete") == 0))
		{
            state = -1;
        }
		else return (do_usage_msg ());
    }

    if (argc < 2)
		return (do_usage_msg ());

	bitp = argv[1];

	/*
	 * 13Feb2015, Maiko (VE4KLM), new feature to allow insert of 'rule'
	 * 25Feb2015, Maiko (VE4KLM), rewrote this, better syntax checking
	 */
	if (*bitp++ == 'L')
	{
		/* The 'L' argument MUST have an argument (no space) !!! */
		if (!(*bitp))
			return (do_usage_msg ());

		offset = atoi (bitp);
		log (-1, "offset %d into tcp access list", offset);
		shiftargs = 1;
	}

#ifdef	DONT_COMPILE
	/* 13Feb2015, Maiko (VE4KLM), end of day, this is a bit 'lazy' perhaps ? */
	if (!memcmp (argv[1], "L", 1))
	{
		offset = atoi (argv[1]+1);

		log (-1, "table offset %d", offset);

		shiftargs = 1;
	}
#endif

    if(strcmp(argv[1+shiftargs],"all") == 0){
        target = 0;
        bits = 0;
    } else {
        /* If IP address is followed by an optional slash and
         * a length field, (e.g., 128.96/16) get it;
         * otherwise assume a full 32-bit address
         */
        if((bitp = strchr(argv[1+shiftargs],'/')) != NULLCHAR){
            /* Terminate address token for resolve() call */
            *bitp++ = '\0';
            bits = atoi(bitp);
        } else
            bits = 32;
  
        if((target = resolve(argv[1+shiftargs])) == 0){
            tprintf(Badhost,argv[1+shiftargs]);
            return 1;
        }
    }
  
    if(argc > (2+shiftargs)){
        if(strcmp(argv[2+shiftargs],"all") == 0){
            lport = 1;
            hport = 65534U;
        } else {
            lport = atoi(argv[2+shiftargs]);
            hport = lport;
        }
    } else {
        lport = 0;
        hport = 0;
    }
    if(argc > (3+shiftargs))
        hport = atoi(argv[3+shiftargs]);
  
    if(strcmp(argv[shiftargs],"delete") == 0){
        prev = NULLACCESS;
        head = tpacc = TCPaccess;
        while(tpacc != NULLACCESS){
            head = tpacc;
            tpacc = tpacc->nxtbits;
            if((head->target == target) &&
                (head->bits == bits)     &&
                (head->lowport == lport) &&
            (head->highport == hport)) { /*match*/
  
  
                /*now delete. watch for special cases*/
                if(head == TCPaccess) /* first in chain */
                    TCPaccess = head->nxtbits;
                else
                    /*
                      sanity check: we cant get here with
                      prev == NULLACCESS !!
                     */
                    prev->nxtbits = tpacc;
                free(head);
                return 0;
            }
            prev = head;
        }
        j2tputs("Not found.\n");
        return 1;
    }
    /* add the access */
    addtaccess(target,bits,lport,hport,state, offset);
    return 0;
}

/* 13Feb2015, Maiko (VE4KLM), Create new subcmd hierarchy for TCP access */
static struct cmds DFAR AccessCmds[] = {
    { "list",	doaccesslist, 0, 0, NULLCHAR },
    { "permit",	doaccessfunc, 0, 0, NULLCHAR },
    { "deny",	doaccessfunc, 0, 0, NULLCHAR },
    { "delete",	doaccessfunc, 0, 0, NULLCHAR },
#ifdef TCPACCESS_EXPIRY
    { "expiry",	doaccessexpiry, 0, 0, NULLCHAR },
#endif
    { NULLCHAR, NULL, 0, 0, NULLCHAR }
};

int doaccess (int argc, char **argv, void *p)
{
    return subcmd (AccessCmds, argc, argv, p);
}

/* check to see if port is "authorized".  Returns 0 if matching permit record
   is found or no access records exists, -1 if not found or deny record found */
int
tcp_check(accptr,src,port)
struct rtaccess *accptr;
int32 src;
int16 port;
{
    unsigned int mask;
  
    if(accptr == NULLACCESS)
        return 0;               /* no access control */
    for(;accptr != NULLACCESS;accptr = accptr->nxtbits) {
        mask = NETBITS(accptr->bits);
        if(( accptr->target == ((int32)mask & src)) &&
            ((( port >= accptr->lowport ) && (port <= accptr->highport))
        || (!accptr->lowport))){
            return (accptr->status);
        }
    }
    return -1; /* fall through to here if not found */
}
#endif
  
/* These are the interface dependent tcp parameters */
static int doiftcpblimit __ARGS((int argc,char *argv[],void *p));
static int doiftcpirtt __ARGS((int argc,char *argv[],void *p));
static int doiftcpmaxwait __ARGS((int argc,char *argv[],void *p));
static int doiftcpretries __ARGS((int argc,char *argv[],void *p));
static int doiftcptimertype __ARGS((int argc,char *argv[],void *p));
static int doiftcpwindow __ARGS((int argc,char *argv[],void *p));
static int doiftcpsyndata __ARGS((int argc,char *argv[],void *p));
static int doiftcpmss __ARGS((int argc,char *argv[],void *p));
  
struct cmds DFAR Iftcpcmds[] = {
    { "blimit",   doiftcpblimit,  0, 0, NULLCHAR },
    { "irtt",     doiftcpirtt,    0, 0, NULLCHAR },
    { "maxwait",  doiftcpmaxwait, 0, 0, NULLCHAR },
    { "mss",      doiftcpmss,     0, 0, NULLCHAR },
    { "retries",  doiftcpretries, 0, 0, NULLCHAR },
    { "syndata",  doiftcpsyndata, 0, 0, NULLCHAR },
    { "timertype",doiftcptimertype,0,0, NULLCHAR },
    { "window",   doiftcpwindow,  0, 0, NULLCHAR },
    { NULLCHAR,	NULL,			0, 0, NULLCHAR }
};
  
int doiftcp(int argc, char *argv[],void *p) {
    if(!p)
        return 0;
    return subcmd(Iftcpcmds,argc,argv,p);
}
  
int doiftcpblimit(int argc,char *argv[],void *p) {
    struct iface *ifp = p;
  
    return tcpblimit(argc,argv,(void *)&ifp->tcp->blimit);
}
  
int doiftcpirtt(int argc,char *argv[],void *p) {
    struct iface *ifp = p;
  
    return tcpirtt(argc,argv,(void *)&ifp->tcp->irtt);
}
  
int doiftcpmaxwait(int argc,char *argv[],void *p) {
    struct iface *ifp = p;
  
    return tcpmaxwait(argc,argv,(void *)&ifp->tcp->maxwait);
}
  
int doiftcpmss(int argc,char *argv[],void *p) {
    struct iface *ifp = p;
  
    return tcpmss(argc,argv,(void *)&ifp->tcp->mss);
}
  
int doiftcpretries(int argc,char *argv[],void *p) {
    struct iface *ifp = p;
  
    return tcpretries(argc,argv,(void *)&ifp->tcp->retries);
}
  
int doiftcptimertype(int argc,char *argv[],void *p) {
    struct iface *ifp = p;
  
    return tcptimer(argc,argv,(void *)&ifp->tcp->timertype);
}
  
int doiftcpwindow(int argc,char *argv[],void *p) {
    struct iface *ifp = p;
  
    return tcpwindow(argc,argv,(void *)&ifp->tcp->window);
}
  
int doiftcpsyndata(int argc,char *argv[],void *p) {
    struct iface *ifp = p;
  
    return tcpsyndata(argc,argv,(void *)&ifp->tcp->syndata);
}
  
void init_iftcp(struct iftcp *tcp) {
    tcp->blimit = Tcp_blimit;
    tcp->maxwait = Tcp_maxwait;
    tcp->window = Tcp_window;
    tcp->mss = Tcp_mss;
    tcp->irtt = Tcp_irtt;
    tcp->retries = Tcp_retries;
    tcp->timertype = tcptimertype;
    tcp->syndata = Tcp_syndata;
}
  
