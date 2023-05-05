/* Polled Kiss ala G8BPQ for JNOS
 * Copyright 1993, Johan. K. Reinalda, WG7J
 * Hereby placed in the public domain.
 */
#include "global.h"
#include "slip.h"
#include "kiss.h"
#include "asy.h"
#include "devparam.h"
#include "kisspoll.h"
  
#ifdef POLLEDKISS
  
#define WAITTIME 165
  
void kiss_poller(int xdev,void *p1,void *p2) {
    int i;
    long interval = (long)p1;
    struct mbuf *bp;
    struct iface *master,*iface;
    struct slip *sp = &Slip[xdev];
  
    /* The actual physical interface */
    master = sp->kiss[0];
  
    while (1) {
        for(i=0;i<16;i++) {
            if((iface=sp->kiss[i]) == NULL)
                continue;
  
            /* Get buffer for the poll packet, and generate it */
            if((bp = alloc_mbuf(2)) == NULL)
                continue;
            bp->data[0] = PARAM_POLL;
            bp->data[0] |= (i << 4);
            bp->data[1] = bp->data[0];  /* The checksum */
            bp->cnt = 2;
  
            if(iface->port){
                iface->rawsndcnt++;
                iface->lastsent = secclock();
            }
            /* slip_raw also increments sndrawcnt */
            slip_raw(master,bp);
  
            j2pause(WAITTIME);    /* give time to start sending frame */
  
            if(sp->rx) {
                /* if frame busy, wait for a full frame (max) */
                j2alarm ((int32)interval);
                pwait(&sp->rx);
            }
        }
    }
}
  
#endif /* POLLEDKISS */
  
