/* IP-related user commands
 * Copyright 1991 Phil Karn, KA9Q
 */
 /* Mods by PA0GRI */
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "timer.h"
#include "netuser.h"
#include "iface.h"
#include "session.h"
#include "ip.h"
#include "cmdparse.h"
#include "commands.h"
#include "rip.h"
#include "rspf.h"
#include "domain.h"
#include "pktdrvr.h"
#include "socket.h"

#define	DEBUG_RIPAMPRGW
  
int32 Ip_addr;
#ifdef	RIPAMPRGW
int32 Ip_upstairs;
#endif

#ifdef __GNUC__
int Route_Sort = 1;
#else
extern int Route_Sort=1;
#endif
  
static int doadd __ARGS((int argc,char *argv[],void *p));
#ifdef  IPACCESS
static int doaccess __ARGS((int argc,char *argv[],void *p));
#endif
static int dodrop __ARGS((int argc,char *argv[],void *p));
#ifdef ENCAP
static int doencap __ARGS((int argc,char *argv[],void *p));
#endif
static int doflush __ARGS((int argc,char *argv[],void *p));
#ifdef	RIPAMPRGW
static int doupstairs (int, char**, void*);
#endif
static int doipaddr __ARGS((int argc,char *argv[],void *p));
static int doipstat __ARGS((int argc,char *argv[],void *p));
static int dolookup __ARGS((int argc,char *argv[],void *p));
static int dortimer __ARGS((int argc,char *argv[],void *p));
static int dottl __ARGS((int argc,char *argv[],void *p));
int doipheard __ARGS((int argc,char *argv[],void *p));
static int doipflush __ARGS((int argc,char *argv[],void *p));
static int doiphport __ARGS((int argc,char *argv[],void *p));
static int doiphsize __ARGS((int argc,char *argv[],void *p));
void dumproute __ARGS((struct route *rp,char *p));
static int doroutesort __ARGS((int argc,char *argv[],void *p));

#ifdef	DEBUG_RIPAMPRGW
static int genencaptxt (int argc, char **argv, void *p);
#endif

#ifdef  DYNGWROUTES
/* From 04Apr2008, N8CML (Ron) - New : display dyngw route entries */
static int dodyngwdump (int argc, char **argv, void *p);
#endif

static struct cmds DFAR Ipcmds[] = {
#ifdef  IPACCESS
    { "access",       doaccess,       0,      0, NULLCHAR },
#endif
    { "address",      doipaddr,       0,      0, NULLCHAR },
#ifdef ENCAP
    { "encap",        doencap,        0,      0, NULLCHAR },
#endif
    { "heard",        doipheard,      0,      0, NULLCHAR },
    { "flush",        doipflush,      0,      0, NULLCHAR },
    { "hport",        doiphport,      0,      0, NULLCHAR },
    { "hsize",        doiphsize,      0,      0, NULLCHAR },
#ifdef	RIPAMPRGW
    { "upstairs",     doupstairs,     0,      0, NULLCHAR },
#endif
    { "rtimer",       dortimer,       0,      0, NULLCHAR },
    { "status",       doipstat,       0,      0, NULLCHAR },
    { "ttl",          dottl,          0,      0, NULLCHAR },
    { NULLCHAR,		NULL,	0,	0,	NULLCHAR }
};

/* "route" subcommands */
static struct cmds DFAR Rtcmds[] = {
    { "add",          doadd,          0,      3,
#ifdef	ENCAP
    "route add <dest addr>[/<bits>] <if name> [<gateway> | direct [<metric> | <protocol>]]\nwhere <protocol> = <#>,ipip,udp" },
#else
    "route add <dest addr>[/<bits>] <if name> [<gateway> | direct [metric]]" },
#endif
    { "addprivate",   doadd,          0,      3,
    "route addprivate <dest addr>[/<bits>] <if name> [<gateway> | direct [metric]]" },
  
    { "drop",         dodrop,         0,      2,
     "route drop <dest addr>[/<bits>]" },

    { "flush",        doflush,        0,      0,
    NULLCHAR },

