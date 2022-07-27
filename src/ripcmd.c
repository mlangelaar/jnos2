/* RIP-related user commands
 *   Al Broscious, N3FCT
 *   Phil Karn, KA9Q
 *
 *  Changes Copyright (c) 1993 Jeff White - N0POY, All Rights Reserved.
 *  Permission granted for non-commercial copying and use, provided
 *  this notice is retained.
 *
 * Rehack for RIP-2 (RFC1388) by N0POY 4/1993
 *
 * Beta release 11/10/93 V0.91
 *
 * 2/19/94 release V1.0
 *
 */
  
#include <stdio.h>
#include "global.h"
#ifdef RIP
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "cmdparse.h"
#include "timer.h"
#include "iface.h"
#include "udp.h"
#include "rip.h"
#include "commands.h"
  
static struct cmds DFAR Ripcmds[] = {
    { "accept",   dodroprefuse,  0, 2, "rip accept <gateway> " },
    { "add",      doripadd,      0, 3, "rip add <dest> <interval> [<flags>] [<ripver>] [AUTH <password>] [RD <routing domain>]" },
    { "proxy",    doripproxy,    0, 4, "rip proxy <src> <dest> <interval> [<flags>] [AUTH <password>] [RD <routing domain>]" },
    { "drop",     doripdrop,     0, 2, "rip drop <dest> [<domain>]" },
    { "authadd",  doripauthadd,  0, 3, "rip authadd <interface> <routing domain> [<password>]" },
    { "authdrop", doripauthdrop, 0, 3, "rip authdrop <interface> <routing domain>" },
    { "reject",   doripreject,   0, 2, "rip reject <version>" },
    { "filter",   doripfilter,   0, 0, NULLCHAR },
    { "kick",     doripkick,     0, 0, NULLCHAR },
    { "merge",    doripmerge,    0, 0, NULLCHAR },
    { "refuse",   doaddrefuse,   0, 2, "rip refuse <gateway>" },
#ifdef RIP98
    { "rip98rx",  dorip98rx,     0, 0, NULLCHAR },
#endif
    { "request",  doripreq,      0, 2, NULLCHAR },
    { "status",   doripstat,     0, 0, NULLCHAR },
    { "trace",    doriptrace,    0, 0, NULLCHAR },
    { "ttl",      doripttl,      0, 0, NULLCHAR },
    { 0,		0,	0, 0, NULLCHAR }
};
  
// rip add 192.133.30.15 360 SUBPA 2 AUTH password RD 2 RT 0
// rip add <dest> <interval> <ripver> [<flags>] [AUTH <password>]
//    [RD <routing domain>]
// rip proxy <src> <dest> <interval> [<flags>] [AUTH <password>]
//    [RD <routing domain>]
// rip authadd <ifc> <rd> [<password>]
// rip authdrop <ifc> <rd>
// rip reject <ifc> <version>
// rip filter <on|off>
// rip drop <dest> <domain>
  
int
dorip(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Ripcmds,argc,argv,p);
}
  
/* Add an entry to the RIP output list */
  
int
doripadd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int x;
    char flags = RIP_SPLIT | RIP_BROADCAST | RIP_POISON;
    char ripver = 1;
    char rip_auth[RIP_AUTH_SIZE+1];
    int16 domain = 0;
    int16 route_tag = 0;
    int32 dest;
  
    for (x = 0; x < RIP_AUTH_SIZE+1; x++)    /* Null out the string */
        rip_auth[x] = '\0';
  
    strcpy(rip_auth, RIP_NO_AUTH);
  
    if (argc > 3)
        flags = htoi(argv[3]);
    if (argc > 4)
        ripver = atoi(argv[4]);
    if (argc > 5) {
        for (x = 5; x < argc; x++) {
            if (!strcmp(argv[x], "AUTH")) {
                x++;
                strcpy(rip_auth,argv[x]);
            } else if (!strcmp(argv[x], "RD")) {
                x++;
                domain = atoi(argv[x]);
            }
        }
    }
  
    dest = resolve(argv[1]);
	/* 11Oct2009, Maiko, Use atoi() for int32 vars */
    if (rip_add(dest,atoi(argv[2]),flags,ripver,rip_auth,domain,route_tag,0)) {
        return 0;
//      if (ripver > RIP_VERSION_1) {
//         struct route *rp;
//
//         rp = rt_lookup(dest);
//         return(ripauthadd(rp->iface->name,domain,rip_auth));
//      }
    } else {
        return 1;
    }
