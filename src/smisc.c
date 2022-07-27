/* Miscellaneous Internet servers:
 * discard, echo and remote, Copyright 1991 Phil Karn, KA9Q
 * Rsysopserver, by KF5MG.
 * Traceserver, by WG7J.
 * Identserver, by K2MF.
 */
#include "global.h"
#include "netuser.h"
#include "mbuf.h"
#include "socket.h"
#include "proc.h"
#include "remote.h"
#include "smtp.h"
#include "iface.h"
#include "tcp.h"
#include "nr4.h"
#include "commands.h"
#include "unix.h"
#include "mailbox.h"
#include "cmdparse.h"
#include "usock.h"

#ifdef TRACESERVER
static void traceserv __ARGS((int s,void *unused,void *p));
extern struct cmds DFAR Cmds[];

/* Start up TCP trace server */
int
trace1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

    if(argc < 2)
        port = IPPORT_TRACE;
    else
        port = atoi(argv[1]);
    return start_tcp(port,"Trace Server",traceserv,576);
}

/* Stop trace server */
int
trace0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

    if(argc < 2)
        port = IPPORT_TRACE;
    else
        port = atoi(argv[1]);
    return stop_tcp(port);
}

static void
traceserv(s,unused,p)
int s;
void *unused;
void *p;
{
#define CMDLINE 80    
    char *cmd;
    
    sockmode(s,SOCK_ASCII);
    sockowner(s,Curproc);
    log(s,"open trace");
    
    close_s(Curproc->output);
    close_s(Curproc->input);  /* so dotrace() won't use stdoutSock */
    Curproc->output = Curproc->input = s;
    j2setflush(s,'\n');

    /* allocate the command buffer */
    cmd = mallocw(CMDLINE+1);
    
    while(recvline(s,cmd,CMDLINE) > 0) {
        if(!strnicmp("trace",cmd, 5))    /* only allow 'trace ...' command ! */
            cmdparse(Cmds,cmd,NULL);
        else usputs(s,"Only trace cmd allowed\n");
    }
    free(cmd);
    removetrace();
    log(s,"close trace");
    Curproc->input = -1;   /* avoid closing sock s twice */
/*    close_s(s);  n5knx:  already done during process destruction */
}
#endif /* TRACESERVER */

#ifdef DISCARDSERVER
static void discserv __ARGS((int s,void *unused,void *p));

/* Start up TCP discard server */
int
dis1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

    if(argc < 2)
        port = IPPORT_DISCARD;
    else
        port = atoi(argv[1]);
    return start_tcp(port,"Discard Server",discserv,576);
}

/* Stop discard server */
int
dis0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

    if(argc < 2)
        port = IPPORT_DISCARD;
    else
        port = atoi(argv[1]);
    return stop_tcp(port);
}

static void
discserv(s,unused,p)
int s;
void *unused;
void *p;
{
    struct mbuf *bp;

    sockowner(s,Curproc);
    log(s,"open discard");
    while(recv_mbuf(s,&bp,0,NULLCHAR,NULL) > 0)
        free_p(bp);
    log(s,"close discard");
    close_s(s);
}
#endif /* DISCARDSERVER */

#ifdef ECHOSERVER
static void echoserv __ARGS((int s,void *unused,void *p));

/* Start up TCP echo server */
int
echo1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

    if(argc < 2)
        port = IPPORT_ECHO;
    else
        port = atoi(argv[1]);
    return start_tcp(port,"Echo Server",echoserv,2048);
}

/* stop echo server */
int
echo0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

    if(argc < 2)
        port = IPPORT_ECHO;
    else
        port = atoi(argv[1]);
    return stop_tcp(port);
}

static void
echoserv(s,unused,p)
int s;
void *unused;
void *p;
{
    struct mbuf *bp;

    sockowner(s,Curproc);
    log(s,"open echo");
    while(recv_mbuf(s,&bp,0,NULLCHAR,NULL) > 0)
        send_mbuf(s,bp,0,NULLCHAR,0);

    log(s,"close echo");
    close_s(s);
}
#endif /* ECHOSERVER */

