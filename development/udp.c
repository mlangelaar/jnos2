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

#ifdef	IPV6
#include "ipv6.h"
#include "sockaddr.h"
#include "enet.h"
#include "icmpv6.h"	/* 16Apr2023 */
#endif

#define NLP     nos_log_peerless	/* 16Apr2023 */

static char *nobodylistening = "no UDP (%d) listener";	/* 16Apr2023 */

extern int lognolisteners;	/* 18Apr2023, Maiko (ipcmd.c) */

static struct udp_cb *lookup_udp __ARGS((struct socket *socket));
#ifdef	IPV6
static struct udp_cb *lookup_udpv6 (struct j2socketv6 *socket6);
#endif
  
struct mib_entry Udp_mib[] = {
    { "",         { 0 } },
    { "udpInDatagrams",   { 0 } },
    { "udpNoPorts",       { 0 } },
    { "udpInErrors",      { 0 } },
    { "udpOutDatagrams",  { 0 } }
};
  
/* UDP control structures list */
struct udp_cb *Udps;
 
#ifdef	IPV6
/*
 * Create a UDP control block for lsocket, so that we can queue
 * incoming datagrams. New function 11Apr2023, Maiko, this one
 * is specific to IPV6, trying to get DNS working now ...
 */
struct udp_cb* open_udpv6 (lsocket6, r_upcall)
struct j2socketv6 *lsocket6;
void (*r_upcall)__ARGS((struct iface *,struct udp_cb *,int));
{
    register struct udp_cb *up;
 
  /* 11Apr2023, Maiko, We'll need a new lookup_udpv6() as well */ 
    if((up = lookup_udpv6(lsocket6)) != NULLUDP){
        /* Already exists */
        Net_error = CON_EXISTS;
        return NULLUDP;
    }
    up = (struct udp_cb *)callocw(1,sizeof (struct udp_cb));
    copyipv6addr (lsocket6->address, up->socket6.address);
    up->socket6.port = lsocket6->port;
    up->r_upcall = r_upcall;
  
    up->next = Udps;
    Udps = up;
    return up;
}
#endif
 
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
 
#ifdef	IPV6
/*
 * 12Apr2023, Maiko (VE4KLM), IPV6 version of send_udp(), might
 * be able to break out common code down the road, but not now.
 */
int send_udpv6 (lsocket6,fsocket6,tos,ttl,data,length,id,df)
struct j2socketv6 *lsocket6;     /* Source socket */
struct j2socketv6 *fsocket6;     /* Destination socket */
char tos;           /* Type-of-service for IP */
char ttl;           /* Time-to-live for IP */
struct mbuf *data;      /* Data field, if any */
int16 length;           /* Length of data field */
int16 id;           /* Optional ID field for IP */
char df;            /* Don't Fragment flag for IP */
{
    struct ipv6 ipv6;
    struct mbuf *bp;
    struct udp udp;

	unsigned char laddr6[16];
  
    if(length != 0 && data != NULLBUF)
        trim_mbuf(&data,length);
    else
        length = len_p(data);
  
    length += UDPHDR;

    // log (-1, "src %s", human_readable_ipv6 (lsocket6->address));
    // log (-1, "dst %s", human_readable_ipv6 (fsocket6->address));

    copyipv6addr (lsocket6->address, laddr6);

    if (laddr6[0] == 0x00)
	copyipv6addr (myipv6addr, laddr6);
  
    udp.source = lsocket6->port;
    udp.dest = fsocket6->port;
    udp.length = length;
  
    /* Don't need a pseudo-header structure, just pass IPV6 structure */
	copyipv6addr (laddr6, ipv6.source);
	copyipv6addr (fsocket6->address, ipv6.dest);
	ipv6.next_header = UDP_PTCL;
    ipv6.payload_len = length;
  
    if((bp = htonudpv6 (&udp,data, &ipv6)) == NULLBUF) {
        Net_error = NO_MEM;
        free_p(data);
        return 0;
    }
    udpOutDatagrams++;
    ipv6_send(ipv6.source, ipv6.dest, UDP_PTCL, tos, ttl, bp, length, id, df);
    return (int)length;
}

