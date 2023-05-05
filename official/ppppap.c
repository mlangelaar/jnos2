/*
 *  PPPPAP.C    -- Password Authentication Protocol for PPP
 *
 *  This implementation of PPP is declared to be in the public domain.
 *
 *  Jan 91  Bill_Simpson@um.cc.umich.edu
 *      Computer Systems Consulting Services
 *
 *  Acknowledgements and correction history may be found in PPP.C
 *
 * Mods by PA0GRI
 */
#include "global.h"
#ifdef PPP
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#include "session.h"
#include "socket.h"
#include "ppp.h"
#include "pppfsm.h"
#include "ppplcp.h"
#include "ppppap.h"
#include "cmdparse.h"
#include "files.h"
#include "trace.h"
#include "main.h"
  
static int dopap_user       __ARGS((int argc, char *argv[], void *p));
  
static void pap_monitor __ARGS((int mustask, void *v1, void *v2));
static void pap_pwdlookup __ARGS((struct pap_s *pap_p));
  
static struct mbuf *pap_makereq __ARGS((struct fsm_s *fsm_p));
  
static int pap_verify __ARGS((char *username, char *password));
static void pap_shutdown __ARGS((struct fsm_s *fsm_p));
static void pap_opening __ARGS((struct fsm_s *fsm_p, int flag));
  
static int pap_request  __ARGS((struct fsm_s *fsm_p,struct config_hdr *hdr,
    struct mbuf *data));
static int pap_check    __ARGS((struct fsm_s *fsm_p,struct config_hdr *hdr,
    struct mbuf *data));
static void pap_timeout __ARGS((void *vp));
  
static void pap_free    __ARGS((struct fsm_s *fsm_p));
  
  
static struct fsm_constant_s pap_constants = {
    "Pap",
    PPP_PAP_PROTOCOL,
    0x000E,             /* codes 1-3 recognized */
  
    Pap,
    PAP_REQ_TRY,
    PAP_FAIL_MAX,
    0,
    PAP_TIMEOUT * 1000L,
  
    pap_free,
  
    fsm_no_action,      /* pap_reset, */
    fsm_no_action,      /* pap_starting, */
    fsm_no_action,      /* pap_opening, */
    fsm_no_action,      /* pap_closing, */
    fsm_no_action,      /* pap_stopping, */
  
    pap_makereq,
    fsm_no_check,       /* pap_request, */
    fsm_no_check,       /* pap_ack, */
    fsm_no_check,       /* pap_nak, */
    fsm_no_check,       /* pap_reject */
};
  
  
/****************************************************************************/
  
/* "ppp <iface> pap" subcommands */
static struct cmds DFAR Papcmds[] = {
    { "timeout",  doppp_timeout,  0,  0,  NULLCHAR }, /* 05Jul2016, Maiko, compiler */
    { "try",      doppp_try,  0,  0,  NULLCHAR },
    { "user",     dopap_user, 0,  0,  NULLCHAR },
    { NULLCHAR, NULL, 0, 0, NULLCHAR }
};
  
  
int
doppp_pap(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct iface *ifp = p;
    register struct ppp_s *ppp_p = ifp->edv;
  
    return subcmd(Papcmds, argc, argv, &(ppp_p->fsm[Pap]));
}
  
  
/* Set user/password */
int
dopap_user(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct fsm_s *fsm_p = p;
    register struct pap_s *pap_p = fsm_p->pdv;
  
    if (argc < 2) {
        tprintf("%s\n",
        (pap_p->username == NULLCHAR) ? "None" : pap_p->username);
        return 0;
    }
    free(pap_p->username);
    pap_p->username = NULLCHAR;
    free(pap_p->password);
    pap_p->password = NULLCHAR;
  
    if (stricmp(argv[1],"none") != 0) {
        pap_p->username = j2strdup(argv[1]);
        if (argc > 2) {
            pap_p->password = j2strdup(argv[2]);
        } else {
            pap_pwdlookup( pap_p );
        }
    }
    return 0;
}
  
  
/****************************************************************************/
/* Bring up a session on the console for for the username/password.
 * Return a NULLCHAR in either username or password if aborted.
 */
