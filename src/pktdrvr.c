/* Driver for FTP Software & Crynwr (TM) packet driver interface. (PC specific code)
 * Copyright 1991 Phil Karn, KA9Q
 * Mods by G8FSL 12/94 to add dopkstat, fix set_rcv_mode.
 */
#include <stdio.h>
#include <dos.h>
#include "global.h"
#ifdef PACKET
#include "proc.h"
#include "mbuf.h"
#include "netuser.h"
#include "enet.h"
#include "arcnet.h"
#include "ax25.h"
#include "slip.h"
#include "kiss.h"
#include "iface.h"
#include "ec.h"
#include "arp.h"
#include "trace.h"
#include "pktdrvr.h"
#include "config.h"
#include "devparam.h"
  
static long access_type __ARGS((int intno,int if_class,int if_type,
    int if_number, char *type,unsigned typelen,
    INTERRUPT (*receiver) __ARGS((void)) ));
static int driver_info __ARGS((int intno,int handle,int *version,
    int *class,int *type,int *number,int *basic));
static int release_type __ARGS((int intno,int handle));
static int get_address __ARGS((int intno,int handle,char *buf,int len));
static int pk_raw __ARGS((struct iface *iface,struct mbuf *bp));
static int pk_stop __ARGS((struct iface *iface));
static int send_pkt __ARGS((int intno,char *buffer,unsigned length));
static int set_rcv_mode __ARGS((int intno, int handle, int mode));
static int get_rcv_mode __ARGS((int intno, int handle));
static int32 pk_ioctl __ARGS((struct iface *iface,int cmd,int set,int32 val));
static void stop __ARGS((int intno));
  
  
static INTERRUPT (*Pkvec[])() = { pkvec0,pkvec1,pkvec2 };
static struct pktdrvr Pktdrvr[PK_MAX];
static int Derr;
static char Pkt_sig[] = "PKT DRVR"; /* Packet driver signature */
static char iptype[] = {IP_TYPE >> 8,IP_TYPE};
static char arptype[] = {ARP_TYPE >> 8,ARP_TYPE};
#ifdef RARP
static char revarptype[] = {REVARP_TYPE >> 8, REVARP_TYPE};
#endif
#ifdef ARCNET
static char arcip[] = {ARC_IP};
static char arcarp[] = {ARC_ARP};
#endif
  
/*
 * Send routine for packet driver
 */
  
int
pk_send(bp,iface,gateway,prec,del,tput,rel)
struct mbuf *bp;    /* Buffer to send */
struct iface *iface;    /* Pointer to interface control block */
int32 gateway;      /* Ignored  */
int prec;
int del;
int tput;
int rel;
{
    if(iface == NULLIF){
        free_p(bp);
        return -1;
    }
    return (*iface->raw)(iface,bp);
}
  
/* Send raw packet (caller provides header) */
static int
pk_raw(iface,bp)
struct iface *iface;    /* Pointer to interface control block */
struct mbuf *bp;    /* Data field */
{
    register struct pktdrvr *pp;
    int16 size;
    struct mbuf *bp1;
  
    pp = &Pktdrvr[iface->dev];
    size = len_p(bp);
  
/*    dump(iface,IF_TRACE_OUT,pp->class,bp);    moved, G8FSL */
    iface->rawsndcnt++;
    iface->lastsent = secclock();
  
    /* Perform class-specific processing, if any */
    switch(pp->class){
        case CL_ETHERNET:
            if(size < RUNT){
            /* Pad the packet out to the minimum */
                if((bp1 = alloc_mbuf(RUNT-size)) == NULLBUF){
                    free_p(bp);
                    return -1;
                }
                memset(bp1->data,0,RUNT-size);
                bp1->cnt = RUNT-size;
                append(&bp,bp1);
                size = RUNT;
            }
            break;
        case CL_KISS:
        /* This *really* shouldn't be done here, but it was the
         * easiest way. Put the type field for KISS TNC on front.
         */
            bp1 = pushdown(bp,1);
            bp = bp1;
            bp->data[0] = PARAM_DATA;
            size++;
            break;
    }
  
    dump(iface,IF_TRACE_OUT,pp->class,bp);  /* moved here, G8FSL */
  
    if(bp->next != NULLBUF){
        /* Copy to contiguous buffer, since driver can't handle mbufs */
        bp1 = copy_p(bp,size);
        free_p(bp);
        bp = bp1;
        if(bp == NULLBUF)
            return -1;
    }
    if (send_pkt(pp->intno,bp->data,bp->cnt) == -1L)
        pp->nosends++;
    free_p(bp);
    return 0;
}
  
