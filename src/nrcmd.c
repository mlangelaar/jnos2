/*
 * JNOS 2.0
 *
 * $Id: nrcmd.c,v 1.1 2015/04/22 01:51:45 root Exp $
 *
 * net/rom user command processing
 * Copyright 1989 by Daniel M. Frank, W9NK.  Permission granted for
 * non-commercial distribution only.
 *
 * Mods by G1EMM, PA0GRI, WG7J and VE4KLM
 */
#include <ctype.h>
#include <time.h>
#ifdef MSDOS
#include <dos.h>
#endif
#include "global.h"
#ifdef NETROM
#include "mbuf.h"
#include "proc.h"
#include "ax25.h"
#include "mailbox.h"
#include "netrom.h"
#include "nr4.h"
#include "timer.h"
#include "iface.h"
#include "pktdrvr.h"
#include "lapb.h"
#include "cmdparse.h"
#include "session.h"
#include "socket.h"
#include "commands.h"
#include "files.h"

#include "jlocks.h"	/* 06Feb2006, Maiko, New locking */

/* 07Aug2018, Maiko, prototype (should use string.h with GNU_SOURCE, but */ 
extern char *strcasestr (const char*, const char*);

int G8bpq;
int Nr_hidden = 1;

#ifdef	INP2011
/* 13Sep2011, Maiko (VE4KLM), don't display routeless nodes by default */
int Nr_routeless = 0;
#endif

/* 04Oct2011, Maiko, Time to move to a new, more readable node display */
#ifdef	INP2011
unsigned Nr_sorttype = 2;
#else
unsigned Nr_sorttype = 1;
#endif

char Nr4user[AXALEN];
  
char *Nr4states[] = {
    "Disconnected",
    "Conn Pending",
    "Connected",
    "Disc Pending",
    "Listening"
} ;
  
char *Nr4reasons[] = {
    "Normal",
    "By Peer",
    "Timeout",
    "Reset",
    "Refused"
} ;
static int dobcnodes __ARGS((int argc,char *argv[],void *p));
static int dobcpoll __ARGS((int argc,char *argv[],void *p));
static int dointerface __ARGS((int argc,char *argv[],void *p));
static int donfadd __ARGS((int argc,char *argv[],void *p));
static int donfdrop __ARGS((int argc,char *argv[],void *p));
static int donfdump __ARGS((void));
static int donfmode __ARGS((int argc,char *argv[],void *p));
static int donodefilter __ARGS((int argc,char *argv[],void *p));
static int donodetimer __ARGS((int argc,char *argv[],void *p));
extern int donralias __ARGS((int argc,char *argv[],void *p));
static int donracktime __ARGS((int argc,char *argv[],void *p));
static int donrmycall __ARGS((int argc,char *argv[],void *p));
static int donrchoketime __ARGS((int argc,char *argv[],void *p));
static int donrconnect __ARGS((int argc,char *argv[],void *p));
static int donrirtt __ARGS((int argc,char *argv[],void *p));
static int donrkick __ARGS((int argc,char *argv[],void *p));
static int dorouteadd __ARGS((int argc,char *argv[],void *p));
static int doroutedrop __ARGS((int argc,char *argv[],void *p));
static int donrqlimit __ARGS((int argc,char *argv[],void *p));
static int donrreset __ARGS((int argc,char *argv[],void *p));
static int donrretries __ARGS((int argc,char *argv[],void *p));
static int donrroute __ARGS((int argc,char *argv[],void *p));
int donrstatus __ARGS((int argc,char *argv[],void *p));
static int donrsave __ARGS((int argc,char *argv[],void *p));
static int donrload __ARGS((int argc,char *argv[],void *p));
static int donrttl __ARGS((int argc,char *argv[],void *p));
static int donruser __ARGS((int argc,char *argv[],void *p));
static int donrwindow __ARGS((int argc,char *argv[],void *p));
void doobsotick __ARGS((void));
static int doobsotimer __ARGS((int argc,char *argv[],void *p));
static int dominquality __ARGS((int argc,char *argv[],void *p));
static int donrtype __ARGS((int argc,char *argv[],void *p));
static int donrpromisc __ARGS((int argc,char *argv[],void *p));
static int donrderate __ARGS((int argc,char *argv[],void *p));
static int doroutesort __ARGS((int argc,char *argv[],void *p));
static int donrhidden __ARGS((int argc,char *argv[],void *p));
static int donrg8bpq __ARGS((int argc,char *argv[],void *p));
static void doallinfo __ARGS((void));
static struct iface *FindNrIface __ARGS((char *name));
  
int donrneighbour __ARGS((int argc,char *argv[],void *p));
  
extern int donr4tdisc __ARGS((int argc,char *argv[],void *p));
extern struct nr_bind *find_best __ARGS((struct nr_bind *list,unsigned obso));
extern struct nr_bind *find_bind __ARGS((struct nr_bind *list,struct nrnbr_tab *np));
extern void nrresetlinks(struct nrroute_tab *rp);

#if defined (NETROM) && defined (INP3)
extern int donrr (int, char**, void*);
extern int dol3rtt (int, char**, void*);
#endif

/* 04Oct2005, Maiko, New NETROM and INP3 debugging flag */
static int donrdebug (int argc, char **argv, void *p)
{
    extern int Nr_debug;
  
    return setbool (&Nr_debug, "netrom and inp3 debugging flag", argc, argv);
}

/*
 * 02Sep2020, Maiko (VE4KLM), Obsolescence count and not to broadcast them
 * after a minimum, have always been hardcoded. I recently noticed this after
 * seeing the parameters had been changed to meet NEDA specs for a particular
 * installation of JNOS. The problem with this approach is these changes are
 * vunerable to software upgrades, which would wipe them out, unless someone
 * is on top of it during each software update.
 *
 * A better approach I believe is to provide the ability to modify these two
 * parameters from a command line, like is done for other netrom parameters,
 * so with that in mind, I give you two new netrom command as of 2.0m.5 :
 *
 *   netrom obsoinit <value>            default is 6, for NEDA use 5
 *   netrom obsominbc <value>           default is 5, for NEDA use 3
 *
 * the NEDA values came from Brian (N1URO) w.r.t. his URONODE cfgs
 */

extern unsigned Obso_init, Obso_minbc;	/* defined in nr3.c */

extern int setuns (unsigned*, char*, int, char *[]);	/* defined in cmdparse.c */

static int donrobsoinit (int argc, char **argv, void *p)
{
    return setuns (&Obso_init, "Initial Value for Obsolescence", argc, argv);
}

static int donrobsominbc (int argc, char **argv, void *p)
{
    return setuns (&Obso_minbc, "Minimum Obsolescence to Broadcast", argc, argv);
}

static struct cmds DFAR Nrcmds[] = {
    { "acktime",      donracktime,    0, 0,   NULLCHAR },
    { "alias",    donralias,  0, 0,   NULLCHAR },
    { "bcnodes",  dobcnodes,  0, 2,   "netrom bcnodes <iface>" },
    { "bcpoll",   dobcpoll,   0, 2,   "netrom bcpoll <iface>" },
#ifdef NETROMSESSION
    { "connect",  donrconnect, 1024, 2,   "netrom connect <node>" },
#endif
    { "call",     donrmycall,   0, 0,   NULLCHAR },
    { "choketime",    donrchoketime,  0, 0,   NULLCHAR },
/* 04Oct2005, Maiko, New netrom debugging flag */
	{ "debug",		donrdebug,		0, 0,	NULLCHAR },
    { "derate",       donrderate,     0, 0,   NULLCHAR },
#ifdef G8BPQ
    { "g8bpq",        donrg8bpq,      0, 0,   NULLCHAR },
#endif
    { "hidden",   donrhidden, 0, 0,   NULLCHAR },
    { "interface",    dointerface,    0, 0, NULLCHAR },
    { "irtt",         donrirtt,       0, 0,   NULLCHAR },
    { "kick",         donrkick,       0, 2,   "netrom kick <&nrcb>" },
    { "load",         donrload,       0, 0,   NULLCHAR },
#ifdef INP3
	{ "l3rtt",	dol3rtt,	0,	3, "netrom l3rtt <neighbour> <iface>" },
#endif
    { "minquality",   dominquality,   0, 0,   NULLCHAR },
    { "neighbour",    donrneighbour,  0, 0,   NULLCHAR },
    { "nodefilter",   donodefilter,   0, 0,   NULLCHAR },
    { "nodetimer",    donodetimer,    0, 0,   NULLCHAR },
#ifdef INP3
	{ "nrr",			donrr,			0,	2,	"netrom nrr <remote call>" },
#endif
    { "obsotimer",    doobsotimer,    0, 0,   NULLCHAR },
/*
 * 02Sep2020, Maiko (VE4KLM), Sysop can now change these two obsolescence
 * parameters at their command prompt and/or in the 'autoexec.nos' file.
 */
    { "obsoinit",    donrobsoinit,    0, 0,   NULLCHAR },
    { "obsominbc",    donrobsominbc,    0, 0,   NULLCHAR },

