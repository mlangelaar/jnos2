/* User interface subroutines for AX.25
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#ifdef AX25
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "lapb.h"
#include "ax25.h"
#include "lapb.h"
#include <ctype.h>
  
/* Open an AX.25 connection */
struct ax25_cb *
open_ax25(iface,local,remote,mode,window,r_upcall,t_upcall,s_upcall,user)
struct iface *iface;    /* Interface */
char *local;        /* Local address */
char *remote;       /* Remote address */
int mode;       /* active/passive/server */
int16 window;       /* Window size in bytes */
void (*r_upcall)__ARGS((struct ax25_cb *,int));       /* Receiver upcall handler */
void (*t_upcall)__ARGS((struct ax25_cb *,int));   /* Transmitter upcall handler */
void (*s_upcall)__ARGS((struct ax25_cb *,int,int));   /* State-change upcall handler */
int user;       /* User linkage area */
{
    struct ax25_cb *axp;
    char remtmp[AXALEN];
  
    if(remote == NULLCHAR){
        remote = remtmp;
        setcall(remote," ");
    }
    if((axp = find_ax25(local,remote,iface)) != NULLAX25 && axp->state != LAPB_DISCONNECTED)
        return NULLAX25;    /* Only one to a customer */
    if(axp == NULLAX25 && (axp = cr_ax25(local,remote,iface)) == NULLAX25)
        return NULLAX25;
/*
    memcpy(axp->remote,remote,AXALEN);
    memcpy(axp->local,local,AXALEN);
 */
    if(window)  /* If given, use it. Otherwize use interface default */
        axp->window = window;
    axp->r_upcall = r_upcall;
    axp->t_upcall = t_upcall;
    axp->s_upcall = s_upcall;
    axp->user = user;
  
    switch(mode){
        case AX_SERVER:
            axp->flags.clone = 1;
        case AX_PASSIVE:    /* Note fall-thru */
            axp->state = LAPB_LISTEN;
            return axp;
        case AX_ACTIVE:
            break;
    }
    switch(axp->state){
        case LAPB_DISCONNECTED:
            est_link(axp);
            lapbstate(axp,LAPB_SETUP);
            break;
        case LAPB_SETUP:
            free_q(&axp->txq);
            break;
        case LAPB_DISCPENDING:  /* Ignore */
            break;
        case LAPB_RECOVERY:
        case LAPB_CONNECTED:
            free_q(&axp->txq);
            est_link(axp);
            lapbstate(axp,LAPB_SETUP);
            break;
    }
    return axp;
}
  
/* Send data on an AX.25 connection. Caller provides optional PID. If
 * a PID is provided, then operate in stream mode, i.e., a large packet
 * is automatically packetized into a series of paclen-sized data fields.
 *
 * If pid == -1, it is assumed the packet (which may actually be a queue
 * of distinct packets) already has a PID on the front and it is passed
 * through directly even if it is very large.
 */
int
send_ax25(axp,bp,pid)
struct ax25_cb *axp;
struct mbuf *bp;
int pid;
{
    struct mbuf *bp1;
    int16 offset,len,size;
  
    if(axp == NULLAX25 || bp == NULLBUF) {
        free_p(bp);
        return -1;
    }
  
    if(pid != -1){
        offset = 0;
        len = len_p(bp);
        /* It is important that all the pushdowns be done before
         * any part of the original packet is freed.
         * Otherwise the pushdown might erroneously overwrite
         * a part of the packet that had been duped and freed.
         */
        while(len != 0){
            size = min(len,axp->paclen);
            dup_p(&bp1,bp,offset,size);
            len -= size;
            offset += size;
            bp1 = pushdown(bp1,1);
            bp1->data[0] = pid;
            enqueue(&axp->txq,bp1);
        }
        free_p(bp);
    } else {
        enqueue(&axp->txq,bp);
    }
    lapb_output(axp);
    return 0;	/* don't think anybody uses this value */
}
  
