/* "Dumb terminal" mailbox interface
 * Copyright 1991 Phil Karn, KA9Q
 *
 *      May '91 Bill Simpson
 *              move to separate file for compilation & linking
 *      Sep '91 Bill Simpson
 *              minor changes for DTR & RLSD
 *      Jan '93 Doug Crompton
 *              Mods to code to make it work with both terminal
 *              and Modem. Timers and CD check revamped. Now Always
 *              detects CD loss and timeouts work properly.
 *              Setting Tiptimeout to 0 disables idle timeout
 *      Feb '93 Added code to support Xmodem - RAW Serial DATA
 *      Mar '93 Changed asy_send to asy_sen_wait, an internal function
 *              To eliminate asyinc buffers from growing beyond control
 *              and to stop data from flowing to serial device if CD is lost
 *      Jun '94 James Dugal - added code to reset server if CD lost or idle
 *              timeout occurs.  Also mark closure of tip socket to avoid probs.
 *
 * Command Syntax now : 'start tip <interface> <modem|terminal> [timeout sec]'
 */
#include <ctype.h>
#include "global.h"
#if defined(TIPSERVER) || defined(TELNETSERVER)
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#ifdef UNIX
#include "unixasy.h"
#else
#include "i8250.h"
#endif
#include "asy.h"
#include "socket.h"
#include "usock.h"
#include "telnet.h"
#include "mailbox.h"
#include "tipmail.h"
#include "devparam.h"
#include "lapb.h"
  
#ifdef TIPSERVER
  
/*
 * 02Aug2020, Maiko (VE4KLM), this was previously defined in tipmail.h,
 * which is wrong ! gcc 10 rightfully complaining of multiple defines.
 */
static struct tipcb *Tiplist;

static void tip_in __ARGS((int dev,void *n1,void *n2));
static void tipidle __ARGS((void *t));
static int asy_send_wait __ARGS((int dev,int modem,struct mbuf *bp));
  
#define Tiptimeout 180;      /* Default tip inactivity timeout (seconds) */
  
/* Input process */
static void
tip_in(dev,n1,n2)
int dev;
void *n1,*n2;
{
    struct tipcb *tip;
    struct mbuf *bp;
    char *buf[2], line[MBXLINE];
    int c, ret, pos = 0;
  
    tip = (struct tipcb *) n1;
  
    while((c = get_asy(dev)) != -1){
        tip->firstwarn=1;
        tip->timeout=tip->default_timeout;
        Asy[dev].iface->lastrecv = secclock();
        if (!tip->raw) {
            bp = NULLBUF;
            c &= 0x7f;
            ret = 0;
            if(tip->echo == WONT){
                switch(c){
                    case 21:        /* CTRL-U */        /*DCB*/
                        if (pos){
                            bp = qdata("^U\r\n",4);
                            pos = 0;
                        }
                        ret = 1;
                        break;
                    case 18:        /* CTRL-R */
                        if(pos){    /* DCB */
                            bp = pushdown(qdata(line,pos),4);
                            memcpy(bp->data,"^R\r\n",4);
                        }
                        ret = 1;
                        break;
                    case 0x7f:      /* DEL */
                    case '\b':
                        bp = NULLBUF;
                        if(pos){
                            --pos;
                            bp = qdata("\b \b",3);
                        }
                        ret = 1;
                        break;
                    case '\r':
                        c = '\n';       /* CR => NL */
                    case '\n':
                        bp = qdata("\r\n",2);
                        break;
                    default:
                        bp = pushdown(NULLBUF,1);
                        *bp->data = c;
                        break;
                }
                asy_send_wait(dev,tip->chk_modem_cd,bp);
                tip->iface->lastsent = secclock();
                if(ret)
                    continue;
            }
            line[pos++] = c;
            if(pos == MBXLINE - 1 || tip->echo == WILL
            || c == '\n'){
                line[pos] = '\0';
                pos = 0;
                usputs(tip->s,line);
                usflush(tip->s);
            }
        } else {
            usputc(tip->s,c);
            usflush(tip->s);
        }
    }
    /* get_asy() failed, terminate */
    close_s(tip->s); tip->s = -1;
    tip->in = tip->proc;     /* tip0 must kill me last, not first! */
    tip->proc = Curproc;
    buf[1] = Asy[dev].iface->name;
    tip0(2,buf,NULL);
}
/* Start mailbox on serial line */
int
tipstart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct iface *ifp;
    register struct asy *ap;
    register struct tipcb *tip;
    struct mbuf *bp, *dp, *op; /* op is output, dp is duplicate */
