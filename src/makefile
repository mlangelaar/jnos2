#
# Makefile for JNOS 2.0 - started 15Oct2004 by VE4KLM (Maiko Langelaar)
#
# 07Oct2019, Maiko (VE4KLM), Now using ./configure in conjunction with makefile
#  (configure_02Jan2022_v1.10 ensures compatibility with ./configure script)
#
#
CC = gcc
RM = rm -f
AR = ar rcs
#
#  PATCHES = -DDEBUG -DCHKSTK -fsigned-char
#
#   -DDEBUG      self explanatory
#   -DCHKSTK     enable stack checking (seems to cause random crashes too)
#
PATCHES = -fsigned-char
#
# JNOS uses NCURSES (make sure these match what you have installed)
#
ICURSES = -I/usr/include/ncurses
LCURSES = -lncurses
#
# Compiler Warnings
#
# JNOS/Linux is much cleaner in this area, but there are some portability
# warnings still (-Wtraditional) and some non-globally-prototyped functions.
#
WARNINGS = -W -Wimplicit -Wreturn-type -Wswitch -Wpointer-arith \
	   -Wcast-qual -Wcast-align -Waggregate-return -Wparentheses \
#	   -Wtraditional -Wunused\
	   -Wuninitialized -Wformat -Wconversion \
	   -Wstrict-prototypes -Wmissing-prototypes

WARNINGS = -Wall

# This is for my use in testing.  Don't worry about it unless you want to be
# able to build custom versions.
NOS = jnos

# I strongly advise leaving the debugging information in, because this is NOT
# production-quality code.  -g1 instead of -g3 will make nos smaller, however,
# and leaving off -g entirely will make it a LOT smaller (or use 'strip').
# Using -static will aid gdb in tracing failures in libc routines, at the
# expense of a larger binary.
#DEBUG = -static -g3  ## -DHOLD_PARSER=\"/usr/local/bin/scanjmsg\"
DEBUG = -g3  ## -DHOLD_PARSER=\"/usr/local/bin/scanjmsg\"
#DBGLIB = -lg

CFLAGS = -DUNIX $(DEBUG) $(PATCHES) $(WARNINGS)

all: c4cfg $(NOS) jnospwmgr

c4cfg:
	@if [ ! -f configure.okay ]; then echo -e "\n you have to run './configure' first.\n"; exit 1; fi;

whitepages: prep_wp $(NOS)

prep_wp:
	rm -f config.o files.o mailbox.o mboxfile.o mboxmail.o merge.o smtpserv.o sort.o version.o wpages.o

#
# 06Jul2016, Maiko (VE4KLM), check config.h for B2F, that will determine if
# we need to link in the crypto and crypt libraries or not. I'm not a master
# at doing Makefile scripting (this took me forever to figure out, sigh).
#
# 07Oct2019, Maiko, Doing this a bit different, now using new ./configure
# to generate a marker file to say if we are using B2F or not, works !
#

ifneq ("$(wildcard ./configure.wlsl)","")
 B2FLIBS=-lcrypto
else
 B2FLIBS=
endif

CLIENTS= telnet.o ftpcli.o finger.o smtpcli.o hop.o \
        tip.o nntpcli.o dialer.o rlogin.o callcli.o \
        mailcli.o pop2cli.o pop3cli.o rdate.o look.o

SERVERS= ttylink.o ftpserv.o smisc.o smtpserv.o convers.o \
	nntpserv.o fingerd.o mboxcmd.o mailbox.o mboxfile.o \
	mboxmail.o mboxgate.o mailfor.o  bmutil.o forward.o \
	tipmail.o mailutil.o index.o expire.o calldbd.o \
	buckbook.o pop2serv.o pop3serv.o timed.o b2f.o \
	qrz.o fbbfwd.o lzhuf.o term.o tcpgate.o http.o \
	wpages.o merge.o sort.o ufgets.o tnlink.o

HFDD = hfddrtns.o hfdd.o winmor.o ptcpro.o kam.o hfddinit.o pk232.o hfddq.o vara.o

BOOTP=	bootp.o bootpd.o bootpcmd.o bootpdip.o

