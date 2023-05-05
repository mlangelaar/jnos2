/* ARP commands
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Mods by G1EMM
 *
 * Mods by SM6RPZ
 * 1992-05-28 - Added interface to "arp add ..."-command.
 * 1992-07-26 - Small cosmetic changes here and there.
 */
#include <ctype.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "internet.h"
#include "ip.h"
#include "enet.h"
#include "ax25.h"
#include "arp.h"
#include "netuser.h"
#include "cmdparse.h"
#include "commands.h"
#include "iface.h"
#include "rspf.h"
#include "socket.h"
#include "domain.h"
#include "session.h"
  
int Arp_Sort=1;  /* set Initial sort mode */
  
static int doarpsort __ARGS((int argc,char *argv[],void *p));
static void make_arp_string __ARGS((struct arp_tab *ap,char *buf));
static int doarpadd __ARGS((int argc,char *argv[],void *p));
static int doarpdrop __ARGS((int argc,char *argv[],void *p));
static int doarpflush __ARGS((int argc,char *argv[],void *p));
static int doarppoll __ARGS((int argc,char *argv[],void *p));
static int doarpeaves __ARGS((int argc,char *argv[],void *p));
static int doarpqueue __ARGS((int argc,char *argv[],void *p));
static void dumparp __ARGS((void));
  
static struct cmds DFAR Arpcmds[] = {
    { "add", doarpadd, 0, 4,
    "arp add <hostid> ether|ax25|netrom|arcnet|mac <hardware addr> <iface>" },
  
    { "drop", doarpdrop, 0, 3,
    "arp drop <hostid> ether|ax25|netrom|arcnet|mac <iface>" },
  
    { "eaves", doarpeaves, 0, 0, NULLCHAR },
  
    { "flush", doarpflush, 0, 0,
    NULLCHAR },
  
    { "maxq", doarpqueue, 0, 0, NULLCHAR },
  
    { "poll", doarppoll, 0, 0, NULLCHAR },
  
    { "publish", doarpadd, 0, 4,
    "arp publish <hostid> ether|ax25|netrom|arcnet|mac <hardware addr> <iface>" },
  
    { "sort",    doarpsort, 0, 0,
    NULLCHAR },
  
    { NULLCHAR,	NULL,	0, 0, NULLCHAR }
};
  
char *Arptypes[] = {
    "NET/ROM",
    "10 Mb Ethernet",
    "3 Mb Ethernet",
    "AX.25",
    "Pronet",
    "Chaos",
    "",
    "Arcnet",
    "Appletalk"
};
  
int
doarp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2){
        dumparp();
        return 0;
    }
    return subcmd(Arpcmds,argc,argv,p);
}
  
