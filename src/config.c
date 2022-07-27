/*
 * JNOS 2.0
 *
 * $Id: config.c,v 1.9 2012/03/20 15:58:52 ve4klm Exp ve4klm $
 *
 * A collection of stuff heavily dependent on the configuration info
 * in config.h. The idea is that configuration-dependent tables should
 * be located here to avoid having to pepper lots of .c files with #ifdefs,
 * requiring them to include config.h and be recompiled each time config.h
 * is modified.
 *
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Mods by PA0GRI and VE4KLM
 */
#include <stdio.h>
#ifdef MSDOS
#include <dos.h>
#include <conio.h>
#endif
#include "global.h"
#include "config.h"
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#ifdef  ARCNET
#include "arcnet.h"
#endif
#include "lapb.h"
#include "ax25.h"
#include "enet.h"
#include "kiss.h"
#include "netrom.h"
#include "nr4.h"
#include "pktdrvr.h"
#include "ppp.h"
#include "slip.h"
#include "arp.h"
#include "icmp.h"
#include "unix.h"
#include "smtp.h"
#include "usock.h"
#include "cmdparse.h"
#include "commands.h"
#include "mailbox.h"
#include "mailcli.h"
#include "ax25mail.h"
#include "nr4mail.h"
#include "tipmail.h"
#include "bootp.h"
#include "daemon.h"
#include "slhc.h"
#include "rspf.h"
#include "main.h"
#if defined(UNIX) && defined(STATUSWIN)
#include "sessmgr.h"
#endif

#ifdef EA5HVK_VARA
extern int dovaracmd (int, char **argv, void *p);
#endif

static int docls __ARGS((int argc,char *argv[],void *p));
static int dostart __ARGS((int argc,char *argv[],void *p));
static int dostop __ARGS((int argc,char *argv[],void *p));
static void network __ARGS((int,void *,void *));

#ifdef APPLETALK
/* The appletalk.c source is missing... */
extern int at_attach();
extern int at_forus();
extern void at_dump();
#endif

#ifdef  NNTPS
extern int donntpcli __ARGS((int argc,char *argv[],void *p));
extern int nntp0 __ARGS((int argc,char *argv[],void *p));
extern int nntp1 __ARGS((int argc,char *argv[],void *p));
#endif

#ifdef HTTPVNC
extern int httpvnc1 (int, char*[], void*);
extern int httpvnc0 (int, char*[], void*);
extern int httpvncS (int, char*[], void*);
#endif

#ifdef  CONVERS
int conv0 __ARGS((int argc,char *argv[],void *p));
int conv1 __ARGS((int argc,char *argv[],void *p));
#endif

#ifdef RSYSOPSERVER
int rsysop0 __ARGS((int argc,char *argv[],void *p));
int rsysop1 __ARGS((int argc,char *argv[],void *p));
#endif

#ifdef TRACESERVER
int trace0 __ARGS((int argc,char *argv[],void *p));
int trace1 __ARGS((int argc,char *argv[],void *p));
#endif
#ifdef SNMPD
int snmpd1 (int, char**, void*);
/* 23Feb2012, Maiko, New function so I can ask for configuration info */
extern int dosnmp (int argc, char **argv, void *p);
#endif
#ifdef TERMSERVER
int term0 __ARGS((int argc,char *argv[],void *p));
int term1 __ARGS((int argc,char *argv[],void *p));
#endif

#ifdef  AX25
static void axip __ARGS((struct iface *iface,struct ax25_cb *axp,char *src,
char *dest,struct mbuf *bp,int mcast));
static void axarp __ARGS((struct iface *iface,struct ax25_cb *axp,char *src,
char *dest,struct mbuf *bp,int mcast));
#ifdef RARP
static void axrarp __ARGS((struct iface *iface,struct ax25_cb *axp,char *src,
char *dest,struct mbuf *bp,int mcast));
#endif
#ifdef NETROM
static void axnr __ARGS((struct iface *iface,struct ax25_cb *axp,char *src,
char *dest,struct mbuf *bp,int mcast));
#endif
#endif

#if defined(EXPIRY) && defined(WPAGES)
 /* 01Feb2012, Maiko, Incorporate Lantz TNOS WP code */
extern int dooldwpages (int argc, char **argv, void *p);
#endif

struct mbuf *Hopper;

/*
 * 07Mar2009, Maiko, changing this from unsigned to int, which
 * resolves a few comparison between unsigned/signed warnings.
 */
int Nsessions = NSESSIONS;

#ifndef UNIX
/* Free memory threshold, below which things start to happen to conserve
 * memory, like garbage collection, source quenching and refusing connects
 */
int32 Memthresh = MTHRESH;

int Nibufs = NIBUFS;            /* Number of interrupt buffers */
unsigned Ibufsize = IBUFSIZE;   /* Size of each interrupt buffer */
#endif

/* Transport protocols atop IP */
struct iplink Iplink[] = {
    { TCP_PTCL,       tcp_input },
    { UDP_PTCL,       udp_input },
    { ICMP_PTCL,      icmp_input },
#ifdef ENCAP
    { IP_PTCL,        ipip_recv },
    { IP_PTCL_OLD,    ipip_recv },
#endif
#ifdef  AXIP
    { AX25_PTCL,      axip_input },
#endif
#ifdef  RSPF
    { RSPF_PTCL,      rspf_input },
#endif  /* RSPF */
    { 0,               0 }
};

/* Transport protocols atop ICMP */
struct icmplink Icmplink[] = {
    { TCP_PTCL,       tcp_icmp },
    { 0,              0 }
};

/* ARP protocol linkages */
struct arp_type Arp_type[NHWTYPES] = {
#ifdef  NETROM
    { AXALEN, 0, 0, 0, 0, NULLCHAR, pax25, setcall },   /* ARP_NETROM */
#else
    { 0, 0, 0, 0, 0, NULLCHAR,NULL,NULL },
#endif

#ifdef  ETHER
    { EADDR_LEN,IP_TYPE,ARP_TYPE,REVARP_TYPE,1,Ether_bdcst,pether,gether }, /* ARP_ETHER */
#else
    { 0, 0, 0, 0, 0, NULLCHAR,NULL,NULL },
#endif

    { 0, 0, 0, 0, 0, NULLCHAR,NULL,NULL },              /* ARP_EETHER */

#ifdef  AX25
    { AXALEN, PID_IP, PID_ARP, PID_RARP, 10, Ax25multi[0], pax25, setcall },
#else
    { 0, 0, 0, 0, 0, NULLCHAR,NULL,NULL },              /* ARP_AX25 */
#endif

    { 0, 0, 0, 0, 0, NULLCHAR,NULL,NULL },              /* ARP_PRONET */

    { 0, 0, 0, 0, 0, NULLCHAR,NULL,NULL },              /* ARP_CHAOS */

    { 0, 0, 0, 0, 0, NULLCHAR,NULL,NULL },              /* ARP_IEEE802 */

#ifdef  ARCNET
    { AADDR_LEN, ARC_IP, ARC_ARP, 0, 1, ARC_bdcst, parc, garc }, /* ARP_ARCNET */
#else
    { 0, 0, 0, 0, 0, NULLCHAR,NULL,NULL },
#endif

    { 0, 0, 0, 0, 0, NULLCHAR,NULL,NULL }              /* ARP_APPLETALK */
};

#ifdef  AX25
/* Linkage to network protocols atop ax25 */
struct axlink Axlink[] = {
    { PID_IP,         axip },
    { PID_ARP,        axarp },
#ifdef RARP
    { PID_RARP,   axrarp },
#endif
#ifdef  NETROM
    { PID_NETROM,     axnr },
#endif
    { PID_NO_L3,      axnl3 },
    { 0,              NULL }
};
#endif

