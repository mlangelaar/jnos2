/* SLIP (Serial Line IP) encapsulation and control routines.
 * Copyright 1991 Phil Karn
 *
 * Van Jacobsen header compression hooks added by Katie Stevens, UC Davis
 *
 *  - Feb 1991  Bill_Simpson@um.cc.umich.edu
 *          reflect changes to header compression calls
 *          revise status display
 */
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "ip.h"
#include "slhc.h"
#include "asy.h"
#include "slip.h"
#include "trace.h"

/*
 * 27/28Feb2009, Maiko, Make these available outside of this module,
 * since I need to call them from within the new multipsk.c module.
 */  
struct mbuf *slip_decode (register struct slip *sp, char c);

struct mbuf *slip_encode (struct mbuf *bp, int usecrc);
  
/* Slip level control structure */
struct slip Slip[ASY_MAX];
  
/* Send routine for point-to-point slip */
int
slip_send(bp,iface,gateway,prec,del,tput,rel)
struct mbuf *bp;    /* Buffer to send */
struct iface *iface;    /* Pointer to interface control block */
int32 gateway;      /* Ignored (SLIP is point-to-point) */
int prec;
int del;
int tput;
int rel;
{
#ifdef VJCOMPRESS
    register struct slip *sp;
    int type;
#endif
    if(iface == NULLIF){
        free_p(bp);
        return -1;
    }
#ifdef VJCOMPRESS
    sp = &Slip[iface->xdev];
    if (sp->escaped & SLIP_VJCOMPR) {
        /* Attempt IP/ICP header compression */
        type = slhc_compress(sp->slcomp,&bp,TRUE);
        bp->data[0] |= type;
    }
#endif
    return (*iface->raw)(iface,bp);
}
/* Send a raw slip frame */
int
slip_raw(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
    struct mbuf *bp1;
    struct slip *sp;
  
    dump(iface,IF_TRACE_OUT,Slip[iface->xdev].type,bp);
    iface->rawsndcnt++;
#ifdef J2_SNMPD_VARS
	// log (-1, "%s %d", iface->name, iface->rawsndbytes);
    iface->rawsndbytes += len_p (bp);
	// log (-1, "%s %d", iface->name, iface->rawsndbytes);
#endif
    iface->lastsent = secclock();
    sp =&Slip[iface->xdev];
  
    if((bp1 = slip_encode(bp,sp->usecrc)) == NULLBUF){
        return -1;
    }
    if (iface->trace & IF_TRACE_RAW)
        raw_dump(iface,-1,bp1);
    return sp->send(iface->dev,bp1);
}

/* Encode a packet in SLIP format */
struct mbuf *slip_encode (struct mbuf *bp, int usecrc)
{
    struct mbuf *lbp;   /* Mbuf containing line-ready packet */
    register char *cp;
    int c,crc,done;
  
    /* Allocate output mbuf that's twice as long as the packet.
     * This is a worst-case guess (consider a packet full of FR_ENDs!)
     */
    lbp = alloc_mbuf((int16)(2*len_p(bp) + 3));
    if(lbp == NULLBUF){
        /* No space; drop */
        free_p(bp);
        return NULLBUF;
    }
    cp = lbp->data;
  
    /* Flush out any line garbage */
    *cp++ = FR_END;
  
    /* Set crc to zero */
    crc = 0;
    done = 0;
  
    /* Copy input to output, escaping special characters */
    while(1) {
        c = PULLCHAR(&bp);
        if(c == -1) {       /* If we reached the end of the data */
            if(!usecrc)     /* and we don't use a CRC, */
                break;      /* we're done... */
            c = crc;        /* otherwise, encode the crc as well */
            done = 1;
        } else              /* A regular data char, calculate crc */
            crc ^= c;
        switch(c){
            case FR_ESC:
                *cp++ = FR_ESC;
                *cp++ = T_FR_ESC;
                break;
            case FR_END:
                *cp++ = FR_ESC;
                *cp++ = T_FR_END;
                break;
            default:
                *cp++ = c;
        }
        if(done)
            break;
    }
    *cp++ = FR_END;
    lbp->cnt = (int16)(cp - lbp->data);
  
    return lbp;
}
/* Process incoming bytes in SLIP format
 * When a buffer is complete, return it; otherwise NULLBUF
 */
struct mbuf *slip_decode (register struct slip *sp, char c)
{
    struct mbuf *bp;
#ifdef POLLEDKISS
    struct mbuf *bp1, *bprev;
#endif
  
