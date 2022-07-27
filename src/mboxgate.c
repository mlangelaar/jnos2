/* The are the GATECMDS functions */
#include <time.h>
#include <ctype.h>
#ifdef MSDOS
#include <alloc.h>
#endif
#ifdef  UNIX
#include <sys/types.h>
#include <sys/stat.h>
#endif
#include "global.h"
#include "timer.h"
#include "proc.h"
#include "socket.h"
#include "usock.h"
#include "session.h"
#include "smtp.h"
#include "dirutil.h"
#include "telnet.h"
#include "ftp.h"
#include "ftpserv.h"
#include "commands.h"
#include "netuser.h"
#include "files.h"
#include "bm.h"
#include "pktdrvr.h"
#include "ax25.h"
#include "mailbox.h"
#include "ax25mail.h"
#include "nr4mail.h"
#include "cmdparse.h"
#include "mailfor.h"

#ifdef	HFDD
#include "hfdd.h"
#endif
  
/* The dombtelnet(), gw_* functions need to be there if FOQ_CMDS defined
 * they are used by dochat() (Operator), and dombfinger commands - WG7J
 */
  
#if defined GATECMDS || defined FOQ_CMDS
  
int
dombtelnet(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
    int s, len, i;
    char dsocket[MAXSOCKSIZE];
    struct sockaddr_in fsocket;
  
    m = (struct mbx *) p;
    fsocket.sin_family = AF_INET;
    if(argc < 3)
        fsocket.sin_port = IPPORT_TELNET;
    else
        fsocket.sin_port = atoip(argv[2]);
  
    if((fsocket.sin_addr.s_addr = resolve(argv[1])) == 0){
        tprintf(Badhost,argv[1]);

        free(m->startmsg);  /* OK if NULLCHAR */
        m->startmsg = NULLCHAR;

        return 0;
    }
    /* Only local telnets to the ttylink port are are allowed
     * to the unprivileged user
     * If the first letter of the command is 'Q', then it was
     * the QUERY command !
     */
    if(*argv[0] != 'Q') {
        if( ( !(m->privs & TELNET_CMD) &&
              !(ismyaddr((int32)fsocket.sin_addr.s_addr) &&
                fsocket.sin_port == IPPORT_TTYLINK)
            )
#ifdef AMPRNET
            || (hibyte(hiword((int32)fsocket.sin_addr.s_addr)) == AMPRNET) ?
               (m->privs&T_NO_AMPRNET) : (m->privs&T_AMPRNET_ONLY)
#endif /* AMPRNET */
          ){
            j2tputs(Noperm);
#ifdef MAILERROR
            mail_error("%s: Telnet denied: %s\n",m->name,cmd_line(argc,argv,m->stype));
#endif
            /* Shouldn't happen here, but just in case */
            free(m->startmsg);  /* OK if NULLCHAR */
            m->startmsg = NULLCHAR;

            return 0;
        }
    }
    /* See if we have a route to this address */
    if(rt_lookup((int32)fsocket.sin_addr.s_addr) == NULL &&
    !ismyaddr((int32)fsocket.sin_addr.s_addr)) {
        tprintf("No route to %s!\n",psocket(&fsocket));
        return 0;
    }
    if((s = j2socket(AF_INET,SOCK_STREAM,0)) == -1){
        j2tputs(Nosock);

        free(m->startmsg);  /* OK if NULLCHAR */
        m->startmsg = NULLCHAR;

        return 0;
    }
#ifdef GWTRACE
    logmbox (m->user, m->name, "TELNET to %s:%d", argv[1], fsocket.sin_port);
#endif
    if(fsocket.sin_port == IPPORT_TTYLINK) {
        m->startmsg = mallocw(80);
        len = MAXSOCKSIZE;
        i = j2getpeername(m->user,dsocket,&len);
        sprintf(m->startmsg,"*** Incoming call from %s@%s ***\n",
        m->name,i != -1 ? psocket(dsocket): Hostname);
    }
    m->state = MBX_GATEWAY;
    return gw_connect(m,s,(struct sockaddr *)&fsocket,SOCKSIZE);
}
  
/* Generic mbox gateway code. It sends and frees the contents of m->startmsg
 * when the connection has been established unless it a null pointer.
 */
