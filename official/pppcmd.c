/*
 *  PPPCMD.C    -- PPP related user commands
 *
 *  This implementation of PPP is declared to be in the public domain.
 *
 *  Jan 91  Bill_Simpson@um.cc.umich.edu
 *      Computer Systems Consulting Services
 *
 *  Acknowledgements and correction history may be found in PPP.C
 */
  
#include "global.h"
#ifdef PPP
#include "mbuf.h"
#include "iface.h"
#include "pktdrvr.h"
#include "ppp.h"
#include "pppfsm.h"
#include "ppplcp.h"
#include "ppppap.h"
#include "pppipcp.h"
#include "cmdparse.h"
  
static struct iface *ppp_lookup __ARGS((char *ifname));
  
static int doppp_quick      __ARGS((int argc, char *argv[], void *p));
static int doppp_trace      __ARGS((int argc, char *argv[], void *p));
  
static int spot __ARGS((int16 work,int16 want,int16 will,int16 mask));
static void genstat     __ARGS((struct ppp_s *ppp_p));
static void lcpstat     __ARGS((struct fsm_s *fsm_p));
static void papstat     __ARGS((struct fsm_s *fsm_p));
static void ipcpstat        __ARGS((struct fsm_s *fsm_p));
  
static int dotry_nak        __ARGS((int argc, char *argv[], void *p));
static int dotry_req        __ARGS((int argc, char *argv[], void *p));
static int dotry_terminate  __ARGS((int argc, char *argv[], void *p));
  
  
/* "ppp" subcommands */
static struct cmds DFAR Pppcmds[] = {
    { "ipcp",     doppp_ipcp, 0,  0,  NULLCHAR },	/* 05Jul2016, Maiko, compiler */
    { "lcp",      doppp_lcp,  0,  0,  NULLCHAR },
    { "pap",      doppp_pap,  0,  0,  NULLCHAR },
    { "quick",    doppp_quick,    0,  0,  NULLCHAR },
    { "trace",    doppp_trace,    0,  0,  NULLCHAR },
    { NULLCHAR,	NULL, 0, 0, NULLCHAR }
};
  
/* "ppp <iface> <ncp> try" subcommands */
static struct cmds DFAR PppTrycmds[] = {	/* 05Jul2016, Maiko, compiler */
    { "configure",    dotry_req,  0,  0,  NULLCHAR },
    { "failure",  dotry_nak,  0,  0,  NULLCHAR },
    { "terminate",    dotry_terminate,    0,  0,  NULLCHAR },
    { NULLCHAR, NULL, 0, 0, NULLCHAR }
};
  
static char *PPPStatus[] = {
    "Physical Line Dead",
    "Establishment Phase",
    "Authentication Phase",
    "Network Protocol Phase",
    "Termination Phase"
};
  
static char *NCPStatus[] = {
    "Closed",
    "Listening -- waiting for remote host to attempt open",
    "Starting configuration exchange",
    "Remote host accepted our request; waiting for remote request",
    "We accepted remote request; waiting for reply to our request",
    "Opened",
    "Terminate request sent to remote host"
};
  
int PPPtrace;
struct iface *PPPiface;  /* iface for trace */
  
  
/****************************************************************************/
  
static struct iface *
ppp_lookup(ifname)
char *ifname;
{
    register struct iface *ifp;
  
    if ((ifp = if_lookup(ifname)) == NULLIF) {
        tprintf("%s: Interface unknown\n",ifname);
        return(NULLIF);
    }
    if (ifp->type != CL_PPP) {
        tprintf("%s: not a PPP interface\n",ifp->name);
        return(NULLIF);
    }
    return(ifp);
}
  
/****************************************************************************/
  
