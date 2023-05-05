/*
 *  HOP.C   -- trace route packets take to a remote host
 *
 *  02-90   -- Katie Stevens (dkstevens@ucdavis.edu)
 *         UC Davis, Computing Services
 *         Davis, CA
 *  04-90   -- Modified by Phil Karn to use raw IP sockets to read replies
 *  08-90   -- Modified by Bill Simpson to display domain names
 *
 *  Mods by PA0GRI  (newsession param)
 */
  
#include "global.h"
#ifdef HOPCHECKSESSION
#include "mbuf.h"
#include "usock.h"
#include "socket.h"
#include "session.h"
#include "timer.h"
#include "proc.h"
#include "netuser.h"
#include "domain.h"
#include "commands.h"
#include "tty.h"
#include "cmdparse.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "unix.h"

#ifdef	IPV6
/* 14Apr2023, Maiko */
#include "ipv6.h"
#include "icmpv6.h"
#endif
  
#define HOPMAXQUERY 5       /* Max# queries each TTL value */
static int16 Hoprport = 32768U+666;  /* funny port for udp probes */
#define HOP_HIGHBIT 32768       /* Mask to check ICMP msgs */
  
  
#define HOPTRACE    1       /* Enable HOP tracing */
#ifdef HOPTRACE
static int Hoptrace = 0;
static int hoptrace __ARGS((int argc,char *argv[],void *p));
#endif
  
  
static unsigned  short Hopmaxttl  = 30;     /* max attempts */
static unsigned  short Hopmaxwait = 5;      /* secs timeout each attempt */
static unsigned  short Hopquery   = 3;      /* #probes each attempt */
  
static int hopcheck __ARGS((int argc,char *argv[],void *p));
static int hopttl __ARGS((int argc,char *argv[],void *p));
static int hopwait __ARGS((int argc,char *argv[],void *p));
static int hopnum __ARGS((int argc,char *argv[],void *p));
static int geticmp __ARGS((int s,int16 lport,int16 fport, int32 *sender,char *type,char *code));
#ifdef	IPV6
static int geticmpv6 (int s, int16 lport, int16 fport, unsigned char *sender6, char *type, char *code);
#endif
  
static struct cmds DFAR Hopcmds[] = {
    { "check",    hopcheck,   2048,   2,  "check <host>" },
    { "maxttl",   hopttl,     0,  0,  NULLCHAR },
    { "maxwait",  hopwait,    0,  0,  NULLCHAR },
    { "queries",  hopnum,     0,  0,  NULLCHAR },
#ifdef HOPTRACE
    { "trace",    hoptrace,   0,  0,  NULLCHAR },
#endif
    { NULLCHAR,	NULL,		0,	0, NULLCHAR }
};
  
/* attempt to trace route to a remote host */
int
dohop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Hopcmds,argc,argv,p);
}
  
/* Set/show # queries sent each TTL value */
static int
hopnum(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 r;
    int16 x = Hopquery;
    r = setshort(&x,"# queries each attempt",argc,argv);
    if ((x <= 0)||(x > HOPMAXQUERY)) {
        tprintf("Must be  0 < x <= %d\n",HOPMAXQUERY);
        return 0;
    } else {
        Hopquery = x;
    }
    return (int)r;
}
#ifdef HOPTRACE
/* Set/show tracelevel */
static int
hoptrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Hoptrace,"HOPCHECK tracing",argc,argv);
}
#endif
/* Set/show maximum TTL value for a hopcheck query */
static int
hopttl(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 r;
    int16 x = Hopmaxttl;
    r = setshort(&x,"Max attempts to reach host",argc,argv);
    if ((x <= 0)||(x > 255)) {
        tprintf("Must be  0 < x <= 255\n");
        return 0;
    } else {
        Hopmaxttl = x;
    }
    return (int)r;
}
/* Set/show #secs until timeout for a hopcheck query */
static int
hopwait(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 r;
    int16 x = Hopmaxwait;
    r = setshort(&x,"# secs to wait for reply to query",argc,argv);
    if (x <= 0) {
        tprintf("Must be > 0\n");
        return 0;
    } else {
        Hopmaxwait = x;
    }
    return (int)r;
}

/* 21Dec2004, Maiko, This replaces the GOTO exit_hop label */
static int do_exit_hop (struct session *sp, int c)
{
    keywait(NULLCHAR,1);
    freesession(sp);
    return c;
}
  
