/* This file contains code to implement the Routing Information Protocol (RIP)
 * and is derived from 4.2BSD code. Mike Karels of Berkeley has stated on
 * TCP-IP that the code may be freely used as long as UC Berkeley is
 * credited. (Well, here's some credit :-). AGB 4-28-88
  
 * Further documentation on the RIP protocol is now available in Charles
 * Hedrick's draft RFC, as yet unnumbered. AGB 5-6-88
 *
 * The RIP RFC has now been issued as RFC1058. AGB 7-23-88
 *
 * Code gutted and substantially rewritten. KA9Q 9/89
 *
 * Mods by PA0GRI
 *
 *  Changes Copyright (c) 1993 Jeff White - N0POY, All Rights Reserved.
 *  Permission granted for non-commercial copying and use, provided
 *  this notice is retained.
 *
 * Rehack for RIP-2 (RFC1388) by N0POY 4/1993
 *  Modules needed for changes:  rip.c, rip.h, ripcmd.c, ripdump.c, ip.h
 *                               commands.h, iface.h, iface.c, version.c
 *
 * Beta release 11/16/93 V0.95
 *
 * Bug fix that prevented split horizon routing to work fixed.
 * 2/19/94 release V1.0
 *
 * Modifications in proc_rip and nbits to handle incoming subnet targets,
 * and in send_routes for RIP-1 to broadcast subnet routes properly.
 * 11/15/95 by soros@integra.hu
 *
 * G8BPQ's RIP 98 added 2/6/99 - G4HIP/N5KNX
 *
 * Feb/Mar of 2010 - VE4KLM added AMPRgateway specific RIP updates
 */

#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include "global.h"
#ifdef RIP
#include "mbuf.h"
#include "netuser.h"
#include "udp.h"
#include "timer.h"
#include "iface.h"
#include "ip.h"
#include "internet.h"
#include "rip.h"
#include "arp.h"
#include "socket.h"

struct rip_stat Rip_stat;
int16 Rip_trace;
FILE *Rip_trace_file = NULLFILE;
char *Rip_trace_fname = NULLCHAR;
int Rip_merge;
int32 Rip_ttl = RIP_TTL;
int16 Rip_ver_refuse = 0;
int Rip_default_refuse = FALSE;
#ifdef RIP98
int Rip98allow = 1;
#else
#define riplen RIP_ENTRY
#endif
struct rip_list *Rip_list;
struct udp_cb *Rip_cb;
struct rip_auth *Rip_auth;
struct rip_refuse *Rip_refuse;
char Rip_nullpass[RIP_AUTH_SIZE];

static void rip_rx __ARGS((struct iface *iface,struct udp_cb *sock,int cnt));
static void proc_rip __ARGS((struct iface *iface,int32 gateway,
struct rip_route *ep,unsigned char version));
static char *putheader __ARGS((char *cp,char command,char version,int16 domain));
static char *putentry __ARGS((char *cp,int16 fam,int16 tag,int32 target,
int32 targmask,int32 router,int32 metric));
#ifdef RIP98
static char *put98entry __ARGS((char *cp,int32 target, int bits,int32 metric));
#endif
static void send_routes __ARGS((int32 dest,int16 port,int trig,
int flags,char version, struct rip_list *rdata));
static void pullheader __ARGS((struct rip_head *ep, struct mbuf **bpp));
static int check_authentication __ARGS((struct rip_auth *auth,
struct mbuf **bpp, struct rip_head *header, int32 srcaddr, char *ifcname,
struct rip_authenticate *entry));
static char *putauth  __ARGS((char *cp, int16 authtype,
char *authpass));
static void rip_trace(short level, char *errstr, ...);
static int32 extractnet(int32 addr);

#ifdef RIPAMPRGW

/*
 * 27Oct2014, Maiko (VE4KLM), Before now, the RIPAMPRGW and Regular rip were
 * mutually exclusive - you could only use one or the other, not both at the
 * same time. Chances are, one will never use both at the same time, but from
 * a technical point of view, the RIPAMPRGW should not keep the Regular rip
 * from being available or functioning properly, so that is now fixed.
 *
 * rip_amprgw = 738197505 - value returned by resolve ("44.0.0.1")
 *
 */

static int32 rip_amprgw = 738197505;

#else

static int32 rip_amprgw = -1;

#endif

/* Send RIP CMD_RESPONSE packet(s) to the specified rip_list entry */

void
rip_shout(p)
void *p;
{
    register struct rip_list *rl;
  
    rl = (struct rip_list *)p;
    stop_timer(&rl->rip_time);
    send_routes(rl->dest,RIP_PORT,0,rl->flags,rl->rip_version,rl);
    set_timer(&rl->rip_time,rl->interval*1000);
    start_timer(&rl->rip_time);
}
  
/* Extract Net Address from Address	*/
static int32
extractnet(addr)
int32 addr;
{
    switch (hibyte(hiword(addr)) >> 6) {
        case 3:     /* Class C address */
            return (0xffffff00L & addr);
        case 2:     /* Class B address */
            return (0xffff0000L & addr);
        case 0:     /* Class A address */
        case 1:
            return (0xff000000L & addr);
    }
    return 0L;
}

/* Send the routing table. */
static void
send_routes(dest,port,trig,flags,version,rdata)
int32 dest;                   /* IP destination address to send to */
int16 port;
int trig;                     /* Send only triggered updates? */
int flags;
char version;                 /* Version of RIP packet */
struct rip_list *rdata;       /* Used for RIP-2 packets */
{
    char *cp;
    int i,bits,numroutes,maxroutes;
    int16 pktsize;
#ifdef RIP98
    int16 riplen = RIP_ENTRY;
#endif
    struct mbuf *bp;
    struct route *rp;
    struct socket lsock,fsock;
    struct iface *iface;
  
#ifdef RIP98
    if (version == RIP_VERSION_98)  /* sometimes we use 98 internally too!! */
        version = RIP_VERSION_X;  /* Finangle factor RIP98 = Latest RIP version +1 */
    if (version == RIP_VERSION_X) 
        riplen = RIP98_ENTRY;
#endif

    if((rp = rt_lookup(dest)) == NULLROUTE) {
        rip_trace(1, "No route to [%s] exists, cannot send", inet_ntoa(dest));
        return;                 /* No route exists, can't do it */
    }
    iface = rp->iface;
  