int
doppp_commands(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct iface *ifp;
  
    if (argc < 2) {
        j2tputs( "ppp <iface> required\n" );
        return -1;
    }
    if ((ifp = ppp_lookup(argv[1])) == NULLIF)
        return -1;
  
    if ( argc == 2 ) {
        ppp_show( ifp );
        return 0;
    }
  
    return subcmd(Pppcmds, argc - 1, &argv[1], ifp);
}
  
  
/* Close connection on PPP interface */
int
doppp_close(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct fsm_s *fsm_p = p;
  
    fsm_p->flags &= ~(FSM_ACTIVE | FSM_PASSIVE);
  
    fsm_close( fsm_p );
    return 0;
}
  
  
int
doppp_passive(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct fsm_s *fsm_p = p;
  
    fsm_p->flags &= ~FSM_ACTIVE;
    fsm_p->flags |= FSM_PASSIVE;
  
    fsm_start(fsm_p);
    return 0;
}
  
  
int
doppp_active(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct fsm_s *fsm_p = p;
  
    fsm_p->flags &= ~FSM_PASSIVE;
    fsm_p->flags |= FSM_ACTIVE;
  
    if ( fsm_p->state < fsmLISTEN ) {
        fsm_p->state = fsmLISTEN;
    }
    return 0;
}
  
  
static int
doppp_quick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct iface *ifp = p;
    register struct ppp_s *ppp_p = ifp->edv;
    struct lcp_s *lcp_p = ppp_p->fsm[Lcp].pdv;
    struct ipcp_s *ipcp_p = ppp_p->fsm[IPcp].pdv;
  
    lcp_p->local.want.accm = 0L;
    lcp_p->local.want.negotiate |= LCP_N_ACCM;
    lcp_p->local.want.magic_number += (long)&lcp_p->local.want.magic_number;
    lcp_p->local.want.negotiate |= LCP_N_MAGIC;
    lcp_p->local.want.negotiate |= LCP_N_ACFC;
    lcp_p->local.want.negotiate |= LCP_N_PFC;
  
    ipcp_p->local.want.compression = PPP_COMPR_PROTOCOL;
    ipcp_p->local.want.slots = 16;
    ipcp_p->local.want.slot_compress = 1;
    ipcp_p->local.want.negotiate |= IPCP_N_COMPRESS;
    doppp_active( 0, NULL, &(ppp_p->fsm[IPcp]) );
  
    return 0;
}
  
  
/****************************************************************************/
  
void
ppp_show(ifp)
struct iface *ifp;
{
    register struct ppp_s *ppp_p = ifp->edv;
  
    genstat(ppp_p);
    if ( ppp_p->fsm[Lcp].pdv != NULL )
        lcpstat(&(ppp_p->fsm[Lcp]));
    if ( ppp_p->fsm[Pap].pdv != NULL )
        papstat(&(ppp_p->fsm[Pap]));
    if ( ppp_p->fsm[IPcp].pdv != NULL )
        ipcpstat(&(ppp_p->fsm[IPcp]));
}
  
  
static void
genstat(ppp_p)
register struct ppp_s *ppp_p;
{
  
    j2tputs(PPPStatus[ppp_p->phase]);
  
    if (ppp_p->phase == pppREADY) {
        tprintf("\t(open for %s)",
        tformat(secclock() - ppp_p->upsince));
    }
    tputc('\n');
  
    tprintf("%10u In,  %10u Flags,%6u ME, %6u FE, %6u CSE, %6u other\n",
    ppp_p->InRxOctetCount,
    ppp_p->InOpenFlag,
    ppp_p->InMemory,
    ppp_p->InFrame,
    ppp_p->InChecksum,
    ppp_p->InError);
    tprintf("\t\t%6u Lcp,%6u Pap,%6u IPcp,%6u Unknown\n",
    ppp_p->InNCP[Lcp],
    ppp_p->InNCP[Pap],
    ppp_p->InNCP[IPcp],
    ppp_p->InUnknown);
    tprintf("%10u Out, %10u Flags,%6u ME, %6u Fail\n",
    ppp_p->OutTxOctetCount,
    ppp_p->OutOpenFlag,
    ppp_p->OutMemory,
    ppp_p->OutError);
    tprintf("\t\t%6u Lcp,%6u Pap,%6u IPcp\n",
    ppp_p->OutNCP[Lcp],
    ppp_p->OutNCP[Pap],
    ppp_p->OutNCP[IPcp]);
}
  
  
static int
spot(work,want,will,mask)
int16 work;
int16 want;
int16 will;
int16 mask;
{
    char blot = ' ';
    int result = (work & mask);
  
    if ( !(will & mask) ) {
        blot = '*';
    } else if ( (want ^ work) & mask ) {
        blot = (result ? '+' : '-');
    }
    tputc( blot );
    return result;
}
  