#ifdef	DEBUG_RIPAMPRGW
	/* 09Mar2010, Maiko, New function to generate a encap.txt file */
    { "genencaptxt",     genencaptxt,    0,      0,
    NULLCHAR },
#endif
  
    { "lookup",       dolookup,       0,      2,
    "route lookup <dest addr>" },
  
    { "sort",         doroutesort,    0,      0,
    NULLCHAR },

#ifdef  DYNGWROUTES
/* From 04Apr2008, N8CML (Ron) - New : dump dyngw route entries */
    { "dyngwdump",   dodyngwdump,    0,      0,
    NULLCHAR },
#endif
  
    { NULLCHAR,		NULL,			0,		0,
	NULLCHAR }
};
  
int
doip(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Ipcmds,argc,argv,p);
}

#ifdef  IPACCESS

/* 28Dec2004, Replaces 'usage' label with function */
static int do_usage ()
{
	j2tputs(" Format: ip access <permit|deny|delete> <proto> <src addr>[/<bits>] <dest addr>[/<bits>] <if name> [low [high]]\n");
	return 1;
}

static int
doaccess(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp;
    int32 source,target;
    unsigned sbits,tbits;
    char *bitp;
    int16 lport,hport,protocol,state;
    char *cp; /* for printing the table */
    struct rtaccess *tpacc;
    struct rtaccess *btacc;
    struct rtaccess *bfacc;
    struct rtaccess *head;
    char tmpbuf[15];
  
    if(argc == 1){ /* print out the table */
        j2tputs("Source Address  Len Dest Address    Len Interface    Proto Low   High  State\n");
        for(tpacc = IPaccess;tpacc != NULLACCESS;tpacc = tpacc->nxtiface){
            for(btacc = tpacc;btacc != NULLACCESS;btacc = btacc->nxtbits){
                if(btacc->source != 0)
                    cp = inet_ntoa(btacc->source);
                else
                    cp = "all";
                tprintf("%-16s",cp);
                tprintf("%2u  ",btacc->sbits);
                if(btacc->target != 0)
                    cp = inet_ntoa(btacc->target);
                else
                    cp = "all";
                tprintf("%-16s",cp);
                tprintf("%2u  ",btacc->bits);
                tprintf("%-13s",btacc->iface->name);
                switch (btacc->protocol) {
                    case 0:
                        cp = "any";
                        break;
                    case ICMP_PTCL:
                        cp = "icmp";
                        break;
                    case TCP_PTCL:
                        cp = "tcp";
                        break;
                    case UDP_PTCL:
                        cp = "udp";
                        break;
                    default:
                        cp = itoa(btacc->protocol,tmpbuf,10);
                }
                tprintf("%-5s ",cp);
                tprintf("%5u ",btacc->lowport);
                tprintf("%5u ",btacc->highport);
                if(btacc->status)
                    cp = "deny";
                else
                    cp = "permit";
                tprintf("%-6s\n",cp);
            }
        }
        return 0;
    }
  
    if(strcmp(argv[1],"permit") == 0){
        state = 0;
    } else {
        if((strcmp(argv[1],"deny") == 0)
        || (strcmp(argv[1],"delete") == 0)){
            state = -1;
        } else {
			return (do_usage ());
        }
    }
    if (argc < 6) return (do_usage ());

    switch (*argv[2]){
        case 'a':       /* ANY */
            protocol = 0;
            break;
        case 'i':       /* ICMP */
            protocol = ICMP_PTCL;
            break;
        case 't':       /* TCP */
            protocol = TCP_PTCL;
            break;
        case 'u':       /* UDP */
            protocol = UDP_PTCL;
            break;
        default:
            protocol = atoi(argv[2]);
    }
  
    if(strcmp(argv[3],"all") == 0){
        source = 0;
        sbits = 0;
    } else {
        /* If IP address is followed by an optional slash and
         * a length field, (e.g., 128.96/16) get it;
         * otherwise assume a full 32-bit address
         */
        if((bitp = strchr(argv[3],'/')) != NULLCHAR){
            /* Terminate address token for resolve() call */
            *bitp++ = '\0';
            sbits = atoi(bitp);
        } else
            sbits = 32;
  
        if((source = resolve(argv[3])) == 0){
            tprintf(Badhost,argv[3]);
            return 1;
        }
    }
    if(strcmp(argv[4],"all") == 0){
        target = 0;
        tbits = 0;
    } else {
        if((bitp = strchr(argv[4],'/')) != NULLCHAR){
            *bitp++ = '\0';
            tbits = atoi(bitp);
        } else
            tbits = 32;
  
        if((target = resolve(argv[4])) == 0){
            tprintf(Badhost,argv[4]);
            return 1;
        }
    }
  
    if((ifp = if_lookup(argv[5])) == NULLIF){
        tprintf(Badinterface,argv[5]);
        return 1;
    }
  
    if(((protocol != TCP_PTCL) && (protocol != UDP_PTCL)) || (argc < 7)) {
        lport = 0;
        hport = 0;
    } else {
        if(strcmp(argv[6],"all") == 0){
            lport = 0;
        } else {
            lport = atoi(argv[6]);
        }
        if((argc < 8) || (lport == 0))
            hport = lport;
        else
            hport = atoi(argv[7]);
    }
  
    if(strcmp(argv[1],"delete") == 0){
        head = IPaccess;
        for(tpacc = IPaccess;tpacc != NULLACCESS;head = tpacc,tpacc = tpacc->nxtiface){
            if(tpacc->iface == ifp){
                for(btacc = tpacc;btacc != NULLACCESS;
                head = btacc,btacc = btacc->nxtbits){
                    if((btacc->protocol == protocol) &&
                        (btacc->source == source)     &&
                        (btacc->sbits == sbits)       &&
                        (btacc->target == target)     &&
                        (btacc->bits == tbits)        &&
                        (btacc->lowport == lport)     &&
                    (btacc->highport == hport)) { /*match*/
                        bfacc = btacc; /* save to unalloc */
                /*now delete. watch for special cases*/
                        if(btacc != tpacc){ /* not at head of list */
                            head->nxtbits = btacc->nxtbits;
                            free(bfacc);
                            return 0;
                        }
                        if(btacc == IPaccess){ /* real special case */
                            if(IPaccess->nxtbits == NULLACCESS)
                                IPaccess = btacc->nxtiface;
                            else {
                                IPaccess = btacc->nxtbits;
                                (btacc->nxtbits)->nxtiface = btacc->nxtiface;
                            }
                        } else { /* we know tpacc=btacc <> IPaccess */
                            if(btacc->nxtbits == NULLACCESS)
                                head->nxtiface = btacc->nxtiface;
                            else {
                                head->nxtiface = btacc->nxtbits;
                                (btacc->nxtbits)->nxtiface = btacc->nxtiface;
                            }
                        }
                        free(bfacc);
                        return 0;
                    }
                }
            }
        }
        j2tputs("Not found.\n");
        return 1;
    }
    /* add the access */
    addaccess(protocol,source,sbits,target,tbits,ifp,lport,hport,state);
    return 0;
}
#endif

