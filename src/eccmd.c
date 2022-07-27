/* 3C501 commands
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#ifdef PC_EC
#include "mbuf.h"
#include "iface.h"
#include "ec.h"
#include "enet.h"
  
int
doetherstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct ec *ecp;
    char buf[20];
  
    for(ecp = Ec;ecp < &Ec[Nec]; ecp++){
        pether(buf,ecp->iface->hwaddr);
        tprintf("Controller %u, Ethernet address %s\n",
        (unsigned int)((ecp-Ec) / sizeof(struct ec)),buf);
  
        tprintf("recv      bad       overf     drop      nomem     intrpt\n");
        tprintf("%-10lu%-10lu%-10lu%-10lu%-10lu%-10lu\n",
        ecp->estats.recv,ecp->estats.bad,ecp->estats.over,
        ecp->estats.drop,ecp->estats.nomem,ecp->estats.intrpt);
  
        tprintf("xmit      timeout   jam       jam16\n");
        if(tprintf("%-10lu%-10lu%-10lu%-10lu\n",
            ecp->estats.xmit,ecp->estats.timeout,ecp->estats.jam,
            ecp->estats.jam16) == EOF)
            break;
    }
    return 0;
}
#endif /* PC_EC */