   /* Compute maximum packet size and number of routes we can send */
    pktsize = ip_mtu(dest) - IPLEN;
    pktsize = min(pktsize,RIP_PKTSIZE);
    maxroutes = (pktsize - RIP_HEADER) / riplen;

    lsock.address = INADDR_ANY;
    lsock.port = RIP_PORT;
    fsock.address = dest;
    fsock.port = port;
  
   /* Allocate space for a full size RIP packet and generate header */
    if((bp = alloc_mbuf(pktsize)) == NULLBUF)
        return;
    numroutes = 0;
  
   /* See if we know information about what to send out */
  
    if ((version >= RIP_VERSION_2) && (rdata != NULLRL)) {
        cp = putheader(bp->data,RIPCMD_RESPONSE, version, rdata->domain);
      /* See if we need to put an authentication header on */
        if (flags & RIP_AUTHENTICATE) {
            cp = putauth(cp, RIP_AUTH_SIMPLE, rdata->rip_auth_code);
            numroutes++;
        }
    } else {
        cp = putheader(bp->data,RIPCMD_RESPONSE, version, 0);
    }
  
   /* Emit route to ourselves, if requested */
    if(flags & RIP_US && !trig) {  /* avoid looping triggered updates - G4HIP */
        if (version == RIP_VERSION_1)
            cp = putentry(cp,RIP_AF_INET,0,iface->addr,0,0,1);
#ifdef RIP98
        else if (version == RIP_VERSION_X)
            cp = put98entry(cp, iface->addr,32,1);
#endif
        else
            cp = putentry(cp,RIP_AF_INET,0,iface->addr,0xFFFFFFFFL,0,1);
        numroutes++;
    }
  
   /* Emit default route, if appropriate */
  
    if(R_default.iface != NULLIF && !(R_default.flags & RTPRIVATE)
    && (!trig || (R_default.flags & RTTRIG))){
        if(!(flags & RIP_SPLIT) || iface != R_default.iface){
            if (version == RIP_VERSION_1)
                cp = putentry(cp,RIP_AF_INET,0,0,0,0,R_default.metric);
#ifdef RIP98
            else if (version == RIP_VERSION_X)
                cp = put98entry(cp, 0, 0, R_default.metric);
#endif
	    else
                cp = putentry(cp,RIP_AF_INET,R_default.route_tag,0,0,0,R_default.metric);
            numroutes++;
        } else if (trig && (flags & RIP_POISON)) {
         /* Poisoned reverse */
            if (version == RIP_VERSION_1)
                cp = putentry(cp,RIP_AF_INET,0,0,0,0,RIP_METRIC_UNREACHABLE);
#ifdef RIP98
            else if (version == RIP_VERSION_X)
                cp = put98entry(cp, 0, 0, RIP_METRIC_UNREACHABLE);
#endif
            else
                cp = putentry(cp,RIP_AF_INET,R_default.route_tag,0,0,0,
                RIP_METRIC_UNREACHABLE);
            numroutes++;
        }
    }
  
    for(bits=0;bits<32;bits++){
        for(i=0;i<HASHMOD;i++){
            for(rp = Routes[bits][i];rp != NULLROUTE;rp=rp->next){
                if((rp->flags & RTPRIVATE)
                    || (trig && !(rp->flags & RTTRIG)))
                    continue;

#ifdef RIP98
                /* With RIP98, don't send a host entry which is marked as a gateway */
                if ((version == RIP_VERSION_X) && (rp->gateway == dest))
		    continue;
#endif
	       
                if(numroutes >= maxroutes){
               /* Packet full, flush and make another */
                    bp->cnt = RIP_HEADER + numroutes * riplen;
                    send_udp(&lsock,&fsock,0,0,bp,bp->cnt,0,0);
                    Rip_stat.vdata[(int)version].output++;
                    if((bp = alloc_mbuf(pktsize)) == NULLBUF)
                        return;
                    numroutes = 0;
  
                    if (version >= RIP_VERSION_2 && rdata != NULLRL) {
                        cp = putheader(bp->data,RIPCMD_RESPONSE, version,
                        rdata->domain);
                  /* See if we need to put an authentication header on */
                        if (flags & RIP_AUTHENTICATE) {
                            cp = putauth(cp, RIP_AUTH_SIMPLE, rdata->rip_auth_code);
                            numroutes++;
                        }
                    } else {
                        cp = putheader(bp->data,RIPCMD_RESPONSE, version, 0);
		    }
                }
  
                if(!(flags & RIP_SPLIT) || iface != rp->iface){
                    if (version == RIP_VERSION_1)
			if(extractnet(dest)==extractnet(rp->target))
                            /* if dest is in the same net send target	SJ */
                            cp = putentry(cp,RIP_AF_INET,0,rp->target,0,0,rp->metric);
			else
                            /* if dest is in an other net send target's net	*/
                            cp = putentry(cp,RIP_AF_INET,0,extractnet(rp->target),0,0,rp->metric);
#ifdef RIP98
                    else if (version == RIP_VERSION_X)
                        cp = put98entry(cp, rp->target, rp->bits, rp->metric);
#endif
                    else
                        cp = putentry(cp,RIP_AF_INET,rp->route_tag,rp->target,
                        (0xFFFFFFFFL << (32 - rp->bits)),rdata?rdata->proxy_route:0,
                        rp->metric);
                    numroutes++;
                } else if(trig && (flags & RIP_POISON)) {
                    if (version == RIP_VERSION_1)
                        cp = putentry(cp,RIP_AF_INET,0,rp->target,0,0,RIP_METRIC_UNREACHABLE);
#ifdef RIP98
                    else if (version == RIP_VERSION_X)
                        cp = put98entry(cp, rp->target, rp->bits, RIP_METRIC_UNREACHABLE);
#endif
                    else
			cp = putentry(cp,RIP_AF_INET,rp->route_tag,rp->target,
                        (0xFFFFFFFFL << (32 - rp->bits)),rdata?rdata->proxy_route:0,
                        RIP_METRIC_UNREACHABLE);
                    numroutes++;
                }
            }
        }
    }
    if(numroutes != 0){
        bp->cnt = RIP_HEADER + numroutes * riplen;
        send_udp(&lsock,&fsock,0,0,bp,bp->cnt,0,0);
        Rip_stat.vdata[(int)version].output++;
    } else {
        free_p(bp);
    }
}
  
