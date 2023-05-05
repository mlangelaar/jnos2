/* Mail Command Line Interface -- Clients
 * Copyright 1992 William Allen Simpson
 *      partly based on a MAIL client design by Anders Klemets, SM0RGV
 *
 * Mods by PA0GRI
 * Improved param cracking
 */
#include <ctype.h>
#include <time.h>
#include "global.h"
#include "timer.h"
#include "proc.h"
#include "socket.h"
#include "domain.h"
#include "cmdparse.h"
#include "files.h"
#include "netuser.h"
#include "mailcli.h"
#include "mailutil.h"
#include "smtp.h"
  
  
/* Tracing levels:
    0 - no tracing
    1 - serious errors reported
    2 - transient errors reported
    3 - session progress reported
 */
unsigned short Mailtrace = 1;
  
#ifdef MAILCLIENT
  
int Mailquiet = FALSE;
#ifdef LZW
int poplzw = TRUE;
#endif
  
struct mailservers *Mailservers = NULLMAIL;
  
static int domsquiet __ARGS((int argc,char *argv[],void *p));
static int domstrace __ARGS((int argc,char *argv[],void *p));
static int doadds __ARGS((int argc,char *argv[],void *p));
static int dodrops __ARGS((int argc,char *argv[],void *p));
static int dokicks __ARGS((int argc,char *argv[],void *p));
static int dolists __ARGS((int argc,char *argv[],void *p));
#ifdef LZW
static int dopoplzw __ARGS((int argc,char *argv[],void *p));
#endif
#ifdef POPT4
static int dopopt4 __ARGS((int argc,char *argv[],void *p));
#endif
  
static void mailtick __ARGS((void *tp));
  
static char mreaderr[] = "popmail: Missing";
  
static struct cmds DFAR Mailcmds[] = {
    { "addserver",    doadds,         0, 2, "popmail addserver <mailserver>"
    " [<seconds>] [hh:mm-hh:mm] "
#ifdef POP2CLIENT
    "pop2"
#endif
#ifdef POP3CLIENT
#ifdef POP2CLIENT
    "|"
#endif
    "pop3"
#endif
    " <mailbox> <username> <password>" },
    { "dropserver",   dodrops,        0, 2, "popmail dropserver <mailserver> [<username>]" },
    { "kick",         dokicks,        0, 2, "popmail kick <mailserver> [<username>]" },
    { "list",         dolists,        0, 0, NULLCHAR },
#ifdef LZW
    { "lzw",          dopoplzw,       0, 0, NULLCHAR },
#endif
    { "quiet",        domsquiet,      0, 0, NULLCHAR },
#ifdef POPT4
    { "t4",           dopopt4,        0, 0, NULLCHAR },
#endif
    { "trace",        domstrace,      0, 0, NULLCHAR },
    { NULLCHAR,		NULL,	0, 0, NULLCHAR }
};
  
  
#ifdef LZW
static
int dopoplzw(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&poplzw,"pop lzw",argc,argv);
}
#endif

int
domsread(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Mailcmds,argc,argv,p);
}
  
  
static int
domsquiet(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Mailquiet,"mail quiet",argc,argv);
}
  
#ifdef POPT4
int32 Popt4=0;
  
static int
dopopt4(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint32(&Popt4,"POP T4",argc,argv);
}
#endif /* POPT4 */
  
#endif /* MAILCLIENT */ 
  
static int
domstrace(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    return setshort(&Mailtrace,"mail tracing",argc,argv);
}
  
#ifdef MAILCLIENT

/* 29Dec2004, Replaces the GOTO 'quit' LABEL */
static int do_quit (struct mailservers *np)
{
    Mailservers = np->next;
    free(np->hostname);
    free(np->username);
    free(np->password);
    free(np->mailbox);
    free((char *)np);
    return -1;
}
  
static int
doadds(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mailservers *np;
    int i;
    int32 addr;
  
    if((addr = resolve(argv[1])) == 0L){
        tprintf("Unknown host %s\n",argv[1]);
        return 1;
    }
  
    i = 2;
    np = (struct mailservers *) callocw(1,sizeof(struct mailservers));
    np->hostname = j2strdup(argv[1]);
    np->next = Mailservers;
    Mailservers = np;
    np->lowtime = np->hightime = -1;
    np->timer.func = mailtick;  /* what to call on timeout */
    np->timer.arg = (void *)np;
  
    if( argc > i && isdigit(*argv[i])){
        if(strchr(argv[i],'-') == NULLCHAR )
            /* set timer duration */
            set_timer(&np->timer,(uint32)atoi(argv[i++])*1000);
    }
  
    if( argc > i && isdigit(*argv[i])){
        int lh, ll, hh, hl;
        sscanf(argv[i++], "%d:%d-%d:%d", &lh, &ll, &hh, &hl);
        np->lowtime = lh * 100 + ll;
        np->hightime = hh * 100 + hl;
    }
  
    if ( argc > i ) {
        struct daemon *dp = Mailreaders;
  
        for ( ; dp->name != NULLCHAR ; dp++ ) {
            if ( stricmp(dp->name, argv[i]) == 0 ) {
                np->reader = dp;
                break;
            }
        }
        if ( np->reader == NULLDAEMON ) {
            tprintf("unrecognized protocol '%s'\n", argv[i] );
            return (do_quit (np));
        }
        i++;
    } else {
        tprintf("%s protocol\n",mreaderr);
        return (do_quit (np));
    }
  
    if ( argc > i ) {
        np->mailbox = j2strdup(argv[i++]);
    } else {
        tprintf("%s mailbox\n",mreaderr);
        return (do_quit (np));
    }
  
    if ( argc > i ) {
        np->username = j2strdup(argv[i++]);
    } else {
        tprintf("%s username\n",mreaderr);
        return (do_quit (np));
    }
  
    if ( argc > i ) {
        np->password = j2strdup(argv[i++]);
    } else {
        char poppass[20];

        j2tputs("Enter POP password: ");
        tflush();
        if(recvline(Curproc->input, poppass, sizeof(poppass)) == -1)
        	return (do_quit (np));
        rip(poppass);
        np->password = j2strdup(poppass);
    }
  
    start_timer(&np->timer);            /* and fire it up */

    return 0;
}
  
