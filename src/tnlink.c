/*
 * 30Jun2019, Maiko (VE4KLM) - new code to allow RMS express clients
 * to connect to a JNOS call and automatically get linked to CMS side,
 * the base of this code is essentially from the JNOS ttylink.c file.
 *
 * 30Jul2019, Initially I defined this as RMSCALL, since I was asked to
 * support telnet to CMS server via an AX25 jumpstart call - but you can
 * connect to any telnet server for that matter. My original tests were
 * to my own JNOS (localhost), so I decided to make this a more generic
 * module once I got it working, so now defined as TNCALL instead !
 */
#include <time.h>
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "telnet.h"
#include "usock.h"
#include "proc.h"
#include "tty.h"

#define	NEW_WAY  

#ifdef TNCALL

struct tnlinkparms {
	char *host;
	int cronly;
	int port;
};

struct tnlinkparms tnlinkparms;

void from_remote (int insock, void *outsock, void *tx)
{
	struct mbuf *bp, *cbp;
	struct proc **txproc;
	int c, serrno, osock;

	long osock_l;	/* 15Jan */

	osock_l = (long)outsock;
	osock = osock_l;
	txproc = (struct proc **)tx;

	log (osock, "from_remote starting up");

	while(1)
	{
#ifdef	NEW_WAY	
		if ((c = socklen (osock, 0)) > 0)
		{
#endif
#ifndef	NEW_WAY
		j2alarm (60000);
#endif
		if (recv_mbuf(osock,&bp,0,NULLCHAR,0) == -1) break;
#ifndef	NEW_WAY
 		j2alarm(0);
#endif
		// log (osock, "bytes read from remote");

/*
 * 03Aug2019, VE4KLM, I think I have to deal with Callsign: and Password:
 * inside of JNOS, the Winlink Express expects to see a WL2K sid, you can't
 * do this inside of the Winlink Express connection scripts unfortunately.
 */
		{
			char buffer[200];

			extern char *strcasestr (const char*, const char*);

			int len = len_p (bp);
			dup_p (&cbp, bp, 0, len);
			if (len > 198) len = 198;
			pullup (&cbp, buffer, len);
			buffer[len] = 0;	/* terminate string */
			// log (osock, "FR [%s]", buffer);
			free_p (cbp);

			if (strcasestr (buffer, "Callsign"))
			{
				char RMSTelCall[AXBUF], *mcptr;

				pax25 (RMSTelCall, Mycall);

				/* 02Nov2015, Maiko (VE4KLM), strip off any SSID */
				mcptr = RMSTelCall;
				while (*mcptr && *mcptr != '-') mcptr++;
				*mcptr=0;

				log (osock, "application login as [%s]", RMSTelCall);

				usprintf (osock, "%s\r", RMSTelCall);
				//usputc (osock,'\0');
				free_p (bp);
				usflush (osock);
				continue;
			}

			if (strcasestr (buffer, "Password"))
			{

				usprintf (osock, "CMSTelnet\r");
				//usputc (osock,'\0');
				free_p (bp);
				usflush (osock);
				continue;
			}
		}

		if(send_mbuf(insock,bp,0,NULLCHAR,0) == -1) break;

		// log (osock, "bytes sent to Ax25 side");

		usflush (insock);

#ifdef	NEW_WAY	
		} else if (c < 0) break;
#endif
		j2pause(60);
	}

	serrno = errno;

	log (osock, "from_remote terminated %d", serrno);

	*txproc = NULLPROC;
}

/* 30Dec2004, Replaces GOTO 'waitmsgtx' label */
static void waitmsgtx (int s)
{
   int index = 20;

    /* wait for I frame to be ACKed - K5JB/N5KNX */
    while (socklen (s,1) > 0 && index--)
        j2pause(500L);

    close_s(s);

    return;
}

#undef	SHOULD_USE_TEL_OUTPUT_CODE_HERE

/* This function handles all incoming "RMS Express" sessions, be they TCP,
 * NET/ROM or AX.25
 */
