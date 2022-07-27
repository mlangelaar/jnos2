/* Tracing routines for KISS TNC
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Modified by G1EMM  19/11/90 to support multiport KISS mode.
 */
/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
  
#include "global.h"
#include "mbuf.h"
#include "kiss.h"
#include "devparam.h"
#include "ax25.h"
#include "trace.h"
#include "socket.h"  

void
ki_dump(s,bpp,check)
int s;
struct mbuf **bpp;
int check;
{
    int type,t;
    int val;
  
#ifdef MONITOR
    if (!Trace_compact_header)
#endif
        usprintf(s,"KISS: ");
    type = PULLCHAR(bpp);
    t = type & 0x0f;
    if(t == PARAM_DATA || t == PARAM_CRCREQ){
#ifdef MONITOR
        if (!Trace_compact_header)
#endif
            usprintf(s,"Port %d Data\n", type >> 4);
        ax25_dump(s,bpp,check);
        return;
    }
    if(type == PARAM_RETURN){
        usprintf(s,"RETURN\n");
        return;
    } else {
#ifdef MONITOR
        if (!Trace_compact_header)
#endif
            usprintf(s,"Port %d ", type >> 4);
    }
    val = PULLCHAR(bpp);

	/* 12Oct2009, Maiko, Yeesh people - use "%d" for (int) val !!! */
    switch(t){
        case PARAM_TXDELAY:
            usprintf(s,"TX Delay: %d ms\n", val * 10);
            break;
        case PARAM_PERSIST:
            usprintf(s,"Persistence: %d/256\n", val + 1);
            break;
        case PARAM_SLOTTIME:
            usprintf(s,"Slot time: %d ms\n", val * 10);
            break;
        case PARAM_TXTAIL:
            usprintf(s,"TX Tail time: %d ms\n", val * 10);
            break;
        case PARAM_FULLDUP:
            usprintf(s,"Duplex: %s\n",val == 0 ? "Half" : "Full");
            break;
        case PARAM_HW:
            usprintf(s,"Hardware %d\n", val);
            break;
        case PARAM_POLL:
            usprintf(s,"Poll\n");
            break;
        default:
            usprintf(s,"code %d arg %d\n", type, val);
            break;
    }
}
  
int
ki_forus(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
    struct mbuf *bpp;
    int i;
  
    if((bp->data[0] & 0x0f) != PARAM_DATA)
        return 0;
    dup_p(&bpp,bp,1,AXALEN);
    i = ax_forus(iface,bpp);
    free_p(bpp);
    return i;
}
