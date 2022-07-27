/* TCP header conversion routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "tcp.h"
#include "ip.h"
#include "internet.h"
  
/* Convert TCP header in host format into mbuf ready for transmission,
 * link in data (if any). If ph != NULL, compute checksum, otherwise
 * take checksum from tcph->checksum
 */
struct mbuf *
htontcp(tcph,data,ph)
register struct tcp *tcph;
struct mbuf *data;
struct pseudo_header *ph;
{
    int16 hdrlen;
    struct mbuf *bp;
    register char *cp;
  
    hdrlen =  TCPLEN;
    if(tcph->optlen > 0 && tcph->optlen <= TCP_MAXOPT){
        hdrlen += tcph->optlen;
    } else if(tcph->mss != 0){
        hdrlen += MSS_LENGTH;
    }
    bp = pushdown(data,hdrlen);
    cp = bp->data;
    cp = put16(cp,tcph->source);
    cp = put16(cp,tcph->dest);
    cp = put32(cp,tcph->seq);
    cp = put32(cp,tcph->ack);
    *cp++ = hdrlen << 2;    /* Offset field */
    *cp = 0;
    if(tcph->flags.congest)
        *cp |= 64;
    if(tcph->flags.urg)
        *cp |= 32;
    if(tcph->flags.ack)
        *cp |= 16;
    if(tcph->flags.psh)
        *cp |= 8;
    if(tcph->flags.rst)
        *cp |= 4;
    if(tcph->flags.syn)
        *cp |= 2;
    if(tcph->flags.fin)
        *cp |= 1;
    cp++;
    cp = put16(cp,tcph->wnd);
    if(ph == NULLHEADER){
        /* Use user-supplied checksum */
        cp = put16(cp,tcph->checksum);
    } else {
        /* Zero out checksum field for later recalculation */
        *cp++ = 0;
        *cp++ = 0;
    }
    cp = put16(cp,tcph->up);
  
    /* Write options, if any */
    if(hdrlen > TCPLEN){
        if(tcph->optlen > 0)
            memcpy(cp,tcph->options,tcph->optlen);
        else if(tcph->mss != 0){
            *cp++ = MSS_KIND;
            *cp++ = MSS_LENGTH;
            cp = put16(cp,tcph->mss);
        }
    }
    /* Recompute checksum, if requested */
    if(ph != NULLHEADER)
        put16(&bp->data[16],cksum(ph,bp,ph->length));
  
    return bp;
}
/* Pull TCP header off mbuf */
int
ntohtcp(tcph,bpp)
register struct tcp *tcph;
struct mbuf **bpp;
{
    int hdrlen,i,optlen,kind;
    register int flags;
    char hdrbuf[TCPLEN],*cp;
  
    i = pullup(bpp,hdrbuf,TCPLEN);
    /* Note that the results will be garbage if the header is too short.
     * We don't check for this because returned ICMP messages will be
     * truncated, and we at least want to get the port numbers.
     */
    tcph->source = get16(&hdrbuf[0]);
    tcph->dest = get16(&hdrbuf[2]);
    tcph->seq = get32(&hdrbuf[4]);
    tcph->ack = get32(&hdrbuf[8]);
    hdrlen = (hdrbuf[12] & 0xf0) >> 2;
    flags = hdrbuf[13];
    tcph->flags.congest = flags & 64;
    tcph->flags.urg = flags & 32;
    tcph->flags.ack = flags & 16;
    tcph->flags.psh = flags & 8;
    tcph->flags.rst = flags & 4;
    tcph->flags.syn = flags & 2;
    tcph->flags.fin = flags & 1;
    tcph->wnd = get16(&hdrbuf[14]);
    tcph->checksum = get16(&hdrbuf[16]);
    tcph->up = get16(&hdrbuf[18]);
    tcph->mss = 0;
    tcph->optlen = hdrlen - TCPLEN;
  
    /* Check for option field. Only space for one is allowed, but
     * since there's only one TCP option (MSS) this isn't a problem
     */
    if(i < TCPLEN || hdrlen < TCPLEN)
        return -1;  /* Header smaller than legal minimum */
    if(tcph->optlen == 0)
        return (int)hdrlen; /* No options, all done */
  
    if(tcph->optlen > len_p(*bpp)){
        /* Remainder too short for options length specified */
        return -1;
    }
    pullup(bpp,tcph->options,tcph->optlen); /* "Can't fail" */
    /* Process options */
    for(cp=tcph->options,i=tcph->optlen; i > 0;){
        kind = *cp++;
        /* Process single-byte options */
        switch(kind){
            case EOL_KIND:
                i--;
                cp++;
                return (int)hdrlen; /* End of options list */
            case NOOP_KIND:
                i--;
                cp++;
                continue;   /* Go look for next option */
        }
        /* All other options have a length field */
        optlen = uchar(*cp++);
  
        /* Process valid multi-byte options */
        switch(kind){
            case MSS_KIND:
                if(optlen == MSS_LENGTH){
                    tcph->mss = get16(cp);
                }
                break;
        }
        optlen = max(2,optlen); /* Enforce legal minimum */
        i -= optlen;
        cp += optlen - 2;
    }
    return (int)hdrlen;
}
