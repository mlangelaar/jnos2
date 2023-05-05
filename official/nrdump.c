/*
 * JNOS 2.0
 *
 * $Id: nrdump.c,v 1.3 2012/03/20 16:30:03 ve4klm Exp $
 *
 * NET/ROM header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J
 */
#include "global.h"
#ifdef NETROM
#include "mbuf.h"
#include "netrom.h"
#include "nr4.h"
#include "trace.h"
#include "socket.h"  

/* Display NET/ROM network and transport headers */
void
netrom_dump(s,bpp,check,type)
int s;
struct mbuf **bpp;
int check;
int16 type;
{
    char src[AXALEN],dest[AXALEN];
    char tmp[AXBUF];
    char thdr[NR4MINHDR];
    register int i;
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;

    /* See if it is a routing broadcast */
	if (uchar(*(*bpp)->data) == NR3NODESIG)
	{
        (void)PULLCHAR(bpp);        /* Signature */

#ifdef	DONT_COMPILE

		/* 20Aug2011, Maiko, This can crash JNOS if wrong data comes in */

		/*
		 * 20Sep2005, Maiko, INP3 sends it's RIF records using NR3NODESIG
		 * in CONNECTED frames, BUT it does NOT put a L3 header on it !!!
		 */
		if (type == I)
		{
			char tmpalias[15];

			pullup (bpp, src, AXALEN);
			pullup (bpp, dest, 5);
			pullup (bpp, tmpalias, (int16)dest[3] - 2);

			usprintf (s, "INP3 RIF: %s hops %d tt %d %d alias %s\n",
				pax25(tmp,src), (int)dest[0], (int)dest[1],
					(int)dest[2], tmpalias);

			return;
		}
#endif
        pullup(bpp,tmp,ALEN);
        tmp[ALEN] = '\0';
        usprintf(s,"NET/ROM Routing: %s\n",tmp);
        for(i = 0;i < NRDESTPERPACK;i++) {
            if (pullup(bpp,src,AXALEN) < AXALEN)
                break;
            usprintf(s,"        %12s",pax25(tmp,src));
            pullup(bpp,tmp,ALEN);
            tmp[ALEN] = '\0';
            usprintf(s,"%8s",tmp);
            pullup(bpp,src,AXALEN);
            usprintf(s,"    %12s",pax25(tmp,src));
            tmp[0] = PULLCHAR(bpp);
            usprintf(s,"    %3u\n",uchar(tmp[0]));
        }
        return;
    }
    /* See if it is a routing poll - WG7J */
    if(uchar(*(*bpp)->data) == NR3POLLSIG) {
        (void)PULLCHAR(bpp);        /* Signature */
        pullup(bpp,tmp,ALEN);
        tmp[ALEN] = '\0';
        usprintf(s,"NET/ROM Poll: %s\n",tmp);
        return;
    }
    /* Decode network layer */
    pullup(bpp,src,AXALEN);
#ifdef MONITOR
    if (Trace_compact_header)
        usprintf(s, "NET/ROM %s", pax25(tmp, src));
    else
#endif
        usprintf(s,"NET/ROM: %s",pax25(tmp,src));
  
    pullup(bpp,dest,AXALEN);
    usprintf(s,"->%s",pax25(tmp,dest));
  
    i = PULLCHAR(bpp);
#ifdef MONITOR
    if (!Trace_compact_header)
#endif
        usprintf(s," ttl %d\n",i);
  
    /* Read first five bytes of "transport" header */
    pullup(bpp,thdr,NR4MINHDR);

#ifdef	INP3
	if (thdr[0] == 0x00 &&
		thdr[1] == 0x01 &&
		thdr[2] == 0x00 &&
		thdr[3] == 0x00 &&
		thdr[4] == 0x00)
	{
		pullup (bpp, src, AXALEN);
		pullup (bpp, dest, 1);
		usprintf (s, "INP3 NRR: %s %d\n", pax25 (tmp,src), (int)dest[0]);
		return;
	}
#endif

    switch(thdr[4] & NR4OPCODE){
        case NR4OPPID:  /* network PID extension */
            if (thdr[0] == NRPROTO_IP && thdr[1] == NRPROTO_IP) {
#ifdef MONITOR
                if (Trace_compact_header)
                    usprintf(s, " [IP]:\n");
#endif
                ip_dump(s,bpp,check) ;
                return;
            }
            else
#ifdef MONITOR
                if (Trace_compact_header)
                    usprintf(s, " [%x/%x]:\n",uchar(thdr[0]), uchar(thdr[1]));
                else
#endif
                    usprintf(s,"         protocol family %x, proto %x",
                    uchar(thdr[0]), uchar(thdr[1])) ;
            break ;
        case NR4OPCONRQ:    /* Connect request */
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, " conn req %d/%d", uchar(thdr[0]), uchar(thdr[1]));
            else
#endif
                usprintf(s,"         conn rqst: ckt %d/%d",uchar(thdr[0]),uchar(thdr[1]));
            i = PULLCHAR(bpp);
#ifdef MONITOR
            if (!Trace_compact_header)
#endif
                usprintf(s," wnd %d",i);
            pullup(bpp,src,AXALEN);
            usprintf(s," %s",pax25(tmp,src));
            pullup(bpp,dest,AXALEN);
            usprintf(s,"@%s",pax25(tmp,dest));
            break;
        case NR4OPCONAK:    /* Connect acknowledgement */
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, " conn ack %d/%d->%d/%d",
                uchar(thdr[0]), uchar(thdr[1]),
                uchar(thdr[2]), uchar(thdr[3]));
            else