/* Add an entry to the rip broadcast list */
  
int
rip_add(dest,interval,flags,version,authpass,domain,route_tag,proxy)
int32 dest;
int32 interval;
char flags;
char version;
char authpass[RIP_AUTH_SIZE];
int16 domain;
int16 route_tag;
int32 proxy;
{
    register struct rip_list *rl;
    struct route *rp;
  
    for(rl = Rip_list; rl != NULLRL; rl = rl->next)
        if((rl->dest == dest) && (rl->domain == domain))
            return 0;
  
    if((rp = rt_lookup(dest)) == NULLROUTE){
        tprintf("%s is unreachable\n",inet_ntoa(dest));
        return 0;
    }
  
   /* get a chunk of memory for the rip interface descriptor */
    rl = (struct rip_list *)callocw(1,sizeof(struct rip_list));
  
   /* tack this record on as the first in the list */
    rl->next = Rip_list;
    if(rl->next != NULLRL)
        rl->next->prev = rl;
    Rip_list = rl;
  
    rl->dest = dest;
  
    rip_trace(9, "Rip added V%d Flags %d Tag %d Proxy %s Domain %d Auth %s Interval %d",
    version, flags, route_tag, inet_ntoa(proxy), domain, authpass, interval);
  
   /* and the interface ptr, tick interval and flags */
    rl->iface = rp->iface;
    rl->rip_version = version;
    rl->interval = interval;
    rl->flags = flags;
    rl->proxy_route = proxy;
    rl->route_tag = route_tag;
    rl->domain = domain;
    memcpy(rl->rip_auth_code,authpass,RIP_AUTH_SIZE);
  
   /* and set up the timer stuff */
    set_timer(&rl->rip_time,interval*1000);
    rl->rip_time.func = rip_shout;
    rl->rip_time.arg = rl;
    start_timer(&rl->rip_time);
    return 1;
}
  
/* add a gateway to the rip_refuse list which allows us to ignore their
 * advertisements
 */
  
int
riprefadd(gateway)
int32 gateway;
{
    register struct rip_refuse *rl;
  
    for(rl = Rip_refuse; rl != NULLREF; rl = rl->next)
        if(rl->target == gateway)
            return 0;         /* Already in table */
  
   /* get a chunk of memory for the rip interface descriptor */
    rl = (struct rip_refuse *)callocw(1,sizeof(struct rip_refuse));
  
   /* tack this record on as the first in the list */
    rl->next = Rip_refuse;
    if(rl->next != NULLREF)
        rl->next->prev = rl;
    Rip_refuse = rl;
  
   /* fill in the gateway to ignore */
    rl->target = gateway;
    return 0;
}
  
/* Add an authentication type to an interface name */
  
int
ripauthadd(ifcname, domain, password)
char *ifcname;
int16 domain;
char *password;
{
    register struct rip_auth *ra;
    int x;
  
    for(ra = Rip_auth; ra != NULLAUTH; ra = ra->next)
        if(!strcmp(ifcname,ra->ifc_name) && (ra->domain == domain))
            return 1;         /* Already in table */
  
   /* get a chunk of memory for the rip interface descriptor */
    ra = (struct rip_auth *)callocw(1,sizeof(struct rip_auth));
  
   /* tack this record on as the first in the list */
    ra->next = Rip_auth;
    if(ra->next != NULLAUTH)
        ra->next->prev = ra;
    Rip_auth = ra;
  
   /* fill in the data */
    ra->ifc_name = mallocw(strlen(ifcname)+1);
    strcpy(ra->ifc_name, ifcname);
    ra->domain = domain;
    for (x = 0; x < RIP_AUTH_SIZE+1; x++)
        ra->rip_auth_code[x] = '\0';
    strcpy(ra->rip_auth_code, password);
    return 0;
}
  
/* Drop an authentication to an interface name */
  
int
ripauthdrop(ifcname, domain)
char *ifcname;
int16 domain;
{
    register struct rip_auth *ra;
  
    for(ra = Rip_auth; ra != NULLAUTH; ra = ra->next)
        if(!strcmp(ifcname,ra->ifc_name) && (ra->domain == domain))
            break;
  
   /* leave if we didn't find it */
    if(ra == NULLAUTH)
        return 0;
  
   /* Unlink from list */
    if(ra->next != NULLAUTH)
        ra->next->prev = ra->prev;
    if(ra->prev != NULLAUTH)
        ra->prev->next = ra->next;
    else
        Rip_auth = ra->next;
  
    free((char *)ra->ifc_name);
    free((char *)ra);
    return 0;
}
  
/* drop a RIP target */
  
int
rip_drop(dest,domain)
int32   dest;
int16 domain;
{
    register struct rip_list *rl;
  
    for(rl = Rip_list; rl != NULLRL; rl = rl->next)
        if((rl->dest == dest) && (rl->domain == domain))
            break;
  
   /* leave if we didn't find it */
    if(rl == NULLRL)
        return 0;
  
   /* stop the timer */
    stop_timer(&rl->rip_time);
  
   /* Unlink from list */
    if(rl->next != NULLRL)
        rl->next->prev = rl->prev;
    if(rl->prev != NULLRL)
        rl->prev->next = rl->next;
    else
        Rip_list = rl->next;
  
   /* and deallocate the descriptor memory */
    free((char *)rl);
    return 0;
}
  
/* drop a RIP-refuse target from the rip_refuse list */
  
int
riprefdrop(gateway)
int32 gateway;
{
    register struct rip_refuse *rl;
  
    for(rl = Rip_refuse; rl != NULLREF; rl = rl->next)
        if(rl->target == gateway)
            break;
  
   /* leave if we didn't find it */
    if(rl == NULLREF)
        return 0;
  
   /* Unlink from list */
    if(rl->next != NULLREF)
        rl->next->prev = rl->prev;
    if(rl->prev != NULLREF)
        rl->prev->next = rl->next;
    else
        Rip_refuse = rl->next;
  
   /* and deallocate the structure memory */
    free((char *)rl);
    return 0;
}
  
/* function to output a RIP CMD_RESPONSE packet for the rip_trigger list */
  
