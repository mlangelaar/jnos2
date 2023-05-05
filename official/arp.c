/* Address Resolution Protocol (ARP) functions. Sits between IP and
 * Copyright 1991 Phil Karn, KA9Q
 * Level 2, mapping IP to Level 2 addresses for all outgoing datagrams.
 *
 * Mods by G1EMM
 *
 * Mods by SM6RPZ
 * 1992-05-28 - Added interface to arp_lookup() and arp_add().
 * 1992-07-07 - Added arp_timeout(). This function now works according to the
 *              last page in RFC826. When an ARP entry times out we will first
 *              try an ARP reply before deleting the entry.
 * 1992-07-08 - We will update the ARP table and create new entries each time
 *              we hear an ARP request message on the channel. By doing so we
 *              will gain knowledge of other stations hardware addresses
 *              whithout sending a lot of ARP requests. If we are running RSPF,
 *              it will be informed about these "findings".
 * 1992-07-11 - Datagrams to be sent while awaiting an resolution will be put
 *              on a queue. The length of this queue in datagrams is defined
 *              in arp.h (ARP_QUEUE).
 *
 * 1992-10-27 The above 3 behaviours are now configurable via additional
 *      arp subcommands - WG7J
 *
 */
  
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "enet.h"
#include "ax25.h"
#include "icmp.h"
#include "ip.h"
#include "arp.h"
#include "icmp.h"
#include "rspf.h"
  
extern int Maxarpq;
  
static void arp_output __ARGS((struct iface *iface,int16 hardware,int32 target,char *hw_addr));
static void arp_timeout __ARGS((void *p));  /* sm6rpz */
  
/* Hash table headers */
struct arp_tab *Arp_tab[HASHMOD];
struct arp_stat Arp_stat;
  
/* Resolve an IP address to a hardware address; if not found,
 * initiate query and return NULLCHAR.  If an address is returned, the
 * interface driver may send the packet; if NULLCHAR is returned,
 * res_arp() will have saved the packet on its pending queue,
 * so no further action (like freeing the packet) is necessary.
 */
char *
res_arp(iface,hardware,target,bp)
struct iface *iface;    /* Pointer to interface block */
int16 hardware;         /* Hardware type */
int32 target;           /* Target IP address */
struct mbuf *bp;        /* IP datagram to be queued if unresolved */
{
    register struct arp_tab *arp;
    struct ip ip;
  
    if((arp = arp_lookup(hardware,target,iface)) != NULLARP && arp->state == ARP_VALID)
        return arp->hw_addr;
    if(arp != NULLARP){
        if(len_q(arp->pending) > Maxarpq) {
            /* Earlier packets are already pending, kick
             * this one back as a source quench
             */
            ntohip(&ip,&bp);
            icmp_output(&ip,bp,ICMP_QUENCH,0,NULL);
            free_p(bp);
        } else
            enqueue(&arp->pending,bp);
    } else {
        /* Create an entry and put the datagram on the
         * queue pending an answer
         */
        arp = arp_add(target,hardware,NULLCHAR,0,iface);
        enqueue(&arp->pending,bp);
        arp_output(iface,hardware,target,NULLCHAR);
    }
    return NULLCHAR;
}
  
/* Handle incoming ARP packets. This is almost a direct implementation of
 * the algorithm on page 5 of RFC 826, except for:
 * 1. Outgoing datagrams to unresolved addresses are kept on a queue
 *    pending a reply to our ARP request.
 * 2. The names of the fields in the ARP packet were made more mnemonic.
 * 3. Requests for IP addresses listed in our table as "published" are
 *    responded to, even if the address is not our own.
 */