/* send probes to trace route of a remote host */
static int
hopcheck(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct session *sp;     /* Session for trace output */
    int s;              /* Socket for queries */
    int s1;             /* Raw socket for replies */
    struct socket lsocket;      /* Local socket sending queries */
    struct socket rsocket;      /* Final destination of queries */
#ifdef	IPV6
    struct j2socketv6 lsocket6;      /* Local socket sending queries */
    struct j2socketv6 rsocket6;      /* Final destination of queries */
	unsigned char icv6source[16];
	unsigned char lastaddr6[16];	/* 15Apr2023, Maiko */
#endif
    int64 cticks;           /* Timer for query replies */
    int32 icsource;         /* Sender of last ICMP reply */
    char ictype;            /* ICMP type last ICMP reply */
    char iccode;            /* ICMP code last ICMP reply */
    int32 lastaddr;         /* Sender of previous ICMP reply */
    struct sockaddr_in sock;
#ifdef	IPV6
	struct j2sockaddr_in6 sock6;	/* 14Apr2023, Maiko */
	short family;
	int protocol;
#endif
    register struct usock *usp;
    register struct sockaddr_in *sinp;
#ifdef IPV6
    register struct j2sockaddr_in6 *sinp6;
	int16 ipvN_hop_port;	/* 15Apr2023, Maiko */
#endif
    unsigned char sndttl, q;
    int tracedone = 0;
    int ilookup = 1;        /* Control of inverse domain lookup */
    int c;
    extern int j2optind;
    char *hostname;
#ifdef g1emmx
    char *sname;            /* Used in resolve_a() call */
#endif
    int trans;          /* Save IP address translation state */
    int save_trace;
	int breakout;	/* used to replace GOTO done label */
  
    /*Make sure this comes from console - WG7J*/
    if(Curproc->input != Command->input)
        return 0;
  
    j2optind = 1;
    while((c = j2getopt(argc,argv,"n")) != EOF){
        switch(c){
            case 'n':
                ilookup = 0;
                break;
        }
    }
    if(j2optind >= argc) {
        j2tputs("Insufficient args\n");
        return 1;
    }
    hostname = argv[j2optind];
    /* Allocate a session descriptor */
    if((sp = newsession(hostname,HOP,0)) == NULLSESSION){
        j2tputs(TooManySessions);
        keywait(NULLCHAR,1);
        return 1;
    }
    sp->s = s = -1;
    sp->flowmode = 1;
 
#ifdef	IPV6
	/*
	 * 14Apr2023, Maiko (VE4KLM), See if I can get this working IPV6 :]
	 */

    tprintf("Resolving %s (ipv6) ... ",hostname);

	copyipv6addr (resolve6 (hostname), sock6.sin6_addr.s6_addr);

	if (sock6.sin6_addr.s6_addr[0] != 0x00)
	{
		family = sock6.sin6_family = AF_INET6;
		sock6.sin6_port = Hoprport;
		protocol = ICMPV6_PTCL;

    	tprintf("\nhopcheck to %s\n", human_readable_ipv6 (sock6.sin6_addr.s6_addr));
	}
	else
	{
#endif

    /* Setup UDP socket to remote host */
    family = sock.sin_family = AF_INET;
    sock.sin_port = Hoprport;
	protocol = ICMP_PTCL;	/* 14Apr2023, Maiko */
 
    tprintf("Resolving %s (ipv4) ... ",hostname);

    if((sock.sin_addr.s_addr = resolve(hostname)) == 0){
        tprintf(Badhost,hostname);
        c=1;  /* exit code */
        return (do_exit_hop (sp, c));
    }

    tprintf("\nhopcheck to %s\n",psocket((struct sockaddr *)&sock));

#ifdef	IPV6
	}
#endif
  
    /*
	 * Open socket to remote host
	 * 14Apr2023, Maiko, passing family now, set earlier
     */
    if((sp->s = s = j2socket(family,SOCK_DGRAM,0)) == -1){
        j2tputs(Nosock);
        c=1;  /* exit code */
        return (do_exit_hop (sp, c));
    }

#ifdef IPV6
	if (family == AF_INET6)
	{
    	if(j2connect(s,(char *)&sock6,sizeof(sock6)) == -1){
        	j2tputs("Connect (IPV6) failed\n");
        	c=1;  /* exit code */
        	return (do_exit_hop (sp, c));
    	}
	}
	else
#endif
    if(j2connect(s,(char *)&sock,sizeof(sock)) == -1){
        j2tputs("Connect failed\n");
        c=1;  /* exit code */
        return (do_exit_hop (sp, c));
    }
	/* 14Apr2023, Maiko, passing family and protocol now */
    if((s1 = j2socket(family,SOCK_RAW,protocol)) == -1){
        j2tputs(Nosock);
        c=1;  /* exit code */
        return (do_exit_hop (sp, c));
    }
    /* turn off icmp tracing while hop-checking */
    save_trace = Icmp_trace;
    Icmp_trace = 0;
    /* Setup structures to send queries */
    /* Retrieve socket details for user socket control block */
    usp = itop(s);

#ifdef	IPV6
	if (family == AF_INET6)
	{
    	sinp6 = (struct j2sockaddr_in6 *)usp->name;
		/* 15Apr2023, Maiko, Should be my own IPV6 address ? not sure why local is blank */
 		copyipv6addr (sinp6->sin6_addr.s6_addr, lsocket6.address);
 		// copyipv6addr (myipv6addr, lsocket6.address);
    	lsocket6.port = sinp6->sin6_port;
    	sinp6 = (struct j2sockaddr_in6 *)usp->peername;
 		copyipv6addr (sinp6->sin6_addr.s6_addr, rsocket6.address);
		ipvN_hop_port = sinp6->sin6_port;	/* 15Apr2023, Maiko */
	}
	else
	{
#endif
    sinp = (struct sockaddr_in *)usp->name;
    lsocket.address = sinp->sin_addr.s_addr;
    lsocket.port = sinp->sin_port;
    sinp = (struct sockaddr_in *)usp->peername;
    rsocket.address = sinp->sin_addr.s_addr;
		ipvN_hop_port = sinp->sin_port;	/* 15Apr2023, Maiko */
#ifdef IPV6
	}
#endif
 
    /* Send queries with increasing TTL; start with TTL=1 */
    if (Hoptrace)
        log(sp->s,"HOPCHECK start trace to %s\n",sp->name);

 	/* 15Apr2023, Maiko, Oops, port source ipvN specific as well ! */
    for (sndttl=1; (sndttl < Hopmaxttl); ++sndttl, ipvN_hop_port++)
	{

	/* 15Apr2023, Maiko, Fooling around - force hop port = 53 !!! */
	ipvN_hop_port = 53;	/* since this is a UDP based traceroute */

        /* Increment funny UDP port number each round */
#ifdef	IPV6
		if (family == AF_INET6)
	        rsocket6.port = ipvN_hop_port;
		else
#endif
        rsocket.port = ipvN_hop_port;
        tprintf("%3d:",sndttl);
        lastaddr = (int32)0;
#ifdef	IPV6
		zeroipv6addr (lastaddr6);	/* 15Apr2023, Maiko */
#endif
		breakout = 0;
        /* Send a round of queries */
        for (q=0; (q < Hopquery); ++q)
		{
#ifdef	IPV6
			if (family == AF_INET6)
            	send_udpv6(&lsocket6,&rsocket6,0,sndttl,NULLBUF,0,0,0);
			else
#endif
            send_udp(&lsocket,&rsocket,0,sndttl,NULLBUF,0,0,0);

            cticks = msclock();
            j2alarm ((int32)(Hopmaxwait*1000));
 
            /* Wait for a reply to our query */
#ifdef	IPV6
			if (family == AF_INET6)
				c = geticmpv6 (s1,lsocket6.port,rsocket6.port, icv6source, &ictype, &iccode);
			else	
#endif 
				c = geticmp(s1,lsocket.port,rsocket.port, &icsource, &ictype, &iccode);

			if (c == -1)
			{
                if(errno != EALARM)
				{
                    j2alarm (0); /* cancel alarm */
                    breakout = 1;	/* replaces GOTO done label */
					break;
                }
                /* Alarm rang, give up waiting for replies */
                j2tputs(" ***");
                continue;
            }
            /* Save #ticks taken for reply */
            cticks = msclock() - cticks;

            /* Report ICMP reply */
#ifdef	IPV6
			/* 15Apr2023, Maiko */
			if (family == AF_INET6)
			{
				if (lastaddr6[0] != 0x00)
                    j2tputs("\n    ");

                // tprintf(" %-15s", ipv6shortform (human_readable_ipv6 (icv6source), 0));
                tprintf(" %-15s", human_readable_ipv6 (icv6source));

				/* don't bother with inverse lookup at this point */

                copyipv6addr (icv6source, lastaddr6);
			}
			else
#endif
            if (icsource != lastaddr)
			{
                struct rr *save_rrlp, *rrlp;
  
                if(lastaddr != (int32)0)
                    j2tputs("\n    ");
            /* Save IP address translation state */
                trans = DTranslate;
            /* Force output to be numeric IP addr */
                DTranslate = 0;
                tprintf(" %-15s",inet_ntoa(icsource));
            /* Restore original state */
                DTranslate = trans;
#ifdef g1emmx
                if((sname = resolve_a(icsource, FALSE)) != NULLCHAR) {
                    tprintf(" %s", sname);
                    free(sname);
                }
#else
                if(ilookup){
                    for(rrlp = save_rrlp = inverse_a(icsource);
                        rrlp != NULLRR;
                    rrlp = rrlp->next){
                        if(rrlp->rdlength > 0){
                            switch(rrlp->type){
                                case TYPE_PTR:
                                    tprintf(" %s", rrlp->rdata.name);
                                    break;
                                case TYPE_A:
                                    tprintf(" %s", rrlp->name);
                                    break;
                            }
                            if(rrlp->next != NULLRR)
                                tprintf("\n%20s"," ");
                        }
                    }
                    free_rr(save_rrlp);
#endif
                }
                lastaddr = icsource;
            }

	/* 11Oct2009, Maiko, Use "%d" for int32 vars */
            tprintf(" (%d ms)", (int32)cticks);

#ifdef HOPTRACE
#ifdef	IPV6
			if (family == AF_INET6)
			{
				if (Hoptrace)
                	log(sp->s, "hoptrace not done yet");
			}
			else
#endif
            if (Hoptrace)
                log(sp->s,
                "(hopcheck) ICMP from %s (%d ms) %s %s",
                inet_ntoa(icsource),
                (int32)cticks,
                Icmptypes[(int)ictype],
                (((int)ictype == ICMP_TIME_EXCEED)?Exceed[(int)iccode]:Unreach[(int)iccode]));
#endif

#ifdef	IPV6  
			if (family == AF_INET6)
			{
            	if (ictype == ICMPV6_TIME_EXCEED)
                	continue;

            /* Reply was: destination unreachable */
            switch(iccode) {
                case ICMPV6_PORT_UNREACH:
                    ++tracedone;
                    break;
                case ICMPV6_NET_UNREACH:
                    ++tracedone;
                    j2tputs(" !N");
                    break;
                case ICMPV6_HOST_UNREACH:
                    ++tracedone;
                    j2tputs(" !H");
                    break;
                case ICMPV6_ADMIN_PROHIB:
                    ++tracedone;
                    j2tputs(" !A");
                    break;
                default:
                    j2tputs(" !?");
                    break;
            }
			}
			else
			{
#endif
            /* Check type of reply */
            if (ictype == ICMP_TIME_EXCEED)
                continue;
            /* Reply was: destination unreachable */
            switch(iccode) {
                case ICMP_PORT_UNREACH:
                    ++tracedone;
                    break;
                case ICMP_NET_UNREACH:
                    ++tracedone;
                    j2tputs(" !N");
                    break;
                case ICMP_HOST_UNREACH:
                    ++tracedone;
                    j2tputs(" !H");
                    break;
                case ICMP_PROT_UNREACH:
                    ++tracedone;
                    j2tputs(" !P");
                    break;
                case ICMP_FRAG_NEEDED:
                    ++tracedone;
                    j2tputs(" !F");
                    break;
                case ICMP_ROUTE_FAIL:
                    ++tracedone;
                    j2tputs(" !S");
                    break;
                case ICMP_ADMIN_PROHIB:
                    ++tracedone;
                    j2tputs(" !A");
                    break;
                default:
                    j2tputs(" !?");
                    break;
            }
#ifdef	IPV6
		}
#endif
        }
		if (breakout)	/* replaces GOTO done label */
			break;
        /* Done with this round of queries */
        j2alarm (0);
        tputc('\n');
        /* Check if we reached remote host this round */
        if (tracedone != 0)
            break;
    }
  
    /* Done with hopcheck */
/*    done: replaced this GOTO label with the breakout variable */
	close_s(s);
    sp->s = -1;
    close_s(s1);
    j2tputs("hopcheck done: ");
    Icmp_trace = save_trace;

#ifdef	IPV6
	if (family == AF_INET6)
	{
    	if (sndttl >= Hopmaxttl)
        	j2tputs("!! maximum TTL exceeded\n");
		else if (!memcmp (icv6source, rsocket6.address, 16) &&(iccode == ICMPV6_PORT_UNREACH))
        	tprintf("normal (type %d code %d)\n", ictype, iccode);
    	else
        	tprintf("!! %d %d\n", ictype, iccode);
	}
	else
	{
#endif
    if (sndttl >= Hopmaxttl) {
        j2tputs("!! maximum TTL exceeded\n");
    } else if ((icsource == rsocket.address)
    &&(iccode == ICMP_PORT_UNREACH)) {
        tprintf("normal (%s %s)\n",
        Icmptypes[(int)ictype],Unreach[(int)iccode]);
    } else {
        tprintf("!! %s %s\n",
        Icmptypes[(int)ictype],Unreach[(int)iccode]);
    }
#ifdef	IPV6
	}
#endif

#ifdef HOPTRACE
    if (Hoptrace)
        log(sp->s,"HOPCHECK to %s done",sp->name);
#endif
    c=0;  /* return code */

	return (do_exit_hop (sp, c));	/* replaces exit_hop GOTO label */
}
 