#ifdef  MAILCLIENT
struct daemon Mailreaders[] = {
#ifdef  POP2CLIENT
    { "POP2",         2048,   pop2_job },
#endif
#ifdef  POP3CLIENT
    { "POP3",         2048,   pop3_job },
#endif
    { NULLCHAR,		0,	NULL }
};
#endif

#ifdef  MAILBOX
void (*Listusers) __ARGS((int s)) = listusers;
#else
void (*Listusers) __ARGS((int s)) = NULL;
#endif

/* 30Sep2011, Maiko, Just happened to run into stack problems */
#ifdef	INP2011
#define NWSTACK 4096
#else
#if defined(ETHER) || defined(UNIX)
#define NWSTACK 2048
#else
#define NWSTACK 1024
#endif
#endif	/* end of INP2011 */


/* daemons to be run at startup time */
struct daemon Daemons[] = {
    { "killer",       1024,   killer },
#ifdef UNIX
    { "timer",        2048,   timerproc },
#ifdef STATUSWIN
    { "statuswin",    1024,   StatusRefresh },
#endif
#else
    { "timer",        1024,   timerproc },
#endif
    { "network",      NWSTACK,network },
#ifndef HEADLESS
    { "keyboard",     512,    keyboard },
#endif
#ifndef UNIX
#ifdef CONVERS
    { "gcollect",     512,    gcollect },
#else
    { "gcollect",     256,    gcollect },
#endif
#endif
    { NULLCHAR,       0,      NULLVFP((int, void *, void *)) }
};

#ifdef	TUN
extern int tun_send __ARGS((struct mbuf *bp,struct iface *iface,int32 gateway,int prec, int del, int tput, int rel));
#endif

struct iftype Iftypes[] = {
    /* This entry must be first, since Loopback refers to it */
    { "None",         NULL,           NULL,           NULL,
    NULL,           CL_NONE,        0 },

#ifdef  AX25
    { "AX25",         ax_send,        ax_output,      pax25,
    setcall,        CL_AX25,        AXALEN },
#endif

#ifdef  SLIP
    { "SLIP",         slip_send,      NULL,           NULL,
    NULL,           CL_NONE,        0 },
#endif

#ifdef  ETHER
    /* Note: NULL is specified for the scan function even though
     * gether() exists because the packet drivers don't support
     * address setting.
     */
    { "Ethernet",     enet_send,      enet_output,    pether,
    NULL,           CL_ETHERNET,    EADDR_LEN },
#endif

#ifdef  NETROM
    { "NETROM",       nr_send,        NULL,           pax25,
    setcall,        CL_NETROM,      AXALEN },
#endif

#ifdef  SLFP
    { "SLFP",         pk_send,        NULL,           NULL,
    NULL,           CL_NONE,        0 },
#endif

#ifdef  PPP
    { "PPP",          ppp_send,       ppp_output,     NULL,
    NULL,           CL_PPP,         0 },
#endif

#ifdef  TUN
    { "TUN",         tun_send,      NULL,           NULL,
    NULL,           CL_NONE,        0 },
#endif

    { NULLCHAR,			NULL,			NULL,		NULL,
    NULL,           CL_NONE,        0 }
};