void tnlhandle (int s, void *t, void *p)
{
	struct sockaddr_in fsocket;

	int rsock, serrno;

	long rsock_l;	/* 15Jan */

	struct usock *up;

#ifndef	SHOULD_USE_TEL_OUTPUT_CODE_HERE
	struct mbuf *bp;
#endif
	struct proc *txproc;

	/* 01Aug2019, make this stuff configurable on the fly */
	extern char *tnlink_host;
	extern int tnlink_port;
	extern int tnlink_cronly;

	/*
	 * 25Jul2019, VE4KLM (Maiko), Always learning something with this
	 * stuff - all sockowner() does is mark the socket as belonging to
	 * a process, so that when the process dies, ALL sockets marked as
	 * belonging to that process will be struck down as well.
	 */	
	sockowner (s, Curproc);

	log (s, "incoming TN request");

	fsocket.sin_family = AF_INET;

	// fsocket.sin_addr.s_addr = resolve ("192.168.200.201");
	// fsocket.sin_port = 23;

	// fsocket.sin_addr.s_addr = resolve ("cms.winlink.org");
	// fsocket.sin_port = 8772;

	/* 01Aug2019, Now configurable in 'ax25 tncall' */
	fsocket.sin_addr.s_addr = resolve (tnlink_host);
	fsocket.sin_port = tnlink_port;

	if ((rsock = j2socket(AF_INET,SOCK_STREAM,0)) == -1)
	{
		log (s, "no socket for telnet");
		return;
	}

	if (tnlink_cronly)	/* cms winlink requires this */
	{
        	if ((up = itop (rsock)) == NULLUSOCK)
        	{
                	log (s, "no options for telnet");
                	return;
        	}

		strcpy (up->eol, "\r");
	}

	if (j2connect (rsock, (char*)&fsocket, SOCKSIZE) == -1)
	{
		log (s, "no connect for telnet");
		return;
	}

	log (s, "connected to remote system");

	rsock_l = rsock;

	txproc = newproc ("from_remote", 1024, from_remote,
				s, (void*)rsock_l, &txproc, 0);

	log (s, "from_ax25 loop starting up");

#ifdef	SHOULD_USE_TEL_OUTPUT_CODE_HERE

	while ((c = recvchar(s)) != EOF)
	{
		usputc (rsock,(char)c);
  
		if (c == '\r' /* && tn->eolmode */)
            usputc (rsock,'\0');
  
		usflush(rsock);
	}

#else

	while(1)
	{
		j2alarm (60000);
		if (recv_mbuf(s,&bp,0,NULLCHAR,0) == -1) break;
 		j2alarm(0);

		// log (s, "bytes read from ax25");

		if(send_mbuf(rsock,bp,0,NULLCHAR,0) == -1) break;

		// log (s, "bytes sent to remote");
	/*
	 * 30July2019, Maiko (VE4KLM), This is very important !!!
	 *
	 * Don't ask me why, but I fought for over 2 weeks to figure
	 * out why I had to hit ENTER twice to get a damn command to
	 * get recognized ! Noticed a little blurb in bmutil.c about
	 * this, then noticed this next line of code (tel_output.c).
	 *
	 * 10Aug2019, Maiko, This breaks the sending of messages from
	 * the packet side to the CMS, of course it will, but it seems
	 * it's not needed anymore, so what did I do wrong before ?
	 *  (able to receive from CMS and send to CMS now)
	 */
		// usputc(rsock,'\0');

		if (txproc == NULLPROC)
			break;

		usflush (rsock);
	}

#endif

	serrno = errno;

	log (s, "from_ax25 loop terminated %d", serrno);

	killproc (txproc);

	close_s (rsock);

	return (waitmsgtx (s));
}

/* A little wrapper for tcp connections to tnlink */
void tnlink_tcp (int s,void *t,void *p)
{
    tnlhandle(s,(void *)TELNET,p);
}
  
int tnlstart (int argc, char **argv, void *p)
{
    int16 port;
  
    if(argc < 2)
        port = IPPORT_TNLINK;
    else
        port = atoi(argv[1]);
  
    return start_tcp(port,"tnlink",tnlink_tcp,2048);
}
  
/* Shut down tnlink server */
int tnl0 (int argc, char **argv, void *p)
{
    int16 port;
  
    if(argc < 2)
        port = IPPORT_TNLINK;
    else
        port = atoi(argv[1]);

    return stop_tcp(port);
}

#endif /* TNCALL */