#ifndef UNIX
    struct fifo *fp;
#endif
    char *buf[2];
    int dev, c, i, off, cnt, cmd, s[2];
 
	long type_l = (long)TIP_LINK;	/* 01Oct2009, Maiko, 64 bit warning */
 
    if((ifp = if_lookup(argv[1])) == NULLIF){
        tprintf(Badinterface,argv[1]);
        return 1;
    }
    for(dev=0,ap = Asy;dev < ASY_MAX;dev++,ap++)
        if(ap->iface == ifp)
            break;
    if(dev == ASY_MAX){
        tprintf("Interface %s not asy port\n",argv[1]);
        return 1;
    }
    if(ifp->raw == bitbucket){
        tprintf("Tip session already active on %s\n",argv[1]);
        return 1;
    }
    j2psignal(Curproc,0);     /* Don't keep the parser waiting */
    chname(Curproc,"Mbox tip");
    tip = (struct tipcb *) callocw(1,sizeof(struct tipcb));
  
    tprintf("Tip started on %s - ",argv[1]);
    if (argc>2 && tolower(argv[2][0]) == 'm') {
        tip->chk_modem_cd=1;
        j2tputs(" with");
    } else {
        tip->chk_modem_cd=0;
        j2tputs(" without");
    }
    j2tputs(" CD check - ");
    if (argc>3)
        tip->default_timeout=atoi(argv[3]);
    else
        tip->default_timeout=Tiptimeout;
  
    if (tip->default_timeout)
        tprintf("%d Second",tip->default_timeout);
    else
        j2tputs("No");
    j2tputs(" Timeout\n");
  
    /* Save output handler and temporarily redirect output to null */
    tip->asy_dev=dev;
    tip->rawsave = ifp->raw;
    ifp->raw = bitbucket;
    tip->iface = ifp;
    tip->proc = Curproc;
    tip->timer.func = tipidle;
    tip->timer.arg = (void *) tip;
    tip->raw=0;
    tip->next = Tiplist;
    Tiplist = tip;
    buf[1] = ifp->name;
  
    /* Suspend packet input drivers */
    suspend(ifp->rxproc);
  
    for(;;) {
        ifp->ioctl(ifp,PARAM_UP,TRUE,0);
  
        /* Wait for DCD to be asserted if modem*/
        if (tip->chk_modem_cd) {
            j2pause(1000);
            while (! carrier_detect(tip->asy_dev))
#ifdef UNIX
                j2pause(3000);  /* WAS a tighter loop: pwait(NULL);*/
#else
                pwait(NULL);
#endif
            j2pause(1000);
        }
  
        if(j2socketpair(AF_LOCAL,SOCK_STREAM,0,s) == -1){
            tprintf("Could not create socket pair, errno %d\n",errno);
            tip0(2,buf,p);
            return 1;
        }
        seteol(s[0],"\n");
        seteol(s[1],"\n");
        tip->echo = WONT;
        tip->s = s[0];
        if (tip->chk_modem_cd) {
            log(tip->s,"Telephone MBOX Login");
        }
#ifdef MAILBOX
        newproc ("MBOX Tip Server", 2048, mbx_incom,
			s[1], (void *)type_l, (void *)tip, 0);
#else
        newproc ("TTYLINK Tip", 2048, ttylink_tcp, s[1], NULL, NULL, 0);
#endif
        /* check for line idle timeout and CD failure */
        tip->firstwarn=1;
        tip->timeout=tip->default_timeout;
        set_timer(&tip->timer,1000);
        start_timer(&tip->timer);
  
        j2setflush(tip->s,-1);
        sockmode(tip->s,SOCK_ASCII);
  
        /* Now fork into two paths, one rx, one tx */
#ifndef UNIX
        /* first clear (ignore) junk in asyinc input
           which is always receiving - modem can garbage
           on disconnect
        */
        fp = &ap->fifo;
        fp->wp = fp->rp = fp->buf;
        fp->cnt = 0;
#endif
  
        tip->in = newproc("Mbox tip in",256,tip_in,dev,(void *)tip,NULL,0);
        while((cnt = recv_mbuf(tip->s,&bp,0,NULL,0)) != -1) {
            if(!tip->raw) {
                dup_p(&dp,bp,off=0,cnt); /* dup the whole pkt to pull */
                for (i=0; i<cnt; i++)
                switch(PULLCHAR(&dp)) {
                    case IAC:      /* ignore most telnet options */
                        dup_p(&op,bp,off,i-off);
                        asy_send_wait(dev,tip->chk_modem_cd,op);
                        ifp->lastsent = secclock();
  
                        if((cmd = ++i < cnt ?  PULLCHAR(&dp) :
                            recvchar(tip->s)) == -1)
                            break;
                        if(cmd > 250 && cmd < 255) {
                            if((c = ++i < cnt ? PULLCHAR(&dp) :
                                recvchar(tip->s)) == -1)
                                break;
                            switch(cmd){
                                case WILL:
                                    if(c == TN_ECHO) {
                                        tip->echo = cmd;
                                        cmd = DO;
                                    }
                                    else
                                        cmd = DONT;
                                    break;
                                case WONT:
                                    if(c == TN_ECHO)
                                        tip->echo = cmd;
                                    cmd = DONT;
                                    break;
                                case DO:
                                case DONT:
                                    cmd = WONT;
                                    break;
                            }
/*                      usprintf(tip->s,"%c%c%c",IAC,cmd,c);    */
                            usputc(tip->s,IAC);
                            usputc(tip->s,cmd);
                            usputc(tip->s,c);
                            usflush(tip->s);
                        }
                        off = i + 1;
                        break;
                    case '\r':
                        if ( ++i < cnt &&  /* Skip NL but not IAC */
                        (PULLCHAR(&dp) == IAC)) {
                            dp = pushdown(dp,1);
                            *dp->data = IAC;
                        }
                        break;
                    case '\n':
                        dup_p(&op,bp,off,i-off);
                        append(&op,qdata("\r\n",2));
                        asy_send_wait(dev,tip->chk_modem_cd,op);
                        ifp->lastsent = secclock();
                        off = i + 1;
                        break;
                }
  
                (void)pullup(&bp,NULLCHAR,off);
                asy_send_wait(dev,tip->chk_modem_cd,bp);
            } else {
                asy_send_wait(dev,tip->chk_modem_cd,bp);
            }
            ifp->lastsent = secclock();
            tip->firstwarn=1;
            tip->timeout=tip->default_timeout;
            if (pwait(NULL)) break;       /* alert() provides errno */
        }

        stop_timer(&tip->timer);
        j2pause(2000);
        close_s(tip->s); tip->s = -1;
        killproc(tip->in);
        tip->in=NULLPROC;
  
        pwait(itop(s[1])); /* let mailbox terminate, if necessary */
  
        /* Tell line to go down if modem */
        if (tip->chk_modem_cd) {
            ifp->ioctl(ifp,PARAM_DOWN,TRUE,0);
            j2pause(5000);
        }
  
    }
}
  