/* Command lookup and branch tables */
struct cmds DFAR Cmds[] = {
    /* The "go" command must be first */
    { "",             go,             0, 0, NULLCHAR },
#ifndef AMIGA
#ifdef SHELL
    { "!",            doshell,        0, 0, NULLCHAR },
#endif
#endif
#ifdef FTPSESSION
    { "abort",    doabort,    0, 0, NULLCHAR },
#endif
#ifdef AMIGA
    { "amiga",        doamiga,        0, 0, NULLCHAR },
#endif
#if defined(MAC) && defined(APPLETALK)
    { "applestat",    doatstat,       0,      0, NULLCHAR },
#endif
#if defined(AX25) || defined(ETHER) || defined(APPLETALK)
    { "arp",          doarp,          0, 0, NULLCHAR },
#endif
#ifdef  APRSD
	{ "aprs",			doaprs,			0,  0,  NULLCHAR },
	{ "aprsc",		dombaprs,	1024,	0,	NULLCHAR },
#endif
#ifdef ASY
    { "asystat",      doasystat,      0, 0, NULLCHAR },
#ifdef UNIX
    { "asyconfig",    doasyconfig,    0, 2, "asyconfig <iface> <parameter>" },
#endif
#endif
#ifdef ATCMD
    { "at",       doat,       0, 0, NULLCHAR },
#endif
    { "attach",       doattach,       0, 2,
    "attach <hardware> <hw specific options>" },
#ifdef TTYLINKSERVER
    { "attended",     doattended,     0, 0, NULLCHAR },
#endif
#ifdef  AX25
#ifdef AUTOROUTE
    { "autoroute",doax25autoroute, 0, 0, NULLCHAR },
#endif /* AUTOROUTE */
    { "ax25",     doax25,     0, 0, NULLCHAR },
#endif /* AX25 */
#ifdef AXUISESSION
    { "axui",     doaxui,     1024, 2,
    "axui <interface> [<callsign>]" },
#endif
#ifdef BBSSESSION
    { "bbs",      dotelnet,   1024, 0, NULLCHAR },
#endif
#ifdef  BOOTPCLIENT
    { "bootp",    dobootp,    0, 0, NULLCHAR },
#endif
#ifdef BOOTPSERVER
    { "bootpd",   bootpdcmd,  0, 0,  NULLCHAR },
#endif
#ifdef BPQ
    { "bpq",      dobpq,      0, 0,  NULLCHAR },
#endif
#ifdef RLINE
    { "bulletin", dombrline,  0, 0,  NULLCHAR },
#endif
/* This one is out of alpabetical order to allow abbreviation to "c" */
#ifdef AX25SESSION
    { "connect",  doconnect,  1024, 3,    "connect <interface> <callsign>" },
#endif
#if defined(CALLCLI) /* redundant: || defined SAMCALLB || defined QRZCALLB */
    { "callbook", docallbook, 1024, 2, "callbook CALLSIGN" },
#endif
#if defined(CALLSERVER) || defined(CALLCLI) /* redundant: || defined SAMCALLB || defined QRZCALLB */
    { "callserver2", docallserver, 0, 0, NULLCHAR },
#endif
#if !defined(AMIGA)
#ifdef DOSCMD
    { "cd",       docd,       0, 0, NULLCHAR },
#endif
#if defined(CALLSERVER) || defined(BUCKTSR)
    { "cdrom", docdrom, 0, 0, NULLCHAR },
#endif
#endif
    { "close",        doclose,        0, 0, NULLCHAR },
#ifndef HEADLESS
    { "cls",      docls,      0, 0, NULLCHAR },
#endif
#ifdef ASY
    { "comm",     doasycomm,  0, 3,"comm <interface> <text-string>" },
#endif
#ifdef CONVERS
    { "convers",  doconvers,  0,  0, NULLCHAR },
#endif
#ifdef DOSCMD
    { "copy",     docopy,     0, 3,"copy <file> <newfile>" },
#endif
    /* 24Apr2021, Maiko (VE4KLM), New - print date to console */
    { "date",   dodate,   0, 0, NULLCHAR },
/* This one is out of alpabetical order to allow abbreviation to "d" */
#ifdef SESSIONS
    { "disconnect",   doclose,    0, 0, NULLCHAR },
#endif
#ifndef AMIGA
#ifdef DIRSESSION
    { "dir",      dodir,      0, 0, NULLCHAR }, /* note sequence */
#endif
#endif
#ifdef DOSCMD
    { "delete",   dodelete,   0, 2, "delete <file>" },
#endif
    { "detach",   dodetach,   0, 2, "detach <interface>" },
#ifdef  DIALER
    { "dialer",       dodialer,       512, 2,
    "dialer <iface> [<file> [<seconds> [<pings> [<hostid>]]]]" },
#endif
    { "domain",       dodomain,       0, 0, NULLCHAR },
#ifdef  DRSI
    { "drsistat",     dodrstat,       0, 0, NULLCHAR },
#endif
#if defined(ALLCMD) && defined(DOMDUMP)
/* 06Oct2009, Maiko, This doesn't look useful anymore, was it ever ? */  
    { "dump",     domdump,    0, 0, NULLCHAR },
#endif
#ifdef EDITOR
    { "editor",   editor,   512, 2, "editor <file>" },
#endif
#ifdef MAILERROR
    { "errors",   doerror,    0, 0, NULLCHAR },
#endif
#ifdef  EAGLE
    { "eaglestat",    doegstat,       0, 0, NULLCHAR },
#endif
    { "echo",         doecho,         0, 0, NULLCHAR },
/* #if defined(MD5AUTHENTICATE) && defined(LZW) && defined(TTYLINKSESSION)
 *   "elzwlink",     dotelnet,    1024, 4, "elzwlink <address> [port] loginid passwd",
 * #endif
 */
    { "eol",          doeol,          0, 0, NULLCHAR },
#if     (!defined(MSDOS) || defined(ESCAPE))
    { "escape",       doescape,       0, 0, NULLCHAR },
#endif
#if defined(MD5AUTHENTICATE) && defined(TELNETSESSION)
    { "etelnet",      dotelnet,    1024, 4, "etelnet <address> [port] loginid passwd" },
#endif
#ifdef  PC_EC
    { "etherstat",    doetherstat,    0, 0, NULLCHAR },
#endif
#if defined(MD5AUTHENTICATE) && defined(TTYLINKSESSION)
    { "ettylink",     dotelnet,    1024, 4, "ettylink <address> [port] loginid passwd" },
#endif
    { "exit",         doexit,         0, 0, NULLCHAR },
#ifdef EXPIRY
    { "expire",   doexpire,   0, 0, NULLCHAR },
#endif
#ifdef FINGERSESSION
    { "finger",   dofinger,   1024, 2, "finger name@host" },
#endif
#if defined(MSDOS) && defined(ALLCMD)
    { "fkey",         dofkey,         0, 0, NULLCHAR },
#endif
#ifdef FTPSESSION
    { "ftp",      doftp,      2048, 2, "ftp <address>" },
    { "ftype",    doftype,    0, 0, NULLCHAR },
#ifdef LZW
    { "ftpclzw",  doftpclzw,  0, 0, NULLCHAR },
    { "ftpslzw",  doftpslzw,  0, 0, NULLCHAR },
#endif
#endif
#ifdef FTPSERVER
    { "ftpmaxservers", doftpmaxsrv, 0, 0, NULLCHAR },
    { "ftptdisc", doftptdisc, 0, 0, NULLCHAR },
#endif
#ifdef TCPGATE
    { "gate",         dogate,         0, 0, NULLCHAR },
#endif
#ifdef HAPN
    { "hapnstat",     dohapnstat,     0, 0, NULLCHAR },
#endif
    { "help",         dohelp,         0, 0, NULLCHAR },
/* 09Mar2005, Maiko, New HFDD commands required */
#ifdef  HFDD
	{ "hfdd",			dohfdd,			0, 0, NULLCHAR },
#endif
    { "history",  dohistory,  0, 0, NULLCHAR },
#ifdef HOPCHECKSESSION
    { "hop",          dohop,          0, 0, NULLCHAR },
#endif
    { "hostname",     dohostname,     0, 0, NULLCHAR },
#ifdef  HS
    { "hs",           dohs,           0, 0, NULLCHAR },
#endif
#ifdef HTTP
    { "http",         dohttp,         0, 0, NULLCHAR },
#endif
#ifdef HTTPVNC
    { "hvs",          httpvncS,     0, 0, NULLCHAR },
#endif
    { "icmp",         doicmp,         0, 0, NULLCHAR },
    { "ifconfig",     doifconfig,     0, 0, NULLCHAR },
#ifdef ALLCMD
    { "info",         doinfo,         0, 0, NULLCHAR },
#endif
    { "index",        doindex,     2048, 2, "index <mailbox>" },
#ifdef INP2011
	/* 03Nov2013, Maiko, Need to configure some INP stuff */
    { "inp",		doinp,       0, 0, NULLCHAR },
#endif  /* NETROM */
    { "ip",           doip,           0, 0, NULLCHAR },
#ifdef  MSDOS
    { "isat",         doisat,         0, 0, NULLCHAR },
#endif
    { "kick",         dokick,         0, 0, NULLCHAR },
#ifdef LOCK
    { "lock",         dolock,         0, 0, NULLCHAR },
#endif
    { "log",      dolog,      0, 0, NULLCHAR },
#ifdef LOOKSESSION
    { "look",     dolook,     1024, 2, "look <user|sock#>" },
#endif
#ifdef LZW
    { "lzw",      dolzw,      0, 2, "lzw <mode|bits|trace>" },
#ifdef TTYLINKSESSION
    { "lzwlink",  dotelnet,   1024, 2, "lzwlink <address> [port]" },
#endif
#endif
#if defined(ALLCMD) && !defined(UNIX)
    { "mail",     dobmail,    0, 0, NULLCHAR },
#endif
#ifdef MAILMSG
    { "mailmsg",   domailmsg,         0, 3, "mailmsg <to> [<subj>] <msg|/pathname>" },
#endif
#ifdef  MAILBOX
    { "mbox",         dombox,         0, 0, NULLCHAR },
#endif
#ifndef UNIX
    { "memory",       domem,          0, 0, NULLCHAR },
#endif
#ifdef DOSCMD
    { "mkdir",    domkd,      0, 2, "mkdir <directory>" },
#endif
#ifdef  AX25
    { "mode",         domode,         0, 2, "mode <interface>" },
#endif
#ifdef MORESESSION
    { "more",     domore,     0, 2, "more <file> [searchstring]" },
#endif
#ifdef ALLCMD
    { "motd",         domotd,         0, 0, NULLCHAR },
#endif
#ifdef MSDOS
#ifdef MULTITASK
    { "multitask",    dobackg,    0, 0, NULLCHAR },
#endif
#endif
#ifdef NETROM
    { "netrom",       donetrom,       0, 0, NULLCHAR },
#endif  /* NETROM */
#if defined(NNTP) || defined(NNTPS)
    { "nntp",         donntp,         0, 0, NULLCHAR },
#endif
#ifdef NRS
    { "nrstat",       donrstat,       0, 0, NULLCHAR },
#endif
#ifdef EXPIRY
    { "oldbid",   dooldbids,  0, 0, NULLCHAR },
#endif
    { "param",        doparam,        0, 2, "param <interface>" },
    { "pause",        dopause,        0, 0, NULLCHAR },
#ifdef PINGSESSION
    { "ping",         doping,         512, 2,
    "ping <hostid> [<length> [<interval> [incflag]]]" },
#endif
#ifdef PI
    { "pistatus",     dopistat,       0, 0, NULLCHAR },
#endif
#ifdef PACKET
    { "pkstat",       dopkstat,       0, 0, NULLCHAR },
#endif
#ifdef MAILCLIENT
    { "popmail",      domsread,       0, 0, NULLCHAR },
#endif
#ifdef PPP
    { "ppp",          doppp_commands, 0, 0, NULLCHAR },
#endif
    { "prompt",       doprompt,       0, 0, NULLCHAR },
    { "ps",           ps,             0, 0, NULLCHAR },
#if !defined(AMIGA)
#ifdef DOSCMD
    { "pwd",      docd,       0, 0, NULLCHAR },
#endif
#endif
#if (defined(AX25) || defined(ETHER))
#ifdef RARP
    { "rarp",     dorarp,     0, 0, NULLCHAR },
#endif /* RARP */
#endif
#ifdef RDATECLI
    { "rdate",    dordate,    0, 0, NULLCHAR },
#endif
#ifdef ALLCMD
    { "record",   dorecord,   0, 0, NULLCHAR },
#endif
    { "remark",   doremark,   0, 0, NULLCHAR },
#if defined(REMOTESERVER) || defined(REMOTECLI)
    { "remote",   doremote,   0, 3, "remote"
#ifdef REMOTESERVER
                                  " -s syskey"
#if defined(ENCAP) && defined(UDP_DYNIPROUTE)
                                  " | -g gwkey"
#endif
#endif /* REMOTESERVER */
#ifdef REMOTECLI
#ifdef REMOTESERVER
                                  " |"
#endif
                                  " [-p port] [-k key] [-a kickaddr]"
#if defined(ENCAP) && defined(UDP_DYNIPROUTE)
                                  " [-r addr/#bits]"
#endif
                                  " <hostname> exit|reset|kick"
#if defined(ENCAP) && defined(UDP_DYNIPROUTE)
                                  "|add|drop|udpadd"
#endif
#endif /* REMOTECLI */
    },
#endif /* REMOTESERVER || REMOTECLI */
#ifdef DOSCMD
    { "rename",   dorename,   0, 3, "rename <oldfile> <newfile>" },
#endif
#ifdef REPEATSESSION
    { "repeat",   dorepeat, 512, 3, "repeat <interval> <command> [args...]" },
#endif
    { "reset",    doreset,    0, 0, NULLCHAR },
    { "rewrite",  dorewrite,  0, 2, "rewrite <address>" },
#ifdef  RIP
    { "rip",          dorip,          0, 0, NULLCHAR },
#endif
#ifdef RLOGINSESSION
    { "rlogin",       dorlogin,       2048, 2, "rlogin <address>" },
#endif
#ifdef DOSCMD
    { "rmdir",    dormd,      0, 2, "rmdir <directory>" },
#endif
    { "route",    doroute,    0, 0, NULLCHAR },
#ifdef  RSPF
    { "rspf",         dorspf,         0, 0, NULLCHAR },
#endif  /* RSPF */
#ifdef SESSIONS
    { "session",      dosession,      0, 0, NULLCHAR },
#endif
#ifdef UNIX
#ifndef HEADLESS
    { "sessmgr",      dosessmgr,      0, 0, NULLCHAR },
#endif
#endif
#ifdef  SCC
    { "sccstat",      dosccstat,      0, 0, NULLCHAR },
#endif
#if     !defined(AMIGA)
#ifdef SHELL
    { "shell",    doshell,    0, 0, NULLCHAR },
#endif
#endif
    { "skick",    dokicksocket,0,2, "skick <socket#>" },
    { "smtp",     dosmtp,     0, 0, NULLCHAR },
#ifdef SNMPD
	/* 23Feb2012, Maiko, Commands like iface # to port mappings */
    { "snmp",       dosnmp,           0, 0, NULLCHAR },
#endif
    { "socket",       dosock,         0, 0, NULLCHAR },
    { "source",       dosource,       0, 2, "source <filename>" },
#ifdef SERVERS
    { "start",        dostart,        0, 2, "start <servername>" },
    { "stop",         dostop,         0, 2, "stop <servername>" },
#endif
    { "status",   dostatus,   0, 0, NULLCHAR },
#if defined(AX25SESSION) && defined(SPLITSCREEN)
    { "split",    doconnect,  1024, 3, "split <interface> <callsign>" },
#endif
    { "tcp",      dotcp,      0, 0, NULLCHAR },
#ifdef ALLCMD
    { "tail",     dotail,     0, 2, "tail <filename>" },
    { "taillog",  dotaillog,  0, 0, NULLCHAR },
#endif
#ifdef TELNETSESSION
    { "telnet",   dotelnet,   1024, 2, "telnet <address> [port]" },
#endif
#ifdef TERMSERVER
    { "term",     doterm,     0,  0, NULLCHAR },
#endif
#ifdef TTYLINKSESSION
    { "ttylink",  dotelnet,  1024, 2, "ttylink <address> [port]" },
#endif
#ifdef  MAILBOX
    { "third-party",  dothirdparty,   0, 0, NULLCHAR },
#endif
#ifdef TIPSESSION
    { "tip",      dotip,      256, 2, "tip <iface (async only!)>" },
#endif
#ifdef  PACKETWIN
    { "tsync_stat",   do_tsync_stat,  0, 0, NULLCHAR },
    { "tsydump",              do_tsync_dump,  0, 2, "tsydump <channel>" },
#endif
#ifdef  TRACE
    { "trace",        dotrace,        0, 0, NULLCHAR },
    { "strace",   dostrace,   0, 0, NULLCHAR },
#endif
    { "udp",          doudp,          0, 0, NULLCHAR },
#ifdef ALLCMD
    { "upload",   doupload,   0, 0, NULLCHAR },
#endif
#ifdef EA5HVK_VARA
    { "varacmd",   dovaracmd,   0, 0, NULLCHAR },
#endif
#ifdef  MSDOS
#ifdef ALLCMD
#ifdef SWATCH
    { "watch",    doswatch,   0, 0, NULLCHAR },
#endif
#endif
    { "watchdog", dowatchdog, 0, 0, NULLCHAR },
#endif
#if defined(EXPIRY) && defined(WPAGES)
   /* 01Feb2012, Maiko (VE4KLM), try to incorporate Lantz TNOS WP code */
    { "wpages", dooldwpages, 0, 0, NULLCHAR },
#endif
    { "write",    dowrite,    0, 3, "write <user|sock#> <msg>" },
#ifdef MAILBOX
    { "writeall", dowriteall, 0, 2, "writeall <msg>" },
#endif
    { "?",        dohelp,     0, 0, NULLCHAR },
    { NULLCHAR,       NULLFP((int,char**,void*)),         0, 0,
    "Unknown command; type \"?\" for list" }
};

