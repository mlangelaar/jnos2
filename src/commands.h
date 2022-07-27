 /* Mods by PA0GRI */
#ifndef _COMMANDS_H
#define _COMMANDS_H
  
/* In 8250.c, amiga.c: */
int doasystat __ARGS((int argc,char *argv[],void *p));
  
/* In alloc.c: */
int domem __ARGS((int argc,char *argv[],void *p));
int dostat __ARGS((int argc,char *argv[],void *p));
  
/* In amiga.c: */
int doamiga __ARGS((int argc,char *argv[],void *p));

#ifdef  APRSD
/* In aprssrv.c */
int doaprs (int argc, char *argv[], void *p);
int dombaprs (int argc, char *argv[], void *p);

/* 03Apr2004, Maiko, Only one command now to start all aprs services,
 * the old t14501on/off, etc, etc are no longer defined or used
 */
int aprsstart (int argc, char *argv[], void *p);
int aprsstop (int argc, char *argv[], void *p);

#endif
  
/* In arpcmd.c: */
int doarp __ARGS((int argc,char *argv[],void *p));
  
/* In asy.c: */
int asy_attach __ARGS((int argc,char *argv[],void *p));
int doasycomm __ARGS((int argc,char *argv[],void *p));
  
/* In at.c */
int doat __ARGS((int argc,char *argv[],void *p));
  
/* In ax25.c: */
int axip_attach __ARGS((int argc,char *argv[],void *p));
int doaxstat __ARGS((int argc,char *argv[],void *p));

#ifdef	FLDIGI_KISS
/* In fldigi.c */
int fldigi_attach (int argc, char **argv, void *p);
#endif
  
/* In ax25aar.c: */
int doax25autoroute __ARGS((int argc,char *argv[],void *p));
  
/* In ax25cmd.c: */
int doax25 __ARGS((int argc,char *argv[],void *p));
int doaxheard __ARGS((int argc,char *argv[],void *p));
int doaxdest __ARGS((int argc,char *argv[],void *p));
int doconnect __ARGS((int argc,char *argv[],void *p));
int ifax25 __ARGS((int argc,char *argv[],void *p));
  
int doax25window __ARGS((int argc,char *argv[],void *p));
int doax25blimit __ARGS((int argc,char *argv[],void *p));
int doax25maxframe __ARGS((int argc,char *argv[],void *p));
int doax25n2 __ARGS((int argc,char *argv[],void *p));
int doax25paclen __ARGS((int argc,char *argv[],void *p));
int doax25pthresh __ARGS((int argc,char *argv[],void *p));
int doax25t2 __ARGS((int argc,char *argv[],void *p));	/* K5JB */
int doax25t3 __ARGS((int argc,char *argv[],void *p));
int doax25timertype __ARGS((int argc,char *argv[],void *p));
int doax25t4 __ARGS((int argc,char *argv[],void *p));
int doax25version __ARGS((int argc,char *argv[],void *p));
int doax25maxwait __ARGS((int argc,char *argv[],void *p));
  
/* In bootp.c */
int dobootp __ARGS((int argc,char *argv[],void *p));
  
/* In bootpd.c */
int bootpdcmd __ARGS((int argc,char *argv[],void *p));
  
/* In bpq.c */
int bpq_attach __ARGS((int argc, char *argv[], void *p));
int dobpq __ARGS((int argc, char *argv[], void *p));
  
/* In buckbook.c  */
int docallserver __ARGS((int argc,char *argv[],void *p));
int docdrom __ARGS((int argc, char *argv[], void *p));
  
/* In calldbd.c: */
int cdbstart __ARGS((int argc,char *argv[],void *p));
int cdb0 __ARGS((int argc,char *argv[],void *p));
int docallbook __ARGS((int argc,char *argv[],void *p));
  
/* In convers.c */
int doconvers __ARGS((int argc, char **argv, void *p));
  
/* In dialer.c: */
int dodialer __ARGS((int argc,char *argv[],void *p));
  
/* In dirutil.c: */
int docd __ARGS((int argc,char *argv[],void *p));
int dodir __ARGS((int argc,char *argv[],void *p));
int dircmd __ARGS((int argc,char *argv[],void *p));
int domkd __ARGS((int argc,char *argv[],void *p));
int dormd __ARGS((int argc,char *argv[],void *p));
int docopy __ARGS((int argc, char *argv[], void *p));
int dodelete __ARGS((int argc,char *argv[],void *p));
int dorename __ARGS((int argc,char *argv[],void *p));
  
/* In domain.c: */
int dodomain __ARGS((int argc,char *argv[],void *p));
  
/* In drsi.c: */
int dodrstat __ARGS((int argc,char *argv[],void *p));
int dr_attach __ARGS((int argc,char *argv[],void *p));
int dodr __ARGS((int argc,char *argv[],void *p));
  