#ifdef	RIPAMPRGW
static int doupstairs (int argc, char **argv, void *p)
{
	int32	n;
  
	if (argc < 2)
		tprintf ("%s\n", inet_ntoa (Ip_upstairs));

	else if ((n = resolve (argv[1])) == 0)
	{
        tprintf (Badhost, argv[1]);
        return 1;
    }
	else Ip_upstairs = n;

    return 0;
}

int ismulticast (int32 addr)
{
	return (addr == Ip_upstairs);
}

#endif

static int
doipaddr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int32 n;
  
    if(argc < 2) {
        tprintf("%s\n",inet_ntoa(Ip_addr));
    } else if((n = resolve(argv[1])) == 0){
        tprintf(Badhost,argv[1]);
        return 1;
    } else
        Ip_addr = n;
    return 0;
}
static int dortimer (int argc, char **argv, void *p)
{
    return setint32 (&ipReasmTimeout, "IP reasm timeout (sec)", argc, argv);
}
static int dottl (int argc, char **argv, void *p)
{
    return setint32 (&ipDefaultTTL, "IP Time-to-live", argc, argv);
}

#ifdef DYNGWROUTES
/* From 04Apr2008, N8CML (Ron) - New : 'D' flag to show dyngw route entries */
char RouteHeader[] = "Destination      Len Interface Gateway          Metric PD Timer  Use\n";
#else
char RouteHeader[] = "Destination      Len Interface Gateway          Metric P Timer  Use\n";
#endif