#ifdef	TUN
extern int tun_attach __ARGS((int argc,char *argv[],void *p));
#endif

#ifdef BAYCOM_SER_FDX
/* 15Nov08, Maiko (VE4KLM), New interface to linux kernel baycom module */
extern int baycom_attach __ARGS((int argc,char *argv[],void *p));
#endif	/* End of BAYCOM_SER_FDX */

#ifdef MULTIPSK
/* 05Feb09, Maiko (VE4KLM), New interface to MULTI-PSK tcp/ip control */
extern int mpsk_attach (int argc,char *argv[],void *p);
#endif	/* End of MULTIPSK */

#ifdef AGWPE
/* 23Feb2012, Maiko (VE4KLM), New interface to AGWPE tcp/ip control */
extern int agwpe_attach (int argc,char *argv[],void *p);
#endif	/* End of AGWPE */

#ifdef WINRPR
/* 02Nov2020, Maiko (VE4KLM), New interface to WINRPR tcp/ip control */
extern int winrpr_attach (int argc,char *argv[],void *p);
#endif	/* End of WINRPR */

#ifdef EA5HVK_VARA
/* 05Jan2021, Maiko (VE4KLM), New interface to VARA modem */
extern int vara_attach (int argc,char *argv[],void *p);
#endif	/* End of EA5HVK_VARA */

#ifdef	INP2011
extern int doINPtimer (int argc, char **argv, void* p);
#endif