#if defined(__BORLANDC__) && __BORLANDC__ >= 0x0452 && ( defined(CPU386)||defined(CPU486)||defined(CPU586))
/* The BC4.0 through BC4.5 compilers, in -3 (i.e. 386) codegen mode, will
   push 32-bit regs instead of the 16-bit regs we expect, when the
   INTERRUPT keyword is used.  One solution is to change BC4's behaviour
   by using #pragma to change the codegen option briefly!
   Note: the true ISR in pkvec.asm will itself use PUSHALL to save all
   32-bit regs when assembled for a 386.
   -- pe1nmb & n5knx
*/
#pragma option -1
#endif
/* Packet driver receive routine. Called from an assembler ISR that pushes
 * the interrupting driver's number on the stack, then invokes us as an ISR.
 */
INTERRUPT
pkint(bp, di, si, ds, es, dx, cx, bx, ax, ip, cs, flags, dev)
unsigned short  bp, di, si, ds, es, dx, cx, bx, ax, ip, cs, flags;
int dev;
{
    register struct pktdrvr *pp;
    struct phdr phdr;
  
    if(dev < 0 || dev >= PK_MAX)
        return; /* Unknown device */
    pp = &Pktdrvr[dev];
    if(pp->iface == NULLIF)
        return; /* Unknown device */
    switch(ax){
        case 0: /* Space allocate call */
            if((pp->buffer = alloc_mbuf(cx+sizeof(phdr))) != NULLBUF){
                es = FP_SEG(pp->buffer->data);
                di = FP_OFF(pp->buffer->data+sizeof(phdr));
                pp->buffer->cnt = cx + sizeof(phdr);
                phdr.iface = pp->iface;
                phdr.type = pp->class;
                memcpy(&pp->buffer->data[0],(char *)&phdr,sizeof(phdr));
            } else {
                es = di = 0;
                pp->nobufs++;  /* Bump "no buffer available" counter */
            }
            break;
        case 1: /* Packet complete call */
            enqueue(&Hopper,pp->buffer);
            pp->buffer = NULLBUF;
            break;
        default:
            break;
    }
}
#if defined(__BORLANDC__) && __BORLANDC__ >= 0x0452 && (defined(CPU386)||defined(CPU486)||defined(CPU586))
/* Turn back on the previous codegen option */
#pragma option -1.
#endif

/* Shut down the packet interface */
static int
pk_stop(iface)
struct iface *iface;
{
    struct pktdrvr *pp;
  
    pp = &Pktdrvr[iface->dev];
    /* Call driver's release_type() entry */
    if(release_type(pp->intno,pp->handle1) == -1)
        tprintf("%s: release_type error code %u\n",iface->name,Derr);
  
    if(pp->class == CL_ETHERNET || pp->class == CL_ARCNET){
        release_type(pp->intno,pp->handle2);
#ifdef RARP
        release_type(pp->intno,pp->handle3);
#endif
    }
    pp->iface = NULLIF;
    return 0;
}
/* Attach a packet driver to the system
 * argv[0]: hardware type, must be "packet"
 * argv[1]: software interrupt vector, e.g., x7e
 * argv[2]: interface label, e.g., "trw0"
 * argv[3]: maximum number of packets allowed on transmit queue, e.g., "5"
 * argv[4]: maximum transmission unit, bytes, e.g., "1500"
 * argv[5]: optional IP address
 */
