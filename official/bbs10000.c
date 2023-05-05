
/*
 * November, 4, 2020 - Moving from GET to POST - putting the credentials in
 * the URL is just unacceptable, but you have to realize ; when I wrote this
 * stuff back in 2009, my knowledge of HTTP protocols was quite limited, and
 * still is in some ways. The protocol has evolved and it's alot to read.
 *
 * Web Based BBS Access for JNOS 2.0g and beyond ...
 *
 *   June 8, 2009 - started working in a play area on a work machine.
 *   July 3, 2009 - put into my official development source (prototype).
 *  July 21, 2009 - prototype pretty much ready, just need to add some form
 *                  of session control, so that multiple web clients can use
 *                  this all at the same time, without interfering with each
 *                  other's data and data flow.
 * February, 2010 - refining session control for multiple web clients at once.
 * April 24, 2010 - seems to be working nicely over last while - done.
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 */

#include "global.h"

#ifdef	HTTPVNC

#ifndef OPTIONAL
#define OPTIONAL
#endif

#include "ctype.h"
#include "cmdparse.h"
#include "commands.h"

#ifndef MSDOS
#include <time.h>
#endif

#include <sys/stat.h>
#include "mbuf.h"

#include "socket.h"

/* 01Feb2002, Maiko, DOS compile for JNOS will complain without this */
#ifdef  MSDOS
#include "internet.h"
#endif

#include "netuser.h"
#include "ip.h"
#include "files.h"
#include "session.h"
#include "iface.h"
#include "udp.h"
#include "mailbox.h"
#include "usock.h"
#include "mailutil.h"
#include "smtp.h"

#include "proc.h"

#include "bbs10000.h"	/* 03Jul2009, Maiko, New header file */

/*
 * 05Sep2022, Maiko (VE4KLM), use /jnos/spool/httpvnc.html file
 * to put in the starting <html>, <head> sections, with <style>,
 * <script> sections, use CSS, etc - for custom look and feel.
 */
#define	USE_CUSTOM_HTML_FILE

#define	MAXTPRINTF	(SOBUF - 5)

#define ISCMDLEN 256

/*
 * 08Jul2009, Maiko (VE4KLM), Now support multiple web clients coming
 * in at the same time. Need to write a few support functions for it.
 */

#define	MAXHVSSESS	20

static HTTPVNCSESS hvs[MAXHVSSESS];

static int maxhvs = 0;	/* start with zero clients */

static int hvsdebug = 0;

static int showhvs ()
{
	HTTPVNCSESS *ptr;

	int cnt;

	tprintf ("web based mailbox/vnc session list\n");
	tprintf ("I V R  Ipaddr         Callsign\n");

	for (cnt = 0; cnt < maxhvs; cnt++)
	{
		ptr = &hvs[cnt];

		tprintf ("%d %d %d  %s  ", cnt, ptr->valid,
			ptr->reused, inet_ntoa (ptr->ipaddr));

		if (ptr->currcall)
			tprintf ("%s\n", ptr->currcall);
		else
			tprintf ("-\n");
	}

	return 0;
}

/*
 * 25Nov2021, Maiko (VE4KLM), Adding original IP address for mod_proxy mode,
 * and if we really want to support multiple calls from the same IP address,
 * cause you never know, you might have a club or group of calls operating
 * from the same operations site with a firewall router in front, then we
 * should add a callsign as well, then do lookup on IP and callsign !
 *
 * But that is NOT so easy, so don't use multiple sessions from the
 * same IP address (internal or mod_proxy mode), working on it :(
 *
 */

static HTTPVNCSESS *getmyhvs (int s, char *orgipaddr)
{
	struct sockaddr_in fsocket;

	int cnt, len_i = sizeof(fsocket);

	int32 incoming_ipaddr;	/* 25Nov2021 */

	if (orgipaddr && *orgipaddr)
		incoming_ipaddr = aton (orgipaddr);

	else if (j2getpeername (s, (char*)&fsocket, &len_i) == -1)
	{
		log (s, "unable to get client information");
		return (HTTPVNCSESS*)0;
	}
	else incoming_ipaddr = fsocket.sin_addr.s_addr;

	for (cnt = 0; cnt < maxhvs; cnt++)
	{
		if (hvs[cnt].ipaddr == incoming_ipaddr /*(int32)fsocket.sin_addr.s_addr*/)
		{
			if (hvsdebug)
				log (s, "found client (%d) in session list", cnt);

			break;
		}
	}

	/* if we don't find a session, then Add and Initialize a new one */

	if (cnt == maxhvs)
	{
		int reclaim = 0;

		/* 23Feb2010, try to reclaim any sessions marked as not valid */
		while (reclaim < maxhvs)
		{
			if (!hvs[reclaim].valid)	/* 06Mar2010, Maiko, was == 2 */
			{
				if (hvsdebug)
					log (s, "reclaim (%d) in session list", reclaim);

				hvs[reclaim].reused++;	

				break;
			}

			reclaim++;
		}

		if (reclaim == maxhvs && maxhvs == MAXHVSSESS)
		{
			log (s, "max (%d) web clients reached", MAXHVSSESS);
			return (HTTPVNCSESS*)0;
		}

		if (reclaim < maxhvs)
			cnt = reclaim;
		else
		{
			cnt = maxhvs;
			hvs[cnt].reused = 0;	/* 06Mar2010, Maiko, Statistics */
			maxhvs++;
		}

		if (hvsdebug)
			log (s, "add and initialize new client (%d) to session list", cnt);

		hvs[cnt].ipaddr = incoming_ipaddr; /* fsocket.sin_addr.s_addr; */
		hvs[cnt].mbxs[0] = hvs[cnt].mbxs[1] = -1;
		hvs[cnt].escape_char = hvs[cnt].clear_char = 0;

		/* 05Nov2020, Maiko, New for aborting send message */
		hvs[cnt].abort_char = 0;

		/*
		 * 04Sep2022, Maiko, New to send message, although I wonder if we
		 * really need it, we already have /ex - maybe to make complete ?
		 */
		hvs[cnt].send_char = 0;

		hvs[cnt].currcall = (char*)0;
		hvs[cnt].mbxl = hvs[cnt].mbxr = 0;

		hvs[cnt].valid = 0;	/* 23Feb2010, for DOS attacks filling us up */

	}

	return &hvs[cnt];
}