#endif
 
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

#ifdef	IPV6
/*
 * 12Apr2023, Maiko (VE4KLM), IPV6 version of recv_udp(), might
 * be able to break out common code down the road, but not now.
 */

int recv_udpv6 (up,fsocket6,bp)
register struct udp_cb *up;
struct j2socketv6 *fsocket6;     /* Place to stash incoming socket */
struct mbuf **bp;       /* Place to stash data packet */
{
    struct j2socketv6 sp6;
    struct mbuf *buf;
    int16 length;
  
    if(up == NULLUDP){
	// log (-1, "recv_udpv6 - null udp callback");
        Net_error = NO_CONN;
        return -1;
    }
    if(up->rcvcnt == 0){
	// log (-1, "recv_udpv6 - zero receive count");
        Net_error = WOULDBLK;
        return -1;
    }

    buf = dequeue(&up->rcvq);
    up->rcvcnt--;

#ifdef	NOPE_NOT_THE_CASE
	/*
	 * 12/13Apr2023, Maiko, Need to pull off the ethernet header
	 * in this case as well (just like the RAW sockets) ...
	 */ 
    pullup (&buf, NULLCHAR, sizeof(struct ether));
#endif

    /* Strip socket header */
    pullup(&buf,(char *)&sp6,sizeof(sp6));

	/* 13Apr2023, Maiko, Debugging, let's see what's getting pulled up here !!!
    log (-1, "Strip SH port %d addr %s", sp6.port,  human_readable_ipv6 (sp6.address));
 	*/ 

    /* Fill in the user's foreign socket structure, if given */
    if(fsocket6 != (struct j2socketv6*)0){
        copyipv6addr (sp6.address, fsocket6->address);
        fsocket6->port = sp6.port;
    }
    /* Hand data to user */
    length = len_p(buf);
    if(bp != NULLBUFP)
        *bp = buf;
    else
        free_p(buf);

	// log (-1, "udp_recvudpv6() returning %d bytes", length);

    return (int)length;
}

#endif

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

#ifdef	IPV6
/*
 * 13Apr2023, Maiko (VE4KLM), It would help if we had an actual udp_input
 * function for IPV6, got so focused on the SOCKET side, completely forgot
 * that receive counts will be zero if nothing is able to arrive, yup :]
 *  (again, worry about breaking out duplicate code down the road)
 */