/* Display and/or manipulate routing table */
int
doroute(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register int i,j,k,bits;
    int flow_tmp,nosort;
    register struct route *rp;
    char *temp,temp2[80];
  
    if(argc >= 2)
        return subcmd(Rtcmds,argc,argv,p);
  
    /* Dump IP routing table
     * Dest            Len Interface    Gateway          Use
     * 192.001.002.003 32  sl0          192.002.003.004  0
     * modified for Sorted output - D. Crompton 2.92
     */
  
    flow_tmp=Current->flowmode;
    Current->flowmode=1;
  
    j2tputs(RouteHeader);
  
    for(j=0,bits=31;bits>=0;bits--)
        for(i=0;i<HASHMOD;i++)
            for(rp = Routes[bits][i];rp != NULLROUTE;j++,rp = rp->next);
  
    if (j)
	{
		/* 22Dec2005, Maiko, Changed malloc() to mallocw() instead ! */
        temp=mallocw((unsigned)j*80);

        nosort = (temp == NULLCHAR);  /* too big an allocation? */
  
        for(bits=31,k=0;bits>=0;bits--)
            for(i=0;i<HASHMOD;i++)
                for(rp = Routes[bits][i];rp != NULLROUTE;rp = rp->next,k+=80) {
                    if (nosort) {
                        dumproute(rp,temp2);
                        tprintf("%s\n",&temp2[4]);
                    } else
                        dumproute(rp,&temp[k]);
                }
  
        if (!nosort) {
#ifdef UNIX
            if (Route_Sort)
			{
			/*	log (-1, "calling j2qsort, %d entries", j); */

				j2qsort(temp,(size_t)j,80,
					(int (*)(const void*,const void*)) strcmp);
			}
#else
            if (Route_Sort) qsort(temp,(size_t)j,80,(int (*) ()) strcmp);
#endif
  
            for (i=0,k=4;i<j;i++,k+=80) {
                j2tputs(&temp[k]);
                if (tputc('\n') == EOF)
                {
                    Current->flowmode=flow_tmp;
                    free(temp);
                    return 0;
                }
            }
            free(temp);
        }
    }
    if(R_default.iface != NULLIF){
        dumproute(&R_default,temp2);
        tprintf("%s\n",&temp2[4]);
    }
    Current->flowmode = flow_tmp;
    return 0;
}

#ifdef	DEBUG_RIPAMPRGW

/*
 * 09Mar2010, Maiko, Routines I wrote to generate an 'encap.txt' type file,
 * to help me to debug the new RIP update code. The first 16 bytes of each
 * entry (line) in the file can be used by an external sort program to sort
 * the file according to gateway IP address.
 *
 * 08Nov2020, Maiko (VE4KLM), A request was made to remove the first column
 * of information so that the file is more like the 'true' encap.txt, done,
 * but added a flag to keep the original (debugging) functionality.
 *
 * This was requested I believe because there are reports that people are not
 * able to download their encap.txt anymore from the portal system because of
 * the fact their 44 block isn't on BGP - it's supposedly being looked into.
 *
 */