    { "promiscuous",  donrpromisc,    0, 0,   NULLCHAR },
    { "qlimit",       donrqlimit,     0, 0,   NULLCHAR },
    { "route",    donrroute,  0, 0,   NULLCHAR },
    { "reset",        donrreset,      0, 2,   "netrom reset <&nrcb>" },
    { "retries",      donrretries,    0, 0,   NULLCHAR },
    { "status",       donrstatus,     0, 0,   NULLCHAR },
    { "save",         donrsave,       0, 0,   NULLCHAR },
#if defined(NETROMSESSION) && defined(SPLITSCREEN)
    { "split",    donrconnect, 1024, 2,   "netrom split <node>" },
#endif
    { "timertype",    donrtype,   0, 0,   NULLCHAR },
    { "ttl",          donrttl,        0, 0,   NULLCHAR },
#ifdef NR4TDISC
    { "tdisc",    donr4tdisc, 0, 0,   NULLCHAR },
#endif
    { "user",     donruser,   0, 0,   NULLCHAR },
    { "window",       donrwindow,     0, 0,   NULLCHAR },
    { NULLCHAR,	NULL,	0, 0, NULLCHAR }
} ;
  
  
struct timer Nodetimer ; /* timer for nodes broadcasts */
struct timer Obsotimer ; /* timer for aging routes */
  
/* Command multiplexer */
int
donetrom(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    return subcmd(Nrcmds,argc,argv,p) ;
}

/*
 * 07Aug2018, Maiko, Aggggg, stub function just to call doroutedump
 */
int INTERNALroutedump (int argc, char **argv, void *p)
{
	if (argc < 2)
		return doroutedump(NULL);

	return doroutedump (argv[1]);
}

static struct cmds Routecmds[] = {
    { "add",  dorouteadd,     0, 6,
    "netrom route add <alias> <destination> <interface> <quality> <neighbor>" },
    { "drop", doroutedrop, 0, 4,
    "netrom route drop <destination> <neighbor> <interface>" },
    { "info", dorouteinfo, 0, 0, "" },
	/* 07Aug2018, Maiko (VE4KLM), new filter (substring) for nodes command */
    { "nodes", INTERNALroutedump, 0, 1,
	"netrom route nodes <optional substring>" },
    { "sort", doroutesort, 0, 1, "" },
    { NULLCHAR, NULL, 0, 0,
	"" }
};
  
/* Route command multiplexer */
static int
donrroute(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
	/* 07Aug2018, Maiko (VE4KLM), doroutedump now part of Routecmds stub */

    return subcmd(Routecmds,argc,argv,p) ;
}
  
/* Code to sort Netrom node listing
 * D. Crompton  2/92
 * Dump a list of known netrom routes in
 * sorted order determined by sort
 * flag - default = sort by alias
 */
 
#ifdef	INP2011
#define RTSIZE 27
#else 
#define RTSIZE 17
#endif

/* 07Aug2018, Maiko (VE4KLM), Added filter parameter, just a basic string
 * within a string case insensitive for now, got tired of seeing several
 * hundred flash by and not seeing the one I wanted :)
 */

int doroutedump (char *searchforpattern)
{
    register struct nrroute_tab *rp;
    register int i, j, k, column;
    char *temp;
#ifdef INP2011
	int nrcnt = 0;	/* 30Sep2011, Maiko, just count routeless nodes now */
#endif

	/* 07Aug2018, Maiko, this is where these variables should be defined !!! */
	char *ptr, tcall[AXBUF], talias[AXALEN];
  
    column = 1 ;

	jnos_lock (JLOCK_NR);

    for (i = 0, j = 0; i < NRNUMCHAINS; i++)
        for (rp = Nrroute_tab[i]; rp != NULLNRRTAB; j++, rp = rp->next);
  
    if (j)
	{
        /* Allocate maximum size */
		/* 22Dec2005, Maiko, Changed malloc() to mallocw() intead ! */
        if ((temp = mallocw ((unsigned)j * RTSIZE)) == NULLCHAR)
		{
            j2tputs (Nospace);
			jnos_unlock (JLOCK_NR);
            return -1;
        }
  
        for (i = 0, j = 0, k = 0 ; i < NRNUMCHAINS ; i++)
		{
            for (rp = Nrroute_tab[i]; rp != NULLNRRTAB; rp = rp->next)
			{

#ifdef INP2011
				/*
				 * 13Sep2011, Maiko, Now that it is very possible to have
				 * node entries that have no routes (see newinp.c code), it
				 * may be an idea to be able to suppress display of these.
				 */
                if (!Nr_routeless && rp->routes == NULLNRBIND)
				{
					/*
					 * 30Sep2011, Maiko, Just count, don't display anymore
					 *
					if (Nr_debug)
					{
						log (-1, "node [%s] has no routes",
							pax25 (tmp, rp->call));
					}
					 */

					nrcnt++;

                    continue;
				}
#endif
                if (!Nr_hidden && *rp->alias == '#')
                    continue;
		/*
		 * 04Oct2011, Maiko, I like how Xnet displays the node table,
		 * the screen formatting makes it a bit easier to find stuff.
		 * To cut down on duplicate code, I am just going to redo the
		 * existing code for the original 2 sort options, and add in
		 * the new xnet style display if INP2011 is defined.
		 */
				pax25 (tcall, rp->call);

				strcpy (talias, rp->alias);
			/*
			 * 07Aug2018, Maiko (VE4KLM), I've been wanting to put in a filter
			 * feature for years now, finally got tired of entering the 'nodes'
			 * command on my console only to watch several hundred stream by,
			 * so now I can add a filter parameter (simple pattern match).
			 */
				if ((searchforpattern == NULL) ||
					(strcasestr (tcall, searchforpattern)) ||
					(strcasestr (talias, searchforpattern)))
				{

					if ((ptr = strchr (talias, ' ')) != NULLCHAR)
						*ptr = 0;

					ptr = &temp[k];
#ifdef	INP2011
					if (Nr_sorttype == 2)
						sprintf (ptr, "%-9.9s:%6.6s:%s", tcall, talias, tcall);
					else
#endif
					if (Nr_sorttype)
					{
						/* old version - don't include colon for null alais */
						if (*talias)
						{
							strcpy (ptr, talias);
							strcat (ptr, ":");
						}
						else *ptr = 0;

						strcat (ptr, tcall);
					}
					else
					{
						strcpy (ptr, tcall);
						strcat (ptr, ":");
						strcat (ptr, talias);
					}

                	k += RTSIZE;

                	j++;    /* number actually shown */
				}
            }
        }

#ifdef INP2011
		if (Nr_debug)
			log (-1, "counted [%d] routeless nodes", nrcnt);
#endif

		/* make sure we unlock BEFORE going to stdout !!! */
		jnos_unlock (JLOCK_NR);

#ifdef UNIX

		/* log (-1, "calling j2qsort, %d entries", j); */

        j2qsort (temp, (size_t)j, RTSIZE,
			(int (*)(const void*,const void*)) strcmp);
#else
        qsort (temp, (size_t)j, RTSIZE, (int (*) ()) strcmp);
#endif
        for (i = 0, k = 0; i < j; i++, k += RTSIZE)
		{
#ifdef INP2011
            if (Nr_sorttype == 2)
               tprintf ("%-16s  ", &temp[k+10]);
			else
#endif
               tprintf ("%-16s  ", &temp[k]);

            if (column++ == 4)
			{
                if (tputc ('\n') == EOF)
				{
                    free (temp);
                    return 0;
                }
                column = 1 ;

            }
        }
  
        if (column != 1)
            tputc ('\n') ;

		tprintf ("*** %d nodes displayed\n", j);

        free (temp);
    }
	else jnos_unlock (JLOCK_NR);

    return 0 ;
}
  