INTERNET= tcpcmd.o tcpuser.o tcptimer.o tcpout.o tcpin.o \
	tcpsubr.o tcphdr.o udpcmd.o udp.o udphdr.o \
	domain.o domhdr.o ripcmd.o rip.o \
	ipcmd.o ip.o iproute.o iphdr.o rtdyngw.o \
	icmpcmd.o icmp.o icmpmsg.o icmphdr.o \
	arpcmd.o arp.o arphdr.o rarp.o \
	netuser.o rspf.o rspfcmd.o rspfhdr.o \
	bbs10000.o tcpwatch.o

AX25=   ax25cmd.o ax25user.o ax25.o axheard.o ax25aar.o \
	lapbtime.o lapb.o kiss.o kisspoll.o ax25subr.o ax25hdr.o \
	ax25mail.o axui.o ax25xdigi.o fldigi.o axhsort.o

APRS=	aprs.o aprssrv.o aprsstat.o aprsmice.o \
	aprsdupe.o aprsbc.o aprsdigi.o aprsmsg.o \
	aprsmsgr.o aprsmsgdb.o aprscfg.o aprswx.o \
	aprsposdb.o vhttp.o

NETROM=	nrcmd.o nr4user.o nr4timer.o nr4.o nr4subr.o nr4hdr.o \
	nr3.o nrs.o nrhdr.o nr4mail.o
#
# 01Sep2011, Maiko, Take out the old (2005) modules
# INP3 = inp3.o inp3rtns.o nrr.o l3rtt.o newinp.o
#
INP = nrr.o newinp.o

PPP=	asy.o ppp.o pppcmd.o pppfsm.o ppplcp.o \
	ppppap.o pppipcp.o pppdump.o \
	slhc.o slhcdump.o slip.o

NET=	ftpsubr.o sockcmd.o sockuser.o socket.o sockutil.o  \
	iface.o timer.o cmdparse.o mbuf.o misc.o pathname.o files.o  \
	kernel.o ksubr.o getopt.o wildmat.o lzw.o devparam.o md5.o \
	tun.o callval.o baycom.o multipsk.o snmpd.o agwpe.o winrpr.o \
	wlauth.o j2pwmgr/j2pwrtns.o

DUMP= 	trace.o rspfdump.o kissdump.o ax25dump.o arpdump.o nrdump.o \
	ipdump.o icmpdump.o udpdump.o tcpdump.o ripdump.o

# at and xmodem, at least, don't really belong here....
UNIX=	unix.o unixasy.o dirutil.o at.o lcsum.o xmodem.o glob.o \
	dumbcons.o editor.o jlocks.o qsort.o chkstk.o sysevent.o \
	j2tmpnam.o getusage.o j2KLMlists.o j2strings.o j2base36.o \
	activeBID.o
#
# 26Oct2009, Maiko, Separate compile instructions for modules that
# need to include curses.h header file. Should not have to parse the
# curses include directory for every single JNOS module, only these.
#

J2CURSES=  curses.o rawcons.o sessmgr.o ttydriv.o

curses.o: curses.c
	$(CC) -c $(CFLAGS) $(ICURSES) -DLCURSES=\"$(LCURSES)\" $< -o $@
rawcons.o: rawcons.c
	$(CC) -c $(CFLAGS) $(ICURSES) $< -o $@
sessmgr.o: sessmgr.c
	$(CC) -c $(CFLAGS) $(ICURSES) $< -o $@
ttydriv.o: ttydriv.c
	$(CC) -c $(CFLAGS) $(ICURSES) $< -o $@

$(NOS): main.o config.o version.o session.o jheard.o clients.a servers.a \
	internet.a net.a netrom.a ax25.a aprs.a unix.a dump.a ppp.a bootp.a \
	hfdd.a inp.a j2curses.a
	$(CC) $(CFLAGS) -o $(NOS) main.o config.o version.o session.o \
	jheard.o clients.a servers.a hfdd.a net.a internet.a net.a netrom.a \
	unix.a ax25.a aprs.a dump.a ppp.a bootp.a inp.a j2curses.a \
	$(LCURSES) $(DBGLIB) $(B2FLIBS)

#       
# net.a is specified twice, above, due to some mutual dependencies with
# internet.a and this is one way to deal with the single-pass library
# search argorithm.

