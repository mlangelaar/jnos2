/* Application programming interface routines - based loosely on the
 * "socket" model in Berkeley UNIX.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <time.h>
#ifdef  __STDC__
#include <stdarg.h>
#endif
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "timer.h"
#include "iface.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "ax25.h"
#include "lapb.h"
#include "netrom.h"
#include "nr4.h"
#include "proc.h"
#include "lzw.h"
#include "usock.h"
#include "socket.h"
#ifdef UNIX
#include "unix.h"
#endif
  
/* In connect(), accept() and recv_mbuf()
 * the noblock options have been removed, since they never occur anyway.
 * If you want then back, do a '#define useblock 1'
 * The socket's 'noblock' parameter is now only effective for sending
 * data; ie if set, only sending data will not block. - WG7J
 */
  
  
static void autobind __ARGS((int s,int af));
static int checkaddr __ARGS((int type,char *name,int namelen));
static void rip_recv __ARGS((struct raw_ip *rp));
static void s_trcall __ARGS((struct tcb *tcb,int cnt));
static void s_tscall __ARGS((struct tcb *tcb,int old,int new));
static void s_ttcall __ARGS((struct tcb *tcb,int cnt));
static void s_urcall __ARGS((struct iface *iface,struct udp_cb *udp,int cnt));
static void trdiscard __ARGS((struct tcb *tcb,int cnt));
#ifdef  NETROM
static void s_nrcall __ARGS((struct nr4cb *cb,int cnt));
static void s_nscall __ARGS((struct nr4cb *cb,int old,int new));
static void s_ntcall __ARGS((struct nr4cb *cb,int cnt));
#endif
static struct usock *addsocket(void);
static void deletesocket(int s);
  
static int16 Lport = 1024;
  
char *Socktypes[] = {
    "Not Used",
    "TCP",
    "UDP",
    "AX25 I",
    "AX25 UI",
    "Raw IP",
    "NETROM3",
    "NETROM",
    "Loc St",
    "Loc Dg",
    "File"
};
char Badsocket[] = "Bad socket";
  
#if defined(AX25) && defined(TTYCALL) && defined(TTYCALL_CONNECT)
extern char Ttycall[AXALEN];    /* KA1NNN */
#endif

/* The following two variables are needed because there can be only one
 * socket listening on each of the AX.25 modes (I and UI)
 */
#ifdef  AX25
int Axi_sock = -1;  /* Socket number listening for AX25 connections */
#endif /* AX.25 */
  
/* Create a user socket, return socket index
 * The mapping to actual protocols is as follows:
 *
 *
 * ADDRESS FAMILY   Stream      Datagram    Raw       Seq. Packet
 *
 * AF_INET          TCP         UDP         IP
 * AF_AX25          I-frames    UI-frames
 * AF_NETROM                                NETROM_L3 NETROM_L4
 * AF_LOCAL         stream      loopback    packet    loopback
 * AF_FILE      write-only 'FILE *' mapping for trace code. - WG7J 
 */
int
j2socket(af,type,protocol)
int af;     /* Address family */
int type;   /* Stream or datagram */
int protocol;   /* Used for raw IP sockets */
{
    struct usock *up;
    int s;
  
    if((up=addsocket()) == NULLUSOCK) {
        errno = EMFILE;
        return -1;  /* Can't add a socket */
    }
  
    /* set the time the socket was created - WG7J */
    up->created = time(NULL);
    up->refcnt = 1;
    s = up->number;
    errno = 0;
    up->look = NULLPROC;
    up->noblock = 0;
    up->rdysock = -1;
    up->cb.p = NULLCHAR;
    up->peername = up->name = NULLCHAR;
    up->namelen = up->peernamelen = 0;
    up->owner = Curproc;
    up->obuf = NULLBUF;
    memset(up->errcodes,0,sizeof(up->errcodes));
    memset(up->eol,0,sizeof(up->eol));
    up->flush = '\n';   /* default is line buffered */
    switch(af){
        case AF_FILE:
                up->type = TYPE_WRITE_ONLY_FILE;
                break;
        case AF_LOCAL:
            up->cb.local = (struct loc *) callocw(1,sizeof(struct loc));
            up->cb.local->peer = up;    /* connect to self */
        switch(type){
            case SOCK_STREAM:
                up->type = TYPE_LOCAL_STREAM;
                up->cb.local->hiwat = LOCSFLOW;
                break;
            case SOCK_DGRAM:
                up->type = TYPE_LOCAL_DGRAM;
                up->cb.local->hiwat = LOCDFLOW;
                break;
            default:
                free(up->cb.local);
                errno = ESOCKTNOSUPPORT;
                break;
        }
            break;
#ifdef  AX25
        case AF_AX25:
        switch(type){
            case SOCK_STREAM:
                up->type = TYPE_AX25I;
                break;
            case SOCK_DGRAM:
                up->type = TYPE_AX25UI;
                break;
            default:
                errno = ESOCKTNOSUPPORT;
                break;
        }
            strcpy(up->eol,AX_EOL);
            break;
#endif
#ifdef NETROM
        case AF_NETROM:
        switch(type)
	{
            case SOCK_SEQPACKET:
                up->type = TYPE_NETROML4;
                break;
            default:
                errno = ESOCKTNOSUPPORT;
                break;
        }
            strcpy(up->eol,AX_EOL);
            break;
#endif
        case AF_INET:
        switch(type){
            case SOCK_STREAM:
                up->type = TYPE_TCP;
                strcpy(up->eol,INET_EOL);
                break;
            case SOCK_DGRAM:
                up->type = TYPE_UDP;
                break;
            case SOCK_RAW:
                up->type = TYPE_RAW;
                up->cb.rip = raw_ip(protocol,rip_recv);
                up->cb.rip->user = s;
                break;
            default:
                errno = ESOCKTNOSUPPORT;
                break;
        }
            break;
        default:
            errno = EAFNOSUPPORT;
            break;
    }
    if(errno)
        return -1;
  
    return s;
}

/* At present (Jnos 1.10L) only the TRACE & REDIRECT & FWDFILE code makes use of AF_FILE
   sockets, so we can save some codespace by testing for this -- n5knx
*/
#if defined(TRACE) || defined(REDIRECT) || defined(FWDFILE)
/* Open a file as a socket 
 * This allows an output socket to go either to a file,
 * or a network socket - WG7J
 */
int sockfopen(char *filename,char *mode) {
    FILE *fp;
    struct usock *up;
    struct sockaddr_fp *sfp;
    int s;

    if(strpbrk(mode,"wWaA") == NULLCHAR)
       return -1;       /* write_only files !*/

    if((s =j2socket(AF_FILE,0,0)) == -1)
        return -1;    /* No socket */

    if((up = itop(s)) == NULLUSOCK){
        /* Should not happen !*/
        close_s(s);
        return -1;
    }
    
    if((fp=fopen(filename,mode)) == NULL) {
        close_s(s);
        return -1;      /* Can't open file */
    }

    /* This could be coded in bind() also */
    up->cb.fp = fp;
    up->name = j2strdup(filename);
    up->namelen = strlen(up->name);
    sfp = mallocw(sizeof(struct sockaddr_fp));
    sfp->sfp_family = AF_FILE;
    sfp->filename = up->name;
    sfp->len = up->namelen;
    up->peername = (char *)sfp;
    up->peernamelen = sizeof(struct sockaddr_fp);
    return s;
}
#endif  /* TRACE | REDIRECT | FWDFILE */

#ifdef TRACE  
/* Open stdout as a socket 
 * This allows the trace code to go either to a file,
 * or a network socket - WG7J
 */
int stdoutsockfopen(void) {
    struct sockaddr_fp *sfp;
    struct usock *up;
    int s;

    if((s =j2socket(AF_FILE,0,0)) == -1)
        return -1;    /* No socket */

    if((up = itop(s)) == NULLUSOCK){
        /* Should not happen !*/
        close_s(s);
        return -1;
    }
    /* This could be coded in bind() also */
    up->cb.fp = stdout;
    up->name = j2strdup("stdout");
    up->namelen = strlen(up->name);
    sfp = mallocw(sizeof(struct sockaddr_fp));
    sfp->sfp_family = AF_FILE;
    sfp->filename = up->name;
    sfp->len = up->namelen;
    up->peername = (char *)sfp;
    up->peernamelen = sizeof(struct sockaddr_fp);
    return s;
}
#endif /* TRACE */

/*
 * Attach a local address/port to a socket. If not issued before a connect
 * or listen, will be issued automatically
 *
 * 10Apr06, Maiko (VE4KLM), bind renamed to j2bind() to avoid conflicts
 * with the actual system libraries of the same name. I will be doing the
 * same with accept, listen, socket, etc. Makes porting to other systems
 * alot easier in the end (I think). Most of the functions are so old, that
 * perhaps they should once and for all be renamed to be specific to JNOS.
 */