static void
lcpstat(fsm_p)
struct fsm_s *fsm_p;
{
    struct lcp_s *lcp_p = fsm_p->pdv;
    struct lcp_value_s *localp = &(lcp_p->local.work);
    int16  localwork = lcp_p->local.work.negotiate;
    int16  localwant = lcp_p->local.want.negotiate;
    int16  localwill = lcp_p->local.will_negotiate;
    struct lcp_value_s *remotep = &(lcp_p->remote.work);
    int16  remotework = lcp_p->remote.work.negotiate;
    int16  remotewant = lcp_p->remote.want.negotiate;
    int16  remotewill = lcp_p->remote.will_negotiate;
  
    tprintf("LCP %s\n",
    NCPStatus[fsm_p->state]);
  
    tprintf("\t\t MRU\t ACCM\t\t AP\t PFC  ACFC Magic\n");
  
    j2tputs("\tLocal:\t");
  
    spot( localwork, localwant, localwill, LCP_N_MRU );
    tprintf( "%4d\t", localp->mru );
  
    spot( localwork, localwant, localwill, LCP_N_ACCM );
    tprintf( "0x%08x\t", localp->accm );
  
    if ( spot( localwork, localwant, localwill, LCP_N_AUTHENT ) ) {
        switch ( localp->authentication ) {
            case PPP_PAP_PROTOCOL:
                j2tputs( "Pap\t" );
                break;
            default:
                tprintf( "0x%04x\t", localp->authentication);
                break;
        };
    } else {
        j2tputs( "None\t" );
    }
  
    tprintf( spot( localwork, localwant, localwill, LCP_N_PFC )
    ? "Yes " : "No  " );
    tprintf( spot( localwork, localwant, localwill, LCP_N_ACFC )
    ? "Yes " : "No  " );
  
    spot( localwork, localwant, localwill, LCP_N_MAGIC );
    if ( localp->magic_number != 0L ) {
        tprintf( "0x%08x\n", localp->magic_number );
    } else {
        j2tputs( "unused\n" );
    }
  
    j2tputs("\tRemote:\t");
  
    spot( remotework, remotewant, remotewill, LCP_N_MRU );
    tprintf( "%4d\t", remotep->mru );
  
    spot( remotework, remotewant, remotewill, LCP_N_ACCM );
    tprintf( "0x%08x\t", remotep->accm );
  
    if ( spot( remotework, remotewant, remotewill, LCP_N_AUTHENT ) ) {
        switch ( remotep->authentication ) {
            case PPP_PAP_PROTOCOL:
                j2tputs( "Pap\t" );
                break;
            default:
                tprintf( "0x%04x\t", remotep->authentication);
                break;
        };
    } else {
        j2tputs( "None\t" );
    }
  
    tprintf( spot( remotework, remotewant, remotewill, LCP_N_PFC )
    ? "Yes " : "No  " );
    tprintf( spot( remotework, remotewant, remotewill, LCP_N_ACFC )
    ? "Yes " : "No  " );
  
    spot( remotework, remotewant, remotewill, LCP_N_MAGIC );
    if ( remotep->magic_number != 0L ) {
        tprintf( "0x%08x\n", remotep->magic_number );
    } else {
        j2tputs( "unused\n" );
    }
}
  
  
static void
papstat(fsm_p)
struct fsm_s *fsm_p;
{
    struct pap_s *pap_p = fsm_p->pdv;
  