int
pk_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct iface *if_pk;
    int version,class,type,number;
    unsigned int intno;
    long handle;
    int i;
  
    long drvvec;
    char sig[8];    /* Copy of driver signature "PKT DRVR" */
    register struct pktdrvr *pp;
    char tmp[25];
  
    for(i=0;i<PK_MAX;i++){
        if(Pktdrvr[i].iface == NULLIF)
            break;
    }
    if(i >= PK_MAX){
        j2tputs("Too many packet drivers\n");
        return -1;
    }
    if(if_lookup(argv[2]) != NULLIF){
        tprintf(Existingiface,argv[2]);
        return -1;
    }
  
    intno = htoi(argv[1]);
    /* Verify that there's really a packet driver there, so we don't
     * go off into the ozone (if there's any left)
     */
    drvvec = (long)getvect(intno);
    movblock(FP_OFF(drvvec)+3, FP_SEG(drvvec),
        FP_OFF(sig),FP_SEG(sig),strlen(Pkt_sig));
    if(strncmp(sig,Pkt_sig,strlen(Pkt_sig)) != 0){
        tprintf("No packet driver loaded at int 0x%x\n",intno);
        return -1;
    }
    if_pk = (struct iface *)callocw(1,sizeof(struct iface));
    if_pk->name = j2strdup(argv[2]);
    if (argc > 5) {
        if((if_pk->addr = resolve(argv[5])) == 0) {
            j2tputs(Noipaddr);
            free(if_pk->name);
            free((char *)if_pk);
            return -1;
        }
    } else
        if_pk->addr = Ip_addr;
    pp = &Pktdrvr[i];
    if_pk->mtu = atoi(argv[4]);
    if_pk->dev = i;
    if_pk->raw = pk_raw;
    if_pk->stop = pk_stop;
    if_pk->ioctl = pk_ioctl;
    pp->intno = intno;
    pp->iface = if_pk;
  
    /* Version 1.08 of the packet driver spec dropped the handle
     * requirement from the driver_info call.  However, if we are using
     * a version 1.05 packet driver, the following call will fail.
     */
    if(driver_info(intno,-1,&version,&class,&type,&number,&pp->functionality) < 0) {
        if (Derr != BAD_HANDLE) {
            tprintf("Problem - driver info call returned error %u\n",Derr);
            free(if_pk->name);
            free((char *)if_pk);
            return -1;
        }
#define PKTDRV_DEBUG
#ifdef PKTDRV_DEBUG
        j2tputs("This appears to be an old spec driver\n");
#endif
        /* So it is a pre-1.08 driver that needs the handle in the
         * driver_info() call. Find out by exhaustive search what class
         * this driver is (ugh) */
        for(class=1;class<=NCLASS;class++){
            /* Store handle in temp long so we can tell an
             * error return (-1) from a handle of 0xffff
             */
            handle = access_type(intno,class,ANYTYPE,0,iptype,2,
            Pkvec[if_pk->dev]);
            if(handle != -1 || Derr == TYPE_INUSE){
                pp->handle1 = (short)handle;
                break;
            }
        }
        /* Now that we know, release it and do it all over again with the
         * right type fields
         */
        release_type(intno,pp->handle1);
        if (driver_info(intno,(int)handle,&version,&class,&type,&number,&pp->functionality) < 0) {
            tprintf("Can't get driver info - error %u\n",Derr);
            free(if_pk->name);
            free((char *)if_pk);
            return -1;
        }
    }
    switch(class){
        case CL_ETHERNET:
            pp->handle1 = (short)access_type(intno,class,type,number,iptype,2,
                Pkvec[if_pk->dev]);
            pp->handle2 = (short)access_type(intno,class,type,number,arptype,2,
                Pkvec[if_pk->dev]);
#ifdef RARP
            pp->handle3 = (short)access_type(intno,class,type,number,revarptype,2,
                Pkvec[if_pk->dev]);
#endif
            setencap(if_pk,"Ethernet");
        /* Get hardware Ethernet address from driver */
            if_pk->hwaddr = mallocw(EADDR_LEN);
            get_address(intno,pp->handle1,if_pk->hwaddr,EADDR_LEN);
            if(if_pk->hwaddr[0] & 1){
                tprintf("Warning! Interface '%s' has a multicast address:",
                    if_pk->name);
                tprintf(" (%s)\n", (*if_pk->iftype->format)(tmp,if_pk->hwaddr));
            }
            break;
#ifdef  ARCNET
        case CL_ARCNET:
            pp->handle1 = access_type(intno,class,type,number,arcip,1,
                Pkvec[if_pk->dev]);
            pp->handle2 = (short) access_type(intno,class,type,number,arcarp,1,
                Pkvec[if_pk->dev]);
            if_pk->send = anet_send;
            if_pk->output = anet_output;
        /* Get hardware ARCnet address from driver */
            if_pk->hwaddr = mallocw(AADDR_LEN);
            get_address(intno,pp->handle1,if_pk->hwaddr,AADDR_LEN);
            /* setencap(if_pk,NULL);*/
            break;
#endif
        case CL_SERIAL_LINE:
            pp->handle1 = (short)access_type(intno,class,type,number,NULLCHAR,0,
                Pkvec[if_pk->dev]);
            setencap(if_pk,"SLIP");
            break;
#ifdef  AX25
        case CL_KISS:  /* Note that the raw routine puts on the command byte */
            if_pk->xdev = -1;  /* This is needed to fool the KISS handler */
        case CL_AX25:
            /* Note that this asks for access to all protocols */
            pp->handle1 = (short)access_type(intno,class,type,number,NULLCHAR,0,
                Pkvec[if_pk->dev]);
            setencap(if_pk,"AX25");
            if_pk->hwaddr = mallocw(AXALEN);
            memcpy(if_pk->hwaddr,Mycall,AXALEN);
            break;
#endif
        case CL_SLFP:
            pp->handle1 = (short)access_type(intno,class,type,number,NULLCHAR,0,
                Pkvec[if_pk->dev]);
            if_pk->send = pk_send;
            setencap(if_pk,"SLFP");
            get_address(intno,pp->handle1,(char *)&if_pk->addr,4);
            break;
        default:
            tprintf("Packet driver has unsupported class %u\n",class);
            free(if_pk->name);
            free((char *)if_pk);
            return -1;
    }
    pp->class = class;
    pp->type = type;
    pp->number = number;
    if_pk->next = Ifaces;
    Ifaces = if_pk;
  
    return 0;
}
static long
access_type(intno,if_class,if_type,if_number,type,typelen,receiver)
int intno;
int if_class;
int if_type;
int if_number;
char *type;
unsigned typelen;
INTERRUPT (*receiver)();
{
    union REGS regs;
    struct SREGS sregs;
  
    segread(&sregs);
    regs.h.dl = if_number;      /* Number */
    sregs.ds = FP_SEG(type);    /* Packet type template */
    regs.x.si = FP_OFF(type);
    regs.x.cx = typelen;        /* Length of type */
    sregs.es = FP_SEG(receiver);    /* Address of receive handler */
    regs.x.di = FP_OFF(receiver);
    regs.x.bx = if_type;        /* Type */
    regs.h.ah = ACCESS_TYPE;    /* Access_type() function */
    regs.h.al = if_class;       /* Class */
    int86x(intno,&regs,&regs,&sregs);
    if(regs.x.cflag){
        Derr = regs.h.dh;
        return -1;
    } else
        return regs.x.ax;
}
static int
release_type(intno,handle)
int intno;
int handle;
{
    union REGS regs;
  
    regs.x.bx = handle;
    regs.h.ah = RELEASE_TYPE;
    int86(intno,&regs,&regs);
    if(regs.x.cflag){
        Derr = regs.h.dh;
        return -1;
    } else
        return 0;
}
static int
send_pkt(intno,buffer,length)
int intno;
char *buffer;
unsigned length;
{
    union REGS regs;
    struct SREGS sregs;
  
    segread(&sregs);
    sregs.ds = FP_SEG(buffer);
    sregs.es = FP_SEG(buffer); /* for buggy univation pkt driver - CDY */
    regs.x.si = FP_OFF(buffer);
    regs.x.cx = length;
    regs.h.ah = SEND_PKT;
    int86x(intno,&regs,&regs,&sregs);
    if(regs.x.cflag){
        Derr = regs.h.dh;
        return -1;
    } else
        return 0;
}
static int
driver_info(intno,handle,version,class,type,number,basic)
int intno;
int handle;
int *version,*class,*type,*number,*basic;
{
    union REGS regs;
  
    regs.x.bx = handle;
    regs.h.ah = DRIVER_INFO;
    regs.h.al = 0xff;
    int86(intno,&regs,&regs);
    if(regs.x.cflag){
        Derr = regs.h.dh;
        return -1;
    }
    if(version != NULL)
        *version = regs.x.bx;
    if(class != NULL)
        *class = regs.h.ch;
    if(type != NULL)
        *type = regs.x.dx;
    if(number != NULL)
        *number = regs.h.cl;
    if(basic != NULL)
        *basic = regs.h.al;
    return 0;
}
static int
get_address(intno,handle,buf,len)
int intno;
int handle;
char *buf;
int len;
{
    union REGS regs;
    struct SREGS sregs;
  
    segread(&sregs);
    sregs.es = FP_SEG(buf);
    regs.x.di = FP_OFF(buf);
    regs.x.cx = len;
    regs.x.bx = handle;
    regs.h.ah = GET_ADDRESS;
    int86x(intno,&regs,&regs,&sregs);
    if(regs.x.cflag){
        Derr = regs.h.dh;
        return -1;
    }
    return 0;
}
  