void dump2encap (FILE *fpo, register struct route *rp)
{
	char *ptr, temp[100];

	ptr = temp;

	if (rp->iface != &Encap)	/* I only want encap entries */
		return;

#ifdef	ONLY_FOR_ME
	/*
	 * The fuller.net encap.txt is sorted by gateway address (sort of),
	 * me putting in these 16 bytes lets me run 'sort' on the generated
	 * file, so that I too can have the order by ascending gateway ip
	 * address, easier to do a 'diff', well sort of anyways :)
	 *
	 * 08Nov2020, Maiko, Took this out for the masses, I only put it in
	 * for debugging purposes when I originally wrote it, so take it out
	 * so the generated encap.txt actually looks like the real thing !
	 */
    if (rp->gateway != 0)
    	ptr += sprintf (ptr, "%-16.16s", inet_ntoa (rp->gateway));
#endif

    ptr += sprintf (ptr, "route addprivate");

    if (rp->target != 0)
    	ptr += sprintf (ptr, " %s/%u", inet_ntoa (rp->target), rp->bits);

    if (rp->iface->name)
    	ptr += sprintf (ptr, " %s", rp->iface->name);

    if (rp->gateway != 0)
    	ptr += sprintf (ptr, " %s", inet_ntoa (rp->gateway));

    fprintf (fpo, "%s\n", temp);
}

int genencaptxt (int argc, char **argv, void *p)
{
    register int i, j, bits;

    register struct route *rp;

    FILE *fp;

    for (j = 0, bits = 31; bits >= 0; bits--)
        for (i = 0; i < HASHMOD; i++)
            for (rp = Routes[bits][i]; rp != NULLROUTE; j++, rp = rp->next);
    if (j)
	{
		fp = fopen ("encap.txt.gen", "w");

        for (bits = 31; bits >= 0; bits--)
            for (i = 0; i < HASHMOD; i++)
                for (rp = Routes[bits][i]; rp != NULLROUTE; rp = rp->next)
                        dump2encap (fp, rp);
		fclose (fp);
    }

    return 0;
}

#endif	/* End of DEBUG_RIPAMPRGW */
  
/* Dump a routing table entry */
void
dumproute(rp,temp)
register struct route *rp;
char *temp;
{
    char *cp;
    unsigned int a=0;
    char *name;
  
    if(rp->target != 0) {
        if(DTranslate && rp->bits==32 && (name = resolve_a(rp->target,!DVerbose)) != NULLCHAR) {
            strcpy(temp,name);
            free(name);
        } else {
            cp = inet_ntobos(rp->target);
            sprintf(temp,"%4s",cp);
        }
        cp = inet_ntoa(rp->target);
    } else {
        strcpy(temp,"default");    /* Don't really matter, but avoid unknown value */
        cp = "default";
    }
    a=4;
    a+=sprintf(&temp[a],"%-16.16s ",cp);
    a+=sprintf(&temp[a],"%-4u",rp->bits);

#ifdef	ENCAP
	if (rp->iface == &Encap && rp->protocol == UDP_PTCL)
	{
		a += sprintf (&temp[a],"%-5.5s UDP ", rp->iface->name);
	}
	else
	{
#endif
	if (rp->iface->name)
		cp = rp->iface->name;
	else
		cp = " ";
    a+=sprintf(&temp[a],"%-9.9s ",cp);
#ifdef	ENCAP
	}
#endif

    if(rp->gateway != 0)
        cp = inet_ntoa(rp->gateway);
    else
        cp = " ";
    a+=sprintf(&temp[a],"%-16.16s ",cp);
    a+=sprintf(&temp[a],"%-6d ",rp->metric);

    temp[a] = (rp->flags & RTPRIVATE) ? 'P' : ' ';
    a++;
#ifdef  DYNGWROUTES
    /* From 04Apr2008, N8CML (Ron) - New : 'D' flag to show dyngw routes */
    temp[a] = (rp->flags & RTDYNGWROUTE) ? 'D' : ' ';
    a++;
#endif
    temp[a] = ' ';
    a++;

    if(rp->timer.state == TIMER_STOP){
        if(rp->timer.duration == 2) a+=sprintf(&temp[a],"rspf   ");  /* it9lta */
        else a+=sprintf(&temp[a],"man    ");
    } else {
        a+=sprintf(&temp[a],"%-7d",read_timer(&rp->timer) / 1000);
    }
    sprintf(&temp[a],"%d",rp->uses);
}
  
  
/* Sort Route dump */
static int
doroutesort(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    extern int Route_Sort;
  
    return setbool(&Route_Sort,"Route Sort flag",argc,argv);
}