static void
pap_monitor(unused, v1, v2)
int unused;
void *v1;
void *v2;
{
    struct iface *iface = v1;
    struct fsm_s *fsm_p = v2;
    struct pap_s *pap_p = fsm_p->pdv;
    char buf[21];
    struct session *sp;
    int wait_code = 0;
  
    /* Allocate a session control block */
    if((sp = newsession("PPP/PAP",PPPPASS,0)) == NULLSESSION){
        j2tputs(TooManySessions);
        return;
    }
  
    while ( !main_exit && wait_code == 0 ) {
        /* get user name */
        if (pap_p->username == NULLCHAR) {
            tprintf ("%s: PPP/PAP  Username: ", iface->name);
            usflush(sp->output);
            if (recvline(sp->input,buf,20) > 0) {
                rip(buf);
                if (strlen(buf) > 0) {
                    pap_p->username = j2strdup(buf);
                }
            }
        } else {
            tprintf ("%s: PPP/PAP  Username: %s\n",
            iface->name, pap_p->username);
            usflush(sp->output);
        }
  
        /* get pass word */
        if (pap_p->username != NULLCHAR
        && pap_p->password == NULLCHAR) {
            /* turn off echo */
            sp->ttystate.echo = 0;
            tprintf("%s: PPP/PAP  Password: ",iface->name);
            usflush(sp->output);
            if (recvline(sp->input,buf,20) > 0) {
                rip(buf);
                if ( strlen(buf) > 0 ) {
                    pap_p->password = j2strdup(buf);
                }
            }
            tputc('\n');
            usflush(sp->output);
            /* Turn echo back on */
            sp->ttystate.echo = 1;
        }
  
        /* send pap request */
        fsm_sendreq(fsm_p);
        wait_code = pwait ( pap_p );
  
        /* show ack/nak reply */
        if ( wait_code != EABORT && pap_p->message != NULLCHAR ) {
            tprintf ("%s: PPP/PAP  %s\n",
            iface->name, pap_p->message );
        }
        tputc('\n');
        usflush(sp->output);
  
    }
  
    /* clean up */
    if ( wait_code != EABORT ) {
        j2pause ( 10000L );
    }
    freesession(sp);
    pap_p->pp = NULLPROC;
}
  
  
/* Check the FTP userfile for this user; get password if available */
static void
pap_pwdlookup(pap_p)
struct pap_s *pap_p;
{
    char *buf;
    char *password;
    long permission;
  
    if ( pap_p->username == NULLCHAR )
        return;
  
    if ( (buf = userlookup( pap_p->username, &password, NULLCHARP,
        &permission, NULL )) == NULLCHAR )
        return;
  
    /* Check permissions for this user */
    if ( (permission & PPP_PWD_LOOKUP) == 0 ) {
        /* Not in ftpuser file for password lookup */
        free(buf);
        return;
    }
  
    /* Save the password from this userfile record */
    if ( strlen(password) != 0 )
        pap_p->password = j2strdup(password);
    free(buf);
}
  
  
/*******************************************/
/* Verify user and password sent by remote host */
static int
pap_verify(username,password)
char *username;
char *password;
{
    long privs;
    char *path;
    int anony = 0;
  
    /* Use same login as FTP server */
    path = mallocw(FILE_PATH_SIZE);
    privs = userlogin(username,password,&path,FILE_PATH_SIZE,&anony,"pppperm");
    free(path);
  
    /* Check privs for this user */
    if (privs == -1) {
        trace_log(PPPiface,"PAP: username/password incorrect or not found: %s",
        username);
        return -1;
    }
  
    if ((privs & PPP_ACCESS_PRIV) == 0) {
        trace_log(PPPiface,"PAP: no permission for PPP access: %s",
        username);
        return -1;
    }
    return 0;
}
  
  
/****************************************************************************/
/* Build a request to send to remote host */
static struct mbuf *
pap_makereq(fsm_p)
struct fsm_s *fsm_p;
{
    struct pap_s *pap_p = fsm_p->pdv;
    struct mbuf *req_bp = NULLBUF;
    register char *cp;
    int len;
  
    PPP_DEBUG_ROUTINES("pap_makereq()");
  
    if ( pap_p->username == NULLCHAR
    ||  pap_p->password == NULLCHAR ) {
        fsm_log( fsm_p, "NULL username or password" );
        return NULLBUF;
    }
  
#ifdef PPP_DEBUG_OPTIONS
    if (PPPtrace & PPP_DEBUG_OPTIONS)
        trace_log(PPPiface, "    making user id %s", pap_p->username);
#endif
  
