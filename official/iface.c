/* IP interface control and configuration routines
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Mods by PA0GRI
 */
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#include "ip.h"
#include "netuser.h"
#include "ax25.h"
#include "enet.h"
#include "arp.h"
#include "pktdrvr.h"
#include "cmdparse.h"
#include "commands.h"
#include "nr4.h"
#include "socket.h"
#include "mailbox.h"
#include "tcp.h"	/* 29Sep2019, Maiko (VE4KLM) */
  
#ifdef NETROM
extern struct iface *Nr_iface;
extern char Nralias[];
#endif
  
static void showiface __ARGS((struct iface *ifp));
int mask2width __ARGS((int32 mask));
static int ifipaddr __ARGS((int argc,char *argv[],void *p));
static int iflinkadr __ARGS((int argc,char *argv[],void *p));
static int ifbroad __ARGS((int argc,char *argv[],void *p));
static int ifnetmsk __ARGS((int argc,char *argv[],void *p));
static int ifmtu __ARGS((int argc,char *argv[],void *p));
static int ifforw __ARGS((int argc,char *argv[],void *p));
static int ifencap __ARGS((int argc,char *argv[],void *p));
static int ifdescr __ARGS((int argc,char *argv[],void *p));
#ifdef RXECHO
static int ifrxecho __ARGS((int argc,char *argv[],void *p));
#endif
  
struct iftcp def_iftcp = {DEF_RTT,0L,DEF_WND,DEF_MSS,31,DEF_RETRIES,0,0};
  
/* Interface list header */
struct iface *Ifaces = &Loopback;
  
/* Loopback pseudo-interface */
struct iface Loopback = {
#ifdef ENCAP
    &Encap,     /* Link to next entry */
#else
    NULLIF,
#endif
    "loopback", /* name     */
    NULLCHAR,   /* description */
    0x7f000001L,    /* addr         127.0.0.1 */
    0xffffffffL,    /* broadcast    255.255.255.255 */
    0xffffffffL,    /* netmask      255.255.255.255 */
    MAXINT16,       /* mtu          No limit */
    0,      /* flags    */
#ifdef NETROM
    0,      /* quality  */
    0,      /* autofloor */
#endif
    0,              /* trace        */
    NULLCHAR,       /* trfile       */
    -1,             /* trsock       */
#ifdef J2_TRACESYNC
	/* 05Nov2010, required (ugly stuff) */
	{ NULLTIMER, 0, 0, (void*)0, (void*)0, 0 },	/* trsynct */
	0,				/* trsynci */
#endif
    NULLIF,         /* forw         */
#ifdef RXECHO    
    NULLIF,         /* rxecho       */
#endif
    NULLPROC,       /* rxproc       */
    NULLPROC,       /* txproc       */
    NULLPROC,       /* supv         */
    0,              /* dev          */
    (int32(*)__FARGS((struct iface*,int,int,int32)))NULL,/* (*ioctl)     */
    NULLFP((struct iface*,int,int32)),                   /* (*iostatus)  */
    NULLFP((struct iface*)),                             /* (*stop)      */
    NULLCHAR,       /* hwaddr       */
#ifdef AX25
    NULL,           /* ax25 protocol data */
#endif /* AX25 */
    &def_iftcp,     /* tcp protocol data */
    NULL,           /* extension    */
    CL_NONE,        /* type         */
    0,              /* xdev         */
    0,              /* port         */
    &Iftypes[0],    /* iftype       */
#ifdef	ENCAP
	0,				/* protocol - 12Oct2004, Maiko, IPUDP Support */
#endif
    NULLFP((struct mbuf*,struct iface*,int32,int,int,int,int)), /* (*send)   */
    NULLFP((struct iface*,char*,char*,int16,struct mbuf*)),     /* (*output) */
    NULLFP((struct iface*,struct mbuf*)),                       /* (*raw)    */
    NULLVFP((struct iface*)),                                   /* (*show)   */
    NULLFP((struct iface*,struct mbuf*)),                       /* (*discard)*/
    NULLFP((struct iface*,struct mbuf*)),                       /* (*echo)   */
    0,              /* ipsndcnt     */
    0,              /* rawsndcnt    */
    0,              /* iprecvcnt    */
    0,              /* rawrcvcnt    */
    0,              /* lastsent     */
    0,              /* lastrecv     */
};
  
