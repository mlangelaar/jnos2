/* AX25 header tracing
 * Copyright 1991 Phil Karn, KA9Q
 */
 /* Mods by PA0GRI */
/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
#include "global.h"
#ifdef AX25
#include "mbuf.h"
#include "proc.h"
#include "ax25.h"
#include "lapb.h"
#include "trace.h"
#include "socket.h"

#ifdef UNIX
/* We can't use usputc since we don't intercept that call for rerouting to
   the trace screen (for efficiency or stubbornness, hard to say which! n5knx
   */
#define PUTNL(s)	usputs(s,"\n")
#else
#define PUTNL(s)	usputc(s,'\n')
#endif
  
static char *decode_type __ARGS((int16 type));
  
/* Dump an AX.25 packet header */
void
ax25_dump(s,bpp,check)
int s;
struct mbuf **bpp;
int check;  /* Not used */
{
    char tmp[AXBUF];
    char frmr[3];
    int control,pid,seg;
    int16 type;
    int unsegmented;
    struct ax25 hdr;
    char *hp;
  
#ifdef MONITOR
    if (!Trace_compact_header)
#endif
        usprintf(s,"AX25: ");
    /* Extract the address header */
    if(ntohax25(&hdr,bpp) < 0){
        /* Something wrong with the header */
        usprintf(s," bad header!\n");
        return;
    }
    usprintf(s,"%s",pax25(tmp,hdr.source));
    usprintf(s,"->%s",pax25(tmp,hdr.dest));
    if(hdr.ndigis > 0){
#ifdef MONITOR
        if (!Trace_compact_header)
#endif
            usprintf(s," v");
        for(hp = hdr.digis[0]; hp < &hdr.digis[hdr.ndigis][0];
        hp += AXALEN){
            /* Print digi string */
            usprintf(s," %s%s",pax25(tmp,hp),
            (hp[ALEN] & REPEATED) ? "*":"");
        }
    }
    if((control = PULLCHAR(bpp)) == -1)
        return;
  
    type = ftype(control);
#ifdef MONITOR
    if (!Trace_compact_header || (type != I && type != UI))
    {
#endif
        usprintf(s," %s",decode_type(type));
    /* Dump poll/final bit */
        if(control & PF){
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, "!");
            else
#endif
            switch(hdr.cmdrsp){
                case LAPB_COMMAND:
                    usprintf(s,"(P)");
                    break;
                case LAPB_RESPONSE:
                    usprintf(s,"(F)");
                    break;
                default:
                    usprintf(s,"(P/F)");
                    break;
            }
        }
#ifdef MONITOR
    }
#endif
    /* Dump sequence numbers */
#ifdef MONITOR
    if (!Trace_compact_header)
#endif
        if((type & 0x3) != U)   /* I or S frame? */
            usprintf(s," NR=%d",(control>>5)&7);
    if(type == I || type == UI){
#ifdef MONITOR
        if (!Trace_compact_header)
#endif
            if(type == I)
                usprintf(s," NS=%d",(control>>1)&7);
        /* Decode I field */
        if((pid = PULLCHAR(bpp)) != -1){    /* Get pid */
            if(pid == PID_SEGMENT){
                unsegmented = 0;
                seg = PULLCHAR(bpp);
#ifdef MONITOR
                if (!Trace_compact_header)
#endif
                    usprintf(s,"%s remain %u",seg & SEG_FIRST ?
                    " First seg;" : "",seg & SEG_REM);
                if(seg & SEG_FIRST)
                    pid = PULLCHAR(bpp);
            } else
                unsegmented = 1;
  
            switch(pid){
                case PID_SEGMENT:
                    PUTNL(s);
                    break;  /* Already displayed */
                case PID_ARP:
#ifdef MONITOR
                    if (Trace_compact_header)
                        usprintf(s, " ARP ");
                    else
#endif
                        usprintf(s," pid=ARP\n");
                    arp_dump(s,bpp);
                    break;
                case PID_RARP:
#ifdef MONITOR
                    if (Trace_compact_header)
                        usprintf(s, " RARP ");
                    else
#endif
                        usprintf(s," pid=RARP\n");
                    arp_dump(s,bpp);
                    break;
#ifdef NETROM
                case PID_NETROM:
#ifdef MONITOR
                    if (Trace_compact_header)
                        usputs(s, " ");
                    else
#endif
                        usprintf(s," pid=NET/ROM\n");
                /* Don't verify checksums unless unsegmented */
                    netrom_dump(s,bpp,unsegmented, type);
                    break;
#endif
                case PID_IP:
#ifdef MONITOR
                    if (Trace_compact_header)
                        usprintf(s, " IP ");
                    else
#endif
                        usprintf(s," pid=IP\n");
                /* Don't verify checksums unless unsegmented */
                    ip_dump(s,bpp,unsegmented);
                    break;
                case PID_X25:
#ifdef MONITOR
                    if (Trace_compact_header)
                        usprintf(s, " X.25\n");
                    else
#endif
                        usprintf(s," pid=X.25\n");
                    break;
                case PID_TEXNET:
#ifdef MONITOR
                    if (Trace_compact_header)
                        usprintf(s, " TEXNET\n");
                    else
#endif
                        usprintf(s," pid=TEXNET\n");
                    break;
                case PID_NO_L3:
#ifdef MONITOR
                    if (Trace_compact_header)
                        usprintf(s, "\n");
                    else
#endif
                        usprintf(s," pid=Text\n");
                    break;
                default:
#ifdef MONITOR
                    if (Trace_compact_header)
                        usprintf(s, " [0x%x]\n", pid);
                    else
#endif
                        usprintf(s," pid=0x%x\n",pid);
            }
        }
        else PUTNL(s);
    } else if(type == FRMR && pullup(bpp,frmr,3) == 3){
        usprintf(s,": %s",decode_type(ftype(frmr[0])));
        usprintf(s," Vr = %d Vs = %d",(frmr[1] >> 5) & MMASK,
        (frmr[1] >> 1) & MMASK);
        if(frmr[2] & W)
            usprintf(s," Invalid control field");
        if(frmr[2] & X)
            usprintf(s," Illegal I-field");
        if(frmr[2] & Y)
            usprintf(s," Too-long I-field");
        if(frmr[2] & Z)
            usprintf(s," Invalid seq number");
        PUTNL(s);
    } else
        PUTNL(s);
  
}
static char *
decode_type(type)
int16 type;
{
    switch(type){
        case I:
            return "I";
        case SABM:
            return "SABM";
        case DISC:
            return "DISC";
        case DM:
            return "DM";
        case UA:
            return "UA";
        case RR:
            return "RR";
        case RNR:
            return "RNR";
        case REJ:
            return "REJ";
        case FRMR:
            return "FRMR";
        case UI:
            return "UI";
        default:
            return "[invalid]";
    }
}
  
/* Return 1 if this packet is directed to us, 0 otherwise. Note that
 * this checks only the ultimate destination, not the digipeater field
 */
int
ax_forus(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
    struct mbuf *bpp;
    char dest[AXALEN];
  
    /* Duplicate the destination address */
    if(dup_p(&bpp,bp,0,AXALEN) != AXALEN){
        free_p(bpp);
        return 0;
    }
    if(pullup(&bpp,dest,AXALEN) < AXALEN)
        return 0;
    if(addreq(dest,iface->hwaddr))
        return 1;
    else
        if(addreq(dest,iface->ax25->cdigi))
            return 1;
#ifdef MAILBOX
    else
        if(addreq(dest,iface->ax25->bbscall))
            return 1;
#endif
    else
        return 0;
}
  
#endif /* AX25 */
  