static int
doarpadd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 hardware;
    int32 addr;
    char *hwaddr;
    struct arp_tab *ap;
    struct arp_type *at;
    int pub = 0;
    struct iface *iface;
  
    if(argv[0][0] == 'p')   /* Is this entry published? */
        pub = 1;
    if((addr = resolve(argv[1])) == 0){
        tprintf(Badhost,argv[1]);
        return 1;
    }
    if(argc == 4 && tolower(argv[2][0]) != 'n') {
        tprintf("Usage: arp %s <hostid> ether|ax25|netrom|arcnet|mac <hardware addr> <iface>\n",
        pub ? "publish" : "add");
        return -1;
    }
    /* This is a kludge. It really ought to be table driven */
    switch(tolower(argv[2][0])){
        case 'n':       /* Net/Rom pseudo-type */
            argc = 5;
            argv[4] = "netrom";     /* Force use of netrom interface */
            hardware = ARP_NETROM;
            break;
        case 'e':       /* "ether" */
            hardware = ARP_ETHER;
            break;
        case 'a':       /* "ax25" */
        switch(tolower(argv[2][1])) {
            case 'x':
                hardware = ARP_AX25;
                break;
            case 'r':
                hardware = ARP_ARCNET;
                break;
            default:
                tprintf("Usage: arp %s <hostid> ether|ax25|netrom|arcnet|mac <hardware addr> <iface>\n",
                pub ? "publish" : "add");
                return -1;
        }
            break;
        case 'm':       /* "mac appletalk" */
            hardware = ARP_APPLETALK;
            break;
        default:
            tprintf("Usage: arp %s <hostid> ether|ax25|netrom|arcnet|mac <hardware addr> <iface>\n",
            pub ? "publish" : "add");
            return -1;
    }
    if((iface = if_lookup(argv[4])) == NULLIF) {
        tprintf("No such interface %s\n", argv[4]);
        return 1;
    }
    /* If an entry already exists, clear it */
    if((ap = arp_lookup(hardware,addr,iface)) != NULLARP)
        arp_drop(ap);
  
    at = &Arp_type[hardware];
    if(at->scan == NULLFP((char*,char*))){
        j2tputs("Attach device first\n");
        return 1;
    }
    /* Allocate buffer for hardware address and fill with remaining args */
    hwaddr = mallocw((unsigned)at->hwalen);
    /* Destination address */
    (*at->scan)(hwaddr,argv[3]);
    ap = arp_add(addr,hardware,hwaddr,pub,iface);   /* Put in table */
    free(hwaddr);                                   /* Clean up */
    stop_timer(&ap->timer);                     /* Make entry permanent */
    set_timer(&ap->timer,0);
#ifdef  RSPF
    rspfarpupcall(addr,hardware,NULLIF);  /* Do a RSPF upcall */
#endif  /* RSPF */
    return 0;
}
  
static int
doarpeaves(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    return setflag(argc,argv[1],ARP_EAVESDROP,argv[2]);
}
  
static int
doarppoll(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    return setflag(argc,argv[1],ARP_KEEPALIVE,argv[2]);
}
  
int Maxarpq = 5;
  
static int
doarpqueue(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    return setint(&Maxarpq,"Max queue",argc,argv);
}
  
  
/* Remove an ARP entry */
static int
doarpdrop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 hardware;
    int32 addr;
    struct arp_tab *ap;
    struct iface *iface;
  
    if((addr = resolve(argv[1])) == 0){
        tprintf(Badhost,argv[1]);
        return 1;
    }
    if(argc == 3 && tolower(argv[2][0]) != 'n') {
        j2tputs("Usage: arp drop <hostid> ether|ax25|netrom|arcnet|mac <iface>\n");
        return -1;
    }
    /* This is a kludge. It really ought to be table driven */
    switch(tolower(argv[2][0])){
        case 'n':
            argc = 4;
            argv[3] = "netrom";     /* Force use of netrom interface */
            hardware = ARP_NETROM;
            break;
        case 'e':       /* "ether" */
            hardware = ARP_ETHER;
            break;
        case 'a':       /* "ax25" */
        switch(tolower(argv[2][1])) {
            case 'x':
                hardware = ARP_AX25;
                break;
            case 'r':
                hardware = ARP_ARCNET;
                break;
            default:
                j2tputs("Usage: arp drop <hostid> ether|ax25|netrom|arcnet|mac <iface>\n");
                return -1;
        }
            break;
        case 'm':       /* "mac appletalk" */
            hardware = ARP_APPLETALK;
            break;
        default:
            j2tputs("Usage: arp drop <hostid> ether|ax25|netrom|arcnet|mac <iface>\n");
            return -1;
    }
    if((iface = if_lookup(argv[3])) == NULLIF) {
        tprintf("No such interface %s\n", argv[3]);
        return 1;
    }
    if((ap = arp_lookup(hardware,addr,iface)) == NULLARP)
        return -1;
    arp_drop(ap);
    return 0;
}
  