//  return 0;
}
  
/* Add a proxy entry to the RIP output list */
  
int
doripproxy(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int x;
    char flags = RIP_SPLIT | RIP_BROADCAST | RIP_POISON;
    char ripver = RIP_VERSION_2;
    char rip_auth[RIP_AUTH_SIZE+1];
    int16 domain = 0;
    int16 route_tag = 0;
    int32 dest;
    int32 proxy;
  
    for (x = 0; x < RIP_AUTH_SIZE+1; x++)    /* Null out the string */
        rip_auth[x] = '\0';
  
    strcpy(rip_auth, RIP_NO_AUTH);
  
    if (argc > 4)
        flags = htoi(argv[4]);
    if (argc > 5) {
        for (x = 5; x < argc; x++) {
            if (!strcmp(argv[x], "AUTH")) {
                x++;
                strcpy(rip_auth,argv[x]);
            } else if (!strcmp(argv[x], "RD")) {
                x++;
                domain = atoi(argv[x]);
            }
        }
    }
  
    dest = resolve(argv[2]);
    proxy = resolve(argv[1]);
	/* 11Oct2009, Maiko, Use atoi() for int32 vars */
    if (rip_add(dest,atoi(argv[3]),flags,ripver,rip_auth,domain,route_tag,proxy)) {
//      struct route *rp;
//      rp = rt_lookup(dest);
//      return(ripauthadd(rp->iface->name,domain,rip_auth));
    }
    return 1;
}
  
/* Add an entry to the RIP refuse list */
  
int
doaddrefuse(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return riprefadd(resolve(argv[1]));
}
  
/* Drop an entry from the RIP output list */
  
int
doripdrop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 domain = 0;
  
    if (argc > 2)
        domain = atoi(argv[2]);
  
    return rip_drop(resolve(argv[1]),domain);
}
  
/* Add an entry to the RIP authentication list */
  
int
doripauthadd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if (argc > 3) {
        return ripauthadd(argv[1],atoi(argv[2]),argv[3]);
    } else {
        return ripauthadd(argv[1],atoi(argv[2]),RIP_NO_AUTH);
    }
}
  
/* Drop an entry from the RIP authentication list */
  
int
doripauthdrop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return ripauthdrop(argv[1],atoi(argv[2]));
}
  
int
doripkick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct rip_list *rl;
  
    rl = (struct rip_list *)Rip_list;
    if(Rip_list != NULLRL)
        for(rl = Rip_list; rl != NULLRL; rl = rl->next)
            rip_shout(rl);
    return 0;
}
  
/* Drop an entry from the RIP refuse list */
  
int
dodroprefuse(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return riprefdrop(resolve(argv[1]));
}
  
/* Initialize the RIP listener */
  
int
doripinit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return rip_init();
}
  
int
doripstop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    del_udp(Rip_cb);
    Rip_cb = NULLUDP;
    return 0;
}
  
int
doripreq(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 replyport;
    int16 version;
  
    if (argc > 2)
        version = atoi(argv[2]);
    else
        version = 2;
  
    if(argc > 3)
        replyport = atoi(argv[3]);
    else
        replyport = RIP_PORT;
    return ripreq(resolve(argv[1]),replyport,version);
}
  
