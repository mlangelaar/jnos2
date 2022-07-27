#include "global.h"
#ifdef AX25
#ifdef AUTOROUTE
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "ax25.h"
#include "icmp.h"
#include "ip.h"
#include "arp.h"
#include "icmp.h"
#include "rip.h"
#include "socket.h"
#include "cmdparse.h"
#include <ctype.h>
  
int ax25_check_corruption __ARGS((struct ax25 *header));
int ax25_legal            __ARGS((char *call_ssid));
int Ax25_autoroute = 0; /*auto-routing on by default*/
  
#ifdef RSPF
extern int RspfActive;
#endif
  
int
doax25autoroute(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
#ifdef RSPF
    if(RspfActive) {
        j2tputs("RSPF active. ");
        argc = 0;
    }
#endif
    return setbool(&Ax25_autoroute,"AX.25 IP autorouting",argc,argv);
}
  
/* Create an ARP_RESPONSE packet for any IP packet we hear via an ax25 interface
 * This will automatically update any preexisting entry.
 * If no route is currently defined, then add that as well...
 *
 */
void
ax25eavesdrop(iface,source,dest,bp)
struct iface *iface;
char *source;
char *dest;
struct mbuf *bp;
{
    struct ip ipstuff;
    register struct route *rt;
    struct arp_tab *ap;
    int16 ip_len;
    int16 length;
    char (*mpp)[AXALEN];
  
    if(len_p(bp) < IPLEN){
        /* The packet is shorter than a legal IP header */
        return;
    }
    /* Sneak a peek at the IP header's IHL field to find its length */
    ip_len = (bp->data[0] & 0xf) << 2;
    if(ip_len < IPLEN){
        /* The IP header length field is too small */
        return;
    }
    if(cksum(NULLHEADER,bp,ip_len) != 0){
        /* Bad IP header checksum; discard */
        return;
    }
    if(ntohip(&ipstuff,&bp) == -1){
        return;
    }
  
    Arp_stat.recv++;
  
    for(mpp = Ax25multi;(*mpp)[0] != '\0';mpp++){
        if(addreq(source,*mpp)){
            /* This guy is trying to say he's got the broadcast address! */
            Arp_stat.badaddr++;
            return;
        }
    }
  
  
/* If this entry already exists, but points to a different iface, don't add. */
    if((rt = rt_lookup(ipstuff.source)) != NULLROUTE)
        if(rt != &R_default)
            if(rt->iface != iface)
                return;
  
/* If no route currently exists, add one with a high metric, ttl = ARPLIFE, mark it as private */
/* If we know a better route, or we have a manual route entered, go home. */
    if( rt == NULLROUTE ||
        rt == &R_default ||
        ((rt->timer.duration != 0) && (rt->metric >= (RIP_INFINITY - 1))))
        rt_add(ipstuff.source, (unsigned int)32, (int32)0, iface, RIP_INFINITY - 1, ARPLIFE, 1);
  
/* If route is via a gateway, do not add an ARP entry. */
    if( rt->gateway == (int32)0){
        /* If this guy is already in the table, update its entry
         * unless it's a manual entry (noted by the lack of a timer)
         */
        ap = NULLARP;   /* ap plays the role of merge_flag in the spec */
        if(((ap = arp_lookup(ARP_AX25,ipstuff.source,iface)) != NULLARP
        && ap->timer.duration != 0) || ap == NULLARP){
            ap = arp_add(ipstuff.source,ARP_AX25,source,0,iface);
        }
    }
    return;
}
  
/* Inspect the AX.25 header to determine if it is legal.  If it has anything
    wrong with it, dump this packet...
*/
#define ishash(x)   ((char)(x) == '#' ? 1 : 0)
#define isaspace(x) ((char)(x) == ' ' ? 1 : 0)
  
int ax25_check_corruption(header)
struct ax25 *header;
{
    int loop;
  
    /* The header structure contains two fixed-length and one variable-length
        char arrays, containing the ax25 callsign/ssid pairs for the source,
        destination, and (optional) digipeaters.
  
        The integer "ndigis" says how many digis are in the path, upto the
        constant MAXDIGIS.
  
        Because the arrays are not null-terminated, we can just walk through
        the char arrays looking for any illegal characters to determine if
        the header is corrupt.  We'll rely on the higher-level protocols to
        determine if the data in the packet is corrupt...
  
        We return 0 if the packet is possibly good, or 1 if it is definitely
        corrupt.
    */
  
    if (header->ndigis > MAXDIGIS || header->ndigis < 0){
        return 1;
    }
  
    if (ax25_legal(header->source)){
        return 1;
    }
  
    if (ax25_legal(header->dest)){
        return 1;
    }
  
    for (loop = 0; loop < header->ndigis; loop++)
        if (ax25_legal(header->digis[loop])){
            return 1;
        }
  
    return 0;
  
  
}
  
/* Check the callsign portions. */
int ax25_legal(call_ssid)
char *call_ssid;
{
    int loop;
    unsigned char chr;
  
    for (loop = 0; loop < AXALEN-1; loop++){
  
        /* Put the ascii value of the char into chr */
        chr = (*(call_ssid + loop));
        chr = chr >> 1;
  
        /* Now see if it valid. */
        if ((isupper(chr) | isdigit(chr) | ishash(chr) | isaspace(chr)) == 0)
         /* A bad chr */
            return 1;
    }
  
    /* The header passed inspection, so return 0... */
    return 0;
  
}
  
#endif /* AUTOROUTE */
#endif /* AX25 */