/* In eagle.c: */
int eg_attach __ARGS((int argc,char *argv[],void *p));
int doegstat __ARGS((int argc,char *argv[],void *p));
  
/* In ec.c: */
int doetherstat __ARGS((int argc,char *argv[],void *p));
int ec_attach __ARGS((int argc,char *argv[],void *p));
  
/* In editor.c: */
int editor __ARGS((int argc,char *argv[],void *p));

/* In expire.c: */
int doexpire __ARGS((int argc,char *argv[],void *p));
int dooldbids __ARGS((int argc,char *argv[],void *p));
  
/* In finger.c: */
int dofinger __ARGS((int argc,char *argv[],void *p));
  
/* In fingerd.c: */
int finstart __ARGS((int argc,char *argv[],void *p));
int fin0 __ARGS((int argc,char *argv[],void *p));
  
/* In ftpcli.c: */
int doftp __ARGS((int argc,char *argv[],void *p));
int doftype __ARGS((int argc,char *argv[],void *p));
int doabort __ARGS((int argc,char *argv[],void *p));
int doftpslzw __ARGS((int argc,char *argv[],void *p));
int doftpclzw __ARGS((int argc,char *argv[],void *p));
  
/* In ftpserv.c: */
int ftpstart __ARGS((int argc,char *argv[],void *p));
int ftp0 __ARGS((int argc,char *argv[],void *p));
int doftptdisc __ARGS((int argc,char *argv[],void *p));
int doftpmaxsrv __ARGS((int argc,char *argv[],void *p));
  
/* In forward.c: */
int fwdstart __ARGS((int argc,char *argv[],void *p));
int fwd0 __ARGS((int argc,char *argv[],void *p));
  
/* In tcpgate.c: */
int gatestart __ARGS((int argc,char *argv[],void *p));
int gate0 __ARGS((int argc,char *argv[],void *p));
int dogate __ARGS((int argc,char *argv[],void *p)); 

/* In hapn.c: */
int dohapnstat __ARGS((int argc,char *argv[],void *p));
int hapn_attach __ARGS((int argc,char *argv[],void *p));

/* 09Mar2005, Maiko, New HFDD commands required */
#ifdef	HFDD
/* in hfddrtns.c */
int dohfdd (int argc, char *argv[], void *p);
#endif
 
/* In http.c: */
int httpstart __ARGS((int argc,char *argv[],void *p));
int http0 __ARGS((int argc,char *argv[],void *p));
int dohttp __ARGS((int argc,char *argv[],void *p)); 
  
/* In hop.c: */
int dohop __ARGS((int argc,char *argv[],void *p));
  
/* In hs.c: */
int dohs __ARGS((int argc,char *argv[],void *p));
int hs_attach __ARGS((int argc,char *argv[],void *p));
  
/* In icmpcmd.c: */
int doicmp __ARGS((int argc,char *argv[],void *p));
int doping __ARGS((int argc,char *argv[],void *p));
  
/* In iface.c: */
int doifconfig __ARGS((int argc,char *argv[],void *p));
int dodetach __ARGS((int argc,char *argv[],void *p));
  
/* In index.c */
int doindex __ARGS((int argc, char *argv[], void *p));
  
/* In ipcmd.c: */
int doip __ARGS((int argc,char *argv[],void *p));
int doroute __ARGS((int argc,char *argv[],void *p));
  
/* In kiss.c */
int kiss_attach __ARGS((int argc,char *argv[],void *p));
  
/* In ksubr.c: */
int ps __ARGS((int argc,char *argv[],void *p));
  
/* In lzw.c */
int dolzw __ARGS((int argc,char *argv[],void *p));
  
/* In mailbox.c: */
int dombox __ARGS((int argc,char *argv[],void *p));
int dombmailstats __ARGS((int argc,char *argv[],void *p));
  
/* In mailfor.c */
int dombrline __ARGS((int argc,char *argv[],void *p));
  
/* In mailutil.c */
int dorewrite __ARGS((int argc,char *argv[],void *p));
  
/* In main.c: */
int doexit __ARGS((int argc,char *argv[],void *p));
int dohostname __ARGS((int argc,char *argv[],void *p));
int dolog __ARGS((int argc,char *argv[],void *p));
int dohelp __ARGS((int argc,char *argv[],void *p));
int doattach __ARGS((int argc,char *argv[],void *p));
int doparam __ARGS((int argc,char *argv[],void *p));
int domode __ARGS((int argc,char *argv[],void *p));
int domore __ARGS((int argc,char *argv[],void *p));
int donothing __ARGS((int argc,char *argv[],void *p));
int dopause __ARGS((int argc,char *argv[],void *p));
int doescape __ARGS((int argc,char *argv[],void *p));

