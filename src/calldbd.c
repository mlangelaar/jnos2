/* Callbook server */
#include "global.h"
#if defined CALLSERVER || defined SAMCALLB || defined QRZCALLB || defined BUCKTSR
#include "files.h"
#include "mbuf.h"
#include "socket.h"
#include "session.h"
#include "proc.h"
#include "dirutil.h"
#include "commands.h"
#include "mailbox.h"
#include "config.h"

extern char *Callserver;  /* buckbook.c */

static void calldbd __ARGS((int s,void *unused,void *p));

#if defined(SAMCALLB) || defined(QRZCALLB)
int cb_lookup __ARGS((int s,char *call,FILE *fp));
#else
int cb_lookup(int s,char *call);  /* in buckbook.c  */
#endif

/* Start up callsign database service */
int
cdbstart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

    if(argc < 2)
        port = IPPORT_CALLDB;
    else
        port = atoi(argv[1]);

    return start_tcp(port,"CALL Server",calldbd,1024);
}

int
cdb0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

    if(argc < 2)
        port = IPPORT_CALLDB;
    else
        port = atoi(argv[1]);

    return stop_tcp(port);
}

static void
calldbd(s,unused,p)
int s;
void *unused;
void *p;
{
    char user[80];
    int err;

    sockmode(s,SOCK_ASCII);
    sockowner(s,Curproc);
    recvline(s,user,80);
    rip(user);
    log(s,"Callbook lookup: %s",user);
    if(strlen(user) == 0)
    {
        usputs(s,"No Callbook information available\n");
    }
    else
    {
#if defined(SAMCALLB) || defined(QRZCALLB)
        if ((err = cb_lookup(s,user,(FILE *) 0)) != 0)
            usprintf(s,(err == 2) ? "No Callbook information available\n" : "Callbook not active\n");
#else
        cb_lookup(s,user);
#endif
    }
    close_s(s);
/*   log(s,"close Callbook"); */
}

/* This routine has been expanded upon, and is now located in buckbook.c
  - kb7yw Mon  01-27-1992

int
docallserver(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2)
    {
        if(Callserver != NULLCHAR)
      tprintf("The callserver's host name is: %s\n",Callserver);
        else
        {
            j2tputs("Callserver not configured!\nUsage: callserver hostname OR callserver ip_address\n");
        }
    }
    else {
        if(Callserver != NULLCHAR)
            free(Callserver);
        Callserver = j2strdup(argv[1]);
    }
    return 0;
}
*/

#endif /* CALLSERVER */