int
j2bind(s,name,namelen)
int s;      /* Socket index */
char *name; /* Local name */
int namelen;    /* Length of name */
{
    register struct usock *up;
    union sp local;
    struct socket lsock;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return -1;
    }
    if(up->type == TYPE_LOCAL_STREAM || up->type == TYPE_LOCAL_DGRAM){
        errno = EINVAL;
        return -1;
    }
    if(name == NULLCHAR){
        errno = EFAULT;
        return -1;
    }
    if(up->name != NULLCHAR){
        /* Bind has already been issued */
        errno = EINVAL;
        return -1;
    }
    if(checkaddr(up->type,name,namelen) == -1){
        /* Incorrect length or family for chosen protocol */
        errno = EAFNOSUPPORT;
        return -1;
    }
    /* Stash name in an allocated block */
    up->namelen = namelen;
    up->name = mallocw((unsigned)namelen);
    memcpy(up->name,name,(size_t)namelen);
    /* Create control block for datagram sockets */
    switch(up->type){
        case TYPE_UDP:
            local.in = (struct sockaddr_in *)up->name;
            lsock.address = local.in->sin_addr.s_addr;
            lsock.port = local.in->sin_port;
            if ((up->cb.udp = open_udp(&lsock,s_urcall)) == NULLUDP) {
                errno = EISCONN;
                return -1;
            }
            up->cb.udp->user = s;
            break;
    }
    return 0;
}
/* Post a listen on a socket */
int
j2listen(s,backlog)
int s;      /* Socket index */
int backlog;    /* 0 for a single connection, 1 for multiple connections */
{
    register struct usock *up;
    union sp local;
    struct socket lsock;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return -1;
    }
    if(up->type == TYPE_LOCAL_STREAM || up->type == TYPE_LOCAL_DGRAM){
        errno = EINVAL;
        return -1;
    }
    if(up->cb.p != NULLCHAR){
        errno = EISCONN;
        return -1;
    }
    switch(up->type){
        case TYPE_TCP:
            if(up->name == NULLCHAR)
                autobind(s,AF_INET);
  
            local.in = (struct sockaddr_in *)up->name;
            lsock.address = local.in->sin_addr.s_addr;
            lsock.port = local.in->sin_port;
            up->cb.tcb = open_tcp(&lsock,NULLSOCK,
            backlog ? TCP_SERVER:TCP_PASSIVE,0,
            s_trcall,s_ttcall,s_tscall,0,s);
            break;
#ifdef AX25
        case TYPE_AX25I:
            if(up->name == NULLCHAR)
                autobind(s,AF_AX25);
            if(s != Axi_sock){
                errno = EOPNOTSUPP;
                return -1;
            }
            local.ax = (struct sockaddr_ax *)up->name;
            up->cb.ax25 = open_ax25(NULLIF,local.ax->ax25_addr,NULLCHAR,
            backlog ? AX_SERVER:AX_PASSIVE,0,
            s_arcall,s_atcall,s_ascall,s);
            break;
#endif
#ifdef NETROM
        case TYPE_NETROML4:
            if(up->name == NULLCHAR)
                autobind(s,AF_NETROM);
            local.nr = (struct sockaddr_nr *)up->name;
            up->cb.nr4 = open_nr4(&local.nr->nr_addr,NULLNRADDR,
            backlog ? AX_SERVER:AX_PASSIVE,
            s_nrcall,s_ntcall,s_nscall,s);
            break;
#endif
        default:
        /* Listen not supported on datagram sockets */
            errno = EOPNOTSUPP;
            return -1;
    }
    return 0;
}
  
/*
 * Initiate active open. For datagram sockets, merely bind remote address.
 *
 * 10Apr06, Maiko (VE4KLM), renamed to j2connect to avoid sys call conflicts
 */
int
j2connect(s,peername,peernamelen)
int s;          /* Socket index */
char *peername;     /* Peer name */
int peernamelen;    /* Length of peer name */
{
    struct usock *up;
    union cb cb;
    union sp local,remote;
    struct socket lsock,fsock;
#ifdef  AX25
    struct iface *iface;
#endif
  
#ifndef UNIX
    if(availmem() < Memthresh){  /* Not enough resources - WG7J */
        errno = ENOMEM;
        return -1;
    }
#endif
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return -1;
    }
    if(up->type == TYPE_LOCAL_DGRAM || up->type == TYPE_LOCAL_STREAM){
        errno = EINVAL;
        return -1;
    }
    if(peername == NULLCHAR){
        /* Connect must specify a remote address */
        errno = EFAULT;
        return -1;
    }
    if(checkaddr(up->type,peername,peernamelen) == -1){
        errno = EAFNOSUPPORT;
        return -1;
    }
    /* Raw socket control blocks are created in socket() */
    if(up->type != TYPE_RAW && up->type != TYPE_NETROML3 &&
    up->cb.p != NULLCHAR){
        errno = EISCONN;
        return -1;
    }
    up->peername = mallocw((unsigned)peernamelen);
    memcpy(up->peername,peername,(size_t)peernamelen);
    up->peernamelen = peernamelen;
  
    /* Set up the local socket structures */
    if(up->name == NULLCHAR){
        switch(up->type){
            case TYPE_TCP:
            case TYPE_UDP:
            case TYPE_RAW:
                autobind(s,AF_INET);
                break;
#ifdef  AX25
            case TYPE_AX25I:
            case TYPE_AX25UI:
                autobind(s,AF_AX25);
                break;
#endif
#ifdef  NETROM
            case TYPE_NETROML3:
            case TYPE_NETROML4:
                autobind(s,AF_NETROM);
                break;
#endif
        }
    }
    switch(up->type){
        case TYPE_TCP:
        /* Construct the TCP-style ports from the sockaddr structs */
            local.in = (struct sockaddr_in *)up->name;
            remote.in = (struct sockaddr_in *)up->peername;
  
            if(local.in->sin_addr.s_addr == INADDR_ANY)
            /* Choose a local address */
                local.in->sin_addr.s_addr =
                locaddr((int32)remote.in->sin_addr.s_addr);
  
            lsock.address = local.in->sin_addr.s_addr;
            lsock.port = local.in->sin_port;
            fsock.address = remote.in->sin_addr.s_addr;
            fsock.port = remote.in->sin_port;
  
        /* Open the TCB in active mode */
            up->cb.tcb = open_tcp(&lsock,&fsock,TCP_ACTIVE,0,
            s_trcall,s_ttcall,s_tscall,0,s);
  
        /* Wait for the connection to complete */
            while((cb.tcb = up->cb.tcb) != NULLTCB && cb.tcb->state != TCP_ESTABLISHED){
#ifdef useblock
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else
#endif
                    if((errno = pwait(up)) != 0){
                        return -1;
                    }
            }
            if(cb.tcb == NULLTCB){
            /* Probably got refused */
                free(up->peername);
                up->peername = NULLCHAR;
                if(Net_error == NOROUTE)
                    errno = ECONNNOROUTE;
                else
                    errno = ECONNREFUSED;
                return -1;
            }
            break;
        case TYPE_UDP:
#ifdef  AX25
        case TYPE_AX25UI:
#endif
#ifdef  NETROM
        case TYPE_NETROML3:
#endif
        case TYPE_RAW:
        /* Control block already created by bind() */
            break;
#ifdef  AX25
        case TYPE_AX25I:
            local.ax = (struct sockaddr_ax *)up->name;
            remote.ax = (struct sockaddr_ax *)up->peername;
#ifdef JNOS20_SOCKADDR_AX
            if ((iface = if_lookup2 (remote.ax->iface_index)) == NULLIF)
#else
            if((iface = if_lookup(remote.ax->iface)) == NULLIF)
#endif
			{
                errno = EINVAL;
                return -1;
            }
            if(local.ax->ax25_addr[0] == '\0'){
                char *src_call;
            /* The local address was unspecified; set it from the TTYCALL address
             * (if defined) else from the BBSCALL (if defined) else the interface address.
             */
                src_call = iface->hwaddr;
#if defined(TTYCALL) && defined(TTYCALL_CONNECT)
                if (Ttycall[0])  /* KA1NNN */
                    src_call = Ttycall;
#ifdef MAILBOX
                else
#endif
#endif
#ifdef MAILBOX
                if (Bbscall[0])  /* WG7J */
                    src_call = Bbscall;
#endif
                memcpy(local.ax->ax25_addr,src_call,AXALEN);
            }
        /* If we already have an AX25 link we can use it */
            if((up->cb.ax25 = find_ax25(local.ax->ax25_addr, \
                remote.ax->ax25_addr,iface)) != NULLAX25
                && up->cb.ax25->state != LAPB_DISCONNECTED &&
            up->cb.ax25->user == -1) {
                up->cb.ax25->user = s;
                up->cb.ax25->r_upcall = s_arcall;
                up->cb.ax25->t_upcall = s_atcall;
                up->cb.ax25->s_upcall = s_ascall;
                if(up->cb.ax25->state == LAPB_CONNECTED
                    || up->cb.ax25->state == LAPB_RECOVERY)
                    return 0;
            } else
                up->cb.ax25 = open_ax25(iface,local.ax->ax25_addr,
                remote.ax->ax25_addr,AX_ACTIVE,
                0,s_arcall,s_atcall,s_ascall,s);
  
        /* Wait for the connection to complete */
            while((cb.ax25 = up->cb.ax25) != NULLAX25 && cb.ax25->state != LAPB_CONNECTED){
#ifdef useblock
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else
#endif
                    if((errno = pwait(up)) != 0){
                        return -1;
                    }
            }
            if(cb.ax25 == NULLAX25){
            /* Connection probably already exists */
                free(up->peername);
                up->peername = NULLCHAR;
                errno = ECONNREFUSED;
                return -1;
            }
            break;
#endif
#ifdef  NETROM
        case TYPE_NETROML4:
            local.nr = (struct sockaddr_nr *)up->name;
            remote.nr = (struct sockaddr_nr *)up->peername;
            up->cb.nr4 = open_nr4(&local.nr->nr_addr,&remote.nr->nr_addr,
            AX_ACTIVE,s_nrcall,s_ntcall,s_nscall,s);
  
        /* Wait for the connection to complete */
            while((cb.nr4 = up->cb.nr4) != NULLNR4CB && cb.nr4->state != NR4STCON){
#ifdef useblock
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else
#endif
                    if((errno = pwait(up)) != 0){
                        return -1;
                    }
            }
            if(cb.nr4 == NULLNR4CB){
            /* Connection probably already exists */
                free(up->peername);
                up->peername = NULLCHAR;
                errno = ECONNREFUSED;
                return -1;
            }
            break;
#endif
    }
    return 0;
}
/* Wait for a connection. Valid only for connection-oriented sockets. */
int
j2accept(s,peername,peernamelen)
int s;          /* Socket index */
char *peername;     /* Peer name */
int *peernamelen;   /* Length of peer name */
{
    int i;
    register struct usock *up;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return -1;
    }
    if(up->type == TYPE_LOCAL_DGRAM || up->type == TYPE_LOCAL_STREAM){
        errno = EINVAL;
        return -1;
    }
    if(up->cb.p == NULLCHAR){
        errno = EOPNOTSUPP;
        return -1;
    }
    /* Accept is valid only for stream sockets */
    switch(up->type){
        case TYPE_TCP:
#ifdef  AX25
        case TYPE_AX25I:
#endif
#ifdef  NETROM
        case TYPE_NETROML4:
#endif
            break;
        default:
            errno = EOPNOTSUPP;
            return -1;
    }
    /* Wait for the state-change upcall routine to signal us */
    while(up->cb.p != NULLCHAR && up->rdysock == -1){
#ifdef useblock
        if(up->noblock){
            errno = EWOULDBLOCK;
            return -1;
        } else
#endif
            if((errno = pwait(up)) != 0){
                return -1;
            }
    }
    if(up->cb.p == NULLCHAR){
        /* Blown away */
        errno = EBADF;
        return -1;
    }
    i = up->rdysock;
    up->rdysock = -1;
  
    up = itop(i);
    if(peername != NULLCHAR && peernamelen != NULL){
        *peernamelen = min(up->peernamelen,*peernamelen);
        memcpy(peername,up->peername,(size_t)*peernamelen);
    }
    /* set the time this was created - WG7J */
    up->created = time(NULL);
    return i;
}
/* Low-level receive routine. Passes mbuf back to user; more efficient than
 * higher-level functions recv() and recvfrom(). Datagram sockets ignore
 * the len parameter.
 */