/* Send a message on the specified serial line
   Wait for queue to empty - for slow serial
   lines where data flow control is desired
   Eliminates large queue - I.E. memory hogging
   Stops data from flowing if CD is lost       */
  
static int
asy_send_wait(dev,modem,bp)
int dev;
int modem;
struct mbuf *bp;
  
{
    if (carrier_detect(dev) || !modem) {
        asy_send(dev,bp);
        while (len_p(Asy[dev].sndq)>1
        && (carrier_detect(dev) || !modem)){
#ifdef UNIX
            j2pause(100);  /* WAS a tighter loop: pwait(NULL);*/
#else
            pwait(NULL);
#endif
        }
  
    } else {
        free_p(bp);
    }
    return 0;
}
  
int
tip0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct iface *ifp;
    struct tipcb *tip, *prev = NULLTIP;
    struct proc *proc;
  
    if((ifp = if_lookup(argv[1])) == NULLIF){
        tprintf(Badinterface,argv[1]);
        return 1;
    }
    for(tip = Tiplist; tip != NULLTIP; prev = tip, tip = tip->next)
        if(tip->iface == ifp) {
            if(prev != NULLTIP)
                prev->next = tip->next;
            else
                Tiplist = tip->next;
            proc = tip->proc;
            close_s(tip->s); tip->s = -1;
            ifp->raw = tip->rawsave;
            resume(ifp->rxproc);
            stop_timer(&tip->timer);
            killproc(tip->in);
            tip->in = NULLPROC;
            free((char *)tip);
            killproc(proc);
            return 0;
        }
    return 0;
}
  