/* netrom Route Dump Sort - ALIAS or CALL first */
/* 04Oct2011, Maiko, Added 'modern' option to give an Xnet style listing */
static int doroutesort (int argc, char **argv, void *p)
{
	static char *ststr[3] = {
		"Call",
		"Alias",
		"Modern"
	};

    if (argc < 2)
	{
        tprintf ("Netrom Sort by %s\n", ststr[Nr_sorttype]);
        return 0 ;
    }
  
    switch (argv[1][0])
	{
#ifdef	INP2011
        case 'M':
        case 'm':
            Nr_sorttype = 2 ;
            break ;
#endif
        case 'A':
        case 'a':
            Nr_sorttype = 1 ;
            break ;
        case 'C':
        case 'c':
            Nr_sorttype = 0 ;
            break ;
        default:
#ifdef	INP2011
            j2tputs ("usage: netrom sort [alias|call|modern]\n");
#else
            j2tputs ("usage: netrom sort [alias|call]\n");
#endif
            return -1 ;
    }
  
    return 0 ;
}
  
/* Print detailed information on  ALL routes  (sorted) */
/*  D. Crompton */
static void
doallinfo()
{
    register struct nrroute_tab *rp ;
    register struct nr_bind *bp ;
    register struct nrnbr_tab *np ;
    char neighbor[AXBUF] ;
    char buf[17];
    char *cp,*temp;
    int i,j,k, flow_tmp;

	int actual_sorttype = Nr_sorttype;

#ifdef INP2011
	/*
	 * 05Oct2011, Maiko, Treat new 'Modern' format as 'Callsign', there is
	 * no point changing the sysop side of it (for now). I rarely (if ever)
	 * use this command at the JNOS console, but that's just me :)
	 */

	if (Nr_sorttype == 2)
		actual_sorttype = 0;
#endif
  
    flow_tmp=Current->flowmode;
    Current->flowmode=1;
  
    for (i=0,j=0;i<NRNUMCHAINS;i++)
        for (rp=Nrroute_tab[i];rp!=NULLNRRTAB;rp= rp->next)
            for(bp = rp->routes ; bp != NULLNRBIND ; bp = bp->next,j++);
  
#define STRSIZE 72
  
    if (j)
	{
		/* 22Dec2005, Maiko, Changed malloc() to mallocw() intead ! */
        if ((temp = mallocw((unsigned)j*STRSIZE)) == NULLCHAR)
		{
            j2tputs(Nospace);
            return;
        }
  
        for (i=0,k=0;i<NRNUMCHAINS;i++)
            for (rp=Nrroute_tab[i];rp!=NULLNRRTAB;rp= rp->next)
                for(bp = rp->routes ; bp != NULLNRBIND ; bp = bp->next,k+=STRSIZE) {
                    np = bp->via ;
                    if (actual_sorttype) {
                        strcpy(buf,rp->alias) ;
                        if((cp = strchr(buf,' ')) == NULLCHAR)
                            cp = &buf[strlen(buf)] ;
                        if(cp != buf)
                            *cp++ = ':' ;
                        pax25(cp,rp->call) ;
                    } else {
                        pax25(buf,rp->call);
                        cp=&buf[strlen(buf)];
                        *cp++=':';
                        strcpy(cp,rp->alias);
                    }
                    /* NOTE:
                     * IF you change the size of this sprintf buffer,
                     * ALSO change the size of the STRSIZE define above !!!
                     */
                    sprintf(&temp[k],"%-16s %s %3d %3d %-8s %-9s %c %-5u",buf,
                    rp->flags & G8BPQ_NODEMASK ? "(BPQ)" : "     ",
                    bp->quality,bp->obsocnt,
                    np->iface->name,
                    pax25(neighbor,np->call),
                    (bp->flags & NRB_PERMANENT ? 'P' :
                    bp->flags & NRB_RECORDED ? 'R' : 'B'),bp->usage);
                    if(rp->flags & G8BPQ_NODETTL) {
                        sprintf(buf,"    %u\n",rp->hops);
                        strcat(&temp[k],buf);
                    } else
                        strcat(&temp[k],"\n");
                }
#ifdef UNIX
        j2qsort(temp,(size_t)j,STRSIZE,(int (*)(const void*,const void*)) strcmp);
#else
        qsort(temp,(size_t)j,STRSIZE,(int (*) ()) strcmp);
#endif
        tprintf("%-16s      Qual Obs Port    Neighbor Type Usage Hops\n",
            (actual_sorttype?"Alias:Node":"Node:Alias"));
        for(i=0,k=0;i<j;i++,k+=STRSIZE)
            if (j2tputs(&temp[k])==EOF)
                break;
        free(temp);
        Current->flowmode=flow_tmp;
    }
}
  
/* Find the interface, and check if it is active for netrom */
static struct iface *FindNrIface(char *name) {
    struct iface *ifp;
  
    if((ifp = if_lookup(name)) == NULLIF) {
        tprintf(Badinterface,name);
        return NULLIF;
    }
    if(!(ifp->flags & IS_NR_IFACE)) {
        tprintf("%s is not active for netrom\n",name);
        return NULLIF;
    }
    return ifp;
}
  
  
/* print detailed information on an individual route
 * Shows alias as well - WG7J
 */