int
recv_mbuf(s,bpp,flags,from,fromlen)
int s;          /* Socket index */
struct mbuf **bpp;  /* Place to stash receive buffer */
int flags;      /* Unused; will control out-of-band data, etc */
char *from;     /* Peer address (only for datagrams) */
int *fromlen;       /* Length of peer address */
{
    register struct usock *up;
    int cnt=0;
    union cb cb;
    struct socket fsocket;
#ifdef  NETROM
    struct nr3hdr n3hdr;
#endif
    union sp remote;
    struct ip ip;
    struct mbuf *bp;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return -1;
    }
    if(up->ibuf != NULLBUF){
        /* Return input buffer */
        cnt = len_p(up->ibuf);
        if(bpp != NULLBUFP)
            *bpp = up->ibuf;
        else
            free_p(up->ibuf);
        up->ibuf = NULLBUF;
        return cnt;
    }
    switch(up->type){
        case TYPE_LOCAL_STREAM:
        case TYPE_LOCAL_DGRAM:
			/*
			 * 05Apr2014, Maiko (VE4KLM), One must always check to see if
			 * socket is valid, if we don't then JNOS might crash. Many a
			 * time you see 'mbox reset <call>' crashing JNOS. This is one
			 * spot where it can happen. Actually caught this during WIN32
			 * development when I was playing with WINMOR on Windows 7 ...
			 */
            while (itop (s) != NULLUSOCK &&
					up->cb.local != NULLLOC &&
					up->cb.local->q == NULLBUF &&
					up->cb.local->peer != NULLUSOCK)
			{
#ifdef useblock
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else
#endif
                    if((errno = pwait(up)) != 0){
                        return -1;
                    }
            }

			/* 05Apr2014, Maiko (VE4KLM), Must check for valid socket */
			if (itop (s) == NULLUSOCK)
			{
				log (-1, "socket %d has vanished", s);
				return -1;
			}

            if(up->cb.local == NULLLOC){
                errno = EBADF;
                return -1;
            }
            if(up->cb.local->q == NULLBUF &&
            up->cb.local->peer == NULLUSOCK){
                errno = ENOTCONN;
                return -1;
            }
        /* For datagram sockets, this will return the
         * first packet on the queue. For stream sockets,
         * this will return everything.
         */
            bp = dequeue(&up->cb.local->q);
            if(up->cb.local->q == NULLBUF
                && (up->cb.local->flags & LOC_SHUTDOWN))
                close_s(s);
            j2psignal(up,0);
            cnt = len_p(bp);
            break;

        case TYPE_TCP:

			/*
			 * 06Jan08, Convers is crashing like crazy when network suffers
			 * a disconnect - it *looks* like the tcb is invalid at some point
			 * but the original code does not know how to see it. This looks
			 * tricky, let's try to use the tcpval() function first ...
			 */
			while (1)
			{	
				/* 12Apr2014, Maiko (VE4KLM), Again, check for valid socket ! */
				if (itop (s) == NULLUSOCK)
				{
					log (-1, "socket %d has vanished", s);
					return -1;
				}

			    if(!tcpval(cb.tcb = up->cb.tcb))
					return -1;

				if (cb.tcb == NULLTCB)
					break;

				if (cb.tcb->r_upcall == trdiscard)
					break;

            	if ((cnt = recv_tcp (cb.tcb, &bp, 0)) != -1)
					break;

                if((errno = pwait(up)) != 0)
                   return -1;
            }

            if(cb.tcb == NULLTCB)
			{
            /* Connection went away */
                errno = ENOTCONN;
                return -1;
            }
			else if(cb.tcb->r_upcall == trdiscard)
			{
            /* Receive shutdown has been done */
                errno = ENOTCONN;   /* CHANGE */
                return -1;
            }
            break;

        case TYPE_UDP:
            while((cb.udp = up->cb.udp) != NULLUDP
            && (cnt = recv_udp(cb.udp,&fsocket,&bp)) == -1){
#ifdef useblock
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else
#endif
                    if((errno = pwait(up)) != 0){
                        return -1;
                    }
            }
            if(cb.udp == NULLUDP){
            /* Connection went away */
                errno = ENOTCONN;
                return -1;
            }
            if(from != NULLCHAR && fromlen != (int *)NULL && *fromlen >= (int)SOCKSIZE){
                remote.in = (struct sockaddr_in *)from;
                remote.in->sin_family = AF_INET;
                remote.in->sin_addr.s_addr = fsocket.address;
                remote.in->sin_port = fsocket.port;
                *fromlen = SOCKSIZE;
            }
            break;
        case TYPE_RAW:
            while((cb.rip = up->cb.rip) != NULLRIP
            && cb.rip->rcvq == NULLBUF){
#ifdef useblock
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else
#endif
                    if((errno = pwait(up)) != 0){
                        return -1;
                    }
            }
            if(cb.rip == NULLRIP){
            /* Connection went away */
                errno = ENOTCONN;
                return -1;
            }
            bp = dequeue(&cb.rip->rcvq);
            ntohip(&ip,&bp);
  
            cnt = len_p(bp);
            if(from != NULLCHAR && fromlen != (int *)NULL && *fromlen >= (int)SOCKSIZE){
                remote.in = (struct sockaddr_in *)from;
                remote.in->sin_family = AF_INET;
                remote.in->sin_addr.s_addr = ip.source;
                remote.in->sin_port = 0;
                *fromlen = SOCKSIZE;
            }
            break;
#ifdef  AX25
        case TYPE_AX25I:
            while((cb.ax25 = up->cb.ax25) != NULLAX25
            && (bp = recv_ax25(cb.ax25,(int16)0)) == NULLBUF){
#ifdef useblock
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else
#endif
                    if((errno = pwait(up)) != 0){
                        return -1;
                    }
            }
            if(cb.ax25 == NULLAX25){
            /* Connection went away */
                errno = ENOTCONN;
                return -1;
            }
            cnt = bp->cnt;
            break;
#endif /* ax25 */
#ifdef NETROM
        case TYPE_NETROML3:
            while((cb.rnr = up->cb.rnr) != NULLRNR
            && cb.rnr->rcvq == NULLBUF){
#ifdef useblock
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else
#endif
                    if((errno = pwait(up)) != 0){
                        return -1;
                    }
            }
            if(cb.rnr == NULLRNR){
            /* Connection went away */
                errno = ENOTCONN;
                return -1;
            }
            bp = dequeue(&cb.rnr->rcvq);
            ntohnr3(&n3hdr,&bp);
            cnt = len_p(bp);
            if(from != NULLCHAR && fromlen != NULLINT
            && *fromlen >= (int)sizeof(struct sockaddr_nr)){
                remote.nr = (struct sockaddr_nr *)from;
                remote.nr->nr_family = AF_NETROM;
            /* The callsign of the local user is not part of
               NET/ROM level 3, so that field is not used here */
                memcpy(remote.nr->nr_addr.node,n3hdr.source,AXALEN);
                *fromlen = sizeof(struct sockaddr_nr);
            }
            break;
        case TYPE_NETROML4:
            while((cb.nr4 = up->cb.nr4) != NULLNR4CB
            && (bp = recv_nr4(cb.nr4,(int16)0)) == NULLBUF){
#ifdef useblock
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else
#endif
                    if((errno = pwait(up)) != 0){
                        return -1;
                    }
            }
            if(cb.nr4 == NULLNR4CB){
            /* Connection went away */
                errno = ENOTCONN;
                return -1;
            }
            cnt = bp->cnt;
            break;
#endif
    }
    if(bpp != NULLBUFP)
        *bpp = bp;
    else
        free_p(bp);
    return cnt;
}
/* Low level send routine; user supplies mbuf for transmission. More
 * efficient than send() or sendto(), the higher level interfaces.
 * The "to" and "tolen" parameters are ignored on connection-oriented
 * sockets.
 *
 * In case of error, bp is freed so the caller doesn't have to worry about it.
 */