int
gw_connect(m,s,fsocket,len)
struct mbx *m;
int s;
struct sockaddr *fsocket;
int len;
{
    int c,timeout;
    char *cp, *cp1;
    struct proc *child;
    struct gwalarm *gwa;
    char *node, *tocall, whereto[128], buf[80];
    char temp[AXBUF];
    struct nrroute_tab *rp;
  
    sockmode(s,SOCK_ASCII);
    child = newproc("gw supervisor",256,gw_superv,0,Curproc,m,0);
    j2tputs("Trying...");
    if(m->privs & NO_ESCAPE)
        tputc('\n');
    else {
        j2tputs("  The escape character is: ");
        if(m->escape < 32)
            tprintf("CTRL-%c\n",m->escape+'A'-1);
        else
            tprintf("'%c'\n",m->escape);
    }
    usflush(Curproc->output);
  
    /*find out where we're going to*/
    tocall = j2strdup(psocket(fsocket));
    if((cp1 = strchr(tocall,' ')) != NULLCHAR)
        *cp1 = '\0';
#if defined GATECMDS && defined NETROM
    if(fsocket->sa_family == AF_NETROM) {
        /*find the node alias*/
        setcall(temp,tocall);
        rp = find_nrroute(temp);
        node = j2strdup(rp->alias);
        if((cp1 = strchr(node,' ')) != NULLCHAR)
            *cp1 = '\0';
        sprintf(whereto,"%s:%s",node,tocall);
        free(node);
    } else
#endif
        strcpy(whereto,tocall);
    free(tocall);
  
    if(j2connect(s,(char *)fsocket,len) == -1)
	{
        if((cp = sockerr(s)) != NULLCHAR)
		{
            switch(cp[0])
			{
                case 'R':
                    sprintf(buf,"%susy from",
                    (m->family == AF_NETROM)?"B":"*** b");
                    break;
                case 'T':
                    if(m->family != AF_NETROM) {
                        sprintf(buf,"*** timeout with");
                        break;
                    }
                default:
                    sprintf(buf,"%sailure with",
                    (m->family == AF_NETROM)?"F":"*** f");
                    break;
            }
            tprintf("%s%s %s\n\n",
            (m->family == AF_NETROM) ? Mbnrid : "",buf,whereto);
        }
        j2shutdown(s,2);  /* HB9RWM suggestion */
        close_s(s);
        killproc(child);

        free(m->startmsg);  /* OK if already NULLCHAR */
        m->startmsg = NULLCHAR;

        return 0;
    }
    /* The user did not type the escape character */
    killproc(child);
  
    tprintf("%s%sonnected to %s\n",
    (m->family == AF_NETROM) ? Mbnrid : "",
    (m->family == AF_NETROM) ? "C" : "*** c",
    whereto);
  
    if(m->startmsg != NULLCHAR){
        usputs(s,m->startmsg);
        free(m->startmsg);
        m->startmsg = NULLCHAR;
    }
  
    /* Since NOS does not flush the output socket after a certain
     * period of time, we have to arrange that ourselves.
     */
    gwa = (struct gwalarm *) mallocw(sizeof(struct gwalarm));
    gwa->s1 = Curproc->output;
    gwa->s2 = s;
    set_timer(&gwa->t,2*1000);
    gwa->t.func = gw_alarm;
    gwa->t.arg = (void *) gwa;
    start_timer(&gwa->t);
    /* Fork off the receive process */
    child = newproc("gw in",1024,gw_input,s,m,Curproc,0);
  
    timeout = 0;
    for(;;){
        j2alarm(Mbtdiscinit*1000);
        if((c = recvchar(Curproc->input)) == EOF) {
            timeout = (errno == EALARM);
            j2alarm(0);
            break;
        }
        j2alarm(0);
        /* Only check ESCAPE char if that is currently turned on */
        if( !(m->privs & NO_ESCAPE) && c == m->escape){
            if(socklen(Curproc->input,0))
                recv_mbuf(Curproc->input,NULL,0,NULLCHAR,0);
            break;
        }
        if(usputc(s,c) == EOF)
            break;
    }
    stop_timer(&gwa->t);
    free((char *)gwa);
    close_s(s);
    killproc(child); /* get rid of the receive process */
    if(m->family == AF_INET) {
        tprintf("%c%c%c\n",IAC,WONT,TN_ECHO);
        RESET_OPT(m,TN_ECHO);
    }
    if(timeout)
        return EOF;
    return 0;
}
  