void
rip_trigger()
{
    register struct rip_list *rl;
    int bits,i;
    struct route *rp;
  
    for(rl=Rip_list;rl != NULLRL;rl = rl->next){
        send_routes(rl->dest,RIP_PORT,1,rl->flags,rl->rip_version,rl);
    }
   /* Clear the trigger list */
    R_default.flags &= ~RTTRIG;
    for(bits=0;bits<32;bits++){
        for(i=0;i<HASHMOD;i++){
            for(rp = Routes[bits][i];rp != NULLROUTE;rp = rp->next){
                rp->flags &= ~RTTRIG;
            }
        }
    }
}
  
/* Start RIP agent listening at local RIP UDP port */
int
rip_init()
{
    struct socket lsock;
    int x;

    lsock.address = INADDR_ANY;

    lsock.port = RIP_PORT;
  
    if(Rip_cb == NULLUDP)
        Rip_cb = open_udp(&lsock,rip_rx);
  
    for (x = 0; x < RIP_AUTH_SIZE; x++)
        Rip_nullpass[x] = 0;
  
    Rip_trace = 0;
  
   /* Add the 0 domain with no password */
  
    ripauthadd(DEFAULTIFC, 0, RIP_NO_AUTH);

    return 0;
}

/* Process RIP input received from 'interface'. */
static void
rip_rx(iface,sock,cnt)
struct iface *iface;
struct udp_cb *sock;
int cnt;
{
#ifdef RIP98
    int16 riplen = RIP_ENTRY;
#endif
    struct mbuf *bp;
    struct socket fsock;
    struct rip_refuse *rfl;
    struct rip_route entry;
    struct rip_head header;
    struct route *rp;
    struct rip_list *rl;

   /* receive the RIP packet */
    recv_udp(sock,&fsock,&bp);

   /* check the gateway of this packet against the rip_refuse list and
    * discard it if a match is found
    */

    for(rfl=Rip_refuse;rfl != NULLREF;rfl = rfl->next){
	if(fsock.address == rfl->target){
	    Rip_stat.refusals++;
	    rip_trace(2, "RIP refused from %s",inet_ntoa(fsock.address));
	    free_p(bp);
	    return;
	}
    }

    pullheader(&header, &bp);

#ifdef RIP98
   /* Fiddle to make RIP98 look like latest official RIP + 1 */
   if (header.rip_vers == RIP_VERSION_98) {
       header.rip_vers = RIP_VERSION_X;
       riplen = RIP98_ENTRY;
   }
#endif

   /* increment the rcvd cnt */
    Rip_stat.vdata[header.rip_vers].rcvd++;

   /* Check to see if we'll accept this version on this interface */

    if (header.rip_vers <= Rip_ver_refuse) {
	rip_trace(3, "RIP version %d refused from [%s]",
#ifdef RIP98
        (header.rip_vers == RIP_VERSION_X) ? RIP_VERSION_98 : header.rip_vers,
#else
        header.rip_vers,
#endif
	inet_ntoa(fsock.address));
	Rip_stat.refusals++;
	free_p(bp);
	return;
    }

#ifdef RIP98
    /* If this is a RIP 98 frame and rip98rx is off - discard the frame */
    if ((header.rip_vers == RIP_VERSION_X) && (Rip98allow == 0)) {
        rip_trace (1, "RIP-98 frame received - rip98rx is off");
  	Rip_stat.refusals++;
  	free_p(bp);
  	return;
    }
#endif

    /* Check the version of the frame */
    switch (header.rip_vers) {
	case RIP_VERSION_2 :
	    break;

	case RIP_VERSION_0 :
	    rip_trace(1, "RIP Version 0 refused from [%s]", inet_ntoa(fsock.address));
	    Rip_stat.version++;
	    free_p(bp);
	    return;

	case RIP_VERSION_1 :
	 /* Toss RIP if header is bogus for V1 */
	    if (header.rip_domain != 0) {
		rip_trace(1, "RIP-1 bogus header, data in null fields from [%s]",
		inet_ntoa(fsock.address));
		Rip_stat.vdata[RIP_VERSION_1].unknown++;
		free_p(bp);
		return;
	    }
	    break;

#ifdef RIP98
        case RIP_VERSION_X :
            if ((header.rip_cmd != RIPCMD_RESPONSE) && (header.rip_cmd != RIPCMD_REQUEST)) {
	        rip_trace(1, "RIP-98 invalid header received from [%s]\n", inet_ntoa(fsock.address));
		Rip_stat.vdata[RIP_VERSION_X].unknown++;
	        free_p(bp);
		return;
	    }
            break;
#endif
    }

    rip_trace(2, "RIP Packet version %d processing",
#ifdef RIP98
        (header.rip_vers == RIP_VERSION_X) ? RIP_VERSION_98 : header.rip_vers);
#else
        header.rip_vers);