#ifdef ENCAP
/* Encapsulation pseudo-interface */
struct iface Encap = {
    NULLIF,
    "encap",        /* name         */
    NULLCHAR,   /* description */
    INADDR_ANY,     /* addr         0.0.0.0 */
    0xffffffffL,    /* broadcast    255.255.255.255 */
    0xffffffffL,    /* netmask      255.255.255.255 */
    MAXINT16,       /* mtu          No limit */
    0,      /* flags    */
#ifdef NETROM
    0,      /* quality  */
    0,      /* autofloor */
#endif
    0,              /* trace        */
    NULLCHAR,       /* trfile       */
    -1,             /* trsock       */
#ifdef J2_TRACESYNC
	/* 05Nov2010, required (ugly stuff) */
	{ NULLTIMER, 0, 0, (void*)0, (void*)0, 0 },	/* trsynct */
	0,				/* trsynci */
#endif
    NULLIF,         /* forw         */
#ifdef RXECHO    
    NULLIF,         /* rxecho       */
#endif    
    NULLPROC,       /* rxproc       */
    NULLPROC,       /* txproc       */
    NULLPROC,       /* supv         */
    0,              /* dev          */
    (int32(*)__FARGS((struct iface*,int,int,int32)))NULL,/* (*ioctl)     */
    NULLFP((struct iface*,int,int32)),                   /* (*iostatus)  */
    NULLFP((struct iface*)),                             /* (*stop)      */
    NULLCHAR,       /* hwaddr       */
#ifdef AX25
    NULL,           /* ax.25 protocol data */
#endif
    &def_iftcp,     /* tcp protocol data */
    NULL,           /* extension    */
    CL_NONE,        /* type         */
    0,              /* xdev         */
    0,              /* port         */
    &Iftypes[0],    /* iftype       */
#ifdef	ENCAP
	0,				/* protocol - 12Oct2004, Maiko, IPUDP Support */
#endif
    ip_encap,       /* (*send)      */
    NULLFP((struct iface*,char*,char*,int16,struct mbuf*)),     /* (*output) */
    NULLFP((struct iface*,struct mbuf*)),                       /* (*raw)    */
    NULLVFP((struct iface*)),                                   /* (*show)   */
    NULLFP((struct iface*,struct mbuf*)),                       /* (*discard)*/
    NULLFP((struct iface*,struct mbuf*)),                       /* (*echo)   */
    0,              /* ipsndcnt     */
    0,              /* rawsndcnt    */
    0,              /* iprecvcnt    */
    0,              /* rawrcvcnt    */
    0,              /* lastsent     */
    0,              /* lastrecv     */
};
#endif /*ENCAP*/
  
char Noipaddr[] = "IP address field missing, and ip address not set\n";
  
struct cmds DFAR Ifcmds[] = {
#ifdef AX25
    { "ax25",                 ifax25,         0,      0,      NULLCHAR },
#endif
    { "broadcast",            ifbroad,        0,      2,      NULLCHAR },
    { "description",          ifdescr,        0,      2,      NULLCHAR },
    { "encapsulation",        ifencap,        0,      2,      NULLCHAR },
    { "forward",              ifforw,         0,      2,      NULLCHAR },
    { "ipaddress",            ifipaddr,       0,      2,      NULLCHAR },
    { "linkaddress",          iflinkadr,      0,      2,      NULLCHAR },
    { "mtu",                  ifmtu,          0,      2,      NULLCHAR },
    { "netmask",              ifnetmsk,       0,      2,      NULLCHAR },
#ifdef RXECHO
    { "rxecho",               ifrxecho,       0,      2,      NULLCHAR },
#endif
    { "tcp",                  doiftcp,        0,      0,      NULLCHAR },
    { NULLCHAR,		NULL,				0,		0,		NULLCHAR }
};
  
/* Set interface parameters */
int
doifconfig(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp;
  
    if(argc < 2){
        for(ifp = Ifaces;ifp != NULLIF;ifp = ifp->next)
            showiface(ifp);
        return 0;
    }
    if((ifp = if_lookup(argv[1])) == NULLIF){
        tprintf(Badinterface,argv[1]);
        return 1;
    }
    if(argc == 2){
        showiface(ifp);
        if ( ifp->show != NULLVFP((struct iface*)) ) {
            (*ifp->show)(ifp);
        }
        return 0;
    }
  
    return subcmd(Ifcmds,argc-1,&argv[1],ifp);
}
  
