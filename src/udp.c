/* Internet User Data Protocol (UDP)
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "iface.h"
#include "udp.h"
#include "ip.h"
#include "internet.h"

/* 11Oct2005, Maiko, DOS compile fails without this */
#ifdef MSDOS
#include "socket.h"
#endif

#include "icmp.h"
#ifdef  APRSD
#include "aprs.h"
#endif
  
static struct udp_cb *lookup_udp __ARGS((struct socket *socket));
  
struct mib_entry Udp_mib[] = {
    { "",         { 0 } },
    { "udpInDatagrams",   { 0 } },
    { "udpNoPorts",       { 0 } },
    { "udpInErrors",      { 0 } },
    { "udpOutDatagrams",  { 0 } }
};
  
/* UDP control structures list */
struct udp_cb *Udps;
  
/* Create a UDP control block for lsocket, so that we can queue
 * incoming datagrams.
 */
struct udp_cb *
open_udp(lsocket,r_upcall)
struct socket *lsocket;
void (*r_upcall)__ARGS((struct iface *,struct udp_cb *,int));
{
    register struct udp_cb *up;
  
    if((up = lookup_udp(lsocket)) != NULLUDP){
        /* Already exists */
        Net_error = CON_EXISTS;
        return NULLUDP;
    }
    up = (struct udp_cb *)callocw(1,sizeof (struct udp_cb));
    up->socket.address = lsocket->address;
    up->socket.port = lsocket->port;
    up->r_upcall = r_upcall;
  
    up->next = Udps;
    Udps = up;
    return up;
}
  
/* Send a UDP datagram */
int
send_udp(lsocket,fsocket,tos,ttl,data,length,id,df)
struct socket *lsocket;     /* Source socket */
struct socket *fsocket;     /* Destination socket */
char tos;           /* Type-of-service for IP */
char ttl;           /* Time-to-live for IP */
struct mbuf *data;      /* Data field, if any */
int16 length;           /* Length of data field */
int16 id;           /* Optional ID field for IP */
char df;            /* Don't Fragment flag for IP */
{
    struct mbuf *bp;
    struct pseudo_header ph;
    struct udp udp;
    int32 laddr;
  
    if(length != 0 && data != NULLBUF)
        trim_mbuf(&data,length);
    else
        length = len_p(data);
  
    length += UDPHDR;
  
    laddr = lsocket->address;
    if(laddr == INADDR_ANY)
        laddr = locaddr(fsocket->address);
  
    udp.source = lsocket->port;
    udp.dest = fsocket->port;
    udp.length = length;
  
    /* Create IP pseudo-header, compute checksum and send it */
    ph.length = length;
    ph.source = laddr;
    ph.dest = fsocket->address;
    ph.protocol = UDP_PTCL;
  
    if((bp = htonudp(&udp,data,&ph)) == NULLBUF){
        Net_error = NO_MEM;
        free_p(data);
        return 0;
    }
    udpOutDatagrams++;
    ip_send(laddr,fsocket->address,UDP_PTCL,tos,ttl,bp,length,id,df);
    return (int)length;
}
/* Accept a waiting datagram, if available. Returns length of datagram */
int
recv_udp(up,fsocket,bp)
register struct udp_cb *up;
struct socket *fsocket;     /* Place to stash incoming socket */
struct mbuf **bp;       /* Place to stash data packet */
{
    struct socket sp;
    struct mbuf *buf;
    int16 length;
  
    if(up == NULLUDP){
        Net_error = NO_CONN;
        return -1;
    }
    if(up->rcvcnt == 0){
        Net_error = WOULDBLK;
        return -1;
    }
    buf = dequeue(&up->rcvq);
    up->rcvcnt--;
  
    /* Strip socket header */
    pullup(&buf,(char *)&sp,sizeof(struct socket));
  
    /* Fill in the user's foreign socket structure, if given */
    if(fsocket != NULLSOCK){
        fsocket->address = sp.address;
        fsocket->port = sp.port;
    }
    /* Hand data to user */
    length = len_p(buf);
    if(bp != NULLBUFP)
        *bp = buf;
    else
        free_p(buf);
    return (int)length;
}
/* Delete a UDP control block */
int
del_udp(conn)
struct udp_cb *conn;
{
    register struct udp_cb *up;

    struct udp_cb *udplast = NULLUDP;
  
    for(up = Udps;up != NULLUDP;udplast = up,up = up->next){
        if(up == conn)
            break;
    }
    if(up == NULLUDP){
        /* Either conn was NULL or not found on list */
        Net_error = INVALID;
        return -1;
    }
    /* Remove from list */
    if(udplast != NULLUDP)
        udplast->next = up->next;
    else
        Udps = up->next;    /* was first on list */
  