/* Dump RIP statistics */
int
doripstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct rip_list *rl;
    struct rip_refuse *rfl;
    struct rip_auth *ra;
    int cnt;
  
    for (cnt = 1; cnt < RIP_VERSIONS; cnt++) {
	/* 12Oct2009, Maiko, Use "%d" for int32 vars */
        tprintf("RIP V%d: sent %d rcvd %d reqst %d resp %d unk %d\n",
#ifdef RIP98
        (cnt == RIP_VERSION_X) ? RIP_VERSION_98 : cnt,
#else
        cnt,
#endif
        Rip_stat.vdata[cnt].output, Rip_stat.vdata[cnt].rcvd,
        Rip_stat.vdata[cnt].request, Rip_stat.vdata[cnt].response,
        Rip_stat.vdata[cnt].unknown);
    }
  
    tprintf("Version Errors:            %d   ",Rip_stat.version);
    tprintf("Address Family Errors:     %d\n",Rip_stat.addr_family);
    tprintf("Rip refusals:              %d   ",Rip_stat.refusals);
    tprintf("Wrong Domain on Interface: %d\n",Rip_stat.wrong_domain);
    tprintf("Authentication failures:   %d   ",Rip_stat.auth_fail);
    tprintf("Unknown Authentication:    %d\n",Rip_stat.unknown_auth);
  
    if(Rip_list != NULLRL){
        tprintf("Active RIP output interfaces:\n");
        tprintf("Ver Dest Addr       Int  Flags Domain Proxy           Authentication\n");
        for(rl=Rip_list; rl != NULLRL; rl = rl->next){
            tprintf("%-4d%-16s%-5u0x%-4X%-7u", rl->rip_version,
            inet_ntoa(rl->dest),rl->interval, rl->flags,rl->domain);
            tprintf("%-16s%-16s\n", inet_ntoa(rl->proxy_route), rl->rip_auth_code);
        }
    }
    if(Rip_refuse != NULLREF){
        tprintf("Refusing announcements from gateways:\n");
        for(rfl=Rip_refuse; rfl != NULLREF;rfl = rfl->next){
            if(tprintf("%s\n",inet_ntoa(rfl->target)) == EOF)
                break;
        }
    }
    if (Rip_auth != NULLAUTH) {
        tprintf("\nAuthentications accepted:\n");
        tprintf("Interface           Domain   Password\n");
        for (ra = Rip_auth; ra != NULLAUTH; ra = ra->next) {
            tprintf("%-20s%-9u%-16s\n",ra->ifc_name,ra->domain,ra->rip_auth_code);
        }
    }
    tprintf("Refusing versions less than or equal to V%d\n",Rip_ver_refuse);
  
    return 0;
}
  
int
doriptrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if (argc > 1) {
        Rip_trace = atoi(argv[1]);
        if ((Rip_trace == 0) && (Rip_trace_file != NULLFILE)) {  // Turn off tracing
            fclose(Rip_trace_file);
            Rip_trace_file = NULLFILE;
            if (Rip_trace_fname != NULLCHAR) {
                free(Rip_trace_fname);
                Rip_trace_fname = NULLCHAR;
            }
        }
        if (argc > 2) {
            if ((Rip_trace_file = fopen(argv[2],APPEND_TEXT)) == NULLFILE) {
                tprintf("Cannot write to %s\n",argv[2]);
            } else {
                Rip_trace_fname = j2strdup(argv[2]);
            }
            fclose(Rip_trace_file);
        }
    } else {
        if (Rip_trace_file != NULLFILE) {
            tprintf("Tracing RIP status level %d to file %s\n", Rip_trace,
            Rip_trace_fname);
        } else {
            tprintf("Tracing RIP status level %d\n", Rip_trace);
        }
    }
    return 0;
}
  
int doripttl (int argc, char **argv, void *p)
{
    return setint32 (&Rip_ttl, "RIP route ttl", argc, argv);
}
  
int
doripreject(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setshort(&Rip_ver_refuse,"RIP version refusal level",argc,argv);
}
  
int
doripmerge(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool((int *)&Rip_merge,"RIP merging",argc,argv);
}
  
int
doripfilter(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool((int *)&Rip_default_refuse,"RIP default refusal",argc,argv);
}
  
#ifdef RIP98
/* Facility to ignore incoming RIP98 frames when rip server is active */
int
dorip98rx(argc,argv,p)
int argc;
char *argv[];
void *p;
{
   return setbool (&Rip98allow, "Rip 98 reception", argc, argv);
}
#endif /* RIP98 */
#endif /* RIP */
  