#ifdef	DYNGWROUTES
/* 04Jan09, Maiko (VE4KLM), Added rtdyngw modifications by N8CML (Ron) */
static int dodyngwdump (int argc, char **argv, void *p)
{
    extern void rtdyngw_dump (void);

    rtdyngw_dump ();

    return 1;
}
#endif
  
#ifdef	ENCAP
/* 14Oct2004, Maiko, IPUDP support */
int GLOB_encap_protocol = 0;
#endif

#ifdef	DYNGWROUTES
extern void rtdyngw_add (char *hostname, struct route *ptr);
#endif
  
/* Add an entry to the routing table
 * E.g., "add 1.2.3.4 ax0 5.6.7.8 3"
 */
static int
doadd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp;
    int32 dest,gateway = 0;
    unsigned bits;
    char *bitp;
    int32 metric;
    char private;

#ifdef	DYNGWROUTES
	struct route *routerec;
#endif

#ifdef	ENCAP
	int protocol = 0;
#endif
  
    if(strncmp(argv[0],"addp",4) == 0)
        private = 1;
    else
        private = 0;
    if(strcmp(argv[1],"default") == 0){
        dest = 0L;
        bits = 0;
    } else {
        /* If IP address is followed by an optional slash and
         * a length field, (e.g., 128.96/16) get it;
         * otherwise assume a full 32-bit address
         */
        if((bitp = strchr(argv[1],'/')) != NULLCHAR){
            /* Terminate address token for resolve() call */
            *bitp++ = '\0';
            bits = atoi(bitp);
        } else
            bits = 32;
  
        if((dest = resolve(argv[1])) == 0){
            tprintf(Badhost,argv[1]);
            return 1;
        }
    }

    if((ifp = if_lookup(argv[2])) == NULLIF){
        tprintf(Badinterface,argv[2]);
        return 1;
    }
  
    metric = 1;
  
    if(argc > 3)
	{
        /* Next "trick is needed to set the metric on subnets
         * higher as the default 1 for rspf.
         * route add subnet/bits iface default 10
         */
        if(strcmp(argv[3],"direct") == 0){      /* N1BEE */
            gateway = 0;
        /* calculate a nice metric based on subnet mask size */
            if(bits != 0 && bits < 32)
                metric = (39 - bits) * 5 / 17;
        } else {
            if((gateway = resolve(argv[3])) == 0){
                tprintf(Badhost,argv[3]);
                return 1;
            }
        }

	/* 19Oct2004, Maiko, New 'protocol' option for encap routing (K2MF) */
		if (argc > 4)
		{
#ifdef ENCAP
                if (ifp == &Encap)
				{
                    strlwr (argv[4]);

                    switch (*argv[4])
					{
                        case 'i':
                            protocol = 0;	/* default operation */
                            break;
                        case 'u':
                            /* UDP */
                            protocol = UDP_PTCL;
                            break;
                        default:
                            if (isdigit ((int)(*argv[4])))
                                protocol = atoi (argv[4]);
                            break;
                    }
                } else
#endif
                    metric = atoi(argv[4]);
				/* 11Oct2009, Maiko, Use atoi on int32 vars */
		}
    }

#ifdef	ENCAP
	GLOB_encap_protocol = protocol;
#endif
  