int
dorouteinfo(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    char *cp;
    register struct nrroute_tab *rp ;
    register struct nrroute_tab *npp;
    struct nr_bind *bp ;
    struct nrnbr_tab *np ;
    char destbuf[AXBUF];
    char neighbor[AXBUF] ;
    char alias[AXALEN];
    char buf[AXALEN];
    char nb_alias[AXALEN];
    int print_header=1;
    int16 rhash;
  
    if(argc == 1) {
        doallinfo();
        return 0;
    }
  
    putalias(alias,argv[1],0);
    strupr(argv[1]);    /*make sure it's upper case*/
    if((rp = find_nrboth(alias,argv[1])) == NULLNRRTAB){
        /*no such call or node alias*/
        j2tputs("no such node\n\n");
        return 0;
    }
    /*copy the real alias*/
    strcpy(buf,rp->alias) ;
    if((cp = strchr(buf,' ')) != NULLCHAR)
        *cp = '\0';

#ifdef INP2011 
	/* 13Sep2011, Maiko, We can now have *routeless* node entries */
	if (rp->routes == NULLNRBIND)
	{
		j2tputs ("no route(s)\n\n");
		return 0;
	}
#endif

    for(bp = rp->routes ; bp != NULLNRBIND ; bp = bp->next) {
        np = bp->via ;
        /* now we have to find the alias of the neighbour used
         * so we can print that as well!
         */
        rhash = nrhash(np->call);
        for(npp=Nrroute_tab[rhash];npp!=NULLNRRTAB;npp=npp->next)
            if(addreq(npp->call,np->call))
                break;
        if (npp==NULLNRRTAB)  /* K5JB */
            strcpy(nb_alias,"##gone");
        else {     /* found; remove trailing spaces */
            strcpy(nb_alias,npp->alias) ;
            if((cp = strchr(nb_alias,' ')) != NULLCHAR)
                *cp = '\0';
        }
        if(print_header) {
            print_header = 0;
            j2tputs("      Node     "
#ifdef G8BPQ
            "      "
#endif
            "      Neighbour"
/* 26Aug2011, Maiko, INP only applies to neighbour */
#if defined (G8BPQ) || defined(INP2011)
            "      "
#endif
	/* 26Aug2011, Maiko, Remove types group it with Obs to save space */
            "    Port  Qual Obs Usage"

/* 26Aug2011, Maiko, Don't think BPQ and INP will happen together (gamble?) */
#if defined (G8BPQ) || defined (INP2011)
            " Hops (I)rtt"
#endif
            "\n");
  
            tprintf("%6s:%-9s "
#ifdef G8BPQ
            "%s "
#endif
            ,buf,pax25(destbuf,rp->call)
#ifdef G8BPQ
            ,rp->flags & G8BPQ_NODEMASK ? "(BPQ)" : "     "
#endif
            );
        } else
            j2tputs("                       ");

        tprintf("%6s:%-9s ", nb_alias,pax25(neighbor,np->call));

	/* 26Aug2011, Maiko, Again trying to accomodate both BPQ and INP */

#if defined(G8BPQ) && defined(INP2011)
		if (npp && (npp->flags & G8BPQ_NODEMASK) && np->inp_state)
			tprintf ("(MIX)");
		else
#endif
#ifdef G8BPQ
		if (npp && (npp->flags & G8BPQ_NODEMASK))
			tprintf ("(BPQ)");
		else
#endif

	/* 26Aug2011, Maiko, BPQ and INP will not happen together (gamble?) */
#ifdef	INP2011
		if (np->inp_state)
			tprintf ("(%s)", (np->inp_state - 1) ? "INP" : "---");
		else
#endif
		tprintf ("     ");

	/* 26Aug2011, Maiko, Group type with Obs to save space */
        tprintf (" %-6s %3d %d/%c %5u", np->iface->name, bp->quality,
			bp->obsocnt, bp->flags & NRB_PERMANENT ? 'P' : (bp->flags & NRB_RECORDED ? 'R' : 'B'), bp->usage);

#if defined(G8BPQ) && defined(INP2011)
		if ((npp && (npp->hops || npp->irtt)) && np->inp_state)
			tprintf(" argggg");
		else
		{
#endif

#ifdef	INP2011
	/* 26Aug2011, Maiko, You must add neighbour (rtt) to get total */
        tprintf (" %4u %6u", bp->hops, np->rtt + bp->tt);
#endif

#ifdef G8BPQ
        if(npp && (npp->hops || npp->irtt))
		{
            if(npp->hops)
                tprintf(" %4u",npp->hops);
            else
                j2tputs(  "     ");
            if(npp->irtt)
                tprintf(" %6u",npp->irtt);
        }
#endif
#if defined(G8BPQ) && defined(INP2011)
		}
#endif
        tputc('\n');
    }
    tputc('\n');
    return 0 ;
}
  
int
donrneighbour(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int i,printheader=1;
    struct nrnbr_tab *np;
    struct nrroute_tab *rp;
    struct nr_bind *bind;
    int16 rhash;
    int justused;
    char tmp[AXBUF];
    char alias[AXALEN];
    char *cp;
    int percentage;
    int quality = 0;
    int obsocnt = 0;
#ifdef G8BPQ
    unsigned int irtt = 0;
#endif
  
    for(i=0;i<NRNUMCHAINS;i++) {    /*loop through all chains of neighbours*/
        for(np=Nrnbr_tab[i];np!=NULLNTAB;np=np->next) {
            /* If the interface is hidden, then do not show the route */
/*
            if(np->iface->flags & HIDE_PORT && Curproc->input != Command->input)
                continue;
 */
            if(printheader) {
                j2tputs("Routes :\n" \
                "   Neighbour             Port  Qual Obs Dest Tries Retries Perc"
/* 26Aug2011, Maiko, Don't think BPQ and INP will happen together (gamble?) */
#if defined (G8BPQ) || defined (INP2011)
                "  (I)rtt"
#endif
                "\n");
                printheader = 0;
            }
            /* has this one been used recently ? */
            if((secclock() - np->lastsent) < 60)
                justused = 1;
            else
                justused = 0;
  
            /* now we have to find the alias of this neighbour
             * so we can print that as well!
             */
            rhash = nrhash(np->call);
            for(rp=Nrroute_tab[rhash];rp!=NULLNRRTAB;rp=rp->next)
                if(addreq(rp->call,np->call))
                    break;
  
            if(rp != NULLNRRTAB) {
                /* found, now remove trailing spaces */
                strcpy(alias,rp->alias) ;
                if((cp = strchr(alias,' ')) != NULLCHAR)
                    *cp = '\0';
                /*find the quality for this neighbour*/
                bind = find_bind(rp->routes,np);
                if(bind) {
                    quality = bind->quality;
                    obsocnt = bind->obsocnt;
                } else  // should never happen !
                    quality = obsocnt = 0;
#ifdef G8BPQ
                irtt = rp->irtt;
#endif
            } else {
                strcpy(alias,"##TEMP");
            }
            if(np->tries)
                percentage = (int)( (((long)(np->tries)) * 100L) /
                ((long)((long)np->tries + (long)np->retries)) );
            else
                percentage = 0;

            /* print it all out */
            tprintf("%c %6s:%-9s ",
				(justused) ? '>' : ' ',
					alias,pax25(tmp,np->call));
	/*
	 * 26Aug2011, Maiko, All this just to be able to fit stuff on a line on
	 * the stupid screen. I'm really not sure if BPQ and INP can happen at the
	 * same time, I personally don't think so, but what if (arggg) ? Better be
	 * safe then sorry, and just observe operation and see what happens. So I
	 * will mark it as (MIX) in the remote chance this occurs, then deal with
	 * it (if anyone even notices it happening).
	 */

#if defined(G8BPQ) && defined(INP2011)
			if (rp && (rp->flags & G8BPQ_NODEMASK) && np->inp_state)
				tprintf ("(MIX)");
			else
#endif
#ifdef G8BPQ
			if (rp && (rp->flags & G8BPQ_NODEMASK))
				tprintf ("(BPQ)");
			else
#endif

	/* 26Aug2011, Maiko, BPQ and INP will not happen together (gamble?) */
#ifdef	INP2011
			if (np->inp_state)
				tprintf ("(%s)", (np->inp_state - 1) ? "INP" : "---");
			else
#endif
			tprintf ("     ");

			tprintf (" %-6s %3d %d",
            	np->iface->name, quality, obsocnt);

	/* 26Aug2011, Maiko, Add route type to stay consistent with dorouteinfo */
			if (bind)
				tprintf ("/%c", bind->flags & NRB_PERMANENT ? 'P' : (bind->flags & NRB_RECORDED ? 'R' : 'B'));
			else
				tprintf ("/?");

			tprintf ("  %3d %-5u %-5u   %3d %%", np->refcnt,
				np->tries, np->retries, percentage);

#if defined(G8BPQ) && defined(INP2011)
			if (irtt && np->inp_state)
                tprintf(" argggg");
			else
			{
#endif

#ifdef G8BPQ
            if(irtt)
                tprintf(" %6u",irtt);
#endif
#ifdef	INP2011
			if (np->inp_state)
				tprintf (" %6d", np->rtt);
#endif

#if defined(G8BPQ) && defined(INP2011)
			}
#endif
            tputc('\n');
        }
    }
    if(!printheader)
        tputc('\n');
    return 0;
}
  
/* define the netrom call,
 * this simply changes the interface linkaddress!
 * but is a little easier to use...
 */