void
arp_input(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
    struct arp arp;
    struct arp_tab *ap;
    struct arp_type *at;
    int i, found;
    int32 wanted;
  
    Arp_stat.recv++;
    if(ntoharp(&arp,&bp) == -1)     /* Convert into host format */
        return;
    if(arp.hardware >= NHWTYPES){
        /* Unknown hardware type, ignore */
        Arp_stat.badtype++;
        return;
    }
    at = &Arp_type[arp.hardware];
    if(arp.protocol != at->iptype){
        /* Unsupported protocol type, ignore */
        Arp_stat.badtype++;
        return;
    }
    if((int16)uchar(arp.hwalen) > MAXHWALEN || uchar(arp.pralen) != sizeof(int32)){
        /* Incorrect protocol addr length (different hw addr lengths
         * are OK since AX.25 addresses can be of variable length)
         */
        Arp_stat.badlen++;
        return;
  
    }
    if(arp.sprotaddr == 0 || arp.tprotaddr == 0)
    {
        /* We are going to dead-end references for [0.0.0.0], since
         * experience shows that these cause total lock up -- N1BEE
         */
        Arp_stat.badaddr++;
        return;
    }
    if(memcmp(arp.shwaddr,at->bdcst,at->hwalen) == 0){
        /* This guy is trying to say he's got the broadcast address! */
        Arp_stat.badaddr++;
        return;
    }
    /* Try to refine ARP according to section 5.7 in Douglas E.
     * Comers book "Internetworking With TCP/IP", 2nd ed. page 77.
     * I.e we will use all ARP-packets we can see thereby lessen
     * the ARP traffic a little. -- sm6rpz
     */
    if(((ap = arp_lookup(arp.hardware,arp.sprotaddr,iface)) != NULLARP
        && dur_timer(&ap->timer) != 0)
        || ( (iface->flags & ARP_EAVESDROP) &&
        ap == NULLARP && arp.opcode != REVARP_REQUEST)
    ) {
        ap = arp_add(arp.sprotaddr,arp.hardware,arp.shwaddr,0,iface);
    }
    /* See if we're the address they're looking for */
    if(ismyaddr(arp.tprotaddr) != NULLIF){
        if(ap == NULLARP)   /* Only if not already in the table */
            arp_add(arp.sprotaddr,arp.hardware,arp.shwaddr,0,iface);
  
        if(arp.opcode == ARP_REQUEST){
            /* Swap sender's and target's (us) hardware and protocol
             * fields, and send the packet back as a reply
             */
            memcpy(arp.thwaddr,arp.shwaddr,(int16)uchar(arp.hwalen));
            /* Mark the end of the sender's AX.25 address
             * in case he didn't
             */
            if(arp.hardware == ARP_AX25)
                arp.thwaddr[uchar(arp.hwalen)-1] |= E;
  
            memcpy(arp.shwaddr,iface->hwaddr,at->hwalen);
            wanted = arp.tprotaddr;
            arp.tprotaddr = arp.sprotaddr;
/*            arp.sprotaddr = iface->addr;*/
            /* reply with what was asked for ! From Mike Galagher */
            arp.sprotaddr = wanted;
            arp.opcode = ARP_REPLY;
            if((bp = htonarp(&arp)) == NULLBUF)
                return;
  
            if(iface->forw != NULLIF)
                (*iface->forw->output)(iface->forw,
                arp.thwaddr,iface->forw->hwaddr,at->arptype,bp);
            else
                (*iface->output)(iface,arp.thwaddr,
                iface->hwaddr,at->arptype,bp);
            Arp_stat.inreq++;
#ifdef  RSPF
            /* Do an RSPF upcall */
            rspfarpupcall(arp.tprotaddr,arp.hardware,NULLIF);
#endif  /* RSPF*/
        } else {
            Arp_stat.replies++;
#ifdef  RSPF
            /* Do an RSPF upcall */
            rspfarpupcall(arp.sprotaddr,arp.hardware,iface);
#endif  /* RSPF*/
        }
    } else if(arp.opcode == ARP_REQUEST
        && (ap = arp_lookup(arp.hardware,arp.tprotaddr,iface)) != NULLARP
    && ap->pub){
        /* Otherwise, respond if the guy he's looking for is
         * published in our table.
         */
        memcpy(arp.thwaddr,arp.shwaddr,(int16)uchar(arp.hwalen));
        memcpy(arp.shwaddr,ap->hw_addr,at->hwalen);
        arp.tprotaddr = arp.sprotaddr;
        arp.sprotaddr = ap->ip_addr;
        arp.opcode = ARP_REPLY;
        if((bp = htonarp(&arp)) == NULLBUF)
            return;
        if(iface->forw != NULLIF)
            (*iface->forw->output)(iface->forw,
            arp.thwaddr,iface->forw->hwaddr,at->arptype,bp);
        else
            (*iface->output)(iface,arp.thwaddr,
            iface->hwaddr,at->arptype,bp);
        Arp_stat.inreq++;
    }
	else if(arp.opcode == REVARP_REQUEST)
	{
		found = 0;	/* 01Dec2004, Maiko, Replaced 'found:' GOTO & label */

        for(i=0;i<HASHMOD;i++)
		{
            for(ap = Arp_tab[i];ap != NULLARP;ap = ap->next)
			{
                if(memcmp(ap->hw_addr,arp.thwaddr,at->hwalen) == 0)
				{
					found = 1;
                    break;
				}
			}

			if (found)
				break;
		}

		if (ap != NULLARP && ap->pub)
		{
            memcpy(arp.shwaddr,iface->hwaddr,at->hwalen);
            arp.tprotaddr = ap->ip_addr;
            arp.sprotaddr = iface->addr;
            arp.opcode = REVARP_REPLY;
            if((bp = htonarp(&arp)) == NULLBUF)
                return;
            if(iface->forw != NULLIF)
                (*iface->forw->output)(iface->forw,
                arp.thwaddr,iface->forw->hwaddr,REVARP_TYPE,bp);
            else
                (*iface->output)(iface,arp.thwaddr,
                iface->hwaddr,REVARP_TYPE,bp);
            Arp_stat.inreq++;
        }
    }
}
/* Add an IP-addr / hardware-addr pair to the ARP table */
struct arp_tab *
arp_add(ipaddr,hardware,hw_addr,pub,iface)
int32 ipaddr;           /* IP address, host order */
int16 hardware;         /* Hardware type */
char *hw_addr;          /* Hardware address, if known; NULLCHAR otherwise */
int pub;                /* Publish this entry? */
struct iface *iface;
{
    struct mbuf *bp;
    register struct arp_tab *ap;
    struct arp_type *at;
    unsigned hashval;
  