static int
dodrops(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mailservers *np, *npprev = NULLMAIL;
  
    for (np = Mailservers; np != NULLMAIL; npprev = np, np = np->next) {
        if(stricmp(np->hostname,argv[1]) == 0) {
            if(argc>2 && stricmp(argv[2],np->username))
                continue;  /* server's username must match */
            stop_timer(&np->timer);
            free(np->hostname);
            free(np->username);
            free(np->password);
            free(np->mailbox);
  
            if(npprev != NULLMAIL)
                npprev->next = np->next;
            else
                Mailservers = np->next;
            free((char *)np);
            return 0;
        }
    }
    j2tputs("No such server enabled.\n");
    return -1;
}
  
  
static int
dolists(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mailservers *np;
  
    for (np = Mailservers; np != NULLMAIL; np = np->next) {
        char tbuf[80];
        if (np->lowtime != -1 && np->hightime != -1)
            sprintf(tbuf, " %02d:%02d-%02d:%02d",
                np->lowtime/100, np->lowtime%100,
                np->hightime/100, np->hightime%100);
        else
            tbuf[0] = '\0';
        tprintf("%-32s (%d/%d%s) %s %s\n", np->hostname,
            read_timer(&np->timer) / 1000, dur_timer(&np->timer) / 1000,
            tbuf, np->reader->name, np->username );
    }
    return 0;
}
  
  
static int
dokicks(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mailservers *np;
  
    for (np = Mailservers; np != NULLMAIL; np = np->next) {
        if(stricmp(np->hostname,argv[1]) == 0) {
            if(argc>2 && stricmp(argv[2],np->username))
                continue;  /* server's username must match */
            /* If the timer is not running, the timeout function
             * has already been called, and we don't want to call
             * it again.
             */
            if ( np->timer.duration == 0 || run_timer(&np->timer) ) {
                stop_timer(&np->timer);
                mailtick((void *)np);
            }
            return 0;
        }
    }
    j2tputs("No such server enabled.\n");
    return -1;
}
  
  
static void
mailtick(tp)
void *tp;
{
    struct mailservers *np = tp;
    struct tm *ltm;
    time_t t;
    int now;
  
#ifndef UNIX
    if(availmem() - np->reader->stksize < Memthresh){
        /* Memory is tight, don't do anything */
        if (Mailtrace >= 2)
            log(-1,"%s tick exit -- low memory", np->reader->name);
        start_timer(&np->timer);
        return;
    }
#endif
  
    time(&t);
    ltm = localtime(&t);
    now = ltm->tm_hour * 100 + ltm->tm_min;
    if (np->lowtime < np->hightime) {  /* doesn't cross midnight */
        if (now < np->lowtime || now >= np->hightime) {
            if (Mailtrace >= 2)
                log(-1,"%s window to '%s' not open",np->reader->name,np->hostname);
            start_timer(&np->timer);
            return;
        }
    } else {
        if (now < np->lowtime && now >= np->hightime) {
            if (Mailtrace >= 2)
                log(-1,"%s window to '%s' not open",np->reader->name,np->hostname);
            start_timer(&np->timer);
            return;
        }
    }
  
    if (newproc(np->reader->name, np->reader->stksize, np->reader->fp,0,tp,NULL,0) == NULLPROC)
        start_timer(&np->timer);
}
  
  
int
mailresponse(s,buf,comment)
int s;          /* Socket index */
char *buf;      /* User buffer */
char *comment;  /* comment for error message */
{
#ifdef POPT4
    j2alarm(Popt4*1000);
#endif
    if (recvline(s,buf,RLINELEN) != -1) {
#ifdef POPT4
        j2alarm(0);
#endif
        if ( Mailtrace >= 3 ) {
            rip(buf);
            log(s,"%s <== %s", comment, buf);
        }
        return 0;
    }
#ifdef POPT4
    j2alarm(0);
#endif
    if ( Mailtrace >= 2 )
        log(s,"receive error for %s response", comment);
    return -1;
}
  
  
/* Check to see if mailbox is already busy (or perpetually locked) */
int
mailbusy( np )
struct mailservers *np;
{
    int countdown = 10;
  
    while ( mlock( Mailspool, np->mailbox ) ) {
        if ( --countdown > 0 ) {
            j2pause( 60000 ); /* 60 seconds */
        } else {
            start_timer(&np->timer);
            return TRUE;
        }
    }
  
    /* release while processing */
    rmlock( Mailspool, np->mailbox );
    return FALSE;
}
  
#endif /* MAILCLIENT */
