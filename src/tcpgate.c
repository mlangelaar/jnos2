/* TCPGATE - taken from WAMPES (so I understand) and found its way
   into TNOS Unix version. Since I wanted to play DOS only on a very
   simple 286/386sx type of machine as a router hereis a hacked about
   version for the general use of. Usual cast iron underwear applies
   if you use it. Robin Gilks, G8ECJ, 19-Feb-1997
*/

#include "global.h"
#ifdef TCPGATE
#include "mbuf.h"
#include "socket.h"
#include "commands.h"
#include "netuser.h"
#include "cmdparse.h"

/* function prototypes */
static void gate __ARGS((int s,void *unused,void *p));
static void gate_output(int s1,void *s2,void *p);

static int dogatestatus __ARGS((int argc,char *argv[],void *p));
static int dogatetdisc __ARGS((int argc,char *argv[],void *p));
static int dogatemax __ARGS((int argc,char *argv[],void *p));



/* note that this structure will change when I get round to it - I prefer
   a linked list a la original but at least this works!! */
#define MAXPORTS 11     /* how many ports we want to redirect */

struct portsused {
	int portin;
	int portout;
	int32 addrout;
};

/* useful info about who is connected to what */
struct gateinfo {
    int32 addrin;
    int portin;                 /*user details */
    int32 addrout;
    int portout;                /* what they are connected to */
    int portgate;               /* via what gate port */
    struct gateinfo *next;      /* next user */
};

int GateUsers = 0;          /* referenced in PC.C, same as all other servers */
static struct portsused ports[MAXPORTS];
static int Firstserver = 1;
static int Gatemax = 20;        /* arbitrary number of users */

static int32 Gatetdisc = 1800;

struct gateinfo *GateInfo = NULL;



static struct cmds Gatecmds[] = {
	{ "maxcli",	dogatemax,	0, 0, NULLCHAR },
	{ "status",	dogatestatus,	0, 0, NULLCHAR }, 
	{ "tdisc",	dogatetdisc,	0, 0, NULLCHAR },
	{ NULLCHAR,	NULL,		0, 0, NULLCHAR }
};

int
dogate (argc, argv, p)		/* main config entry point */
int argc;
char *argv[];
void *p;
{
struct gateinfo *gate;
struct socket sp;
char temp[80];

    if (argc < 2) {
        if (GateInfo) {
            j2tputs("Remote Socket:Port       connects via       to TcpGate Socket:Port\n");
            for (gate = GateInfo; gate != NULL; gate = gate->next) {
                sp.address = gate->addrin;
                sp.port = gate->portin;
                sprintf(temp, "%-28s %u             ", pinet(&sp), gate->portgate);
                sp.address = gate->addrout;
                sp.port = gate->portout;
                strcat(temp, pinet(&sp));
                strcat(temp, "\n");
                j2tputs (temp);
            }
        }
        return 0;
    }
    else
        return subcmd (Gatecmds, argc, argv, p);
}

int
dogatestatus (argc, argv, p)
int argc;
char *argv[];
void *p;
{
int k;
struct socket sp;

	for (k = 0; k < MAXPORTS; k++)	{
		if (ports[k].portin)	{
			sp.address = ports[k].addrout;
			sp.port = ports[k].portout;
			tprintf ("GATE Server on port #%-4u connects to %s\n", ports[k].portin, pinet(&sp));
		}
	}
	tprintf("\n");

    dogatemax (0, argv, p);
	dogatetdisc (0, argv, p);

    return 0;
}


static int
dogatemax (argc, argv, p)
int argc;
char *argv[];
void *p;
{
    return setint (&Gatemax, "Max. GATE connects", argc, argv);
}



static int dogatetdisc (int argc, char **argv, void *p)
{
    return setint32 (&Gatetdisc, "GATE Server tdisc (sec)", argc, argv);
}



/* Start up gate service refered to in 'start' table in config.c */
/* Usage: "start gate port# hostname [port#] */
int
gatestart (argc, argv, p)
int argc;
char *argv[];
void *p;
{
int port, k;

    port = atoi(argv[1]);

    if(Firstserver) {                   // initialize
        Firstserver = 0;
        for(k = 0; k < MAXPORTS; k++)
            ports[k].portin = 0;
    }
        
	for (k = 0; k < MAXPORTS; k++) {
		if (ports[k].portin == port)	{
			tprintf ("You already have a gateway server on port #%d!\n", port);
			return 0;
		}
	}
	for (k = 0; k < MAXPORTS; k++) {
		if (!ports[k].portin)
			break;
	}
	if (k == MAXPORTS)	{
		tprintf ("All %d gateway ports are assigned!\n", MAXPORTS);
		return 0;
	}

    if((ports[k].addrout = resolve(argv[2])) == 0){
		tprintf ("Hostname %s not resolved\n", argv[2]);
		return 0;
	}

	if(argc < 4)
        ports[k].portout = port;
    else
		ports[k].portout = atoi(argv[3]);
            
	ports[k].portin = port;

    if (start_tcp(port,"GATE Server", gate, 320)) {
        ports[k].portin = 0;
        tprintf("start failed, code=%d\n", Net_error);
        return -1;
    } else
        return 0;
}