static int
donrmycall(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    int len;
    char tmp[AXBUF];
  
    if(Nr_iface == NULLIF) {
        j2tputs("Attach netrom interface first\n") ;
        return 1 ;
    }
  
    if(argc < 2) {
        if (Nr_iface->hwaddr == NULLCHAR)
            j2tputs("not set\n");
        else
            tprintf("%s\n",pax25(tmp,Nr_iface->hwaddr));
    } else {
        if( (len=strlen(argv[1])) > (AXBUF - 1)) {
            j2tputs("too long\n");
            return 1;
        }
        if(Nr_iface->hwaddr != NULLCHAR)
            free(Nr_iface->hwaddr);
        Nr_iface->hwaddr = mallocw(AXALEN);
        setcall(Nr_iface->hwaddr,argv[1]);
#ifdef MAILBOX
        setmbnrid();
#endif
    }
    return 0;
}
  
/* make an interface available to net/rom */
/* arguments are:
 * argv[0] - "interface"
 * argv[1] - "iface" , the interface name
 * argv[2] - "quality", the interface broadcast quality
 * argv[3] - "n" or "v", to override the default (verbose)
 *            n = never broadcast verbose
 *            v = always broadcast verbose
 */
static
int
dointerface(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    struct iface *ifp ;
    int i,mtu ;
  
    if(Nr_iface == NULLIF) {
        j2tputs("Attach netrom interface first\n") ;
        return 1 ;
    }
  
    if(argc < 3) {
        i = 0;
        for(ifp=Ifaces;ifp;ifp=ifp->next) {
            if(ifp->flags & IS_NR_IFACE){
                if(!i) {
                    i = 1;
                    j2tputs("Iface  Qual MinBcQual\n");
                }
                tprintf("%-6s %-3d    %d\n",
                ifp->name,ifp->quality,ifp->nr_autofloor);
            }
        }
        return 0;
    }
  
    if((ifp = if_lookup(argv[1])) == NULLIF){
        tprintf(Badinterface,argv[1]);
        return 1;
    }
  
    if(ifp->type != CL_AX25){
        tprintf("Interface %s is not NETROM compatible\n",argv[1]);
        return 1;
    }
  
    /* activate the interface */
    ifp->flags |= IS_NR_IFACE;
  
    /* set quality */
    if((ifp->quality=atoi(argv[2])) > 255) /*Maximum quality possible*/
        ifp->quality = 255;
    /*check to see if quality is not 0 */
    if(ifp->quality <= 0)
        ifp->quality = 1;
  
    /* Set the minimum broadcast quality */
    ifp->nr_autofloor = Nr_autofloor;
    if(argc > 3)
        ifp->nr_autofloor = atoi(argv[3]);
  
    /* Check, and set the NETROM MTU - WG7J */
    if((mtu = ifp->ax25->paclen - 20) < Nr_iface->mtu)
        Nr_iface->mtu = mtu;
  
    /* Poll other nodes on this interface */
    nr_bcpoll(ifp);
  
    return 0 ;
}
  
  
/* convert a null-terminated alias name to a blank-filled, upcased */
/* version.  Return -1 on failure. */
int
putalias(to,from,complain)
register char *to, *from ;
int complain ;
{
    int len, i ;
  
    if((len = strlen(from)) > ALEN) {
        if(complain)
            j2tputs("alias too long - six characters max\n") ;
        return -1 ;
    }
  
    for(i = 0 ; i < ALEN ; i++) {
        if(i < len) {
            if(islower(*from))
                *to++ = toupper(*from++) ;
            else
                *to++ = *from++ ;
        }
        else
            *to++ = ' ' ;
    }
  
    *to = '\0' ;
    return 0 ;
}
  
/* Add a route */
static int
dorouteadd(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
    char alias[AXALEN] ;
    char dest[AXALEN] ;
    unsigned quality ;
    char neighbor[AXALEN] ;
    struct iface *ifp;
    int naddr ;
  
    /* format alias (putalias prints error message if necessary) */
    if(putalias(alias,argv[1],1) == -1)
        return -1 ;
  
    /* format destination callsign */
    if(setcall(dest,argv[2]) == -1) {
        j2tputs("bad destination callsign\n") ;
        return -1 ;
    }
  
    /* find interface */
    if((ifp = FindNrIface(argv[3])) == NULLIF) {
        return -1;
    }
  
    /* get and check quality value */
    if((quality = atoi(argv[4])) > 255) {
        j2tputs("maximum route quality is 255\n") ;
        return -1 ;
    }
  
    /* Change from 871225 -- no digis in net/rom table */
    naddr = argc - 5 ;
    if(naddr > 1) {
        j2tputs("Use the ax25 route command to specify digipeaters\n") ;
        return -1 ;
    }
  
    /* format neighbor address string */
    setcall(neighbor,argv[5]) ;
 
    nr_routeadd(alias,dest,ifp,quality,neighbor,1,0) ;
  
    /* Now see if we have a route to this neighbour,
     * if not add the implied route as a temporary one.
     * 930527 - WG7J
     */
    if(find_nrroute(neighbor) == NULLNRRTAB)
	{
        nr_routeadd("##TEMP",neighbor,ifp,quality,neighbor,0,1);
 	} 

    return 0;
}
  
  
/* drop a route */
static int
doroutedrop(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    char dest[AXALEN], neighbor[AXALEN] ;
    struct iface *ifp;
  
    /* format destination and neighbor callsigns */
    if(setcall(dest,argv[1]) == -1) {
        j2tputs("bad destination callsign\n") ;
        return -1 ;
    }
    if(setcall(neighbor,argv[2]) == -1) {
        j2tputs("bad neighbor callsign\n") ;
        return -1 ;
    }
  
    /* find interface */
    if((ifp = FindNrIface(argv[3])) == NULLIF) {
        return -1;
    }
  
    return nr_routedrop(dest,neighbor,ifp) ;
}
  
/* Broadcast nodes list on named interface. */
static int
dobcnodes(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    struct iface *ifp;
  
    /* find interface */
    if((ifp = FindNrIface(argv[1])) == NULLIF) {
        return -1;
    }
    nr_bcnodes(ifp) ;
    return 0;
}
  
/* Poll nodes for routes on named interface. - WG7J */
static int
dobcpoll(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    struct iface *ifp;
  
    /* find interface */
    if((ifp = FindNrIface(argv[1])) == NULLIF) {
        return -1;
    }
    nr_bcpoll(ifp) ;
    return 0;
}
  
/* Set outbound node broadcast interval */
static int
donodetimer(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2){
        tprintf("Nodetimer %d/%d seconds\n",
        read_timer(&Nodetimer)/1000,
        dur_timer(&Nodetimer)/1000);
        return 0;
    }
    stop_timer(&Nodetimer) ;        /* in case it's already running */
    Nodetimer.func = (void (*)__ARGS((void*)))donodetick;/* what to call on timeout */
    Nodetimer.arg = NULLCHAR;               /* dummy value */
    set_timer(&Nodetimer,atoi(argv[1])*1000);      /* set timer duration */
    start_timer(&Nodetimer);                /* and fire it up */
    return 0;
}
  
void
donodetick()
{
    struct iface *ifp;
  
    for(ifp=Ifaces;ifp;ifp=ifp->next)
        if(ifp->flags & IS_NR_IFACE)
            nr_bcnodes(ifp) ;
  
    /* Restart timer */
    start_timer(&Nodetimer) ;
}
  
/* Set timer for aging routes */
static int
doobsotimer(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2){
        tprintf("Obsotimer %d/%d seconds\n",
        read_timer(&Obsotimer)/1000,
        dur_timer(&Obsotimer)/1000);
        return 0;
    }
    stop_timer(&Obsotimer) ;        /* just in case it's already running */
    Obsotimer.func = (void (*)__ARGS((void*)))doobsotick;/* what to call on timeout */
    Obsotimer.arg = NULLCHAR;               /* dummy value */
    set_timer(&Obsotimer,atoi(argv[1])*1000);      /* set timer duration */
    start_timer(&Obsotimer);                /* and fire it up */
    return 0;
}
 
#ifdef	INP2011
extern int INP_active;
#endif 
  