    tprintf("PAP %s\n",
    NCPStatus[fsm_p->state]);
  
    tprintf( "\tMessage: '%s'\n", (pap_p->message == NULL) ?
    "none" : pap_p->message );
}
  
  
static void
ipcpstat(fsm_p)
struct fsm_s *fsm_p;
{
    struct ipcp_s *ipcp_p = fsm_p->pdv;
    struct ipcp_value_s *localp = &(ipcp_p->local.work);
    int16  localwork = ipcp_p->local.work.negotiate;
    struct ipcp_value_s *remotep = &(ipcp_p->remote.work);
    int16  remotework = ipcp_p->remote.work.negotiate;
  
    tprintf("IPCP %s\n",
    NCPStatus[fsm_p->state]);
    tprintf("\tlocal IP address: %s",
    inet_ntoa(localp->address));
    tprintf("  remote IP address: %s\n",
    inet_ntoa(localp->other));
  
    if (localwork & IPCP_N_COMPRESS) {
        tprintf("    In\tTCP header compression enabled:"
        " slots = %d, flag = 0x%02x\n",
        localp->slots,
        localp->slot_compress);
        slhc_i_status(ipcp_p->slhcp);
    }
  
    if (remotework & IPCP_N_COMPRESS) {
        tprintf("    Out\tTCP header compression enabled:"
        " slots = %d, flag = 0x%02x\n",
        remotep->slots,
        remotep->slot_compress);
        slhc_o_status(ipcp_p->slhcp);
    }
}
  
  
/****************************************************************************/
/* Set timeout interval when waiting for response from remote peer */
int
doppp_timeout(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct fsm_s *fsm_p = p;
    struct timer *t = &(fsm_p->timer);
  
    if (argc < 2) {
        tprintf("%d\n",dur_timer(t)/1000);
    } else {
        int x = (int)strtol( argv[1], NULLCHARP, 0 );
  
        if (x <= 0) {
            tprintf("Timeout value %s (%d) must be > 0\n",
            argv[1], x);
            return -1;
        } else {
            set_timer(t, x * 1000L);
        }
    }
    return 0;
}
  
  
int
doppp_try(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(PppTrycmds, argc, argv, p);
}
  
  
static int
dotry_nak(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct fsm_s *fsm_p = p;
  
    if (argc < 2) {
        tprintf("%d\n",fsm_p->try_nak);
    } else {
        int x = (int)strtol( argv[1], NULLCHARP, 0 );
  
        if (x <= 0) {
            tprintf("Value %s (%d) must be > 0\n",
            argv[1], x);
            return -1;
        } else {
            fsm_p->try_nak = x;
        }
    }
    return 0;
}
  
  
static int
dotry_req(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct fsm_s *fsm_p = p;
  
    if (argc < 2) {
        tprintf("%d\n",fsm_p->try_req);
    } else {
        int x = (int)strtol( argv[1], NULLCHARP, 0 );
  
        if (x <= 0) {
            tprintf("Value %s (%d) must be > 0\n",
            argv[1], x);
            return -1;
        } else {
            fsm_p->try_req = x;
        }
    }
    return 0;
}
  
  
static int
dotry_terminate(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct fsm_s *fsm_p = p;
  
    if (argc < 2) {
        tprintf("%d\n",fsm_p->try_terminate);
    } else {
        int x = (int)strtol( argv[1], NULLCHARP, 0 );
  
        if (x <= 0) {
            tprintf("Value %s (%d) must be > 0\n",
            argv[1], x);
            return -1;
        } else {
            fsm_p->try_terminate = x;
        }
    }
    return 0;
}
  
  
static int
doppp_trace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct iface *ifp = p;
    register struct ppp_s *ppp_p = ifp->edv;
    int tracing = ppp_p->trace;
    int result = setint(&tracing,"PPP tracing",argc,argv);
  
    ppp_p->trace = tracing;
    return result;
}
  
#endif /* PPP */