/* Receive incoming data on an AX.25 connection */
struct mbuf *
recv_ax25(axp,cnt)
struct ax25_cb *axp;
int16 cnt;
{
    struct mbuf *bp;
  
    if(axp->rxq == NULLBUF)
        return NULLBUF;
  
    if(cnt == 0){
        /* This means we want it all */
        bp = axp->rxq;
        axp->rxq = NULLBUF;
    } else {
        bp = ambufw(cnt);
        bp->cnt = pullup(&axp->rxq,bp->data,cnt);
    }
    /* If this has un-busied us, send a RR to reopen the window */
    if(len_p(axp->rxq) < axp->window
        && (len_p(axp->rxq) + bp->cnt) >= axp->window)
        sendctl(axp,LAPB_RESPONSE,RR);
  
    return bp;
}
  
/* Close an AX.25 connection */
int
disc_ax25(axp)
struct ax25_cb *axp;
{
    if(axp == NULLAX25)
        return -1;
    switch(axp->state){
        case LAPB_DISCONNECTED:
            break;      /* Ignored */
        case LAPB_LISTEN:
            del_ax25(axp);
            break;
        case LAPB_DISCPENDING:
            lapbstate(axp,LAPB_DISCONNECTED);
            break;
        case LAPB_CONNECTED:
        case LAPB_RECOVERY:
            free_q(&axp->txq);
            axp->retries = 0;
            sendctl(axp,LAPB_COMMAND,DISC|PF);
            stop_timer(&axp->t3);
            start_timer(&axp->t1);
            lapbstate(axp,LAPB_DISCPENDING);
            break;
    }
    return 0;
}

/*
 * 31May2007, Maiko (VE4KLM), retrieve ax25 CB using a remote call, or the
 * original &AXB (8 digit hex value) shown when one does 'ax25 status'. I
 * wrote this function so I could quickly and conveniently reset, kick, or
 * delete any ax25 connection simply by specifying a remote call, instead
 * of struggling with the 8 digit hex value. I find it much easier, quicker,
 * more convenient to use a remote callsign - BUT I have still preserved
 * the original way to retrieve using the &AXB (8 digit hex value).
 *
 * The ax25val() function is no more, I've removed it. This function will
 * now be used instead (in ax25cmd.c). The kick_ax25() function has been
 * modified to do it's own 'ax25val' checks before trying to recover.
 *
 * 10Mar2021, Maiko, now have to account for 12 digit hex value, seems
 * more recent distros (Ubuntu 20 and/or GCC 9+) using 48 bit addr ?
 */
struct ax25_cb *getax25cb (char *axcbid)
{
    register struct ax25_cb *axpd, *axp = NULLAX25;

	char ax25rcall[AXALEN];
 
	if (isdigit (*axcbid)) 
	{
	    /* 09Mar20121, Maiko (VE4KLM), replace htoi with htol, and better way anyways */
    	axpd = (void*)htol (axcbid);

    	if (axpd != NULLAX25)
		{
	    	for (axp = Ax25_cb; axp != NULLAX25; axp = axp->next)
        		if (axp == axpd)
					break;
		}
	}
	else
	{
		setcall (ax25rcall, axcbid);

		for (axp = Ax25_cb; axp != NULLAX25; axp = axp->next)
			if (addreq (axp->remote, ax25rcall))
				break;
	}

	return axp;
}

/* Force a retransmission */
int
kick_ax25(axp)
struct ax25_cb *axp;
{
	register struct ax25_cb *axpd;

	if (axp != NULLAX25)
	{
	   	for (axpd = Ax25_cb; axpd != NULLAX25; axpd = axpd->next)
		{
       		if (axp == axpd)
			{
				recover (axp);
				break;
			}
		}
	}

	return 0;
}
  
/* Abruptly terminate an AX.25 connection */
int
reset_ax25(axp)
struct ax25_cb *axp;
{
    void (*upcall)__ARGS((struct ax25_cb *,int,int));
  
    if(axp == NULLAX25)
        return -1;
    /* Be nice and send a DM - WG7J */
    disc_ax25(axp);
    upcall = (void (*)(struct ax25_cb *,int,int)) axp->s_upcall;
    lapbstate(axp,LAPB_DISCONNECTED);
    /* Clean up if the standard upcall isn't in use */
    if(upcall != s_ascall)
        del_ax25(axp);
    return 0;
}
  
#endif
  