/* Go through the routing table, reducing the obsolescence count of
 * non-permanent routes, and purging them if the count reaches 0
 */
void
doobsotick()
{
    register struct nrnbr_tab *np ;
    register struct nrroute_tab *rp, *rpnext ;
    register struct nr_bind *bp, *bpnext ;
    int i ;
  
    for(i = 0 ; i < NRNUMCHAINS ; i++) {
        for(rp = Nrroute_tab[i] ; rp != NULLNRRTAB ; rp = rpnext) {
            rpnext = rp->next ;     /* save in case we free this route */
        /* Check all bindings for this route */
            for(bp = rp->routes ; bp != NULLNRBIND ; bp = bpnext) {
                bpnext = bp->next ;     /* in case we free this binding */
                if(bp->flags & NRB_PERMANENT)   /* don't age these */
                    continue ;
                if(--bp->obsocnt == 0) {                /* time's up! */
                    if(bp->next != NULLNRBIND)
                        bp->next->prev = bp->prev ;
                    if(bp->prev != NULLNRBIND)
                        bp->prev->next = bp->next ;
                    else
                        rp->routes = bp->next ;
                    rp->num_routes-- ;                      /* one less binding */
                    np = bp->via ;                          /* find the neighbor */
                    free((char *)bp) ;                              /* now we can free the bind */
                    /* Check to see if we can free the neighbor */
                    if(--np->refcnt == 0) {
                        if(np->next != NULLNTAB)
                            np->next->prev = np->prev ;
                        if(np->prev != NULLNTAB)
                            np->prev->next = np->next ;
                        else {
                            Nrnbr_tab[nrhash(np->call)] = np->next ;
                        }
                        free((char *)np) ;      /* free the storage */
                    }
                }
            }

            if(rp->num_routes == 0) {               /* did we free them all? */

#ifdef	INP2011

			/*
			 * 21Sep2011, Maiko, This is why we still have ##TEMP entries,
			 * because I was allowing the node to be removed from nrroute
			 * table - hopefuly this little bit clears it up. Still need
			 * clean any links though, since no more routes present.
			 */
				if (INP_active)
                	nrresetlinks(rp);
				else
				{

				/* 13Sep2011, Maiko, debug see if *routeless* nodes removed */
				if (Nr_debug)
				{
					char tmp[AXBUF];

					log (-1, "obsolete node [%s]",
						pax25 (tmp, rp->call));
				}
#endif
                if(rp->next != NULLNRRTAB)
                    rp->next->prev = rp->prev ;
                if(rp->prev != NULLNRRTAB)
                    rp->prev->next = rp->next ;
                else
                    Nrroute_tab[i] = rp->next ;
        /* No more routes left !
         * We should close/reset any netrom connections
         * still idling for this route ! - WG7J
         */
                nrresetlinks(rp);
                free((char *)rp) ;
#ifdef	INP2011
				}
#endif
            }
        }
    }
  
    start_timer(&Obsotimer) ;
}

static struct cmds Nfcmds[] = {
    { "add",  donfadd,        0, 3,
        "netrom nodefilter add <neighbor> <interface> [quality]" },
    { "drop", donfdrop,       0, 3,
        "netrom nodefilter drop <neighbor> <interface>" },
    { "mode", donfmode,       0, 0,   NULLCHAR },
    { NULLCHAR, NULLFP((int,char**,void*)), 0, 0,
        "nodefilter subcommands: add drop mode" }
} ;
  
/* nodefilter command multiplexer */
static int
donodefilter(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    if(argc < 2) {
        donfdump() ;
        return 0 ;
    }
    return subcmd(Nfcmds,argc,argv,p) ;
}
  
/* display a list of <callsign,interface> pairs from the filter
 * list.
 */
static int
donfdump()
{
    int i, column = 1 ;
    struct nrnf_tab *fp ;
    char buf[AXBUF] ;
  
    for(i = 0 ; i < NRNUMCHAINS ; i++)
        for(fp = Nrnf_tab[i] ; fp != NULLNRNFTAB ; fp = fp->next) {
            pax25(buf,fp->neighbor) ;
            tprintf("%-7s  %-8s  %-3d   ",
            buf,fp->iface->name, fp->quality) ;
            if(column++ == 3) {
                if(tputc('\n') == EOF)
                    return 0;
                column = 1 ;
            }
        }
  
    if(column != 1)
        tputc('\n') ;
  
    return 0 ;
}
  
/* add an entry to the filter table */
static int
donfadd(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    struct iface *ifp;
    unsigned qual;
    char neighbor[AXALEN] ;
  
    /* format callsign */
    if(setcall(neighbor,argv[1]) == -1) {
        j2tputs("bad neighbor callsign\n") ;
        return -1 ;
    }
  
    /* find interface */
    if((ifp = FindNrIface(argv[2])) == NULLIF) {
        return -1;
    }
  
    qual = ifp->quality; /* set default quality */
  
    if(argc > 3)
        qual = atoi(argv[3]);
  
    return nr_nfadd(neighbor,ifp,qual) ;
}
  
/* drop an entry from the filter table */
static int
donfdrop(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    struct iface *ifp;
    char neighbor[AXALEN] ;
  
    /* format neighbor callsign */
    if(setcall(neighbor,argv[1]) == -1) {
        j2tputs("bad neighbor callsign\n") ;
        return -1 ;
    }
  
    /* find interface */
    if((ifp = FindNrIface(argv[2])) == NULLIF) {
        return -1;
    }
  
    return nr_nfdrop(neighbor,ifp) ;
}
  
/* nodefilter mode subcommand */
static int
donfmode(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    if(argc < 2) {
        j2tputs("filter mode is ") ;
        switch(Nr_nfmode) {
            case NRNF_NOFILTER:
                j2tputs("none\n") ;
                break ;
            case NRNF_ACCEPT:
                j2tputs("accept\n") ;
                break ;
            case NRNF_REJECT:
                j2tputs("reject\n") ;
                break ;
            default:
                j2tputs("some strange, unknown value\n") ;
        }
        return 0 ;
    }
  
    switch(argv[1][0]) {
        case 'n':
        case 'N':
            Nr_nfmode = NRNF_NOFILTER ;
            break ;
        case 'a':
        case 'A':
            Nr_nfmode = NRNF_ACCEPT ;
            break ;
        case 'r':
        case 'R':
            Nr_nfmode = NRNF_REJECT ;
            break ;
        default:
            j2tputs("modes are: none accept reject\n") ;
            return -1 ;
    }
  
    return 0 ;
}
  
/* netrom network packet time-to-live initializer */
static int
donrttl(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
    return setshort(&Nr_ttl,"Time to live",argc,argv);
}
  
/* show hidden (ie '#...') nodes or not */
static int
donrhidden(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    return setbool(&Nr_hidden,"Hidden nodes",argc,argv);
}
  
#ifdef G8BPQ
/* Emulate G8BPQ conreq/conack frames - WG7J */
static int
donrg8bpq(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    return setbool(&G8bpq,"G8BPQ mode",argc,argv);
}
#endif
  
/* allow automatic derating of netrom routes on link failure */
static int
donrderate(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    extern int Nr_derate;
  
    return setbool(&Nr_derate,"Derate flag",argc,argv);
}
  
/* promiscuous acceptance of broadcasts */
static int
donrpromisc(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    extern int Nr_promisc;
  
    return setbool(&Nr_promisc,"Promiscuous flag",argc,argv);
}
  
