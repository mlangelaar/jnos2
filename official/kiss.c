/* Routines for AX.25 encapsulation in KISS TNC
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Modified by G1EMM 19/11/90 to support multi-port KISS mode.
 */
 /* Mods by G1EMM */
  
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "kiss.h"
#include "devparam.h"
#include "slip.h"
#include "asy.h"
#include "ax25.h"
#include "pktdrvr.h"
#include "commands.h"
  
/* Send raw data packet on KISS TNC */
int
kiss_raw(iface,data)
struct iface *iface;
struct mbuf *data;
{
    register struct mbuf *bp;
  
    /* Put type field for KISS TNC on front */
    bp = pushdown(data,1);
    bp->data[0] = PARAM_DATA;
    bp->data[0] |= (iface->port << 4);
    if(iface->port){
        iface->rawsndcnt++;
#ifdef J2_SNMPD_VARS
        iface->rawsndbytes += len_p (bp);
#endif
        iface->lastsent = secclock();
    }
    /* slip_raw also increments sndrawcnt */
    slip_raw(Slip[iface->xdev].iface,bp);
    return 0;
}
  
/* Process incoming KISS TNC frame */
void
kiss_recv(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
    char kisstype;
    struct iface *kissif;
    int port;
  
    kisstype = PULLCHAR(&bp);
    port = kisstype >> 4;
  
#ifdef notdef
    if(iface->xdev) {
        if((kissif = Slip[iface->xdev].kiss[port]) == NULLIF && port != 0) {
            free_p(bp);
            return;
        }
    } else {
        kissif = iface;
    }
#endif
  
    if((kissif = Slip[iface->xdev].kiss[port]) == NULLIF) {
        free_p(bp);
        return;
    }
  
    switch(kisstype & 0xf){
        case PARAM_DATA:
            ax_recv(kissif,bp);
            break;
        default:
            free_p(bp);
            break;
    }
}
/* Perform device control on KISS TNC by sending control messages */
int32
kiss_ioctl(iface,cmd,set,val)
struct iface *iface;
int cmd;
int set;
int32 val;
{
    struct mbuf *hbp;
    char *cp;
    int32 rval = 0L;
  
    /* New behaviour: pass all kiss cmds not known to be handled by our asy
     * driver, to the TNC which is expected to "do the right thing".  If this
     * new behaviour is undesired, #define KISS_PARAM_FILTER to restore the
     * pre-1.11f filtering of allowed KISS cmds (and maintain the list, below).
     * -- G8ECJ and N5KNX
     */
    switch(cmd){
        case PARAM_RETURN:
        case PARAM_RETURN2:
            set = 1;    /* Note fall-thru */
#ifdef KISS_PARAM_FILTER
        case PARAM_TXDELAY:
        case PARAM_PERSIST:
        case PARAM_SLOTTIME:
        case PARAM_TXTAIL:
        case PARAM_FULLDUP:
        case PARAM_HW:
#else
        default:        /* most cmds except those handled by asy driver */
#endif
            if(!set){
#ifdef KISS_PARAM_FILTER
        default:        /* Not implemented */
#endif
                rval = -1;  /* Can't read back */
                break;
            }
        /* Allocate space for cmd and arg */
            if((hbp = alloc_mbuf(2)) == NULLBUF){
	      /* free_p(hbp);  why do this?? it's NULL */
                rval = -1;
                break;
            }
            cp = hbp->data;
            *cp++ = cmd;
            *cp = (char)val;
            hbp->cnt = 2;
            if(hbp->data[0] != (char) PARAM_RETURN)
                hbp->data[0] |= (iface->port << 4);
            if(iface->port){
                iface->rawsndcnt++;
                iface->lastsent = secclock();
            }
        /* Even more "raw" than kiss_raw */
            slip_raw(Slip[iface->xdev].iface,hbp);
            rval = val;
            break;

        case PARAM_SPEED:   /* These go to the local asy driver */
        case PARAM_DTR:
        case PARAM_RTS:
        case PARAM_UP:
        case PARAM_DOWN:
            rval = asy_ioctl(iface,cmd,set,val);
            break;
    }
    return rval;
}
  
#ifdef KISS
static int kiss_stop(struct iface *iface);

static int
kiss_stop(iface)
struct iface *iface;
{
    Slip[iface->xdev].kiss[iface->port] = NULLIF;
    return 0;
}
  
/* Attach a kiss interface to an existing asy interface in the system
 * argv[0]: hardware type, must be "kiss"
 * argv[1]: master interface, e.g., "ax4"
 * argv[2]: kiss port, e.g., "4"
 * argv[3]: interface label, e.g., "ax0"
 * argv[4]: maximum transmission unit, bytes
 */
int
kiss_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *if_asy, *if_kiss;
    int port;
  
    if((if_asy = if_lookup(argv[1])) == NULLIF){
        tprintf("Interface %s does not exist\n",argv[1]);
        return -1;
    }
  
    /* Check for ASY type interface ! - WG7J */
    if(if_asy->type != CL_AX25) {
        tprintf("Multidrop KISS not allowed in interface: %s\n",argv[1]);
        return -1;
    }
  
    if(if_lookup(argv[3]) != NULLIF){
        tprintf("Interface %s already exists\n",argv[4]);
        return -1;
    }
  
    if((port = atoi(argv[2])) == 0){
        tprintf("Port 0 automatically assigned to interface %s\n",argv[1]);
        return -1;
    }
  
    if(port < 1 || port > 15){
        j2tputs("Ports 1 to 15 only\n");
        return -1;
    }
  
    if(Slip[if_asy->xdev].kiss[port] != NULLIF){
        tprintf("Port %d already allocated on interface %s\n", port, argv[1]);
        return -1;
    }
    /* Create interface structure and fill in details */
    if_kiss = (struct iface *)callocw(1,sizeof(struct iface));
    if_kiss->addr = if_asy->addr;
    if_kiss->name = j2strdup(argv[3]);
  
    if(argc >= 5){
        if_kiss->mtu = atoi(argv[4]);
    } else {
        if_kiss->mtu = if_asy->mtu;
    }
  
    if_kiss->dev = if_asy->dev;
    if_kiss->stop = kiss_stop;
    setencap(if_kiss,"AX25");
    if_kiss->ioctl = kiss_ioctl;
    if_kiss->raw = kiss_raw;
    if(if_kiss->hwaddr == NULLCHAR)
        if_kiss->hwaddr = mallocw(AXALEN);
    memcpy(if_kiss->hwaddr,Mycall,AXALEN);
    if_kiss->xdev = if_asy->xdev;
    if_kiss->next = Ifaces;
    Ifaces = if_kiss;
    if_kiss->port = port;
    Slip[if_kiss->xdev].kiss[if_kiss->port] = if_kiss;
    return 0;
}
  
#endif /* KISS */