#ifdef	DYNGWROUTES

	/* 01Nov2004, Maiko, Dynamic Gateways for IP Routes */

	routerec = rt_add (dest, bits, gateway, ifp, metric, 0, private);

	if (routerec)
	{
		/*
		 * If a gateway is defined and the gateway is a domain name and
		 * not an ip address, then assume it's a dynamic (dyndns type)
		 * entry, which we will now add to our *refresh* table for
		 * regular processing (re-resolving of the domain name).
		 *
		 * Having a separate refresh table that points to the specific
		 * route entry is better I think then adding an extra hostname
		 * field to the route structure.
		 */
		if (gateway && !isdigit (argv[3][0]))
			rtdyngw_add (argv[3], routerec);
	}
 	else
#else
    if (rt_add (dest,bits,gateway,ifp,metric,0,private) == NULLROUTE)
#endif
	{
        tprintf("Can't add route to %s/%u via %s %s\n",
			argv[1],bits,argv[2],(argc>3?argv[3]:""));
	}

#ifdef	ENCAP
	GLOB_encap_protocol = 0;	/* reset for next time */
#endif

#ifdef  RSPF
    if(!private)
	{
        rspfrouteupcall(dest,bits,gateway);     /* Do an RSPF upcall */
	}
#endif

    return 0;
}
/* Drop an entry from the routing table
 * E.g., "drop 128.96/16
 */
static int
dodrop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char *bitp;
    unsigned bits;
    int32 n;
  
    if(strcmp(argv[1],"default") == 0){
        n = 0L;
        bits = 0;
    } else {
        /* If IP address is followed by an optional slash and length field,
         * (e.g., 128.96/16) get it; otherwise assume a full 32-bit address
         */
        if((bitp = strchr(argv[1],'/')) != NULLCHAR){
            /* Terminate address token for resolve() call */
            *bitp++ = '\0';
            bits = atoi(bitp);
        } else
            bits = 32;
  
        if((n = resolve(argv[1])) == 0){
            tprintf(Badhost,argv[1]);
            return 1;
        }
    }
    return rt_drop(n,bits);
}
/* Force a timeout on all temporary routes */
static int
doflush(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct route *rp;
    struct route *rptmp;
    int i,j;
  
    if(R_default.timer.state == TIMER_RUN){
        rt_drop(0,0);   /* Drop default route */
    }
    for(i=0;i<HASHMOD;i++){
        for(j=0;j<32;j++){
            for(rp = Routes[j][i];rp != NULLROUTE;rp = rptmp){
                rptmp = rp->next;
                if(rp->timer.state == TIMER_RUN){
                    rt_drop(rp->target,rp->bits);
                }
            }
        }
    }
    return 0;
}
  
static int
dolookup(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct route *rp;
    int32 addr;
    char temp[80];
  
    addr = resolve(argv[1]);
    if(addr == 0){
        tprintf(Badhost,argv[1]);
        return 1;
    }
    if((rp = rt_lookup(addr)) == NULLROUTE){
        tprintf("Host %s (%s) unreachable\n",argv[1],inet_ntoa(addr));
        return 1;
    }
  
    dumproute(rp,temp);
    j2tputs(RouteHeader);
    tprintf("%s\n",&temp[4]);
  
    return 0;
}
  
static int
doipstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct reasm *rp;
    register struct frag *fp;
    int i;
  
    for(i=1;i<=NUMIPMIB;i++){
        tprintf("(%2d)%-20s%10d",i, Ip_mib[i].name,Ip_mib[i].value.integer);
        if(i % 2)
            j2tputs("     ");
        else
            tputc('\n');
    }
    if((i % 2) == 0)
        tputc('\n');
  
    if(Reasmq != NULLREASM)
        j2tputs("Reassembly fragments:\n");
    for(rp = Reasmq;rp != NULLREASM;rp = rp->next){
        tprintf("src %s",inet_ntoa(rp->source));
        tprintf(" dest %s",inet_ntoa(rp->dest));
        if(tprintf(" id %u pctl %u time %d len %u\n",
            rp->id,uchar(rp->protocol),read_timer(&rp->timer),
            rp->length) == EOF)
            break;
        for(fp = rp->fraglist;fp != NULLFRAG;fp = fp->next){
            if(tprintf(" offset %u last %u\n",fp->offset,
                fp->last) == EOF)
                break;
        }
    }
    return 0;
}
  
