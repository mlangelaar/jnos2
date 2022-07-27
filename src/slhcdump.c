/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
#include "global.h"
#if defined(VJCOMPRESS) || defined(PPP)
#include "mbuf.h"
#include "internet.h"
#include "ip.h"
#include "slhc.h"
#include "trace.h"
#include "socket.h"  

static int16 decodeint __ARGS((struct mbuf **bpp));
  
static int16
decodeint(bpp)
struct mbuf **bpp;
{
    char tmpbuf[2];
  
    pullup(bpp,tmpbuf,1);
    if (tmpbuf[0] == 0)
        pullup(bpp,tmpbuf,2);
    else {
        tmpbuf[1] = tmpbuf[0];
        tmpbuf[0] = 0;
    }
    return(get16(tmpbuf));
}
  
void
vjcomp_dump(s,bpp,unused)
int s;
struct mbuf **bpp;
int unused;
{
    char changes;
    char tmpbuf[2];
  
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;
  
    /* Dump compressed TCP/IP header */
    changes = pullchar(bpp);
    usprintf(s,"\tchanges: 0x%02x",uchar(changes));
    if (changes & NEW_C) {
        pullup(bpp,tmpbuf,1);
        usprintf(s,"   connection: 0x%02x",uchar(tmpbuf[0]));
    }
    pullup(bpp,tmpbuf,2);
    usprintf(s,"   TCP checksum: 0x%04x",get16(tmpbuf));
  
    if (changes & TCP_PUSH_BIT)
        usprintf(s,"   PUSH");
    usprintf(s,"\n");
  
    switch (changes & SPECIALS_MASK) {
        case SPECIAL_I:
            usprintf(s,"\tdelta ACK and delta SEQ implied by length of data\n");
            break;
  
        case SPECIAL_D:
            usprintf(s,"\tdelta SEQ implied by length of data\n");
            break;
  
        default:
            if (changes & NEW_U) {
                usprintf(s,"\tUrgent pointer: 0x%02x",decodeint(bpp));
            }
            if (changes & NEW_W)
                usprintf(s,"\tdelta WINDOW: 0x%02x",decodeint(bpp));
            if (changes & NEW_A)
                usprintf(s,"\tdelta ACK: 0x%02x",decodeint(bpp));
            if (changes & NEW_S)
                usprintf(s,"\tdelta SEQ: 0x%02x",decodeint(bpp));
            break;
    };
    if (changes & NEW_I) {
        usprintf(s,"\tdelta ID: 0x%02x\n",decodeint(bpp));
    } else {
        usprintf(s,"\tincrement ID\n");
    }
}
  
  
/* dump serial line IP packet; may have Van Jacobson TCP header compression */
void
sl_dump(s,bpp,unused)
int s;
struct mbuf **bpp;
int unused;
{
    struct mbuf *bp, *tbp;
    unsigned char c;
    int len;
  
    bp = *bpp;
    c = bp->data[0];
    if (c & SL_TYPE_COMPRESSED_TCP) {
        usprintf(s,"serial line VJ Compressed TCP: len %3u\n",
        len_p(*bpp));
        vjcomp_dump(s,bpp,0);
    } else if ( c >= SL_TYPE_UNCOMPRESSED_TCP ) {
        usprintf(s,"serial line VJ Uncompressed TCP: len %3u\n",
        len = len_p(bp));
        /* Get our own copy so we can mess with the data */
        if ( (tbp = copy_p(bp, len)) == NULLBUF )
            return;
  
        usprintf(s,"\tconnection ID = %d\n",
        uchar(tbp->data[9]));   /* FIX THIS! */
        /* Restore the bytes used with Uncompressed TCP */
        tbp->data[0] &= 0x4f;       /* FIX THIS! */
        tbp->data[9] = TCP_PTCL;    /* FIX THIS! */
        /* Dump contents as a regular IP packet */
        ip_dump(s,&tbp,1);
        free_p(tbp);
    } else {
        usprintf(s,"serial line IP: len: %3u\n",len_p(*bpp));
        ip_dump(s,bpp,1);
    }
}
  
#endif /* #if defined(VJCOMPRESS) || defined(PPP) */  