#
# 02Nov2015, Maiko (VE4KLM), new password manager for JNOS 2.0 project
#
jnospwmgr: j2pwmgr/j2pwmgr.[co] j2pwmgr/j2pwrtns.[co]
	cc -DNOJ2STRLWR j2pwmgr/j2pwmgr.c j2pwmgr/j2pwrtns.c -o jnospwmgr
#
# mail2ind produces a utility to look for bids, dump indices, salvage
# sequence.seq, rebuild indices, etc.
#
mail2ind:	mail2ind.o index.o glob.o wildmat.o misc.o
	$(CC) $(CFLAGS) -DMAIL2IND mail2ind.o index.c glob.o misc.o wildmat.o -o mail2ind
	rm -f index.o

# dumpdate  produces a program which displays longint dates in several files
dumpdate:	 dumpdate.o
	$(CC) $(CFLAGS) dumpdate.o -o dumpdate

# epass  produces a program which converts a challenge and passwd to MD5 format
epass:	epass.c md5.o
	$(CC) $(CFLAGS) -DMD5AUTHENTICATE epass.c md5.o -o epass

# pushmail  produces the program that moves some of mqueue to another system
pushmail:	pushmail.o glob.o wildmat.o
	$(CC) $(CFLAGS) pushmail.o glob.o wildmat.o -o pushmail

# u2j	produces program to feed jnos email from Linux
u2j:	u2j.o
	$(CC) $(CFLAGS) u2j.o -o u2j
	echo "YOU MUST EDIT u2j.c TO SPECIFY WHERE JNOS mqueue IS LOCATED"

# scanjmsg	produces program to scan incoming bulletins for suitable content
scanjmsg:	scanjmsg.o getopt.o
	$(CC) $(CFLAGS) scanjmsg.o getopt.o -o scanjmsg
	echo "YOU MUST EDIT scanjmsg.c TO SPECIFY WHERE scanjmsg.dat IS LOCATED"

clean:
	$(RM) *.[oa]
	$(RM) j2pwmgr/*.[oa]
	$(RM) jnos jnospwmgr
	$(RM) configure.okay configure.wlsl makefile.okay

clients.a: $(CLIENTS)
	$(RM) clients.a
	$(AR) clients.a $(CLIENTS)

servers.a: $(SERVERS)
	$(RM) servers.a
	$(AR) servers.a $(SERVERS)

hfdd.a: $(HFDD)
	$(RM) hfdd.a
	$(AR) hfdd.a $(HFDD)

ppp.a: $(PPP)
	$(RM) ppp.a
	$(AR) ppp.a $(PPP)

bootp.a: $(BOOTP)
	$(RM) bootp.a
	$(AR) bootp.a $(BOOTP)

internet.a: $(INTERNET)
	$(RM) internet.a
	$(AR) internet.a $(INTERNET)

ax25.a: $(AX25)
	$(RM) ax25.a
	$(AR) ax25.a $(AX25)

aprs.a: $(APRS)
	$(RM) aprs.a
	$(AR) aprs.a $(APRS)

netrom.a: $(NETROM)
	$(RM) netrom.a
	$(AR) netrom.a $(NETROM)

inp.a: $(INP)
	$(RM) inp.a
	$(AR) inp.a $(INP)

net.a: $(NET)
	$(RM) net.a
	$(AR) net.a $(NET)

dump.a: $(DUMP)
	$(RM) dump.a
	$(AR) dump.a $(DUMP)

unix.a: $(UNIX)
	$(RM) unix.a
	$(AR) unix.a $(UNIX)

j2curses.a: $(J2CURSES)
	$(RM) j2curses.a
	$(AR) j2curses.a $(J2CURSES)

depend: unix.h
	makedepend -- $(CFLAGS) -- $(CLIENTS:o=c) $(SERVERS:o=c) \
	$(BOOTP:o=c) $(INTERNET:o=c) $(AX25:o=c) $(APRS:o=c) $(NETROM:o=c) \
	$(INP:o=c) $(PPP:o=c) $(NET:o=c) $(DUMP:o=c) $(UNIX:o=c) \
	$(J2CURSES:o=c) main.c version.c config.c jheard.c session.c u2j.c \
	dumpdate.c pushmail.c mail2ind.c epass.c