void
gw_input(s,notused,p)
int s;
void *notused;
void *p;
{
    int c;
    struct proc *parent;
    struct mbx *m;
    char *cp, *cp1;
    char response[4];
    int no_tel_opts = 0;
    struct usock *up;
  
    parent = (struct proc *) p;
    m = (struct mbx *) notused;
  
    cp1 = j2strdup(Mbnrid);
    if((cp = strchr(cp1,'}')) != NULLCHAR)
        *cp = '\0';
    strupr(cp1);
  
#if defined(AX25) || defined(NETROM)
/* n5knx: Determine connection type, and don't handle telnet options
 * for AF_AX25 or AF_NETROM sockets */

    if((up = itop(s)) != NULLUSOCK){
        if (
#ifdef AX25
	    up->type == TYPE_AX25I /* || up->type == TYPE_AX25UI*/
#ifdef NETROM
            ||
#endif
#endif /* AX25 */
#ifdef NETROM
            up->type == TYPE_NETROML3 || up->type == TYPE_NETROML4
#endif /* NETROM */
        ) {
            no_tel_opts=1;  /* help s be transparent.  See also Escape Off. */
            if (m->family == AF_AX25 || m->family == AF_NETROM) {
                /* when we are connecting ax.25 and netrom streams, let's be as transparent as possible */
                sockmode(s,SOCK_BINARY);
                sockmode(m->user,SOCK_BINARY);
            }
        }
    }
#endif /* AX25 || NETROM */

    while((c = recvchar(s)) != EOF){
        if(c != IAC || no_tel_opts){
            tputc((char)c);
            continue;
        }
        /* IAC received, get command sequence */
        c = recvchar(s);
        switch(c){
            case WILL:
                response[0] = IAC;
                response[1] = DONT;
                response[2] = recvchar(s);
                response[3] = '\0';
                usputs(s,response);
                break;
            case WONT:
            case DONT:
                c = recvchar(s);
                break;
            case DO:
                    response[0] = IAC;
                    response[1] = WONT;
                    response[2] = recvchar(s);
                    response[3] = '\0';
                    usputs(s,response);
                    break;
            case IAC:       /* Escaped IAC */
                usputc(s,IAC);
                break;
        }
    }
  
    if (m->family == AF_AX25 || m->family == AF_NETROM)
                sockmode(m->user,SOCK_ASCII);  /* back to normal */

    if((cp = sockerr(s)) != NULLCHAR && m->family != AF_NETROM) {
        switch(cp[0]) {
            case 'T':
                usprintf(m->user,"\n*** %s: Link failure",cp1);
                break;
            case 'R':
                usputs(m->user,"*** DM received");
                break;
        }
    }
    usprintf(m->user,"\n%s%seconnected to %s\n\n",
    (m->family == AF_NETROM) ? Mbnrid : "",
    (m->family == AF_NETROM) ? "R" : "*** r",
    cp1);
  
    free(cp1);
    cp1 = NULLCHAR;
  
    /* Tell the parent that we are no longer connected */
    alert(parent,ENOTCONN);
    pwait(Curproc); /* Now wait to be killed */
}
  
/* Check if the escape character is typed while the parent process is busy
 * doing other things.
 */
void
gw_superv(null,proc,p)
int null;
void *proc;
void *p;
{
    struct proc *parent;
    struct mbx *m;
    int c;
    parent = (struct proc *) proc;
    m = (struct mbx *) p;
    while((c = recvchar(Curproc->input)) != EOF)
        if(c == m->escape){
            /* flush anything in the input queue */
            if(socklen(Curproc->input,0))
                recv_mbuf(Curproc->input,NULL,0,NULLCHAR,0);
            break;
        }
    alert(parent,EINTR);     /* Tell the parent to quit */
    pwait(Curproc);          /* Please kill me */
}
  
void
gw_alarm(p)
void *p;
{
    struct gwalarm *gwa = (struct gwalarm *)p;
    char oldbl;
    struct usock *up;
  
    /* Flush sockets s1 and s2, but first make sure that the socket
     * is set to non-blocking mode, to prevent the flush from blocking
     * if the high water mark has been reached.
     */
    if((up = itop(gwa->s1)) != NULLUSOCK) {
        oldbl = up->noblock;
        up->noblock = 1;
        usflush(gwa->s1);
        up->noblock = oldbl;
    }
    if((up = itop(gwa->s2)) != NULLUSOCK) {
        oldbl = up->noblock;
        up->noblock = 1;
        usflush(gwa->s2);
        up->noblock = oldbl;
    }
    start_timer(&gwa->t);
}
#endif /* GATECMDS || FOQ_CMDS */
  
#ifdef GATECMDS
  
/*Enlighten them a bit!
 */
static char Mbconnecthelp[] =
"Syntax:\n"
#ifdef NETROM
"'C <node>'        for NET/ROM connects\n"
#endif
#ifdef AX25
"'C <port> <call>' for AX.25 connects\n"
#ifdef NOINVSSID_CONN
"'CONNISC <port> <call>' for AX.25 connects (no SSID inversion done)\n"
#endif
#endif
#ifdef CONVERS
"'CONV [channel]'  to access conference bridge\n"
#endif
"\n";
  
