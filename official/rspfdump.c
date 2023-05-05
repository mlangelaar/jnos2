/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
#include "global.h"
#ifdef RSPF
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "socket.h"
#include "ip.h"
#include "rspf.h"
  
/* Dump an RSPF packet */
void
rspf_dump(s,bpp,source,dest,check)
int s;
struct mbuf **bpp;
int32 source,dest;
int check;      /* If 0, bypass checksum verify */
{
    union rspf rspf;
    struct pseudo_header ph;
    int16 csum;
    int sync;
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;
  
    usprintf(s,"RSPF: ");
  
    /* Compute checksum */
    ph.source = source;
    ph.dest = dest;
    ph.protocol = RSPF_PTCL;
    ph.length = len_p(*bpp);
    if((csum = cksum(&ph,*bpp,ph.length)) == 0)
        check = 0;  /* No checksum error */
  
    ntohrspf(&rspf,bpp);
  
    if(rspf.hdr.version != RSPF_VERSION)
        usprintf(s,"version %u ",rspf.hdr.version);
    switch(rspf.hdr.type){
        case RSPF_FULLPKT:
            if(rspf.pkthdr.csum == 0)
                check = 0;
            usprintf(s,"type ROUTING UPDATE ");
            if(rspf.pkthdr.fragtot != 1)
                usprintf(s,"fragment %u frag total %u ",rspf.pkthdr.fragn,
                rspf.pkthdr.fragtot);
            if(rspf.pkthdr.sync != 4)
                usprintf(s,"sync %u ",rspf.pkthdr.sync);
            usprintf(s,"nodes %u id %u",rspf.pkthdr.nodes,rspf.pkthdr.envid);
            if(check)
                usprintf(s," CHECKSUM ERROR (%u)",csum);
            usprintf(s,"\n");
            if(rspf.pkthdr.sync != 0)
                sync = rspf.pkthdr.sync - 4;
            else
                sync = len_p(*bpp);
            if(sync % 5 != 0){
                usprintf(s,"      %d bytes\n",sync);
                pullup(bpp,NULLCHAR,sync);
                sync = 0;
            }
            rspfnodedump(s,bpp,sync / 5);
            break;
        case RSPF_RRH:
            if(rspf.rrh.csum == 0)
                check = 0;
            usprintf(s,"type RRH seq 0x%04x flags %d",rspf.rrh.seq,rspf.rrh.flags);
            if(check)
                usprintf(s," CHECKSUM ERROR (%u)",csum);
            usprintf(s,"\n");
            break;
        default:
            usprintf(s,"Unknown packet type\n");
    }
}
  
void
rspfnodedump(s,bpp,adjcnt)
int s;       
struct mbuf **bpp;  /* routing update without packet header */
int adjcnt;     /* number of links before first node header */
{
    int c, links = 0;
    char buf[128];
    struct rspfnodeh nodeh;
    struct rspflinkh linkh;
    *buf = '\0';
    for(;;) {
        if(*buf != '\0') {
            usprintf(s,"%s",buf);
            *buf = '\0';
        }
        if(len_p(*bpp) == 0)
            break;
        if(adjcnt){
            if((c = PULLCHAR(bpp)) == -1)
                break;
            sprintf(buf,"            %s/%u\n",inet_ntoa(pull32(bpp)),c);
            adjcnt--;
            continue;
        }
        if(links){
            if(ntohrspflink(&linkh,bpp) == -1)
                break;
            sprintf(buf,"      horizon %u ERP factor %u cost %u adjacencies %u\n",
            linkh.horizon,linkh.erp,linkh.cost,linkh.adjn);
            adjcnt = linkh.adjn;
            links--;
            continue;
        }
        if(ntohrspfnode(&nodeh,bpp) == -1)
            break;
        sprintf(buf,"      Reporting Router: %s Seq %u Subseq %u links %u\n",
        inet_ntoa(nodeh.addr),(int16)nodeh.seq,nodeh.subseq,
        nodeh.links);
        links = nodeh.links;
    }
}
#endif /* RSPF */
  
