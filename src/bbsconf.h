#ifndef _CONFIG_H
#define _CONFIG_H
/* This is the configuration for the WA7TAS.OR.USA.NOAM bbs */
/* #define WHOFOR "description_goes_here" */
  
/* NOTE: only the below listed config files have been tested.
 * Due to the virtually unlimited number of combinations of options
 * in config.h, it is impossible to test each possible variation!
 * Others may or may not compile and link without errors !
 * Effort has been made to provide a clean set of #defines throughout
 * the source to produce a good compile, but no guarantees are made.
 * Your mileage may vary!!!
 * tested are: distconf.h, gwconfig.h, bbsconf.h, users.h homeslip.h
 */
  
  
/* Software options */
  
#undef CONVERS		1 /* Conference bridge (babble-box :-) */
/* Now some converse options ... see convers.c for more comments */
#undef LINK		1 /* permit this convers node to be linked with others*/
#undef XCONVERS		1 /* LZW Compressed convers server and links */
#undef CNV_VERBOSE	1 /* Verbose msgs */
#undef CNV_CHAN_NAMES	1 /* Convers named channels */
#undef CNV_TOPICS	1 /* Convers channel topics are gathered */
#undef CNV_CALLCHECK	1 /* Convers only allows callsigns */
#undef CNV_LOCAL_CHANS	1 /* Convers local channels and msg-only channels */
#undef CNV_ENHANCED_VIA	1 /* If convers user is local, "via" gives more info */
#undef CNV_CHG_PERSONAL	1 /* Allow users to change personal data permanently */
#undef CNV_LINKCHG_MSG	1 /* Send link-change messages in convers */
#undef CNV_VERBOSE_CHGS	1 /* Default to /VERBOSE yes. Use this judiciously! */
#undef CNV_TIMESTAMP_MSG 1 /* Add hh:mm prefix to msgs sent to local users */

/* Use only ONE of the 2 news options: */
#undef NNTP		1 /* Netnews client */
#undef NNTPS		1 /* Netnews client and server */
#define NEWS_TO_MAIL	1 /* NNTPS emails per gateway file */
#define NN_USESTAT		1 /* Try GROUP/STAT cmds if NEWNEWS fails */
#undef NN_INN_COMPAT	1 /* send "mode reader" cmd after connecting to server */
#define NN_REMOVE_R_LINES	1 /* remove R: lines from incoming email */
#define NNTP_TIMEOUT	900 /* idle-timeout #secs for nntp client (both versions)*/
#define NNTPS_TIMEOUT	3600 /* idle-timeout #secs for nntp server */

#undef STKTRACE		1 /* Include stack tracing code */
#define TRACE		1 /* Include packet tracing code */
#define MONITOR		1 /* Include user-port monitor trace mode */
#undef MONSTAMP		1 /* add time stamp to monitor-style trace headers */
#undef DIALER		1 /* SLIP/PPP redial code */
#undef POP2CLIENT	1 /* POP2 client -- IAB not recommended */
#undef POP3CLIENT	1 /* POP3 client -- IAB draft standard */
#undef POPT4		1 /* add 'pop t4' command to pop3 client, setting timeout */
#undef RDATECLI		1 /* Time Protocol client */
#define REMOTECLI	1 /* remote UDP kick/exit/reset */
#undef ESCAPE		1 /* Allow Unix style escape on PC */
#define ATCMD		1 /* Include timed 'at' execution */
#define NR4TDISC	1 /* Include Netrom L4 timeout-disconnect */
#undef XMODEM		1 /* xmodem file xfer for tipmail  */
#undef IPACCESS		1 /* Include IP access control code */
#undef TCPACCESS	1 /* Include TCP access control code */
#undef MD5AUTHENTICATE	1 /* Accept MD5-Authenticated logins */
#undef ENCAP		1 /* Include IP encapsulation code */
#undef MBOX_DYNIPROUTE	1 /* Add XG mbox cmd to route dynamic IPaddr via encap*/
#undef UDP_DYNIPROUTE	1 /* Support dynamic IPaddr encap routes via UDP/remote */
#undef AUTOROUTE	1 /* Include AX.25 IP auto-route code(causes problems when VC mode is used for ip) */
#undef HOPPER		1 /* Include SMTP hopper code by G8FSL */
#define TRANSLATEFROM	1 /* smtp server rewrites from addrs too */
#undef SMTP_REFILE	1 /* smtp server rewrites to addr according to from|to */
#undef AGGRESSIVE_GCOLLECT 1 /* exit 251 when availmem < 1/4 of 'mem threshold'*/
#undef KICK_SMTP_AFTER_SHELLCMD 1 /* kick smtp client after each shell cmd */
#undef TN_KK6JQ		1 /* add more telnet options support */
#undef LOCK		1 /* Include keyboard locking */
#undef AX25PASSWORD	1 /* Ask ax.25 users for their passwords */
#undef NRPASSWORD	1 /* Also ask NetRom users for passwords */
#define TTYCALL		1 /* Include AX.25 ttylink call */
#undef TTYCALL_CONNECT	1 /* SABM pkt uses TTYCALL, not BBSCALL, as src call */
#undef MULTITASK	1 /* Include Dos shell multi-tasker */
#define SHELL		1 /* Include shell command */
#undef SWATCH		1 /* stopWATCH code */
#undef UNIX_DIR_LIST	1 /* Unix-style output from DIR and ftp DIR cmds. */
#undef MEMLOG		1 /* include alloc/free debugging to MEMLOG.DAT file? */
#define ALLCMD		1 /* include dump,fkey,info,mail,motd,record,tail,
			   * taillog,upload,watch commands */