#endif
                usprintf(s,"         conn ack: ur ckt %d/%d my ckt %d/%d",
                uchar(thdr[0]), uchar(thdr[1]), uchar(thdr[2]),
                uchar(thdr[3]));
            i = PULLCHAR(bpp);
#ifdef MONITOR
            if (!Trace_compact_header)
#endif
                usprintf(s," wnd %d",i);
            break;
        case NR4OPDISRQ:    /* Disconnect request */
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, " disc %d/%d",uchar(thdr[0]), uchar(thdr[1]));
            else
#endif
                usprintf(s,"         disc: ckt %d/%d",
                uchar(thdr[0]),uchar(thdr[1]));
            break;
        case NR4OPDISAK:    /* Disconnect acknowledgement */
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, " disc ack %d/%d", uchar(thdr[0]),uchar(thdr[1]));
            else
#endif
                usprintf(s,"         disc ack: ckt %d/%d",
                uchar(thdr[0]),uchar(thdr[1]));
            break;
        case NR4OPINFO: /* Information (data) */
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, " %d/%d", uchar(thdr[0]),uchar(thdr[1]));
            else
            {
#endif
                usprintf(s,"         info: ckt %d/%d",
                uchar(thdr[0]),uchar(thdr[1]));
                usprintf(s," txseq %d rxseq %d",
                uchar(thdr[2]), uchar(thdr[3]));
#ifdef MONITOR
            }
#endif
            break;
        case NR4OPACK:  /* Information acknowledgement */
#ifdef MONITOR
            if (Trace_compact_header)
                usprintf(s, " ack %d/%d",uchar(thdr[0]), uchar(thdr[1]));
            else
            {
#endif
                usprintf(s,"         info ack: ckt %d/%d",
                uchar(thdr[0]),uchar(thdr[1]));
                usprintf(s," txseq %d rxseq %d",
                uchar(thdr[2]), uchar(thdr[3]));
#ifdef MONITOR
            }
#endif
            break;
        default:
            usprintf(s,"         unknown transport type %d",
            thdr[4] & 0x0f) ;
            break;
    }
    if(thdr[4] & NR4CHOKE)
        usprintf(s," CHOKE");
    if(thdr[4] & NR4NAK)
        usprintf(s," NAK");
    if(thdr[4] & NR4MORE)
        usprintf(s," MORE");
    usprintf(s,"\n");
}
  
#endif /* NETROM */
  