/* List of supported hardware devices */
struct cmds DFAR Attab[] = {

#ifdef  PC_EC
    /* 3-Com Ethernet interface */
    { "3c500", ec_attach, 0, 7,
    "attach 3c500 <address> <vector> arpa <label> <buffers> <mtu> [ip_addr]" },
#endif

#ifdef  ASY
    /* Ordinary PC asynchronous adaptor */
    { "asy",  asy_attach, 0, 8,
    "attach asy <address> <vector> "

#ifdef SLIP
    "slip"
#endif

#ifdef AX25
#ifdef SLIP
    "|"
#endif
    "ax25"
#endif

#ifdef NRS
#if ((defined SLIP) || (defined AX25))
    "|"
#endif
    "nrs"
#endif

#ifdef POLLEDKISS
#if ((defined SLIP) || (defined AX25) || (defined NRS))
    "|"
#endif
    "pkiss"
#endif

#ifdef PPP
#if ((defined SLIP) || (defined AX25) || (defined NRS) || (defined POLLEDKISS))
    "|"
#endif
    "ppp"
#endif

    " <label> <bufsize> <mtu> <speed> [options]" },
#endif  /* ASY */

#ifdef BPQ
    /* g8bpq_host */
    { "bpq", bpq_attach, 0, 3,
    "attach bpq init <vec> <stream>\n"
    "attach bpq <port> <label> [mtu] [callsign]" },
#endif

#ifdef FLDIGI_KISS
    /* FLDIGI KISS interface piggy-backing on an AXUDP interface. */
    { "fldigi", fldigi_attach, 0, 1,
    "attach fldigi <axudp port>" },
#endif

#ifdef  KISS
    /* Multi-port KISS interface piggy-backing on ASY interface. */
    { "kiss", kiss_attach, 0, 4,
    "attach kiss <asy_interface_label> <port> <label> [mtu]" },
#endif  /* KISS */

#ifdef  PC100
    /* PACCOMM PC-100 8530 HDLC adaptor */
    { "pc100", pc_attach, 0, 8,
    "attach pc100 <address> <vector> ax25 <label> <bufsize>\
    <mtu> <speed> [ip_addra] [ip_addrb]" },
#endif

#ifdef  DRSI
    /* DRSI PCPA card in low speed mode */
    { "drsi", dr_attach, 0, 8,
    "attach drsi <address> <vector> ax25 <label> <bufsize> <mtu>\
    <chan a speed> <chan b speed> [ip addr a] [ip addr b]" },
#endif

#ifdef  EAGLE
    /* EAGLE RS-232C 8530 HDLC adaptor */
    { "eagle", eg_attach, 0, 8,
    "attach eagle <address> <vector> ax25 <label> <bufsize>\
    <mtu> <speed> [ip_addra] [ip_addrb]" },
#endif

#ifdef SCC
	{ "escc", scc_attach, 0, 5,
/*	"attach escc <board label> baycom|drsi|opto <addr> <vec> [t<tickchan>]\n"*/
	"attach escc <board label> <devices> init <addr> <spacing> <Aoff> <Boff> <Dataoff>"
	" <intack> <vec> [p]<clock> [[hdwe] [param] [t<tickchan>]]\n"
	"attach escc <board label> <chan> "
#ifdef SLIP
	  "slip"
#if defined(KISS) || (defined AX25) || defined(NRS)
	  "|"
#endif
#endif
#ifdef KISS
	  "kiss"
#if (defined AX25) || defined(NRS)
	  "|"
#endif
#endif
#ifdef AX25
	  "ax25"
#ifdef NRS
	  "|"
#endif
#endif
#ifdef NRS
	  "nrs"
#endif
	  " <label> <mtu> <speed> <bufsize> [[callsign] [s]]\n" },
#endif /* SCC */

#ifdef  HAPN
    /* Hamilton Area Packet Radio (HAPN) 8273 HDLC adaptor */
    { "hapn", hapn_attach, 0, 8,
    "attach hapn <address> <vector> ax25 <label> <rx bufsize>\
    <mtu> csma|full [ip_addr]" },
#endif

#ifdef  APPLETALK
    /* Macintosh AppleTalk */
    { "0", at_attach, 0, 7,
    "attach 0 <protocol type> <device> arpa <label> <rx bufsize> <mtu> [ip_addr]" },
#endif

#ifdef NETROM
    /* fake netrom interface */
    { "netrom", nr_attach, 0, 1,
    "attach netrom [ip_addr]" },
#endif

#ifdef  PACKET
    /* FTP Software's packet driver spec */
    { "packet", pk_attach, 0, 4,
    "attach packet <int#> <label> <buffers> <mtu> [ip_addr]" },
#endif

#ifdef  HS
    /* Special high speed driver for DRSI PCPA or Eagle cards */
    { "hs", hs_attach, 0, 7,
    "attach hs <address> <vector> ax25 <label> <bufsize> <mtu>\
    <txdelay> <persistence> [ip_addra] [ip_addrb]" },
#endif

#ifdef  PI
    /* Special high speed driver for PI 8530 HDLC adapter */
    { "pi", pi_attach, 0, 8,
    "attach pi <address> <vector> <dmachannel> ax25 <label> <bufsize>\
    <mtu> <speed a> <speed b> [ip_addra] [ip_addrb]" },
#endif

#ifdef SCC
	{ "scc", scc_attach, 0, 5,
/*	"attach scc <board label> baycom|drsi|opto <addr> <vec> [t<tick chan>]\n"*/
	"attach scc <board label> <devices> init <addr> <spacing> <Aoff> <Boff> <Dataoff>"
	" <intack> <vec> [p]<clock> [[hdwe] [param] [t<tickchan>]]\n"
	"attach scc <board label> <chan> "
#ifdef SLIP
	  "slip"
#if defined(KISS) || (defined AX25) || defined(NRS)
	  "|"
#endif
#endif
#ifdef KISS
	  "kiss"
#if (defined AX25) || defined(NRS)
	  "|"
#endif
#endif
#ifdef AX25
	  "ax25"
#ifdef NRS
	  "|"
#endif
#endif
#ifdef NRS
	  "nrs"
#endif
	  " <label> <mtu> <speed> <bufsize> [[callsign] [s]]\n" },
#endif /* SCC */

#ifdef  PACKETWIN
    /* Gracilis PackeTwin */
    { "tsync", tsync_attach, 0, 11,
    "attach tsync <chan> <vec> <iobase> <hdx|fdx> <dma|ints> <rxdmachan> <name> <rxsize> <txsize> <speed> [txclk] [rxclk] [nrz|nrzi] [ipaddr] [#rxbufs]" },
#endif

#ifdef AXIP
	{ "axip", axip_attach, 0, 4,
	"attach axip <name> <mtu> <remote-hostid> [callsign]" },

	/* 29May2002 (12Mar2001) VE4KLM */
	{ "axudp", axip_attach, 0, 4,
	"attach axudp <name> <mtu> <remote-hostid> [callsign] [UDP src port] [UDP dest port]" },
#endif

#ifdef	TUN
	{ "tun", tun_attach, 0, 3,
	"attach tun <name> <mtu> <devid>" },
#endif

#ifdef BAYCOM_SER_FDX
	/* 15Nov08, Maiko (VE4KLM), New interface to linux kernel baycom module */
	{ "baycom", baycom_attach, 0, 4,
	"attach baycom <name> <mtu> <devname>" },
#endif	/* End of BAYCOM_SER_FDX */

#ifdef	MULTIPSK
	/* 05Feb09, Maiko (VE4KLM), New interface to MULTI-PSK tcp/ip control */
	{ "multipsk", mpsk_attach, 0, 2,
	"attach multipsk <name> <hostname> <port>" },
#endif	/* End of MULTIPSK */

#ifdef	AGWPE
	/* 23Feb2012, Maiko (VE4KLM), New interface to AGWPE tcp/ip control */
	{ "agwpe", agwpe_attach, 0, 2,
	"attach agwpe <name> <hostname> <port>" },
#endif	/* End of AGWPE */

#ifdef	WINRPR
	/* 02Nov2020, Maiko (VE4KLM), New interface to WINRPR tcp/ip control */
	{ "winrpr", winrpr_attach, 0, 2,
	"attach winrpr <name> <hostname> <port>" },
#endif	/* End of WINRPR */

#ifdef	EA5HVK_VARA
	/* 05Jan2021, Maiko (VE4KLM), New interface to VARA modem */
	{ "vara", vara_attach, 0, 2,
	"attach vara <name> <hostname> <port>" },
#endif	/* End of EA5HVK_VARA */