#ifdef	IPV6
/*
 * 14Apr2023, Maiko, too IP version specific to have one function, so
 * will need to write an IPV6 equivalent of geticmp() further below.
 */
static int geticmpv6 (s,lport,fport,sender6,type,code)
int s;
int16 lport;
int16 fport;
unsigned char *sender6;
char *type,*code;
{
    int size;
    struct icmpv6 icmpv6hdr;
    struct ipv6 ipv6hdr;
    struct udp udphdr;
    struct mbuf *bp;
    struct j2sockaddr_in6 sock6;
  
    for(;;){
        size = sizeof(sock6);
        if(recv_mbuf(s,&bp,0,(char *)&sock6,&size) == -1)
            return -1;

        /* It's an ICMP message, let's see if it's interesting */
        ntohicmpv6(&icmpv6hdr,&bp);

	// log (-1, "type %d code %d", icmpv6hdr.type, icmpv6hdr.code);

        if((icmpv6hdr.type != ICMPV6_TIME_EXCEED ||
            icmpv6hdr.code != ICMPV6_HOP_LIMIT_EXCEED)
        && icmpv6hdr.type != ICMPV6_DEST_UNREACH){
            /* We're not interested in these */
            free_p(bp);
            continue;
        }
        ntohipv6(&ipv6hdr,&bp);
        if(ipv6hdr.next_header != UDP_PTCL){
            /* Not UDP, so can't be interesting */
            free_p(bp);
            continue;
        }
        ntohudp(&udphdr,&bp);
        if(udphdr.dest != fport || udphdr.source != lport){
            /* Not from our hopcheck session */
            free_p(bp);
            continue;
        }
        /* Passed all of our checks, so return it */
        copyipv6addr (sock6.sin6_addr.s6_addr, sender6);
        *type = icmpv6hdr.type;
        *code = icmpv6hdr.code;
        free_p(bp);
        return 0;
    }
}
#endif
 