    /* Get buffer for authenticate request packet */
    len = 2 + strlen(pap_p->username) + strlen(pap_p->password);
    if ((req_bp = alloc_mbuf(len)) == NULLBUF)
        return NULLBUF;
  
    /* Load user id and password for authenticate packet */
    cp = req_bp->data;
    *cp++ = (char)strlen(pap_p->username);
    if ( strlen(pap_p->username) > 0 )
        cp = stpcpy(cp, pap_p->username);
  
    *cp++ = (char)strlen(pap_p->password);
    if ( strlen(pap_p->password) > 0 )
        cp = stpcpy(cp, pap_p->password);
  
    req_bp->cnt += len;
    return(req_bp);
}
  
  
/****************************************************************************/
  
/* abandon PAP attempt; shutdown LCP layer */
static void
pap_shutdown(fsm_p)
struct fsm_s *fsm_p;
{
    struct ppp_s *ppp_p = fsm_p->ppp_p;
  
    PPP_DEBUG_ROUTINES("pap_shutdown()");
  
    if (PPPtrace > 1)
        fsm_log( fsm_p, "Failed; close connection" );
  
    fsm_close( &(ppp_p->fsm[Lcp]) );
}
  
  
/* Configuration negotiation complete */
static void
pap_opening(fsm_p, flag)
struct fsm_s *fsm_p;
int flag;
{
    register struct ppp_s *ppp_p = fsm_p->ppp_p;
  
    fsm_log(fsm_p, "Open");
  
    stop_timer(&(fsm_p->timer));
  
    if ( !((fsm_p->flags &= ~flag) & (PPP_AP_LOCAL | PPP_AP_REMOTE)) ) {
        fsm_p->state = fsmOPENED;
    }
    ppp_p->flags &= ~flag;
    ppp_ready(ppp_p);
}
  
  
/****************************************************************************/
/* Check request from remote host */
static int
pap_request(fsm_p, hdr, data)
struct fsm_s *fsm_p;
struct config_hdr *hdr;
struct mbuf *data;
{
    struct mbuf *reply_bp;
    int result;
    char *message;
    int mess_length;
    char *username = NULLCHAR;
    int userlen;
    char *password = NULLCHAR;
    int passwordlen;
  
    PPP_DEBUG_ROUTINES("pap_request()");
  
    /* Extract userID/password sent by remote host */
    if ( (userlen = pullchar(&data)) != -1 ) {
        register int i;
        register char *cp;
  
        cp = username = mallocw(userlen+1);
        for ( i = userlen; i-- > 0; ) {
            *cp++ = PULLCHAR(&data);
        }
        *cp = '\0';
    }
  
#ifdef PPP_DEBUG_OPTIONS
    if (PPPtrace & PPP_DEBUG_OPTIONS)
        trace_log(PPPiface,"    checking user: %s", username);
#endif
  
    if ( (passwordlen = pullchar(&data)) != -1 ) {
        register int i;
        register char *cp;
  
        cp = password = mallocw(passwordlen+1);
        for ( i = passwordlen; i-- > 0; ) {
            *cp++ = PULLCHAR(&data);
        }
        *cp = '\0';
    }
  
#ifdef PPP_DEBUG_OPTIONS
    if (PPPtrace & PPP_DEBUG_OPTIONS)
        trace_log(PPPiface,"    checking password: %s", password);
#endif
  
    if (pap_verify(username,password) == 0) {
        free( fsm_p->ppp_p->peername );
        fsm_p->ppp_p->peername = j2strdup(username);
        result = CONFIG_ACK;
        message = " Welcome";
    } else {
        result = CONFIG_NAK;
        message = " Invalid username or password";
    }
  
    /* the space at the beginning of the message is crucial */
    /* it is replaced with the length of the message */
    mess_length = strlen(message);
    reply_bp = qdata(message,mess_length);
    reply_bp->data[0] = (char)(mess_length - 1);
  
    fsm_send(fsm_p, result, hdr->id, reply_bp);
  
    if (result == CONFIG_NAK) {
        if ( fsm_p->retry_nak > 0 ) {
            fsm_p->retry_nak--;
        } else {
            pap_shutdown(fsm_p);
        }
    }
    free_p(data);
    free(username);
    free(password);
    return (result != CONFIG_ACK);
}
  
  
/* Check acknowledgement from remote host */
static int
pap_check(fsm_p, hdr, data)
struct fsm_s *fsm_p;
struct config_hdr *hdr;
struct mbuf *data;
{
    struct pap_s *pap_p = fsm_p->pdv;
    char *message;
    int mess_length;
    int full_length;
    int len;
  