#ifdef NETROMSESSION
/* Initiate a NET/ROM transport connection */
static int
donrconnect(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    struct nrroute_tab *np;
    struct sockaddr_nr lsocket, fsocket;
    char alias[AXBUF];
    struct session *sp;
    int split = 0;
  
    /*Make sure this comes from console - WG7J*/
    if(Curproc->input != Command->input)
        return 0;
  
#ifdef SPLITSCREEN
    if(argv[0][0] == 's')
        split  = 1;
#endif
  
    /* Get a session descriptor */
    if((sp = newsession(argv[1],NRSESSION,split)) == NULLSESSION) {
        j2tputs(TooManySessions);
        return 1 ;
    }
  
    if((sp->s = j2socket(AF_NETROM,SOCK_SEQPACKET,0)) == -1){
        j2tputs(Nosock);
        keywait(NULLCHAR,1);
        freesession(sp);
        return 1;
    }
  
    /* See if the requested destination is a known alias or call,
     * use it if it is.  Otherwize give an error message. - WG7J
     */
    putalias(alias,argv[1],0);
    strupr(argv[1]);    /*make sure it's upper case*/
    if((np = find_nrboth(alias,argv[1])) == NULLNRRTAB){
    /*no such call or node alias*/
        j2tputs("no such node\n\n");
        keywait(NULLCHAR,1);
        freesession(sp);
        return 1;
    }

#ifdef INP2011 
	/* 13Sep2011, Maiko, We can now have *routeless* node entries */
	if (np->routes == NULLNRBIND)
	{
		j2tputs ("no route(s)\n\n");
		return 0;
	}
#endif

    /* Setup the local side of the connection */
    lsocket.nr_family = AF_NETROM;
  
    /* Set up our local username, bind would use Mycall instead */
    memcpy(lsocket.nr_addr.user,Nr4user,AXALEN);
  
    /* Putting anything else than Nr_iface->hwaddr here will not work ! */
    memcpy(lsocket.nr_addr.node,Nr_iface->hwaddr,AXALEN);
  
    /* Now bind the socket to this */
    j2bind(sp->s,(char *)&lsocket,sizeof(struct sockaddr_nr));
  
  
    /* Set up the remote side of the connection */
    fsocket.nr_family = AF_NETROM;
    memcpy(fsocket.nr_addr.user,np->call,AXALEN);
    memcpy(fsocket.nr_addr.node,np->call,AXALEN);
    fsocket.nr_family = AF_NETROM;
  
    return tel_connect(sp, (char *)&fsocket, sizeof(struct sockaddr_nr));
}
#endif
  
/* Reset a net/rom connection abruptly */
static int donrreset (int argc, char **argv, void *p)
{
    struct nr4cb *cb ;

    /* 10Mar2021, Maiko (VE4KLM), new getnr4cb() function */
    if ((cb = getnr4cb (argv[1])) == NULLNR4CB)
    {
        j2tputs (Notval) ;
        return 1 ;
    }

    reset_nr4 (cb);

    return 0;
}
  
/* Force retransmission on a net/rom connection */
  
static int donrkick (int argc, char **argv, void *p)
{
    struct nr4cb *cb ;

    /* 10Mar2021, Maiko (VE4KLM), new getnr4cb() function */
    if ((cb = getnr4cb (argv[1])) == NULLNR4CB)
    {
        j2tputs (Notval) ;
        return 1 ;
    }

    kick_nr4 (cb);

    return 0;
}
  
/* netrom transport ACK delay timer */
static int donracktime (int argc, char **argv, void *p)
{
    return setint32 (&Nr4acktime, "Ack delay time (ms)", argc, argv);
}
  
/* netrom transport choke timeout */
static int donrchoketime (int argc, char **argv, void *p)
{
    return setint32 (&Nr4choketime, "Choke timeout (ms)", argc, argv);
}
  
/* netrom transport initial round trip time */
  
static int donrirtt (int argc, char **argv, void *p)
{
    return setint32 (&Nr4irtt, "Initial RTT (ms)", argc, argv);
}
  
/* netrom transport receive queue length limit.  This is the */
/* threshhold at which we will CHOKE the sender. */
  
static int
donrqlimit(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
    return setshort(&Nr4qlimit,"Queue limit (bytes)",argc,argv);
}
  
/* Display or change our NET/ROM username */
static int
donruser(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char buf[AXBUF];
  
    if(argc < 2){
        pax25(buf,Nr4user);
        tprintf("%s\n",buf);
        return 0;
    }
    if(setcall(Nr4user,argv[1]) == -1)
        return -1;
    Nr4user[ALEN] |= E;
    return 0;
}
  
/* netrom transport maximum window.  This is the largest send and */
/* receive window we may negotiate */
  
static int
donrwindow(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
    return setshort(&Nr4window,"Window (frames)",argc,argv);
}
  
/* netrom transport maximum retries.  This is used in connect and */
/* disconnect attempts; I haven't decided what to do about actual */
/* data retries yet. */
  
static int
donrretries(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
    return setshort(&Nr4retries,"Retry limit",argc,argv);
}
  
  
/* Display the status of NET/ROM connections */
  