/* Remove iface tracing to the current output socket */
void removetrace() {
    struct iface *ifp;
    
    for(ifp=Ifaces;ifp;ifp=ifp->next)
        if(ifp->trsock == Curproc->output) {
           ifp->trace = 0;
           /* We have to close the socket, because usesock() was called !*/
           close_s(ifp->trsock);
           ifp->trsock = -1;
           
        }
};

/* Set interface IP address */
static int
ifipaddr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    /* Do not allow loopback iface to be changed ! - WG7J */
    if(ifp == &Loopback) {
        j2tputs("Cannot change IP address !\n");
        return 0;
    }
    ifp->addr = resolve(argv[1]);
    return 0;
}
  
/* Set link (hardware) address */
static int
iflinkadr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    if(ifp->iftype == NULLIFT || ifp->iftype->scan == NULL){
        j2tputs("Can't set link address\n");
        return 1;
    }
    if(ifp->hwaddr != NULLCHAR)
        free(ifp->hwaddr);
    ifp->hwaddr = mallocw((unsigned)ifp->iftype->hwalen);
    (*ifp->iftype->scan)(ifp->hwaddr,argv[1]);
#ifdef MAILBOX
#ifdef NETROM
    if(ifp == Nr_iface) /*the netrom call just got changed! - WG7J*/
        setmbnrid();
#endif
#endif
    return 0;
}
  
/* Set interface broadcast address. This is actually done
 * by installing a private entry in the routing table.
 */
static int
ifbroad(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
    struct route *rp;
  
    rp = rt_blookup(ifp->broadcast,32);
    if(rp != NULLROUTE && rp->iface == ifp)
        rt_drop(ifp->broadcast,32);
    ifp->broadcast = resolve(argv[1]);
    rt_add(ifp->broadcast,32,0L,ifp,1L,0L,1);
    return 0;
}
  
/* Set the network mask. This is actually done by installing
 * a routing entry.
 */
static int
ifnetmsk(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
    struct route *rp;
  
    /* Remove old entry if it exists */
    rp = rt_blookup(ifp->addr & ifp->netmask,mask2width(ifp->netmask));
    if(rp != NULLROUTE)
        rt_drop(rp->target,rp->bits);
  
    ifp->netmask = htol(argv[1]);
    rt_add(ifp->addr,mask2width(ifp->netmask),0L,ifp,1L,0L,0);
    return 0;
}
  
/* Command to set interface encapsulation mode */
static int
ifencap(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    if(setencap(ifp,argv[1]) != 0){
        tprintf("Encapsulation mode '%s' unknown\n",argv[1]);
        return 1;
    }
    return 0;
}
/* Function to set encapsulation mode */
int
setencap(ifp,mode)
struct iface *ifp;
char *mode;
{
    struct iftype *ift = (struct iftype*)0;
  
    if(mode != NULL) {
        /* Configure the whole interface */
        for(ift = &Iftypes[0];ift->name != NULLCHAR;ift++)
            if(strnicmp(ift->name,mode,strlen(mode)) == 0)
                break;
        if(ift->name == NULLCHAR){
            return -1;
        }
        ifp->iftype = ift;
        ifp->send = ift->send;
        ifp->output = ift->output;
        ifp->type = ift->type;
    }
    /* Set the tcp and ax25 interface parameters */
    if(!ifp->tcp)
        ifp->tcp = callocw(1,sizeof(struct iftcp));
    init_iftcp(ifp->tcp);
#ifdef AX25
	/* 05Dec08, Maiko, Better make sure 'ift' is valid ptr, or else crash */
    if (ift && ift->type == CL_AX25)
	{
        if(!ifp->ax25)
            ifp->ax25 = callocw(1,sizeof(struct ifax25));
        init_ifax25(ifp->ax25);
        ifp->flags |= AX25_BEACON|MAIL_BEACON|AX25_DIGI|LOG_AXHEARD|LOG_IPHEARD;
    }
#endif
    return 0;
}
  