#ifdef REMOTESERVER
char *Rempass = NULLCHAR; /* Remote access password */
#if defined(UDP_DYNIPROUTE) && defined(ENCAP)
char *Gatepass = NULLCHAR; /* remote encap-gateway password */
#ifndef DYNIPROUTE_TTL
#define DYNIPROUTE_TTL 900
#endif
#endif /* ENCAP && UDP_DYNIPROUTE */

extern void where_outta_here(int resetme, int retcode);
static int chkrpass __ARGS((struct mbuf **bp, char *));

static int Rem = -1;
 
/* Start remote UDP exit/reboot server */
/* Modified by K2MF to listen for route add/drop requests from dynamic-IP-addressed
 * gateways that know the password, and manage a private encapped route to that gateway.
 */
int
rem1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
#if defined(UDP_DYNIPROUTE) && defined(ENCAP)
    struct route *rp;
    char *cp, *buf;
    int32 target;
    int tlen,fail,logroute;
    int16 bits=0;
#endif
    struct sockaddr_in lsocket,fsock;
    int i;
    int command;
    struct mbuf *bp;
    int32 addr;
    char temp[20];

	long addr_l; /* 01Oct2009, Maiko, Bridging var for 64 bit warnings */

#if defined(UDP_DYNIPROUTE) && defined(ENCAP)
	/* 14Oct2004, Maiko, IPUDP Support */
	extern int GLOB_encap_protocol;