    PPP_DEBUG_ROUTINES("pap_check()");
  
    /* ID field must match last request we sent */
    if (hdr->id != fsm_p->lastid) {
        PPP_DEBUG_CHECKS("PAP: wrong ID");
        tprintf ("id mismatch hdrid=%d, lastid=%d\n",
        hdr->id, fsm_p->lastid);
        free_p(data);
        return -1;
    }
  
    /* Log ASCII message from remote host, if any */
    if ( (mess_length = pullchar(&data)) != -1 ) {
        message = mallocw( mess_length+1 );
        full_length = len_p(data);
        len = dqdata(data, message, mess_length);
        message[len] = '\0';
  
        free( pap_p->message );
        pap_p->message = message;
  
        if (PPPtrace) {
            trace_log(PPPiface,"%s PPP/PAP %s %s: %s",
            fsm_p->ppp_p->iface->name,
            (len < mess_length) ? "Short"
            : (mess_length < full_length) ? "Long"
            : "Valid",
            (hdr->code == CONFIG_ACK) ? "Ack" : "Nak",
            message);
        }
        return (len < mess_length  ||  mess_length < full_length);
    }
    free_p(data);
    PPP_DEBUG_CHECKS( "PAP: missing message count" );
    return -1;
}
  
  
/************************************************************************/
/*          E V E N T   P R O C E S S I N G         */
/************************************************************************/
  
/* Process incoming packet */
void
pap_proc(fsm_p,bp)
struct fsm_s *fsm_p;
struct mbuf *bp;
{
    struct pap_s *pap_p = fsm_p->pdv;
    struct config_hdr hdr;
  
    PPPtrace = fsm_p->ppp_p->trace;
    PPPiface = fsm_p->ppp_p->iface;
  
    if ( ntohcnf(&hdr, &bp) == -1 )
        fsm_log( fsm_p, "short authentication packet" );
  
    if (PPPtrace > 1)
        trace_log(PPPiface, "%s PPP/%s Recv,"
        "  option: %s, id: %d, len: %d",
        fsm_p->ppp_p->iface->name,
        fsm_p->pdc->name,
        fsmCodes[hdr.code],
        hdr.id, hdr.len);
  
    hdr.len -= CONFIG_HDR_LEN;      /* Length includes envelope */
    trim_mbuf(&bp, hdr.len);        /* Trim off padding */
  
    switch(hdr.code) {
        case CONFIG_REQ:
            if ( pap_request(fsm_p, &hdr, bp) == 0) {
                pap_opening(fsm_p, PPP_AP_LOCAL);
            }
            break;
  
        case CONFIG_ACK:
            if (pap_check(fsm_p, &hdr, bp) == 0) {
                alert ( pap_p->pp, -1 );
                pap_opening(fsm_p, PPP_AP_REMOTE);
            }
            break;
  
        case CONFIG_NAK:
            if (pap_check(fsm_p, &hdr, bp) == 0) {
                stop_timer(&(fsm_p->timer));
  
            /* Must have sent a bad username or password */
                free ( pap_p->username );
                pap_p->username = NULLCHAR;
                free ( pap_p->password );
                pap_p->password = NULLCHAR;
  
                j2psignal ( pap_p, 1 );
            }
            break;
  
        default:
            if (PPPtrace)
                trace_log(PPPiface, "%s PPP/Pap Unknown packet type: %d;"
                " dropping packet",
                fsm_p->ppp_p->iface->name,
                hdr.code);
            free_p(bp);
            break;
    }
}
  
  
/* Timeout while waiting for reply from remote host */
static void
pap_timeout(vp)
void *vp;
{
    struct fsm_s *fsm_p = (struct fsm_s *)vp;
    struct pap_s *pap_p = fsm_p->pdv;
  
    PPPtrace = fsm_p->ppp_p->trace;
    PPPiface = fsm_p->ppp_p->iface;
  
    fsm_log( fsm_p, "Timeout" );
  
    if (fsm_p->retry > 0) {
        free ( pap_p->message );
        pap_p->message = j2strdup("Request timeout");
        j2psignal ( pap_p, 1 );
    } else {
        free ( pap_p->message );
        pap_p->message = j2strdup("Request retry exceeded");
        j2psignal ( pap_p, 1 );
        pwait ( NULL );
        fsm_log(fsm_p, "Request retry exceeded");
        pap_shutdown(fsm_p);
    }
}
  
  
/************************************************************************/
/*          I N I T I A L I Z A T I O N         */
/************************************************************************/
  