#ifdef RXECHO
/* Set interface IP address */
static int
ifrxecho(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
    struct iface *rxecho;

    if(!stricmp("off",argv[1])) {
        ifp->rxecho = NULLIF;
        return 0;
    };
    if((rxecho = if_lookup(argv[1])) == NULL) {
        tprintf(Badinterface,argv[1]);
        return 0;
    };
    if(ifp->type != rxecho->type) {
        tprintf("'%s not the same type!\n",argv[1]);
        return 0;
    };
    ifp->rxecho = rxecho;
    return 0;
}
#endif /* RXECHO */  
  
/* Set interface Maximum Transmission Unit */
static int
ifmtu(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    ifp->mtu = atoi(argv[1]);
#ifdef NETROM
    /* Make sure NETROM mtu <= 236 ! - WG7J */
    if(ifp == Nr_iface)
        if(Nr_iface->mtu > NR4MAXINFO)
            Nr_iface->mtu = NR4MAXINFO;
#endif
    return 0;
}
  
/* Set interface forwarding */
static int
ifforw(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
    ifp->forw = if_lookup(argv[1]);
    if(ifp->forw == ifp)
        ifp->forw = NULLIF;
    return 0;
}
  
/*give a little description for each interface - WG7J*/
static int
ifdescr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp = p;
  
#ifdef NETROM
#ifdef ENCAP
    if((ifp == &Loopback) || (ifp == &Encap) || (ifp == Nr_iface))
#else
        if((ifp == &Loopback) || (ifp == Nr_iface))
#endif /*ENCAP*/
#else  /*NETROM*/
#ifdef ENCAP
            if((ifp == &Loopback) || (ifp == &Encap))
#else
                if(ifp == &Loopback)
#endif /*ENCAP*/
#endif /*NETROM*/
                    return 0;
  
    if(ifp->descr != NULLCHAR){
        free(ifp->descr);
        ifp->descr = NULLCHAR;        /* reset the pointer */
    }
    if(!strlen(argv[1]))
        return 0;           /* clearing the buffer */
  
    ifp->descr = mallocw(strlen(argv[1])+5); /* allow for the EOL char etc */
    strcpy(ifp->descr, argv[1]);
    strcat(ifp->descr, "\n");         /* add the EOL char */
  
    return 0;
}
  
/* Display the parameters for a specified interface */
static void
showiface(ifp)
register struct iface *ifp;
{
    char tmp[25];
  
    tprintf("%-8s IP addr %s MTU %u Link encap ",ifp->name,
    inet_ntoa(ifp->addr),(int)ifp->mtu);
    if(ifp->iftype == NULLIFT){
        j2tputs("not set\n");
    } else {
        tprintf("%s\n",ifp->iftype->name);
        if(ifp->iftype->format != NULL && ifp->hwaddr != NULLCHAR) {
            tprintf("         Link addr %s",
            (*ifp->iftype->format)(tmp,ifp->hwaddr));
#ifdef AX25
            if(ifp->iftype->type == CL_AX25) {
#ifdef MAILBOX
                tprintf("   BBS %s",pax25(tmp,ifp->ax25->bbscall));
#endif
                if(ifp->ax25->cdigi[0])
                    tprintf("   Cdigi %s",pax25(tmp,ifp->ax25->cdigi));

                tprintf("   Paclen %d   Irtt %d\n",
			(int)ifp->ax25->paclen, ifp->ax25->irtt);

                if(ifp->ax25->bctext)
                    tprintf("         BCText: %s\n",ifp->ax25->bctext);
#ifdef AXIP
                paxipdest(ifp);  /* only prints if axip encap */
#endif
            }
#endif
#ifdef NETROM
            else if(ifp == Nr_iface) {
                tprintf("   Alias %s\n",Nralias);
            }
#endif
#if((defined AX25) || (defined NETROM))
            else
#endif
                tputc('\n');
        }
    }
    tprintf("         flags 0x%x trace 0x%x netmask 0x%08x broadcast %s\n",
    ifp->flags,(int)ifp->trace,ifp->netmask,inet_ntoa(ifp->broadcast));
    if(ifp->forw != NULLIF)
        tprintf("         output forward to %s\n",ifp->forw->name);
#ifdef RXECHO    
    if(ifp->rxecho != NULLIF)
        tprintf("         rx packets echoed to %s\n",ifp->rxecho->name);
#endif
	/* 12Oct2009, Maiko, Use "%d" for int32 vars */
    tprintf("         sent: ip %d tot %d idle %s\n",
    ifp->ipsndcnt,ifp->rawsndcnt,tformat(secclock() - ifp->lastsent));
    tprintf("         recv: ip %d tot %d idle %s\n",
    ifp->iprecvcnt,ifp->rawrecvcnt,tformat(secclock() - ifp->lastrecv));
    if(ifp->descr != NULLCHAR)
        tprintf("         descr: %s",ifp->descr);
  
}
  