    switch(uchar(c)){
        case FR_END:
            bp = sp->rbp_head;
            sp->rbp_head = NULLBUF;
#ifdef POLLEDKISS
            if(sp->polled) {
                sp->rx = 0;
                j2psignal(&sp->rx,1); /* tell the poller we're done */
            }
            if(sp->usecrc) {
                if(bp) {
                /* Check the crc. After an ex-or of data and checksum
                 * the result should be 0 - WG7J
                 */
                    if(sp->rxcrc) {
                        free_p(bp);
                        bp = NULLBUF;
                    }
                /* If valid, delete the crc byte from the data */
                /* If the length of the last mbuf in chain becomes
                 * zero, we must delete it. Otherwise upper level code
                 * thinks we lost connection - OH2BNS
                 */
                    else if(--sp->rbp_tail->cnt == 0) {
                        for(bp1=bp, bprev=NULLBUF; bp1->cnt!=0; bprev=bp1,bp1=bp1->next)
                            ;
                        free_p(bp1);
                        if (bprev)
                            bprev->next = NULLBUF;
                        else
                            bp = NULLBUF;  /* minimal frame (crc) (FR_END) ignored */
                    }
                }
            }
#endif
            return bp;  /* Will be NULLBUF if empty frame */
        case FR_ESC:
            sp->escaped |= SLIP_FLAG;
            return NULLBUF;
    }
    if(sp->escaped & SLIP_FLAG){
        /* Translate 2-char escape sequence back to original char */
        sp->escaped &= ~SLIP_FLAG;
        switch(uchar(c)){
            case T_FR_ESC:
                c = FR_ESC;
                break;
            case T_FR_END:
                c = FR_END;
                break;
            default:
                sp->errors++;
                break;
        }
    }
    /* We reach here with a character for the buffer;
     * make sure there's space for it
     */
    if(sp->rbp_head == NULLBUF){
        /* Allocate first mbuf for new packet */
        if((sp->rbp_tail = sp->rbp_head = alloc_mbuf(SLIP_ALLOC)) == NULLBUF)
            return NULLBUF; /* No memory, drop */
        sp->rcp = sp->rbp_head->data;
#ifdef POLLEDKISS
        /* If polled kiss, signal poller that we are receiving a packet */
        if(sp->polled)
            sp->rx = 1;
        if(sp->usecrc)  /* clear the crc - WG7J */
            sp->rxcrc = 0;
#endif
    } else if(sp->rbp_tail->cnt == SLIP_ALLOC){
        /* Current mbuf is full; link in another */
        if((sp->rbp_tail->next = alloc_mbuf(SLIP_ALLOC)) == NULLBUF){
            /* No memory, drop whole thing */
            free_p(sp->rbp_head);
            sp->rbp_head = NULLBUF;
            return NULLBUF;
        }
        sp->rbp_tail = sp->rbp_tail->next;
        sp->rcp = sp->rbp_tail->data;
    }
    /* Store the character, increment fragment and total
     * byte counts
     */
    *sp->rcp++ = c;
    sp->rbp_tail->cnt++;
#ifdef POLLEDKISS
    /* If this interface uses CRC, adjust the crc */
    if(sp->usecrc)
        sp->rxcrc ^= c;
#endif
    return NULLBUF;
}
  
  
/* Process SLIP line input */
void
asy_rx(xdev,p1,p2)
int xdev;
void *p1;
void *p2;
{
    int c;
    struct mbuf *bp;
    register struct slip *sp;
    int cdev;
  
    sp = &Slip[xdev];
    cdev = sp->iface->dev;
  
    bp = NULL;
    while ( (c = sp->get(cdev)) != -1 ) {
        if((bp = slip_decode(sp,(char)c)) == NULLBUF)
            continue;   /* More to come */
  
#ifdef VJCOMPRESS
        if (sp->iface->trace & IF_TRACE_RAW)
            raw_dump(sp->iface,IF_TRACE_IN,bp);
  
        if (sp->escaped & SLIP_VJCOMPR) {
            if ((c = bp->data[0]) & SL_TYPE_COMPRESSED_TCP) {
                if ( slhc_uncompress(sp->slcomp, &bp) <= 0 ) {
                    free_p(bp);
                    sp->errors++;
                    continue;
                }
            } else if (c >= SL_TYPE_UNCOMPRESSED_TCP) {
                bp->data[0] &= 0x4f;
                if ( slhc_remember(sp->slcomp, &bp) <= 0 ) {
                    free_p(bp);
                    sp->errors++;
                    continue;
                }
            }
        }
#endif
        if ( net_route( sp->iface, sp->type, bp ) != 0 ) {
            free_p(bp);
            bp = NULL;
        }
    }
    if(bp)
        free_p(bp);
}
  
  
  
/* Show serial line status */
void
slip_status(iface)
struct iface *iface;
{
    struct slip *sp;
  
    if (iface->xdev > SLIP_MAX)
        /* Must not be a SLIP device */
        return;
  
    sp = &Slip[iface->xdev];
    if (sp->iface != iface)
        /* Must not be a SLIP device */
        return;
  
    tprintf("  IN:\t%d pkts\n", iface->rawrecvcnt);
#ifdef VJCOMPRESS
    slhc_i_status(sp->slcomp);
#endif
    tprintf("  OUT:\t%d pkts\n", iface->rawsndcnt);
#ifdef VJCOMPRESS
    slhc_o_status(sp->slcomp);
#endif
}
  
  
