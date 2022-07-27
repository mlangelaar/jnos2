/* Socket status display code
 * Copyright 1991 Phil Karn, KA9Q
 */
#ifdef MSDOS
#include <dos.h>
#endif
#include "global.h"
#include "iface.h"
#include "mbuf.h"
#include "proc.h"
#include "lzw.h"
#include "usock.h"
#include "socket.h"
#include "ax25.h"
#include "netrom.h"
#include "tcp.h"
#include "udp.h"
#include "commands.h"
#include "config.h"

#include <unistd.h>	/* 12Mar2009, Maiko, sbrk() prototype */
  
/* Socket status display command */
int
dosock(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct usock *up;
    int s,i;
    struct sockaddr fsock;
    char *cp;
  
    if(argc < 2){
        char *procname;
#ifdef UNIX
        extern int _start;    /* Approximates the lowest addr we can access */

        j2tputs("S#  Type    PCB      Remote socket         Owner\n");
#else
        j2tputs("S#  Type    PCB  Remote socket         Owner\n");
#endif
        s = 0;
        while((s=getnextsocket(s)) != -1) {
            if((up=itop(s)) == NULLUSOCK)
                continue;
            i = sizeof(fsock);
            if(j2getpeername(s,(char *)&fsock,&i) == 0 && i != 0)
                cp = psocket(&fsock);
            else
                cp = "";
            procname = up->owner->name;
  
#ifdef UNIX
            if (procname < (char *)&_start  ||  procname+20 > (char *)sbrk(0))
                procname = "?";   /* we sometimes get a bogus ptr */

            tprintf("%3d %-8s%8.8lx %-22s%8.8lx %-10s\n",
#else
            tprintf("%3d %-8s%4.4x %-22s%4.4x %-10s\n",
#endif
            s,Socktypes[(int)up->type],FP_SEG(up->cb.p),cp,
            FP_SEG(up->owner),procname);
        }
        return 0;
    }
    s = atoi(argv[1]);
    up = itop(s);
    if(up == NULLUSOCK){
        j2tputs("Socket not in use\n");
        return 0;
    }
#ifdef UNIX
    tprintf("%s %8.8lx %s",Socktypes[(int)up->type],FP_SEG(up->cb.p),
#else
    tprintf("%s %4.4x %s",Socktypes[up->type],FP_SEG(up->cb.p),
#endif

	/*
	 * 14Apr2016, Maiko (VE4KLM), One will have to mask the 'flag' now
	 * before looking at the mode values, since I've had to add bits to
	 * the flag for the new IAC processing used in telnet forwarding.
	 */
    (up->flag & SOCK_FLAG_MASK) == SOCK_ASCII ? "ascii" : "binary");

    if(up->eol[0] != '\0'){
        j2tputs(" eol seq:");
        for(i=0;up->eol[i] != '\0' && i<(int)sizeof(up->eol);i++)
            tprintf(" %02x",up->eol[i]);
    }
    tprintf("  Refcnt: %d  Since: %s",up->refcnt,ctime(&up->created));
    if(up->cb.p == NULL)
        return 0;
    switch(up->type){
        case TYPE_WRITE_ONLY_FILE:
            tprintf("File: %s\n", up->name);
            break;
        case TYPE_RAW:
        case TYPE_LOCAL_DGRAM:
            tprintf("Inqlen: %d packets\n",socklen(s,0));
            tprintf("Outqlen: %d packets\n",socklen(s,1));
            break;
        case TYPE_LOCAL_STREAM:
            tprintf("Inqlen: %d bytes\n",socklen(s,0));
            tprintf("Outqlen: %d bytes\n",socklen(s,1));
            break;
        case TYPE_TCP:
            st_tcp(up->cb.tcb);
            break;
        case TYPE_UDP:
            st_udp(up->cb.udp,0);
            break;
#ifdef  AX25
        case TYPE_AX25I:
            st_ax25(up->cb.ax25);
            break;
#endif
#ifdef  NETROM
        case TYPE_NETROML4:
            donrdump(up->cb.nr4);
            break;
#endif
    }
#ifdef LZW
	/* 12Oct2009, Maiko, Use "%d" for int32 vars */
    if(up->zout != NULLLZW)
        tprintf("Compressed %d bytes.\n",up->zout->cnt);
    if(up->zin != NULLLZW)
        tprintf("Decompressed %d bytes.\n",up->zin->cnt);
#endif
    return 0;
}
  
/* Kick the session related to a particular socket
 * this is easier then the tcp kick, ax25 kick, etc... commands
 * 920117 - WG7J
 */
int
dokicksocket(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct usock *up;
    int s;  /*socket to kick*/
    int retval=0;
  
    s = atoi(argv[1]);
    up = itop(s);
    if(up == NULLUSOCK){
        j2tputs("Socket not in use\n");
        return 0;
    }
    if(up->type == TYPE_TCP)
        retval = kick_tcp(up->cb.tcb);
#ifdef AX25
    if(up->type == TYPE_AX25I)
        retval = kick_ax25(up->cb.ax25);
#endif
#ifdef NETROM
    if(up->type == TYPE_NETROML4)
        retval = kick_nr4(up->cb.nr4);
#endif
    if(retval == -1)
        j2tputs("Kick not successfull\n");
    return 0;
}
  
  