#endif

    switch(header.rip_cmd){
	case RIPCMD_RESPONSE:
	    rip_trace(2, "RIPCMD_RESPONSE from [%s] domain %d",inet_ntoa(fsock.address),
	    header.rip_domain);

	    Rip_stat.vdata[header.rip_vers].response++;
#ifdef RIP98
            if (header.rip_vers == RIP_VERSION_X)
	        pull98entry(&entry,&bp);
            else
#endif
	    pullentry(&entry,&bp);

#ifdef RIP98
	    if ((header.rip_vers >= RIP_VERSION_2) && (header.rip_vers != RIP_VERSION_X)) {
#else
	    if (header.rip_vers >= RIP_VERSION_2) {
#endif
	 /* We still have the authentication entry from above */

		if (!check_authentication(Rip_auth, &bp, &header,fsock.address,
		iface->name, (struct rip_authenticate *)&entry)) {
		    free_p(bp);
		    return;
		}
	    }
	    proc_rip(iface,fsock.address,&entry,header.rip_vers);

	    while(len_p(bp) >= riplen){
#ifdef RIP98
	        if (header.rip_vers == RIP_VERSION_X)
		    pull98entry(&entry,&bp);
	        else
#endif
		pullentry(&entry,&bp);
		proc_rip(iface,fsock.address,&entry,header.rip_vers);
            }
  
        /* If we can't reach the sender of this update, or if
         * our existing route is not through the interface we
         * got this update on, add him as a host specific entry
         */
            if((rp = rt_blookup(fsock.address,32)) != NULLROUTE){
            /* Host-specific route already exists, refresh it */
                start_timer(&rp->timer);
            } else if((rp = rt_lookup(fsock.address)) == NULLROUTE
            || rp->iface != iface) {
                entry.rip_family = RIP_AF_INET;
		entry.rip_tag = 0;
                entry.rip_dest = fsock.address;
                if (header.rip_vers > RIP_VERSION_1)
                    entry.rip_dest_mask = 32;
                else
                    entry.rip_dest_mask = 0;
                entry.rip_router = 0;
                entry.rip_metric = 1;       /* will get incremented to 2 */
                proc_rip(iface,fsock.address,&entry,header.rip_vers);
            }
            if(Rip_merge)
                rt_merge(Rip_trace);
            rip_trigger();
            break;
  
        case RIPCMD_REQUEST:
            rip_trace(2, "RIPCMD_REQUEST from [%s] domain %d",inet_ntoa(fsock.address),
            header.rip_domain);
  
            Rip_stat.vdata[header.rip_vers].request++;
	/* For now, just send the whole table with split horizon
         * enabled when the source port is RIP_PORT, and send
         * the whole table with split horizon disable when another
         * source port is used. This should be replaced with a more
         * complete implementation that checks for non-global requests
         */
  
            if (header.rip_vers > RIP_VERSION_1) {
         /* RIP-2, let's see if we know something about this guy */
                if ((rp = rt_lookup (fsock.address)) == NULLROUTE) { /* G4HIP */
                    rip_trace (2, "RIPCMD_REQUEST - no route to [%s]", inet_ntoa (fsock.address));
                    break;
                }
                for (rl = Rip_list; rl != NULLRL; rl = rl->next)
                    if ((rl->dest == rp->target) && (rl->domain == header.rip_domain))
                        break;
  
                if (rl == NULLRL)
                    if (fsock.port == RIP_PORT)
                        send_routes(fsock.address,fsock.port,0,(RIP_BROADCAST | RIP_SPLIT |
                        RIP_POISON),header.rip_vers,NULLRL);
                    else
                        send_routes(fsock.address,fsock.port,0,(RIP_BROADCAST),
			header.rip_vers,NULLRL);
                    else
                        if (fsock.port == RIP_PORT)
                            send_routes(fsock.address,fsock.port,0,rl->flags,header.rip_vers,rl);
                        else
                            send_routes(fsock.address,fsock.port,0,(rl->flags & ~(RIP_SPLIT | RIP_POISON)),
                            header.rip_vers,rl);
            } else {
                if(fsock.port == RIP_PORT)
                    send_routes(fsock.address,fsock.port,0,(RIP_SPLIT | RIP_BROADCAST | RIP_POISON),
                    header.rip_vers,NULLRL);
                else
                    send_routes(fsock.address,fsock.port,0,RIP_BROADCAST,
                    header.rip_vers,NULLRL);
            }
            break;
  
        default:
            rip_trace(1, "RIPCMD Unknown or not implemented from [%s] command %d",
            inet_ntoa(fsock.address),header.rip_cmd);
	    Rip_stat.vdata[header.rip_vers].unknown++;
            break;
    } /* switch */
    free_p(bp);
}
  
/* Apply a set of heuristics for determining the number of significant bits
 * (i.e., the address mask) in the target address. Needed since RIP doesn't
 * include the address mask for each entry.  Applies only to RIP-1 packets.
 */
  
int
nbits(target,ifaddr,ifnetmask)
int32 target;
int32 ifaddr;
int32 ifnetmask;
{
    int bits;
  
    if(target == 0)
	return 0;   /* Special case: 0.0.0.0 is the default route */
  
   /* Check the host-part bytes of
    * the address to check for byte-wide zeros
    * which we'll consider to be subnet routes.
    * e.g.  44.80.0.0 will be considered to be equal to 44.80/16
    * whereas 44.80.1.0 will be considered to be 44.80.1/24
    */
    switch (hibyte(hiword(target)) >> 6) {
        case 3:     /* Class C address */
      /*is it a host address ? i.e. are there any 1's in the
       * host part ?
       */
            if(target & 0xff)
                if(((target & 0xffffff00L) == (ifaddr & 0xffffff00L)) &&
                ((target & (~ifnetmask)) == 0))
			/* target is a subnet address of an other subnet in
			our CLASS C net, apply our subnet mask		*/
                   bits = mask2width(ifnetmask);
                else
		   bits = 32;	/* host address			*/
            else
                bits = 24;	/* CLASS C netmask		*/
            break;
        case 2:     /* Class B address */
            if(target & 0xffff)
                if(((target & 0xffff0000L) == (ifaddr & 0xffff0000L)) &&
                ((target & (~ifnetmask)) == 0))
			/* target is a subnet address of an other subnet in
			our CLASS B net, apply our subnet mask		*/
                   bits = mask2width(ifnetmask);
                else
                   bits = 32;
            else
                bits = 16;
            break;
        case 0:     /* Class A address */
        case 1:
            if(target & 0xffffffL)
                if(((target & 0xff000000L) == (ifaddr & 0xff000000L)) &&
		((target & (~ifnetmask)) == 0))
			/* target is a subnet address of an other subnet in
			our CLASS A net, apply our subnet mask		*/
                   bits = mask2width(ifnetmask);
                else
                   bits = 32;
            else
                bits = 8;
    }
  
    return bits;
}
  
/* Remove and process a RIP response entry from a packet */
  
