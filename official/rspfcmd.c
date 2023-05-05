#include "global.h"
#ifdef RSPF
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "cmdparse.h"
#include "netuser.h"
#include "socket.h"
#include "rspf.h"
#include "ip.h"
  
int Rspfownmode = -1;
static int dointerface __ARGS((int argc,char *argv[],void *p));
static int domessage __ARGS((int argc,char *argv[],void *p));
static int domaxping __ARGS((int argc,char *argv[],void *p));
static int domode __ARGS((int argc,char *argv[],void *p));
static int dorrhtimer __ARGS((int argc,char *argv[],void *p));
static int dotimer __ARGS((int argc,char *argv[],void *p));
static int doroutes __ARGS((int argc,char *argv[],void *p));
static int dostatus __ARGS((int argc,char *argv[],void *p));
static int dosuspect __ARGS((int argc,char *argv[],void *p));
static struct timer rrhtimer, rspftimer;

/* 05Dec2020, Maiko, compiler complaing about lack of braces */  

static struct cmds DFAR Rspfcmds[] = {
    { "interface",    dointerface,    0,      0,      NULLCHAR },
    { "message",      domessage,      0,      0,      NULLCHAR },
    { "maxping",      domaxping,      0,      0,      NULLCHAR },
    { "mode",         domode,         0,      0,      NULLCHAR },
    { "rrhtimer",     dorrhtimer,     0,      0,      NULLCHAR },
    { "routes",       doroutes,       0,      0,      NULLCHAR },
    { "status",       dostatus,       0,      0,      NULLCHAR },
    { "suspecttimer", dosuspect,      0,      0,      NULLCHAR },
    { "timer",        dotimer,        0,      0,      NULLCHAR },
    { NULLCHAR,		NULL,		0,	0,	NULLCHAR }
};
  
int
dorspf(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Rspfcmds,argc,argv,p);
}
  
/* The suspect timer controls how often old links expire. When a link has
 * expired, we try to renew its entry by various methods.
 */
static int
dosuspect(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(Rspfifaces == NULLRIFACE){
        j2tputs("RSPF is not active - define interface first.\n");
        return 0;
    }
    if(argc < 2){
        tprintf("Suspect timer: %d/%d seconds\n",
        read_timer(&Susptimer)/1000,
        dur_timer(&Susptimer)/1000);
        return 0;
    }
    Susptimer.func = rspfsuspect; /* what to call on timeout */
    Susptimer.arg = NULL;                   /* dummy value */

/* 11Oct2009, Maiko, set_timer expects uint32, not atol () */
    set_timer(&Susptimer,(uint32)atoi(argv[1])*1000); /* set timer duration */

    start_timer(&Susptimer);                /* and fire it up */
    return 0;
}
  
/* The RRH timer controls the interval between Router-To-Router Hello
 * messages. These messages announce that your station is live and well
 * and that you are willing to exchange RSPF routing updates.
 */
static int
dorrhtimer(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(Rspfifaces == NULLRIFACE){
        j2tputs("RSPF is not active - define interface first.\n");
        return 0;
    }
    if(argc < 2){
        tprintf("RRH timer: %d/%d seconds\n",
        read_timer(&rrhtimer)/1000,
        dur_timer(&rrhtimer)/1000);
        return 0;
    }
    rrhtimer.func = rspfevent; /* what to call on timeout */
    rrhtimer.arg = (void *) &rrhtimer;
    set_timer(&rrhtimer,(uint32)atoi(argv[1])*1000); /* set timer duration */
    start_timer(&rrhtimer);         /* and fire it up */
    return 0;
}
  
/* This timer controls the interval between the RSPF routing updates. */
static int
dotimer(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(Rspfifaces == NULLRIFACE){
        j2tputs("RSPF is not active - define interface first.\n");
        return 0;
    }
    if(argc < 2){
        tprintf("RSPF update timer: %d/%d seconds\n",
        read_timer(&rspftimer)/1000,
        dur_timer(&rspftimer)/1000);
        return 0;
    }
    rspftimer.func = rspfevent; /* what to call on timeout */
    rspftimer.arg = (void *) &rspftimer;
    set_timer(&rspftimer,(uint32)atoi(argv[1])*1000); /* set timer duration */
    start_timer(&rspftimer);                /* and fire it up */
    return 0;
}
  
/* Called when either the RRH timer, the Update timer or the Suspect timer
 * expires.
 */
