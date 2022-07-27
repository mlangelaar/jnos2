#ifndef _CONFIG_H
#define _CONFIG_H
/* This is the configuration as distributed by WG7J */
  
/* Software options */
  
#define CONVERS     1   /* Conference bridge (babble-box :-) */
/* Use only ONE of the 2 news options below !!! */
#undef NNTP        1   /* Netnews client */
#undef NNTPS       1   /* Netnews client and server */
#undef STKTRACE   1   /* Include stack tracing code */
#define TRACE       1   /* Include packet tracing code */
#undef DIALER      1   /* SLIP redial code */
#undef POP2CLIENT  1   /* POP2 client -- IAB not recommended */
#define POP3CLIENT  1   /* POP3 client -- IAB draft standard */
#undef ESCAPE      1   /* Allow Unix style escape on PC */
#define ATCMD       1   /* Include timed 'at' execution */
#define NR4TDISC    1   /* Include Netrom L4 timeout-disconnect */
#define XMODEM      1   /* xmodem file xfer for tipmail  */
#undef IPACCESS    1   /* Include IP access control code */
#undef TCPACCESS   1   /* Include TCP access control code */
#undef ENCAP       1   /* Include IP encapsulation code */
#undef AUTOROUTE   1   /* Include AX.25 IP auto-route code(causes problems when VC mode is used for ip) */
#define LOCK        1   /* Include keyboard locking */
#define TTYCALL     1   /* Include AX.25 ttylink call */
#define MONITOR     1   /* Include user-port monitor trace mode */
#define MULTITASK   1   /* Include Dos shell multi-tasker */
#define SHELL       1   /* Include shell command */
#undef SWATCH      1   /* stopWATCH code */
#define ALLCMD     1    /* if undefined, exclude a bunch of commands */
/*excluded are:
 *   delete,rename,more,tail,dump,status,motd,cd,dir,finger,fkey,info,mail,mkdir
 *   pwd,record,rmdir,watch,test,upload
 */
  
#define SPLITSCREEN     1
  
/* Protocol options */
  
#define AX25        1   /* Ax.25 support */
#define NETROM      1   /* NET/ROM network support */
#define NRS         1   /* NET/ROM async interface */
#undef RIP         1   /* Include RIP routing */
#undef LZW         1   /* LZW-compressed sockets */
#define SLIP        1   /* Serial line IP on built-in ports */
#undef PPP         1   /* Point-to-Point Protocol code */
#undef VJCOMPRESS  1   /* Van Jacobson TCP compression for SLIP */
#undef RDATE       1   /* Include the Time Protocol */
#undef RSPF        1   /* Include Radio Shortest Path First Protocol */
#define AXIP        1   /* digipeater via ip port 93 interface */
#undef RARP        1   /* Include Reverse Address Resolution Protocol */
  
  
/* Network services */
  
#define SERVERS         1   /* Include TCP servers */
#define AX25SERVER       1   /* Ax.25 server */
#define NETROMSERVER     1   /* Net/rom server */
#define TELNETSERVER     1   /* Tcp telnet server */
#define TTYLINKSERVER    1   /* Tcp ttylink server */
#define SMTPSERVER       1   /* Tcp smtp server */
#define FTPSERVER        1   /* Tcp ftp server */
#define FINGERSERVER     1   /* Tcp finger server */
#undef POP2SERVER       1   /* POP2 server -- IAB not recommended */
#define POP3SERVER      1   /* POP3 server -- IAB draft standard */
#define REMOTESERVER     1   /* Udp remote server */
#undef ECHOSERVER       1   /* Tcp echo server */
#undef DISCARDSERVER    1   /* Tcp discard server */
#define TIPSERVER        1   /* Serial port tip server */
#define DOMAINSERVER    1   /* Udp Domain Name Server */
#undef CALLSERVER       1   /* Include BuckMaster CDROM server support */
#undef ICALL            1   /* Buckmaster's international callsign database April '92 */
  
  
/* Outgoing Sessions */
  
#define SESSIONS        1
#undef CALLCLI          1   /* Include BuckMaster CDROM client code only  */
#define AX25SESSION      1
#define NETROMSESSION    1
#define TELNETSESSION    1
#define TTYLINKSESSION   1
#define BBSSESSION      1
#define FTPSESSION       1
#define FINGERSESSION    1
#define PINGSESSION      1
#undef HOPCHECKSESSION  1   /* IP path tracing command */
#undef RLOGINSESSION    1   /* Rlogin client code */
#define TIPSESSION       1
#define DIRSESSION       1
#define MORESESSION      1
#define REPEATSESSION    1
  