int
send_mbuf(s,bp,flags,to,tolen)
int s;          /* Socket index */
struct mbuf *bp;    /* Buffer to send */
int flags;      /* not currently used */
char *to;       /* Destination, only for datagrams */
int tolen;      /* Length of destination */
{
    register struct usock *up;
    union cb cb;
    union sp local,remote;
    struct socket lsock,fsock;
    int cnt;
  
    if((up = itop(s)) == NULLUSOCK){
        free_p(bp);
        errno = EBADF;
        return -1;
    }
    if(up->obuf != NULLBUF){
        /* If there's unflushed output, send it.
         * Note the importance of clearing up->obuf
         * before the recursive call!
         */
        struct mbuf *bp1;
  
        bp1 = up->obuf;
        up->obuf = NULLBUF;
        send_mbuf(s,bp1,flags,to,tolen);
    }
    if(up->name == NULLCHAR){
        /* Automatic local name assignment for datagram sockets */
        switch(up->type){
            case TYPE_LOCAL_STREAM:
            case TYPE_LOCAL_DGRAM:
                break;  /* no op */
            case TYPE_UDP:
            case TYPE_RAW:
                autobind(s,AF_INET);
                break;
#ifdef  AX25
            case TYPE_AX25UI:
                autobind(s,AF_AX25);
                break;
#endif
#ifdef  NETROM
            case TYPE_NETROML3:
            case TYPE_NETROML4:
                autobind(s,AF_NETROM);
                break;
#endif
            default:
                free_p(bp);
                errno = ENOTCONN;
                return -1;
        }
    }
    cnt = len_p(bp);
    switch(up->type){
        case TYPE_WRITE_ONLY_FILE:
            {
            struct mbuf *tbp = bp;
            FILE *fp = up->cb.fp;
            while(bp) {
                if(fwrite(bp->data,bp->cnt,1,fp) != 1) {
                    errno = EBADF;
                    return -1;
                }
                bp=bp->next;
            }
            free_p(tbp);
            break;
            }
        case TYPE_LOCAL_DGRAM:
            if(up->cb.local->peer == NULLUSOCK){
                free_p(bp);
                errno = ENOTCONN;
                return -1;
            }
            enqueue(&up->cb.local->peer->cb.local->q,bp);
            j2psignal(up->cb.local->peer,0);
        /* If high water mark has been reached, block */
            while(up->cb.local->peer != NULLUSOCK &&
                len_q(up->cb.local->peer->cb.local->q) >=
            up->cb.local->peer->cb.local->hiwat){
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else if((errno = pwait(up->cb.local->peer)) != 0){
                    return -1;
                }
            }
            if(up->cb.local->peer == NULLUSOCK){
                errno = ENOTCONN;
                return -1;
            }
            break;
        case TYPE_LOCAL_STREAM:
            if(up->cb.local->peer == NULLUSOCK){
                free_p(bp);
                errno = ENOTCONN;
                return -1;
            }
            append(&up->cb.local->peer->cb.local->q,bp);
            j2psignal(up->cb.local->peer,0);
        /* If high water mark has been reached, block */
            while(up->cb.local->peer != NULLUSOCK &&
                len_p(up->cb.local->peer->cb.local->q) >=
            up->cb.local->peer->cb.local->hiwat){
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else if((errno = pwait(up->cb.local->peer)) != 0){
                    return -1;
                }
            }
            if(up->cb.local->peer == NULLUSOCK){
                errno = ENOTCONN;
                return -1;
            }
            break;
        case TYPE_TCP:
            if((cb.tcb = up->cb.tcb) == NULLTCB){
                free_p(bp);
                errno = ENOTCONN;
                return -1;
            }
            cnt = send_tcp(cb.tcb,bp);
  
            while((cb.tcb = up->cb.tcb) != NULLTCB &&
            cb.tcb->sndcnt > cb.tcb->window){
            /* Send queue is full */
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else if((errno = pwait(up)) != 0){
                    return -1;
                }
            }
            if(cb.tcb == NULLTCB){
                errno = ENOTCONN;
                return -1;
            }
            break;
        case TYPE_UDP:
            local.in = (struct sockaddr_in *)up->name;
            lsock.address = local.in->sin_addr.s_addr;
            lsock.port = local.in->sin_port;
            if(to != NULLCHAR)
                remote.in = (struct sockaddr_in *)to;
            else if(up->peername != NULLCHAR)
                remote.in = (struct sockaddr_in *)up->peername;
            else {
                errno = ENOTCONN;
                return -1;
            }
            fsock.address = remote.in->sin_addr.s_addr;
            fsock.port = remote.in->sin_port;
            send_udp(&lsock,&fsock,0,0,bp,0,0,0);
            break;
        case TYPE_RAW:
            local.in = (struct sockaddr_in *)up->name;
            if(to != NULLCHAR)
                remote.in = (struct sockaddr_in *)to;
            else if(up->peername != NULLCHAR)
                remote.in = (struct sockaddr_in *)up->peername;
            else {
                errno = ENOTCONN;
                return -1;
            }
            ip_send((int32)local.in->sin_addr.s_addr,(int32)remote.in->sin_addr.s_addr,
            (char)up->cb.rip->protocol,0,0,bp,0,0,0);
            break;
#ifdef  AX25
        case TYPE_AX25I:
            if((cb.ax25 = up->cb.ax25) == NULLAX25){
                free_p(bp);
                errno = ENOTCONN;
                return -1;
            }
            send_ax25(cb.ax25,bp,PID_NO_L3);
  
            while((cb.ax25 = up->cb.ax25) != NULLAX25 &&
            len_q(cb.ax25->txq) * cb.ax25->paclen > cb.ax25->window){
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else if((errno = pwait(up)) != 0){
                    return -1;
                }
            }
            if(cb.ax25 == NULLAX25){
                errno = EBADF;
                return -1;
            }
            break;
        case TYPE_AX25UI:
            local.ax = (struct sockaddr_ax *)up->name;
            if(to != NULLCHAR)
                remote.ax = (struct sockaddr_ax *)to;
            else if(up->peername != NULLCHAR)
                remote.ax = (struct sockaddr_ax *)up->peername;
            else {
                errno = ENOTCONN;
                return -1;
            }
#ifdef JNOS20_SOCKADDR_AX
            ax_output (if_lookup2 (remote.ax->iface_index),
#else
            ax_output(if_lookup(remote.ax->iface),
#endif
            remote.ax->ax25_addr, local.ax->ax25_addr,PID_NO_L3,bp);
            break;
#endif
#ifdef NETROM
        case TYPE_NETROML3:
        /* This should never happen, since the TCP code will peek at the
         * interface mtu ! - WG7J
         */
            if(len_p(bp) > NR4MAXINFO) {
                free_p(bp);
                errno = EMSGSIZE;
                return -1;
            }
            local.nr = (struct sockaddr_nr *)up->name;
            if(to != NULLCHAR)
                remote.nr = (struct sockaddr_nr *)to;
            else if(up->peername != NULLCHAR)
                remote.nr = (struct sockaddr_nr *)up->peername;
            else {
                errno = ENOTCONN;
                return -1;
            }
        /* The NETROM username is always ignored in outgoing traffic */
            nr_sendraw(remote.nr->nr_addr.node,
            up->cb.rnr->protocol,up->cb.rnr->protocol,bp);
            break;
        case TYPE_NETROML4:
            if((cb.nr4 = up->cb.nr4) == NULLNR4CB) {
                free_p(bp);
                errno = ENOTCONN;
                return -1;
            }
            send_nr4(cb.nr4,bp);
  
            while((cb.nr4 = up->cb.nr4) != NULLNR4CB &&
            cb.nr4->nbuffered >= cb.nr4->window){
                if(up->noblock){
                    errno = EWOULDBLOCK;
                    return -1;
                } else if((errno = pwait(up)) != 0){
                    errno = EINTR;
                    return -1;
                }
            }
            if(cb.nr4 == NULLNR4CB){
                errno = EBADF;
                return -1;
            }
            break;
#endif
    }
    return cnt;
}
/* Return local name passed in an earlier bind() call */
int
j2getsockname(s,name,namelen)
int s;      /* Socket index */
char *name; /* Place to stash name */
int *namelen;   /* Length of same */
{
    register struct usock *up;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return -1;
    }
    if(name == NULLCHAR || namelen == (int *)NULL){
        errno = EFAULT;
        return -1;
    }
    if(up->name == NULLCHAR){
        /* Not bound yet */
        *namelen = 0;
        return 0;
    }
    if(up->name != NULLCHAR){
        *namelen = min(*namelen,up->namelen);
        memcpy(name,up->name,(size_t)*namelen);
    }
    return 0;
}
/* Get remote name, returning result of earlier connect() call. */
int
j2getpeername(s,peername,peernamelen)
int s;          /* Socket index */
char *peername;     /* Place to stash name */
int *peernamelen;   /* Length of same */
{
    register struct usock *up;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return -1;
    }
    if(up->peername == NULLCHAR){
        errno = ENOTCONN;
        return -1;
    }
    if(peername == NULLCHAR || peernamelen == (int *)NULL){
        errno = EFAULT;
        return -1;
    }
    *peernamelen = min(*peernamelen,up->peernamelen);
    memcpy(peername,up->peername,(size_t)*peernamelen);
    return 0;
}
/* Return length of protocol queue, either send or receive. */
int
socklen(s,rtx)
int s;      /* Socket index */
int rtx;    /* 0 = receive queue, 1 = transmit queue */
{
    register struct usock *up;
    int len = -1;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return -1;
    }
    if(up->cb.p == NULLCHAR){
        errno = ENOTCONN;
        return -1;
    }
    if(rtx < 0 || rtx > 1){
        errno = EINVAL;
        return -1;
    }
    switch(up->type){
        case TYPE_LOCAL_DGRAM:
        switch(rtx){
            case 0:
                len = len_q(up->cb.local->q);
                break;
            case 1:
                if(up->cb.local->peer != NULLUSOCK)
                    len = len_q(up->cb.local->peer->cb.local->q);
                break;
        }
            break;
        case TYPE_LOCAL_STREAM:
        switch(rtx){
            case 0:
                len = len_p(up->cb.local->q) + len_p(up->ibuf);
                break;
            case 1:
                if(up->cb.local->peer != NULLUSOCK)
                    len = len_p(up->cb.local->peer->cb.local->q)
                    + len_p(up->obuf);
                break;
        }
            break;
        case TYPE_TCP:
        switch(rtx){
            case 0:
                len = up->cb.tcb->rcvcnt + len_p(up->ibuf);
                break;
            case 1:
                len = up->cb.tcb->sndcnt + len_p(up->obuf);
                break;
        }
            break;
        case TYPE_UDP:
        switch(rtx){
            case 0:
                len = up->cb.udp->rcvcnt;
                break;
            case 1:
                len = 0;
                break;
        }
            break;
#ifdef  AX25
        case TYPE_AX25I:
        switch(rtx){
            case 0:
                len = len_p(up->cb.ax25->rxq) + len_p(up->ibuf);
                break;
            case 1: /* Number of packets, not bytes */
                len = len_q(up->cb.ax25->txq);
                if(up->obuf != NULLBUF)
                    len++;
        }
            break;
#endif
#ifdef NETROM
        case TYPE_NETROML3:
        switch(rtx){
            case 0:
                len = len_q(up->cb.rnr->rcvq);
                break;
            case 1:
                len = 0;
        }
            break;
        case TYPE_NETROML4:
        switch(rtx){
            case 0:
                len = len_p(up->cb.nr4->rxq) + len_p(up->ibuf);
                break;
            case 1: /* Number of packets, not bytes */
                len = len_q(up->cb.nr4->txq);
                if(up->obuf != NULLBUF)
                    len++;
                break;
        }
            break;
#endif
        case TYPE_RAW:
        switch(rtx){
            case 0:
                len = len_q(up->cb.rip->rcvq);
                break;
            case 1:
                len = 0;
                break;
        }
            break;
    }
    return len;
}
/* Force retransmission. Valid only for connection-oriented sockets. */
int
sockkick(s)
int s;  /* Socket index */
{
    register struct usock *up;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return -1;
    }
    if(up->type == TYPE_LOCAL_STREAM || up->type == TYPE_LOCAL_DGRAM){
        errno = EINVAL;
        return -1;
    }
    if(up->cb.p == NULLCHAR){
        errno = ENOTCONN;
        return -1;
    }
    switch(up->type){
        case TYPE_TCP:
            kick_tcp(up->cb.tcb);
            break;
#ifdef  AX25
        case TYPE_AX25I:
            kick_ax25(up->cb.ax25);
            break;
#endif
#ifdef NETROM
        case TYPE_NETROML4:
            kick_nr4(up->cb.nr4);
            break;
#endif
        default:
        /* Datagram sockets can't be kicked */
            errno = EOPNOTSUPP;
            return -1;
    }
    return 0;
}
  