#undef DOSCMD		1 /* Include cd,copy,del,mkdir,pwd,ren,rmdir commands */
#undef MAILMSG		1 /* Include mailmsg command */
  
#define SPLITSCREEN	1 /* Needed for split, netrom split, and ttylink cmds */
#define STATUSWIN	1 /* Up to 3 line status window */
#undef STATUSWINCMD	1 /* status off|on command to modify status window */
#undef SHOWIDLE		1 /* show relative system-idle in status line */
#undef SHOWFH		1 /* show free filehandles in status line */

#undef PS_TRACEBACK	1 /* ps <pid> option enabled to do a back-trace */
#undef RXECHO		1 /* Echo rx packet to another iface - WG7J */
#define REDIRECT	1 /* Allow cmd [options] > outfile. Use >> to append */
#undef EDITOR		1 /* include internal ascii editor */
			/* PICK ONE FROM CHOICES ED or TED: */
#define ED		1 /* editor uses Unix ed syntax; OK for remote sysops. ~13KB */
#undef TED		1 /* editor uses TED syntax; local console only */
  
/* Protocol options */
  
#define AX25		1 /* Ax.25 support */
#define NETROM		1 /* NET/ROM network support */
#undef NRS		1 /* NET/ROM async interface */
#undef RIP		1 /* Include RIP routing */
#undef RIP98		1 /* Include RIP98 routing */
#undef LZW		1 /* LZW-compressed sockets */
#undef SLIP		1 /* Serial line IP on built-in ports */
#undef PPP		1 /* Point-to-Point Protocol code */
#undef PPP_DEBUG_RAW	1 /* Additional PPP debugging code...see pppfsm.h */
#undef VJCOMPRESS	1 /* Van Jacobson TCP compression for SLIP */
#undef RSPF		1 /* Include Radio Shortest Path First Protocol */
#undef AXIP		1 /* digipeater via ip port 93 interface */
#undef RARP		1 /* Include Reverse Address Resolution Protocol */
#undef BOOTPCLIENT	1 /* Include BootP protocol client */
  
  
/* Network services */
  