static int set_rcv_mode(int intno, int handle, int mode) {
    union REGS regs;
  
    regs.x.cx = mode;
    regs.x.bx = handle;
    regs.h.ah = SET_RCV_MODE;
    int86(intno,&regs,&regs);
    if(regs.x.cflag){
        Derr = regs.h.dh;
        return -1;
    }
    return 0;
}
  
static int get_rcv_mode(int intno, int handle) {
    union REGS regs;
  
    regs.x.bx = handle;
    regs.h.ah = GET_RCV_MODE;
    int86(intno,&regs,&regs);
    if(regs.x.cflag){
        Derr = regs.h.dh;
        return -1;
    }
    return regs.x.ax;
}
  
/* Perform device control on the packet driver - WG7J */
int32
pk_ioctl(iface,cmd,set,val)
struct iface *iface;
int cmd;
int set;
int32 val;
{
    struct pktdrvr *pp;
    int32 rval = 0;
  
    pp = &Pktdrvr[iface->dev];
  
    /* At present, the only parameter supported is receive mode */
    switch(cmd){
        case PARAM_RCV_MODE:
            if ((pp->functionality & 2) == 0) {
                rval = -1L; /* set/get_rcv_mode are extended driver functions */
                break;
            }
            if(set) {
                /* set_rcv_mode needs only be called once as it affects
                 * all handles. However there must be exactly one active
                 * handle - the one used for the set_rcv_mode call.
                 * Therefore we need to release all handles but one
                 * (and	hope that no other application is using the driver)
                 * G8FSL 941214
                 */

                if (pp->handle2) {	/* Hoping that no driver will use 0 as a handle! */
                    if (release_type(pp->intno,pp->handle2) == -1L) {
#ifdef PKTDRV_DEBUG
                        tprintf("Error %u releasing arp handle\n",Derr);
#endif
                    }
                }
#ifdef RARP
                if (pp->handle3) {
                    if (release_type(pp->intno,pp->handle3) == -1L) {
#ifdef PKTDRV_DEBUG
                        tprintf("Error %u releasing rarp handle\n",Derr);
#endif
                    }
                }
#endif
                if(set_rcv_mode(pp->intno, pp->handle1, (int)val) == -1) {
                    tprintf("Cannot set packet driver to mode %d - error %u\n", (int)val, Derr);
                } else {
                    rval = val;
                }

                /* And now re-access the temporarily released handles */
                if (pp->handle2) {
                    switch(pp->class) {
                    case CL_ETHERNET:
                        pp->handle2 = (short)access_type(pp->intno,pp->class,pp->type,pp->number,arptype,2,
                            Pkvec[pp->iface->dev]);
                	break;
#ifdef ARCNET
                    case CL_ARCNET:
                        pp->handle2 = (short)access_type(pp->intno,pp->class,pp->type,pp->number,arcarp,1,
                            Pkvec[pp->iface->dev]);
                        break;
#endif
                    }
                }
#ifdef RARP
                if (pp->handle3) {
                    switch(pp->class) {
                    case CL_ETHERNET:
                        pp->handle3 = (short)access_type(pp->intno,pp->class,pp->type,pp->number,revarptype,2,
                            Pkvec[pp->iface->dev]);
                        break;
                    }
                }
#endif
            } else {
                rval = (int32) get_rcv_mode(pp->intno, pp->handle1);
                if(rval == -1)
                    j2tputs("Can't read mode!\n");
            }
            break;
        default:        /* Not implemented */
            rval = -1L;
            break;
    }
    return rval;
}
  