int donrstatus (int argc, char **argv, void *p)
{
    int i ;
    struct nr4cb *cb ;
    char luser[AXBUF], ruser[AXBUF], node[AXBUF] ;
  
    if (argc < 2)
	{
#ifdef UNIX
        /* 09Mar2021, NCB is now 12 wide */
        j2tputs("&NCB         Snd-W Snd-Q Rcv-Q     LUser      RUser @Node     State\n");
#else
        j2tputs("&NCB Snd-W Snd-Q Rcv-Q     LUser      RUser @Node     State\n");
#endif
        for(i = 0 ; i < NR4MAXCIRC ; i++) {
            if((cb = Nr4circuits[i].ccb) == NULLNR4CB)
                continue ;
            pax25(luser,cb->local.user) ;
            pax25(ruser,cb->remote.user) ;
            pax25(node,cb->remote.node) ;
#ifdef UNIX
        /* 09Mar2021, NCB is now 12 wide */
            if(tprintf("%-12lx %5d %5d %5d %9s  %9s %-9s %s\n",
#else
                if(tprintf("%4.4x   %3d %5d %5d %9s  %9s %-9s %s\n",
#endif
                    FP_SEG(cb), cb->nbuffered, len_q(cb->txq),
                    len_p(cb->rxq), luser, ruser, node,
                    Nr4states[cb->state]) == EOF)
                    break;
        }

        return 0 ;
    }

    /* 10Mar2021, Maiko (VE4KLM), new getnr4cb() function */
    if ((cb = getnr4cb (argv[1])) == NULLNR4CB)
    {
        j2tputs (Notval) ;
        return 1 ;
    }

    donrdump (cb) ;

    return 0 ;
}
  
/* Dump one control block */
  
void
donrdump(cb)
struct nr4cb *cb ;
{
    char luser[AXBUF], ruser[AXBUF], node[AXBUF] ;
    unsigned seq ;
    struct nr4txbuf *b ;
    struct timer *t ;
  
    pax25(luser,cb->local.user) ;
    pax25(ruser,cb->remote.user) ;
    pax25(node,cb->remote.node) ;
  
    tprintf("Local: %s %d/%d Remote: %s @ %s %d/%d State: %s\n",
    luser, cb->mynum, cb->myid, ruser, node,
    cb->yournum, cb->yourid, Nr4states[cb->state]) ;
  
    tprintf("Window: %-5u Rxpect: %-5u RxNext: %-5u RxQ: %-5d %s\n",
    cb->window, uchar(cb->rxpected), uchar(cb->rxpastwin),
    len_p(cb->rxq), cb->qfull ? "RxCHOKED" : "") ;
  
    tprintf(" Unack: %-5u Txpect: %-5u TxNext: %-5u TxQ: %-5d %s\n",
    cb->nbuffered, uchar(cb->ackxpected), uchar(cb->nextosend),
    len_q(cb->txq), cb->choked ? "TxCHOKED" : "") ;
  
    j2tputs("TACK: ") ;
    if(run_timer(&cb->tack))
        tprintf("%d", read_timer(&cb->tack)) ;
    else
        j2tputs("stop") ;
    tprintf("/%d ms; ", dur_timer(&cb->tack)) ;
  
    j2tputs("TChoke: ") ;
    if(run_timer(&cb->tchoke))
        tprintf("%d", read_timer(&cb->tchoke)) ;
    else
        j2tputs("stop") ;
    tprintf("/%d ms; ", dur_timer(&cb->tchoke)) ;
  
    j2tputs("TCD: ") ;
    if(run_timer(&cb->tcd))
        tprintf("%d", read_timer(&cb->tcd)) ;
    else
#ifndef NR4TDISC
        j2tputs("stop") ;
    tprintf("/%d ms", dur_timer(&cb->tcd)) ;
#else
    j2tputs("stop") ;
    tprintf("/%d ms; ", dur_timer(&cb->tcd)) ;
  
    j2tputs("TDisc: ") ;
    if(run_timer(&cb->tdisc))
        tprintf("%d", (read_timer(&cb->tdisc)/1000)) ;
    else
        j2tputs("stop") ;
    tprintf("/%d", (dur_timer(&cb->tdisc)/1000)) ;
#endif
  
    if(run_timer(&cb->tcd))
        tprintf("; Tries: %u\n", cb->cdtries) ;
    else
        tputc('\n') ;
  
	/* 11Oct2009, Maiko, Use "%d" for int32 vars */
    tprintf("Backoff Level %u SRTT %d ms Mean dev %d ms\n",
    cb->blevel, cb->srtt, cb->mdev) ;
  
    /* If we are connected and the send window is open, display */
    /* the status of all the buffers and their timers */
  
    if(cb->state == NR4STCON && cb->nextosend != cb->ackxpected) {
  
        j2tputs("TxBuffers:  Seq  Size  Tries  Timer\n") ;
  
        for(seq = cb->ackxpected ;
            nr4between(cb->ackxpected, seq, cb->nextosend) ;
        seq = (seq + 1) & NR4SEQMASK) {
  
            b = &cb->txbufs[seq % cb->window] ;
            t = &b->tretry ;
  
            if(tprintf("            %3u   %3d  %5d  %d/%d\n",
                seq, len_p(b->data), b->retries + 1,
                read_timer(t), dur_timer(t))
                == EOF)
                break;
        }
  
    }
  
}
  
/* netrom timers type - linear v exponential */
static int
donrtype(argc,argv,p)
int argc ;
char *argv[] ;
void *p ;
{
    extern unsigned Nr_timertype;
  
    if(argc < 2) {
        tprintf("Netrom timer type is %s\n", Nr_timertype ? "linear" : "exponential" ) ;
        return 0 ;
    }
  
    switch(argv[1][0]) {
        case 'l':
        case 'L':
            Nr_timertype = 1 ;
            break ;
        case 'e':
        case 'E':
            Nr_timertype = 0 ;
            break ;
        default:
            j2tputs("use: netrom timertype [linear|exponential]\n") ;
            return -1 ;
    }
  
    return 0 ;
}
  
static int
dominquality(argc,argv,p)
int argc ;
char *argv[] ;
void *p ;
{
    unsigned val ;
    extern unsigned Nr_autofloor;
  
    if(argc < 2) {
        tprintf("%u\n", Nr_autofloor) ;
        return 0 ;
    }
  
    val = atoi(argv[1]) ;
  
    if(val == 0 || val > 255 ) {
        j2tputs("maximum route quality is 255\n") ;
        return 1 ;
    }
  
    Nr_autofloor = val ;
  
    return 0 ;
}
  
/* Fixed and now functional, 920317 WG7J */
int
donrload(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    char buff[255];
    FILE *fn;
    time_t now, prev;
    int quality,obso;
    int permanent,record;
    struct iface *ifp;
    char alias[12],dest[12],iface[12],neighbor[12],type[3],*ptr;
    char destalias[ALEN+1]; /*alias in 'alias form'*/
    char destcall[AXALEN];  /*in callsign (ie shifted) form */
    char destneighbor[AXALEN];
  
    if(Nr_iface == NULLIF) {
        j2tputs("Attach netrom interface first\n") ;
        return 1;
    }
  
    if((fn = fopen(Netromfile,READ_TEXT)) == NULLFILE){
/*
        j2tputs("Can't open netrom save file!\n");
*/
        return 0;
    }
  
    if(fgets(buff,sizeof(buff),fn) == NULLCHAR){ /* read the timestamp */
        fclose(fn);
        return 1;
    }
    if((strncmp(buff,"time = ",7))!= 0){
/*
        j2tputs("Wrong node file content\n");
*/
        fclose(fn);
        return 1;
    }
    time(&now);
    sscanf(buff,"time =%ld",&prev);
/*
    tprintf("now = %ld , prev = %ld\n",now,prev);
*/
    if(prev >= now){
    /*
        j2tputs("You traveled back in time!!\n");
    */
        fclose(fn);
        return 1;
    }
  
    while(fgets(buff,sizeof(buff),fn) != NULLCHAR){
        if((ptr = strchr(buff,':')) == 0){
            sscanf(buff,"%s%s%i%i%s%s"
            ,dest,type,&quality,&obso,iface,neighbor);
            alias[0] = '\0';
        } else {
            *ptr = ' ';
            sscanf(buff,"%s%s%s%i%i%s%s"
            ,alias,dest,type,&quality,&obso,iface,neighbor);
        }
    /*Set and check calls / alias - WG7J */
        if(setcall(destcall,dest) == -1) {
        /*
        tprintf("Bad call %s\n",dest);
        */
            continue;
        }
        if(setcall(destneighbor,neighbor) == -1) {
        /*
        tprintf("Bad call %s\n",neighbor);
        */
            continue;
        }
        if(putalias(destalias,alias,1) == -1)
            continue;
  
    /* find interface */
        if((ifp = if_lookup(iface)) == NULLIF)
            continue;
  
    /* Is it a netrom interface ? */
        if(!(ifp->flags & IS_NR_IFACE))
            continue;
  
        /* get and check quality value */
        if(quality  > 255 || quality < (int)Nr_autofloor) {
        /*
            j2tputs("maximum route quality is 255\n") ;
         */
            continue;
        }
    /* Check the type of route - WG7J */
        permanent = record = 0;
        if(strchr(type,'P') != NULLCHAR)
            permanent = 1;
        else {
            if(strchr(type,'R') != NULLCHAR)
                record = 1;
        }
        nr_routeadd (destalias, destcall, ifp, quality,
			destneighbor, permanent, record);
    }
    fclose(fn);
    return 0;
}
  
int
donrsave(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    register struct nrroute_tab *rp ;
    register struct nr_bind *bp ;
    register struct nrnbr_tab *np ;
    char neighbor[AXBUF] ;
    register int i;
    char buf[16] ;
    char *cp ;
    FILE *fn;
    time_t now;
  
#ifdef __TURBOC__
    if((fn = fopen(Netromfile,"wt+")) == NULLFILE){
#else
        if((fn = fopen(Netromfile,"w+")) == NULLFILE){
#endif
            j2tputs("Can't write netrom save file!\n");
            return 1;
        }
        time(&now);
        fprintf(fn,"time = %ld\n",now);
        for(i = 0 ; i < NRNUMCHAINS ; i++){
            for(rp = Nrroute_tab[i] ; rp != NULLNRRTAB ; rp = rp->next){
                strcpy(buf,rp->alias) ;
            /* remove trailing spaces */
                if((cp = strchr(buf,' ')) == NULLCHAR)
                    cp = &buf[strlen(buf)] ;
                if(cp != buf)           /* don't include colon for null alias */
                    *cp++ = ':' ;
                for(bp = rp->routes; bp != NULLNRBIND; bp = bp->next) {
                    pax25(cp,rp->call) ;
                    fprintf(fn,"%-16s  ",buf) ;
                    np = bp->via ;
                    if(fprintf(fn,"%1s %3d  %3d  %-8s  %s\n",
                        (bp->flags & NRB_PERMANENT ? "P" :
                        bp->flags & NRB_RECORDED ? "R" : "X"),
                        bp->quality,bp->obsocnt,
                        np->iface->name,
                        pax25(neighbor,np->call)) == EOF)
                        break;
                }
            }
        }
        fclose(fn);
        return 0;
    }
  
#endif /* NETROM */