    { NULLCHAR,	NULL,	0,	0,
	NULLCHAR }
};

#ifndef UNIX

/* Functions to be called on each clock tick */
void (*Cfunc[]) __ARGS((void)) = {
    pctick, /* Call PC-specific stuff to keep time */
    kbint,  /* Necessary because there's no hardware keyboard interrupt */
    refiq,  /* Replenish interrupt pool */
#ifdef  ASY
    asytimer,
#endif
#ifdef  SCC
    scctimer,
#endif
#ifdef BPQ
    bpqtimer,
#endif
    NULL,
};

/* Entry points for garbage collection */
void (*Gcollect[]) __ARGS((int)) = {
    tcp_garbage,
    ip_garbage,
    udp_garbage,
    st_garbage,
#ifdef  AX25
    lapb_garbage,
#endif
#ifdef  NETROM
    nr_garbage,
#endif
    NULL
};

/* Functions to be called at shutdown */
void (*Shutdown[])() = {
#ifdef SCC
    sccstop,
#endif
    uchtimer,       /* Unlink timer handler from timer chain */
    NULLVFP((void)),
};

#endif /* UNIX */

#ifndef HEADLESS
static int
docls(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    clrscr();
    return 0;
}
#endif

/* Packet tracing stuff */
#ifdef  TRACE
#include "trace.h"

#define NULLMONFP NULLVFP((int,struct mbuf **, int))

/* Protocol tracing function pointers. Matches list of class definitions
 * in pktdrvr.h.
 */
struct trace Tracef[] = {
    { NULLFP((struct iface*,struct mbuf*)), ip_dump },        /* CL_NONE */

#ifdef  ETHER                           /* CL_ETHERNET */
    { ether_forus,    ether_dump },
#else
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },
#endif  /* ETHER */

    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },    /* CL_PRONET_10 */
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },    /* CL_IEEE8025 */
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },    /* CL_OMNINET */

#ifdef  APPLETALK
    { at_forus,       at_dump },        /* CL_APPLETALK */
#else
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },
#endif  /* APPLETALK */

#ifdef VJCOMPRESS
    { NULLFP((struct iface*,struct mbuf*)), sl_dump },        /* CL_SERIAL_LINE */
#else
    { NULLFP((struct iface*,struct mbuf*)), ip_dump },        /* CL_SERIAL_LINE */
#endif
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },   /* CL_STARLAN */

#ifdef  ARCNET
    { arc_forus,      arc_dump },       /* CL_ARCNET */
#else
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },
#endif  /* ARCNET */

#ifdef  AX25
    { ax_forus,       ax25_dump },      /* CL_AX25 */
#else
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },
#endif  /* AX25 */

#ifdef  AX25                           /* CL_KISS */
    { ki_forus,       ki_dump },
#else
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },
#endif  /* KISS */

    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },  /* CL_IEEE8023 */
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },  /* CL_FDDI */
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },  /* CL_INTERNET_X25 */
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },  /* CL_LANSTAR */
#ifdef  SLFP
    { NULLFP((struct iface*,struct mbuf*)), ip_dump },        /* CL_SLFP */
#else
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },
#endif  /* SLFP */

#ifdef  NETROM                          /* CL_NETROM */
    { NULLFP((struct iface*,struct mbuf*)), ip_dump },
#else
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP },
#endif

#ifdef PPP
    { NULLFP((struct iface*,struct mbuf*)), ppp_dump }       /* CL_PPP */
#else
    { NULLFP((struct iface*,struct mbuf*)), NULLMONFP }
#endif /* PPP */
};

#else

/* Stub for packet dump function */
void
dump(iface,direction,type,bp)
struct iface *iface;
int direction;
unsigned type;
struct mbuf *bp;
{
}
void
raw_dump(iface,direction,bp)
struct iface *iface;
int direction;
struct mbuf *bp;
{
}

#endif  /* TRACE */


#ifdef  AX25
#ifdef AUTOROUTE
/* G4JEC's automatic ARP & Route resolution code primitive. */
extern void ax25eavesdrop(struct iface * iface,
char *source, char * dest,
struct mbuf * bp);

extern int Ax25_autoroute;
#endif /* AUTOROUTE */

/* Hooks for passing incoming AX.25 data frames to network protocols */
void
axip(iface,axp,src,dest,bp,mcast)
struct iface *iface;
struct ax25_cb *axp;
char *src;
char *dest;
struct mbuf *bp;
int mcast;
{
#ifdef AUTOROUTE
    struct mbuf *nbp;

    if(Ax25_autoroute) {
        dup_p(&nbp, bp, 0, len_p(bp));
        (void) ax25eavesdrop(iface,src,dest,nbp);
        free_p(nbp);
    }
#endif
    (void)ip_route(iface,bp,mcast);
}

static void
axarp(iface,axp,src,dest,bp,mcast)
struct iface *iface;
struct ax25_cb *axp;
char *src;
char *dest;
struct mbuf *bp;
int mcast;
{
    (void)arp_input(iface,bp);
}

#ifdef RARP
static void
axrarp(iface,axp,src,dest,bp,mcast)
struct iface *iface;
struct ax25_cb *axp;
char *src;
char *dest;
struct mbuf *bp;
int mcast;
{
    (void)rarp_input(iface,bp);
}
#endif /* RARP */

#ifdef  NETROM
static void
axnr(iface,axp,src,dest,bp,mcast)
struct iface *iface;
struct ax25_cb *axp;
char *src;
char *dest;
struct mbuf *bp;
int mcast;
{
	/*
	 * 14Sep2005, Maiko (VE4KLM), In all the years the netrom code has
	 * been in use, NOBODY has ever watched out for cases where netrom
	 * code is compiled in, BUT 'attach netrom' was never run. IF any
	 * people ran their JNOS that way, AND they had netrom neighbours,
	 * they probably found their JNOS crashing on a regular basis, ie,
	 * each time a neighbour did a netrom broadcast or sent a netrom
	 * frame. Those people probably didn't even know that neighbours
	 * were sending them netrom frames or broadcasts. That explains
	 * reports of first time JNOS installs crashing on a constant
	 * basis. This should stabilize things. Thanks Barry :-)
	 */

	if (Nr_iface != NULLIF)	/* Is NET/ROM interface attached ? */
	{
    	if (!mcast)
		{
			/*
			 * 03Sep2011, Maiko, The code below was in newinp.c, but it
			 * is much easier to redirect to nr_nodercv() from here, then
			 * from within the inp_rif_recv() function. This will solve
			 * alot of problems for me if I put it in here instead.
			 *
			 * 04Sep2011, Maiko, Xnet could be a neighbour regardless of
			 * whether we are using INP or not, so do the NULL check every
			 * time now, even if INP2011 is not defined, just do it !
			 */
			if (axp == NULL)
			{
				if (Nr_debug)
					log (-1, "ax25_cb (NULL), treat it as multicast");

        		nr_nodercv (iface, src, bp);
			}
			else 
        		nr_route (bp,axp);
		}
    	else
        	nr_nodercv (iface,src,bp);
	}
	else free_p (bp);
}

