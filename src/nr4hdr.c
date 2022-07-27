/* Net/rom transport layer header conversion routines.
 * Copyright 1989 by Daniel M. Frank, W9NK.  Permission granted for
 * non-commercial distribution only.
 * 11Oct2005, Maiko (VE4KLM), Added support for NRR stuff
 */
#include "global.h"
#ifdef NETROM
#include "mbuf.h"
#include "proc.h"
#include "nr4.h"

#ifdef	NRR
#include "nrr.h"
#endif
  
/* Convert a net/rom transport header to host format structure.
 * Return -1 if error, 0 if OK.
 */
int
ntohnr4(hdr,bpp)
register struct nr4hdr *hdr;
struct mbuf **bpp;
{
    unsigned char tbuf[NR4MINHDR];
    int i;
  
    if(pullup(bpp, (char *)tbuf, NR4MINHDR) < NR4MINHDR)
        return -1;
  
    hdr->opcode = tbuf[4];
    hdr->flags = 0;

	/* 11Oct2005, Maiko (VE4KLM), NRR frames now supported */
	/* 03Mar2006, Maiko, Now separate from INP3, function name changed */
#ifdef	NRR
	if (nrr_frame (tbuf))
	{
		hdr->flags = NR4_NRR;
		return 0;
	}
#endif 

    switch (tbuf[4] & NR4OPCODE)
	{
        case NR4OPPID:      /* protocol ID extension */
            hdr->u.pid.family = tbuf[0];
            hdr->u.pid.proto = tbuf[1];
            break;
        case NR4OPCONRQ:    /* connect request */
            hdr->u.conreq.myindex = tbuf[0];
            hdr->u.conreq.myid = tbuf[1];
            if((i = PULLCHAR(bpp)) == -1)
                return -1;
            hdr->u.conreq.window = i;
            if(pullup(bpp,hdr->u.conreq.user,AXALEN) < AXALEN)
                return -1;
            if(pullup(bpp,hdr->u.conreq.node,AXALEN) < AXALEN)
                return -1;
#ifdef G8BPQ
            /* See if they are sending us two extra bytes - WG7J */
            if(pullup(bpp,(char *)&hdr->u.conreq.t4init,2) == 2)
                hdr->flags |= NR4_G8BPQRTT;
            else
#endif
                hdr->u.conreq.t4init = 0;
            break;
        case NR4OPCONAK:    /* connect acknowledge */
            hdr->yourindex = tbuf[0];
            hdr->yourid = tbuf[1];
            hdr->u.conack.myindex = tbuf[2];
            hdr->u.conack.myid = tbuf[3];
            if((i = PULLCHAR(bpp)) == -1)
                return -1;
            hdr->u.conack.window = i;
#ifdef G8BPQ
            /* Is there an extra byte ? - WG7J */
            if((i = PULLCHAR(bpp)) != -1) {
                hdr->flags = NR4_G8BPQTTL;
                hdr->u.conack.ttl = i;
            }
#endif
            break;
        case NR4OPDISRQ:    /* disconnect request */
            hdr->yourindex = tbuf[0];
            hdr->yourid = tbuf[1];
            break;
        case NR4OPDISAK:    /* disconnect acknowledge */
            hdr->yourindex = tbuf[0];
            hdr->yourid = tbuf[1];
            break;
        case NR4OPINFO:     /* information frame */
            hdr->yourindex = tbuf[0];
            hdr->yourid = tbuf[1];
            hdr->u.info.txseq = tbuf[2];
            hdr->u.info.rxseq = tbuf[3];
            break;
        case NR4OPACK:      /* information acknowledge */
            hdr->yourindex = tbuf[0];
            hdr->yourid = tbuf[1];
            /* tbuf[2], ordinarily used for txseq,
               is not significant for info ack */
            hdr->u.ack.rxseq = tbuf[3];
            break;
        default:        /* what kind of frame is this? */
            return -1;
    }
    return 0;
}
  
/* Convert host-format level 4 header to network format */
struct mbuf *
htonnr4(hdr)
struct nr4hdr *hdr;
{
#ifdef G8BPQ
    static int16 hlen[NR4NUMOPS] = {5,22,7,5,5,5,5};
#else
    static int16 hlen[NR4NUMOPS] = {5,20,6,5,5,5,5};
#endif
    struct mbuf *rbuf;
    register char *cp;
    unsigned char opcode;
  
    opcode = hdr->opcode & NR4OPCODE;
  
    if(opcode >= NR4NUMOPS)
        return NULLBUF;
  
    if(hdr == (struct nr4hdr *)NULL)
        return NULLBUF;
  
    if((rbuf = alloc_mbuf(hlen[opcode])) == NULLBUF)
        return NULLBUF;
  
    rbuf->cnt = hlen[opcode];
    cp = rbuf->data;
  
    cp[4] = hdr->opcode;
  
    switch(opcode){
        case NR4OPPID:
            *cp++ = hdr->u.pid.family;
            *cp = hdr->u.pid.proto;
            break;
        case NR4OPCONRQ:
            *cp++ = hdr->u.conreq.myindex;
            *cp++ = hdr->u.conreq.myid;
            cp += 3; /* skip to sixth byte */
            *cp++ = hdr->u.conreq.window;
            memcpy(cp,hdr->u.conreq.user,AXALEN);
            cp += AXALEN;
            memcpy(cp,hdr->u.conreq.node,AXALEN);
#ifdef G8BPQ
            if(G8bpq) {
                /* Add the initial transport layer timeout in seconds,
                 * as G8BPQ does it. - WG7J
                 */
                cp += AXALEN;
                *(unsigned int *)cp = (unsigned int)(Nr4irtt / 1000);
            } else
                rbuf->cnt -= 2;
#endif
            break;
        case NR4OPCONAK:
            *cp++ = hdr->yourindex;
            *cp++ = hdr->yourid;
            *cp++ = hdr->u.conack.myindex;
            *cp++ = hdr->u.conack.myid;
            cp++;   /* already loaded pid */
            *cp = hdr->u.conack.window;
#ifdef G8BPQ
            if(G8bpq && (hdr->flags & NR4_G8BPQMASK)) {
                /* Ala g8bpq, add the ttl value we use - WG7J */
                *(++cp) = (unsigned char) Nr_ttl;
            } else
                rbuf->cnt--;
#endif
            break;
        case NR4OPDISRQ:
            *cp++ = hdr->yourindex;
            *cp = hdr->yourid;
            break;
        case NR4OPDISAK:
            *cp++ = hdr->yourindex;
            *cp = hdr->yourid;
            break;
        case NR4OPINFO:
            *cp++ = hdr->yourindex;
            *cp++ = hdr->yourid;
            *cp++ = hdr->u.info.txseq;
            *cp = hdr->u.info.rxseq;
            break;
        case NR4OPACK:
            *cp++ = hdr->yourindex;
            *cp++ = hdr->yourid;
            *cp++ = 0;  /* txseq field is don't care, but
                       emulate real netrom and G8BPQ
                       behavior with a zero -- N1BEE */
            *cp = hdr->u.ack.rxseq;
            break;
    }
    return rbuf;
}
  
#endif /* NETROM */
  