/* 24Apr2021, Maiko (VE4KLM), New, print date to console */
int dodate (int argc, char **argv, void *p);

int doremark __ARGS((int argc,char *argv[],void *p));
int doremote __ARGS((int argc,char *argv[],void *p));
int doboot __ARGS((int argc,char *argv[],void *p));
int dosource __ARGS((int argc,char *argv[],void *p));
int doattended __ARGS((int argc,char *argv[],void *p));
int domdump __ARGS((int argc,char *argv[],void *p));
int dostatus __ARGS((int argc,char *argv[],void *p));
int domotd __ARGS((int argc,char *argv[],void *p));
int dofmotd __ARGS((int argc,char *argv[],void *p));
int dothirdparty __ARGS((int argc,char *argv[],void *p));
int dotail __ARGS((int argc,char *argv[],void *p));
int dotaillog __ARGS((int argc,char *argv[],void *p));
int dolock __ARGS((int argc,char *argv[],void *p));
int morecmd __ARGS((int argc,char *argv[],void *p));
int doerror __ARGS((int, char **, void *));
int dowrite __ARGS((int, char **, void *));
int dowriteall __ARGS((int, char **, void *));
int doprompt __ARGS((int, char **, void *));
int dohistory __ARGS((int, char **, void *));
int dorepeat __ARGS((int, char **, void *));
  
/* In look.c */
int dolook __ARGS((int argc,char *argv[],void *p));
  
/* In nntpcli.c */
int donntp __ARGS((int argc,char *argv[],void *p));

/* 03Nov2013, Maiko, Need to be able to better configure INP functionality */
#ifdef INP2011
/* In newinp.c */
int doinp __ARGS((int argc,char *argv[],void *p));
#endif

/* In nrcmd.c: */
int donetrom __ARGS((int argc,char *argv[],void *p));
int nr_attach __ARGS((int argc,char *argv[],void *p));
int donrstatus __ARGS((int argc,char *argv[],void *p));
  
/* In nrs.c: */
int donrstat __ARGS((int argc,char *argv[],void *p));

/* In twin_dr.c/twin_at.c  - Gracilis PackeTwin */
int tsync_attach __ARGS((int argc,char *argv[],void *p));
int do_tsync_stat __ARGS((int argc,char *argv[],void *p));
int do_tsync_dump __ARGS((int argc,char *argv[],void *p));
  
/* In pc.c: */
int doshell __ARGS((int argc,char *argv[],void *p));
int dofkey __ARGS((int argc,char *argv[],void *p));
int doisat __ARGS((int argc,char *argv[],void *p));
int dobmail __ARGS((int argc,char *argv[],void *p));
int dobackg __ARGS((int argc,char *argv[],void *p));
int dowatchdog __ARGS((int argc,char *argv[],void *p));

/* In pc100.h: */
int pc_attach __ARGS((int argc,char *argv[],void *p));
  
/* In pktdrvr.c: */
int pk_attach __ARGS((int argc,char *argv[],void *p));
int dopkstat __ARGS((int argc,char *argv[],void *p));
  
/* In pi.c: */
int pi_attach __ARGS((int argc,char *argv[],void *p));
int dopistat __ARGS((int argc,char *argv[],void *p));
  
/* In popcli.c */
int dopop __ARGS((int argc,char *argv[],void *p));
  
/* In popserv.c */
int pop1 __ARGS((int argc,char *argv[],void *p));
int pop0 __ARGS((int argc,char *argv[],void *p));
  
/* in pop2serv.c */
int pop2start   __ARGS((int argc,char *argv[],void *p));
int pop2stop    __ARGS((int argc,char *argv[],void *p));
  
/* in pop3serv.c */
int pop3start   __ARGS((int argc,char *argv[],void *p));
int pop3stop    __ARGS((int argc,char *argv[],void *p));
  
/* In rarp.c: */
int dorarp __ARGS((int argc,char *argv[],void *p));
  
/* In ripcmd.c: */
int dodroprefuse __ARGS((int argc,char *argv[],void *p));
int doripadd __ARGS((int argc,char *argv[],void *p));
int doripproxy __ARGS((int argc,char *argv[],void *p));
int doripdrop __ARGS((int argc,char *argv[],void *p));
int doripauthadd __ARGS((int argc,char *argv[],void *p));
int doripauthdrop __ARGS((int argc,char *argv[],void *p));
int doripreject __ARGS((int argc,char *argv[],void *p));
int doripfilter __ARGS((int argc,char *argv[],void *p));
int doripkick __ARGS((int argc,char *argv[],void *p));
int doripmerge __ARGS((int argc,char *argv[],void *p));
int doaddrefuse __ARGS((int argc,char *argv[],void *p));
int doripreq __ARGS((int argc,char *argv[],void *p));
int doripstat __ARGS((int argc,char *argv[],void *p));
int doriptrace __ARGS((int argc,char *argv[],void *p));
int doripttl __ARGS((int argc,char *argv[],void *p));
int dorip __ARGS((int argc,char *argv[],void *p));
int doripinit __ARGS((int argc,char *argv[],void *p));
int doripstop __ARGS((int argc,char *argv[],void *p));
int dorip98rx __ARGS((int argc,char *argv[],void *p));
  
