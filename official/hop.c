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
static int geticmp __ARGS((int s,int16 lport,int16 fport,
int32 *sender,char *type,char *code));
  
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
    int64 cticks;           /* Timer for query replies */
    int32 icsource;         /* Sender of last ICMP reply */
    char ictype;            /* ICMP type last ICMP reply */
    char iccode;            /* ICMP code last ICMP reply */
    int32 lastaddr;         /* Sender of previous ICMP reply */
    struct sockaddr_in sock;
    register struct usock *usp;
    register struct sockaddr_in *sinp;
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
  
    /* Setup UDP socket to remote host */
    sock.sin_family = AF_INET;
    sock.sin_port = Hoprport;
    tprintf("Resolving %s... ",hostname);
    if((sock.sin_addr.s_addr = resolve(hostname)) == 0){
        tprintf(Badhost,hostname);
        c=1;  /* exit code */
        return (do_exit_hop (sp, c));
    }
  
    /* Open socket to remote host */
    tprintf("hopcheck to %s\n",psocket((struct sockaddr *)&sock));
    if((sp->s = s = j2socket(AF_INET,SOCK_DGRAM,0)) == -1){
        j2tputs(Nosock);
        c=1;  /* exit code */
        return (do_exit_hop (sp, c));
    }
    if(j2connect(s,(char *)&sock,sizeof(sock)) == -1){
        j2tputs("Connect failed\n");
        c=1;  /* exit code */
        return (do_exit_hop (sp, c));
    }
    if((s1 = j2socket(AF_INET,SOCK_RAW,ICMP_PTCL)) == -1){
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
    sinp = (struct sockaddr_in *)usp->name;
    lsocket.address = sinp->sin_addr.s_addr;
    lsocket.port = sinp->sin_port;
    sinp = (struct sockaddr_in *)usp->peername;
    rsocket.address = sinp->sin_addr.s_addr;
  
    /* Send queries with increasing TTL; start with TTL=1 */
    if (Hoptrace)
        log(sp->s,"HOPCHECK start trace to %s\n",sp->name);
    for (sndttl=1; (sndttl < Hopmaxttl); ++sndttl, sinp->sin_port++)
	{
        /* Increment funny UDP port number each round */
        rsocket.port = sinp->sin_port;
        tprintf("%3d:",sndttl);
        lastaddr = (int32)0;
		breakout = 0;
        /* Send a round of queries */
        for (q=0; (q < Hopquery); ++q)
		{
            send_udp(&lsocket,&rsocket,0,sndttl,NULLBUF,0,0,0);
            cticks = msclock();
            j2alarm ((int32)(Hopmaxwait*1000));
  
            /* Wait for a reply to our query */
			if (geticmp(s1,lsocket.port,rsocket.port,
				&icsource, &ictype, &iccode) == -1)
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
            if (icsource != lastaddr) {
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
            if (Hoptrace)
                log(sp->s,
                "(hopcheck) ICMP from %s (%d ms) %s %s",
                inet_ntoa(icsource),
                (int32)cticks,
                Icmptypes[(int)ictype],
                (((int)ictype == ICMP_TIME_EXCEED)?Exceed[(int)iccode]:Unreach[(int)iccode]));
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
#ifdef HOPTRACE
    if (Hoptrace)
        log(sp->s,"HOPCHECK to %s done",sp->name);
#endif
    c=0;  /* return code */

	return (do_exit_hop (sp, c));	/* replaces exit_hop GOTO label */
}
  
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
  