static void
proc_rip(iface,gateway,ep,version)
struct iface *iface;
int32 gateway;
register struct rip_route *ep;
unsigned char version;
{
    int32 interval;
    int32 target;
    unsigned int bits;
    register struct route *rp;
    struct rip_list *rl;
    int add = 0;   /* action flags */
    int drop = 0;
    int trigger = 0;

	int32 rip_source = gateway;	/* NEW - 27Oct2014, Maiko (VE4KLM) */

    if(ep->rip_family != RIP_AF_INET) {
        if (ep->rip_family == RIP_AF_AUTH)
            return;
      /* Skip non-IP addresses */
        rip_trace(1, "RIP_rx: Not an IP family packet\n");
        Rip_stat.addr_family++;
        return;
    }
    /* Drop routing entry with metric zero (invalid) */
    if(ep->rip_metric == 0) return; 		/* by SJ */

   /* RIP-1 says all unused fields must be zero */
    if (version == RIP_VERSION_1) {
        if (ep->rip_tag != 0 || ep->rip_dest_mask != 0 || ep->rip_router != 0) {
            rip_trace(1,"RIP_rx: RIP-1 entry bad, data in null fields");
            Rip_stat.vdata[version].unknown++;
        }
    /* Guess at the mask, since it's not explicit for RIP-1 */
        bits = nbits(ep->rip_dest,iface->addr,iface->netmask); /* by SJ	*/
        target = ep->rip_dest;
    } else {
      /* Assume RIP-2 */
        if (!ep->rip_dest_mask) {
         /* No netmask, guess */
            bits = nbits(ep->rip_dest,iface->addr,iface->netmask); /* by SJ */
        } else {
            bits = mask2width(ep->rip_dest_mask);
        }
        target = ep->rip_dest;
      /* Check for "proxy" rip */
        if (ep->rip_router) {
            rip_trace(3, "Proxy rip pointing to [%s]", inet_ntoa(ep->rip_router));
            gateway = ep->rip_router;
        }
    }

#ifndef IGNORE_RIP_TAG
	/*
	 * 09Mar2010, Maiko, Only accept IPIP routes right now. Brian indicated
	 * to me that there is at least one IPUDP route in the RIP updates, but
	 * for this initial version, just ignore it, even though JNOS has the
	 * code to handle it - just want to get this initial version working.
	 *
	 * 04Aug2013, Maiko, Seems that Gus is getting RIP routes for which the
	 * tag field is NOT being set, which effectively makes JNOS ignore ALL the
	 * RIP broadcasts. Add '#define IGNORE_RIP_TAG' to config.h to fix this.
	 */
	if (ep->rip_tag != 4)
	{
        rip_trace (2, "Route %s/%u is not ipip - ignore type %d (for now)",
			inet_ntoa (target), bits, ep->rip_tag);

		return;
	}
#endif
 
   /* Don't ever add a route to myself through somebody! */
    if(bits == 32 && ismyaddr(target) != NULLIF) {
        rip_trace(2, "Route to self [%s]/32 metric %d", inet_ntoa(target),
        ep->rip_metric);
        return;
    }
  
   /* Check to see if we'll take a default route, zero bits mean default */
  
    if (Rip_default_refuse && bits == 0) {
        rip_trace(2, "Default route refused from [%s]", inet_ntoa(target));
        return;
    }
  
   /* Update metric to reflect link cost */
    ep->rip_metric++;
    ep->rip_metric = min(ep->rip_metric,RIP_METRIC_UNREACHABLE);
  
   /* Find existing entry, if any */
    rp = rt_blookup(target,bits);
 
	/* 27Oct2014, Maiko (VE4KLM), Replaces the hard coded RIPAMPRGW */
	if (rip_source == rip_amprgw)
	{
	   /*
		* 04Mar2010, Maiko, Encap.txt was all private routes, which is probably
	    * what we want anyways, since the BBS 'ip' command could result in alot
	    * of information coming out, and over slow links that would not be good,
		* so instead just make sure we update ENCAP interface routes only.
	    */
	    if(rp != NULLROUTE && rp->iface != iface)
		{
	        rip_trace(3, "Route %s/%u unchanged, not encap",
				inet_ntoa (target), bits);

	        return;
		}
	}
	else
	{
		/* Don't touch private routes */
		if(rp != NULLROUTE && (rp->flags & RTPRIVATE))
		{
        	rip_trace(3, "Route to [%s]/%u unchanged, private",
				inet_ntoa(target), bits);

        	return;
    	}
	}

    if(rp == NULLROUTE) {
        if(ep->rip_metric < RIP_METRIC_UNREACHABLE) {
         /* New route; add it and trigger an update */
            add++;
            trigger++;
        }
    } else if(rp->metric == RIP_METRIC_UNREACHABLE) {
      /* Route is in hold-down; ignore this guy */
        rip_trace(2, "Route to [%s]/%u ignored (hold-down) metric %d",
        inet_ntoa(target), bits, ep->rip_metric);
    } else if(rp->gateway == gateway && rp->iface == iface) {
      /* This is the gateway for the entry we already have;
       * restart the timer
       */
        start_timer(&rp->timer);
        if(rp->metric != ep->rip_metric) {
         /* Metric has changed. Update it and trigger an
          * update. If route has become unavailable, start
          * the hold-down timeout.
          */
		/* 12Oct2009, Maiko, Use "%d" for int32 vars */
            rip_trace(3, "Metric change [%s]/%u %d -> %d", inet_ntoa(target),
            bits, rp->metric, ep->rip_metric);
            if(ep->rip_metric == RIP_METRIC_UNREACHABLE)
                rt_timeout(rp);      /* Enter hold-down timeout */
            else
                rp->metric = ep->rip_metric;
            trigger++;
        }
    }
	else
	{
      /* Entry is from a different gateway than the current route */
        if(ep->rip_metric < rp->metric)
		{ 
         /* Switch to a new gateway */
            rip_trace(3, "Metric better [%s]/%u new: %d old: %d",
				inet_ntoa(target), bits, ep->rip_metric, rp->metric);
            drop++;
            add++;
            trigger++;
        }
		else
		{
			/* 27Oct2014, Maiko (VE4KLM), Replaces the hard coded RIPAMPRGW */
			if (rip_source == rip_amprgw)
			{
		/*
		 * 27Feb2010, Maiko, The metric is always 2 with the AMPRgw updates,
		 * so just change the darn gateway, the original code doesn't consider
		 * this, so perhaps RIP is not entirely appropriate for the 44 net, but
		 * we might as well use it anyways if we can make it work. The reason
		 * the metric is always 2 is that there is only ONE route from us to
		 * the other gateway. There are no alternate or multiple routes, so
		 * of course the metric will never change. So RIP may not be quite
		 * what we want to use here, but it can still work, let's see ...
		 */
				rip_trace(3, "gateway change %s/%u Metrics new: %d old: %d",
						inet_ntoa(target), bits, ep->rip_metric, rp->metric);
				drop++;
				add++;
				trigger++;
			}
			else
			{
		        /* Metric is no better, stay with current route */
            	rip_trace(3, "Metric not better [%s]/%u new: %d old: %d",
					inet_ntoa(target), bits, ep->rip_metric, rp->metric);
			}
        }
    }
    if(drop) {
      /* Switching to a better gateway; delete old entry */
  
        rip_trace(2, "Route drop [%s]/%u", inet_ntoa(target), bits);
        rt_drop(target,bits);
    }
  
    if(add) {
      /* Add a new entry */
        interval = Rip_ttl;
        for(rl=Rip_list; rl != NULLRL; rl = rl->next){
            if(rl->iface == iface){
                interval = rl->interval * 4;
                break;
            }
        }
        rip_trace(2, "Route add [%s]/%u through %s via ",
        inet_ntoa(target), bits, iface->name);
        rip_trace(2, "[%s] metric %d", inet_ntoa(gateway),
        ep->rip_metric);

        rp = rt_add (target, (unsigned) bits, gateway, iface,
        		(int) ep->rip_metric, interval, 0);

		if (!rp) /* 09Mar2010, Maiko, You must check for NULL or crash ! */
		{
       	  rip_trace (2, "rt_add [ %s / %u ] failed", inet_ntoa (target), bits);
		  return;
		}

		/* 27Oct2014, Maiko (VE4KLM), Replaces the hard coded RIPAMPRGW */
		if (rip_source == rip_amprgw)
		{
		   /* 04Mar2010, Maiko, Make sure we add the route as a private route */
			rp->flags |= RTPRIVATE;
		}

      /* Add in the routing tag for RIP-2 */
  
        if (version >= RIP_VERSION_2)
            rp->route_tag = ep->rip_tag;
        else
            rp->route_tag = 0;
    }
   /* If the route changed, mark it for a triggered update */
    if(trigger){
        rp->flags |= RTTRIG;
    }
}
  