int
gate0 (argc, argv, p)		/* referenced in the 'stop' table in config.c */
int argc;
char *argv[];
void *p;
{
int port, k;

	port = atoi(argv[1]);

	for (k = 0; k < MAXPORTS; k++) {
		if (ports[k].portin == port)	{
			ports[k].portin = 0;
			return (stop_tcp(port));
		}
	}
	tprintf ("No gateway server was found on port #%d!\n", port);
	return 0;
}



static int get_p_index(int s)
{
    int i;
    struct sockaddr_in lsocket;

    i = SOCKSIZE;
    j2getsockname(s,(char *)&lsocket,&i);
    for(i=0;i<MAXPORTS;i++)
        if(ports[i].portin == lsocket.sin_port)
            break;
    return i;
}


static void
gate (s_in, v1, p)
int s_in;
void *v1;
void *p;
{

    struct sockaddr_in rsocket;
	struct proc *txproc;
   	int  i, pindex, s_out, datalen;
    struct mbuf *bp;  /* Place to stash receive buffer */
    struct gateinfo *gate, *tmpgate1, *tmpgate2;

	long s_out_l;	/* 05Dec2020, Maiko, bridge for cast warning */
	
    sockowner (s_in, Curproc);
	close_s(Curproc->output);
	close_s(Curproc->input);
	Curproc->output = Curproc->input = s_in;

#ifndef UNIX
    if (availmem() < Memthresh)
		return;
#endif
    if (Gatemax < GateUsers)
        return;

	pindex = get_p_index(s_in);

    rsocket.sin_family = AF_INET;
    rsocket.sin_port = ports[pindex].portout;
    rsocket.sin_addr.s_addr = ports[pindex].addrout;

    /* Open the outbound connection */
    if ((s_out = j2socket(AF_INET,SOCK_STREAM,0)) == -1){
	    log(s_in,"GATE - no outbound socket for #%u",ports[pindex].portin);
        return;
    }

	if (j2connect(s_out,(char *)&rsocket,sizeof(rsocket)) == -1){
	    log(s_in,"GATE connect on port #%-4u failed to %s:%-4u",ports[pindex].portin, inet_ntoa(ports[pindex].addrout), ports[pindex].portout);
        return;
	}

    GateUsers++;        /* now I have it all set up... */
    log(s_in, "open GATE server on port %d", ports[pindex].portin);

    gate = (struct gateinfo *) callocw(1,sizeof(struct gateinfo));

    gate->addrout = rsocket.sin_addr.s_addr;
    gate->portout = rsocket.sin_port;
    i = SOCKSIZE;
    j2getpeername(s_in,(char *)&rsocket,&i);
    gate->addrin = rsocket.sin_addr.s_addr;
    gate->portin = rsocket.sin_port;
    j2getsockname(s_in,(char *)&rsocket,&i);
    gate->portgate = rsocket.sin_port;
    gate->next = NULL;

    if (GateInfo == NULL)
        GateInfo = gate;
    else {
        for (tmpgate1 = GateInfo; tmpgate1 != NULL; tmpgate2 = tmpgate1, tmpgate1 = tmpgate1->next ) ;
        tmpgate2->next = gate;
    }

	s_out_l = (long)s_out;	/* 05Dec2020, Maiko, bridge for cast warning */

    /* Fork off the transmit process */
	txproc = newproc("gate_out",320,gate_output,s_in,(void *)s_out_l,&txproc,0);

    while(1) {
		if ((datalen = socklen(s_in,0)) > 0)	{
			if (recv_mbuf(s_in,&bp,0,NULLCHAR,0) == -1)
				break;
			if (send_mbuf(s_out,bp,0,NULLCHAR,0) == -1)
				break;

		} else if (datalen < 0) 
 			break;

		if (!datalen) 
            j2pause(60);      /* at least one PC tick */

		if (txproc == NULLPROC)
			break;
	}
	killproc(txproc);

    GateUsers--;
    if (gate == GateInfo)
        GateInfo = GateInfo->next;
    else {
        for (tmpgate1 = tmpgate2 = GateInfo; tmpgate1 && tmpgate1 != gate; tmpgate2 = tmpgate1, tmpgate1 = tmpgate1->next) ;
        tmpgate2->next = tmpgate1->next;
    }
    free(gate);

	log(s_in, "closed GATE server on port %d", ports[pindex].portin);

	/* 05Jul2016, Maiko, should be j2shutdown, NOT shutdown !!! */
    j2shutdown (s_in,2);  /* belt & braces ;-) */

    close_s (s_in);
    close_s (s_out);    /* assume this is local & cleans itself OK */
	
}

static void
gate_output(s_in,s2,tx)
int s_in;
void *s2;
void *tx;
{
struct mbuf *bp;
int s_out;
struct proc **txproc;

	long s_out_l;	/* 05Dec2020, Maiko, bridge for cast warning */

	s_out_l = (long)s2;
	s_out = (int)s_out_l;	/* 05Dec2020, Maiko, compiler warnings */

	txproc = (struct proc **)tx;

	while(1) {
	    j2alarm (Gatetdisc * 1000);
		if (recv_mbuf(s_out,&bp,0,NULLCHAR,0) == -1)
			break;
		j2alarm(0);
		if(send_mbuf(s_in,bp,0,NULLCHAR,0) == -1)
			break;
	}
	*txproc = NULLPROC;
}


#endif	/*TCPGATE*/