#ifdef NETROM
  
int
dombnrneighbour(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
  
    m = (struct mbx *)p;
  
    if(!(m->privs & NETROM_CMD)) {
        j2tputs(Noperm);
        return 0;
    }
    return donrneighbour(argc,argv,NULL);
}
  
int
dombnrnodes(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
  
    m = (struct mbx *)p;
  
    if(!(m->privs & NETROM_CMD)) {
        j2tputs(Noperm);
        return 0;
    }
/*
 * 07Aug2018, Maiko (VE4KLM), filter ability on 'nodes' command :)
 */
	if(argc < 2)
		return doroutedump(NULL);

	if (*argv[1] != '*')	
		return doroutedump (argv[1]);
	else
		argc = 1;

    return dorouteinfo(argc,argv,p);
}
#endif /* NETROM */
  
int
dombescape(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
  
    m = (struct mbx *)p;
    if(argc < 2){
        tprintf("Escape is %s, Escape char: ",
        (m->privs & NO_ESCAPE) ? "OFF" : "ON");
        if(m->escape < 32)
            tprintf("CTRL-%c\n",m->escape+'A'-1);
        else
            tprintf("'%c'\n",m->escape);
        return 0;
    }
    if(strlen(argv[1]) > 1) {
        if(isdigit(*argv[1]))
            m->escape = (char) atoi(argv[1]);
        else {
            if( !strnicmp(argv[1],"OFF",3) || !strnicmp(argv[1],"dis",3) )
                m->privs |= NO_ESCAPE;
            else
                m->privs &= ~NO_ESCAPE;
        }
    } else
        m->escape = *argv[1];
    return 0;
}