    if(hardware >=NHWTYPES)
        return NULLARP; /* Invalid hardware type */
    at = &Arp_type[hardware];
  
    if((ap = arp_lookup(hardware,ipaddr,iface)) == NULLARP){
        /* New entry */
        ap = (struct arp_tab *)callocw(1,sizeof(struct arp_tab));
        ap->hw_addr = mallocw(at->hwalen);
        ap->timer.func = arp_timeout;
        ap->timer.arg = ap;
        ap->hardware = hardware;
        ap->ip_addr = ipaddr;
        ap->iface = iface;
  
        /* Put on head of hash chain */
        hashval = hash_ip(ipaddr);
        ap->prev = NULLARP;
        ap->next = Arp_tab[hashval];
        Arp_tab[hashval] = ap;
        if(ap->next != NULLARP){
            ap->next->prev = ap;
        }
    }
    if(hw_addr == NULLCHAR){
        /* Await response */
        ap->state = ARP_PENDING;
        set_timer(&ap->timer, (uint32)(Arp_type[hardware].pendtime) * 1000);
    } else {
        /* Response has come in, update entry and run through queue */
        ap->state = ARP_VALID;
        set_timer(&ap->timer,ARPLIFE*1000);
        memcpy(ap->hw_addr,hw_addr,at->hwalen);
        ap->pub = pub;
        while((bp = dequeue(&ap->pending)) != NULLBUF)
            ip_route(NULLIF,bp,0);
    }
    start_timer(&ap->timer);
    return ap;
}
  