/* In rdate.c: */
int dordate __ARGS((int argc,char *argv[],void *p));
  
/* In timed.c */
int time0 __ARGS((int argc,char *argv[],void *p));
int time1 __ARGS((int argc,char *argv[],void *p));
  
/* In rlogin.c: */
int dorlogin __ARGS((int argc,char *argv[],void *p));
  
/* In rspf.c: */
int dorspf __ARGS((int argc,char *argv[],void *p));
  
/* In scc.c: */
int scc_attach __ARGS((int argc,char *argv[],void *p));
int dosccstat __ARGS((int argc,char *argv[],void *p));
  
/* In session.c: */
int dosession __ARGS((int argc,char *argv[],void *p));
int go __ARGS((int argc,char *argv[],void *p));
int doclose __ARGS((int argc,char *argv[],void *p));
int doreset __ARGS((int argc,char *argv[],void *p));
int dokick __ARGS((int argc,char *argv[],void *p));
int dorecord __ARGS((int argc,char *argv[],void *p));
int doupload __ARGS((int argc,char *argv[],void *p));
  
/* In smisc.c: */
int trace1 __ARGS((int argc,char *argv[],void *p));
int trace0 __ARGS((int argc,char *argv[],void *p));
int dis1 __ARGS((int argc,char *argv[],void *p));
int dis0 __ARGS((int argc,char *argv[],void *p));
int echo1 __ARGS((int argc,char *argv[],void *p));
int echo0 __ARGS((int argc,char *argv[],void *p));
int rem1 __ARGS((int argc,char *argv[],void *p));
int rem0 __ARGS((int argc,char *argv[],void *p));
int rsysop1 __ARGS((int argc,char *argv[],void *p));
int rsysop0 __ARGS((int argc,char *argv[],void *p));
int identstart __ARGS((int argc,char *argv[],void *p));
int identstop __ARGS((int argc,char *argv[],void *p));
  
/* In smtpcli.c: */
int dosmtp __ARGS((int argc,char *argv[],void *p));
  
/* In smtpserv.c: */
int smtp1 __ARGS((int argc,char *argv[],void *p));
int smtp0 __ARGS((int argc,char *argv[],void *p));
int domailmsg __ARGS((int argc,char *argv[],void *p));
  
/* In sockcmd.c: */
int dosock __ARGS((int argc,char *argv[],void *p));
int dokicksocket __ARGS((int argc,char *argv[],void *p));
  
/* In sw.c: */
int doswatch __ARGS((int argc,char *argv[],void *p));
  
/* In tcpcmd.c: */
int dotcp __ARGS((int argc,char *argv[],void *p));
int doview __ARGS((int argc,char *argv[],void *p));
  
/* In telnet.c: */
int doecho __ARGS((int argc,char *argv[],void *p));
int doeol __ARGS((int argc,char *argv[],void *p));
int dotelnet __ARGS((int argc,char *argv[],void *p));
  
/* In term.c: */
int doterm __ARGS((int argc,char *argv[],void *p));
int term0 __ARGS((int argc,char *argv[],void *p));
int term1 __ARGS((int argc,char *argv[],void *p));
  
/* In tip.c: */
int dotip __ARGS((int argc,char *argv[],void *p));
  
/* In ttylink.c: */
int ttylstart __ARGS((int argc,char *argv[],void *p));
int ttyl0 __ARGS((int argc,char *argv[],void *p));

/* In RMSlink.c: - new 30Jun2019, Maiko
 * now more generic tnlink.c - 30July2019
 */
int tnlstart __ARGS((int argc,char *argv[],void *p));
int tnl0 __ARGS((int argc,char *argv[],void *p));
  
/* In trace.c: */
int dotrace __ARGS((int argc,char *argv[],void *p));
int dostrace __ARGS((int argc,char *argv[],void *p));
  
/* In udpcmd.c: */
int doudp __ARGS((int argc,char *argv[],void *p));
  
/* In version.c: */
int doinfo __ARGS((int argc,char *argv[],void *p));
  
#ifdef UNIX
/* In sessmgr.c */
extern int dosessmgr __ARGS((int, char **, void *));
#endif

#endif  /* _COMMANDS_H */
