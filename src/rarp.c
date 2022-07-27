/* Reverse Address Resolution Protocol (ARP) functions. Sits between IP and
 * Level 2, mapping Level 2 addresses to IP.
 */
#include "global.h"
#ifdef RARP
#include "mbuf.h"
#include "proc.h"
#include "timer.h"
#include "iface.h"
#include "socket.h"
#include "ax25.h"
#include "arp.h"
#include "netuser.h"
#include "cmdparse.h"
#include "pktdrvr.h"
  
struct arp_stat Rarp_stat;
static int Rwaiting = 0;        /* Semaphore used when waiting for a reply */
/* static void arp_output __ARGS((struct iface *iface,int16 hardware,char *hwaddr,char *target)); */
static int dorarpquery __ARGS((int argc,char *argv[],void *p));
static void rarpstat __ARGS((void));
static void rarp_output __ARGS((struct iface *iface,int16 hardware,char *hwaddr,char *target));

/* 05Dec2020, Maiko, compiler doesn't like missing braces, what's with the 'fixed' ? */  
static struct cmds Rarpcmds[] = {
    { "query", dorarpquery, 0, 3 },
#ifdef fixed
    { "rarp query <interface> <ether addr|callsign> [<ether addr|callsign>]" },
#else
    { "rarp query <interface> <callsign> [<callsign>]" },
#endif
    { NULLCHAR, NULL, 0, 0 }
};
  
int
dorarp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2){
        rarpstat();
        return 0;
    }
    return subcmd(Rarpcmds,argc,argv,p);
}
  
static int
dorarpquery(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp;
    char hwaddr[MAXHWALEN], qhwaddr[MAXHWALEN];
    int16 hardware, t;
    static char *errmsg = "Illegal hardware address\n";
    if((ifp = if_lookup(argv[1])) == NULLIF){
        tprintf(Badinterface,argv[1]);
        return -1;
    }
    switch(ifp->iftype->type) {
        case CL_AX25:
            hardware = ARP_AX25;
            break;
#ifdef fixed
        case CL_ETHERNET:
            hardware = ARP_ETHER;
            break;
        default:
            j2tputs("Only Ethernet and AX25 interfaces allowed\n");
#else
        default:
            j2tputs("Only AX25 interfaces allowed\n");
#endif
            return -1;
    }
    if((*ifp->iftype->scan)(hwaddr,argv[2]) == -1) {
        j2tputs(errmsg);
        return -1;
    }
    if(argc > 3 && (*ifp->iftype->scan)(qhwaddr,argv[3]) == -1) {
        j2tputs(errmsg);
        return -1;
    }
    if(argc == 3)
        memcpy(qhwaddr,hwaddr,ifp->iftype->hwalen);
    rarp_output(ifp,hardware,hwaddr,qhwaddr);
  
    t = Arp_type[hardware].pendtime;
    ++Rwaiting;
    tprintf("Trying...   %2d",t);
    while(t--) {
        j2alarm(1000);
        if(pwait(&Rwaiting) != EALARM) {
            j2alarm(0);
            --Rwaiting;
            return 0;
        }
        tprintf("\b\b%2d",t);
    }
    j2tputs("\b\bTimeout.\n");
    --Rwaiting;
    return 0;
}
  
/* Handle incoming RARP packets according to RFC 903.
 */
void
rarp_input(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
    struct arp rarp;
    struct arp_type *at;
    char shwaddr[MAXHWALEN];
  
    Rarp_stat.recv++;
    if(ntoharp(&rarp,&bp) == -1)    /* Convert into host format */
        return;
    if(rarp.hardware >= NHWTYPES){
        /* Unknown hardware type, ignore */
        Rarp_stat.badtype++;
        return;
    }
    at = &Arp_type[rarp.hardware];
    if(rarp.protocol != at->iptype){
        /* Unsupported protocol type, ignore */
        Rarp_stat.badtype++;
        return;
    }
    if((int16)uchar(rarp.hwalen) > MAXHWALEN || uchar(rarp.pralen) != sizeof(int32)){
        /* Incorrect protocol addr length (different hw addr lengths
         * are OK since AX.25 addresses can be of variable length)
         */
        Rarp_stat.badlen++;
        return;
    }
    if(memcmp(rarp.shwaddr,at->bdcst,at->hwalen) == 0){
        /* This guy is trying to say he's got the broadcast address! */
        Rarp_stat.badaddr++;
        return;
    }
    if(rarp.opcode == REVARP_REQUEST) {
        /* We are not a server, so we can only answer requests for a
         * hardware address that is our own. But would be possible to
         * use the ARP table to answer requests for someone elses
         * IP address.
         */
        if(memcmp(rarp.thwaddr,iface->hwaddr,at->hwalen) == 0) {
            memcpy(shwaddr,rarp.shwaddr,at->hwalen);
            /* Mark the end of the sender's AX.25 address
             * in case he didn't
             */
            if(rarp.hardware == ARP_AX25)
                rarp.thwaddr[uchar(rarp.hwalen)-1] |= E;
            memcpy(rarp.shwaddr,iface->hwaddr,at->hwalen);
            rarp.sprotaddr = iface->addr;
            rarp.tprotaddr = iface->addr;
            rarp.opcode = REVARP_REPLY;
  
            if((bp = htonarp(&rarp)) == NULLBUF)
                return;
  
            if(iface->forw != NULLIF)
                (*iface->forw->output)(iface->forw,shwaddr,
                iface->forw->hwaddr,at->rarptype,bp);
            else
                (*iface->output)(iface,shwaddr,
                iface->hwaddr,at->rarptype,bp);
            Rarp_stat.inreq++;
        }
    } else {
        Rarp_stat.replies++;
        if(Rwaiting) {
            j2psignal(&Rwaiting,1);
            tprintf("\nRARP Reply: %s %s\n",
            (*at->format)(shwaddr,rarp.thwaddr),
            inet_ntoa(rarp.tprotaddr));
        }
    }
}
  
/* Send a RARP request to target to resolve hardware address hwaddr */
static void
rarp_output(iface,hardware,hwaddr,target)
struct iface *iface;
int16 hardware;
char *hwaddr;
char *target;
{
    struct arp rarp;
    struct mbuf *bp;
    struct arp_type *at;
  
    at = &Arp_type[hardware];
    if(iface->output == NULLFP((struct iface *,char *,char *,int16,struct mbuf *)))
        return;
  
    rarp.hardware = hardware;
    rarp.protocol = at->iptype;
    rarp.hwalen = at->hwalen;
    rarp.pralen = sizeof(int32);
    rarp.opcode = REVARP_REQUEST;
    memcpy(rarp.shwaddr,iface->hwaddr,at->hwalen);
    rarp.sprotaddr = 0;
    memcpy(rarp.thwaddr,hwaddr,at->hwalen);
    rarp.tprotaddr = 0;
    if((bp = htonarp(&rarp)) == NULLBUF)
        return;
    (*iface->output)(iface,target,
    iface->hwaddr,at->rarptype,bp);
    Rarp_stat.outreq++;
}
  
static void
rarpstat()
{
    tprintf("received %u badtype %u bogus addr %u reqst in %u replies %u reqst out %u\n",
    Rarp_stat.recv,Rarp_stat.badtype,Rarp_stat.badaddr,Rarp_stat.inreq,
    Rarp_stat.replies,Rarp_stat.outreq);
}
#endif /* RARP */
  