int
dombconnect(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
    struct nrroute_tab *np;
    int s;
    struct sockaddr_nr lsocket,fsocket;
    struct sockaddr_ax alsocket;    /*the local socket*/
    struct sockaddr_ax afsocket;    /*the remote socket*/
    struct iface *ifp;
    char alias[AXBUF];
    char local_call[AXALEN];
    char target[AXALEN];

#ifdef NOINVSSID_CONN
	char nisc = 0;
#endif

    m = (struct mbx *) p;

#ifdef NOINVSSID_CONN
	/*
	 * 09Aug2010, Maiko (VE4KLM), I want the option to be able to connect to
	 * a remote system (like Winlink RMS Packet) without having JNOS invert
	 * the SSID of the source callsign. Use 'NISC' instead of 'CONNECT' !
	 * 05Sep2010, Maiko (VE4KLM), Rename this, it messes up 'N' for nodes,
	 * something that tons of people are just used to - this is better.
	 */
	if (!stricmp ("connisc", argv[0]))
		nisc = 1;
#endif 

    if(argc == 1){
        j2tputs(Mbconnecthelp);
        return 0;
    }

#ifdef	HFDD
	if (argc > 2 && hfdd_iface (argv[1]))
	{
		argc = 4;	/* 25Jan2005, trigger display toggle */

		hfdd_client (argc, argv, (void*)0);

		return 0;
	}
#endif

    if(MBSecure)
#ifdef NETROM
        if((m->family != AF_AX25) && (m->family != AF_NETROM)) {
#else
            if(m->family != AF_AX25) {
#endif
                j2tputs(Noperm);
#ifdef MAILERROR
                mail_error("%s: gateway denied (secure mode): %s\n",m->name,cmd_line(argc,argv,m->stype));
#endif
                return 0;
            }
  
            if (argc == 2) {
#ifndef NETROM
                j2tputs(Mbconnecthelp);
                return 0;
            }
#else
    /*NETROM connection wanted*/
            if(!(m->privs & NETROM_CMD)) {
                j2tputs(Noperm);
#ifdef MAILERROR
                mail_error("%s: NETROM gate to %s denied\n",m->name,argv[1]);
#endif
                return 0;
            }
  
            if(Nr_iface == NULLIF){
                j2tputs("NET/ROM not activated.\n\n");
                return 0;
            }
    /* See if the requested destination is a known alias or call,
     * use it if it is.  Otherwize give an error message.
     */
            putalias(alias,argv[1],0);
            strupr(argv[1]);    /*make sure it's upper case*/
            if((np = find_nrboth(alias,argv[1])) == NULLNRRTAB){
        /*no such call or node alias*/
                j2tputs("no such node\n\n");
                j2tputs(Mbconnecthelp);
                dombports(0,NULL,p);
                return 0;
            }
  
            if((s = j2socket(AF_NETROM,SOCK_SEQPACKET,0)) == -1){
                j2tputs(Nosock);
                return 0;
            }
#ifdef GWTRACE
            logmbox (m->user, m->name, "NETROM to %s", argv[1]);
#endif
            lsocket.nr_family = AF_NETROM;
  
    /* Set up our local username, bind would use Mycall instead */
            memcpy(lsocket.nr_addr.user,m->call,AXALEN);
    /* Set up our source address */
            memcpy(lsocket.nr_addr.node,Nr_iface->hwaddr,AXALEN);
  
            j2bind(s,(char *)&lsocket,sizeof(struct sockaddr_nr));
  
            memcpy(fsocket.nr_addr.user,np->call,AXALEN);
            memcpy(fsocket.nr_addr.node,np->call,AXALEN);
            fsocket.nr_family = AF_NETROM;
            m->state = MBX_GATEWAY;
            return gw_connect(m,s,(struct sockaddr *)&fsocket, sizeof(struct sockaddr_nr));
        }
#endif /*NETROM*/
  
#ifdef AX25
    if(argc > 2) {
    /*AX25 gateway connection wanted*/
        if(!(m->privs & AX25_CMD)) {
            j2tputs(Noperm);
#ifdef MAILERROR
            mail_error("%s: AX.25 gate to %s on %s denied\n",m->name,argv[2],argv[1]);
#endif
            return 0;
        }
  
        if( ((ifp = if_lookup(argv[1])) == NULLIF) ||
            ((ifp->flags & HIDE_PORT) && !(m->privs & SYSOP_CMD)) ||
        (ifp->type != CL_AX25) ) {
            tprintf("Unknown port %s\n",argv[1]);
            dombports(0,NULL,p);
            return 0;
        }
        if(setcall(target,argv[2]) == -1){
            tprintf("Bad call %s\n",argv[2]);
            return 0;
        }
    /* If digipeaters are given, put them in the routing table */
        if(argc > 3)
            if(connect_filt(argc,argv,target,ifp) == 0)
                return 0;

        if((s = j2socket(AF_AX25,SOCK_STREAM,0)) == -1){
            j2tputs(Nosock);
            return 0;
        }
#ifdef GWTRACE
        logmbox (m->user, m->name, "AX25 to %s on %s", argv[2], argv[1]);
#endif
  
    /*fill in the known stuff*/
        alsocket.sax_family = afsocket.sax_family= AF_AX25;
  
    /*the remote call to connect to*/
        setcall(afsocket.ax25_addr,argv[2]);

#ifdef JNOS20_SOCKADDR_AX
	/* 30Aug2010, Maiko, New way to reference interface information */
        memset (afsocket.filler, 0, ILEN);
	afsocket.iface_index = if_indexbyname (argv[1]);
#else
    /*the outgoing interface*/
        strncpy(afsocket.iface,argv[1],ILEN);
#endif
  
    /*now set local user call, invert ssid*/
        memcpy(local_call,m->call,AXALEN);
#ifdef NOINVSSID_CONN
		if (!nisc)
#endif
        local_call[AXALEN-1] ^= 0x1e;
        memcpy(alsocket.ax25_addr,local_call,AXALEN);

    /*and bind it (otherwize Mycall will be used!)*/
        j2bind(s,(char *)&alsocket,sizeof(struct sockaddr_ax));
        m->state = MBX_GATEWAY;
        return gw_connect(m,s,(struct sockaddr *)&afsocket, sizeof(struct sockaddr_ax));
    }
#endif /* AX25 */
  
    return 0;
}
  
#ifdef AX25

/* 18Mar2006, New function to cut duplicate code */
static void doportsbytype (int iftype, long privs)
{
	struct iface *ifp;

    for (ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
	{
        if (ifp->type == iftype)
		{
			if ((!(ifp->flags & HIDE_PORT) || (privs & SYSOP_CMD)))
			{
            	tprintf ("%-7s", ifp->name);

            	if (ifp->descr != NULLCHAR)
                	tprintf (":  %s", ifp->descr);
            	else
                	tputc ('\n');
        	}
		}
	}
}

int
dombports (argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m = (struct mbx *)p;
  
    if (m->privs & NO_LISTS)
	{
        j2tputs (Noperm);
        return 0;
    }
    j2tputs ("Available AX.25 Ports:\n");
	doportsbytype (CL_AX25, m->privs);
#ifdef	HFDD
    j2tputs ("Available HFDD Ports:\n");
	doportsbytype (CL_HFDD, m->privs);
#endif
    tputc ('\n');
    return 0;
}
#endif /* AX25 */
  
#endif /* GATECMDS */
