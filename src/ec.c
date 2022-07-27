/* Driver for 3COM Ethernet card (PC specific code)
 *
 * This driver is deprecated - use the loadable packet driver from the
 * Clarkson collection instead. Better yet, junk your 3C501 card and buy
 * something more reasonable.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
  
#define TIMER   20000   /* Timeout on transmissions */
  
#include <dos.h>
#include "global.h"
#ifdef PC_EC
#include "mbuf.h"
#include "enet.h"
#include "iface.h"
#include "pktdrvr.h"
#include "netuser.h"
#include "ec.h"
#include "arp.h"
#include "trace.h"
#include "pc.h"
  
static int ec_init __ARGS((struct iface *iface,unsigned bufsize));
static int ec_raw __ARGS((struct iface *iface,struct mbuf *bp));
static int ec_stop __ARGS((struct iface *iface));
static void getecaddr __ARGS((unsigned base,char *cp));
static void rcv_fixup __ARGS((unsigned base));
static void setecaddr __ARGS((unsigned base,char *cp));
  
static INTERRUPT (*Ecvecsave[EC_MAX])();
static INTERRUPT (*Ecvec[])() = {ec0vec,ec1vec,ec2vec};
  
struct ec Ec[EC_MAX];       /* Per-controller info */
int Nec = 0;
  
/* Initialize interface */
static int
ec_init(iface,bufsize)
struct iface *iface;
unsigned bufsize;   /* Maximum size of receive queue in PACKETS */
{
    register struct ec *ecp;
    register unsigned base;
    int dev;
  
    dev = iface->dev;
    ecp = &Ec[dev];
    base = ecp->base;
    ecp->iface = iface;
  
    /* Pulse IE_RESET */
    outportb(IE_CSR(base),IE_RESET);
  
    /* Save old int vector */
    Ecvecsave[dev] = getirq(ecp->vec);
  
    /* Set interrupt vector */
    if(setirq(ecp->vec,Ecvec[dev]) == -1){
        tprintf("IRQ %u out of range\n",ecp->vec);
        return -1;
    }
    maskon(ecp->vec);   /* Enable interrupt */
    if(iface->hwaddr == NULLCHAR)
        iface->hwaddr = mallocw(EADDR_LEN);
    getecaddr(base,iface->hwaddr);
    setecaddr(base,iface->hwaddr);
    if(memcmp(iface->hwaddr,Ether_bdcst,EADDR_LEN) == 0){
        tprintf("EC address PROM contains broadcast address!!\n");
        return -1;
    }
    /* Enable DMA/interrupt request, gain control of buffer */
    outportb(IE_CSR(base),IE_RIDE|IE_SYSBFR);
  
    /* Enable transmit interrupts */
    outportb(EDLC_XMT(base),EDLC_16 | EDLC_JAM);
  
    /* Set up the receiver interrupts and flush status */
    outportb(EDLC_RCV(base),EDLC_MULTI|EDLC_GOOD|EDLC_ANY|EDLC_SHORT
    |EDLC_DRIBBLE|EDLC_FCS|EDLC_OVER);
    inportb(EDLC_RCV(base));
  
    /* Start receiver */
    outportw(IE_RP(base),0);    /* Reset read pointer */
    outportb(IE_CSR(base),IE_RIDE|IE_RCVEDLC);
    return 0;
}
/* Send raw packet (caller provides header) */
static int
ec_raw(iface,bp)
struct iface *iface;    /* Pointer to interface control block */
struct mbuf *bp;        /* Data field */
{
    register struct ec *ecp;
    register unsigned base;
    register int i;
    short size;
  
    dump(iface,IF_TRACE_OUT,CL_ETHERNET,bp);
    iface->rawsndcnt++;
    iface->lastsent = secclock();
  
    ecp = &Ec[iface->dev];
    base = ecp->base;
  
    ecp->estats.xmit++;
  
