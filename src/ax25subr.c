/* low level AX25 routines:
 * callsign conversion
 * control block management
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
 /* Mods by G1EMM */
#include "global.h"
#ifdef AX25
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "ax25.h"
#include "lapb.h"
#include <ctype.h>
  
struct ax25_cb *Ax25_cb;
  
/* Default AX.25 parameters */
int32 T2init = 1000;    /* 1 sec acknowledgement delay - K5JB */
int32 T3init = 0;       /* No keep-alive polling */
int32 T4init = 300;     /* 5 Minutes of no I frame tx or rx => redundant link */
int16 Maxframe = 1;     /* Stop and wait */
int16 N2 = 10;          /* 10 retries */
int16 Axwindow = 2048;      /* 2K incoming text before RNR'ing */
int16 Paclen = 256;     /* 256-byte I fields */
int16 Pthresh = 128;        /* Send polls for packets larger than this */
int32 Axirtt = 5000;        /* Initial round trip estimate, ms */
int16 Axversion = V2;       /* Protocol version */
int32 Blimit = 30;      /* Retransmission backoff limit */
  
/* Look up entry in connection table
 * Check BOTH the source AND destination address
 * Added 11/15/91, WG7J/PA3DIS
 */
struct ax25_cb *
find_ax25(local,remote,iface)
char *local;
char *remote;
struct iface *iface;
{
    register struct ax25_cb *axp;
    struct ax25_cb *axlast = NULLAX25;
  
    /* Search list */
    for(axp = Ax25_cb; axp != NULLAX25;axlast=axp,axp = axp->next){
        if(addreq(axp->remote,remote) && addreq(axp->local,local) \
        && axp->iface == iface) {
            if(axlast != NULLAX25){
                /* Move entry to top of list to speed
                 * future searches
                 */
                axlast->next = axp->next;
                axp->next = Ax25_cb;
                Ax25_cb = axp;
            }
            return axp;
        }
    }
    return NULLAX25;
}
  
  
/* Remove address entry from connection table */
void
del_ax25(conn)
struct ax25_cb *conn;
{
    register struct ax25_cb *axp;
    struct ax25_cb *axlast = NULLAX25;
  
    for(axp = Ax25_cb; axp != NULLAX25; axlast=axp,axp = axp->next){
        if(axp == conn)
            break;
    }
  
    if(axp == NULLAX25)
        return;     /* Not found */
  
    /* Remove from list */
    if(axlast != NULLAX25)
        axlast->next = axp->next;
    else
        Ax25_cb = axp->next;
  
    /* Timers should already be stopped, but just in case... */
    stop_timer(&axp->t1);
    stop_timer(&axp->t2);	/* K5JB */
    stop_timer(&axp->t3);
    stop_timer(&axp->t4);
  
    axp->r_upcall = NULLVFP((struct ax25_cb*,int));
    axp->s_upcall = NULLVFP((struct ax25_cb*,int,int));
    axp->t_upcall = NULLVFP((struct ax25_cb*,int));
  
    /* Free allocated resources */
    free_q(&axp->txq);
    free_q(&axp->rxasm);
    free_q(&axp->rxq);
    free((char *)axp);
}
  
/* Create an ax25 control block. Allocate a new structure, if necessary,
 * and fill it with all the defaults.
 */
/* This takes BOTH source and destination address.
 * 11/15/91, WG7J/PA3DIS
 */
/* Modified to take the Irtt from the interface - 11/22/93 - WG7J */
struct ax25_cb *
cr_ax25(local,remote,iface)
char *local;
char *remote;
struct iface *iface;
{
    struct ax25_cb *axp;
    struct ifax25 *ifax;
  
    if((remote == NULLCHAR) || (local == NULLCHAR))
        return NULLAX25;
  
    /* Create an entry
     * and insert it at the head of the chain
     */
    axp = (struct ax25_cb *)callocw(1,sizeof(struct ax25_cb));
    axp->next = Ax25_cb;
    Ax25_cb = axp;
  
    /*fill in 'defaults'*/
    memcpy(axp->local,local,AXALEN);
    memcpy(axp->remote,remote,AXALEN);
    axp->user = -1;
    axp->state = LAPB_DISCONNECTED;
  