/* Mailbox options */
  
#define MAILBOX     1   /* Include SM0RGV mailbox server */
#define MAILCMDS    1   /* Include mail commands, S, R, V etc */
#define CALLBOOK    1   /* Simple callbook server over Internet */
#define EXPIRY      1   /* Include message and bid expiry */
#define MAILFOR     1   /* Include Mailbox 'Mail for' beacon */
#define RLINE       1   /* Include BBS R:-line interpretation code */
#define MBFWD       1   /* Include Mailbox AX.25 forwarding */
#define USERLOG     1   /* Include last-message-read tracking for users */
#define REGISTER    1   /* Include User Registration option */
#undef MAILERROR   1   /* Include Mail-on-error option */
  
  
/* Memory options */
  
#define EMS         1   /* Include Expanded Memory Usage */
 /* DO NOT define the following, it doesn't work yet !!! */
#undef XMS         1   /* Include Extended Memory Usage */
  
  
/* Software tuning parameters */
  
#define MTHRESH     16384    /* Default memory threshold */
#define NROWS       25      /* Number of rows on screen */
#define NIBUFS      5       /* Number of interrupt buffers */
#define IBUFSIZE    2048    /* Size of interrupt buffers */
#define NSESSIONS   10      /* Number of interactive clients */
#define DEFNSOCK    40      /* Default number of sockets */
  
  
/* Hardware driver options */
  
#define ASY         1   /* Asynch driver code */
#define KISS        1   /* KISS TNC code */
#define PACKET      1   /* FTP Software's Packet Driver interface */
#define SCC         1   /* PE1CHL generic scc driver */
#define BPQ         1   /* include Bpqhost interface */
#undef PACKETWIN   1   /* Gracilis PackeTwin driver */
#undef PI          1   /* VE3IFB pi dma card scc driver */
#undef ARCNET      1   /* ARCnet via PACKET driver */
#undef PC_EC       1   /* 3-Com 3C501 Ethernet controller */
#undef HS          1   /* High speed (56kbps) modem driver */
#undef HAPN        1   /* Hamilton Area Packet Network driver code */
#undef EAGLE       1   /* Eagle card driver */
#undef PC100       1   /* PAC-COM PC-100 driver code */
#undef APPLETALK   1   /* Appletalk interface (Macintosh) */
#undef DRSI        1   /* DRSI PCPA slow-speed driver */
#undef SLFP         1   /* SLFP packet driver class supported */
  
  
/***************************************************************************/
/* This section corrects some defines that include/exclude others          */
  
#ifdef DIRSESSION
#undef MORESESSION
#define MORESESSION 1
#endif
  
#if defined(NRS)
#undef  NETROM
#define NETROM      1   /* NRS implies NETROM */
#endif
  
#if defined(ARCNET) || defined(SLFP)
#undef  PACKET
#define PACKET      1   /* FTP Software's Packet Driver interface */
#endif
  
#if defined(PC_EC) || defined(PACKET)
#define ETHER   1       /* Generic Ethernet code */
#endif
  
#if defined(CALLSERVER)
#define CALLCLI     1
#endif
  
#if defined(POP2CLIENT) || defined(POP3CLIENT)
#define MAILCLIENT  1
#endif
  
#ifndef MAILBOX
#undef MAILCMDS 1
#undef CALLBOOK 1
#undef CALLCLI 1
#undef EXPIRY 1
#undef MBXTDISC 1
#undef TIPMAIL 1
#undef MAILFOR 1
#undef RLINE 1
#undef MBFWD 1
#undef USERLOG 1
#endif
  
#ifndef MAILCMDS
#undef USERLOG 1
#undef MAILFOR 1
#undef RLINE 1
#undef MBFWD 1
#endif
  
#ifndef TIPMAIL
#undef XMODEM 1
#endif
  
#ifndef AX25
#undef MAILFOR 1
#undef RLINE 1
#undef MBFWD 1
#undef NRS 1
#undef NETROM 1
#undef AXIP 1
#undef NR4TDISC 1
#undef TTYCALL 1
#undef BPQ 1
#undef EAGLE 1
#undef SCC 1
#undef KISS 1
#undef HAPN 1
#undef PI 1
#undef PC100 1
#undef HS 1
#undef AXIP 1
#endif
  
#endif  /* _CONFIG_H */