/* Command to detach an interface */
int
dodetach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct iface *ifp;
  
    if((ifp = if_lookup(argv[1])) == NULLIF){
        tprintf(Badinterface,argv[1]);
        return 1;
    }
    if(if_detach(ifp) == -1)
        j2tputs("Can't detach loopback or encap interface\n");
    return 0;
}

/* Detach a specified interface */
int
if_detach(ifp)
struct iface *ifp;
{
    struct iface *iftmp;
    struct route *rp,*rptmp;
#ifdef AX25
    struct ax25_cb *axcb, *axcbnext;
    struct ax_route *axr, *axr1;
#endif /* AX25 */
    struct arp_tab *ap, *ap1;
    struct mbuf *bp, *bpnext, *bplast = (struct mbuf*)0;
    struct phdr phdr;
    int i,j;
#ifdef TRACE
    extern int stdoutSock;
#endif

    if(ifp == &Loopback)
        return -1;

#ifdef	ENCAP
    if (ifp != &Encap)
	{
#endif
  
#ifdef AX25
#ifdef NETROM
    if_nrdrop(ifp);   /* undo 'netrom interface xxx' command */
#endif /* NETROM */

    /* Drop all AX.25 connections that point to this interface - K2MF */
    for(axcb = Ax25_cb; axcb != NULLAX25; axcb = axcbnext)
	{
        axcbnext = axcb->next;

        if(axcb->iface == ifp)
		{
            j2shutdown(axcb->user,2);
            pwait(NULL);  /* allow disconnect processing to occur */
        }
    }

    /* Drop all ax25 routes that point to this interface */
    for(axr = Ax_routes; axr != NULLAXR; axr = axr1)
	{
        axr1 = axr->next;/* Save the next pointer */
        if(axr->iface == ifp)
            ax_drop(axr->target, ifp, 1);
        /* axr will be undefined after ax_drop() */
    }
#endif
  
    /* Drop all ARP's that point to this interface */
    for(i = 0; i < HASHMOD; ++i)
	{
        for(ap = Arp_tab[i]; ap != NULLARP; ap = ap1)
		{
            ap1 = ap->next; /* Save the next pointer */
            if(ap->iface == ifp)
                arp_drop(ap);
 	       /* ap will be undefined after arp_drop() */
        }
	}

#ifdef	ENCAP
    }	/* end of (ifp != &Encap) */
#endif
  
    /* Drop all routes that point to this interface */
    if(R_default.iface == ifp)
        rt_drop(0L,0);  /* Drop default route */
  
    for(i=0;i<HASHMOD;i++){
        for(j=0;j<32;j++){
            for(rp = Routes[j][i];rp != NULLROUTE;rp = rptmp){
                /* Save next pointer in case we delete this entry */
                rptmp = rp->next;
                if(rp->iface == ifp)
                    rt_drop(rp->target,rp->bits);
            }
        }
    }
#ifdef ENCAP
    if(ifp == &Encap) return(0);  /* n5knx: now merely return */
#endif

    /* Unforward any other interfaces forwarding to this one */
    for(iftmp = Ifaces;iftmp != NULLIF;iftmp = iftmp->next){
        if(iftmp->forw == ifp)
            iftmp->forw = NULLIF;
#ifdef RXECHO    
        if(iftmp->rxecho == ifp)
            iftmp->rxecho = NULLIF;
#endif
    }
  
    /* Call device shutdown routine, if any */
    if(ifp->stop != NULLFP((struct iface*)))
        (*ifp->stop)(ifp);
  
    killproc(ifp->rxproc);
    killproc(ifp->txproc);
    killproc(ifp->supv);
  