/* 25Jun2009, Brand new private httpget and parse functions */
/* 08Jul2009, Maiko, New hextochar function for URI decoding */

char hextochar (char *ptr)
{
	char nib, val;
	int cnt = 2;

	while (cnt)
	{
		if (*ptr >= '0' && *ptr <= '9')
			nib = (*ptr - 0x30);
		else if (*ptr >= 'A' && *ptr <= 'F')
			nib = (*ptr - 0x41) + 10;
		else if (*ptr >= 'a' && *ptr <= 'f')
			nib = (*ptr - 0x61) + 10;
		else
		{
			log (-1, "invalid hex digit");
			nib = 0;
		}

		if (cnt == 2)
			val = nib * 16;
		else
			val += nib;
		cnt--;
		ptr++;
	}
/*
 * 04Nov2020, Maiko, Agggg, this locks up my xterm, I should know better
 *
	if (hvsdebug)
		log (-1, "hex value (%x) (%c)", (int)val, val);
 */
	return val;
}

static char* parse (char *hptr)
{
	char *ptr, *tptr;

	/* make a working copy of hptr, leave the original one intact */
	tptr = malloc (strlen(hptr));

	ptr = tptr;

	if (hvsdebug)
		log (-1, "parse [%s]", hptr);

	while (*hptr && *hptr != '&' && *hptr != ' ')
	{
		/* Map '+' characters to spaces */

		if (*hptr == '+')
			*ptr = ' ';

		/* Map special (encodings) characters to real characters */

		else if (*hptr == '%')
		{
			hptr++;
			*ptr = hextochar (hptr);	/* 08Jul09, Maiko, New function now */
			hptr++;
		}

		else
			*ptr = *hptr;

		hptr++;
		ptr++;
	}

	*ptr = 0;

	if (hvsdebug)
		log (-1, "parse [%s]", tptr);

	return tptr;
}

/*
 * 05Nov2020, Maiko (VE4KLM), User can now customize the beginning
 * section of the html presentation. Maybe they wish to add a logo
 * or some type of background image, or extra instructions ?
 *
 */

static char *starting_html_file = "httpvnc.html";

/*
 * 04Sep2022, Maiko, Really need to get this working. Added new argument to
 * the function to allow us to figure out how much more memory we will need
 * to allocate by including the contents of this customizable startup file.
 *
 * If you have not allocated ptr yet, then pass the value NULL ...
 */
static char *load_starting_html (char *ptr, int *scan4length)
{
	FILE *fp;

	char buf[100];

	sprintf (buf, "%s/%s", Spoolqdir, starting_html_file);

	if ((fp = fopen (buf, READ_TEXT)) == NULLFILE)
		return ptr;

	while (fgets (buf, 98, fp))
	{
		*scan4length += strlen (buf);

		if (ptr)
			ptr += sprintf (ptr, "%s", buf);
	}

	fclose (fp);

	return ptr;
}

#ifdef MBX_CALLCHECK
extern int callcheck (char*); /* 03Sep2010, Maiko, Need callsign validation*/
#endif

extern char *strcasestr (const char*, const char*);	/* 05Nov2020, Maiko */

/* 18Nov2021, Maiko (VE4KLM), Adding another parameter (orgipaddr) to accomodate mod_proxy support */