/* Remove an entry from the ARP table */
void
arp_drop(p)
void *p;
{
    register struct arp_tab *ap;
  
    ap = (struct arp_tab *)p;
    if(ap == NULLARP)
        return;
    stop_timer(&ap->timer); /* Shouldn't be necessary */
    if(ap->next != NULLARP)
        ap->next->prev = ap->prev;
    if(ap->prev != NULLARP)
        ap->prev->next = ap->next;
    else
        Arp_tab[hash_ip(ap->ip_addr)] = ap->next;
    free_q(&ap->pending);
    free(ap->hw_addr);
    free((char *)ap);
}
  
/* Look up the given IP address in the ARP table */
struct arp_tab *
arp_lookup(hardware,ipaddr,iface)
int16 hardware;
int32 ipaddr;
struct iface *iface;
{
    register struct arp_tab *ap;
  
    for(ap = Arp_tab[hash_ip(ipaddr)]; ap != NULLARP; ap = ap->next){
        if(ap->ip_addr == ipaddr && ap->hardware == hardware \
            && ap->iface == iface)
            break;
    }
    return ap;
}
/* Send an ARP request to resolve IP address target_ip */
static void
arp_output(iface,hardware,target,hw_addr)
struct iface *iface;
int16 hardware;
int32 target;
char *hw_addr;
{
    struct arp arp;
    struct mbuf *bp;
    struct arp_type *at;
  
    at = &Arp_type[hardware];
    if(iface->output == NULLFP((struct iface*,char*,char*,int16,struct mbuf*)))
        return;
  
    arp.hardware = hardware;
    arp.protocol = at->iptype;
    arp.hwalen = at->hwalen;
    arp.pralen = sizeof(int32);
    arp.opcode = ARP_REQUEST;
    memcpy(arp.shwaddr,iface->hwaddr,at->hwalen);
    arp.sprotaddr = iface->addr;
    memset(arp.thwaddr,0,at->hwalen);
    arp.tprotaddr = target;
    if((bp = htonarp(&arp)) == NULLBUF)
        return;
    if(hw_addr == NULLCHAR)
        (*iface->output)(iface,at->bdcst,iface->hwaddr,at->arptype,bp);
    else
        (*iface->output)(iface,hw_addr,iface->hwaddr,at->arptype,bp);
    Arp_stat.outreq++;
}
  
/* Called when an ARP entry times out. If an entry has been a valid
 * one we will send out an ARP reply. If this one does not succed,
 * the ARP entry will be dropped. -- sm6rpz
 *
 * Made configurable by WG7J.
 */
static void
arp_timeout(p)
void *p;
{
    struct arp_type *at;
    char *hwaddr;
    struct arp_tab *ap = (struct arp_tab *)p;
  
    if(ap == NULLARP)
        return;
    stop_timer(&ap->timer);
    /* Check to see if the timer is set to ARPLIFE or pendtime. Set_timer()
     * adds at least one tick so we must do a little more flexible check.
     */
    if( (ap->iface->flags & ARP_KEEPALIVE) &&
    (dur_timer(&ap->timer) >= ARPLIFE * 1000)) {
        at = &Arp_type[ap->hardware];
        set_timer(&ap->timer, (uint32)(at->pendtime) * 1000);
    /* timer functions should NEVER call blocked allocs ! - WG7J */
        if((hwaddr = (char *)mallocw((unsigned)at->hwalen))!=NULLCHAR) {
            memcpy(hwaddr,ap->hw_addr,at->hwalen);
            arp_output(ap->iface,ap->hardware,ap->ip_addr,hwaddr);
            free(hwaddr);           /* clean up */
        }
        start_timer(&ap->timer);
    } else
        arp_drop(ap);
}
  
