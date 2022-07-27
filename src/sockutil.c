/* Low level socket routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "iface.h"
#include "mbuf.h"
#include "netuser.h"
#include "socket.h"
#include "usock.h"
#include "lapb.h"
#include "tcp.h"
#include "nr4.h"
#include "config.h"
  
/* Convert a socket (address + port) to an ascii string of the form
 * aaa.aaa.aaa.aaa:ppppp
 */
char *
psocket(p)
void *p;    /* Pointer to structure to decode */
{
#if defined(AX25) || defined(NETROM)
    char tmp[11];
#endif
    static char buf[30];
    union sp sp;
    struct socket socket;
  
    sp.p = p;
    switch(sp.sa->sa_family){
        case AF_FILE:
            sprintf(buf,"%.29s",sp.fp->filename);
            break;
        case AF_LOCAL:
            buf[0] = '\0';
            break;
        case AF_INET:
            socket.address = sp.in->sin_addr.s_addr;
            socket.port = sp.in->sin_port;
            strcpy(buf,pinet(&socket));
            break;
#ifdef  AX25
        case AF_AX25:
            pax25(tmp,sp.ax->ax25_addr);
#ifdef JNOS20_SOCKADDR_AX
	    	if (sp.ax->iface_index != -1)
			{
				struct iface *ifp = if_lookup2 (sp.ax->iface_index);

            	sprintf (buf, "%s on port %s", tmp, ifp->name);
			}
#else
            if(strlen(sp.ax->iface) != 0)
				sprintf(buf,"%s on port %s",tmp,sp.ax->iface);
#endif
            else strcpy(buf,tmp);
            break;
#endif
#ifdef  NETROM
        case AF_NETROM:
            pax25(tmp,sp.nr->nr_addr.user);
            sprintf(buf,"%s @ ",tmp);
            pax25(tmp,sp.nr->nr_addr.node);
            strcat(buf,tmp);
            break;
#endif
    }
    return buf;
}
  
/* Return ASCII string giving reason for connection closing */
char *
sockerr(s)
int s;  /* Socket index */
{
    register struct usock *up;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return Badsocket;
    }
    switch(up->type){
#ifdef  AX25
        case TYPE_AX25I:
            if(up->cb.ax25 != NULLAX25)
                return NULLCHAR;    /* nothing wrong */
            return Axreasons[(int)up->errcodes[0]];
#endif
#ifdef  NETROM
        case TYPE_NETROML4:
            if(up->cb.nr4 != NULLNR4CB)
                return NULLCHAR;    /* nothing wrong */
            return Nr4reasons[(int)up->errcodes[0]];
#endif
        case TYPE_TCP:
            if(up->cb.tcb != NULLTCB)
                return NULLCHAR;    /* nothing wrong */
            return Tcpreasons[(int)up->errcodes[0]];
        default:
            errno = EOPNOTSUPP; /* not yet, anyway */
            return NULLCHAR;
    }
}
/* Get state of protocol. Valid only for connection-oriented sockets. */
char *
sockstate(s)
int s;      /* Socket index */
{
    register struct usock *up;
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return NULLCHAR;
    }
    if(up->cb.p == NULLCHAR){
        errno = ENOTCONN;
        return NULLCHAR;
    }
    switch(up->type){
        case TYPE_TCP:
            return Tcpstates[(int)up->cb.tcb->state];
#ifdef  AX25
        case TYPE_AX25I:
            return Ax25states[up->cb.ax25->state];
#endif
#ifdef  NETROM
        case TYPE_NETROML4:
            return Nr4states[up->cb.nr4->state];
#endif
        default:
        /* Datagram sockets don't have state */
            errno = EOPNOTSUPP;
            return NULLCHAR;
    }
    /*NOTREACHED*/
}
  
#ifndef UNIX
  
void
st_garbage(red)
int red;
{
    struct usock *up;
    int s;
  
    s = 0;
    while((s=getnextsocket(s)) != -1) {
        if((up = itop(s)) == NULLUSOCK)
            continue;   /* Should never happen ! */
        if(up->type == TYPE_LOCAL_STREAM)
            mbuf_crunch(&up->cb.local->q);
    }
}
#endif /* UNIX */
  
