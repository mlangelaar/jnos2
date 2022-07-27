/* Internet rdate client
 * Author: Brian Teravskis, WD0EFL
 * Date: 03/30/92
 *
 * Based on RFC868 Time Protocol
 *
 * Added local hour offset - Doug Crompton 10/28/92
 * and subcommand menu for possible future commands
 * N5KNX: Offset made compile-time option; set your TZ env variable instead!
 */
#include "global.h"
#ifdef RDATECLI
#include "mbuf.h"
#include "socket.h"
#include "session.h"
#include "proc.h"
#include "netuser.h"
#include "commands.h"
#include "tty.h"
#include "timer.h"
#include <time.h>
#include "cmdparse.h"
  
/* #define IPPORT_TIME 37  (in socket.h) */
#define DIFFTIME 2208988800UL
#define RDATETIMEOUT 30
  
#ifdef DESPITE_TZ_VAR
static int rdate_offset=0;
static int dordateoff __ARGS((int argc,char *argv[],void *p));
#endif
  
static int dordateserver __ARGS((int argc,char *argv[],void *p));
  
static struct cmds rdatecmds[] = {
    { "server", dordateserver, 512, 2, "rdate server <address>" },
#ifdef DESPITE_TZ_VAR
    { "offset", dordateoff,    0,   0, NULLCHAR },
#endif
  
    { NULLCHAR,	NULL,	0,	0,	NULLCHAR }
};
  
int
dordate(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(rdatecmds,argc,argv,p);
}
  
static int
dordateserver(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct sockaddr_in sock;
    int s,i_state;
    struct mbuf *bp;
    time_t rtime, raw_time, ltime;
  
    if((sock.sin_addr.s_addr = resolve(argv[1])) == 0){
        log(-1,"RDATE:Host %s unknown\n",argv[1]);
        return 1;
    }
    sock.sin_family = AF_INET;
    sock.sin_port = IPPORT_TIME;
  
    /* Open connection to time server */
    if((s = j2socket(AF_INET,SOCK_STREAM,0)) == -1) {
        log(-1,"RDATE:Can't create socket");
        return 1;
    }
    sockmode(s,SOCK_BINARY);
  
    /* Set timeout timer */
    j2alarm(RDATETIMEOUT*1000);
      /* Connect to time server */
    if(j2connect(s,(char *)&sock,sizeof(sock)) == -1){
        /* Connect failed */
	j2alarm(0);
        log(s,"RDATE: Connect failed");
	j2shutdown(s,2);
        close_s(s);
        return 1;
    }

    j2alarm(RDATETIMEOUT*1000);
    /* Get time info sent by server */
    if(recv_mbuf(s,&bp,0,NULLCHAR,(int *)0) != 4) {
      j2alarm(0);
      log(s,"RDATE: No or bad response");
      close_s(s);
      return 1;
    }

    (void)time(&ltime);
    j2alarm(0);
    rtime = (time_t)pull32(&bp);
    /* Convert 1900 start date to 1970 start date */
    rtime -= DIFFTIME;
    raw_time=rtime;
#ifdef DESPITE_TZ_VAR
    rtime += (rdate_offset*3600L);
#endif
    /* Set the system time */
    i_state = dirps();
    stime(&rtime);
    restore(i_state);
    log(s,"RDATE: Clock set to %.24s from %s (delta %ld)",ctime(&rtime),argv[1],(rtime-ltime));
    tprintf("\nTime received from %s - %s",argv[1],ctime(&raw_time));
#ifdef DESPITE_TZ_VAR
    tprintf("System time set to %s\n",ctime(&rtime));
#endif
    free_q(&bp);
    close_s(s);
    return 0;
} /* dordate */
  
#ifdef DESPITE_TZ_VAR
/* Set time offset */
static int
dordateoff(argc, argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2) {
        tprintf("Usage: 'rdate offset <+-hours>' - Current Offset %d Hours\n",rdate_offset);
        return 0 ;
    }
  
    rdate_offset = atoi(argv[1]);
    return 0;
}
#endif /* DESPITE_TZ_VAR */
#endif /* RDATECLI */