static void
tipidle(t)
void *t;
{
    struct tipcb *tip;
    static char *msg1 = "Line idle - One minute until disconnect...\007\r\n";
    static char *msg2 = "Disconnecting...\007\r\n";
    tip = (struct tipcb *) t;
  
    if (! carrier_detect(tip->asy_dev) && tip->chk_modem_cd) {
        close_s(tip->s); tip->s = -1;
        alert(tip->proc, ENOTCONN);   /* tell tipstart() about hangup */
        return;
    }
  
    if (--tip->timeout<=0 && tip->default_timeout!=0) {
        if (tip->firstwarn) {
            tip->iface->lastsent = secclock();
            asy_send_wait(tip->iface->dev,tip->chk_modem_cd,
            qdata(msg1,strlen(msg1)));
            tip->timeout=60;
            start_timer(&tip->timer);
            tip->firstwarn=0;
        } else {
            asy_send_wait(tip->iface->dev,tip->chk_modem_cd,
            qdata(msg2,strlen(msg2)));
            close_s(tip->s); tip->s = -1;
            alert(tip->proc, EALARM);   /* tell tipstart() about timeout */
        }
    } else {
        start_timer(&tip->timer);
    }
}
  
#endif /* TIPSERVER */
  
#ifdef TELNETSERVER
  
/* Start up Telnet server */
int telnet1 (int argc, char **argv, void *p)
{
    int16 port;

    int narg, flags = 0; /* 19Apr2016, Maiko (VE4KLM), new flags var */
				/* 13Jun2016, Maiko, should be INT */
    if (argc < 2)
        port = IPPORT_TELNET;
    else
        port = atoi (argv[1]);

#ifdef	MAILBOX		/* 23Apr2016 */

	/*
	 * 13Jun2016, Maiko (VE4KLM), Set the default here, not in socket.c !!!
	 * and now I think I know why bleeding2 failed miserably, because I had
	 * this setup in the wrong spot ...
	 */
#ifdef	JNOS_DEFAULT_NOIAC
	flags |= 0x02;
#endif

    if (argc > 2)
	{
		/*
		 * 19Apr2016, Maiko (VE4KLM), bit of a revamp, a whole new way to
 		 * pass options to a socket which gets created on incoming telnet.
		 */
		for (narg = 2; narg < argc; narg++)
		{
			/* just CR */
			if (!stricmp(argv[narg],"cronly"))
				flags |= 0x01;

			/* 19Apr2016, Maiko (VE4KLM), the new SOCK_ENABLE_IAC flag */
         	if (!stricmp(argv[narg],"noiac"))
				flags |= 0x02;

			/*
			 * 12Jun2016, Maiko (VE4KLM), allow disabling of the flag (in case
			 * the sysop opts to preserve JNOS default behavior, which I think
			 * should be the case anyways). This should make all sides of the
			 * 'no iac by default vs use iac by default' camps happy ...
			 *
			 * 13Jun2016, Maiko, stick with the 2nd bit, don't use a 3rd one.
			 */
			if (!stricmp(argv[narg],"iac"))
				flags &= ~0x02;
		}
	}

	/*
	 * 19Apr2016, Maiko (VE4KLM), bit of a revamp, a whole new way to
	 * pass options to the socket that gets created when an incoming
	 * connect comes in. Get's a bit technical, but I had to write a
	 * function (stub of sorts) to allow me to get flags to i_upcall()
	 * which creates the socket and spawns a new process. This is more
	 * elegant, and completely replaces the previous way of doing it.
	 */
    return new_start_tcp (port, "MBOX Server", mbx_incom, 8192, flags);
#else
	return start_tcp (port, "TTYLINK Server", ttylink_tcp, 2048);
#endif
}
  
/* Stop telnet server */
int telnet0 (int argc, char **argv, void *p)
{
    int16 port;
  
    if(argc < 2)
        port = IPPORT_TELNET;
    else
        port = atoi(argv[1]);
    return stop_tcp(port);
}
  
#endif  /* TELNETSERVER */
  
#endif /* TIP | TELNET */
