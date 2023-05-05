/* NETROM mailbox interface
 * Copyright 1991 Phil Karn, KA9Q
 *
 *  May '91 Bill Simpson
 *      move to separate file for compilation & linking
 */
#include <ctype.h>
#include "global.h"
#ifdef NETROMSERVER
#include "proc.h"
#include "netrom.h"
#include "socket.h"
#include "session.h"
#include "cmdparse.h"
#include "commands.h"
#include "mailbox.h"
#include "nr4mail.h"
#include "lapb.h"
#include "telnet.h"
  
static int Nrsocket = -1;
  
int
nr4start(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int s;
  
    if (Nrsocket != -1)
        return -1;
  
    j2psignal(Curproc,0); /* Don't keep the parser waiting */
    chname(Curproc,"NETROM listener");
    Nrsocket = j2socket(AF_NETROM,SOCK_SEQPACKET,0);
    /* bind() is done automatically */
    if (j2listen(Nrsocket,1) == -1) {
        close_s(Nrsocket);
        Nrsocket = -1;
        return -1;
    }
    for(;;){
        if((s = j2accept(Nrsocket,NULLCHAR,NULLINT)) == -1)
            break;  /* Service is shutting down */
#ifdef MAILBOX
        /* Spawn a server */
        if(newproc("mbox",2048,mbx_incom,s,(void *)NR4_LINK,NULL,0) == NULLPROC)
            close_s(s);
#else
        if(newproc("TTYLINK Server",2048,ttylhandle,s,(void *)NR4_LINK,NULL,0) == NULLPROC)
            close_s(s);
#endif
    }
    close_s(Nrsocket);
    Nrsocket = -1;
    return 0;
}
  
int
nr40(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    close_s(Nrsocket);
    Nrsocket = -1;
    return 0;
}
  
#endif /* NETROMSERVER */
  