#endif  /* NETROM */
#endif  /* AX25 */

#ifndef RIP
/* Stub for routing timeout when RIP is not configured -- just remove entry */
void
rt_timeout(s)
void *s;
{
    struct route *stale = (struct route *)s;

#if defined(ENCAP) && (defined(UDP_DYNIPROUTE) || defined(MBOX_DYNIPROUTE))
    if(stale->iface == &Encap)
        /* Drop this route by adding it to the Loopback (bit bucket)
         * interface.  This will keep a 32-bit IP address or smaller
         * block isolated from being part of a larger block that is
         * already routed in the machine which would prevent a temporary
         * encap route from being added in the future. - K2MF */
        rt_add(stale->target,stale->bits,0L,&Loopback,1L,0L,1);
    else
#endif
    rt_drop(stale->target,stale->bits);
}
#endif

#ifdef  SERVERS
#define SSTACK 160      /* servers stack size */
/* "start" and "stop" subcommands */
static struct cmds DFAR Startcmds[] = {

/*
 * 03Apr2004, Maiko, now have only one aprs start command. This will make
 * future upgrades alot easier, just pass the name of the service to start,
 * as opposed to having to create a new start/stop cmd sequence for each new
 * service we want to add to NOS.
 */
#ifdef  APRSD
	{ "aprs",			aprsstart,		0,	2, "start aprs <service>" },
#endif

#ifdef AX25SERVER
    { "ax25",         ax25start,      SSTACK, 0, NULLCHAR },
#endif
#if defined(CALLSERVER) || defined(SAMCALLB) || defined(QRZCALLB) || defined(BUCKTSR)
    { "callbook",     cdbstart,       SSTACK, 0, NULLCHAR },
#endif
#ifdef  CONVERS
    { "convers",      conv1,          0, 0, NULLCHAR },
#endif
#ifdef DISCARDSERVER
    { "discard",      dis1,           0, 0, NULLCHAR },
#endif
#ifdef ECHOSERVER
    { "echo",         echo1,          0, 0, NULLCHAR },
#endif
#ifdef FINGERSERVER
    { "finger",       finstart,       0, 0, NULLCHAR },
#endif
#ifdef FTPSERVER
    { "ftp",          ftpstart,       0, 0, NULLCHAR },
#endif
#if defined(MBFWD) && defined(MAILBOX)
    { "forward",      fwdstart,       2048,0, NULLCHAR },
#endif
#ifdef TCPGATE
    { "gate",         gatestart,      0, 3, "start gate <port> <desthost> [<destport>]" },
#endif
#ifdef HTTP
    { "http",         httpstart,      0, 0, NULLCHAR },
#endif
#ifdef HTTPVNC
    { "httpvnc",       httpvnc1,      0, 0, NULLCHAR },
#endif
#ifdef IDENTSERVER
    { "ident",        identstart,     0, 0, NULLCHAR },
#endif
#ifdef INP2011
	/* 19Aug2011, Maiko, INP again */
    { "inp",       doINPtimer,       0, 0, NULLCHAR },
#endif
#ifdef NETROMSERVER
    { "netrom",       nr4start,       SSTACK, 0, NULLCHAR },
#endif
#ifdef  NNTPS
    { "nntp",         nntp1,          0, 0, NULLCHAR },
#endif
#ifdef  POP2SERVER
    { "pop2",         pop2start,      0, 0, NULLCHAR },
#endif
#ifdef  POP3SERVER
    { "pop3",         pop3start,      0, 0, NULLCHAR },
#endif
#ifdef  RIP
    { "rip",          doripinit,      0,   0, NULLCHAR },
#endif
#ifdef RSYSOPSERVER
    { "rsysop",       rsysop1,        SSTACK, 0, NULLCHAR },
#endif
#ifdef SMTPSERVER
    { "smtp",         smtp1,          0, 0, NULLCHAR },
#endif
#ifdef TELNETSERVER
    { "telnet",       telnet1,        0, 0, NULLCHAR },
#endif
#ifdef TERMSERVER
    { "term",         term1,          0, 0, NULLCHAR },
#endif
#ifdef TIPSERVER
    { "tip",          tipstart,       384, 2, "start tip <interface>" },
#endif
#ifdef  RDATESERVER
    { "time",         time1,          0, 0, NULLCHAR },
#endif
#ifdef  TRACESERVER
    { "trace",        trace1,         0, 0, NULLCHAR },
#endif
#ifdef TTYLINKSERVER
    { "ttylink",      ttylstart,      0, 0, NULLCHAR },
#endif
#ifdef REMOTESERVER
    { "remote",       rem1,           768, 0, NULLCHAR },
#endif
#ifdef TNCALL
    { "tnlink",      tnlstart,      0, 0, NULLCHAR },
#endif
#ifdef SNMPD
    { "snmpd",       snmpd1,           1024, 0, NULLCHAR },
#endif
    { NULLCHAR,		NULL,			0,	0,	NULLCHAR }
};

static struct cmds DFAR Stopcmds[] = {

/*
 * 03Apr2004, Maiko, now have only one aprs start command. This will make
 * future upgrades alot easier, just pass the name of the service to start,
 * as opposed to having to create a new start/stop cmd sequence for each new
 * service we want to add to NOS.
 */
#ifdef  APRSD
	{ "aprs",		aprsstop,			0,	2, "stop aprs <service>" },
#endif

#ifdef AX25SERVER
    { "ax25",         ax250,          0, 0, NULLCHAR },
#endif
#if defined CALLSERVER || defined SAMCALLB || defined QRZCALLB || defined BUCKTSR
    { "callbook",     cdb0,           0, 0, NULLCHAR },
#endif
#ifdef  CONVERS
    { "convers",  conv0,      0, 0, NULLCHAR },
#endif
#ifdef DISCARDSERVER
    { "discard",  dis0,       0, 0, NULLCHAR },
#endif
#ifdef ECHOSERVER
    { "echo",         echo0,          0, 0, NULLCHAR },
#endif
#ifdef FINGERSERVER
    { "finger",   fin0,       0, 0, NULLCHAR },
#endif
#ifdef FTPSERVER
    { "ftp",          ftp0,           0, 0, NULLCHAR },
#endif
#if defined(MBFWD) && defined(MAILBOX)
    { "forward",      fwd0,           0, 0, NULLCHAR },
#endif
#ifdef TCPGATE
    { "gate",         gate0,          0, 2, "stop gate <port>" },
#endif
#ifdef HTTP
    { "http",         http0,          0, 0, NULLCHAR },
#endif
#ifdef HTTPVNC
    { "httpvnc",       httpvnc0,      0, 0, NULLCHAR },
#endif
#ifdef IDENTSERVER
    { "ident",        identstop,      0, 0, NULLCHAR },
#endif
#ifdef NETROMSERVER
    { "netrom",       nr40,           0, 0, NULLCHAR },
#endif
#ifdef  NNTPS
    { "nntp",         nntp0,          0, 0, NULLCHAR },
#endif
#ifdef  POP2SERVER
    { "pop2",         pop2stop,       0, 0, NULLCHAR },
#endif
#ifdef  POP3SERVER
    { "pop3",         pop3stop,       0, 0, NULLCHAR },
#endif
#ifdef  RIP
    { "rip",          doripstop,      0, 0, NULLCHAR },
#endif
#ifdef RSYSOPSERVER
    { "rsysop",       rsysop0,        0, 0, NULLCHAR },
#endif
#ifdef SMTPSERVER
    { "smtp",         smtp0,          0, 0, NULLCHAR },
#endif
#ifdef TELNETSERVER
    { "telnet",       telnet0,        0, 0, NULLCHAR },
#endif
#ifdef TERMSERVER
    { "term",         term0,          0, 0, NULLCHAR },
#endif
#ifdef TIPSERVER
    { "tip",      tip0,       0, 2, "stop tip <interface>" },
#endif
#ifdef  RDATESERVER
    { "time",     time0,      0, 0, NULLCHAR },
#endif
#ifdef  TRACESERVER
    { "trace",    trace0,     0, 0, NULLCHAR },
#endif
#ifdef TTYLINKSERVER
    { "ttylink",  ttyl0,      0, 0, NULLCHAR },
#endif
#ifdef REMOTESERVER
    { "remote",   rem0,       0, 0, NULLCHAR },
#endif
    { NULLCHAR,	NULL,		0,	0,	NULLCHAR }
};