#define SERVERS		1 /* Include TCP servers */
#define AX25SERVER	1 /* Ax.25 server */
#define NETROMSERVER	1 /* Net/rom server */
#define TELNETSERVER	1 /* Tcp telnet server */
#undef RSYSOPSERVER	1 /* Tcp telnet-to-mbox-as-sysop server */
#undef TRACESERVER	1 /* remote interface trace server */
#define TTYLINKSERVER	1 /* Tcp ttylink server */
#undef TTYLINK_AUTOSWAP	1 /* ttylink server automatically swaps to new session */
#define SMTPSERVER	1 /* Tcp smtp server */
#undef RELIABLE_SMTP_BUT_SLOW_ACK 1 /* smtp server delays msg ack until filing completed */
#undef TIMEZONE_OFFSET_NOT_NAME 1 /* smtp headers use hhmm offset from GMT */
#undef SMTP_DENY_RELAY	1 /* Refuse to relay msgs from hosts not in our subnets */
#undef SMTP_VALIDATE_LOCAL_USERS 1 /* local user must be in ftpusers/popusers/users.dat */
#define FTPSERVER	1 /* Tcp ftp server */
#undef FTPDATA_TIMEOUT	1 /* ftp server timeout on recvfile */
#undef WS_FTP_KLUDGE	1 /* ftp server lies to please ws_ftp winsock app */
#define FTPSERV_MORE	1 /* ftp server supports RNFR, RNTO, MDTM commands */
#define FINGERSERVER	1 /* Tcp finger server */
#undef POP2SERVER	1 /* POP2 server -- IAB not recommended */
#define POP3SERVER	1 /* POP3 server -- IAB draft standard */
#define POP_TIMEOUT	600 /* pop server idle timeout value, in secs (>= 600) */
#define REMOTESERVER	1 /* Udp remote server */
#undef RDATESERVER	1 /* Time Protocol server */
#undef ECHOSERVER	1 /* Tcp echo server */
#undef DISCARDSERVER	1 /* Tcp discard server */
#undef TIPSERVER	1 /* Serial port tip server */
#define DOMAINSERVER	1 /* Udp Domain Name Server */
#undef TERMSERVER       1 /* Async serial interface server */
#undef IDENTSERVER	1 /* RFC 1413 identification server (113/tcp) */
#undef BOOTPSERVER	1 /* Include BootP protocol server */
#undef TCPGATE		1 /* TCPGATE redirector server */
/* Pick ONE of the following 5 callbook servers: */
#undef CALLSERVER	1 /* Include BuckMaster CDROM server support */
#undef ICALL		1 /* Buckmaster's international callsign database April '92 */
#undef BUCKTSR		1 /* Buckmaster callsign DB via bucktsr.exe (April 95) */
#undef SAMCALLB		1 /* SAM callbook server. Note that you can NOT have */
			  /* BOTH Buckmaster and SAM defined.If so, SAM is used */
#undef QRZCALLB		1 /* QRZ callbook server. Note that you can NOT have */
			  /* BOTH Buckmaster and QRZ defined.If so, QRZ is used */

#undef HTTP		1 /* Selcuk Ozturk's HTTP server on port 80 */
#undef HTTP_EXTLOG	1 /* HTTP: Add detailed access logging in "/wwwlogs" dir */
#undef CGI_XBM_COUNTER	1 /* HTTP: Add an X-bitmap counter via CGI */
#undef CGI_POSTLOG	1 /* HTTP: Add a POST demo via CGI */

/* Outgoing Sessions */
  
#define SESSIONS	1
#undef CALLCLI		1 /* Include only callbook client code (to query remote server) */
#define AX25SESSION	1 /* Connect and (if SPLITSCREEN) split commands */
#undef AXUISESSION	1 /* Ax.25 unproto (axui) command */
#define NETROMSESSION	1 /* netrom connect & split iff NETROM defined */
#define TELNETSESSION	1 /* telnet cmd */
#define TTYLINKSESSION	1 /* ttylink cmd - split-screen chat */
#define BBSSESSION	1 /* bbs cmd (same as telnet localhost) */
#define FTPSESSION	1 /* ftp,abort,ftype, and iff LZW: ftpclzw,ftpslzw cmds */
#define FTP_RESUME	1 /* add Jnos ftp client resume&rput cmds */
#define FTP_REGET	1 /* add RFC959 ftp client reget&restart cmds */
#define FTP_RENAME	1 /* add RFC959 ftp client rename command */
#define FINGERSESSION	1 /* finger cmd */
#define PINGSESSION	1 /* ping cmd */
#undef HOPCHECKSESSION	1 /* IP path tracing command */
#undef RLOGINSESSION	1 /* Rlogin client code */
#define TIPSESSION	1 /* tip - async dumb terminal emulator */
#undef DIRSESSION	1 /* dir cmd */
#undef MORESESSION	1 /* more - view ASCII file page by page */
#undef REPEATSESSION	1 /* repeat cmd */
#define LOOKSESSION	1 /* follow user activity on the bbs */
#undef DQUERYSESSION	1 /* Include "domain query" cmd */
  
/* Mailbox options */
  
#define MAILBOX		1 /* Include SM0RGV mailbox server */
#define MAILCMDS	1 /* Include mail commands, S, R, V etc */
#undef SEND_EDIT_OK	1 /* Send cmd offers (E)dit option to mbox users */
#define FILECMDS	1 /* Include D,U,W,Z commands */
#undef GATECMDS		1 /* Include gateway releated commands C,E,N,NR,P,PI,T */
#undef GWTRACE		1 /* Log all gateway connects to the logfile */
#define FOQ_CMDS	1 /* Include Finger, Operator, Query
			   * If GATECMDS and FOQ_CMDS are both undefined,
			   * extra code is saved! */
