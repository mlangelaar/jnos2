/*
 *  PPPDUMP.C
 *
 *  12-89   -- Katie Stevens (dkstevens@ucdavis.edu)
 *         UC Davis, Computing Services
 *  PPP.08  05-90   [ks] improve tracing reports
 *  PPP.09  05-90   [ks] add UPAP packet reporting
 *  PPP.14  08-90   [ks] change UPAP to PAP for consistency with RFC1172
 *  PPP.15  09-90   [ks] update to KA9Q NOS v900828
 *  Jan 91  [Bill Simpson] small changes to match rewrite of PPP
 *  Aug 91  [Bill Simpson] fixed some buffer loss
 */
/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
#include "global.h"
#ifdef PPP
#include "mbuf.h"
#include "iface.h"
#include "internet.h"
#include "ppp.h"
#include "trace.h"
#include "socket.h"  

#ifdef TURBOC_SWITCH_BUG
#pragma option -G-
#endif
  
/* dump a PPP packet */
void
ppp_dump(s,bpp,unused)
int s;
struct mbuf **bpp;
int unused;
{
    struct ppp_hdr hdr;
    struct mbuf *tbp;
  
    usprintf(s,"PPP: len %3u\t", len_p(*bpp));
  
    /* HDLC address and control fields may be compressed out */
    if ((byte_t)(*bpp)->data[0] != HDLC_ALL_ADDR) {
        usprintf(s,"(compressed ALL/UI)\t");
    } else if ((byte_t)(*bpp)->data[1] != HDLC_UI) {
        usprintf(s,"(missing UI!)\t");
    } else {
        /* skip address/control fields */
        pull16(bpp);
    }
  
    /* Initialize the expected header */
    hdr.addr = HDLC_ALL_ADDR;
    hdr.control = HDLC_UI;
    hdr.protocol = PULLCHAR(bpp);
  
    /* First byte of PPP protocol field may be compressed out */
    if ( hdr.protocol & 0x01 ) {
        usprintf(s,"compressed ");
    } else {
        hdr.protocol = (hdr.protocol << 8) | PULLCHAR(bpp);
  
        /* Second byte of PPP protocol field must be odd */
        if ( !(hdr.protocol & 0x01) ) {
            usprintf(s, "(not odd!) " );
        }
    }
  
    usprintf(s,"protocol: ");
    switch(hdr.protocol){
        case PPP_IP_PROTOCOL:
            usprintf(s,"IP\n");
            ip_dump(s,bpp,1);
            break;
        case PPP_IPCP_PROTOCOL:
            usprintf(s,"IPCP\n");
            break;
        case PPP_LCP_PROTOCOL:
            usprintf(s,"LCP\n");
            break;
        case PPP_PAP_PROTOCOL:
            usprintf(s,"PAP\n");
            break;
        case PPP_COMPR_PROTOCOL:
            usprintf(s,"VJ Compressed TCP/IP\n");
            vjcomp_dump(s,bpp,0);
            break;
        case PPP_UNCOMP_PROTOCOL:
            usprintf(s,"VJ Uncompressed TCP/IP\n");
            /* Get our own copy so we can mess with the data */
            if ( (tbp = copy_p(*bpp, len_p(*bpp))) == NULLBUF)
                return;
  
            usprintf(s,"\tconnection 0x%02x\n",
            tbp->data[9]);      /* FIX THIS! */
            /* Restore the bytes used with Uncompressed TCP */
            tbp->data[9] = TCP_PTCL;    /* FIX THIS! */
            ip_dump(s,&tbp,1);
            free_p(tbp);
            break;
        default:
            usprintf(s,"unknown 0x%04x\n",hdr.protocol);
            break;
    }
}
  
#ifdef TURBOC_SWITCH_BUG
#pragma option -G
#endif
  
#endif /* PPP */
  
