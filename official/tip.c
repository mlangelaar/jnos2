/* "Dumb terminal" session command for serial lines
 * Copyright 1991 Phil Karn, KA9Q
 *
 *      Feb '91 Bill Simpson
 *              rlsd control and improved dialer
 */
/* mods by PA0GRI */
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#ifdef UNIX
#include "unixasy.h"
#else
#include "i8250.h"
#endif
#include "asy.h"
#include "tty.h"
#include "session.h"
#include "socket.h"
#include "commands.h"
#include "devparam.h"
  
  
static void tip_out __ARGS((int dev,void *n1,void *n2));
  
  
/* Execute user telnet command */
int
dotip(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct session *sp;
    register struct iface *ifp;
    struct asy *ap;
    char *ifn;
    int (*rawsave) __ARGS((struct iface *,struct mbuf *));
    int c;
  
    /*Make sure this comes from console - WG7J*/
    if(Curproc->input != Command->input)
        return 0;
  
    if((ifp = if_lookup(argv[1])) == NULLIF){
        tprintf(Badinterface,argv[1]);
        return 1;
    }
    ap=&Asy[ifp->dev];
    if( ifp->dev >= ASY_MAX || ap->iface != ifp ){
        tprintf("Interface %s not asy port\n",argv[1]);
        return 1;
    }
    if(ifp->raw == bitbucket){
        tprintf("tip or dialer session already active on %s\n",argv[1]);
        return 1;
    }
  
    /* Allocate a session descriptor */
    if((sp = newsession(argv[1],TIP,0)) == NULLSESSION){
        j2tputs(TooManySessions);
        return 1;
    }
  
    /* Save output handler and temporarily redirect output to null */
    rawsave = ifp->raw;
    ifp->raw = bitbucket;
  
    /* Suspend the packet input driver. Note that the transmit driver
     * is left running since we use it to send buffers to the line.
     */
    suspend(ifp->rxproc);
#ifdef POLLEDKISS
    suspend(ap->poller);
#endif
  
    /* Put tty into raw mode */
    sp->ttystate.echo = 0;
    sp->ttystate.edit = 0;
    sockmode(sp->output,SOCK_BINARY);
  
    /* Now fork into two paths, one rx, one tx */
    ifn = if_name( ifp, " tip out" );
    sp->proc1 = newproc(ifn,256,tip_out,ifp->dev,sp,NULL,0);
    free( ifn );
  
    ifn = if_name( ifp, " tip in" );
    chname( Curproc, ifn );
    free( ifn );
  
    /* bring the line up (just in case) */
    if ( ifp->ioctl != NULL )
        (*ifp->ioctl)( ifp, PARAM_UP, TRUE, 0L );
  
    while((c = get_asy(ifp->dev)) != -1)
        tputc(c & 0x7f);
    tflush();
  
    killproc(sp->proc1);
    sp->proc1 = NULLPROC;

    /* If the reason we got a read error was that the iface was detached,
     * then ifp is no longer valid.  We could have had if_detach() just kill
     * us, but we need to call freesession to finish the session.  -- n5knx
     */
    if(if_lookup(argv[1]) != NULLIF){  /* ifp still valid */
        ifp->raw = rawsave;
        resume(ifp->rxproc);
#ifdef POLLEDKISS
        resume(ap->poller);
#endif
    }
    keywait(NULLCHAR,1);
    freesession(sp);
    return 0;
}
  
  
/* Output process, DTE version */
static void
tip_out(dev,n1,n2)
int dev;
void *n1,*n2;
{
    struct mbuf *bp;
    struct iface *ifp; 
    struct session *sp = (struct session *)n1;
    int c;
  
    while((c = recvchar(Curproc->input)) != EOF)
	{
	log (-1, "inchar %d", c);

        if(c == '\n')
            c = '\r';               /* NL => CR */

        bp = pushdown(NULLBUF,1);

	if (c == '^')
        	bp->data[0] = 0xc0;	/* FEND */
	else
        	bp->data[0] = (c & 0x7f);

        asy_send(dev,bp);
        if ((ifp=Asy[dev].iface) != NULLIF)  /* was iface detached on us? */
            ifp->lastsent = secclock();
        else break;  /* exit to kill self */
    }
    sp->proc1 = NULLPROC;    /* prevent redundant killproc by dotip() */
    alert(sp->proc,EABORT);  /* abort "tip in" proc; -> auto reset after detach */
}