#undef MBOX_PING_ALLOWED 1 /* undef=>telnet permission needed for mbox ping */
#undef MBOX_FINGER_ALLOWED 1 /* undef=>telnet permission needed for mbox finger */
#define EXPIRY		1 /* Include message and bid expiry */
#define MAILFOR		1 /* Include Mailbox 'Mail for' beacon */
#define RLINE		1 /* Include BBS R:-line interpretation code */
#define MBFWD		1 /* Include Mailbox AX.25 forwarding */
#define FBBFWD		1 /* add enhanced FBB Forwarding code (no compression) */
#define FBBCMP		1 /* add FBB LZH-Compressed forwarding code */
#define FWDCTLZ		1 /* Use a CTRL-Z instead of /EX to end message forwarding */
#undef FWD_COMPLETION_CMD 1 /* run a forwarding-completed command if set in script */
#undef FBBVERBOSELOG	1 /* log more data for FBB-protocol transfers */
#define USERLOG		1 /* Include last-msg-read,prompt-type user tracking */
#define REGISTER	1 /* Include User Registration option */
#define MAILERROR	1 /* Include Mail-on-error option */
#define HOLD_LOCAL_MSGS	1 /* Hold locally-originated msgs for review by sysop */
#undef FWDFILE		1 /* Include forwarding-to-file (export) feature */
#define EXPEDITE_BBS_CMD 1 /* Use MD5 and net.rc to autologin console to bbs */
  
  
/* Memory options */
  
#undef EMS		1 /* Include Expanded Memory Usage */
#define XMS		1 /* Include Extended Memory Usage */
  
  
/* Software tuning parameters */
  
#define MTHRESH		16384	/* Default memory threshold */
#define NROWS		25	/* Number of rows on screen */
#define NIBUFS		5	/* Number of interrupt buffers */
#define IBUFSIZE	2048	/* Size of interrupt buffers */
#define NSESSIONS	10	/* Number of interactive clients */
#define NAX25		24	/* Number of axip interfaces (if defined) */
#define MAXSCC		4	/* Max number of SCC+ESCC chips (< 16) */
  

/* Hardware driver options */
  
#define ASY		1 /* Asynch driver code */
#undef HP95		1 /* hp95-style uart handling */
#undef KISS		1 /* Multidrop KISS TNC code for Multiport tnc */
#undef POLLEDKISS	1 /* G8BPQ Polled Multidrop KISS TNC code */
#undef PACKET		1 /* FTP Software's Packet Driver interface */
#undef SCC		1 /* PE1CHL generic scc driver */
#undef BPQ		1 /* include Bpqhost interface */
#undef PACKETWIN	1 /* Gracilis PackeTwin driver */
#undef PI		1 /* VE3IFB pi dma card scc driver */
#undef ARCNET		1 /* ARCnet via PACKET driver */
#undef PC_EC		1 /* 3-Com 3C501 Ethernet controller */
#undef HS		1 /* High speed (56kbps) modem driver */
#undef HAPN		1 /* Hamilton Area Packet Network driver code */
#undef EAGLE		1 /* Eagle card driver */
#undef PC100		1 /* PAC-COM PC-100 driver code */
#undef APPLETALK	1 /* Appletalk interface (Macintosh) */
#undef DRSI		1 /* DRSI PCPA slow-speed driver */
#undef SLFP		1 /* SLFP packet driver class supported */
#define PRINTEROK	1 /* OK to name a printer as an output device */
  
  
/***************************************************************************/
/* This section corrects some defines that include/exclude others          */
/* You should not normally change anything below this line.                */
  
#ifdef STATUSWIN
#define SPLITSCREEN	1
#endif
  
#ifdef DIRSESSION
#undef MORESESSION
#define MORESESSION	1
#endif
  
#if defined(NRS)
#undef  NETROM
#define NETROM		1 /* NRS implies NETROM */
#endif
  
#if defined(ARCNET) || defined(SLFP)
#undef  PACKET
#define PACKET		1 /* FTP Software's Packet Driver interface */
#endif
  
#if defined(PC_EC) || defined(PACKET)
#define ETHER		1 /* Generic Ethernet code */
#endif
  
#if defined(BUCKTSR)
#define CALLCLI		1
#undef CALLSERVER	1
#undef ICALL		1
#undef SAMCALLB		1
#undef QRZCALLB		1
#endif

#if defined(SAMCALLB)
#define CALLCLI		1
#undef CALLSERVER	1
#undef ICALL		1
#undef BUCKTSR		1
#undef QRZCALLB		1
#endif
  
#if defined(QRZCALLB)
#define CALLCLI		1
#undef CALLSERVER	1
#undef ICALL		1
#undef BUCKTSR		1
#undef SAMCALLB		1
#endif