static int httpget (int s, HTTPVNCSESS *hvsptr, char **calldata, char **cmddata, char **passdata, char **hostdata, char **orgipaddr)
{
	int retval = 0, err, len;
	char important_stuff[ISCMDLEN+1];
	char *ptr;

	/* 04Nov2020, Maiko (VE4KLM), content length and flag telling us it's a form POST submission */
	int content_length = 0;
	int inpostmode = 0;

	int http_compression = 0;	/* 26Aug2021, Maiko (VE4KLM), http compression support for enabled clients */

	*calldata = *passdata = *cmddata = (char*)0;	/* very important */

	*hostdata = (char*)0;	/* 05Jul09, Maiko, Host from HTTP header */

	*orgipaddr = (char*)0;	/* 18Nov21, Maiko, Original IP address of client when doing mod_proxy */

	while (1)
	{
		j2alarm (10000);
   		err = recvline (s, important_stuff, ISCMDLEN);
		j2alarm (0);

		if (err == -1)
		{
			log (s, "hvs recvline errno %d", errno);
			retval = -1;
			break;
		}

		rip (important_stuff);

		len = strlen (important_stuff);

		if (hvsdebug == 2)
			log (s, "http (%d) [%s]", len, important_stuff);

		if (!len)
		{
			if (inpostmode)
			{
				/* log (s, "content length %d", content_length); */

				j2alarm (10000);
				ptr = important_stuff;
   				while (content_length--)
					*ptr++ = recvchar (s);
				*ptr = 0;
				j2alarm (0);

				/* log (s, "message body [%s]", important_stuff); */

			/*
			 * 30Aug2022, Maiko (VE4KLM), the escape (ctrl-t), clear, and the
			 * abort (ctrl-a) have been changed to input type submit, so now
			 * the user can just click on a button, instead of check boxing
			 * the operation followed by enter, that's the way it should be.
			 *
			 * They used to be "escape=on", "clear=on", and "abort=on"
			 *
			 */
				if (strstr (important_stuff, "escape=Ctrl"))
				{
					if (hvsdebug)
						log (s, "escape toggled");

					hvsptr->escape_char = 1;
				}
				else hvsptr->escape_char = 0;

				/* 06Jul09, Maiko, Ability to clear tmp file */
				if (strstr (important_stuff, "clear=Clear"))
				{
					if (hvsdebug)
						log (s, "clear toggled");

					hvsptr->clear_char = 1;
				}
				else hvsptr->clear_char = 0;

				/* 05Nov2020, Maiko (VE4KLM), Added CTRL-A to abort sending of messages */
				if (strstr (important_stuff, "abort=Ctrl"))
				{
					if (hvsdebug)
						log (s, "abort toggled");

					hvsptr->abort_char = 1;
				}
				else hvsptr->abort_char = 0;

				/* 04Sep2022, Maiko (VE4KLM), Added CTRL-Z to send messages */
				if (strstr (important_stuff, "send=Ctrl"))
				{
					if (hvsdebug)
						log (s, "send toggled");

					hvsptr->send_char = 1;
				}
				else hvsptr->send_char = 0;

				// log (s, "[%s]", important_stuff);

				if ((ptr = strstr (important_stuff, "call=")) != NULL)
					*calldata = parse (ptr + 5);

#ifdef MBX_CALLCHECK
				/*
				 * 03Sep2010, Maiko (VE4KLM), Callsign validation should have
				 * been done here long time ago. The potential for JUNK logins
				 * is very high on any web based system, I should know better.
				 *
				 * The callcheck functionality courtesy of K2MF (Barry)
				 */
				if (*calldata && !callcheck (*calldata))
				{
					logmbox (s, *calldata, "bad login");

					free (*calldata);	/* no point keeping it, free it up */

                	*calldata = (char*)0;	/* very important !!! */
				}
#endif
				if ((ptr = strstr (important_stuff, "pass=")) != NULL)
				{
					// *passdata = parse (ptr + 5);
				/*
				 * 09Aug2022, Maiko (VE4KLM), I have decided to replace the
				 * use of password with an authentication token. For now I
				 * will hardcode the token as 'Jst_<passwd in ftpusers>',
				 * till such time I have a method to properly do tokens.
				 *
				 * Nobody off the street will know this right off the bat,
				 * so that way I can present this to the community, it will
				 * be relatively secure, and buys me a bit of time to work
				 * on a properly implementation (also where to store it).
				 */
					if ((ptr = strstr (ptr, "Jst_")) != NULL)
						*passdata = parse (ptr + 4);
				/*
				 * To reiterate : if my password is 'jack' in ftpusers, then
				 * the Token to enter the httpvnc site is 'Jst_jack'. The Jst
				 * refers to JNOS Security Token - very creative [laughing]
				 */

				}

				if ((ptr = strstr (important_stuff, "cmd=")) != NULL)
					*cmddata = parse (ptr + 4);

				/* 31Aug2022, Maiko (VE4KLM), Added an Exit / Bye button */
				if (strstr (important_stuff, "exit=Exit"))
					*cmddata = j2strdup ("bye");

				/* if either is present, then treat it as a valid request */
				if (*calldata || *cmddata)
					retval = 1;

				inpostmode = 0;
			}

			break;
		}

		if ((ptr = strstr (important_stuff, "Host: ")) != NULL)
			*hostdata = parse (ptr + 6);

		/*
		 * 18Nov2021, Maiko (VE4KLM), For Apache mod_proxy (HTTPS <-> HTTP) I
		 * need the original IP address of the client, since our TCP (getmyhvs)
		 * uses the IP address, which in the case of a proxy setup will always
		 * result in the IP address of the linux side of the JNOS tun link :|
		 */
		if ((ptr = strstr (important_stuff, "X-Forwarded-For: ")) != NULL)
		{
			if (hvsdebug)
				log (s, "header [%s]", important_stuff);

			*orgipaddr = parse (ptr + 17);
		}
		/*
		 * Debugging, it does work, but need to think about how to use this,
		 * the problem is that the original IP is not convenient at all on
		 * the getmyhvs() call, in proxy mode it will always look like it
		 * is coming from the localhost (linux side of tun interface). So
		 * how the heck am I going to avoid session conflicts on Proxy ?
		 *
		if (*orgipaddr)
			log (-1, "actual client ip [%s]", *orgipaddr);
		 *
		 */

		/*
	`	 * 26Aug2021, Maiko (VE4KLM), HTTP compression for clients supporting it
		 *
		 * It's not HTTPS, but at least we can obscure data from plain sight ...
		 *  (this is not encryption, we're not 'breaking any rules' in my opinion)
		 *
		 * Hold on a second Mikey, HTTP compression is from server to client only,
		 * the purpose apparently is to simply reduce bandwidth, faster loading,
		 * which is good of course, but it still doesn't address how to obscure
		 * the stuff we get from the client, so I need to find a way to do it.
		 *
		 * I have no desire to write HTTPS code, not happening for this project :|
		 *
		 * So I am probably shit out of luck, unless I go the challenge/response
		 * route, and only ask for a callsign at login, then have JNOS in some way
		 * get a password or response code back to the user, then login them in ?
		 *
		 * 09Aug2022, Maiko (VE4KLM), Mute point, since we can now use apache
		 * webserver with the mod proxy module to give full HTTPS protection.
		 */

		http_compression = 0;

		if ((ptr = strstr (important_stuff, "Accept-Encoding: ")) != NULL)
		{
			// log (s, "client compression check [%s]", ptr + 17);

			if (strstr (ptr + 17, "gzip") != NULL)
				http_compression = 1;
		}

		if (http_compression && hvsdebug)
			log (s, "client supports gzip http compression");

		/*
		 * 05Nov2020, Maiko (VE4KLM), Are you kidding me ???
		 *
		 * Lynx uses 'Content-length'
		 * Mozilla uses 'Content-Length'
		 *
		 * notice the subtle difference, damn it !
		 *  (use strcasestr, not strstr as I had in the original code)
		 */
		if ((ptr = strcasestr (important_stuff, "Content-length: ")) != NULL)
			content_length = atoi (parse (ptr + 16));

		/*
		 * 04Nov2020, Maiko (VE4KLM), 11 years (wow) later ! Switchto POST
		 * for FORM submissions, I should never have used GET for this.
		 */
		if (!memcmp (important_stuff, "POST / ", 7))
		{
			/* log (s, "got a POST, set flag to loop for body content, read in values"); */

			inpostmode = 1;
		}
		/*
		 * 05Nov2020, Maiko (VE4KLM), the favicon requests are intense, and
		 * use up precision bandwidth in my opinion, why does mozilla insist
		 * on sending 6 of them one after the other, unreal, they're ANNOYING
		 * as hell so deal with it inside this function, this is the quickest
		 * way according to 'https://tools.ietf.org/html/rfc2616#section-6'.
		 */
		else if (!memcmp (important_stuff, "GET /favicon", 12))
		{
			/* confirmed working, not seeing ANY more of these requests */

			tprintf ("HTTP/1.1 404 \r\n\r\n");

			/* just stay in the loop and wait for another legit request :) */
		}

		else if (!memcmp (important_stuff, "GET / ", 6))
			retval = 1;
	}

	return retval;
}