void udp_inputv6 (iface,ipv6,bp,rxbroadcast)
struct iface *iface;    /* Input interface */
struct ipv6 *ipv6;      /* IP header */
struct mbuf *bp;    /* UDP header and data */
int rxbroadcast;    /* The only protocol that accepts 'em */
{
    // struct pseudo_header ph;		/* no need for an extra structure */
    struct udp udp;
    struct udp_cb *up;
    struct j2socketv6 lsocket6;
    struct j2socketv6 fsocket6;
    struct mbuf *packet;
    //int16 length;

#ifdef AXIP
	extern int axudp_frame (struct udp*);
#endif
  
    if(bp == NULLBUF)
        return;

#ifdef	NOT_YET
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
#endif
    /* Extract UDP header in host order */
    if(ntohudp(&udp,&bp) != 0){
        /* Truncated header */
        udpInErrors++;
        free_p(bp);
        return;
    }
    /* If this was a broadcast packet, pretend it was sent to us */
    if(rxbroadcast){
        copyipv6addr (myipv6addr, lsocket6.address);
    } else
        copyipv6addr (ipv6->dest, lsocket6.address);
  
    lsocket6.port = udp.dest;

#ifdef AXIP

	/* Check if the UDP packet qualifies as a possible AXUDP frame */

	if (axudp_frame (&udp))
	{
		// log (-1, "need to see if axip_input can be used as is");

		/*
		 * Use axip_input() for BOTH axip and axudp input processing
		 * 30Apr2023, Maiko, Time to get axudp working for IPV6 !
		 */
		axipv6_input (iface, ipv6, bp, rxbroadcast);
		return;
	}

#endif	/* end of AXIP */

	/* no encap for IPV6 */

#ifdef  APRSD

	/* 27Apr2001, VE4KLM, Internal comms for new APRS Server */

	if (aprsd_frame (&udp))
	{
		log (-1, "need to see if aprsd_torf can be used as is");

		// aprsd_torf (iface, bp, ip);

		free_p (bp);

		return;
	}

#endif	/* end of APRSD */

    /* See if there's somebody around to read it */
    if((up = lookup_udpv6(&lsocket6)) == NULLUDP)
	{
		if (lognolisteners)	/* 18Apr2023, Maiko (ipcmd.c) */
		{
			/* 16Apr2023, Maiko, All this trouble just to do peerless logging */
			struct j2sockaddr_in6 fsock6;
    		copyipv6addr (ipv6->source, fsock6.sin6_addr.s6_addr);
    		fsock6.sin6_port = udp.source;
			fsock6.sin6_family = AF_INET6;	/* 17Apr2023, Maiko (very important) */

       	// log (-1, "src %s port %d", human_readable_ipv6 (fsock6.sin6_addr.s6_addr), fsock6.sin6_port);

			/* 16Apr2023, Maiko, Now able to use nos_log_peerles for IPV6 */
			NLP (&fsock6, nobodylistening, udp.dest);
		}

        /* Nope, return an ICMP message
		 * 16Apr2023, Maiko, Finally implemented the outgoing call
	 	 */
    	if (!rxbroadcast)
		{
    		if ((bp = htonudpv6 (&udp,bp, ipv6)) == NULLBUF)
			{
        		Net_error = NO_MEM;
        		free_p(bp);
        		return;
    		}

        	icmpv6_output (ipv6, bp, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, (union icmpv6_args*)0);
		}

        udpNoPorts++;
        free_p(bp);
        return;
    }
    /* Create space for the foreign socket info */
    packet = pushdown(bp,sizeof(fsocket6));

    copyipv6addr (ipv6->source, fsocket6.address);
    fsocket6.port = udp.source;
    memcpy(&packet->data[0],(char *)&fsocket6,sizeof(fsocket6));
  
    /* Queue it */
    enqueue(&up->rcvq,packet);
    up->rcvcnt++;
    udpInDatagrams++;
    if(up->r_upcall)
        (*up->r_upcall)(iface,up,up->rcvcnt);
}
#endif

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
    if((up = lookup_udp(&lsocket)) == NULLUDP)
	{
		if (lognolisteners)	/* 18Apr2023, Maiko (ipcmd.c) */
		{
			/* 16Apr2023, Maiko, All this trouble just to do peerless logging */
			struct sockaddr_in fsock;
    		fsock.sin_addr.s_addr = ip->source;
    		fsock.sin_port = udp.source;
			fsock.sin_family = AF_INET;	/* 17Apr2023, Maiko (very important) */

			/* 16Apr2023, Maiko, Now able to use nos_log_peerless */
			NLP (&fsock, nobodylistening, udp.dest);
		}

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

#ifdef	IPV6
/*
 * 11Apr2023, Maiko, IPV6 counterpart to the lookup_udp() further down,
 * pretty sure that some time down the road, we can cut duplicate code,
 * and have just one single function for both IPV4 and IPV6, but I am
 * doing that today, get it working first, figure out a few things.
 */
static struct udp_cb *lookup_udpv6 (struct j2socketv6 *socket6)
{
    register struct udp_cb *up;

    struct udp_cb *uplast = NULLUDP;
  
    for(up = Udps;up != NULLUDP;uplast = up,up = up->next){
        if(socket6->port == up->socket6.port
            && (!memcmp (socket6->address, up->socket6.address, 16)
        || up->socket6.address[0] == 0x00)){
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
#endif

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
  