/* Change owner of socket, return previous owner */
struct proc *
sockowner(s,newowner)
int s;          /* Socket index */
struct proc *newowner;  /* Process table address of new owner */
{
    register struct usock *up;
    struct proc *pp;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return NULLPROC;
    }
    pp = up->owner;
    if(newowner != NULLPROC)
        up->owner = newowner;
    return pp;
}
/* Close down a socket three ways. Type 0 means "no more receives"; this
 * replaces the incoming data upcall with a routine that discards further
 * data. Type 1 means "no more sends", and obviously corresponds to sending
 * a TCP FIN. Type 2 means "no more receives or sends". This I interpret
 * as "abort the connection".
 */
int
j2shutdown(s,how)
int s;      /* Socket index */
int how;    /* (see above) */
{
    register struct usock *up;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return -1;
    }
    if(up->cb.p == NULLCHAR){
        errno = ENOTCONN;
        return -1;
    }
    switch(up->type){
        case TYPE_LOCAL_DGRAM:
        case TYPE_LOCAL_STREAM:
            if(up->cb.local->q == NULLBUF)
                close_s(s);
            else
                up->cb.local->flags = LOC_SHUTDOWN;
            break;
        case TYPE_TCP:
        switch(how){
            case 0: /* No more receives -- replace upcall */
                up->cb.tcb->r_upcall = trdiscard;
                break;
            case 1: /* Send EOF */
                close_tcp(up->cb.tcb);
                break;
            case 2: /* Blow away TCB */
                reset_tcp(up->cb.tcb);
                up->cb.tcb = NULLTCB;
                break;
        }
            break;
#ifdef  AX25
        case TYPE_AX25I:
        switch(how){
            case 0:
            case 1: /* Attempt regular disconnect */
                disc_ax25(up->cb.ax25);
                break;
            case 2: /* Blow it away */
                reset_ax25(up->cb.ax25);
                up->cb.ax25 = NULLAX25;
                break;
        }
            break;
#endif
#ifdef  NETROM
        case TYPE_NETROML3:
            close_s(s);
            break;
        case TYPE_NETROML4:
        switch(how){
            case 0:
            case 1: /* Attempt regular disconnect */
                disc_nr4(up->cb.nr4);
                break;
            case 2: /* Blow it away */
                reset_nr4(up->cb.nr4);
                up->cb.nr4 = NULLNR4CB;
                break;
        }
            break;
#endif
        case TYPE_RAW:
        case TYPE_UDP:
            close_s(s);
            break;
        default:
            errno = EOPNOTSUPP;
            return -1;
    }
    j2psignal(up,0);
    return 0;
}
/* Close a socket, freeing it for reuse. Try to do a graceful close on a
 * TCP socket, if possible
 */
int
close_s(s)
int s;      /* Socket index */
{
    struct usock *up;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return -1;
    }
    if(--up->refcnt > 0)
        return 0;   /* Others are still using it */
    usflush(s);
    if(up->ibuf != NULLBUF){
        free_p(up->ibuf);
        up->ibuf = NULLBUF;
    }
    switch(up->type){
        case TYPE_WRITE_ONLY_FILE:
            fclose(up->cb.fp);
            break;
        case TYPE_LOCAL_STREAM:
        case TYPE_LOCAL_DGRAM:
            if(up->cb.local->peer != NULLUSOCK){
                up->cb.local->peer->cb.local->peer = NULLUSOCK;
                j2psignal(up->cb.local->peer,0);
            }
            free_q(&up->cb.local->q);
            free(up->cb.local);
            break;
        case TYPE_TCP:
            if(up->cb.tcb != NULLTCB){  /* In case it's been reset */
                up->cb.tcb->r_upcall = trdiscard;
            /* Tell the TCP_CLOSED upcall there's no more socket */
                up->cb.tcb->user = -1;
                close_tcp(up->cb.tcb);
            }
            break;
        case TYPE_UDP:
            if(up->cb.udp != NULLUDP){
                del_udp(up->cb.udp);
            }
            break;
#ifdef  AX25
        case TYPE_AX25I:
            if(up->cb.ax25 != NULLAX25){
            /* Tell the TCP_CLOSED upcall there's no more socket */
                up->cb.ax25->user = -1;
                disc_ax25(up->cb.ax25);
            }
            break;
#endif
#ifdef  NETROM
        case TYPE_NETROML4:
            if(up->cb.nr4 != NULLNR4CB){
            /* Tell the TCP_CLOSED upcall there's no more socket */
                up->cb.nr4->user = -1;
                disc_nr4(up->cb.nr4);
            }
            break;
#endif
        case TYPE_RAW:
            del_ip(up->cb.rip);
            break;
        default:
            errno = EOPNOTSUPP;
            return -1;
    }