/* IP heard logging - WG7J */
static struct iph *iph_create __ARGS((int32 addr));
static struct iph *iph_lookup __ARGS((int32 addr));
struct iph *Iph;
  
static int Maxipheard = MAXIPHEARD;
static int numdb;
  
static int doiphsize(int argc,char *argv[],void *p) {
    if(argc > 1)                 /* if setting new size ... */
        doipflush(argc,argv,p);  /* we flush to avoid memory problems - K5JB */
    return setint(&Maxipheard,"Max ip-heard",argc,argv);
}
  
/* Configure a port to do ip-heard logging */
static int
doiphport(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setflag(argc,argv[1],LOG_IPHEARD,argv[2]);
}
  
int
doipheard(int argc,char *argv[],void *p) {
    struct iph *iph;
  
    if(j2tputs("Tcp/Ip systems heard:\n"
        "Address                Port       Since       Pkts\n") == EOF)
        return EOF;
    for(iph=Iph;iph;iph=iph->next) {
        if(tprintf("%-22s %-8s %12s %5ld\n",inet_ntoa(iph->addr),iph->iface->name,
            tformat(secclock() - iph->time),iph->count) == EOF)
            return EOF;
    }
    return 0;
}
  
void
log_ipheard(int32 addr,struct iface *ifp) {
    struct iph *niph;
  
    if((niph = iph_lookup(addr)) == NULLIPH)
        if((niph = iph_create(addr)) == NULLIPH)
            return;
    niph->iface = ifp;
    niph->count++;
    niph->time = secclock();
}
  
/* Look up an entry in the ip data base */
struct iph *
iph_lookup(addr)
int32 addr;
{
    register struct iph *ip;
    struct iph *last = NULLIPH;
  
    for(ip = Iph;ip != NULLIPH;last = ip,ip = ip->next){
        if(ip->addr == addr) {
            if(last != NULLIPH){
                /* Move entry to top of list */
                last->next = ip->next;
                ip->next = Iph;
                Iph = ip;
            }
            return ip;
        }
    }
    return NULLIPH;
}
  
/* Create a new entry in the source database */
/* If there are too many entries, override the oldest one - WG7J */
static struct iph *
iph_create(addr)
int32 addr;
{
    register struct iph *iph;
    struct iph *last = NULLIPH;
  
    if(Maxipheard && numdb == Maxipheard) {
    /* find and use last one in list */
        for(iph = Iph;iph->next != NULLIPH;last = iph,iph = iph->next);
    /* delete entry from end */
        if(last)
            last->next = NULLIPH;
        else    /* Only one entry, and Maxipheard == 1 ! */
            Iph = NULLIPH;
    } else {    /* create a new entry */
        numdb++;
        iph = (struct iph *)callocw(1,sizeof(struct iph));
    }
    iph->addr = addr;
    iph->count = 0;            /* VE3DTE */
    iph->next = Iph;
    Iph = iph;
  
    return iph;
}

static int
doipflush(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iph *iph,*iph1;
  
    for(iph = Iph;iph != NULLIPH;iph = iph1){
        iph1 = iph->next;
        free((char *)iph);
    }
    Iph = NULLIPH;
    numdb = 0;  /* K5JB */
    return 0;
}
  
#ifdef ENCAP
/* Set the transmit PID for IP-IP encapped frames */
static int
doencap(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    extern int EncapPID;
    int x = EncapPID;

    if (!setint(&x,"IP-IP encap transmit PID",argc,argv)) { /* change made ? */
        if((x != IP_PTCL) && (x != IP_PTCL_OLD)) {
            tprintf("IP-IP encap transmit PID must equal %d or %d\n",
                IP_PTCL,IP_PTCL_OLD);
            return 1;
        } else
            EncapPID = x;
    }

    return 0;
}
#endif /* ENCAP */