static void socketpairsend (int usock, char *buffer, int len)
{
	char *tptr = buffer;

	int cnt = len;

	while (cnt > 0)
	{
		usputc (usock, *tptr);
		tptr++;
		cnt--;
	}

	usflush (usock);
}

static void launch_mbox (HTTPVNCSESS *hvsptr, char *callsign, char *passwd)
{

	if (hvsptr->mbxl)
	{
		if (hvsdebug)
			log (-1, "mailbox already launched");

		return;
	}

	if (j2socketpair (AF_LOCAL, SOCK_STREAM, 0, hvsptr->mbxs) == -1)
	{
		log (-1, "socketpair failed, errno %d", errno);
		return;
	}

	seteol (hvsptr->mbxs[0], "\n");
	seteol (hvsptr->mbxs[1], "\n");

	sockmode (hvsptr->mbxs[1], SOCK_ASCII);

	strlwr (callsign);	/* 15Jul2009, Maiko, Keep it lower case please ! */

	hvsptr->passinfo.name = j2strdup (callsign);
	hvsptr->passinfo.pass = j2strdup (passwd);

	hvsptr->currcall = hvsptr->passinfo.name;

	newproc ("WEB2MBX", 8192, mbx_incom, hvsptr->mbxs[1],
		(void*)WEB_LINK, (void*)&hvsptr->passinfo, 0);

	hvsptr->mbxl = 1;
}

/*
 * 15Jul2009, Maiko, Put this code into it's own function, which is used
 * to 'time stamp/welcome' a user when they first start a session, as well
 * as provide a 'time stamp/notice' after they clear their session file.
 */