    if(iface && (ifax=iface ->ax25) != NULL) {
        axp->maxframe = ifax->maxframe;
        axp->window = ifax->window;
        axp->iface = iface;
        axp->paclen = ifax->paclen;
        axp->proto = ifax->version; /* Default, can be changed by other end */
        axp->pthresh = ifax->pthresh;
        axp->n2 = ifax->n2;
        axp->srt = ifax->irtt;
        set_timer(&axp->t1,2*axp->srt);
        set_timer(&axp->t2,ifax->t2);
        set_timer(&axp->t3,ifax->t3);
        set_timer(&axp->t4,ifax->t4 * 1000);
    }
    axp->t1.func = recover;
    axp->t1.arg = axp;
  
    axp->t2.func = dlapb_output;	/* K5JB */
    axp->t2.arg = axp;

    axp->t3.func = pollthem;
    axp->t3.arg = axp;
  
    axp->t4.func = redundant;
    axp->t4.arg = axp;
  
    /* Always to a receive and state upcall as default */
    /* Also bung in a default transmit upcall - in case */
    axp->r_upcall = s_arcall;
    axp->s_upcall = s_ascall;
    axp->t_upcall = s_atcall;
  
    return axp;
}
  
/*
 * setcall - convert callsign plus substation ID of the form
 * "KA9Q-0" to AX.25 (shifted) address format
 *   Address extension bit is left clear
 *   Return -1 on error, 0 if OK
 */
int
setcall(out,call)
char *out;
char *call;
{
    int csize;
    unsigned ssid;
    register int i;
    register char *dp;
    char c;
  
    if(out == NULLCHAR || call == NULLCHAR || *call == '\0')
        return -1;
  
    /* Find dash, if any, separating callsign from ssid
     * Then compute length of callsign field and make sure
     * it isn't excessive
     */
    dp = strchr(call,'-');
    if(dp == NULLCHAR)
        csize = strlen(call);
    else
        csize = (int)(dp - call);
    if(csize > ALEN)
        return -1;
    /* Now find and convert ssid, if any */
    if(dp != NULLCHAR){
        dp++;   /* skip dash */
        ssid = atoi(dp);
        if(ssid > 15)
            return -1;
    } else
        ssid = 0;
    /* Copy upper-case callsign, left shifted one bit */
    for(i=0;i<csize;i++){
        c = *call++;
        if(islower(c))
            c = toupper(c);
        *out++ = c << 1;
    }
    /* Pad with shifted spaces if necessary */
    for(;i<ALEN;i++)
        *out++ = ' ' << 1;
  
    /* Insert substation ID field and set reserved bits */
    *out = 0x60 | (ssid << 1);
    return 0;
}
int
addreq(a,b)
register char *a,*b;
{
    if(memcmp(a,b,ALEN) != 0 || ((a[ALEN] ^ b[ALEN]) & SSID) != 0)
        return 0;
    else
        return 1;
}
/* Convert encoded AX.25 address to printable string */
char *
pax25(e,addr)
char *e;
char *addr;
{
    register int i;
    char c;
    char *cp;
  
    cp = e;
    for(i=ALEN;i != 0;i--){
        c = (*addr++ >> 1) & 0x7f;
        if(c != ' ')
            *cp++ = c;
    }
    if ((*addr & SSID) != 0)
        sprintf(cp,"-%d",(*addr >> 1) & 0xf);   /* ssid */
    else
        *cp = '\0';
    return e;
}
  
/* Figure out the frame type from the control field
 * This is done by masking out any sequence numbers and the
 * poll/final bit after determining the general class (I/S/U) of the frame
 */
int16
ftype(control)
register int control;
{
    if((control & 1) == 0)  /* An I-frame is an I-frame... */
        return I;
    if(control & 2)     /* U-frames use all except P/F bit for type */
        return (int16)(uchar(control) & ~PF);
    else            /* S-frames use low order 4 bits for type */
        return (int16)(uchar(control) & 0xf);
}
  
#ifndef UNIX
  
void
lapb_garbage(red)
int red;
{
    register struct ax25_cb *axp;
  
    for(axp=Ax25_cb;axp != NULLAX25;axp = axp->next){
        mbuf_crunch(&axp->rxq);
        mbuf_crunch(&axp->rxasm);
    }
}
#endif /* UNIX */
  
#endif /* AX25 */
  