    /* Get rid of any pending packets */
    free_q(&up->rcvq);
    
    free((char *)up);
    return 0;
}
/* Process an incoming UDP datagram */
void
udp_input(iface,ip,bp,rxbroadcast)
struct iface *iface;    /* Input interface */
struct ip *ip;      /* IP header */
struct mbuf *bp;    /* UDP header and data */
int rxbroadcast;    /* The only protocol that accepts 'em */
{
    struct pseudo_header ph;
    struct udp udp;
    struct udp_cb *up;
    struct socket lsocket;
    struct socket fsocket;
    struct mbuf *packet;
    int16 length;

#ifdef AXIP
	extern int axudp_frame (struct udp*);
#endif
  
    if(bp == NULLBUF)
        return;
  
    /* Create pseudo-header and verify checksum */
    ph.source = ip->source;
    ph.dest = ip->dest;
    ph.protocol = ip->protocol;
    length = ip->length - IPLEN - ip->optlen;
    ph.length = length;
  
    /* Peek at header checksum before we extract the header. This
     * allows us to bypass cksum() if the checksum field was not
     * set by the sender.
     */
    udp.checksum = udpcksum(bp);
    if(udp.checksum != 0 && cksum(&ph,bp,length) != 0){
        /* Checksum non-zero, and wrong */
        udpInErrors++;
        free_p(bp);
        return;
    }
    /* Extract UDP header in host order */
    if(ntohudp(&udp,&bp) != 0){
        /* Truncated header */
        udpInErrors++;
        free_p(bp);
        return;
    }
    /* If this was a broadcast packet, pretend it was sent to us */
    if(rxbroadcast){
        lsocket.address = iface->addr;
    } else
        lsocket.address = ip->dest;
  
    lsocket.port = udp.dest;

#ifdef AXIP

	/* Check if the UDP packet qualifies as a possible AXUDP frame */

	if (axudp_frame (&udp))
	{
		/* Use axip_input() for BOTH axip and axudp input processing */
		axip_input (iface, ip, bp, rxbroadcast);
		return;
	}

#endif	/* end of AXIP */

#ifdef	ENCAP

	/* Check if the UDP packet qualifies as a possible IPUDP frame */

	if (udp.source == IPPORT_IPUDP && udp.dest == IPPORT_IPUDP)
	{
		ipip_recv (iface, ip, bp, rxbroadcast);
		return;
	}

#endif	/* end of ENCAP */

#ifdef  APRSD

	/* 27Apr2001, VE4KLM, Internal comms for new APRS Server */

	if (aprsd_frame (&udp))
	{
		aprsd_torf (iface, bp, ip);

		free_p (bp);

		return;
	}

#endif	/* end of APRSD */

    /* See if there's somebody around to read it */
    if((up = lookup_udp(&lsocket)) == NULLUDP){
        /* Nope, return an ICMP message */
        if(!rxbroadcast){
            bp = htonudp(&udp,bp,&ph);
            icmp_output(ip,bp,ICMP_DEST_UNREACH,ICMP_PORT_UNREACH,NULL);
        }
        udpNoPorts++;
        free_p(bp);
        return;
    }
    /* Create space for the foreign socket info */
    packet = pushdown(bp,sizeof(fsocket));

    fsocket.address = ip->source;
    fsocket.port = udp.source;
    memcpy(&packet->data[0],(char *)&fsocket,sizeof(fsocket));
  
    /* Queue it */
    enqueue(&up->rcvq,packet);
    up->rcvcnt++;
    udpInDatagrams++;
    if(up->r_upcall)
        (*up->r_upcall)(iface,up,up->rcvcnt);
}
/* Look up UDP socket.
 * Return control block pointer or NULLUDP if nonexistant
 * As side effect, move control block to top of list to speed future
 * searches.
 */
static struct udp_cb *
lookup_udp(socket)
struct socket *socket;
{
    register struct udp_cb *up;
    struct udp_cb *uplast = NULLUDP;
  
    for(up = Udps;up != NULLUDP;uplast = up,up = up->next){
        if(socket->port == up->socket.port
            && (socket->address == up->socket.address
        || up->socket.address == INADDR_ANY)){
            if(uplast != NULLUDP){
                /* Move to top of list */
                uplast->next = up->next;
                up->next = Udps;
                Udps = up;
            }
            return up;
        }
    }
    return NULLUDP;
}
  
#ifndef UNIX
  
/* Attempt to reclaim unused space in UDP receive queues */
void
udp_garbage(red)
int red;
{
    register struct udp_cb *udp;
  
    for(udp = Udps;udp != NULLUDP; udp = udp->next){
        mbuf_crunch(&udp->rcvq);
    }
}
#endif /* UNIX */
  