void
rspfevent(t)
void *t;
{
    int cmd;
    struct mbuf *bp;
    struct rspfadj *adj = NULLADJ;
    struct timer *tp;
    tp = (struct timer *) t;
    if(tp == &rrhtimer) {
        cmd = RSPFE_RRH;
        start_timer(tp);
    }
    else if(tp == &rspftimer) {
        cmd = RSPFE_UPDATE;
        start_timer(tp);
    }
    else {
        for(adj = Adjs; adj != NULLADJ; adj = adj->next)
            if(&adj->timer == tp)
                break;
        if(adj == NULLADJ)
            return;
        cmd = RSPFE_CHECK;
    }
    bp = ambufw(1+sizeof(int32));
    *bp->data = cmd;
    memcpy(bp->data + 1,&adj,sizeof(adj));
    bp->cnt = bp->size;
    enqueue(&Rspfinq,bp);
}
  
static int
domessage(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc > 2) {
        j2tputs("Usage: rspf message \"<your message>\"\n");
        return 0;
    }
  
    if(argc < 2) {
        if(Rrh_message != NULLCHAR)
            j2tputs(Rrh_message);
    }
    else {
        if(Rrh_message != NULLCHAR){
            free(Rrh_message);
            Rrh_message = NULLCHAR; /* reset the pointer */
        }
        if(!strlen(argv[1]))
            return 0;               /* clearing the buffer */
        Rrh_message = mallocw(strlen(argv[1])+5);/* allow for EOL */
        strcpy(Rrh_message, argv[1]);
        strcat(Rrh_message, INET_EOL);  /* add the EOL char */
    }
    return 0;
}
  
static int
domaxping(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setshort(&Rspfpingmax,"Max failed pings before deleting adjacency",
    argc,argv);
}
  
static int
domode(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2) {
        j2tputs("RSPF preferred mode is ");
        if(Rspfownmode == -1)
            j2tputs("not set.\n");
        else
            tprintf("%s.\n",(Rspfownmode & CONNECT_MODE) ? "VC mode" :
            "Datagram mode");
        return 0;
    }
    switch(*argv[1]){
        case 'v':
        case 'c':
        case 'V':
        case 'C':
            Rspfownmode = (int)CONNECT_MODE;
            break;
        case 'd':
        case 'D':
            Rspfownmode = (int)DATAGRAM_MODE;
            break;
        case 'n':
        case 'N':
            Rspfownmode = -1;
            break;
        default:
            j2tputs("Usage: rspf mode [vc | datagram | none]\n");
            return 1;
    }
    return 0;
}
  
#ifdef AUTOROUTE
int RspfActive = 0;
extern int Ax25_autoroute;
#endif
  
static int
dointerface(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct rspfiface *riface;
    struct iface *iface;
    struct mbuf *bp;
    int h,q;
    if(argc < 2){
        j2tputs("Iface    Quality    Horizon\n");
        for(riface = Rspfifaces; riface != NULLRIFACE; riface = riface->next)
            tprintf("%-9s%-11d%-11d\n",riface->iface->name,riface->quality,
            riface->horizon);
        return 0;
    }
    if(argc != 4){
        j2tputs("Usage: rspf interface <name> <quality> <horizon>\n");
        return 1;
    }
    if((iface = if_lookup(argv[1])) == NULLIF){
        j2tputs("No such interface.\n");
        return 1;
    }
    if(iface->broadcast == 0){
        tprintf("Broadcast address for interface %s not set\n",argv[1]);
        return 1;
    }
    q = atoi(argv[2]);
    if(q < 1 || q > 127){
        j2tputs("Quality must be between 1 and 127\n");
        return 1;
    }
    h = atoi(argv[3]);
    if(h < 1 || h > 255){
        j2tputs("Horizon must be between 1 and 255\n");
        return 1;
    }
    riface = (struct rspfiface *)callocw(1,sizeof(struct rspfiface));
    riface->iface = iface;
    riface->quality = q;
    riface->horizon = h;
    riface->next = Rspfifaces;
    if(Rspfifaces == NULLRIFACE) {
#ifdef AUTOROUTE
        RspfActive = 1;        /* Make sure ARP autorouting is off ! - WG7J */
        Ax25_autoroute = 0;
#endif
        newproc("RSPF",2048,rspfmain,0,NULL,NULL,0);
    }
    Rspfifaces = riface;
    bp = ambufw(1+sizeof(int32));
    *bp->data = RSPFE_RRH;              /* Send an RRH immediately */
    memcpy(bp->data + 1,&riface,sizeof(riface));
    bp->cnt = bp->size;
    enqueue(&Rspfinq,bp);
    return 0;
}
  