    size = len_p(bp);
    /* Pad the size out to the minimum, if necessary,
     * with junk from the last packet (nice security hole here)
     */
    if(size < RUNT)
        size = RUNT;
    size = (size+1) & ~1;   /* round size up to next even number */
  
    /* Wait for transmitter ready, if necessary. IE_XMTBSY is valid
     * only in the transmit mode, hence the initial check.
     */
    if((inportb(IE_CSR(base)) & IE_BUFCTL) == IE_XMTEDLC){
        for(i=TIMER;(inportb(IE_CSR(base)) & IE_XMTBSY) && i != 0;i--)
            ;
        if(i == 0){
            ecp->estats.timeout++;
            free_p(bp);
            return -1;
        }
    }
    ecp->size = size;
    /* Get control of the board buffer and disable receiver */
    outportb(IE_CSR(base),IE_RIDE|IE_SYSBFR);
    /* Point GP at beginning of packet */
    outportw(IE_GP(base),BFRSIZ-size);
    /* Actually load each piece with a fast assembler routine */
    while(bp != NULLBUF){
        outbuf(IE_BFR(base),bp->data,bp->cnt);
        bp = free_mbuf(bp);
    }
    /* Start transmitter */
    outportw(IE_GP(base),BFRSIZ-size);
    outportb(IE_CSR(base),IE_RIDE|IE_XMTEDLC);
    return 0;
}
/* Ethernet interrupt handler */
void
ecint(dev)
int dev;
{
    register struct ec *ecp;
    register unsigned base;
    struct mbuf *bp;
    int16 size;
    char stat;
    struct phdr phdr;
  
    ecp = &Ec[dev];
    base = Ec[dev].base;
    ecp->estats.intrpt++;
  
    /* Check for transmit jam */
    if(!(inportb(IE_CSR(base)) & IE_XMTBSY)){
        stat = inportb(EDLC_XMT(base));
        if(stat & EDLC_16){
            ecp->estats.jam16++;
            rcv_fixup(base);
        } else if(stat & EDLC_JAM){
            /* Crank counter back to beginning and restart transmit */
            ecp->estats.jam++;
            outportb(IE_CSR(base),IE_RIDE|IE_SYSBFR);
            outportw(IE_GP(base),BFRSIZ - ecp->size);
            outportb(IE_CSR(base),IE_RIDE|IE_XMTEDLC);
        }
    }
    for(;;){
        stat = inportb(EDLC_RCV(base));
        if(stat & EDLC_STALE)
            break;
  
        if(stat & EDLC_OVER){
            ecp->estats.over++;
            rcv_fixup(base);
            continue;
        }
        if(stat & (EDLC_SHORT | EDLC_FCS | EDLC_DRIBBLE)){
            ecp->estats.bad++;
            rcv_fixup(base);
            continue;
        }
        if(stat & EDLC_ANY){
            /* Get control of the buffer */
            outportw(IE_GP(base),0);
            outportb(IE_CSR(base),IE_RIDE|IE_SYSBFR);
  
            /* Allocate mbuf and copy the packet into it */
            size = inportw(IE_RP(base));
            if(size < RUNT || size > GIANT)
                ecp->estats.bad++;
            else if((bp = alloc_mbuf(size+sizeof(phdr))) == NULLBUF)
                ecp->estats.nomem++;
            else {
                ecp->estats.recv++;
                /* Generate descriptor header */
                phdr.iface = ecp->iface;
                phdr.type = CL_ETHERNET;
                memcpy(&bp->data[0],(char *)&phdr,sizeof(phdr));
                inbuf(IE_BFR(base),bp->data+sizeof(phdr),size);
                bp->cnt = size + sizeof(phdr);
                enqueue(&Hopper,bp);
            }
            outportb(IE_CSR(base),IE_RIDE|IE_RCVEDLC);
            outportb(IE_RP(base),0);
        }
    }
    /* Clear any spurious interrupts */
    (void)inportb(EDLC_RCV(base));
    (void)inportb(EDLC_XMT(base));
}
static void
rcv_fixup(base)
register unsigned base;
{
    outportb(IE_CSR(base),IE_RIDE|IE_SYSBFR);
    outportb(IE_CSR(base),IE_RIDE|IE_RCVEDLC);
    outportb(IE_RP(base),0);
}
/* Read Ethernet address from controller PROM */
static void
getecaddr(base,cp)
register unsigned base;
register char *cp;
{
    register int i;
  
    for(i=0;i<EADDR_LEN;i++){
        outportw(IE_GP(base),i);
        *cp++ = inportb(IE_SAPROM(base));
    }
}
/* Set Ethernet address on controller */
static void
setecaddr(base,cp)
register unsigned base;
register char *cp;
{
    register int i;
  
    for(i=0;i<EADDR_LEN;i++)
        outportb(EDLC_ADDR(base)+i,*cp++);
}
/* Shut down the Ethernet controller */
static int
ec_stop(iface)
struct iface *iface;
{
    register unsigned base;
    int dev;
    register struct ec *ecp;
  
