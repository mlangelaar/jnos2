/* Internet Time Server
 * Author: Brian K. Teravskis, WD0EFL
 * Date: 02/15/92
 *
 * Based on RFC868 Time Protocol
 */
#include <time.h>
#include "global.h"
#ifdef RDATESERVER
#include "mbuf.h"
#include "socket.h"
#include "session.h"
#include "proc.h"
#include "commands.h"
  
#define DIFFTIME 2208988800UL	/* 05Jul2016, Maiko, compiler C90 warning */
  
static void timeserver __ARGS((int s,void *unused,void *p));
  
/* Start up time server */
int
time1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;
  
    if(argc < 2)
        port = IPPORT_TIME;
    else
        port = atoi(argv[1]);
  
    return start_tcp(port,"TIME Server",timeserver,512);
} /* time1 */
  
/* Stop the time server */
int
time0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;
  
    if(argc < 2)
        port = IPPORT_TIME;
    else
        port = atoi(argv[1]);
    return stop_tcp(port);
} /* time0 */
  
/*
 * Serve up the time to the connected client
 */
static void
timeserver(s,unused,p)
int s;
void *unused;
void *p;
{
    struct mbuf *bp;
    char    datetime[4];
  
    sockmode(s,SOCK_BINARY);
    sockowner(s,Curproc);
    log(s,"Open Time");
    /*
     * Change 1970 start time to 1900 start time, and put
     * it in network order
     */
    put32(datetime,time((time_t *)0)+DIFFTIME);
  
    /* enqueue for transmission */
    bp = qdata(datetime,sizeof(int32));
    /* Send time data */
    if(send_mbuf(s,bp,0,NULLCHAR,0) == -1)
        log(s,"Time Failed");
  
    log(s,"Close Time");
    close_s(s);
} /* timeserver */
  
#endif /* RDATESERVER */
  
