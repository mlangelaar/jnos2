/*
 * Version control information for "info" command display
 *
 * Cleanup of information display - K2MF 4/93
 *
 * 19Nov2010, Maiko, Updated to reflect newer features from JNOS 2.0, etc
 *
 * 20Mar2012, Maiko (VE4KLM), I need to define better version control !!!
 * 
 * As of now, the version of JNOS 2.0 will be defined as follows :
 *
 *   JNOS 2.0X.n
 *
 * where X is a smaller letter, and n is a sequence number. For a new
 * official release (read below), there is NO sequence number present.
 *
 * The small letter will be bumped up each time I decide to put out a new
 * tar file (a new official release) for those who do not (or can not) use
 * the RSYNC protocol to get the official source code off my system.
 *
 * Any changes (no matter how small) made to an official release must have
 * a sequence number included in the version. Further more, this number must
 * be incremented each time a change is made to any of the JNOS 2.0 source,
 * even the makefile or other support files.
 * 
 * $Id: version.c,v 1.9 2012/04/30 01:34:56 ve4klm Exp ve4klm $
 *
 */

#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "commands.h"   /* prototype for doinfo() */
#if (defined(AX25) && defined(AXIP))
#include "ax25.h"	/* bring in NAX25 definition */
#endif

#define JNOS_VERSION "2.0m.5Gz"

char shortversion[] = "JNOS" JNOS_VERSION ;

#if (defined(MAILBOX) || defined(MBFWD))
#if defined(FBBCMP)

#ifdef B2F
/* 25Mar2008, Maiko (VE4KLM), For systems (WinLink/AirMail) that use B2F */
char MboxIdB2F[]  = "[JNOS-" JNOS_VERSION "-B2FHIM$]\n";
#endif

char MboxIdFC[]  = "[JNOS-" JNOS_VERSION "-BFHIM$]\n";

/* 28Aug2013, Maiko (VE4KLM), We need to get B1F working, period ! */
char MboxIdB1F[]  = "[JNOS-" JNOS_VERSION "-B1FHIM$]\n";

#endif
#if defined(FBBFWD)
char MboxIdF[]  = "[JNOS-" JNOS_VERSION "-FHIM$]\n";
#endif /* FBBFWD */
char MboxId[]   = "[JNOS-" JNOS_VERSION "-IHM$]\n";
#endif /* MAILBOX || MBFWD */

char Version2[] =
"by Maiko Langelaar (VE4KLM) - based on JNOS 1.11f, by James P. Dugal (N5KNX),\n"
"Johan. K. Reinalda (WG7J/PA3DIS), Brandon S. Allbery (KF8NH), and others.\n"
"";

extern int Numrows,Numcols;

char Version[] = JNOS_VERSION " ("
#if defined UNIX
#if defined(linux)
    "Linux"
#elif defined(sun)
    "Solaris"
#if defined(__sparc)
    " SPARC"
#endif
#else
    "Unix"
#endif
#elif defined CPU86
    "8088"
#elif defined CPU186
    "80186"
#elif defined CPU286
    "80286"
#elif defined CPU386
    "80386"
#elif defined CPU486
    "80486"
#elif defined CPU586
    "PENTIUM"
#else
    "cpu unknown"
#endif
    ")";

#ifdef ALLCMD
static char *
compilerver(void)
{
	static char result[24];

#if defined(__GNUC__)
#if defined(__GNUC_MINOR__)
	sprintf(result, "GCC %d.%d", __GNUC__, __GNUC_MINOR__);
#else
	sprintf(result, "GCC %d", __GNUC__);
#endif
#elif defined(__BORLANDC__)
	sprintf(result, "BC %04X", __BORLANDC__);
#elif defined(__TURBOC__)
	sprintf(result, "TC %04X", __TURBOC__);
#else
	strcpy(result,"unknown compiler");
#endif
	return(result);
}