/* Send a RIP request packet to the specified destination */
  
int
ripreq(dest,replyport,version)
int32 dest;
int16 replyport;
int16 version;
{
    struct mbuf *bp;
    struct socket lsock,fsock;
    char *cp;
    register struct rip_list *rl;
  
    lsock.address = INADDR_ANY;
    lsock.port = replyport;
  
   /* if we were given a valid dest addr, ask it (the routers on that net)
    * for a default gateway
    */
  
    if(dest == 0)
        return 0;
  
    fsock.address = dest;
    fsock.port = RIP_PORT;
  
   /* Send out one RIP Request packet as a broadcast to 'dest'  */
    if((bp = alloc_mbuf(RIP_HEADER + (2*RIP_ENTRY))) == NULLBUF)
        return -1;
  
   /* Check to see if we already know something about who we're   */
   /* requesting the RIP from */
  
    for (rl = Rip_list; rl != NULLRL; rl = rl->next)
        if (rl->dest == dest)
            break;
  
    bp->cnt = RIP_HEADER + RIP_ENTRY;
    if (rl != NULLRL) {
        version = rl->rip_version;  /* override */
        cp = putheader(bp->data,RIPCMD_REQUEST, version, rl->domain);
        if (version >= RIP_VERSION_2) {
            if (rl->flags & RIP_AUTHENTICATE) {
                cp = putauth(cp,RIP_AUTH_SIMPLE,rl->rip_auth_code);
                bp->cnt += RIP_ENTRY;
            }
        }
    } else {
        cp = putheader(bp->data,RIPCMD_REQUEST,version, 0);
    }
#ifdef RIP98
    if (version == RIP_VERSION_98) version=RIP_VERSION_X;  /* normalize */
#endif
    Rip_stat.vdata[version].output++;
  
#ifdef RIP98
    if (version == RIP_VERSION_X) {
        cp = put98entry(cp,0,0,RIP_METRIC_UNREACHABLE);
        bp->cnt = RIP_HEADER + RIP98_ENTRY;
    } else
#endif
    cp = putentry(cp,0,0,0L,0L,0L,RIP_METRIC_UNREACHABLE);
    send_udp(&lsock, &fsock,0,0,bp,bp->cnt,0,0);
    return 0;
}
  
/* Write the authentication packet */
  
static char *
putauth(cp,authtype,authpass)
register char *cp;
int16 authtype;
char *authpass;
{
    int x;
  
    cp = put16(cp, 0xFFFF);
    cp = put16(cp, authtype);
  
   /* Put the password in big-endian (network) byte order */
   /* This probably is not the best way to do this, since it */
   /* would hose up on a real big-endian machine.  Oh well */
   /* Something to fix in the future.  Whip me, beat me, make */
   /* me use an Intel micro brain.  -N0POY */
  
    for (x = 0; x < RIP_AUTH_SIZE; x += 4) {
        *cp++ = authpass[x+3];
        *cp++ = authpass[x+2];
        *cp++ = authpass[x+1];
        *cp++ = authpass[x];
    }
    return(cp);
}
  
/* Write the header of a RIP packet */
  
static char *
putheader(cp,command,version,domain)
register char *cp;
char command;
char version;
int16 domain;
{
    *cp++ = command;
#ifdef RIP98
    if (version == RIP_VERSION_X) {
        version = RIP_VERSION_98;
        domain = 0;  /* to be sure its zero */
    }
#endif
    *cp++ = version;
    return put16(cp,domain);
}
  
/* Write a single entry into a rip packet */
  
static char *
putentry(cp,fam,tag,target,targmask,router,metric)
register char *cp;
int16 fam;
int16 tag;
int32 target;
int32 targmask;
int32 router;
int32 metric;
  
{
    cp = put16(cp,fam);
    cp = put16(cp,tag);
    cp = put32(cp,target);
    cp = put32(cp,targmask);
    cp = put32(cp,router);
    return put32(cp,metric);
}
  
/* Check the authentication of RIP-II packets */
int
check_authentication(auth,bpp,header,srcaddr,ifcname,entry)
struct rip_auth *auth;
struct mbuf **bpp;
struct rip_head *header;
int32 srcaddr;
char *ifcname;
struct rip_authenticate *entry;
{
    struct rip_auth *rd;
  