void markermsg (int required, HTTPVNCSESS *hvsptr)
{
	char intro[80], *cp, *ptr = intro;
	time_t t;
	time(&t);
	cp = ctime(&t);
#ifdef UNIX
	if (*(cp+8) == ' ')
		*(cp+8) = '0';  /* 04 Feb, not b4 Feb */
#endif

	ptr += sprintf (ptr, "\n*** %2.2s%3.3s%2.2s %8.8s",
			cp+8, cp+4, cp+22, cp+11);

	switch (required)
	{
		case 0:
			ptr += sprintf (ptr, " New session, welcome [%s]",
						hvsptr->currcall);
			break;

		case 1:
			ptr += sprintf (ptr, " Session file has been cleared");
			break;

		default:
			break;
	}

	ptr += sprintf (ptr, " ***\n\n");

	fputs (intro, hvsptr->fpsf);

	fflush (hvsptr->fpsf);	/* 15Jul2009, Maiko, Flush it ! */
}

/*
 * 09Jul2009, Maiko, process stuff from mailbox, write to session file,
 * which I hope works alot better than the KLUDGE I have been using to
 * date (ie, the sockfopen). This code taken from my HFDD stuff ...
 *
 */

void mailbox_to_file (int dev, void *n1, void *n2)
{
	char outdata[300], *ptr = outdata;
	int c, len;

	HTTPVNCSESS *hvsptr = (HTTPVNCSESS*)n1;

	if ((hvsptr->fpsf = fopen (hvsptr->tfn, APPEND_TEXT)) == NULL)
	{
		log (-1, "fopen failed, errno %d", errno);
        return;
    }

	if (hvsdebug)
		log (-1, "m2f - socket %d", hvsptr->mbxs[0]);

	sockowner (hvsptr->mbxs[0], Curproc);

	sockmode (hvsptr->mbxs[0], SOCK_ASCII);

	markermsg (0, hvsptr);

	while (1)
	{
		/*
		 * The whole idea here is that if we don't get anything from
		 * the keyboard or our forwarding mailbox after 2 seconds, I
		 * think it's safe to assume there is nothing more to come.
		 *
		 * 05Nov2020, Maiko - Trying 1 second now, this is the only
		 * part of the design that might be finicky ? I'm not sure.
		 */

		j2alarm (1000);
    	c = recvchar (hvsptr->mbxs[0]);
		j2alarm (0);

		/*
		 * An EOF tells us that we did not receive anything from
		 * the mailbox for 2 seconds (in this case) or it tells us
		 * that we need to terminate this process !
		 */
		if (c == EOF)
		{
			/*
			 * 10Feb2007, Maiko, Actually if the following happens,
			 * then very likely a mailbox or session died, in which
			 * case (now that we want this to not die), we need to
			 * do cleanup - .....
			 */
			if (errno)
			{
				if (errno != EALARM)
				{
					/* 05Nov2020, Maiko, Errno 107 is typical on mailbox shutdown, don't show it */
					if (errno != 107)
						log (-1, "errno %d reading from mbox", errno);

					break;
				}
			}
		}

 		/* Do not put the EOF into the outgoing buffer */
		else
		{
			*ptr++ = c & 0xff;
		}

		/* Calculate the length of data in the outgoing buffer */
		len = (int)(ptr - outdata);

        if (len > 250 || (len && c == EOF))
		{
			outdata[len] = 0;	/* terminate string !!! */

			fputs (outdata, hvsptr->fpsf);
			fflush (hvsptr->fpsf);

			ptr = outdata;	/* make sure we start from beginning again */

			continue;
		}
	}

	/* 05Nov2020, Maiko, Errno 107 is typical on mailbox shutdown, don't show it */
	if (errno != 107)
		log (-1, "left mailbox_to_file, errno %d", errno);
}