#ifdef  LZW
    if(up->zout != NULLLZW || up->zin != NULLLZW)
        lzwfree(up);
#endif
    free(up->name);
    free(up->peername);
  
    j2psignal(up,0);  /* Wake up anybody doing an accept() or recv() */
#ifdef LOOKSESSION
    if(up->look)    /* Alert the process looking at us */
        alert(up->look,ENOTCONN);
#endif
    deletesocket(s);
    return 0;
}
  
/* Increment reference count for specified socket */
int
usesock(s)
int s;
{
    struct usock *up;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return -1;
    }
    up->refcnt++;
    return 0;
}
  
/* Blow away all sockets belonging to a certain process. Used by killproc(). */
void
freesock(pp)
struct proc *pp;
{
    struct usock *up;
    int s;
  
    s = 0;
    while((s=getnextsocket(s)) != -1) {
        up = itop(s);
        if(up != NULLUSOCK && up->owner == pp) {
            j2shutdown(s,2);
        }
    }
}
  
/* Start of internal subroutines */
  
/* Raw IP receive upcall routine */
static void
rip_recv(rp)
struct raw_ip *rp;
{
    j2psignal(itop(rp->user),1);
    pwait(NULL);
}
  
/* UDP receive upcall routine */
static void
s_urcall(iface,udp,cnt)
struct iface *iface;
struct udp_cb *udp;
int cnt;
{
    j2psignal(itop(udp->user),1);
    pwait(NULL);
}
/* TCP receive upcall routine */
static void
s_trcall(tcb,cnt)
struct tcb *tcb;
int cnt;
{
    /* Wake up anybody waiting for data, and let them run */
    j2psignal(itop(tcb->user),1);
    pwait(NULL);
}
/* TCP transmit upcall routine */
static void
s_ttcall(tcb,cnt)
struct tcb *tcb;
int cnt;
{
    /* Wake up anybody waiting to send data, and let them run */
    j2psignal(itop(tcb->user),1);
    pwait(NULL);
}
/* TCP state change upcall routine */
static void
s_tscall(tcb,old,new)
struct tcb *tcb;
int old,new;
{
    int s,ns;
    struct usock *up,*nup,*oup,*next;
    union sp sp;
  
    s = tcb->user;
    oup = up = itop(s);
  
    switch(new){
        case TCP_CLOSED:
        /* Clean up. If the user has already closed the socket,
         * then up will be null (s was set to -1 by the close routine).
         * If not, then this is an abnormal close (e.g., a reset)
         * and clearing out the pointer in the socket structure will
         * prevent any further operations on what will be a freed
         * control block. Also wake up anybody waiting on events
         * related to this tcb so they will notice it disappearing.
         */
            if(up != NULLUSOCK){
                up->cb.tcb = NULLTCB;
                up->errcodes[0] = tcb->reason;
                up->errcodes[1] = tcb->type;
                up->errcodes[2] = tcb->code;
            }
            del_tcp(tcb);
            break;
        case TCP_SYN_RECEIVED:
        /* Handle an incoming connection. If this is a server TCB,
         * then we're being handed a "clone" TCB and we need to
         * create a new socket structure for it. In either case,
         * find out who we're talking to and wake up the guy waiting
         * for the connection.
         */
            if(tcb->flags.clone){
            /* Clone the socket */
                ns = j2socket(AF_INET,SOCK_STREAM,0);
                nup = itop(ns);
                next = nup->next;
                ASSIGN(*nup,*up);
                nup->next = next;
                nup->number = ns;
                tcb->user = ns;
                nup->cb.tcb = tcb;
            /* Allocate new memory for the name areas */
                nup->name = mallocw(SOCKSIZE);
                nup->peername = mallocw(SOCKSIZE);
            /* Store the new socket # in the old one */
                up->rdysock = ns;
                up = nup;
                s = ns;
            } else {
            /* Allocate space for the peer's name */
                up->peername = mallocw(SOCKSIZE);
            /* Store the old socket # in the old socket */
                up->rdysock = s;
            }
        /* Load the addresses. Memory for the name has already
         * been allocated, either above or in the original bind.
         */
            sp.p = up->name;
            sp.in->sin_family = AF_INET;
            sp.in->sin_addr.s_addr = up->cb.tcb->conn.local.address;
            sp.in->sin_port = up->cb.tcb->conn.local.port;
            up->namelen = SOCKSIZE;
  
            sp.p = up->peername;
            sp.in->sin_family = AF_INET;
            sp.in->sin_addr.s_addr = up->cb.tcb->conn.remote.address;
            sp.in->sin_port = up->cb.tcb->conn.remote.port;
            up->peernamelen = SOCKSIZE;
  
        /* Wake up the guy accepting it, and let him run */
            j2psignal(oup,1);
            pwait(NULL);
            break;
        default:    /* Ignore all other state transitions */
            break;
    }
    j2psignal(up,0);  /* In case anybody's waiting */
}
/* Discard data received on a TCP connection. Used after a receive shutdown or
 * close_s until the TCB disappears.
 */
static void
trdiscard(tcb,cnt)
struct tcb *tcb;
int cnt;
{
    struct mbuf *bp;
  
    recv_tcp(tcb,&bp,(int16)cnt);
    free_p(bp);
}
#ifdef  AX25
/* AX.25 receive upcall */
void
s_arcall(axp,cnt)
struct ax25_cb *axp;
int cnt;
{
    int ns;
    struct usock *up,*nup,*oup,*next;
    union sp sp;
  
    up = itop(axp->user);
    /* When AX.25 data arrives for the first time the AX.25 listener
       is notified, if active. If the AX.25 listener is a server its
       socket is duplicated in the same manner as in s_tscall().
     */
    if (Axi_sock != -1 && axp->user == -1) {
        oup = up = itop(Axi_sock);
        /* From now on, use the same upcalls as the listener */
        axp->t_upcall = up->cb.ax25->t_upcall;
        axp->r_upcall = up->cb.ax25->r_upcall;
        axp->s_upcall = up->cb.ax25->s_upcall;
        if (up->cb.ax25->flags.clone) {
            /* Clone the socket */
            ns = j2socket(AF_AX25,SOCK_STREAM,0);
            nup = itop(ns);
            next = nup->next;
            ASSIGN(*nup,*up);
            nup->next = next;
            nup->number = ns;
            axp->user = ns;
            nup->cb.ax25 = axp;
            /* Allocate new memory for the name areas */
            nup->name = mallocw(sizeof(struct sockaddr_ax));
            nup->peername = mallocw(sizeof(struct sockaddr_ax));
            /* Store the new socket # in the old one */
            up->rdysock = ns;
            up = nup;
        } else {
            axp->user = Axi_sock;
            del_ax25(up->cb.ax25);
            up->cb.ax25 = axp;
            /* Allocate space for the peer's name */
            up->peername = mallocw(sizeof(struct sockaddr_ax));
            /* Store the old socket # in the old socket */
            up->rdysock = Axi_sock;
        }
        /* Load the addresses. Memory for the name has already
         * been allocated, either above or in the original bind.
         */
        sp.p = up->name;
        sp.ax->sax_family = AF_AX25;
        memcpy(sp.ax->ax25_addr,axp->local,AXALEN);
#ifdef JNOS20_SOCKADDR_AX
	/* 30Aug2010, Maiko, New way to reference interface information */
        memset (sp.ax->filler, 0, ILEN);
	sp.ax->iface_index = if_indexbyname (axp->iface->name);
#else
        memcpy(sp.ax->iface,axp->iface->name,ILEN);
#endif
        up->namelen = sizeof(struct sockaddr_ax);
  
        sp.p = up->peername;
        sp.ax->sax_family = AF_AX25;
        memcpy(sp.ax->ax25_addr,axp->remote,AXALEN);
#ifdef JNOS20_SOCKADDR_AX
	/* 30Aug2010, Maiko, New way to reference interface information */
        memset (sp.ax->filler, 0, ILEN);
	sp.ax->iface_index = if_indexbyname (axp->iface->name);
#else
        memcpy(sp.ax->iface,axp->iface->name,ILEN);
#endif
        up->peernamelen = sizeof(struct sockaddr_ax);
        /* Wake up the guy accepting it, and let him run */
        j2psignal(oup,1);
        pwait(NULL);
        return;
    }
    /* Wake up anyone waiting, and let them run */
    j2psignal(up,1);
    pwait(NULL);
}
/* AX.25 transmit upcall */
void
s_atcall(axp,cnt)
struct ax25_cb *axp;
int cnt;
{
    /* Wake up anyone waiting, and let them run */
    j2psignal(itop(axp->user),1);
    pwait(NULL);
}
/* AX25 state change upcall routine */
void
s_ascall(axp,old,new)
register struct ax25_cb *axp;
int old,new;
{
    int s;
    struct usock *up;
  
    s = axp->user;
    up = itop(s);
  
    switch(new){
        case LAPB_DISCONNECTED:
        /* Clean up. If the user has already closed the socket,
         * then up will be null (s was set to -1 by the close routine).
         * If not, then this is an abnormal close (e.g., a reset)
         * and clearing out the pointer in the socket structure will
         * prevent any further operations on what will be a freed
         * control block. Also wake up anybody waiting on events
         * related to this block so they will notice it disappearing.
         */
            if(up != NULLUSOCK){
                up->errcodes[0] = axp->reason;
                up->cb.ax25 = NULLAX25;
            }
            del_ax25(axp);
            break;
        default:    /* Other transitions are ignored */
            break;
    }
    j2psignal(up,0);  /* In case anybody's waiting */
}
#endif
#ifdef NETROM
/* NET/ROM receive upcall routine */
static void
s_nrcall(cb,cnt)
struct nr4cb *cb;
int cnt;
{
    /* Wake up anybody waiting for data, and let them run */
    j2psignal(itop(cb->user),1);
    pwait(NULL);
}
/* NET/ROM transmit upcall routine */
static void
s_ntcall(cb,cnt)
struct nr4cb *cb;
int cnt;
{
    /* Wake up anybody waiting to send data, and let them run */
/*    j2psignal(itop(cb->user),1); */
/* Quick-fix for sending wait problem - Dave Perry, VE3IFB */
    j2psignal(itop(cb->user),0);
    pwait(NULL);
}
/* NET/ROM state change upcall routine */
static void
s_nscall(cb,old,new)
struct nr4cb *cb;
int old,new;
{
    int s,ns;
    struct usock *up,*nup,*oup,*next;
    union sp sp;
  
    s = cb->user;
    oup = up = itop(s);
  
    if(new == NR4STDISC && up != NULLUSOCK){
        /* Clean up. If the user has already closed the socket,
         * then up will be null (s was set to -1 by the close routine).
         * If not, then this is an abnormal close (e.g., a reset)
         * and clearing out the pointer in the socket structure will
         * prevent any further operations on what will be a freed
         * control block. Also wake up anybody waiting on events
         * related to this cb so they will notice it disappearing.
         */
        up->cb.nr4 = NULLNR4CB;
        up->errcodes[0] = cb->dreason;
    }
    if(new == NR4STCON && old == NR4STDISC){
        /* Handle an incoming connection. If this is a server cb,
         * then we're being handed a "clone" cb and we need to
         * create a new socket structure for it. In either case,
         * find out who we're talking to and wake up the guy waiting
         * for the connection.
         */
        if(cb->clone){
            /* Clone the socket */
            ns = j2socket(AF_NETROM,SOCK_SEQPACKET,0);
            nup = itop(ns);
            next = nup->next;
            ASSIGN(*nup,*up);
            nup->next = next;
            nup->number = ns;
            cb->user = ns;
            nup->cb.nr4 = cb;
            cb->clone = 0; /* to avoid getting here again */
            /* Allocate new memory for the name areas */
            nup->name = mallocw(sizeof(struct sockaddr_nr));
            nup->peername = mallocw(sizeof(struct sockaddr_nr));
            /* Store the new socket # in the old one */
            up->rdysock = ns;
            up = nup;
            s = ns;
        } else {
            /* Allocate space for the peer's name */
            up->peername = mallocw(sizeof(struct sockaddr_nr));
            /* Store the old socket # in the old socket */
            up->rdysock = s;
        }
        /* Load the addresses. Memory for the name has already
         * been allocated, either above or in the original bind.
         */
        sp.p = up->name;
        sp.nr->nr_family = AF_NETROM;
        ASSIGN(sp.nr->nr_addr,up->cb.nr4->local);
        up->namelen = sizeof(struct sockaddr_nr);
  
        sp.p = up->peername;
        sp.nr->nr_family = AF_NETROM;
        ASSIGN(sp.nr->nr_addr,up->cb.nr4->remote);
        up->peernamelen = sizeof(struct sockaddr_nr);
  
        /* Wake up the guy accepting it, and let him run */
        j2psignal(oup,1);
        pwait(NULL);
    }
    /* Ignore all other state transitions */
    j2psignal(up,0);  /* In case anybody's waiting */
}
#endif
  