#endif

    if(Rem != -1){
        return 0;
    }
    j2psignal(Curproc,0);
    chname(Curproc,"Remote listener");
    lsocket.sin_family = AF_INET;
    lsocket.sin_addr.s_addr = INADDR_ANY;
    if(argc < 2)
        lsocket.sin_port = IPPORT_REMOTE;
    else
        lsocket.sin_port = atoi(argv[1]);

    if ((Rem = j2socket(AF_INET,SOCK_DGRAM,0)) == -1) {
        j2tputs(Nosock);
        return -1;
    }
    j2bind(Rem,(char *)&lsocket,sizeof(lsocket));
    for(;;){
        i = sizeof(fsock);
        if(recv_mbuf(Rem,&bp,0,(char *)&fsock,&i) == -1)
            break;
        command = PULLCHAR(&bp);

        switch(command){
            case SYS_RESET:
            case SYS_EXIT:
                i = chkrpass(&bp,Rempass);
                log(Rem,"Remote %s %s %s", (command==SYS_EXIT ? "exit" : "reset"),
                    psocket((struct sockaddr *)&fsock),
                    i == 0 ? "PASSWORD FAIL" : "" );
                if(i != 0){
                    where_outta_here((command==SYS_EXIT?0:1),100);
                }
                break;

            case KICK_ME:
                if(len_p(bp) >= sizeof(int32))
                    addr = pull32(&bp);
                else
                    addr = fsock.sin_addr.s_addr;

                strcpy(temp, inet_ntoa(addr));  /* avoid wipeout of psocket's static temp */

                log(Rem,"Remote kick by %s for host %s",
                    psocket((struct sockaddr *)&fsock), temp);
#ifdef NETROM
                donodetick();       /* g3rra's idea!..hmmm */
#endif /* NETROM */
                kick(addr);
				addr_l = (long)addr;	/* 01Oct2009, Maiko, 64 bit warning */
                smtptick ((void*)addr_l);
                break;

#if defined(UDP_DYNIPROUTE) && defined(ENCAP)
            case ROUTE_ME:
            case UNROUTE_ME:
			/* 14Oct2004, Maiko, IPUDP support */
            case UDPROUTE_ME:
                /* Encap route requested add/drop - K2MF */
                logroute = 1;
                tlen = pullchar(&bp);
                /* Extract the target addr string */
                buf = callocw(1,(tlen + 1));
                pullup(&bp,buf,tlen);

                fail = (tlen>18 || (i = chkrpass(&bp,Gatepass)) == 0);

                if(!fail) {
                    /* If IP address is followed by an optional slash and a
                     * length field (e.g. 44.96/16), get it, otherwise assume
                     * a full 32-bit address.
                     */
                    if((cp = strchr(buf,'/')) != NULLCHAR) {
                        *cp++ = '\0';
                        if((bits = (int16)atoi(cp)) < 16)
                            bits = 16;
                    } else
                        bits = 32;

                    /* target addr must exist, not be loopback addr, and if
                     * a route already exists for it, must be an encap route
                     * with a timer running, or be routed to Loopback=BitBucket)
                     */
                    if(!(target = resolve(buf)) || target == Loopback.addr)
                        fail = 1;
                    else if ((rp = rt_lookup(target)) != NULLROUTE) {
                        if (rp->iface != &Encap || !dur_timer(&rp->timer)
                            || !run_timer(&rp->timer))  fail = 1;
/*
                        else if(command == ROUTE_ME
                            && rp->gateway == fsock.sin_addr.s_addr)
*/
						/* 14Oct2004, Maiko, IPUDP routing support */
                        else if (rp->gateway == (int32)fsock.sin_addr.s_addr)
						{
							switch (command)
							{
								case ROUTE_ME:
								case UDPROUTE_ME:
									/* Not a new route so don't log it */
									logroute = 0;
									break;
							}
						}
                    }
                    if(!fail)
					{
						GLOB_encap_protocol = 0;	/* important */

						/* 14Oct2004, Maiko, IPUDP Support */
                       	if (command == ROUTE_ME || command == UDPROUTE_ME)
						{
                       		if (command == UDPROUTE_ME)
								GLOB_encap_protocol = UDP_PTCL;

                            /* Add route via Encap interface,metric 1,private*/
                            if (rt_add (target, bits, fsock.sin_addr.s_addr,
									&Encap, 1, DYNIPROUTE_TTL, 1) == NULLROUTE)
							{
                                fail = 1;
							}
                        }
						else
						{
                            /* Drop this route by adding it to the Loopback (bit
                             * bucket) interface.  This will keep a 32-bit IP
                             * address or smaller block isolated from being part
                             * of a larger block that is already routed in the
                             * machine which would prevent a temporary encap
                             * route from being added in the future. - K2MF */
                            if (rt_add (target, bits, 0, &Loopback,
									1, 0, 1) == NULLROUTE)
							{
                                fail = 1;
							}
                        }

						GLOB_encap_protocol = 0;	/* reset */
                    }
                }
                if (fail || logroute)
				{
                    log (Rem, "Remote route %s by %s for %s/%d%s%s",
                        (command == ROUTE_ME) ? "ADD" : "DROP",
                        	psocket((void *)&fsock),buf,bits,
                        		(i ? "" : " PASSWD"),(fail ? " FAILED" : ""));
				}
                free(buf);
                break;
#endif /* ENCAP && UDP_DYNIPROUTE */
        }
        free_p(bp);
    }
    close_s(Rem);
    Rem = -1;
    return 0;
}
/* Check remote password */
static int
chkrpass(bpp,pass)
struct mbuf **bpp;
char *pass;
{
    char *lbuf;
    int16 len;
    int rval = 0;

    len = len_p(*bpp);

    if (pass == NULLCHAR)
        rval=1;  /* no passwd set, allow anything to match */
    else if(strlen(pass) == len) {
        lbuf = mallocw(len);
        pullup(bpp,lbuf,len);
        if(strncmp(pass,lbuf,len) == 0)
            rval = 1;
        free(lbuf);
    }
    return rval;
}
int
rem0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    close_s(Rem);
    Rem = -1;
    return 0;
}
#endif /* REMOTESERVER */

#ifdef RSYSOPSERVER
static void
rsysopserver(int s,void *o,void *p)
{
        mbx_incom (s, (void *)RSYSOP_LINK, p);
}

/* Start up RSYSOP server */
int
rsysop1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

    if(argc < 2)
       port = IPPORT_RSYSOP;
    else
       port = atoi(argv[1]);

    return start_tcp(port,"RSYSOP Server",rsysopserver,2048);
}

/* Stop RSYSOP server */
int
rsysop0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

    if(argc < 2)
       port = IPPORT_RSYSOP;
    else
       port = atoi(argv[1]);
    return stop_tcp(port);
}
#endif

#ifdef IDENTSERVER