#if defined(CALLSERVER)
#define CALLCLI		1
#endif
  
#if defined(POP2CLIENT) || defined(POP3CLIENT)
#define MAILCLIENT	1
#endif
  
#ifdef POLLEDKISS
#define KISS		1
#endif
  
#ifndef TRACE
#undef MONITOR		1
#undef MONSTAMP		1
#undef TRACESERVER	1
#endif

#ifndef MAILBOX
#undef MAILCMDS		1
#undef FILECMDS		1
#undef GATECMDS		1
#undef FOQ_CMDS		1
#undef CALLBOOK		1
#undef CALLCLI		1
#undef EXPIRY		1
#undef MBXTDISC		1
#undef TIPSERVER	1
#undef MAILFOR		1
#undef RLINE		1
#undef MBFWD		1
#undef FBBFWD		1
#undef FBBCMP		1
#undef USERLOG		1
#endif
  
#ifndef MAILCMDS
#undef MAILFOR		1
#undef RLINE		1
#undef MBFWD		1
#undef HOLD_LOCAL_MSGS	1
#endif
  
#ifndef TIPSERVER
#undef XMODEM		1
#endif
  
#ifndef AX25
#undef AX25SESSION	1
#undef AXUISESSION	1
#undef AX25SERVER	1
#undef MAILFOR		1
#undef NRS		1
#undef NETROM		1
#undef NETROMSESSION	1
#undef NETROMSERVER	1
#undef AXIP		1
#undef NR4TDISC		1
#undef TTYCALL		1
#undef BPQ		1
#undef EAGLE		1
#undef SCC		1
#undef KISS		1
#undef POLLEDKISS	1
#undef HAPN		1
#undef PI		1
#undef PC100		1
#undef HS		1
#undef AX25PASSWORD	1
#undef NRPASSWORD	1
#endif
  
#ifndef NETROM
#undef NRPASSWORD	1
#undef NETROMSESSION	1
#undef NETROMSERVER	1
#endif
  
#ifndef SMTPSERVER
#undef MAILCMDS		1
#undef MBFWD		1
#undef MAILFOR		1
#undef RLINE		1
#undef TRANSLATEFROM	1
#endif
  
#ifndef MBFWD
#undef FWDFILE		1
#undef FBBFWD		1
#undef FBBCMP		1
#endif
  
#ifndef FBBFWD
#undef FBBCMP		1
#else
#define USERLOG		1 /* need to remember (new)lastread */
#endif

#ifndef CONVERS
#undef LINK		1
#undef XCONVERS		1
#undef CNV_VERBOSE	1
#undef CNV_CHAN_NAMES	1
#undef CNV_TOPICS	1
#undef CNV_CALLCHECK	1
#undef CNV_LOCAL_CHANS	1
#undef CNV_ENHANCED_VIA	1
#undef CNV_CHG_PERSONAL	1
#undef CNV_LINKCHG_MSG	1
#undef CNV_VERBOSE_CHGS	1
#endif

#ifndef LZW
#undef XCONVERS		1
#endif

#ifdef UNIX
/* Following options are incompatible with UNIX environment */
/* Many go away when MSDOS is not defined, but it doesn't hurt to undef them here too */
#undef SHOWIDLE
#undef MULTITASK
#undef MEMLOG
#undef TED
#undef EMS
#undef XMS
#undef PACKET
#undef STKTRACE
#undef SWATCH
#undef SCC
#undef PI
#undef BPQ
#undef HS
#undef HAPN
#undef EAGLE
#undef DRSI
#undef PC100
#undef SLFP
#undef PACKETWIN
#undef ARCNET
#undef PC_EC
/* what else?? */
/* Following might work someday, but need work... */
#undef TERMSERVER
#undef CALLSERVER
#undef ICALL
#undef BUCKTSR
#undef SAMCALLB
/* be sure we have a session mgr */
#ifndef SM_CURSES
#ifndef SM_DUMB
#ifndef SM_RAW
#define SM_CURSES 1
#endif
#endif
#endif
#else /* ! UNIX */
#undef SM_CURSES 1
#undef SM_DUMB 1
#undef SM_RAW 1
#endif

#if defined(EDITOR) && defined(ED) && defined(TED)
#error Cannot #define both ED and TED
#endif
#if defined(NNTP) && defined(NNTPS)
#error Cannot #define both NNTP and NNTPS
#endif
#if NIBUFS == 0
#error NIBUFS should never be zero
#endif

#endif  /* _CONFIG_H */