int
dopkstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int i;
    union REGS regs;
    struct SREGS sregs;
    int32 *statp;
    struct pktdrvr *pd;

    for(i=0 ; i<PK_MAX ; i++){
        pd=&Pktdrvr[i];
        if(pd->iface == NULLIF)
            continue;
        tprintf("%s (Int 0x%x):\n",pd->iface->name,pd->intno);
        /* get_statistics is an extended driver function */
        if (pd->functionality & 2) {
            segread(&sregs);
            regs.x.bx = pd->handle1;
            regs.h.ah = GET_STATISTICS;
            int86x(pd->intno,&regs,&regs,&sregs);
            if(regs.x.cflag){
                Derr = regs.h.dh;
                tprintf("  error %u\n",Derr);
                continue;
            }
            statp = MK_FP(sregs.ds,regs.x.si);
            tprintf("  Packets in: %10lu",*statp++);
            tprintf("  out: %10lu\n",*statp++);
            tprintf("  Bytes   in: %10lu",*statp++);
            tprintf("  out: %10lu\n",*statp++);
            tprintf("  Errors  in: %10lu",*statp++);
            tprintf("  out: %10lu\n",*statp++);
            tprintf("  Packets lost: %lu\n",*statp++);
        }            
        tprintf("  Failed RX upcalls: %lu\n",pd->nobufs);
        tprintf("  Failed TX requests: %lu\n",pd->nosends);
    }
    return 0;
}

#endif /* PACKET */