void serv10000 (int s, void *unused OPTIONAL, void *p OPTIONAL)
{
	/* 18Nov2021, Maiko, Added 'orgipaddr' to allow proper mod_proxy support */

	char *body, *ptr, *cmddata, *calldata, *passdata, *hostdata, *orgipaddr;
	int needed, len, resp, hvsptrnotset = 1;
	struct mbx *curmbx;
	HTTPVNCSESS hvsdummy, *hvsptr = &hvsdummy;	/* 25Nov2021, Maiko, so httpget() does not crash */
	FILE *fpco;

	/* 25Nov2021, Maiko, Need to set these to prevent flags from getting lost */
	hvsdummy.escape_char = hvsdummy.abort_char = hvsdummy.clear_char = -1;

	/* 04Sep2022, Maiko, New ctrl-z to send message */
	hvsdummy.send_char = -1;

	/* 25Aug2021, Maiko (VE4KLM), this can really fill up the log, take them out */
	if (hvsdebug)
		log (s, "hvs connect");

	close_s (Curproc->output);

	Curproc->output = s;

#ifdef	DONT_COMPILE
	/*
	 * 08Jul2009, Maiko, Get session state and data for this web client
	 *
	 * 25Nov2021, Maiko (VE4KLM), moving (and adding another argument to)
	 * the getmyhvs() call to after the httpget() call so we can support
	 * the mod_proxy mode with APACHE (https <-> http) ...
	 */
	if ((hvsptr = getmyhvs (s)) == (HTTPVNCSESS*)0)
	{
		/* 25Aug2021, Maiko (VE4KLM), this can really fill up the log, take them out */
		if (hvsdebug)
			log (s, "hvs disconnect");

		close_s (s);
		return;
	}
#endif
	while (1)
	{
		char cmd[80];

		resp = httpget (s, hvsptr, &calldata, &cmddata, &passdata, &hostdata, &orgipaddr);

		if (resp < 1)
			break;
		/*
		 * 25Nov2021, Maiko (VE4KLM), Moved and modifed getmyhvs() to here, see comment
		 * earlier in the code, and new flag hvsptrnotset (only call this once ever),
		 * this does work, BUT any hvsptr flags (clear, ctrl-a, ctrl-t) are lost since
		 * they get written to dummy structure, not the newly acquired hvsptr, so how
		 * to deal with that ? copy them back into the new hvsptr after getmyhvs().
		 */
		if (hvsptrnotset)
		{
			if ((hvsptr = getmyhvs (s, orgipaddr)) == (HTTPVNCSESS*)0)
				break;

			if (hvsdummy.escape_char != -1)
				hvsptr->escape_char = hvsdummy.escape_char;
			if (hvsdummy.abort_char != -1)
				hvsptr->abort_char = hvsdummy.abort_char;
			if (hvsdummy.clear_char != -1)
				hvsptr->clear_char = hvsdummy.clear_char;
			if (hvsdummy.send_char != -1)
				hvsptr->send_char = hvsdummy.send_char;

			hvsptrnotset = 0;	/* important one shot operation */
		}

		if (hvsdebug)
			log (-1, "hostdata [%s]", hostdata);

		if (calldata && *calldata && passdata && *passdata)
		{
			/*
			 * 23Feb2010, Maiko, If we get to this point, then I think it is
			 * safe to say we have a valid session with an actual user call
			 * present, so make sure this session is kept in the getmyhvs()
			 * cache. Prior to this, I cached ALL connects whether a valid
			 * callsign appeared or not, so any port attacks on this server
			 * would just fill up getmyhvs() cache, MAXHVSSESS would be hit,
			 * and no one would be able to use it, unless I restarted NOS.
			 */
			hvsptr->valid = 1;

			if (hvsdebug)
				log (-1, "call [%s] pass [%s]", calldata, passdata);

			launch_mbox (hvsptr, calldata, passdata);

			j2pause (1000);	/* give it time to come up */
		}

		if (cmddata && *cmddata)
		{
			if (hvsdebug)
				log (s, "command [%s]", cmddata);

			sprintf (cmd, "%s\n", cmddata);
		}
		/* 04Sep2022, Maiko, New button to send messages */
		else if (hvsptr->send_char)
		{
			if (hvsdebug)
				log (s, "send character");

			sprintf (cmd, "%c\n",'');
		}
		/* 05Nov2020, Maiko, New button to abort sending of messages */
		else if (hvsptr->abort_char)
		{
			if (hvsdebug)
				log (s, "abort character");

			sprintf (cmd, "%c\n",'');
		}
		else if (hvsptr->escape_char)
		{
			if (hvsdebug)
				log (s, "escape character");

			sprintf (cmd, "%c\n", '');
		}
		else if (hvsptr->clear_char)
		{
			if (hvsdebug)
				log (s, "clear session file");

			*cmd = 0;
		}
		else *cmd = 0;

		/* let's see if the mailbox is really active */
		/*
		 * 09Jul2009, Maiko, Look at the session value actually, since
		 * if the web client terminates with an active mailbox, multiple
		 * mailboxs are created under the same user, which is not good.
		 *
		if (calldata && *calldata)
		 */

		if (hvsptr->currcall)
		{
			if (hvsdebug)
				log (s, "looking for [%s]", hvsptr->currcall);

			for (curmbx=Mbox; curmbx; curmbx=curmbx->next)
				if (!stricmp (curmbx->name, hvsptr->currcall))
					break;

			if (!curmbx && hvsdebug)
				log (-1, "not found !!!");
		}
		else curmbx = (struct mbx*)0;

		if (!curmbx)
			hvsptr->mbxl = hvsptr->mbxr = 0;
		else
		{
			if (!hvsptr->mbxr)
			{
				/*
				 * 08Aug2022, Maiko (VE4KLM), Change tnf[20] to a pointer, so
				 * that we properly allocate space, and negate any risk of an
				 * overflow. The compiler was complaining to me anyways. Just
				 * do it 'right' and make sure to free it when we are done.
				    TODO
				 */

				/* 9 characters + null terminator + lenght of name */
				hvsptr->tfn = malloc (10 + strlen (curmbx->name)); 

				sprintf (hvsptr->tfn, "/tmp/%s.www", curmbx->name);

				if (hvsdebug)
					log (-1, "session file [%s] - start m2f process", hvsptr->tfn);

   				newproc ("m2f", 1024, mailbox_to_file, 0, (void*)hvsptr, (void*)0, 0);
				j2pause (1000);	/* give file a chance to finish */

				hvsptr->mbxr = 1;
			}

			/* 06Jul09, Maiko, This should work to clear file */
			else if (hvsptr->clear_char)
			{
				if (ftruncate (fileno (hvsptr->fpsf), 0))
					log (-1, "truncate session page, errno %d", errno);

				markermsg (1, hvsptr);	/* 15Jul2009, Maiko, Note the clear */
			}
		}

		if (*cmd)
		{
			if (hvsptr->fpsf)
			{
				fputs (cmd, hvsptr->fpsf);	/* should do this before send */

				fflush (hvsptr->fpsf);	/* 15Jul2009, Maiko, Flush it ! */
			}

			if (curmbx)
				socketpairsend (hvsptr->mbxs[0], cmd, strlen (cmd));

			j2pause (1000);	/* give mailbox a chance to display */
		}

	len = 0;

	if (hvsptr->mbxr)
	{
		/* Now read in the generated text file, find length */
		
   		if ((fpco = fopen (hvsptr->tfn, READ_TEXT)))
		{
   			fseek (fpco, 0, SEEK_END);
			len = ftell (fpco);
			rewind (fpco);

			/* leave file open for later - allocate memory first */

			if (hvsdebug)
				log (s, "ftell says %d bytes", len);
		}
	}

	/*
	 * 25Aug2021, Maiko (VE4KLM), Bump this up from 1300
	 * to 1500, added some formatting
	 */
	needed = len + 1500;	/* cmd response + standard form */

	if (hvsdebug)
		log (s, "Body needed %d bytes", needed);

#ifdef	USE_CUSTOM_HTML_FILE
	/* 04Sep2022, Maiko (VE4KLM), Try and get this working finally */
	ptr = load_starting_html (NULL, &needed);
#endif

	if (hvsdebug)
		log (s, "Body with Custom HTML needed %d bytes", needed);

	if ((body = malloc (needed)) == (char*)0)
	{
		log (s, "No memory available");
		break;
	}

	ptr = body;

#ifdef	USE_CUSTOM_HTML_FILE

	/*
	 * 05Nov2020, Maiko, Attempt at customization, so all of the now
	 * commented out code below will go into a supplied default file
	 * which the user is more then welcome to customize.
	 *
	 * 04Sep2022, Maiko, Finally got to making this work properly, it was
	 * never put into use 2 years ago, and was commented out. It would most
	 * certainly be nice to customize the page, even just to add CSS code.
	 */

	ptr = load_starting_html (ptr, &needed);

#else

	ptr += sprintf (ptr, "<html>");

	ptr += sprintf (ptr, "<head><script type=\"text/javascript\">\nfunction scrollElementToEnd (element) {\nif (typeof element.scrollTop != 'undefined' &&\ntypeof element.scrollHeight != 'undefined') {\nelement.scrollTop = element.scrollHeight;\n}\n}\n</script></head>");

	ptr += sprintf (ptr, "<body bgcolor=\"beige\"><br><center>");	/* 29Aug2021, Maiko, Center it so things appear nicer ? */

#endif

	/* 29Aug2021, Bob (VE3TOK) thinks command should go below the display */
	if (hvsptr->mbxr)
	{
		ptr += sprintf (ptr, "<div class=\"myScreen\"><form name=\"formName\"><textarea style=\"border: 1px solid black; padding: 10px;\" name=\"textAreaName\" readonly=\"readonly\" rows=24 cols=80>");

		if (fpco)
		{
			char inbuf[82];

			while (fgets (inbuf, 80, fpco))
				ptr += sprintf (ptr, "%s", inbuf);

			fclose (fpco);
		}

		/* 25Aug2021, Maiko (VE4KLM), the </form> should be in front of the </p> */
		ptr += sprintf (ptr, "</textarea><script>scrollElementToEnd(document.formName.textAreaName);</script></form></div>");
	}

	/*
	 * 05Jul09, Maiko, Now get hostname from HTTP header as we should
	 * 04Nov20, Maiko (VE4KLM), 11 years later, holy cow, switching to POST, should never have used GET
	 */
	
	ptr += sprintf (ptr, "<form name=\"mainName\" ");

#ifdef	APACHE_MOD_PROXY_BBS10000
	/* 19Nov2021, Maiko (VE4KLM), support use of mod_proxy to secure this page (important) */
	ptr += sprintf (ptr, "action=\"https://%s/jnosbbs\" ", hostdata);
#else
	ptr += sprintf (ptr, "action=\"http://%s\" ", hostdata);
#endif
	ptr += sprintf (ptr, "method=\"post\">");

	ptr += sprintf (ptr, "<table bgcolor=\"#aaffee\" style=\"border: 1px solid black;\" cellspacing=\"0\" cellpadding=\"10\"><tr>");

	/*
	 * 08Jul2009, Maiko (VE4KLM), Use different screens for login
	 * and active sessions already logged in - looks better :)
	 */
	if (hvsptr->mbxr)
	{
		ptr += sprintf (ptr, "<td><table bgcolor=\"lightblue\" style=\"border: 1px solid black;\" cellpadding=\"5\"><tr><td>%s</td><td><input type=\"hidden\" name=\"call\" size=\"6\" value=\"%s\"></td></tr></table></td>", hvsptr->currcall, hvsptr->currcall);

		/*
		 * 08Jul2009, Maiko, Make sure to put focus on CMD field !!!
		 *
		 * 25Aug2021, Maiko (VE4KLM), command field length should match display
		 * width, so put it in it's own row by itself - </tr><tr>, the "hidden"
	 	 * input above was not quite in the proper spot, was missing a </td>,
		 * and finally the cmd field needs to SPAN 2 columns <td colspan=2>
		 */

		ptr += sprintf (ptr, "<td><input type=\"submit\" name=\"enter\" value=\"Refresh\">&nbsp;<input type=\"submit\" name=\"exit\" value=\"Exit\">&nbsp;<input type=\"submit\" name=\"abort\" value=\"Ctrl-A\">&nbsp;<input type=\"submit\" name=\"escape\" value=\"Ctrl-T\">&nbsp;<input type=\"submit\" name=\"send\" value=\"Ctrl-Z\">&nbsp;<input type=\"submit\" name=\"clear\" value=\"Clear Session File\"></td></tr><tr><td colspan=2><input type=\"text\" name=\"cmd\" value=\"\" size=\"80\" maxlength=\"80\"></td></tr></table><script>document.mainName.cmd.focus();</script></form>");
	}
	else
	{
		ptr += sprintf (ptr, "<td>Callsign&nbsp;&nbsp;<input type=\"text\" style=\"text-align: center;\" name=\"call\" size=\"6\" value=\"\" required>&nbsp;&nbsp;Token&nbsp;&nbsp;<input type=\"password\" style=\"text-align: center;\" name=\"pass\" size=\"16\" value=\"\" required></td>");

		ptr += sprintf (ptr, "<td><input type=\"hidden\" name=\"cmd\" value=\"\"></td><td><input type=\"submit\" value=\"Enter\"></td></tr></table></form>");

		ptr += sprintf (ptr, "<h4>no active sessions - please login</h4>");

		ptr += sprintf (ptr, "<p>You will need a Token to access this site - contact me to get one ...</p>");

	}

	ptr += sprintf (ptr, "</center></body></html>\r\n");
	
#ifdef	DONT_COMPILE
	/*
	 * 05Nov2020, Maiko (VE4KLM), not sure we need this.
	 * does it slow it down, do other JNOS procs suffer
	 * if this is commented out ? does not seem so ...
	 */
	pwait (NULL);	/* give other processes a chance */
#endif
	len = ptr - body;

	if (hvsdebug)
 		log (s, "Body length %d", len);

	/* write the HEADER record */
	tprintf ("HTTP/1.1 200 OK\r\n");
	tprintf ("Content-Length: %d\r\n", len);
	tprintf ("Content-Type: text/html\r\n");

	/* 05Nov2020, Maiko (VE4KLM), Let people know it's 'recent' */
	tprintf ("Server: JNOS 2.0m HTTP VNC 1.0\r\n");

#ifdef	NOT_READY_TO_DO_THIS_YET
	/* 26Aug2021, Maiko (VE4KLM), for clients supporting HTTP compression */
	if (http_compression)
		tprintf ("Content-Encoding: gzip\r\n");
#endif
	/* use a BLANK line to separate the BODY from the HEADER */
	tprintf ("\r\n");

	/* 01Aug2001, VE4KLM, Arrggg !!! - the tprintf call uses vsprintf
	 * which has a maximum buf size of SOBUF, which itself can vary
	 * depending on whether CONVERSE or POPSERVERS are defined. If
	 * we exceed SOBUF, TNOS is forcefully restarted !!! Sooooo...,
	 * we will have to tprintf the body in chunks of SOBUF or less.
	 *
	tprintf ("%s", body);
	 */

	ptr = body;

	while (len > 0)
	{
#ifdef	DONT_COMPILE
		/*
		 * 05Nov2020, Maiko (VE4KLM), not sure we need this.
		 * does it slow it down, do other JNOS procs suffer
		 * if this is commented out ? does not seem so ...
		 */
		pwait (NULL);	/* give other processes a chance */
#endif

		if (len < MAXTPRINTF)
		{
			tprintf ("%s", ptr);
			len = 0;
		}
		else
		{
			tprintf ("%.*s", MAXTPRINTF, ptr);
			len -= MAXTPRINTF;
			ptr += MAXTPRINTF;
		}
	}

	free (body);

		break;	/* break out !!! */
	}

	/* 25Aug2021, Maiko (VE4KLM), this can really fill up the log, take them out */
	if (hvsdebug)
		log (s, "hvs disconnect");

	close_s (s);
}

/* Start up HTTP VNC server */
int httpvnc1 (int argc, char *argv[], void *p)
{
    int16 port;

    if(argc < 2)
       port = 10000;
    else
       port = atoi(argv[1]);

    return start_tcp (port, "HTTP VNC Server", serv10000, 8192 /* 4096 */);
}

/* Stop the HTTP VNC server */
int httpvnc0 (int argc, char *argv[], void *p)
{
    int16 port;

    if(argc < 2)
       port = 10000;
    else
       port = atoi(argv[1]);

    return stop_tcp(port);
}

/* 24Feb2010, Maiko, New function to show/manipulate the getmyhvs() table */

int httpvncS (int argc, char *arg[], void *p)
{
	return showhvs ();
}

#endif	/* end of HTTPVNC */
