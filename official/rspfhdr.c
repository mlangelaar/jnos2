/* Mods by PA0GRI */
  
#include "global.h"
#ifdef RSPF
#include "mbuf.h"
#include "internet.h"
#include "ip.h"
#include "timer.h"
#include "rspf.h"
  
/* Convert RRH header in internal format to an mbuf in external format */
struct mbuf *
htonrrh(rrh,data,ph)
struct rrh *rrh;
struct mbuf *data;
struct pseudo_header *ph;
{
    struct mbuf *bp;
    register char *cp, *ck;
    int16 checksum;
  
    /* Allocate RRH header and fill it in */
    bp = pushdown(data,RRHLEN); /* pushdown never fails */
  
    cp = bp->data;
    *cp++ = rrh->version;       /* Version number */
    *cp++ = RSPF_RRH;       /* Type number */
    ck = cp;
    cp = put16(cp,0);       /* Clear checksum */
    cp = put32(cp,rrh->addr);   /* Source address */
    cp = put16(cp,rrh->seq);    /* Last packet sent sequence number */
    *cp = rrh->flags;       /* Flags */
  
    /* All zeros and all ones is equivalent in one's complement arithmetic;
     * the spec requires us to change zeros into ones to distinguish an
     * all-zero checksum from no checksum at all
     */
    if((checksum = cksum(ph,bp,ph->length)) == 0)
        checksum = 0xffffL;
        /* checksum = 0xffffffffL; is wrong, 05Dec2020 , Maiko */
    put16(ck,checksum);
    return bp;
}
  
/* Convert RSPF packet header in internal format to an mbuf in external
 * format, without calculating the checksum.
 */
struct mbuf *
htonrspf(pkth,data)
struct rspfpacketh *pkth;
struct mbuf *data;
{
    struct mbuf *bp;
    register unsigned char *cp;
  
    /* Allocate packet header and fill it in */
    bp = pushdown(data,RSPFPKTLEN); /* pushdown never fails */
  
    cp = (unsigned char *) bp->data;
    *cp++ = (char) pkth->version;   /* Version number */
    *cp++ = (char) pkth->type;  /* Type number */
    *cp++ = pkth->fragn;        /* The fragment number */
    *cp++ = pkth->fragtot;      /* The total number of fragments */
    cp = (unsigned char *) put16((char *)cp,pkth->csum); /* The checksum */
    *cp++ = pkth->sync;     /* The sync octet */
    *cp++ = pkth->nodes;        /* The number of nodes */
    put16((char *)cp,pkth->envid);  /* The envelope-ID */
    return bp;
}
  
/* Convert RSPF header in mbuf to internal structure */
int
ntohrspf(rspf,bpp)
union rspf *rspf;
struct mbuf **bpp;
{
    if(len_p(*bpp) < RSPFPKTLEN) /* Packet too short */
        return -1;
    if((rspf->hdr.version = pullchar(bpp)) != RSPF_VERSION) {
        ++Rspf_stat.badvers;
        return -1;           /* Wrong version */
    }
  
    switch (rspf->hdr.type = pullchar(bpp)){
        case RSPF_RRH:
            if(len_p(*bpp) < RRHLEN - 2)    /* Packet too small */
                return -1;
            rspf->rrh.csum = (int16) pull16(bpp);
            rspf->rrh.addr = pull32(bpp);
            rspf->rrh.seq = (int16) pull16(bpp);
            rspf->rrh.flags = pullchar(bpp);
            break;
        case RSPF_FULLPKT:
            rspf->pkthdr.fragn = pullchar(bpp);
            rspf->pkthdr.fragtot = pullchar(bpp);
            rspf->pkthdr.csum = (int16) pull16(bpp);
            rspf->pkthdr.sync = pullchar(bpp);
            rspf->pkthdr.nodes = pullchar(bpp);
            rspf->pkthdr.envid = (int16) pull16(bpp);
            break;
        default:
            return -1;
    }
    return 0;
}
  
/* Convert RSPF node header in internal format to an mbuf in external format */
struct mbuf *
htonrspfnode(nodeh,data)
struct rspfnodeh *nodeh;
struct mbuf *data;
{
    struct mbuf *bp;
    register char *cp;
  
    /* Allocate node header and fill it in */
    bp = pushdown(data,RSPFNODELEN);    /* pushdown never fails */
  
    cp = bp->data;
    cp = put32(cp,nodeh->addr);     /* Reporting router address */
    cp = put16(cp,(int16)nodeh->seq);   /* Sequence number */
    *cp++ = nodeh->subseq;      /* Sub-sequence number */
    *cp++ = nodeh->links;       /* Number of links */
    return bp;
}
  
/* Convert RSPF node header in mbuf to internal structure */
int
ntohrspfnode(nodeh,bpp)
struct rspfnodeh *nodeh;
struct mbuf **bpp;
{
    if(len_p(*bpp) < RSPFNODELEN) /* Packet too short */
        return -1;
    nodeh->addr = pull32(bpp);
    nodeh->seq = (int16) pull16(bpp);
    nodeh->subseq = (unsigned char) pullchar(bpp);
    nodeh->links = (unsigned char) pullchar(bpp);
    return 0;
}
  
/* Convert RSPF link header in internal format to an mbuf in external format */
struct mbuf *
htonrspflink(linkh,data)
struct rspflinkh *linkh;
struct mbuf *data;
{
    struct mbuf *bp;
    register unsigned char *cp;
  
    /* Allocate link header and fill it in */
    bp = pushdown(data,RSPFLINKLEN);    /* pushdown never fails */
  
    cp = (unsigned char *) bp->data;
    *cp++ = linkh->horizon;     /* Horizon */
    *cp++ = linkh->erp;         /* ERP factor */
    *cp++ = linkh->cost;        /* Link cost */
    *cp++ = linkh->adjn;        /* Number of adjacencies */
    return bp;
}
  
/* Convert RSPF link header in mbuf to internal structure */
int
ntohrspflink(linkh,bpp)
struct rspflinkh *linkh;
struct mbuf **bpp;
{
    if(len_p(*bpp) < RSPFLINKLEN) /* Packet too short */
        return -1;
    linkh->horizon = (unsigned char) pullchar(bpp);
    linkh->erp = (unsigned char) pullchar(bpp);
    linkh->cost = (unsigned char) pullchar(bpp);
    linkh->adjn = (unsigned char) pullchar(bpp);
    return 0;
}
#endif /* RSPF */
  