/* Flush all automatic entries in the arp cache */
static int
doarpflush(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct arp_tab *ap;
    struct arp_tab *aptmp;
    int i;
  
    for(i=0;i<HASHMOD;i++){
        for(ap = Arp_tab[i];ap != NULLARP;ap = aptmp){
            aptmp = ap->next;
            if(dur_timer(&ap->timer) != 0)
                arp_drop(ap);
        }
    }
    return 0;
}
  
/* Dump ARP table */
static void
dumparp()
{
    register int i,j,k,flow_tmp;
    register struct arp_tab *ap;
    char *temp;
  
    flow_tmp=Current->flowmode;
    Current->flowmode=1;
  
    tprintf("received %u badtype %u bogus addr %u reqst in %u replies %u reqst out %u\n",
    Arp_stat.recv,Arp_stat.badtype,Arp_stat.badaddr,Arp_stat.inreq,
    Arp_stat.replies,Arp_stat.outreq);
  
    for(i=0,j=0;i<HASHMOD;i++)
        for(ap = Arp_tab[i];ap != (struct arp_tab *)NULL;ap = ap->next,j++);
  
    if (j)
	{
        j2tputs("IP addr         Type           Time Q Address           Interface\n");
  
		/* 22Dec2005, Maiko, Changed malloc() to mallocw() instead ! */
        if ((temp=mallocw((unsigned)j*80)) == NULLCHAR)
		{
            j2tputs(Nospace);
            return;
        }
  
        for(i=0,k=0;i<HASHMOD;i++) {
            for(ap = Arp_tab[i];ap != (struct arp_tab *)NULL;ap = ap->next,k+=80)
                make_arp_string(ap,&temp[k]);
        }
  
#ifdef UNIX
        if (Arp_Sort)
		{
		
			log (-1, "calling j2qsort, %d entries", j);

			j2qsort(temp,(size_t)j,80,
				(int (*)(const void*,const void*)) strcmp);
		}
#else
        if (Arp_Sort) qsort(temp,(size_t)j,80,(int (*) ()) strcmp);
#endif
  
        for(i=0,k=4;i<j;i++,k+=80) {
            j2tputs(&temp[k]);
            if(tputc('\n') == EOF)  break;
        }
        free(temp);
    }
  
    Current->flowmode=flow_tmp;
}
  
void
make_arp_string(ap,buf)
register struct arp_tab *ap;
char *buf;
{
    char e[128];
    unsigned a=0;
    char *name;
  
    if(DTranslate && (name = resolve_a(ap->ip_addr,!DVerbose)) != NULLCHAR) {
        strcpy(buf, name);
        a+=4;
        free(name);
    } else {
        a=sprintf(buf,"%4.4s",inet_ntobos(ap->ip_addr));
    }
  
    a+=sprintf(&buf[a],"%-15.15s ",inet_ntoa(ap->ip_addr));
    a+=sprintf(&buf[a],"%-14.14s ",smsg(Arptypes,NHWTYPES,(unsigned)ap->hardware));
    a+=sprintf(&buf[a],"%4d ",read_timer(&ap->timer)/1000);
  
    if(ap->state == ARP_PENDING)
        a+=sprintf(&buf[a],"%1.1u ",len_q(ap->pending));
    else
        a+=sprintf(&buf[a],"  ");
  
    if(ap->state == ARP_VALID){
        if(Arp_type[ap->hardware].format != NULL){
            (*Arp_type[ap->hardware].format)(e,ap->hw_addr);
        } else {
            e[0]='\0';
        }
        a+=sprintf(&buf[a],"%-17.17s ",e);
    } else {
        a+=sprintf(&buf[a],"[unknown]         ");
    }
  
    if (ap->iface)
        a+=sprintf(&buf[a],"%-6.6s ",ap->iface->name);
  
    if(ap->pub)
        a+=sprintf(&buf[a],"(published)");
  
    return;
}
  
/* Sort ARP dump */
static int
doarpsort(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
    extern int Arp_Sort;
  
    return setbool(&Arp_Sort,"ARP Sort flag",argc,argv);
}
  