void
pap_down(fsm_p)
struct fsm_s *fsm_p;
{
    struct pap_s *pap_p = fsm_p->pdv;
  
    if ( pap_p == NULL )
        return;
  
    PPPtrace = fsm_p->ppp_p->trace;
    PPPiface = fsm_p->ppp_p->iface;
  
    fsm_log(fsm_p, "Down");
  
    fsm_p->flags = FALSE;
  
    switch ( fsm_p->state ) {
        case fsmREQ_Sent:
            stop_timer(&(fsm_p->timer));
            alert ( pap_p->pp, EABORT );
        /* fallthru */
        case fsmOPENED:
        case fsmLISTEN:
        case fsmTERM_Sent:
            fsm_p->state = fsmCLOSED;
            break;
  
        case fsmCLOSED:
        /* Already closed; nothing to do */
            break;
    };
}
  
  
static void
pap_free(fsm_p)
struct fsm_s *fsm_p;
{
    struct pap_s *pap_p = fsm_p->pdv;
  
    free( pap_p->username );
    free( pap_p->password );
    free( pap_p->message );
}
  
  
/* Initialize configuration structure */
void
pap_init(ppp_p)
struct ppp_s *ppp_p;
{
    struct fsm_s *fsm_p = &(ppp_p->fsm[Pap]);
    struct timer *t;
  
    PPPtrace = ppp_p->trace;
    PPPiface = ppp_p->iface;
  
    PPP_DEBUG_ROUTINES("pap_init()");
  
    if (fsm_p->pdv != NULL)
        return;     /* already initialized */
  
    fsm_p->ppp_p = ppp_p;
    fsm_p->pdc = &pap_constants;
    fsm_p->pdv = callocw(1,sizeof(struct pap_s));
  
    fsm_p->try_req = fsm_p->pdc->try_req;
    fsm_p->try_nak = fsm_p->pdc->try_nak;
    fsm_p->try_terminate = fsm_p->pdc->try_terminate;
  
    fsm_p->state = fsmCLOSED;
    fsm_p->retry = fsm_p->try_req;
    fsm_p->retry_nak = fsm_p->try_nak;
  
    /* Initialize timer */
    t = &(fsm_p->timer);
    t->func = (void (*)__ARGS((void*)))pap_timeout;
    t->arg = (void *)fsm_p;
    set_timer(t, fsm_p->pdc->timeout);
    fsm_timer(fsm_p);
    stop_timer(t);
}
  
  
/* Initialize state machine for local */
int
pap_local(ppp_p)
struct ppp_s *ppp_p;
{
    struct fsm_s *fsm_p = &(ppp_p->fsm[Pap]);
  
    PPPtrace = ppp_p->trace;
  
    PPP_DEBUG_ROUTINES("pap_local()");
  
    fsm_p->state = fsmLISTEN;
    fsm_p->flags |= PPP_AP_LOCAL;
    ppp_p->flags |= PPP_AP_LOCAL;
    fsm_p->retry = fsm_p->try_req;
    return 0;
}
  
  
/* Initialize state machine for remote */
int
pap_remote(ppp_p)
struct ppp_s *ppp_p;
{
    struct fsm_s *fsm_p = &(ppp_p->fsm[Pap]);
    struct pap_s *pap_p = fsm_p->pdv;
    char *ifn;
  
    PPPtrace = ppp_p->trace;
  
    PPP_DEBUG_ROUTINES("pap_remote()");
  
    fsm_p->state = fsmREQ_Sent;
    fsm_p->flags |= PPP_AP_REMOTE;
    ppp_p->flags |= PPP_AP_REMOTE;
  
    /* build a process/session to monitor user/password progress */
    ifn = if_name( ppp_p->iface, " PAP" );
    pap_p->pp = newproc( ifn,
    512, pap_monitor, 0, ppp_p->iface, fsm_p, 0);
    free( ifn );
  
    return 0;
}
#endif /* PPP */
  