static int Sident = 0;        /* Number of listeners running */
#define ROOT "root"           /* or "sysop" or ?? */

/* Authentication server - 01/96 K2MF */
static void
identserv
(int s,
void *t,
void *p)
{
    char *addl, *cp, *ident, ports[80], *resp, *user = NULLCHAR;
    int invalid = 0;
    int32 local = 0, remote = 0;
    struct tcb *tcb;

#ifdef MAILBOX
    struct mbx *m;
    struct usock *up;
#endif /* MAILBOX */

    sockmode(s,SOCK_ASCII);
    sockowner(s,Curproc);            /* we own it now */

    recvline(s,ports,80);
    rip(ports);
#ifdef DEBUGIDENT
    log(s,"open IDENT (%s)",ports);
#endif

    if((cp = strchr(ports,',')) != NULLCHAR) {
        /* This line probably contains a pair of TCP ports
         * in "local,remote" format */
        *cp++ = '\0';

	/* 11Oct2009, Maiko, Use atoi() for int32 vars */
        local = atoi(ports);
        remote = atoi(cp);

        if(local <= 0 || local > (int32)MAXINT16
            || remote <= 0 || remote > (int32)MAXINT16)
            /* The pair of TCP ports was out of range or
             * invalid */
            invalid = 1;
        else {
            /* The pair of TCP ports is valid - check for an
             * existing connection */
            for(tcb = Tcbs; tcb != NULLTCB; tcb = tcb->next) {
                if(tcb->conn.local.port == (int16)local
                    && tcb->conn.remote.port == (int16)remote)
                    break;
            }
            if(tcb != NULLTCB) {
                /* Found the connection ! */

#ifdef MAILBOX
                /* Isolate the socket descriptor for the
                 * existing connection */
                up = itop(tcb->user);

                if(up && up->owner != NULLPROC) {
                    /* The socket descriptor has a parent
                     * process - check the parent input
                     * socket against existing mailbox
                     * descriptors */
                    for(m = Mbox; m != NULLMBX; m = m->next) {
                        if(up->owner->input == m->user) {
                            /* Found the matching
                             * mailbox descriptor
                             */
                            user = m->name;
                            break;
                        }
                    }
                }
                if(user == NULLCHAR) {
                    /* The connection must have come from
                     * the command session */
#endif /* MAILBOX */

                    /* Check the DOS environment "USER"
                     * variable */
                    if((user = getenv("USER"))
                        == NULLCHAR)
                        /* The DOS environment "USER"
                         * variable was not found -
                         * the user must be "root" */
                        user = ROOT;

#ifdef MAILBOX
                }
#endif /* MAILBOX */

            }
        }
    }
    if(user == NULLCHAR) {
        /* If a TCP port was out of range or invalid we report
         * this first, otherwise this connection cannot be one
         * of ours ! */
        resp = "ERROR";
        addl = (invalid) ? "INVALID-PORT" : "NO-USER";
        ident = addl;
    } else {
        /* The pair of TCP ports was in range and valid and
         * this is also one of our connections */
        resp = "USERID";
        addl = "KA9Q-NOS";
        ident = user;
    }
    /* Send the reply line */
	/* 11Oct2009, Maiko, Use "%d" for int32 vars */
    usprintf(s,"%d , %d : %s : %s",local,remote,resp,addl);

    if(ident == user)
        /* There is a valid user ID - send it ! */
        usprintf(s," : %s",user);

    usputc(s,'\n');
#ifdef DEBUGIDENT
    log(s,"close IDENT (%s)",ident);
#endif
    close_s(s);
}

/* Start Authentication server */
int
identstart
(int argc,
char *argv[],
void *p)
{
    int16 port;

    Sident++;
    port = (argc < 2) ? IPPORT_IDENT : atoip(argv[1]);

    return start_tcp(port,"ident_server",identserv,1024);
}

/* Stop Authentication server */
int
identstop
(int argc,
char *argv[],
void *p)
{
    int16 port;

    port = (argc < 2) ? IPPORT_IDENT : atoip(argv[1]);

    if(Sident)
        Sident--;

    return stop_tcp(port);
}

#endif /* IDENTSERVER */
