/* AX25 mailbox interface
 * Copyright 1991 Phil Karn, KA9Q
 *
 *  May '91 Bill Simpson
 *      move to separate file for compilation & linking
 */
#include "global.h"
#ifdef AX25SERVER
#include "proc.h"
#include "iface.h"
#include "pktdrvr.h"
#include "ax25.h"
#include "usock.h"
#include "socket.h"
#include "session.h"
#include "mailbox.h"
#include "telnet.h"
#include "ax25mail.h"
  
/* Axi_sock is kept in Socket.c, so that this module won't be called */
extern void conv_incom(int,void *,void *);
  
int
ax25start(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int s,whatcall;
    struct usock *up;
 	long whatcall_l;	/* 01Oct2009, Maiko, 64 bit warnings */ 

    if (Axi_sock != -1)
        return 0;
  
    j2psignal(Curproc,0); /* Don't keep the parser waiting */
    chname(Curproc,"AX25 listener");
    Axi_sock = j2socket(AF_AX25,SOCK_STREAM,0);
    /* bind() is done automatically */
    if(j2listen(Axi_sock,1) == -1){
        close_s(Axi_sock);
        return -1;
    }
    for(;;){
        if((s = j2accept(Axi_sock,NULLCHAR,NULLINT)) == -1)
            break;  /* Service is shutting down */
        /* Spawn a server */
        up = itop(s);
        whatcall = up->cb.ax25->jumpstarted;
        /* Check to see if jumpstart was actually used for
         * this connection.
         * If not, then again eat the line triggering this all
         */
        if(!(whatcall&JUMPSTARTED)) {
            sockmode(s,SOCK_ASCII);    /* To make recvline work */
            recvline(s,NULLCHAR,80);
        }
        /* Now find the right process to start - WG7J */
#ifdef CONVERS
        if(whatcall & CONF_LINK) {
            if(newproc("CONVERS Server",1048,conv_incom,s,(void *)AX25TNC,NULL,0) == NULLPROC)
                close_s(s);
            continue;
        }
#endif
  
#ifdef MAILBOX
  
#ifdef TTYCALL
        if(whatcall & TTY_LINK) {
            if(newproc("TTYLINK Server",2048,ttylhandle,s,(void *)AX25TNC,NULL,0) == NULLPROC)
                close_s(s);
            continue;
        }
#endif

#ifdef TNCALL
        if(whatcall & TN_LINK) {
            if(newproc("tnlink",2048,tnlhandle,s,(void *)AX25TNC,NULL,0) == NULLPROC)
                close_s(s);
            continue;
        }
#endif
		whatcall_l = (long)whatcall;	/* 01Oct2009, Maiko, 64 bit warning */

        if (newproc ("MBOX Server", 2048, mbx_incom, s, (void*)whatcall_l, NULL, 0) == NULLPROC)
            close_s (s);
  
#else /* no MAILBOX ! */
  
        /* Anything now goes to ttylink */
        if(newproc("TTYLINK Server",2048,ttylhandle,s,(void *)AX25TNC,NULL,0) == NULLPROC)
            close_s(s);
  
#endif /* MAILBOX */
  
    }
    close_s(Axi_sock);
    Axi_sock = -1;
    return 0;
}
  
int
ax250(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    close_s(Axi_sock);
    Axi_sock = -1;
    return 0;
}
  
#endif /* AX25SERVER */