/* Read raw network socket looking for ICMP messages in response to our
 * UDP probes
 */
static int
geticmp(s,lport,fport,sender,type,code)
int s;
int16 lport;
int16 fport;
int32 *sender;
char *type,*code;
{
    int size;
    struct icmp icmphdr;
    struct ip iphdr;
    struct udp udphdr;
    struct mbuf *bp;
    struct sockaddr_in sock;
  
    for(;;){
        size = sizeof(sock);
        if(recv_mbuf(s,&bp,0,(char *)&sock,&size) == -1)
            return -1;
        /* It's an ICMP message, let's see if it's interesting */
        ntohicmp(&icmphdr,&bp);
        if((icmphdr.type != ICMP_TIME_EXCEED ||
            icmphdr.code != ICMP_TTL_EXCEED)
        && icmphdr.type != ICMP_DEST_UNREACH){
            /* We're not interested in these */
            free_p(bp);
            continue;
        }
        ntohip(&iphdr,&bp);
        if(iphdr.protocol != UDP_PTCL){
            /* Not UDP, so can't be interesting */
            free_p(bp);
            continue;
        }
        ntohudp(&udphdr,&bp);
        if(udphdr.dest != fport || udphdr.source != lport){
            /* Not from our hopcheck session */
            free_p(bp);
            continue;
        }
        /* Passed all of our checks, so return it */
        *sender = sock.sin_addr.s_addr;
        *type = icmphdr.type;
        *code = icmphdr.code;
        free_p(bp);
        return 0;
    }
}
#endif /* HOPCHECKSESSION */
  