    for (rd = auth; rd != NULLAUTH; rd = rd->next) {
        if ((strcmp(ifcname, rd->ifc_name) == 0) ||
        (strcmp(DEFAULTIFC, rd->ifc_name) == 0)) {
            if (rd->domain == header->rip_domain) {
            /* We'll take this domain, check against a NULL password */
                if (strcmp(rd->rip_auth_code, RIP_NO_AUTH) == 0) {
                    rip_trace(3, "RIP-2 taken due to no password from [%s] domain %d",
                    inet_ntoa(srcaddr), header->rip_domain);
                    return(TRUE);
                } else {
  
               /* Okay, we need an authentication */
  
                    if (entry->rip_family != RIP_AF_AUTH) {
                     /* It doesn't have an authentication packet */
                        rip_trace(2, "RIP-2 lacking authentication packet from [%s] domain %d",
                        inet_ntoa(srcaddr), header->rip_domain);
                        Rip_stat.auth_fail++;
                        return(FALSE);
                    }
  
                    if (entry->rip_auth_type != RIP_AUTH_SIMPLE) {
                     /* Only support simple authentication */
                        rip_trace(2, "RIP-2 wrong type of authentication from [%s]",
                        inet_ntoa(srcaddr));
                        Rip_stat.auth_fail++;
                        return(FALSE);
                    }
  
                    if (memcmp(rd->rip_auth_code,entry->rip_auth_str,RIP_AUTH_SIZE) == 0) {
                        rip_trace(3, "RIP-2 authenticated from [%s] domain %d",
                        inet_ntoa(srcaddr), header->rip_domain);
                        return(TRUE);
                    } else {
                        rip_trace(2, "RIP-2 authentication failed from [%s] domain %d,\n attempted password '%.16s' right password '%.16s'",
                        inet_ntoa(srcaddr), header->rip_domain, entry->rip_auth_str, rd->rip_auth_code);
                        Rip_stat.auth_fail++;
                        return(FALSE);
                    }
                }
            }
        }
    }
   /* Didn't find the right routing domain for this packet */
    rip_trace(3, "RIP-2 domain %d not accepted from [%s]", header->rip_domain,
    inet_ntoa(srcaddr));
    Rip_stat.wrong_domain++;
    return(FALSE);
}
  
/* Route timeout handler. If route has already been marked for deletion
 * then delete it. Otherwise mark for deletion and restart timer.
 */
void
rt_timeout(s)
void *s;
{
    register struct route *rp = (struct route *)s;
  
    stop_timer(&rp->timer);
    if(rp->metric < RIP_METRIC_UNREACHABLE){
        rip_trace(5, "RIP:  route to [%s]/%d expired - hold down", inet_ntoa(rp->target),
        rp->bits);
        rp->metric = RIP_METRIC_UNREACHABLE;
        if(dur_timer(&rp->timer) == 0)
            set_timer(&rp->timer,Rip_ttl*1000);
      /* wait 2/3 of timeout before garbage collect */
        set_timer(&rp->timer,dur_timer(&rp->timer)*2/3);
        rp->timer.func = (void *)rt_timeout;
        rp->timer.arg = (void *)rp;
        start_timer(&rp->timer);
      /* Route changed; mark it for triggered update */
        rp->flags |= RTTRIG;
        rip_trigger();
    } else {
#if !defined(RIPAMPRGW) && defined(ENCAP) && (defined(UDP_DYNIPROUTE) || defined(MBOX_DYNIPROUTE))
        if (rp->iface == &Encap)
            /* Drop this route by adding it to the Loopback (bit
             * bucket) interface.  This will keep a 32-bit IP
             * address or smaller block isolated from being part
             * of a larger block that is already routed in the
             * machine which would prevent a temporary encap route
             * from being added in the future. - K2MF */
            rt_add(rp->target,rp->bits,0L,&Loopback,1L,0L,1);
        else {
#endif
        rip_trace(5, "RIP:  route to [%s]/%d expired - dropped", inet_ntoa(rp->target),
        rp->bits);
        rt_drop(rp->target,rp->bits);
#if !defined(RIPAMPRGW) && defined(ENCAP) && (defined(UDP_DYNIPROUTE) || defined(MBOX_DYNIPROUTE))
        }
#endif
    }
}
  
void
pullheader(ep,bpp)
struct rip_head *ep;
struct mbuf **bpp;
{
    ep->rip_cmd = pullchar(bpp);
    ep->rip_vers = pullchar(bpp);
    ep->rip_domain = (int16)pull16(bpp);
}
  
void rip_trace(short level, char *errstr, ...)
{
    if (level <= Rip_trace) {
        char *timestr;
        time_t timer;
        va_list argptr;
  
        if (Rip_trace_fname != NULLCHAR) {
            time(&timer);
            timestr = ctime(&timer);
            Rip_trace_file = fopen(Rip_trace_fname, APPEND_TEXT);
            fprintf(Rip_trace_file, "%.24s - ", timestr);
  
            va_start(argptr, errstr);
            vfprintf(Rip_trace_file, errstr, argptr);
            va_end(argptr);
            fprintf(Rip_trace_file, "\n");
            fclose(Rip_trace_file);
        } else {
            va_start(argptr, errstr);
            usvprintf(Curproc->output, errstr, argptr);
            va_end(argptr);
            tprintf("\n");
        }
    }
}
  
void
pullentry(ep,bpp)
register struct rip_route *ep;
struct mbuf **bpp;
{
    ep->rip_family = (int16)pull16(bpp);
    ep->rip_tag = (int16)pull16(bpp);
    ep->rip_dest = pull32(bpp);
    ep->rip_dest_mask = pull32(bpp);
    ep->rip_router = pull32(bpp);
    ep->rip_metric = pull32(bpp);
}
  
#ifdef RIP98
/* Write a single entry into a rip98 packet */
static char *
put98entry (cp, target, bits, metric)
register char *cp;
int32 target;
int bits;
int32 metric;

{
int metric8;
    metric8 = (metric && 0x000000ff);
        
    cp = put32 (cp, target);
    *cp++ = uchar (bits);
    *cp++ = uchar (metric8);
    return (char *) cp;
}

/* Pull a RIP98 Entry and converts to "more standard" RIP */
void
pull98entry (ep, bpp)
register struct rip_route *ep;
struct mbuf **bpp;
{
unsigned int mask_bits;
int metric_bits;   

    ep->rip_family = RIP_AF_INET;
    ep->rip_tag = 0;  /* pull16 (bpp) */
    ep->rip_dest = pull32 (bpp);
    mask_bits = pullchar (bpp);
    metric_bits = pullchar (bpp);
    ep->rip_dest_mask = (0xfffffffful << (32-mask_bits));
    ep->rip_metric = metric_bits;
    ep->rip_router = 0;

#if 0
/* Used in debugging, but left here for anyone who has need ! */
    rip_trace (9, "Rip98: Destination [%s] : mask %d : metric %d", inet_ntoa( ep->rip_dest), mask_bits, metric_bits);
#endif
   
}   
#endif /* RIP98 */
#endif /* RIP */