    dev = iface->dev;
    ecp = &Ec[dev];
    base = ecp->base;
  
    /* Disable interrupt */
    maskoff(Ec[dev].vec);
  
    /* Restore original interrupt vector */
    setirq(ecp->vec,Ecvecsave[dev]);
  
    /* Pulse IE_RESET */
    outportb(IE_CSR(base),IE_RESET);
    outportb(IE_CSR(base),0);
    return 0;
}
/* Attach a 3-Com model 3C500 Ethernet controller to the system
 * argv[0]: hardware type, must be "3c500"
 * argv[1]: I/O address, e.g., "0x300"
 * argv[2]: vector, e.g., "3"
 * argv[3]: mode, must be "arpa"
 * argv[4]: interface label, e.g., "ec0"
 * argv[5]: maximum number of packets allowed on receive queue, e.g., "5"
 * argv[6]: maximum transmission unit, bytes, e.g., "1500"
 * argv[7]: IP address, optional (defaults to Ip_addr)
 */
int
ec_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct iface *if_ec;
    int dev;
  
    if(Nec >= EC_MAX){
        tprintf("Too many Ethernet controllers\n");
        return -1;
    }
    if(if_lookup(argv[4]) != NULLIF){
        tprintf(Existingiface,argv[4]);
        return -1;
    }
    dev = Nec++;
    if_ec = (struct iface *)callocw(1,sizeof(struct iface));
    if_ec->addr = Ip_addr;
    if(argc > 7)
        if_ec->addr = resolve(argv[7]);
    if(if_ec->addr == 0){
        tprintf(Noipaddr);
        free((char *)if_ec);
        return -1;
    }
    if_ec->name = j2strdup(argv[4]);
    if_ec->mtu = atoi(argv[6]);
    if_ec->type = CL_ETHERNET;
    if_ec->send = enet_send;
    if_ec->output = enet_output;
    if_ec->raw = ec_raw;
    if_ec->stop = ec_stop;
    if_ec->dev = dev;
  
    Ec[dev].base = htoi(argv[1]);
    Ec[dev].vec = htoi(argv[2]);
  
    if(strcmp(argv[3],"arpa") != 0){
        tprintf("Mode %s unknown for interface %s\n",
        argv[3],argv[4]);
        free(if_ec->name);
        free((char *)if_ec);
        return -1;
    }
    /* Initialize device */
    if(ec_init(if_ec,(unsigned)atoi(argv[5])) != 0){
        free(if_ec->name);
        free((char *)if_ec);
        return -1;
    }
    if_ec->next = Ifaces;
    Ifaces = if_ec;
  
    /* Set the tcp parameters - WG7J */
    setencap(if_ec,NULL);
  
    return 0;
}
#endif /* PC_EC */
  
