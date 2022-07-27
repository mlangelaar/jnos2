/* UDP-related user commands
 * Copyright 1991 Phil Karn, KA9Q
 */
#ifdef MSDOS
#include <dos.h>
#endif
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "socket.h"
#include "udp.h"
#include "internet.h"
#include "cmdparse.h"
#include "commands.h"
  
static int doudpstat __ARGS((int argc,char *argv[],void *p));
  
static struct cmds DFAR Udpcmds[] = {
    { "status",   doudpstat,  0, 0,   NULLCHAR },
    { NULLCHAR,	NULL,	0,	0, NULLCHAR }
};

/* 09Mar2021, Maiko, Clean this up, UCB is now 12 wide */
static char *ucbbanner = "&UCB          RcvQ  Local socket\n";

int
doudp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Udpcmds,argc,argv,p);
}
int
st_udp(udp,n)
struct udp_cb *udp;
int n;
{
    if(n == 0)
#ifdef UNIX
        tprintf("%s", ucbbanner);
  
    return tprintf("%-12lx%6u  %s\n",FP_SEG(udp),udp->rcvcnt,pinet(&udp->socket));
#else
    j2tputs("&UCB Rcv-Q  Local socket\n");
  
    return tprintf("%4.4x%6u  %s\n",FP_SEG(udp),udp->rcvcnt,pinet(&udp->socket));
#endif
}
  
/* Dump UDP statistics and control blocks */
static int
doudpstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct udp_cb *udp;
    register int i;
  
    for(i=1;i<=NUMUDPMIB;i++){
        tprintf("(%2d)%-20s%10d",i, Udp_mib[i].name,Udp_mib[i].value.integer);
        if(i % 2)
            j2tputs("     ");
        else
            tputc('\n');
    }
    if((i % 2) == 0)
        tputc('\n');
  
    j2tputs (ucbbanner);

    for(udp = Udps;udp != NULLUDP; udp = udp->next){
        if(st_udp(udp,1) == EOF)
            return 0;
    }
    return 0;
}