int
doinfo(argc,argv,p)
int argc;
char *argv[];
void *p;
{
#ifndef HEADLESS
#if defined(UNIX) && defined(SM_CURSES)
    extern char curseslibver[];
#endif
#endif

    tprintf("JNOS %s, compiled %s %s by %s",
             Version,__DATE__,__TIME__,compilerver());
#ifdef WHOFOR
    tprintf("\nfor %s,", WHOFOR);
#endif
    j2tputs(" containing:\n\n");

#ifndef SERVERS
    j2tputs("TCP Servers:  None !\n");
#else

    j2tputs("TCP Servers:"
#ifdef SMTPSERVER
    " SMTP"
#ifdef SMTP_VALIDATE_LOCAL_USERS
    "(validated)"
#endif
#ifdef SMTP_DENY_RELAY
    "(filtered)"
#endif
#ifdef SDR_EXCEPTIONS
    "(deny relay exception list)"
#endif
#ifdef TRANSLATEFROM
    "(translated)"
#endif
#ifdef SMTP_REFILE
    "(refiled)"
#endif
#endif /* SMTPSERVER */

#ifdef FINGERSERVER
    " FINGER"
#endif
#ifdef FTPSERVER
    " FTP"
#endif
#ifdef TCPGATE
    " TCPGATE"
#endif
    "\n");

#if (defined(TELNETSERVER) || defined(RSYSOPSERVER) || defined(TRACESERVER) || defined(TTYLINKSERVER) || defined(DISCARDSERVER) || defined(ECHOSERVER) )
    j2tputs("            "

#ifdef TELNETSERVER
    " TELNET"
#endif
#ifdef RSYSOPSERVER
    " RSYSOP"
#endif
#ifdef TRACESERVER
    " TRACE"
#endif
#ifdef TTYLINKSERVER
    " TTYLINK"
#endif
#ifdef DISCARDSERVER
    " DISCARD"
#endif
#ifdef ECHOSERVER
    " ECHO"
#endif
    "\n");
#endif /* TELNETSERVER || RSYSOPSERVER || TRACESERVER || TTYLINKSERVER || DISCARDSERVER || ECHOSERVER */

#if (defined(CALLSERVER) || defined(BUCKTSR) || defined(SAMCALLB) || defined(QRZCALLB) || defined(CONVERS) || defined(NNTPS) || defined(POP2SERVER) || defined(POP3SERVER) || defined(RDATESERVER) || defined(IDENTSERVER) || defined(TERMSERVER) || defined(HTTP))
    j2tputs("            "

#ifdef CALLSERVER
    " CALLBOOK (CD-ROM)"
#endif

#ifdef BUCKTSR
    " CALLBOOK (BUCKTSR)"
#endif /* BUCKTSR */

#ifdef SAMCALLB
    "  CALLBOOK (SAM)"
#endif /* SAMCALLB */

#ifdef QRZCALLB
    " CALLBOOK (QRZ)"
#endif /* QRZCALLB */

#ifdef CONVERS
    " CONVERS"
#endif

#ifdef NNTPS
    " NNTP"
#endif

#ifdef POP2SERVER
    " POP2"
#endif

#ifdef POP3SERVER
    " POP3"
#endif

#ifdef HTTP
    " HTTP"
#endif

#ifdef RDATESERVER
    " TIME"
#endif

#ifdef TERMSERVER
    " TERM"
#endif

#ifdef IDENTSERVER
    " IDENT"
#endif

    "\n");
#endif /* CALLSERVER || CONVERS || NNTPS || POP2SERVER || POP3SERVER || RDATESERVER || TERMSERVER etc */

#endif /* SERVERS */

    j2tputs("TCP Clients: SMTP"

#ifdef FINGERSESSION
    " FINGER"
#endif

#ifdef FTPSESSION
    " FTP"
#endif

#ifdef TELNETSESSION
    " TELNET"
#endif

#ifdef TTYLINKSESSION
    " TTYLINK"
#endif

    "\n");

#if (defined(CALLCLI) || defined(CONVERS) || defined(NNTP) || defined(NNTPS) || defined(POP2CLIENT) || defined(POP3CLIENT) || defined(RLOGINCLI) || defined(RDATECLI) || defined(LOOKSESSION))
    j2tputs ("            "

#ifdef CALLCLI
    " CALLBOOK (CD-ROM)"
#endif

#ifdef CONVERS
    " CONVERS"
#endif

#if (defined(NNTP) || defined(NNTPS))
    " NNTP"
#ifdef NN_USESTAT
    "(stat)"
#endif
#if defined(NNTPS) && defined(NEWS_TO_MAIL)
    "(2mail)"
#endif
#endif

#ifdef POP2CLIENT
    " POP2"
#endif

#ifdef POP3CLIENT
    " POP3"
#endif

#ifdef RLOGINCLI
    " RLOGIN"
#endif

#ifdef RDATECLI
    " TIME"
#endif

#ifdef LOOKSESSION
    " LOOK"
#endif

    "\n");
#endif /* CALLCLI || CONVERS || NNTP || NNTPS || POP2CLIENT || POP3CLIENT || RLOGINCLI || RDATECLI || LOOKSESSION */

#ifdef LZW
    j2tputs("    with LZW compression for TCP sockets\n");
#endif /* LZW */

#if (defined(TCPACCESS) || defined(IPACCESS))
    j2tputs("    with "

#ifdef TCPACCESS
    "TCP"
#endif

#if (defined(TCPACCESS) && defined(IPACCESS))
    "/"
#endif

#ifdef IPACCESS
    "IP"
#endif

    " access controls\n");
#endif /* TCPACCESS || IPACCESS */


#if (defined(DOMAINSERVER) || defined(REMOTESERVER) || defined(BOOTPSERVER))
    j2tputs("UDP Servers:"

#ifdef DOMAINSERVER
    " DOMAIN-NAMESERVER"
#endif /* DOMAINSERVER */

#ifdef REMOTESERVER
    " REMOTE"
#if defined(UDP_DYNIPROUTE) && defined(ENCAP)
    "(with DYNIP)"
#endif
#endif /* REMOTESERVER */

#ifdef BOOTPSERVER
    " BOOTP"
#endif

    "\n");
#endif /* DOMAINSERVER || REMOTESERVER || BOOTPSERVER */

#if (defined(REMOTECLI) || defined(BOOTPCLIENT))
    j2tputs("UDP Clients:"

#ifdef REMOTECLI
    " REMOTE"
#endif

#ifdef BOOTPCLIENT
    " BOOTP"
#endif

    "\n");
#endif /* UDP Clients */

#ifdef MAILBOX

#ifdef TIPSERVER
	j2tputs("TIP ");
#endif

    j2tputs("Mailbox Server"

#if defined(MBOX_DYNIPROUTE) && defined(ENCAP) && defined(TELNETSERVER)
    "(with DYNIP)"
#endif
#ifdef XMODEM
    " with Xmodem file transfer"
#endif /* XMODEM */

    "\n");

#ifdef MAILCMDS
    j2tputs("Full Service BBS"

#if (defined(EXPIRY) || defined(MAILFOR) || defined(MBFWD) || defined(RLINE)) || defined(B2F) || defined(WPAGES)
    " with:"
#endif

#ifdef EXPIRY
    "\n     Message and BID expiry"
#endif

#ifdef MAILFOR
    "\n     'Mail For' beaconing"
#endif

#ifdef MBFWD
    "\n     AX.25 mail forwarding"
#ifdef FBBFWD
    "\n           with FBB Style"
#ifdef FBBCMP
    " Compressed"
#endif
    " Forwarding"
#endif /* FBBFWD */
#endif /* MBFWD */

#ifdef B2F
    "\n           with B2F Style Forwarding for Winlink (RMS) Systems"
#endif

#ifdef RLINE
    "\n     BBS 'R:-line' compatibility"
#endif /* RLINE */

#ifdef WPAGES
    "\n     BBS 'Pseudo-White Pages'   "
#endif /* RLINE */

    "\n");
#endif /* MAILCMDS */

#endif /* MAILBOX */

#if (defined(AXIP) || defined(ENCAP))
    j2tputs("Internet Services:"

#ifdef AXIP
    "  AX.25 Digipeating");
    tprintf(" (%d ports)", NAX25);
    j2tputs(
#endif /* AXIP */

#ifdef ENCAP
    "  IP Encapsulation"
#endif /* ENCAP */

    "\n");
#endif /* AXIP || ENCAP */

#ifdef HOPCHECKSESSION
	j2tputs("Hopcheck IP path tracing\n");
#endif /* HOPCHECKSESSION */

#ifdef RIP
    j2tputs("RIP-2"
#ifdef RIP98
          "/-98"
#endif
                 " Routing Protocol\n");
#endif /* RIP */

#ifdef RSPF
	j2tputs("Radio Shortest Path First Protocol (RSPF)\n");
#endif /* RSPF */

#ifdef RARP
	j2tputs("Reverse Address Resolution Protocol (RARP)\n");
#endif /* RARP */

#ifdef MD5AUTHENTICATE
        j2tputs("MD5 Authentication\n");
#endif

#ifdef MSDOS
    tprintf("%d interrupt buffers of %d bytes\n",Nibufs,Ibufsize);
#endif

#ifdef ASY
#ifdef UNIX
    j2tputs("Generic termios interface driver\n");
#else
    j2tputs("Generic async (8250/16450/16550) interface driver\n");
#endif

#if (defined(KISS) || defined(AX25) || defined(NRS))
    j2tputs("Async interface drivers:"

#ifdef KISS
    "  KISS-TNC"
#endif /* KISS */

#ifdef POLLEDKISS
    "  POLLED-KISS"
#endif /* POLLEDKISS */

#ifdef AX25
    "  AX.25"
#endif /* AX25 */

#ifdef NRS
    "  NET/ROM-TNC"
#endif /* NRS */

    "\n");
#endif /* KISS || AX25 || NRS */

#endif /* ASY */

#ifdef BPQ
    j2tputs("Bpq Host driver\n");
#endif

#ifdef NETROM
    j2tputs("NET/ROM network interface");
#ifdef NRR
    j2tputs (" with (NRR) netrom route record support");
#endif
    j2tputs ("\n");
#endif /* NETROM */

#if (defined(PPP) || defined(SLIP))
    j2tputs("Async IP drivers:"

#ifdef PPP
    "  Point-to-Point (PPP)"
#endif /* PPP */

#ifdef SLIP
    "  Serial Line (SLIP)"
#endif /* SLIP */

    "\n");

#ifdef DIALER
    j2tputs("      with modem dialer for SLIP/PPP\n");
#endif /* DIALER */

#ifdef VJCOMPRESS
    j2tputs("      with Van Jacobson compression for PPP/SLIP\n");
#endif /* VJCOMPRESS */

#endif /* PPP || SLIP */

#ifdef PACKET
	j2tputs("FTP Software's PACKET driver interface\n");
#endif /* PACKET */

#ifdef APPLETALK
	j2tputs("Appletalk interface for MacIntosh\n");
#endif /* APPLETALK */

#ifdef ARCNET
	j2tputs("ARCnet via PACKET driver\n");
#endif /* ARCNET */

#ifdef DRSI
	j2tputs("DRSI PCPA low-speed driver\n");
#endif /* DRSI */

#ifdef EAGLE
    j2tputs("Eagle card 8530 driver\n");
#endif /* EAGLE */

#ifdef ETHER
    j2tputs("Generic ethernet driver\n");
#endif /* ETHER */

#ifdef HAPN
	j2tputs("Hamilton Area Packet Network driver\n");
#endif /* HAPN */

#ifdef HS
	j2tputs("High speed (56 kbps) modem driver\n");
#endif /* HS */

#ifdef PACKETWIN
	j2tputs("Gracilis PackeTwin driver\n");
#endif /* PACKETWIN */

#ifdef PC_EC
	j2tputs("3-Com 3C501 Ethernet controller driver\n");
#endif /* PC_EC */

#ifdef PC100
	j2tputs("PAC-COM PC-100 driver\n");
#endif /* PC100 */

#ifdef PI
	j2tputs("PI SCC card with DMA driver (VE3IFB)\n");
#endif /* PI */

#ifdef SCC
    j2tputs("Generic SCC (8530) driver (PE1CHL)\n");
#endif /* SCC */

#ifdef SLFP
	j2tputs("SLFP via PACKET driver\n");
#endif /* SLFP */
    
#ifdef TRACE
	j2tputs("Hardware interface packet tracing"
#ifdef MONITOR
	" (minimal monitor-style trace available)"
#endif
	"\n");
#endif /* TRACE */


#ifdef PRINTEROK
        j2tputs("Parallel printer\n");
#endif

#ifdef MULTITASK
/*      j2tputs("The Russell Nelson modsets\n"); */
	j2tputs("Multitasking capability when shelling out to MS-DOS\n");
#endif /* MULTITASK */

#ifndef HEADLESS
#if defined(UNIX) && defined(SM_CURSES)
    tprintf("Linked with (n)curses version %s\n", curseslibver);
#endif
#endif

#ifdef AXUISESSION
    j2tputs("AX.25 UI packet tx/rx\n");
#endif

#ifdef EDITOR
    j2tputs(
#ifdef ED
          "Ed"
#endif
#ifdef TED
	  "Ted"
#endif
          " ASCII text editor\n");
#endif

#if (defined(STKTRACE) || defined(SWATCH) || defined(MEMLOG) || defined(SHOWFH) || defined(PS_TRACEBACK)) && defined(MSDOS) || defined(CHKSTK)
    j2tputs("Debug features:"
#ifdef MSDOS
#ifdef STKTRACE
    " STKTRACE"
#endif
#ifdef SWATCH
    " SWATCH"
#endif
#ifdef MEMLOG
    " MEMLOG"
#endif
#ifdef SHOWFH
    " SHOWFH"
#endif
#ifdef PS_TRACEBACK
    " TRACEBACK"
#endif
#endif /* MSDOS */

#ifdef CHKSTK
    " CHKSTK"
#endif
    "\n");
#endif

/*
 * 19Nov2010, Maiko (VE4KLM), Update this file to reflect features I have
 * added to JNOS 2.0 over the last few years. I never really did update this
 * file in all these past years, so let's try to do better from now on. Some
 * stuff has been incorporated into the older code earlier on in this file.
 */

#ifdef J2_TRACESYNC
	j2tputs ("trace file sync feature\n");
#endif
#ifdef HEADLESS
	j2tputs ("No (n)curses, No console, No keyboard - headless\n");
#endif
#ifdef APRSD
#ifndef APRSC
	j2tputs ("APRS Services and Igate\n");
#else
	j2tputs ("APRS Client Only\n");
#endif
#endif
#ifdef RIPAMPRGW
    j2tputs ("AMPRnet RIP Updates (Early 2010)\n");
#endif

#if defined(TUN) || defined(BAYCOM_SER_FDX)
	j2tputs ("Linux kernel (module) interfaces :"
#ifdef BAYCOM_SER_FDX
	" baycom"
#endif
#ifdef TUN
	" tun"
#endif
	"\n");
#endif

#if defined(MULTIPSK) || defined(WINMOR)
	j2tputs ("Soundcard tcp/ip clients :"
#ifdef MULTIPSK
	" MultiPSK"
#endif
#ifdef WINMOR
	" Winmor"
#endif
	"\n");
#endif

#ifdef HFDD
	j2tputs ("HF Digital Devices\n");
#endif
#ifdef DYNGWROUTES
	j2tputs ("Dynamic (dyndns) Gateways in Route Add\n");
#endif
#ifdef MBX_CALLCHECK
	j2tputs ("Callsign validation\n");
#endif
#ifdef HTTPVNC
	j2tputs ("Browser Based User Mailbox\n");
#endif
#ifdef NOINVSSID_CONN
	j2tputs ("Connisc command - connect without ssid inversion\n");
#endif
#ifdef JNOS20_SOCKADDR_AX
	j2tputs ("interface SOCKADDR_AX redefinition\n");
#endif
    return 0;
}

#endif /* ALLCMD */