/* Display accumulated routing updates */
static int
doroutes(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbuf *bp;
    struct rspfrouter *rr;
    if(Rspfifaces == NULLRIFACE){
        j2tputs("RSPF is not active - define interface first.\n");
        return 0;
    }
    bp = makeownupdate(INADDR_ANY,0);
    if(bp == NULLBUF && Rspfrouters == NULLRROUTER) {
        j2tputs("No routing information is available.\n");
        return 0;
    }
    if(bp != NULLBUF) {
        j2tputs("      Local routing update:\n");
        rspfnodedump(Curproc->output,&bp,0);
        tputc('\n');
    }
    for(rr = Rspfrouters; rr != NULLRROUTER; rr = rr->next) {
        tprintf("      Time since receipt: %s",tformat(secclock() - rr->time));
        if(rr->subseq != 0)
            tprintf("  Last subseq: %u",uchar(rr->subseq));
        if(rr->sent)
            j2tputs("  Propagated");
        tputc('\n');
        if(rr->data != NULLBUF) {
            dup_p(&bp,rr->data,0,len_p(rr->data));
            rspfnodedump(Curproc->output,&bp,0);
            tputc('\n');
        }
    }
    return 0;
}
  
static int
dostatus(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct rspfreasm *re;
    struct rspfadj *adj;
    struct mbuf *bp;
    union rspf rspf;
    if(Rspfifaces == NULLRIFACE){
        j2tputs("RSPF is not active - define interface first.\n");
        return 0;
    }
    tprintf("Bad checksum %u  Bad version %u  Not RSPF interface %u\n",
    Rspf_stat.badcsum,Rspf_stat.badvers,Rspf_stat.norspfiface);
    tprintf("RRH in %u  RRH out %u  Update in %u  Update out %u\n",
    Rspf_stat.rrhin,Rspf_stat.rrhout,Rspf_stat.updatein,
    Rspf_stat.updateout);
    tprintf("Non-adjacency update %u  Old node report %u  Polls sent %u\n",
    Rspf_stat.noadjupdate,Rspf_stat.oldreport,Rspf_stat.outpolls);
    if(Adjs == NULLADJ)
        return 0;
    j2tputs("Addr            Cost    Seq    Heard    Timer     TOS    State\n");
    for(adj = Adjs; adj != NULLADJ; adj = adj->next) {
        /* tprintf("%-15s %4u  %5u   %6lu ", inet_ntoa(adj->addr) - %6lu does not match var, 05Dec2020 */
        tprintf("%-15s %4u  %5u   %6u ", inet_ntoa(adj->addr),
        uchar(adj->cost),adj->seq,adj->heard);
        if(run_timer(&adj->timer))
            tprintf("%5d/%-5d",
            read_timer(&adj->timer)/1000 ,dur_timer(&adj->timer)/1000);
        else
            tprintf("%11s","");
        tprintf("  %3u    ", uchar(adj->tos));
        switch(adj->state) {
            case RSPF_TENTATIVE:
                j2tputs("Tentative");
                break;
            case RSPF_OK:
                j2tputs("OK");
                break;
            case RSPF_SUSPECT:
                j2tputs("Suspect");
                break;
            case RSPF_BAD:
                j2tputs("Bad");
                break;
            default:
                j2tputs("Unknown");
                break;
        }
        tputc('\n');
    }
    if(run_timer(&Rspfreasmt)) {
        tprintf("Reassembly timer running: %d/%d seconds\n",
        read_timer(&Rspfreasmt)/1000, dur_timer(&Rspfreasmt)/1000);
    }
    if(Rspfreasmq != NULLRREASM)
        j2tputs("Reassembly fragments:\n");
    for(re = Rspfreasmq; re != NULLRREASM; re = re->next) {
        tprintf("src %s time since last frag %s",inet_ntoa(re->addr),
        tformat((secclock() - re->time)));
        if(dup_p(&bp,re->data,0,RSPFPKTLEN) == RSPFPKTLEN &&
            ntohrspf(&rspf,&bp) != -1)
            tprintf(" frag count %u/%u\n",len_q(re->data),
            rspf.pkthdr.fragtot);
        else {
            tputc('\n');
            free_p(bp);
            continue;
        }
    }
    return 0;
}
#endif /* RSPF */
  