/* TCP port numbers to be considered "interactive" by the IP routing
 * code and given priority in queueing
 */
int Tcp_interact[] = {
    IPPORT_FTP, /* FTP control (not data!) */
    IPPORT_TELNET,  /* Telnet */
    IPPORT_CONVERS, /* Convers */
    IPPORT_XCONVERS,    /* LZW Convers */
    IPPORT_TTYLINK,     /* Tty link */
    IPPORT_RLOGIN,      /* BSD rlogin */
    IPPORT_MTP,         /* Secondary telnet */
    -1
};

static int
dostart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int ret,insave,outsave;

    insave = Curproc->input;
    outsave = Curproc->output;
    Curproc->input = Cmdpp->input;
    Curproc->output = Cmdpp->output;

    ret = subcmd(Startcmds,argc,argv,p);

    Curproc->input = insave;
    Curproc->output = outsave;
    return ret;
}

static int
dostop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Stopcmds,argc,argv,p);
}
#endif  /* SERVERS */

/* Various configuration-dependent functions */

/* put mbuf into Hopper for network task
 * returns 0 if OK
 */
int
net_route(ifp, type, bp)
struct iface *ifp;
int type;
struct mbuf *bp;
{
    struct mbuf *nbp;
    struct phdr phdr;

    phdr.iface = ifp;
    phdr.type = type;

    nbp = pushdown(bp,sizeof(phdr));

    memcpy( &nbp->data[0],(char *)&phdr,sizeof(phdr));
    enqueue(&Hopper,nbp);
    /* Especially on slow machines, serial I/O can be quite
     * compute intensive, so release the machine before we
     * do the next packet.  This will allow this packet to
     * go on toward its ultimate destination. [Karn]
     */
    pwait(NULL);
    return 0;
}

/* Process packets in the Hopper */
static void
network(i,v1,v2)
int i;
void *v1;
void *v2;
{
    struct mbuf *bp;
    struct phdr phdr;
    int i_state;
#ifdef RXECHO
    struct mbuf *bbp;
#if((defined KISS) || (defined AX25))
#include "devparam.h"
    struct iface *kissif;
    unsigned char kisstype;
#endif /* KISS/AX25 */
#endif

	while (1)	/* replaces loop lable */
	{
#ifndef UNIX
    refiq();        /* Replenish interrupt buffer pool */
#endif

    i_state = dirps();
    while(Hopper == NULLBUF)
        pwait(&Hopper);

    /* this is silly, first we turn ints on, then off again...? -WG7J */
    /* Process the input packet */
    /*
    bp = dequeue(&Hopper);
     */

    bp = Hopper;
    Hopper = Hopper->anext;
    bp->anext = NULLBUF;

    restore(i_state);

    pullup(&bp,(char *)&phdr,sizeof(phdr));

	if (phdr.iface != NULLIF)
	{
#ifdef J2_SNMPD_VARS
		phdr.iface->rawrecbytes += len_p (bp);
#endif
		phdr.iface->rawrecvcnt++;
		phdr.iface->lastrecv = secclock();
    }
#ifdef TRACE
    dump(phdr.iface,IF_TRACE_IN,(unsigned)phdr.type,bp);
#endif
#ifdef RXECHO    
    /* If configured, echo this packet before handling - WG7J */
#if((defined KISS) || (defined AX25))
    if(phdr.type == CL_KISS) { /* VK1ZAO: kiss interfaces are multidrop */
        dup_p(&bbp,bp,0,len_p(bp));  /* dup it */
        kisstype = PULLCHAR(&bbp);    /* get port/type byte */
        kissif = Slip[phdr.iface->xdev].kiss[kisstype>>4];
        if(kissif && kissif->rxecho && (kisstype & 0x0f)==PARAM_DATA)
            (*kissif->rxecho->raw)(kissif->rxecho, bbp);  /* send if ifc OK and a data frame */
        else free_p(bbp);
    }
    else
#endif /* #if((defined KISS) || (defined AX25)) */
    if(phdr.iface && phdr.iface->rxecho) {
        dup_p(&bbp,bp,0,len_p(bp));
        (*phdr.iface->rxecho->raw)(phdr.iface->rxecho, bbp);
    }
#endif
    switch(phdr.type){
#if((defined KISS) || (defined AX25))
        case CL_KISS:
            kiss_recv(phdr.iface,bp);
            break;
#endif
#ifdef  AX25
        case CL_AX25:
            ax_recv(phdr.iface,bp);
            break;
#endif
#ifdef  ETHER
        case CL_ETHERNET:
            eproc(phdr.iface,bp);
            break;
#endif
#ifdef ARCNET
        case CL_ARCNET:
            aproc(phdr.iface,bp);
            break;
#endif
#ifdef PPP
        case CL_PPP:
            ppp_proc(phdr.iface,bp);
            break;
#endif
    /* These types have no link layer protocol at the point when they're
     * put in the hopper, so they can be handed directly to IP. The
     * separate types are just for user convenience when running the
     * "iface" command.
     */
        case CL_NONE:
        case CL_SERIAL_LINE:
        case CL_SLFP:
            ip_route(phdr.iface,bp,0);
            break;
        default:
            free_p(bp);
            break;
    }
    /* Let everything else run - this keeps the system from wedging
     * when we're hit by a big burst of packets
     */
    pwait(NULL);

	}	/* replaces GOTO label */
}

/*
 * 03Sep2005, Maiko (VE4KLM), New functions created to remove certain
 * functions from main.c, because the particular function's inclusion
 * are dependent on config.h (which is not included in main.c) ! Up
 * till now, I would kludge by putting #define AXIP or something in
 * the main.c module, which was just BAD on my part. Now solved.
 */

#ifdef	AXIP
extern void init_max_ax25 (int);	/* in ax25.c module */
static int cfg_numaxip = 0;
#endif

#ifdef	DYNGWROUTES
extern void rtdyngw_init (int);		/* in rtdyngw.c module */
static int cfg_numdyngw = 0;
#endif

void jnos2_args (int arg, char *optarg)
{
	switch (arg)
	{
#ifdef	AXIP
		/* max number of axip AND axudp interfaces */
		case 'a':
			cfg_numaxip = atoi (optarg);
			break;
#endif
#ifdef	DYNGWROUTES
		/* max number of routes with dynamic gateways */
		case 'R':
			cfg_numdyngw = atoi (optarg);
			break;
#endif
	}
}

void jnos2_inits (void)
{
#ifdef	AXIP
	/* 20Oct2004, Maiko, AXIP list is now dynamically allocated */
	init_max_ax25 (cfg_numaxip);
#endif

#ifdef	DYNGWROUTES
	/* 02Nov2004, Maiko, Routes dynamic gateways list dynamically allocated */
	rtdyngw_init (cfg_numdyngw);
#endif
}