/* Verify address family and length according to the socket type */
static int
checkaddr(type,name,namelen)
int type;
char *name;
int namelen;
{
    union sp sp;
  
    sp.p = name;
    /* Verify length and address family according to protocol */
    switch(type){
        case TYPE_TCP:
        case TYPE_UDP:
            if(sp.in->sin_family != AF_INET
                || namelen != sizeof(struct sockaddr_in))
                return -1;
            break;
#ifdef  AX25
        case TYPE_AX25I:
            if(sp.ax->sax_family != AF_AX25
                || namelen != sizeof(struct sockaddr_ax))
                return -1;
            break;
#endif
#ifdef  NETROM
        case TYPE_NETROML3:
        case TYPE_NETROML4:
            if(sp.nr->nr_family != AF_NETROM
                || namelen != sizeof(struct sockaddr_nr))
                return -1;
            break;
#endif
    }
    return 0;
}
/* Issue an automatic bind of a local address */
static void
autobind(s,af)
int s,af;
{
    char buf[MAXSOCKSIZE];
    union sp sp;
  
    sp.p = buf;
    switch(af){
        case AF_INET:
            sp.in->sin_family = AF_INET;
            sp.in->sin_addr.s_addr = INADDR_ANY;
            sp.in->sin_port = Lport++;
            if(!Lport) Lport=1024;  /* wrap to 1024, not zero */
            j2bind(s,sp.p,sizeof(struct sockaddr_in));
            break;
#ifdef  AX25
        case AF_AX25:
            sp.ax->sax_family = AF_AX25;
            memset(sp.ax->ax25_addr,'\0',AXALEN);
#ifdef JNOS20_SOCKADDR_AX
		/* 30Aug2010, Maiko, New way to reference interface information */
			memset (sp.ax->filler, 0, ILEN);
			sp.ax->iface_index = -1;
#else
            memset(sp.ax->iface,'\0',ILEN);
#endif
            j2bind(s,sp.p,sizeof(struct sockaddr_ax));
            break;
#endif
#ifdef  NETROM
        case AF_NETROM:
            sp.nr->nr_family = AF_NETROM;
            memcpy(sp.nr->nr_addr.user,Mycall,AXALEN);
            memcpy(sp.nr->nr_addr.node,Mycall,AXALEN);
            j2bind(s,sp.p,sizeof(struct sockaddr_nr));
            break;
#endif
    }
}
  
/* this is called by tipmail only ! - WG7J */
#ifdef TIPSERVER
  
/* Return a pair of mutually connected sockets in sv[0] and sv[1] */
int
j2socketpair(af,type,protocol,sv)
int af;
int type;
int protocol;
int sv[];
{
    struct usock *up0, *up1;
    if(sv == NULLINT){
        errno = EFAULT;
        return -1;
    }
    if(af != AF_LOCAL){
        errno = EAFNOSUPPORT;
        return -1;
    }
    if(type != SOCK_STREAM && type != SOCK_DGRAM){
        errno = ESOCKTNOSUPPORT;
        return -1;
    }
    if((sv[0] = j2socket(af,type,protocol)) == -1)
        return -1;
    if((sv[1] = j2socket(af,type,protocol)) == -1){
        close_s(sv[0]);
        return -1;
    }
    up0 = itop(sv[0]);
    up1 = itop(sv[1]);
    up0->cb.local->peer = up1;
    up1->cb.local->peer = up0;
    return sv[1];
}
#endif
  
struct inet {
    struct inet *next;
    struct tcb *tcb;
    char *name;
    int stack;
#ifdef MAXTASKS
				/*
				 * 13May2014, Maiko, Keep track of number of running tasks, so
				 * that we can limit the number of tasks running simultaneously
				 * for any particular TCP listener. It makes sense to do this
				 * here within start_tcp() and i_upcall(), BUT problem is how
				 * do I decrement the count when a spawned process finishs and
				 * terminates ? We don't keep track of newproc() ids in here,
				 * and trying that might be a bit overcomplicated.
				 *
				 * So I think for now we have to put MAXTASKS in the separate
				 * server modules (like popserv, ftpserv, and so on). No, actually
				 * forget the count here, use a pointer to a function instead.
    int cnt;
				 *
				 * Why did I want to do this in the first place ? DOS attacks !
				 *
				 */
	int (*proccnt)(void);
#endif
    void (*task)(int,void *,void *);
};
#define NULLINET    (struct inet *)0
struct inet *Inet_list;
  
static void i_upcall(struct tcb *tcb,int oldstate,int newstate);
 
/*
 * Start a TCP server. Create TCB in listen state and post upcall for
 * when a connection comes in
 *
 * 19Apr2016, Maiko (VE4KLM), need a way to get flags to the i_upcall
 * so that I can setup telnet listener for systems that use CR only and
 * or do IAC manipulation, so convert the original start_tcp () function
 * to an internal name (new_start_tcp) and add a flag field. Then write
 * a stub of the original function name (start_tcp) to call this one :) 
 */