    /* Search Hopper for any packets from this interface, and remove them */
#ifdef HDEBUG
    j=0;
#endif
    i = dirps();
    for(bp=Hopper; bp; bplast=bp, bp=bpnext) {
        bpnext=bp->anext;
        pullup(&bp,(char *)&phdr,sizeof(phdr));
        if(phdr.iface == ifp) {  /* remove pkt from Hopper */
            if(bp != Hopper)
                bplast->anext = bp->anext;
            else
                Hopper = bp->anext;
            bp->anext = NULLBUF;
            free_p(bp);
#ifdef HDEBUG
            j++;
#endif
        }
        else {  /* put it back just like it was */
            bp=pushdown(bp, sizeof(phdr));  /* make room for phdr */
            memcpy(bp->data,(char *)&phdr,sizeof(phdr));
        }
    }
    restore(i);
#ifdef HDEBUG
    log(-1,"%d pkts zapped in Hopper", j);
#endif

    /* Free allocated memory associated with this interface */
    free(ifp->name);
    free(ifp->hwaddr);
    free(ifp->tcp);
#ifdef AX25
    if(ifp->ax25) {
        free(ifp->ax25->bctext);
        free(ifp->ax25);
    }
#endif
    free(ifp->descr);
  
#ifdef TRACE
    if (ifp->trsock != stdoutSock)   /* from K2MF */
        close_s(ifp->trsock);
    free(ifp->trfile);
#endif

    /* Remove from interface list */
    if(ifp == Ifaces){
        Ifaces = ifp->next;
    } else {
        /* Search for entry just before this one
         * (necessary because list is only singly-linked.)
         */
        for(iftmp = Ifaces;iftmp != NULLIF ;iftmp = iftmp->next)
            if(iftmp->next == ifp)
                break;
        if(iftmp != NULLIF && iftmp->next == ifp)
            iftmp->next = ifp->next;
    }
    /* Finally free the structure itself */
    free((char *)ifp);
    return 0;
}
  
/* Given the ascii name of an interface, return a pointer to the structure,
 * or NULLIF if it doesn't exist
 */
struct iface *if_lookup (char *name)
{
    register struct iface *ifp;
  
    for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
        if(stricmp(ifp->name,name) == 0)
            break;
    return ifp;
}

#ifdef JNOS20_SOCKADDR_AX

/*
 * 30Aug2010, Maiko (VE4KLM), Support functions for changes I made to
 * the 'struct sockaddr_ax' defined in 'ax25.h' header file.
 * 03Sep2010, Maiko, Changed to return a char value, not int value.
 */

char if_indexbyname (char *name_c)
{
    register struct iface *ifp;
	register char cnt_i = 0;
	char index_i = -1;
  
    for (ifp = Ifaces; ifp != NULLIF; ifp = ifp->next, cnt_i++)
	{
        if (stricmp (ifp->name, name_c) == 0)
		{
			index_i = cnt_i;
            break;
		}
	}

    return index_i;
}

struct iface *if_lookup2 (char index_i)
{
    register struct iface *ifp;

	register char cnt_i = 0;
  
    for (ifp = Ifaces; ifp != NULLIF; ifp = ifp->next, cnt_i++)
	{
        if (cnt_i == index_i)
            break;
	}

	return ifp;
}

#endif	/* end of JNOS20_SOCKADDR_AX */

/* Return iface pointer if 'addr' belongs to one of our interfaces,
 * NULLIF otherwise.
 * This is used to tell if an incoming IP datagram is for us, or if it
 * has to be routed.
 */
struct iface *
ismyaddr(addr)
int32 addr;
{
    register struct iface *ifp;
  
    for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
        if(addr == ifp->addr)
            break;
    return ifp;
}
  
/* Given a network mask, return the number of contiguous 1-bits starting
 * from the most significant bit.
 */
int
mask2width(mask)
int32 mask;
{
    int width,i;
  
    width = 0;
    for(i = 31;i >= 0;i--){
        if(!(mask & (1L << i)))
            break;
        width++;
    }
    return width;
}
  
/* return buffer with name + comment */
char *
if_name(ifp,comment)
struct iface *ifp;
char *comment;
{
    char *result = mallocw( strlen(ifp->name) + strlen(comment) + 1 );
    strcpy( result, ifp->name );
    return strcat( result, comment );
}
  
/* Raw output routine that tosses all packets. Used by dialer, tip, etc */
int
bitbucket(ifp,bp)
struct iface *ifp;
struct mbuf *bp;
{
    free_p(bp);
    return 0;
}