int new_start_tcp(port,name,task,stack,flags)
int16 port;
char *name;
void (*task)(int,void *,void *);
int stack;
int flags;
{
    struct inet *in;
    struct socket lsocket;
  
    in = (struct inet *)callocw(1,sizeof(struct inet));
    lsocket.address = INADDR_ANY;
    lsocket.port = port;
    in->tcb = open_tcp(&lsocket,NULLSOCK,TCP_SERVER,0,
        NULLVFP((struct tcb*,int)),NULLVFP((struct tcb*,int)),
        i_upcall,0,-1);
    if(in->tcb == NULLTCB){
        free(in);
        return -1;
    }
	/*
	 * 19Apr2016, Maiko (VE4KLM), in->tcb->user is not assigned until
	 * after a socket is created, so I can temporarily used it to pass
	 * any flags to the i_upcall() function, that should work nicely.
	 */
	in->tcb->user = flags;

	// log (-1, "new_start_tcp - port [%d] flags [%02X]", (int)port, flags);

#ifdef MAXTASKS
	{
		extern int proccnt_popserv();
	in->proccnt = proccnt_popserv;
	}
	/* in->cnt = 3; */
#endif
    in->stack = stack;
    in->task = task;
    in->name = j2strdup(name);
    in->next = Inet_list;
    Inet_list = in;
    return 0;
}

/*
 * 19Apr2016, Maiko (VE4KLM), new stub function to call the original one
 * which I have modified to allow me to pass flags. Read further below. Keep
 * in mind that start_tcp () is still called from the rest of JNOS, there is
 * only one instance where I will call the new_start_tcp (in tipmail.c) ...
 */
int start_tcp(port,name,task,stack)
int16 port;
char *name;
void (*task)(int,void *,void *);
int stack;
{
	return (new_start_tcp(port,name,task,stack,0));
}

/* State change upcall that takes incoming TCP connections */
static void
i_upcall(tcb,oldstate,newstate)
struct tcb *tcb;
int oldstate;
int newstate;
{
    struct inet *in;
    struct sockaddr_in sock;
    struct usock *up;
    int s;

	// log (-1, "old state [%d] new state [%d]", oldstate, newstate);

	/* 14May2014, Maiko, I expect newstate will be SYN_RECEIVED, yup ! */

    if(oldstate != TCP_LISTEN)
        return; /* "can't happen" */
    if(newstate == TCP_CLOSED){
        /* Called when server is shut down */
        del_tcp(tcb);
        return;
    }
    for(in = Inet_list;in != NULLINET;in = in->next)
        if(in->tcb->conn.local.port == tcb->conn.local.port)
            break;
    if(in == NULLINET)
        return; /* not in table - "can't happen" */

#ifdef	MAXTASKS
	if ((*in->proccnt)() >= 3)
	{
		log (-1, "max procs (3) for this listener");

		reset_tcp (tcb);

		/*
		 * 13May2014, Maiko (VE4KLM), It works !!! BUT we're left with a list
		 * of TCBs when you run 'tcp status', they're all in closed state, how
		 * come they don't disappear ? Not sure how to deal with this. I have
		 * tried reset_tcp, close_self, close_tcp, none of them are doing it.
		 *
		 * 14May2014, Suppose I could just return and NOT call reset_tcp, and
		 * do nothing but tie up the client - would that be a bad thing, well
		 * this is a real bugger too, now have tons of 'Close wait' entries,
	 	 * just can't seem to 'win' on this - maybe shelf the idea for now ?
		 */

		return;
	}
	/* in->cnt++; */
#endif

    /* Create a socket, hook it up with the TCB */
    s = j2socket(AF_INET,SOCK_STREAM,0);
    up = itop(s);
    sock.sin_family = AF_INET;
    sock.sin_addr.s_addr = tcb->conn.local.address;
    sock.sin_port = tcb->conn.local.port;
    j2bind(s,(char *)&sock,SOCKSIZE);
 
/* 22Apr2016, Maiko (VE4KLM), would be nice to know which local port is
 * being connected to in those cases where I have multiple TCP listeners
 * going for the same service (ie, telnet) and running on a variety of
 * different and non-standard ports. Then I can tell remote sysops if
 * they're using the wrong port type of thing - very handy to know.
 *
 * This is just debugging, I need to somehow get this nos_log (got it finally)
 *
	log (s, "coming in on port %d", tcb->conn.local.port);
 */
 
    sock.sin_addr.s_addr = tcb->conn.remote.address;
    sock.sin_port = tcb->conn.remote.port;
    up->peernamelen = SOCKSIZE;
    up->peername = mallocw((unsigned)up->peernamelen);
    memcpy(up->peername,(char *)&sock,SOCKSIZE);
    up->cb.tcb = tcb;
/*
	log (-1, "i_upcall - port [%d] user [%02X] flag [%02X]",
		(int)tcb->conn.local.port, tcb->user, up->flag);
*/

	/*
 	 * 19Apr2016, Maiko (VE4KLM), So ? did the flags arrive intact ? YUP
	 *
	 * This is my new way of getting options to the function/process which
	 * will service an incoming connect, does away with the need to do things
	 * like create a new mbx_coming_cr, not necessary anymore. All of this
	 * stuff is being done to allow me to provide more flexible options to
	 * sysops wishing to run a telnet listener on a different port, and be
	 * able to specify cronly and/or noiac for non obcm or linfbb systems.
	 */
	if (tcb->user)
	{
		// log (-1, "i_upcall (before newproc) flags [%x]", tcb->user);

#ifdef DONT_COMPILE	/* 13Jun2016, Maiko, Oops, wrong spot should be in tipmail.c */

	/* 12Jun2016, Maiko (VE4KLM), Default to original JNOS functionality */
#ifdef	JNOS_DEFAULT_NOIAC
#warning "default NO IAC processing in affect"
		up->flag |= SOCK_NO_IAC;
#endif

#endif
		if (tcb->user & 0x01)
			strcpy (up->eol, "\r");

		if (tcb->user & 0x02)
			up->flag |= SOCK_NO_IAC;

#ifdef	DONT_COMPILE /* 13Jun2016, Maiko, Not necessary, don't need 3rd bit */

		/*
		 * 12Jun2016, Maiko (VE4KLM), allow disabling of the flag (in case
		 * the sysop opts to preserve JNOS default behavior, which I think
		 * should be the case anyways). This should make all sides of the
		 * 'no iac by default vs use iac by default' camps happy ...
		 */
		if (tcb->user & 0x04)
			up->flag &= ~SOCK_NO_IAC;
#endif

		/* tcb->user can now be wiped, we don't need it anymore, good ! */
	}

    tcb->user = s;
  
    /* Set the normal upcalls */
    tcb->r_upcall = s_trcall;
    tcb->t_upcall = s_ttcall;
    tcb->s_upcall = s_tscall;
  
    /* And spawn the server task */
    newproc(in->name,in->stack,in->task,s,NULL,NULL,0);
}
  
/* Close down a TCP server created earlier by inet_start */
int
stop_tcp(port)
int16 port;
{
    struct inet *in,*inprev;
  
    inprev = NULLINET;
    for(in = Inet_list;in != NULLINET;inprev=in,in = in->next)
        if(in->tcb->conn.local.port == port)
            break;
    if(in == NULLINET)
        return -1;
    close_tcp(in->tcb);
    free(in->name);
    if(inprev != NULLINET)
        inprev->next = in->next;
    else
        Inet_list = in->next;
    free(in);
    return 0;
}
  
static struct usock *Usock;        /* Socket entry array */

#ifdef	DONT_COMPILE	/* 07May2014, Maiko, Not needed anymore */

/* 12Apr2014, Maiko (VE4KLM), New function required for tcp watch routines */
int gettcbsocket (struct tcb *tcbp)
{
	struct usock *up;

    if (!tcpval(tcbp))	/* better make darn sure of this */
		return -1;

	for (up = Usock; up; up = up->next)
        if (up->cb.tcb == tcbp)
			return up->number;

	return -1;
}

#endif

struct usock *addsocket(void) {
    struct usock *new,*up;
  
    /* See if we can allocate the socket */
    if( (new = (struct usock *) calloc(1,sizeof(struct usock))) == NULLUSOCK)
        return NULLUSOCK;
  
    /* Now add it to the end, to keep the list in increasing order ! */
    if(Usock == NULLUSOCK) {   /* the first one ! */
        new->number = SOCKBASE;
        Usock = new;
    } else {
        /* find the last socket */
        for(up=Usock;up->next;up=up->next);
        new->number = up->number + 1;
        up->next = new;
    }
    return new;
}
  
void deletesocket(int s) {
    struct usock *up,*pp;
  
    /* Find the socket number to delete */
    for(pp=NULL,up=Usock;up;pp=up,up=up->next)
        if(up->number == s)
            break;
    if(!up)
        /* Not found, what happened ??? */
        return;
  
    /* Link it out of the list */
    if(!pp) /* First one on list */
        Usock = Usock->next;
    else
        pp->next = up->next;
  
     /* Free the memory */
    free(up);
}
  
/* Convert a socket index to an internal user socket structure pointer */
struct usock *itop(int s)
{
    struct usock *up;
  
    for(up=Usock;up;up=up->next)
	{
		/* 18Oct2009, Maiko, I'm seeing this happen - why ??? */
		if (up->next == up)
		{
			log (s, "serious problem, detected itop() loop !!!");
    		up = NULLUSOCK;
			break;
		}

        if(up->number == s)
            break;
	}
    return up;
}
  
/* Find the next socket in the list */
int getnextsocket(int s) {
    struct usock *up;
  
    for(up=Usock;up;up=up->next)
        if(up->number > s)
            break;
    if(up)
        return up->number;
    return -1;
}
  
  
