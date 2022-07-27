/*
 * The code in this file was adapted from forward.c by KF5MG, to support
 * FBB-style ax.25 forwarding.
 */

#include <ctype.h>
#include <time.h>
#include "global.h"
#ifdef FBBFWD
#include "bm.h"
#include "mailbox.h"
#include "mailutil.h"
#include "smtp.h"
#include "cmdparse.h"
#include "proc.h"
#include "socket.h"
#include "timer.h"
#include "usock.h"
#include "netuser.h"
#include "ax25.h"
#include "netrom.h"
#include "nr4.h"
#include "files.h"
#include "index.h"
#ifdef FBBCMP
#include "lzhuf.h"
#include "mailfor.h"
#endif /* FBBCMP */
#ifdef UNIX
#include "unix.h"
/* n5knx: Avoid stdout so line endings are handled right */

/* 03Dec2020, removed HARGROVE_VE4KLM_FOX_USE_BASE36 from makefile, permanent ! */
/* 30Aug2020, Maiko (VE4KLM), Seriously time to extend BID limitation !!! */
#include "j2base36.h"

#include "activeBID.h"	/* added 23Oct2020 */

#define PRINTF tcmdprintf
#define PUTS(x) tcmdprintf("%s%c",x,'\n')
#define PUTCHAR(x) tcmdprintf("%c",x)
#else
#define PRINTF printf
#define PUTS puts
#define PUTCHAR putchar
#endif

/* By setting the fp to NULL, we can check in exitbbs()
 * whether a tempfile has been closed or not - WG7J
 */
#define MYFCLOSE(x) { fclose(x); x = (FILE *) 0; }
#define CTLZ    26              /* EOF for CP/M systems */

extern int MbForwarded;

/*
     *** Protocole Error (x) messages.
     0: Subject Packet does not start with a SOH (01) Byte.
     1: Checksum of message is wrong.
     2: Message could not be uncompressed.
     3: Received binary frame is not STX (02) or EOT (04).
     4: Checksum of proposals is wrong.
     5: Answer to proposals must start with "F" or "**".
     6: Answer to proposals must be "FS".
     7: More than 5 answers (with "+", "-" or "=") to proposals.
     8: Answer to proposal is not "+", "-" or "=".
     9: The number of answers does not match the number of proposals.
    10: More than 5 proposals have been received.
    11: The number of fields in a proposal is wrong (6 fields).
    12: Protocol command must be "FA", "FB", "F>", "FF" or "FQ".
    13: Protocol line starting with a letter which is not "F" or "*".
*/

#define FBBerror(x,y) { tprintf("*** Protocol Error (%d)\n", x); log(y,"fbbfwd: detected FBB protocol error %d", x); }

#ifdef FBBVERBOSELOG
static char RmtDisconnect[] = "FBBFWD: remote disconnected early.";
#endif

#ifdef FBBCMP

/*
 * 17Mar2007, Maiko, Direct email from Airmail clients.
 * 31Mar2008, Maiko, Note the AIRMAILGW code is not used in B2F, that
 * processing will be done in the dofbbacceptnote() function call.
 */
#define	AIRMAILGW

static int dofbbacceptnote  __ARGS((struct mbx *, char *));
#ifdef B2F
static int doB2Facceptnote  __ARGS((struct mbx *, char *));
#endif
static int fbbsendmsg __ARGS((struct fwd *, int, FILE *));

extern char *Mbhaddress;
extern char *Mbfwdinfo;
extern char *Mbqth;
extern char *Mbzip;
extern int Mbsmtptoo;
extern int Mbheader;
extern char shortversion[];

/* 21Dec2004, Maiko, Replaces the GOTO early_quit label */
static int do_early_quit (struct mbx *m, long start,
		int result, FILE *tfile, int msgn)
{
   if (m->mfile == NULLFILE || ferror(m->mfile) || feof(m->mfile) ||
        (ftell(m->mfile)-start < m->mbox[msgn].size))
	{
#ifdef JPDEBUG
        log(m->user,"fbbsendmsg: truncated %s msg %d would result from err %d",m->area,msgn,errno);
#endif
        fputs("\r\n*** Cannot find complete message body!\r\n", tfile);
#if defined(MAILERROR) && defined(JPDEBUG)
        mail_error("FBBsendmsg: %s: Cannot find body for %s msg %d", \
            m->name,m->area,msgn);
#endif
/* OK, we were unable to send a complete msg body.  We rely on returning an
   error code to result in a disconnect rather than a completed msg.
*/
//        m->user = -1;  /* this will fail subsequent recvline(m->user...) calls */
        result = -1;   /* this will prevent a /ex or ^Z from being sent */
   }

   fclose(m->mfile);
   m->mfile = NULL;

   // close the data file.
   fclose(tfile);
   return result;
}

#ifdef B2F

#define	TEST_B2F_BULLETINS  /* PERMANENT on 15Dec2017, MAIKO (VE4KLM) */

/*
 * 18Apr2008, Maiko (VE4KLM), Since B2F needs all the headers in the
 * payload (and not the proposal like the usual FBB stuff does, we'll
 * have to have a new function to use to write these headers to the
 * temporary file. For B2F forwards, this would be called instead of
 * the usual 'fbbsendmsg' function normally used in FBB forwarding.
 * 28Apr2010, Maiko, Now needed in forward.c, so remove 'static'.
 */
int B2Fsendmsg (struct fwd *f, int msgn, FILE *tfile)
{
	char buf[LINELEN], tmp[AXBUF], *mybbscallp;
	int  cnt, result  = 0, bodysize = 0;
	long bodystart = 0L, start = 0L;

	struct mbx *m = f->m;

	/* Open the mailbox file */
	sprintf (buf, "%s/%s.txt", Mailspool, m->area);
	if ((m->mfile = fopen (buf,READ_BINARY)) == NULLFILE)
		return (do_early_quit (m, start, result, tfile, msgn));

	/* point to start of this message in file */
	start = m->mbox[msgn].start;
	fseek (m->mfile, start, SEEK_SET);
	if (ferror(m->mfile))
		return (do_early_quit (m, start, result, tfile, msgn));

	/* 15Aug2010, Maiko, This can be no longer than 12 characters */
	fprintf (tfile, "MID: %.12s\r\n", f->bid);

	/* Get the SMTP headers that we want to write for the B2F EM header */
	while (bgets (buf, sizeof (buf), m->mfile) != NULLCHAR && *buf)
	{
		/* log (m->user, "B2Fsendmsg [%s]", buf); */

		if (!strnicmp (buf, "From:", 5) || !strnicmp (buf, "To:", 3))
		{
#ifdef	TEST_B2F_BULLETINS  /* PERMANENT on 15Dec2017, MAIKO (VE4KLM) */

	/*
	 * 04Dec2017, this code was put in a long time ago, but was never put
	 * into use. This GUARANTEES delivery of the message to the Winlink CMS
	 * system, since all winlink wants is a callsign with no extra @baggage
	 * behind it. Any user on JNOS can send to ANYONE at winlink by simply
	 * using 'sp <anycall>@wl2k' at the BBS prompt, no rewrite file rules
	 * required, very simple, very easy, and opens things up nicely.
	 *
	 * 21Dec2017, Completely forgout about 'smtp:' prefixing !
	 */
			if (!strstr (buf, "smtp:"))
			{
				char *ptr;

				/* 15Aug2010, Maiko, Only send the callsign */
				for (ptr = buf; *ptr && *ptr != '@'; ptr++)
					fprintf (tfile, "%c", *ptr);

				fprintf (tfile, "\r\n");
			}
			else fprintf (tfile, "%s\r\n", buf);
#endif
		}

		if (!strnicmp (buf, "Date:", 5))
		{
			/* 15Aug2010, Maiko, Send the date using their format !!! */
			long msgtime = mydate (buf + 6);

			struct tm *msgtm = gmtime (&msgtime);	/* UTC time ! */

			fprintf (tfile, "Date: %04d/%02d/%02d %02d:%02d\r\n",
				msgtm->tm_year+1900, msgtm->tm_mon+1, msgtm->tm_mday,
					msgtm->tm_hour, msgtm->tm_min);
/*
			fprintf (tfile, "Date: 2010/08/16 00:07\r\n");
*/
		}
	}

	fprintf (tfile, "Subject: %s\r\n", f->ind.subject);

	// fprintf (tfile, "Type: Private\r\n");

	if ((mybbscallp = pax25 (tmp, Bbscall)))
		fprintf (tfile, "Mbo: %s\r\n", mybbscallp);

	/*
	 * Now copy the body portion of the message
	 *
	 * 06Dec08, Maiko, Oops forgot strncmp, explains bleedover problems
	 * 12May2010, Maiko, Do this properly, pass through twice, the first
	 * time is to calculate the body size which we have to pass ...
	 */

	for (bodysize = 0, cnt = 0; cnt < 2; cnt++)
	{
		if (cnt)
		{
			fprintf (tfile, "Body: %d\r\n", bodysize);

			/* separate body from the main headers */
			fputs ("\r\n",tfile);

			fseek (m->mfile, bodystart, SEEK_SET);
		}
		else	
			bodystart = ftell (m->mfile);

		while (bgets (buf, sizeof (buf), m->mfile) != NULLCHAR &&
				strncmp (buf, "From ", 5))
		{
			if (cnt)
			{
				fputs (buf, tfile);
				fputs ("\r\n",tfile);
			}
			else
			{
				bodysize += strlen (buf);
				bodysize += 2;	/* include the newline and carriage return */
			}
		}
	}

	if (feof(m->mfile)) clearerr(m->mfile);  /* only place EOF is acceptable */

	/* replaces the GOTO early_quit label */
	return (do_early_quit (m, start, result, tfile, msgn));
}
#endif

#ifdef	DONT_COMPILE	/* now in it's own source file */

/*
 * 21Oct2020, Maiko (VE4KLM), We need to track ALL messages currently being
 * forwarded by all forwarding sessions (threads) and make sure multiple remote
 * systems are NOT forwarding the same message (with same BID) to us at the same
 * time. As demonstrated, this can lead to our system accepting all of them, in
 * turn creating duplicate bulletins which then get forwarded to others, which
 * is just not a good thing :|
 */

struct actbidlist {

	char *system;
	char *msgbid;

	struct actbidlist *next;
};

struct actbidlist *actbidlist = (struct actbidlist*)0;

int delactivebid (int s, char *msgbid)
{
	struct actbidlist *prev, *ptr;

	for (prev = ptr = actbidlist; ptr; prev = ptr, ptr = ptr->next)
	{
		if (!strcmp (ptr->msgbid, msgbid))
			break;
	}

	if (ptr)
	{
#ifdef	BAD_PLACE_TO_PUT_IO_FUNCTION_CRASH
		log (s, "chk4activebid - done with [%s] from [%s]",
			ptr->msgbid, ptr->system);
#endif
		if (ptr == actbidlist)
			actbidlist = (struct actbidlist*)0;
		else
 			prev->next = ptr->next;

		/* free up allocation inside the structure first */

		free (ptr->system);
		free (ptr->msgbid);

		/* now release the structure itself */

		free (ptr);
	}
	else log (s, "something happened, contact maiko");

	log (s, "chk4activebid - done with [%s]", msgbid);

	return 1;
}

int chk4activebid (int s, char *system, char *msgbid)
{
	struct actbidlist *ptr = actbidlist;

	while (ptr)
	{
		/* found a message, so tell system NO WAY right now */

		if (!strcmp (ptr->msgbid, msgbid))
		{
			log (s, "chk4activebid - already processing [%s] from [%s]",
				ptr->msgbid, ptr->system);

			return 1;
		}

		ptr = ptr->next;
	}

	/* if nothing found, then add the current message */

	ptr = mallocw (sizeof (struct actbidlist));
	ptr->system = j2strdup (system);
	ptr->msgbid = j2strdup (msgbid);
	ptr->next = actbidlist;
	actbidlist = ptr;

#ifdef	DEBUG
	log (s, "chk4activebid - processing [%s] from [%s]",
		ptr->msgbid, ptr->system);
#endif
	return 0;
}

#endif	/* moved to it's own source file */

/* fbbsendmsg() is adapted from sendmsg() in forward.c.  It writes the
 * message to tfile just as we would send it using sendmsg().
 * N5KNX 2/95: if we can't send a complete msg, return code -1, otherwise
 * return code 0.
 */
static int fbbsendmsg (f, msgn, tfile)
     struct fwd *f;
     int    msgn;
     FILE   *tfile;                     // Data is returned in this
                                        // file handle. It is open
                                        // when we're called and we
                                        // close it when we leave.
{
   struct mbx *m = f->m;

   int  i       = 0;
   int  rheader = 0;
   int  result  = 0;
   long start = 0L;
   char buf[LINELEN];

   /* If the data part of the message starts with "R:" the RFC-822
    * headers will not be forwarded. Instead we will add an R:
    * line of our own.
    */
   if (Mbheader)
     {
        /* First send recv. date/time and bbs address */
        fprintf (tfile, "R:%s", mbxtime (f->ind.mydate));
        /* If exists, send H-address */
        if (Mbhaddress != NULLCHAR)
           fprintf (tfile, " @:%s", Mbhaddress);
        /* location, if any */
        if (Mbqth != NULLCHAR)
           fprintf (tfile, " [%s]", Mbqth);
        /* if there is info, put it next */
        if (Mbfwdinfo != NULLCHAR)
           fprintf (tfile, " %s", Mbfwdinfo);

#ifdef	DONT_COMPILE	/* 03Sep2020, Maiko, Gus is of the opinion this serves no purpose */

	/* 30Aug2020, Maiko (VE4KLM), Seriously time to extend BID limitation */
	b36bid = j2base36 (f->ind.msgid);

	//log (-1, "[%s] [%ld]", b36bid, f->ind.msgid);

        fprintf (tfile, " #:%s", b36bid);
	free (b36bid);

#endif	/* end of DONT_COMPILE */

        /* The BID, if any */
        if (f->bid[0] != '\0')
           fprintf (tfile, " $:%s", &f->bid[1]);
        /* zip code of the bbs */
        if (Mbzip != NULLCHAR)
           fprintf (tfile, " Z:%s", Mbzip);
//      fputc ('\n',tfile);
        fputs("\r\n", tfile);
     }

   /* Open the mailbox file */
   sprintf (buf, "%s/%s.txt", Mailspool, m->area);
   if ((m->mfile = fopen (buf,READ_BINARY)) == NULLFILE)
		return (do_early_quit (m, start, result, tfile, msgn));

#ifdef	AOFFSET_DEBUG
   log (m->user, "fbbsendmsg - Offset Start [%ld] msgn [%d]", m->mbox[msgn].start, msgn);
#endif

   /* point to start of this message in file */
   start = m->mbox[msgn].start;
   fseek (m->mfile, start, SEEK_SET);
   if (ferror(m->mfile))
		return (do_early_quit (m, start, result, tfile, msgn));

   /* If we also send the smtp headers, now see if the message
    * has any R: headers. If so, send them first.
    */
   if (Mbsmtptoo) {
      while (bgets (buf, sizeof (buf), m->mfile) != NULLCHAR) {
         if (!*buf)
            break;          /* End of smtp headers */
      }
      if(feof(m->mfile) || ferror(m->mfile))
		return (do_early_quit (m, start, result, tfile, msgn));

      /* Found start of msg text, check for R: lines */
      while (bgets (buf, sizeof (buf), m->mfile) != NULLCHAR && !strncmp (buf, "R:", 2)) {
         rheader = 1;
         fputs (buf,tfile);
//       fputc ('\n',tfile);
         fputs("\r\n", tfile);
      }
      /* again point to start of this message in file */
      fseek (m->mfile, start, SEEK_SET);
      if(ferror(m->mfile))
		return (do_early_quit (m, start, result, tfile, msgn));
   }

   /* Go past the SMTP headers to the data of the message.
    * Check if we need to forward the SMTP headers!
    * 920114 - WG7J
    */
   if (Mbsmtptoo && (rheader || Mbheader))
//    fputc ('\n',tfile);
      fputs("\r\n", tfile);
   i = NOHEADER;
   while (bgets (buf, sizeof (buf), m->mfile) != NULLCHAR && *buf) {

	// log (m->user, "[%s]", buf);

      if (Mbsmtptoo) {
         /* YES, forward SMTP headers TOO ! */
         switch (htype (buf, &i)) {
           case XFORWARD:   /* Do not forward the "X-Forwarded-To:" lines */
           case STATUS:     /* Don't forward the "Status:" line either */
           case BBSTYPE:
           case SUBJECT:
           case TO:
           case APPARTO:
           case CC:
           case DATE:
              break;
           case FROM:
              /* Don't forward the "From: " line either.
               * make it ">From: "
               */
              fputc ( '>', tfile);
              /*note fall-through */
           default:
              if (!strncmp (buf, "From ", 5))
                 fputc ('>',tfile);
              fputs (buf,tfile);
//            fputc ('\n',tfile);
              fputs("\r\n", tfile);
         }
      }
   }
   if(feof(m->mfile) || ferror(m->mfile))
		return (do_early_quit (m, start, result, tfile, msgn));

   /* Now we are at the start of message text.
    * the rest of the message is treated below.
    * Remember that R: lines have already been sent,
    * if we sent smtp headers !
    */
   i = 1;

   while (bgets (buf, sizeof (buf), m->mfile) != NULLCHAR && strncmp (buf, "From ", 5)) {
      if (i) {
           if (!strncmp (buf, "R:", 2)) {
              if (Mbsmtptoo)
                 continue;
           } else {
              i = 0;
              if (*buf) {
                 /* Ensure body is separated from R: line */
//               fputc ('\n',tfile);
                 fputs("\r\n", tfile);
              }
           }
      }
      fputs (buf,tfile);
      fputs ("\r\n",tfile);
   }

   if(feof(m->mfile)) clearerr(m->mfile);  /* only place EOF is acceptable */

	/* replaces the GOTO early_quit label */
	return (do_early_quit (m, start, result, tfile, msgn));
}
#endif /* FBBCMP */


/* FBB Fowarding comments:

   FBB Forwarding sends messages in groups instead of normal pbbs forwarding
   that sends everything. i.e. You send 5 messages, he sends 5 messages, you
   send 5 messages, he sends 5 messages, etc.

   In our case... we send up to 5 messages from a forwarding area and then
   let the other system send us messages. When we get to the end of the
   messages in an area, we may send less than 5 messages. When we're done
   with the area... the other system will send us messages and we'll switch
   to another message area and start sending messages again. So... he may
   send 5 messages each time, and we might send 1 message each time... it just
   depends on how many messages are in each area. Hope that makes sense.
*/

#define FBBMAXMSGS  5   /* Maximum number of messages to process. */

#ifdef EXPIRY
extern int Eproc;
int    FBBSendingCnt  = 0;   /* how many dofbbsend()'s in progress (see expire.c) */
#endif

#define fbbUNKNOWN 0
#define fbbNO      1
#define fbbYES     2
#define fbbDEFER   3

/* Convert/parse an FBB FA/FB/FC type message to a fbbpacket structure */

static int fbbparse (struct fbbpacket *msglst, char *fbline)
{
   char *cp, *atbbs, *to;

	/*
	 * Here's an example of 3 FBB message header packets in one shot.
	 *
		FB B KF5MG USA NOS 34535_KF5MG 298
		FB B KF5MG USA NOS 34537_KF5MG 303
		FB B KF5MG USA NOS 34541_KF5MG 309
	 *
	 * Here's an example of the new B2F message header packet.
	 *
		FC EM 1087_VE4PKT 124 112 0
	 *
	 * Data Types are now as follows :
	 *
		FA = compressed ascii msg
		FB = compressed binary file or uncompressed msg
		FC = winlink B2F (compressed and encapsulated)
				- added 11Apr2008, Maiko (VE4KLM)
	 *
	 */

   cp = strtok(fbline," ");
   strncpy(msglst->fbbcmd, cp, sizeof(msglst->fbbcmd));

   // Message type ( P, B, T, or E )
   cp = strtok('\0', " ");
   msglst->type = *cp;

#ifdef B2F
	/* 11Apr2008, Maiko, We only care about BID for FC proposals */
	if (stricmp (msglst->fbbcmd, "FC"))
	{
#endif

   // From id ( Userid only )
   cp = strtok('\0', " ");
   free(msglst->from);
   msglst->from = j2strdup(cp);

   // hostname or flood-area ( USA, WW, KF5MG.#DFW.TX.USA.NOAM, etc.)
   cp = strtok('\0', " ");
   atbbs = j2strdup(cp);

   // To: id ( 6 char max. )
   cp = strtok('\0', " ");
   to = j2strdup(cp);

#ifdef B2F
	}
#endif

   // Bid ( ax.25 PBBS message id )
   cp = strtok('\0', " ");
   free(msglst->messageid);
   msglst->messageid = j2strdup(cp);

#ifdef B2F
	/* 11Apr2008, Maiko, We only care about BID for FC proposals */
	if (!stricmp (msglst->fbbcmd, "FC"))
		return 1;
#endif

   // Size ( At present, we don't do anything with this. )
   cp = strtok('\0', " ");
   msglst->size = atoi(cp);

   // set msglst->to equal to the 'to id' '@' 'hostname'
   free(msglst->to);

   /* 22Dec2005, Maiko, changed malloc() to mallocw() instead ! */
   msglst->to = mallocw(strlen(to) + strlen(atbbs) + 2);

   strcpy(msglst->to, to);
#ifdef use_local	/* see makecl() in forward.c */
   if (stricmp(atbbs, "local")) {  /* N5KNX: drop @atbbs if == "local" */
#endif
      strcat(msglst->to, "@");
      strcat(msglst->to, atbbs);
#ifdef use_local
   }
#endif

   free(to);
   free(atbbs);

   return 1;
}

// This code processes a FS packet from a remote system.
// Format is: FS ccccc     where each c is a character:
// A '+' means the message is accepted.
//   '-' means the message is rejected.
//   '=' means the message is accepted but don't send it yet.
//       deferred messages (=) are a real pain to process. :(
//
// RC =  0 means an error was detected.
//       1 means an 'FF' was received from the remote system.
//       2 means an 'Fx' (where x is something) was received.

#define	MSG_ACCEPTED_FROM_OFFSET	/* 29Jan2015, Maiko (VE4KLM) */

#ifdef	MSG_ACCEPTED_FROM_OFFSET
	long glob_Aoffset;	/* 24Sep2020, Maiko, VE4KLM */
#endif

/*
 * 11Apr2008, Maiko (VE4KLM), This shows the age of the code. According to
 * the FBB spec at 'www.f6fbb.org' - Y, N, and L are also acceptable to use
 * with respect to accepting, rejecting, or deferring (later) a message. The
 * WL2K and Airmail stuff uses the latter values, so we need to add them !
 */
static int fbbdofs(struct fbbpacket *msglst,
            struct fwd *f,
            int    msgcnt,
            long   *mindeferred,
            long   *maxdeferred,
            int    idx)
{

   struct mbx *m;
   int i;
	char fsr;	/* 11Apr2008, Maiko */

#ifdef FBBCMP
   FILE *tfile;
   int  rc;
   int  NotConnected = FALSE;
#endif

#ifdef	MSG_ACCEPTED_FROM_OFFSET
	/* 29Jan2015, Maiko (VE4KLM), Now supporting accept from offset */
	char *fsrptr;
	long Aoffset;
#endif

   m      = f->m;

   // Send any data in our buffer.
   usflush(m->user);
   // Get the FS line.
  j2alarm(Mbtdiscinit*1000);
   if(recvline (m->user, m->line, MBXLINE) == -1 ) {
#ifdef FBBVERBOSELOG
       log(m->user, RmtDisconnect);
#endif
       return 0;
   }
   j2alarm(0);
   rip(m->line);

   // Make sure we got a "FS " line. Anything else is an error.
   if(strnicmp(m->line,"FS ", 3) != 0) {
      tprintf("FBB Error: Expected 'FS ' string. Received \'%s\' string.\n",m->line);
      logmbox (m->user, m->name, "FBB error, expected FS, got %s", m->line);
      return 0;
   }

#ifdef FBBVERBOSELOG
    /* 05Sep2010, Maiko (VE4KLM), Might as well log this as well */
    // Log FBB response only if really interested
    // log(m->user,"got FBB response of %s", m->line);

	/* 11Feb2016, Maiko (VE4KLM), fix this up a bit */
    logmbox (m->user, m->name, "got response %s", m->line);

#endif
   /* log (-1, "Got FS %d [%s] (%d)", strlen (m->line), m->line, msgcnt); */

#ifdef	MSG_ACCEPTED_FROM_OFFSET
	/*
	 * 29Jan2015, Maiko (VE4KLM), This scenario surfaces from time to time, but
	 * was never added to the JNOS code ever, so let's do it. One can't use the
	 * basic 'msgcnt + 3' anymore to check for the right number of responses.
	 */
	fsrptr = &(m->line[3]);
#else
   // Check to see if we got the right number of responses.
   if(((int)strlen(m->line) != msgcnt + 3))
      return 0;
#endif
 
   // log (m->user, "about to validate FS line");

   // Validate FS line. Clean up unused entries.
   for(i=0;i<FBBMAXMSGS;i++)
	{
		if (i < msgcnt)
		{
#ifdef	MSG_ACCEPTED_FROM_OFFSET
	 		/* 29Jan2015, Maiko (VE4KLM), Can't just go byte by byte anymore
			 * for the next valid FSR character, because if it's a '!' or 'A',
			 * then a numeric offset will come after, messing it all up ...
			 */
			fsr = *fsrptr++;
#else
			/* 11Apr2008, Maiko (VE4KLM), Use 'switch' instead (too many) */
			fsr = m->line[3+i];
#endif
			switch (fsr)
			{
				case '+':
				case '-':
				case '=':
				case 'Y':
				case 'N':
				case 'L':
				case 'H':	/* 07Sep2013, Maiko, B1F sends 'H', 'R', and 'E' */
				case 'R':
				case 'E':

#ifdef	MSG_ACCEPTED_FROM_OFFSET
			/* 29Jan2015, Maiko (VE4KLM), Now supporting accept from offset */
				case '!':
				case 'A':
					/* skip the offset for this validation part */
					while (*fsrptr && isdigit(*fsrptr))
						fsrptr++;
#endif
					break;

				default:
					tprintf ("FBB Error: unexpected FS response, got \'%c\'.\n", fsr);
					logmbox (m->user, m->name, "FBB error, unexpected FS response, got %c", fsr);
					return 0;
					break;
			}
		}
		else
		{
         // Zero out and free rest of the FS structure.
         msglst[i].accept = fbbUNKNOWN;
         free(msglst[i].to);
         free(msglst[i].rewrite_to);
         free(msglst[i].from);
         free(msglst[i].messageid);
         free(msglst[i].sline);
         msglst[i].to        = \
         msglst[i].rewrite_to= \
         msglst[i].from      = \
         msglst[i].messageid = \
         msglst[i].sline     = NULLCHAR;
      }
   }

#ifdef	MSG_ACCEPTED_FROM_OFFSET
	/* 29Jan2015, Maiko (VE4KLM), Reset pointer, this time get offsets */
	fsrptr = &(m->line[3]);
#endif

   // log (-1, "FS is okay, now send '+' messages");

   // The FS line is OK. Now we can send the '+' messages and delete the
   // '-' messages. The '=' messages can be ignored for now.
   for(i=0;i<msgcnt;i++)
	{
#ifdef FBBCMP
      if(NotConnected)
          break;
#endif
      pwait(NULL);
      rip(msglst[i].sline); // pretty up log.

      MbForwarded++;

#ifdef	MSG_ACCEPTED_FROM_OFFSET
	/* 29Jan2015, Maiko (VE4KLM), Can't just go byte by byte anymore
	 * for the next valid FSR character, because if it's a '!' or 'A',
	 * then a numeric offset will come after, messing it all up ...
	 */
	fsr = *fsrptr++;

	if (fsr == '!' || fsr == 'A')
	{
		Aoffset = atol (fsrptr);

		while (*fsrptr && isdigit (*fsrptr))
			fsrptr++;

		/* 11Feb2015, Maiko (VE4KLM), only need to see this for OFFSET */
		log (m->user, "FSR [%c] Offset [%ld] Warning - might not work !!! msgn [%d]",
			fsr, Aoffset, msglst[i].number);
	}
	else Aoffset = 0;

#else
	/* 11Apr2008, Maiko (VE4KLM), Need to also check for 'Y', not just '+' */
	fsr = m->line[3+i];
#endif

	/* 07Sep2013, Maiko, Added 'H' for accepted, but held (for B1F) */
	if ((fsr == '+') || (fsr == 'Y') || (fsr == 'H')
#ifdef	MSG_ACCEPTED_FROM_OFFSET
	/* 29Jan2015, Maiko (VE4KLM), Accept continue from offset requests */
		|| (fsr == '!') || (fsr == 'A')
#endif
	)
	{
         // Message is wanted. We need to send it.
         // Don't update the X-Forwarded Header yet. FBB does not tell us
         // if it received the message or not. We have to send all our
         // messages and then see if the connect is still there. If we get
         // a valid FBB Response, then we can assume that the messages were
         // delivered and update the X-Forwarded flag.

         msglst[i].accept = fbbYES;

         // I don't like having to re-read the index here, but sendmsg()
         // needs a lot of index info so it needs to be done.

 /*
  * 24Sep2020, Maiko, The offset needs to be applied to the yap transfer not to the
  * generation of the tfile, what was I thinking at the time ? The challenge however
  * is how to get this offset to inside the Encode called by send_yapp, all inside of
  * the lzhuf.c source file ... uggggg ...
  *
  */

#ifdef	MSG_ACCEPTED_FROM_OFFSET

#ifdef	AOFFSET_DEBUG
		/* 24Sep2020, Maiko (VE4KLM), should try and get this working, happening again with GB7CIP */
		log (m->user, "FSR [%c] Offset [%ld] Start [%ld] msgn [%d]", fsr, Aoffset, m->mbox[msglst[i].number].start, msglst[i].number);
#endif
		/*
		 * 29Jan2015, Maiko (VE4KLM), adjust start of read for 'A' or '!'
		 *
		m->mbox[msglst[i].number].start += Aoffset;
		 *
		 * 02Jul2021, Maiko (VE4KLM), This is just WRONG, the offset applies
		 * to the compressed file, should never have done this. I must admit
		 * my approach to trying to get this Restart Data or Continuance of
		 * a partial file forward (offset) has been half hazard, and I have
		 * yet to find a proper solution to this.
		 *
		 * Changing the start value contributes to buggering up the index,
		 * so it was a mistake to put it in way back when, sorry folks !
		 * 
		 * So having said that, all I've done so far is extract the offset
		 * value from any incoming '!' or 'A' FSR character in the FS string
		 * from a remote BBS, and log it. The worse that can happen is that
		 * the remote BBS will get a checksum error and give up on us :|
		 *
		 * The next 'release' I really want (need) to get this working,
		 * and I have a new max block size feature coming as well, that
		 * might possibly reduce the chances of '!' or 'A' showing up.
		 */
#endif

         // Re-position filepointer to index....
         lseek(idx,m->mbox[msglst[i].number].indexoffset,SEEK_SET);

         // Clear/Free the previous index
         default_index(m->area,&f->ind);

         // and read the index.
         if (read_index(idx,&f->ind) == -1) return(0);  /* should not happen */

         // sendmsg() assumes that f->bid will hold the message id.
         // msglst[i].bid was saved from the makecl() call.
         strcpy(f->bid, msglst[i].bid);

#ifdef FBBCMP
         if(f->m->sid & MBX_FBBCMP) {
              // Open a tmpfile for fbbsendmsg to place
              // the mssage data in. This file is not closed,
              // but passed to send_yapp for encoding and
              // transmitting.

              strcpy(f->iFile, j2tmpnam(NULL));
              if((tfile = fopen(f->iFile, "w+b")) == NULLFILE)
			{
                 log(m->user, "FBBFWD: fbbdofs: Error %d opening %s", errno, f->iFile);
                 return 0;
              }

			/* log (-1, "here were are"); */

/* 18Apr2008, Maiko, B2F has specific headers that need to be put in */
#ifdef	B2F
			/*
			 * 01May2010, Maiko, For a couple of years I've been using the
			 * type to figure out which function to call. Should never have
			 * done that, should be using the sid2 value to determine this.
			 *
			if (msglst[i].type == 'E')
			 */

			if (f->m->sid2 & MBX_B2F)
			{
				/* log (-1, "calling B2Fsendmsg"); */

				rc = B2Fsendmsg (f, msglst[i].number, tfile);

			}
			else
			{
				/* log (-1, "calling fbbsendmsg"); */

				rc = fbbsendmsg (f, msglst[i].number, tfile);
			}

			if (rc == -1)
				NotConnected = TRUE;   /* some error preparing msg */
#else
              // Prepare the text of the message.
              if (fbbsendmsg (f, msglst[i].number, tfile) == -1)
                 NotConnected = TRUE;   /* some error preparing msg */
#endif
			else
			{
				int b2f = 0;	/* 23Apr2008, Maiko (VE4KLM), new for B2F */
#ifdef B2F
			/*
			 * 01May2010, Maiko, Again, why have I been using type to do this,
			 * should be using the sid2 value to determine if B2F or not.
			 *
				if (msglst[i].type == 'E')
			 */
				if (f->m->sid2 & MBX_B2F)
				{
					/* log (m->user, "B2F outgoing"); */
					b2f = 1;
				}
#endif
                 strcpy(f->oFile, j2tmpnam(NULL));

#ifdef	DONT_COMPILE
			/* 23Apr2008, Maiko (VE4KLM), Added b2f flag for B2F fwding */
                 rc = send_yapp(f->m->user, f, f->ind.subject, b2f); // Send it.
#else

#ifdef	MSG_ACCEPTED_FROM_OFFSET
#ifdef	AOFFSET_DEBUG
		log (m->user, "setting glob_Aoffset [%ld]", Aoffset);
#endif
		glob_Aoffset = Aoffset;
#endif
		/* 28Aug2013, Maiko (VE4KLM), I'll be damned B1F same chksum as B2F */
				rc = send_yapp (f->m->user, f, f->ind.subject,
						b2f | (f->m->sid & MBX_FBBCMP1));
#endif

                 if(rc == 0)
				{
#ifdef FBBVERBOSELOG
                    log(m->user,"FBBFWD: send_yapp() error.");
#endif
                    NotConnected = TRUE;
                 }
              }

              // erase the input and output file.
              unlink(f->iFile);
              unlink(f->oFile);
         } else
#endif /* FBBCMP */
         {
           // Send a subject line.
           tprintf("%s\n",f->ind.subject);


           // Send the text of the message.
           if (sendmsg(f, msglst[i].number) == -1) {  /* aborted */
               default_index(m->area,&f->ind);
               break;
           }

           // Send a ctrl-z ... that's OCTAL(32)
           j2tputs("\032\n");
         }

         usflush(m->user);

         // and finally free the index.
         default_index(m->area,&f->ind);

      }
	else
         // The remote system doesn't want this message.
         // It might be a NO (-) or it might be a LATER (=)
         // Go ahead and mark the msg as so. If the link goes
         // down, we still want to mark this message as unwanted.
         // One the link goes back up, we don't want to ask the
         // remote system again about this because it already
         // told us no. This is handeled differently than '+'
         // messages.

	/* 11Apr2008, Maiko (VE4KLM), Need to also check for 'N', not just '-' */
	/* 07Sep2013, Maiko, Need to deal with 'R' for reject (B1F) */
         if ((fsr == '-') || (fsr == 'N') || (fsr == 'R'))
		{
            msglst[i].accept = fbbNO;
            // mark message as forwarded or deleted.
            if(m->areatype == AREA)
               m->mbox[msglst[i].number].status |= BM_FORWARDED;
            else
               m->mbox[msglst[i].number].status |= BM_DELETE;
            m->change = 1;

            logmbox (m->user, m->name, "%s refused", msglst[i].sline);
            //logmbox (m->user, m->name, "refused");
         }
		else
            // The remote system want's this message... later.... not now.
            // To handle this... we ignore the message for now and we
            // attempt to update the m->lastread message number to the
            // first deferred message. That way the next time we transfer
            // messages, we'll ask about this message again. BUT... during
            // this tranfer sequence, we don't want to keep asking about
            // the  same message so we have to keep track of the highest
            // deferred message.
            // Anyway... the mindeferred counter is the FIRST deferred
            // message we do and is used to update the m->lastread counter.
            // The maxdeferred counter is the last deferred message.

	/* 11Apr2008, Maiko (VE4KLM), Need to also check for 'L', not just '=' */
	/* 07Sep2013, Maiko, Handle 'E' for error (B1F), treat it as later */
            if ((fsr == '=') || (fsr == 'L') || (fsr == 'E'))
			{
               msglst[i].accept = fbbDEFER;
               // Re-position filepointer to index....
               lseek(idx,m->mbox[msglst[i].number].indexoffset,SEEK_SET);

               // Clear/Free the previous index
               default_index(m->area,&f->ind);

               // and read the index.
               if(read_index(idx,&f->ind) == -1) return(0);  /* should not happen */

               // maxdeferred will point to the highest deferred msg we've
               // processed.
               if(*maxdeferred < f->ind.msgid)
                  *maxdeferred = f->ind.msgid;

               // mindeferred will point to the first deferred msg we've
               // processed.
               if(*mindeferred == 0)
                  *mindeferred = f->ind.msgid;

               // and finally free the index.
               default_index(m->area,&f->ind);
            }
   } // end for

#ifdef FBBCMP
   if(NotConnected)
       return 0;
#endif


   // Now we'll get a line from the remote system to make sure that the
   // messages were received ok.

   usflush(m->user);
   j2alarm(Mbtdiscinit*1000);
   if(recvline(m->user,m->line,MBXLINE) == -1) {
      // There was a problem. We did not get a response from the remote
      // system after we sent our messages.
      // We'll go ahead and delete the '-' messages. He didn't want them
      // now, so he won't want them later.
#ifdef FBBVERBOSELOG
      log(m->user, RmtDisconnect);
#endif
      return 0;
   } else {
        j2alarm(0);
        if (strncmp (m->line, "*** ", 4) == 0) {  /* *** Protocole err, *** Checksum err, ... */
             rip(m->line);
             logmbox (m->user, m->name, "FBB error %s", m->line);
             return 0;
        }

      // We got a response back, so we can update our '+' messages.
      for(i=0;i<FBBMAXMSGS;i++) {
         if(msglst[i].accept == fbbYES) {
            // mark message as sent.
            if(m->areatype == AREA)
               m->mbox[msglst[i].number].status |= BM_FORWARDED;
            else
               m->mbox[msglst[i].number].status |= BM_DELETE;
            m->change = 1;

            // For each valid note.
            logmbox (m->user, m->name, "%s sent", msglst[i].sline);
            //logmbox (m->user, m->name, "sent");

            free(msglst[i].to);
            free(msglst[i].rewrite_to);
            free(msglst[i].from);
            free(msglst[i].messageid);
            free(msglst[i].sline);
            msglst[i].to        = \
            msglst[i].rewrite_to= \
            msglst[i].from      = \
            msglst[i].messageid = \
            msglst[i].sline     = NULLCHAR;
         }
#ifdef	DONT_COMPILE
		/* 07Sep2010, Maiko, show refused ones as well */
		else logmbox (m->user, m->name, "refused %s", msglst[i].sline);
#endif
      }
   }

   // Change forward direction
   m->state = MBX_REVFWD;
   if(strnicmp(m->line,"FF", 2) == 0)
      return 1;
   else
      return 2;
}

// This code receives the FB packet ( up to 5 messages ) from a FBB
// system. Once we get this FB Packet, we'll figure out which notes we
// want... send our response ( FS string ) and receive the messages.

/* Return codes for dofbbrecv:
   0 => Error, unexpected data format in m->line,
        or can't deliver mail.  Disconnect required.
   1 => All OK, msgs received and processed properly.
   2 => FF received, remote BBS has no data for us.
   3 => FQ received, remote BBS wants to quit.
*/
 
static
int dofbbrecv(struct fwd *f)
{
    int  rc;
    int  msgcnt;
    int  msgsize = 0;

    int  FBBok;
    int  FBBdone;

    int  i,err;
    int  FirstTime;
    int  NotConnected = FALSE;

	int  numfbbfields = 6;	/* FA and FB proposals have 6 additional fields */

    unsigned int checksum;
    char FBBresp[FBBMAXMSGS+4];	/* Must be FBBMAXMSGS+4 bytes */
    char *cp;

    struct fbbpacket *msglst;
    struct mbx       *m;

    msglst = f->msglst;
    m      = f->m;

    msgcnt = 0;


    FBBdone   = FALSE;
    FirstTime = TRUE;

    checksum = 0;

    // log (m->user, "dofbbrecv () entry mline [%s]", m->line);

    // Loop till we've received a F>, FF, FQ, ***, or 5 FB blocks.
    for(;;) {
        // If FirstTime = TRUE it means that we've (probably?) already gotten a
        // line from the remote system and don't need to ask for it
        // again.
        if(!FirstTime || m->line[0] == 0) {
            j2alarm(Mbtdiscinit*1000);
            if(recvline (m->user, m->line, MBXLINE) == -1) {
#ifdef FBBVERBOSELOG
                log(m->user, RmtDisconnect);
#endif
                break;  // The connection has closed on us.
            }
            j2alarm(0);
        }
        FirstTime = FALSE;

        pwait(NULL);
        // Start off  with FBBok = FALSE.
        // If we get a FB or F> we'll reset it to TRUE and
        // continue on. If we get something else we'll exit.
        FBBok = FALSE;

#ifdef	DONT_COMPILE
	{
		char buft[200], *buftptr = buft, *mlineptr = m->line;
		while (*mlineptr)
		{
			buftptr += sprintf (buftptr, "%d ", (int)(*mlineptr));
			mlineptr++;
		}
		log (m->user, "mline [%s]", buft);
	}
#endif
        // strip any trailing NL characters.
        rip(m->line);

 // log (m->user, "dofbbrecv mline [%s] 1rst time %d", m->line, FirstTime);

#ifdef B2F
		/* 20Jan2011, Maiko (VE4KLM), FC proposals have 5 additional fields */
        if(strnicmp(m->line,"fc",2) == 0)
			numfbbfields = 5;
		/*
		 * 01Dec2017, Maiko (VE4KLM), Not sure how I missed this, is it a new
		 * feature in the recently CMS moved over to amazon servers ? I have no
		 * idea. The ;PM is just a list of pending messages to be read, so just
		 * ignore it since B2F forwarding will get them anyways. Would not hurt
		 * to throw this information into the JNOS log though.
		 *
		 * Oops, this belongs in dofbbrecv() in fbbfwd.c source file !
		 */
		if (strnicmp (m->line, ";PM: ", 5) == 0)
		{
            rip (m->line);
			log (m->user, "[%s]", m->line);
			continue;
		}
#endif
        if(strnicmp(m->line,"ff",2) == 0) {
           // End of FBB Packet
           return 2;
        }

        if(strnicmp(m->line,"fq",2) == 0) {
           // End of FBB Packet
           return 3;
        }

        if(strnicmp(m->line,"f>",2) == 0) {

		// log (m->user, "%s", m->line);
/*
 * 23Apr2016, Maiko (VE4KLM), this section where the checkum is computed and
 * then compared to what we get from the remote system should never have been
 * commented out 10 years ago (and left that way), so let's just say I lacked
 * some experience in the FBB protocol way back in September of 2006.
 */
 		// Compare computed Checksum against supplied checksum.
        if (strlen(m->line) > 3)
		{
            checksum += htoi(skipwhite(m->line+2)); /* skip to cksum char(s) */

            if (checksum & 0xFF)	/* invalid sum */
			{
                 logmbox (m->user, m->name, "FBBFWD Proposals checksum err (%x)", checksum & 0xFF);

                 break;  /* FBBdone is FALSE, return code 0 => disconnect */
            }
		}
           // End of FBB Packet
           FBBdone = TRUE;
           break;
        }

		if (!strnicmp (m->line, "fa", 2) || !strnicmp (m->line, "fc", 2)
		/* 10Feb2015, Maiko (VE4KLM), why was this always missing ? */
		    || !strnicmp (m->line, "fb", 2)
)
		{

			// log (m->user, "recv BID [%s]", m->line); /* 06Feb2015, Maiko, DEBUG */
           // Another message
           if(msgcnt >= FBBMAXMSGS)
			{
              // Too many in fact. We're only supposed to accept
              // 5 (FBBMAXMSGS) msgs.
              log(m->user, "FBBFWD: > 5 messages offered.");
              break;  /* we never accept more than 5 ! */
           }
			else
			{
              // add the data to the checksum count.
              cp = m->line;
              i=0;  /* count fields in FA/FB proposal */
              while (*cp)
			{
                 checksum += *cp & 0xff;
                 if (*cp == ' ')
				{
                     i++;  /* we expect single blank delimiters */
                     if (*(cp+1) == ' ')   /* 2 adjacent blanks are illegal */
                         i=-100; /* assure too-few-fields complaint */
                 }
                 cp++;
              }

              // Add a checksum count for the FBB style end-of-line marker.
              // It doesn't uses a CR\LF but uses a CR instead.
              checksum += '\r' & 0xff;
#ifdef FBBVERBOSELOG
              logmbox (m->user, m->name, "incoming proposal %s", m->line);
#endif
              // Convert the FBB message string to it's ax.25 counterpart.
              if(i < numfbbfields) {  /* too few fields? */
                  FBBerror(11,m->user);
                  break;  /* with FBBdone at FALSE */
              }

		/* 13Feb2016, Maiko, Keep proposal for log display later
		 actually forget it, too much info in the log now
		free (msglst[msgcnt].sline);
		msglst[msgcnt].sline = j2strdup(m->line);
			14Feb2015, Maiko, messageid will work and it's
			already saved to msglst structure inside fbbparse
 		*/
              fbbparse(&msglst[msgcnt], m->line);
              msgsize += msglst[msgcnt].size;
              msgcnt++;
              FBBok = TRUE;
           }
        }
		// else log (-1, "fbbfwd - line [%s]", m->line);

		/* 16Jan2005, Maiko, Ignore any comments */
		if (strnicmp(m->line,"; ",2) == 0)
			FBBok = TRUE;

        // If we didn't reset the FBBok flag, something is wrong and we
        // need to exit.
        if(!FBBok)
		{
			//log (-1, "fbbfwd - !FBBok, break out");
           break;
		}
    }

    // We've exited the FB....F> loop.
    // If FBBdone = TRUE we're ok... otherwise we received bad data.
    if((!FBBdone))	/* proposals error, etc. */
	{
		// log (-1, "fbbfwd - !FBBdone, return");
       return 0;
	}

    // FB....F> data looks ok. Now process it and build our FS line.

	// log (m->user, "build FS response");

    strcpy(FBBresp, "FS ");
    // Check each message in the list to see if we want it or not.
    for(i=0;i<msgcnt;i++) {
       pwait(NULL);
       // first check the msgid. If that works, run it through the
       // rewrite file.
       if(msgidcheck(m->user, msglst[i].messageid)) {
          //logmbox (m->user, m->name, "msgidcheck says we have this already");
          logmbox (m->user, m->name, "already have %s", msglst[i].messageid);
          // Nope... already have this one.
          msglst[i].accept = fbbNO;
       }
#ifdef B2F
		/*
		 * 21Jan2011, Maiko (VE4KLM), All we get out of the B2F proposal is the
		 * msgid and data lengths. There is no point doing rewrite and address
		 * validation, since that information is actually in the payload. All
		 * we can do at this point is simply accept the proposal and move on.
		 */
		else if (!strnicmp (msglst[i].fbbcmd, "fc", 2))
          msglst[i].accept = fbbYES;
#endif
		else {
          // Tentativly accept this... now check the validity of the address.
          msglst[i].accept = fbbYES;
          pwait(NULL);
          free(msglst[i].rewrite_to);
          msglst[i].rewrite_to = NULLCHAR;

	/* 16Jun2021, Maiko (VE4KLM), new send specific rules in rewrite, so we need to pass FROM field */
          // if((cp = rewrite_address(msglst[i].to, REWRITE_TO)) != NULLCHAR)

          if((cp = rewrite_address_new (msglst[i].to, REWRITE_TO, msglst[i].from)) != NULLCHAR) {
             // See if this is on the reject list.
             if(!strcmp(cp,"refuse")) {
          		logmbox (m->user, m->name, "%s on refusal list", msglst[i].messageid);
                // oops.. rejected...
                msglst[i].accept = fbbNO;
                free(cp);
             } else {
                // keep track of new, rewritten address. This is passed
                // to the mboxmail dosend() code when we send the msg.
                msglst[i].rewrite_to = cp;
             }
          }

          // If we're still OK, make sure the address resolves ok.
          if(msglst[i].accept == fbbYES) {
             if(msglst[i].rewrite_to == NULLCHAR) {
                if(validate_address(msglst[i].to) == 0){
          			logmbox (m->user, m->name, "address (%s) invalid - check rewrite", msglst[i].to);
                   msglst[i].accept = fbbNO;
                }
             } else {
                if(validate_address(msglst[i].rewrite_to) == 0){
          			logmbox (m->user, m->name, "rewrite address (%s) invalid - check rewrite", msglst[i].rewrite_to);
                   msglst[i].accept = fbbNO;
                   free(msglst[i].rewrite_to);
                   msglst[i].rewrite_to = NULLCHAR;
                }
             }
          }
       }

		/*
		 * 21Oct2020, Maiko (VE4KLM), Late Night, are we in conflict with
		 * any other forward sessions that might be dealing with the same
		 * message simultaneous to this one, check if for 'active BID' !
		 *  (this is a new function to track ALL forwarding threads)
		 *
		 * 22Oct2020, Do this after ALL possible fbbNO assignments.
		 *
		 * Only make the call for messages already accepted by this point.
		 *
		 */
		if (msglst[i].accept == fbbYES && chk4activebid (m->user, m->name, msglst[i].messageid))
		{
			msglst[i].accept = fbbDEFER;	 /* was fbbNO, changed 29Oct2020, no choice */

			/* 24Oct2020, Maiko, Moved out of activeBID.c and put here instead */
			log (m->user, "chk4activebid - already processing [%s]", msglst[i].messageid);

			/*
			 * I'm not sure whether to DEFER instead - technically something
			 * could happen with final message delivery, then we would loose
			 * any opportunity to get the message again from someone else if
			 * we mark it as NO ... let it run for a while, see how it goes.
			 *
			 */
		}

       // By now.. we know if we want the message or not.... update the
       // FBBResp string.
       //
       switch (msglst[i].accept) {
       case fbbUNKNOWN:
          logmbox (m->user, m->name, "FBB Programming err, msglst.accept = Unknown");
          FBBresp[i+3] = '-';
          break;
       case fbbNO:
          FBBresp[i+3] = '-';
          break;
       case fbbYES:
          FBBresp[i+3] = '+';
          break;
       case fbbDEFER:
          // Currently, we don't defer but here it is anyway.
          FBBresp[i+3] = '=';
          break;
       } /* endswitch */
    }
    // Add a null to the end of the string.
    FBBresp[i+3] = '\0';

    // Now we can send it.
    tprintf("%s\n", FBBresp);
    usflush(m->user);
#ifdef FBBVERBOSELOG
    // Log FBB response only if really interested
    //log(m->user,"our FBB response is %s",FBBresp);

    /* 11Feb2016, Maiko (VE4KLM), fix this up a bit */
    logmbox (m->user, m->name, "our response %s", FBBresp);
#endif

    // The remote system will start sending the messages.
    // Loop through the message list.
    //
    for(i=0;i<msgcnt;i++) {
       if(NotConnected)
		{
			//log (-1, "fbbfwd - NotConnected, break");
           // We've lost the connection.....
           break;
		}
       if(msglst[i].accept == fbbYES)
		{
#ifdef FBBCMP
           if (f->m->sid & MBX_FBBCMP)
			{
				int b2f = 0;	/* 24Mar2008, Maiko (VE4KLM), new for B2F */
#ifdef FBBDEBUG2
               log(m->user,"FBBFWD: Processing new FBB Compressed Message.");
#endif
               // Receive the message and uncompress it.
               // Setup input and output file names.
               strcpy(f->iFile, j2tmpnam(NULL));
               strcpy(f->oFile, j2tmpnam(NULL));
#ifdef	B2F
			/* 24Mar2008, Maiko (VE4KLM), now support incoming FC (B2F) msg */
			/* 26Mar2008, Maiko (VE4KLM), check for E message type better */
			if (f->m->state == MBX_REVB2F || msglst[i].type == 'E')
			{
				/* log (m->user, "B2F incoming"); */
				b2f = 1;
			}
#endif
#ifdef DONT_COMPILE
		/* 24Mar2008, Maiko (VE4KLM), Added b2f flag for B2F fwding */
             rc = recv_yapp (m->user, f, &m->subject, Mbtdiscinit * 1000, b2f);
#else
		/* 28Aug2013, Maiko (VE4KLM), I'll be damned B1F same chksum as B2F */
			rc = recv_yapp (m->user, f, &m->subject,
					Mbtdiscinit * 1000, b2f | (f->m->sid & MBX_FBBCMP1));
#endif
               if(rc == 0)
				{
#ifdef FBBVERBOSELOG
                   log(m->user,"FBBFWD: recv_yapp() error.");
#endif
                   NotConnected = TRUE;
                   // erase the input and output file.
                   unlink(f->iFile);
                   unlink(f->oFile);

					/*
					 * 29Oct2020, Maiko (VE4KLM), Should probably delete the BID in
					 * the DUPE list or else it will never get accepted again until
					 * JNOS is restarted and/or I get a DUPE list cleanup working.
					 */
					log (m->user, "delactivebid - removing [%s]", msglst[i].messageid);

					delactivebid (m->user, msglst[i].messageid);		/* part of the new DUPE control code */
               }
			else
				{
                   // Process the message.

                   free(m->to);
                   free(m->tofrom);
                   free(m->tomsgid);
                   free(m->origto);
                   free(m->origbbs);
                   free(m->date);

					m->origto = m->origbbs = m->date = NULLCHAR;
	        /*
         	 * 15Jul2018, VE4KLM, Don't touch incoming messageid in any way
         	 *  strlwr (m->tomsgid = j2strdup (msglst[i].messageid));
         	 */
					m->tomsgid = j2strdup (msglst[i].messageid);

					m->stype = msglst[i].type;
#ifdef B2F
					if (m->stype != 'E')
					{
#endif
						/*
						 * 31Mar2008, Maiko, We can't use 'msglst' to set
						 * mail recipients, since B2F does not send that in
						 * the FBB proposal. That information is contained in
						 * the actual payload, so mail recipients forany B2F
						 * msgs are handled in dofbbacceptnote() below.
						 */
#ifdef AIRMAILGW
						if (!stricmp ("NEXUS@NEXUS", msglst[i].to))
						{
							char tmpsubject[50];

							strlwr(m->to = j2strdup(m->subject));
							free (m->subject);
							sprintf (tmpsubject, "Priority Message from Radio Station %.9s", m->name);
							m->subject = j2strdup (tmpsubject);
						}
						else
#endif
						if (msglst[i].rewrite_to != NULLCHAR)
						{
							strlwr(m->to = j2strdup(msglst[i].rewrite_to));
							strlwr(m->origto = j2strdup(msglst[i].to));
						}
						else
						{
							strlwr(m->to = j2strdup(msglst[i].to));
						}
#ifdef AIRMAILGW
				/*
				 * 17Mar2007, Maiko, Leave m->tofrom = null and set m->origbbs,
				 * to give us a 'proper' FROM header in emails that go out.
				 *
				 * 31Mar2008, Maiko, Oops (TYPO) tofrom was not being set,
				 * actually this may explain some issues Jack (AA6HF) was
				 * mentioning to me recently - some sysops are going back
				 * to JNOS 2.0e, some problems with 'tofrom' field, etc !
				 *
				 */
						if (!stricmp ("NEXUS@NEXUS", msglst[i].to))
							strlwr (m->origbbs = j2strdup(Hostname));
						else
#endif
							strlwr(m->tofrom = j2strdup(msglst[i].from));

						rc = dofbbacceptnote(m, f->oFile);
#ifdef B2F
					/* 07Apr2008, Maiko, decided on separate function */
					} else rc = doB2Facceptnote (m, f->oFile);
#endif

#ifdef FBBVERBOSELOG
					if (rc != 1)
						log (m->user, "FBBFWD: dofbbacceptnote() failed");
#endif
                   /* erase the input and output files */
                   unlink(f->iFile);
                   unlink(f->oFile);

            // For each valid note.
            logmbox (m->user, m->name, "%s received", msglst[i].messageid);
            //logmbox (m->user, m->name, "received");

                   free(m->to);
                   free(m->tofrom);
                   free(m->tomsgid);
                   free(m->origto);
                   free(m->origbbs);
                   free(m->subject);
                   free(m->date);
                   m->to      = \
                   m->tofrom  = \
                   m->tomsgid = \
                   m->origto  = \
                   m->origbbs = \
                   m->subject = \
                   m->date    = NULLCHAR;
#ifdef FBBDEBUG2
                   log(m->user,"FBBFWD: Finished w/ FBB Compressed Message.");
#endif
               }
           } else
#endif /* FBBCMP */
        {
          pwait(NULL);
          sprintf(m->line, "S%c %s < %s $%s_%s->%s",
                                                  msglst[i].type,
                                                  msglst[i].to,
                                                  msglst[i].from,
                                                  "fbbbid",
                                                  msglst[i].rewrite_to,
                                                  msglst[i].messageid);

            logmbox (m->user, m->name, "no compression, mbxparse [%s]", m->line);

          // Call mbx_parse to parse the send command.
          err=mbx_parse(m);

		if (err)
			log (-1, "fbbfwd - mbx_parse err %d", err);

          // Close any open files left from an error.
          if (m->tfile) {
              fclose(m->tfile);
              m->tfile = NULLFILE;
          }
          if(m->tfp) {
              fclose(m->tfp);
              m->tfp   = NULLFILE;
          }
          if(err == -2) return(0);  /*N5KNX: error while processing msg, no choice but to disconnect */
       }
	}	/* explicit braces fixed */
    }

#ifdef FBBDEBUG
    if(Mtrace)
        PUTS("FBBRECV: We've sent our data.");
#endif

    if(NotConnected)
	{	
		//log (-1, "we got disconnected at the end");
        // We got disconneted... tell the caller.
        	return 0;
	}
    else
        return 1;
}



// This is where the forwarding starts.
/* dofbbsend return codes:
   0 => no data sent to remote system (time constraints, not in forward.bbs, etc)
   1 => All OK, msgs were sent to remote BBS
   2 => no data found to send to remote BBS
   3 => Error while sending data to remote BBS
*/
static
int dofbbsend(struct fwd *f)
{
   int    msgcnt;
   int    TMsgCnt;
   int    err = 0;
   int    i;
   int    rc;
   int    idx;
   int    MsgOk;

   int    FBBdone;
   int    FBBok;
   long   FirstRead=0L;
   long   idxloc;

   char   *cp,*dp;
   char   fn[FILE_PATH_SIZE];
   char   oldarea[64];

   char   *savefsline;

   struct indexhdr hdr;
   struct fwdbbs *bbs;

   // for makecl()
   int    bulletin = (f->m->areatype == AREA);
   char   line[80];

   struct fwdarealist   *fwdarea;
   struct fwdarealist   *curarea;
   struct fwdarealist   *newarea;

   struct fbbpacket *msglst;
   struct mbx       *m;

#ifdef	B2F
   unsigned int fc_checksum;
   char *fc_ptr;
#endif

   msglst = f->msglst;
   m      = f->m;

   savefsline = NULLCHAR;

    // Check to see if we have any info to send and make sure it's the
    // right time to send data.
    if(fwdinit(f->m) == -1) {
       // The bbs is not in the forward.bbs file or it's the wrong time
       // to send.
       // Return a 0 to indicate that we have no data for the remote
       // system.
       return 0;
    } else {
       // It's ok to send data to the remote system.
       strcpy(oldarea,m->area);

       // Check to see if we've already read the list of areas to send
       // for this bbs. If f->FwdAreas == NULL, we haven't read the
       // list so we need to do so. If it's not NULL then we've already
       // read the list and we don't need to do it again.
       //
       // Since FBB calls this code for each block of 5 messages, we
       // want to reduce the number of times we read the foward.bbs
       // file to pull out the forward area info.

       if(!f->FwdAreas) {

          // Set curarea to NULL to prevent a warning message.
          curarea = NULL;

          while(!err && fgets(m->line,MBXLINE,m->tfile) != NULLCHAR) {

             pwait(NULL);
             // Skip over non-area lines.
             if(*m->line == '-')     /* end of record reached */
                break;
             cp = m->line;
             rip(cp);           /* adds extra null at end */
             /* skip spaces */
             while(*cp && (*cp == ' ' || *cp == '\t'))
                cp++;
/*             if(*cp == '\0' || *cp == '.' || *cp == '#' ||
                *cp == '+'  || *cp == '&' || *cp == '@')*/
               if(strchr(FWD_SCRIPT_CMDS,*cp)!=NULLCHAR)
                continue;       /* ignore empty or connect-script lines */

             /* find end of area name, and beginning of optional destination string */
             for (dp=cp; *dp && *dp != ' ' && *dp != '\t' && *dp != '\n'; dp++) ;
             if (*dp) *dp++ = '\0';   /* mark end of area name string */

             // Get memory for area info
			/* 22Dec2005, Maiko, changed malloc() to mallocw() instead ! */
             newarea = mallocw(sizeof(struct fwdarealist));

             // Setup area info
             newarea->name        = j2strdup(cp);
             newarea->mindeferred = 0;
             newarea->maxdeferred = 0;
             newarea->next        = NULL;

             /* process optional destination field */
             cp=dp;  /* strip leading blanks */
             while(*cp && (*cp == ' ' || *cp == '\t'))
                 cp++;
             /* find end of optional destination */
             for (dp=cp; *dp && *dp != ' ' && *dp != '\t' && *dp != '\n'; dp++) ;
             if (*dp) *dp = '\0';
             newarea->opt_dest    = j2strdup(cp);

             // Insert area into list
             if(f->FwdAreas == NULL) {
                 f->FwdAreas = newarea;
             } else {
                 curarea->next = newarea;
             }
             curarea = newarea;
          }
       }
       // We're done with the forward.bbs file so we can close it.
       fclose(m->tfile);
       m->tfile = NULLFILE;
    }

    TMsgCnt = 0;
    FBBok  = TRUE;

    fwdarea = f->FwdAreas;

    while(fwdarea) {

        // Change to area.

        cp = fwdarea->name;
        changearea(f->m,cp);

        /* Now create the index filename */
        sprintf(fn,"%s/%s.ind",Mailspool,cp);

        /* Loop backwards through message list, stopping when we've found a
         * message that we've already processed.  Remember msgid's may not be
         * monotonic due to MC/MM command use.
         */

        for(i = m->nmsgs; i; i--) {
            if(!fwdarea->maxdeferred) {
               if(m->mbox[i].msgid == m->lastread) { /* can't use <= since MC/MM exist */
                  break;
               }
            } else {
               if(m->mbox[i].msgid == fwdarea->maxdeferred) {
                  break;
               }
            }
        }

        FBBdone = FALSE;

        if(Mtrace)
           PRINTF("Processing area %s. LastMsgidRead=%ld: begin at %d of %d.\n",
                  cp, m->lastread, i, m->nmsgs);

        if(i) FirstRead = m->mbox[i].msgid;

        // Check for new messages.
        if(i != m->nmsgs) {
           i++;
           /* open the index file */
           if((idx=open(fn,READBINARY)) != -1)
			{
               /* check if there are any messages in this area
                * that need to be forwarded.
                */
               if(read_header(idx,&hdr) != -1)
				{
                   // Position at correct index entry.
                   lseek(idx,m->mbox[i].indexoffset,SEEK_SET);

                   // Reset msgcnt;
                   msgcnt  = 0;
                   FBBdone = FALSE;
#ifdef B2F
					fc_checksum = 0;
#endif
                   // i is set above.... points to next message to
                   // process.
                   for(; i<=m->nmsgs; i++)
		
					{
                       //pwait(NULL);	/* 25Oct2009, Maiko, Too agressive */

                       MsgOk = TRUE;

                       if (read_index(idx,&f->ind) == -1) break;  

                       /* Check x-forwarded-to fields */
                       for(bbs=f->ind.bbslist;bbs;bbs=bbs->next) {
                          if(!stricmp(bbs->call,m->name)) {
                             MsgOk = FALSE;
                             break;
                          }
                       }

                       if(f->ind.status & BM_HOLD) {  /* treat as deferred msg */
                          MsgOk = FALSE;
                          if(fwdarea->maxdeferred < f->ind.msgid)
                             fwdarea->maxdeferred = f->ind.msgid;
                          if(fwdarea->mindeferred == 0)
                             fwdarea->mindeferred = f->ind.msgid;
                       }

                       // We want to send this message.
                       if(MsgOk) {
                          // Build FB line.
                          rc = makecl(f, i, fwdarea->opt_dest, line, NULL, &bulletin);
                          // If FB line ok, send it.
                          if(rc != -1) {

                             // Keep track of message number in area.
                             msglst[msgcnt].number = i;

                             // Keep track of makecl() modified bid.
                             strcpy(msglst[msgcnt].bid, f->bid);
/*
 * 29Jun2018, Maiko, NO UPPER CASING, it's done in makecl where needed !
 */
#ifdef DONT_COMPILE
                             // uppercase the FB line and send it.
                             strupr(line);
#endif
						/*
						 * 10Feb2015, Maiko (VE4KLM), Moved code after upper
						 * so that the log shows what actually went out !
						 */
                             // Copy FB line for message log.
                             free(msglst[msgcnt].sline);
                             //msglst[msgcnt].sline = j2strdup(line);
						/* 13Feb2016, Maiko, I just need to log BID now */
                             msglst[msgcnt].sline = j2strdup(f->bid);

                             tprintf("%s", line);

						/* 11Feb2016, Maiko (VE4KLM), log outgoing proposal */
            			logmbox (m->user, m->name, "proposal %s", line);
#ifdef	B2F
					/*
					 * 29Apr2010, Maiko, FC Proposal requires checksum
					 * took me literally all day to figure out the exact
				 	 * algorithm they are using here, finally figured it
					 * out around 10:15 pm - 2's complement of the sum !
					 */
							if (line[1] == 'C')
							{
								/* log (-1, "update FC proposal checksum"); */

								fc_ptr = line;
						/*
						 * 30Apr2010, Maiko, It still does not work, however it
						 * would seem that 'makecl()' puts a NEWLINE at the end
						 * of the proposal (which we do NOT want to include as
						 * part of the checksum, but was being included). Make
						 * sure we STOP when we hit the newline - it now works!
						 */
								while (*fc_ptr && *fc_ptr != '\n')
									fc_checksum += *fc_ptr++ & 0xff;

								fc_checksum += '\r' & 0xff;

#ifdef	WRONG_SPOT
				/* this stuff should be AFTER we sum up ALL messages */

						/* checksum equals the 2's complement of the sum */
								fc_checksum = ~(fc_checksum & 0xff) + 1;

								fc_checksum &= 0xff;	/* only 2 digits */
#endif
							}
#endif
                             fbbparse(&msglst[msgcnt], line);
                             msgcnt++;
                             TMsgCnt++;

                             // If we've filled our FB Block
                             if(msgcnt >= FBBMAXMSGS)
							{
                                // Send a end-of FB Block flag
#ifdef	B2F
								if (line[1] == 'C')
								{
									/* checksum equals the 2's complement of the sum */
									fc_checksum = ~(fc_checksum & 0xff) + 1;

									fc_checksum &= 0xff;	/* only 2 digits */

									/* log (-1, "FC proposal checksum (%u)", fc_checksum); */
                                	tprintf("F> %X\n", (int)fc_checksum);
								}
								else
#endif
                                tprintf("F>\n");

                                // Process an incoming FS and send any accepted messages.
                                idxloc = tell(idx); /* VK1ZAO: remember current position in index */
                                rc = fbbdofs(msglst, f, msgcnt, &fwdarea->mindeferred, &fwdarea->maxdeferred, idx);
                                free(savefsline);
                                savefsline = j2strdup(m->line);

                                // Reset counter.
                                msgcnt  = 0;

                                if(!rc) {
                                   FBBok = FALSE;
                                   FBBdone = TRUE;
                                } else
                                if(rc == 1) {
                                   FBBok = TRUE;
                                   FBBdone = FALSE;
#ifdef EXPIRY
                                   if (Eproc) FBBdone = TRUE;  /* quit early so expire can run */
                                   else  /* must restore index file position */
#endif
                                   lseek(idx,idxloc,SEEK_SET);  // VK1ZAO: Return to our location in index
                                } else
                                if(rc == 2) {
                                   FBBok = TRUE;
                                   FBBdone = TRUE;
                                }
                             }
                          }
                       }

                       /* Done with this index, clear it */
                       default_index(m->area,&f->ind);
                       m->newlastread = m->mbox[i].msgid;

                       scanmail(f->m);

#ifdef	VERY_BAD_IDEA
			/*
			 * 25Oct2009, Maiko, pwait(NULL) is way too agressive
			 * and causes 99 % cpu on the JNOS exec. I find using
			 * a 100 ms pause takes it down to 45 % cpu, and a 200
			 * ms pause takes it down to 30 %. I still think that
			 * is way too high (on my Pentium 100) !!!
			 *
			 * Guess it comes down to compromise, how much does one
			 * want to wait to parse a large area index, versus how
			 * much do they want to load JNOS down ...
			 *
			 * This requires some new thinking - redesign ???
			 */
			j2pause (200);

                       //pwait(NULL);	/* 25Oct2009, Maiko, Too aggressive */
#endif
                       pwait(NULL);	/* 29Jun2014, Maiko (VE4KLM), Reinstate original code !!! */

                       // If we got an error from dofs() or the response
                       // from dofs() was 2, we're done with this message
                       // transfer. If we got a 1 from dofs() we can send
                       // more messages now so we don't need to exit.
                       if(FBBdone)
                         break;
                       FBBok = TRUE;  /* n5knx: Init, in case for() terminates. */
                   } // end for()

                   /* Done with this index, clear it */
                   default_index(m->area,&f->ind);

                   // Finish off any FB messages.
                   if(msgcnt) {

                      // Send a end-of FB Block flag
#ifdef	B2F
						if (line[1] == 'C')
						{
							/* checksum equals the 2's complement of the sum */
							fc_checksum = ~(fc_checksum & 0xff) + 1;

							fc_checksum &= 0xff;	/* only 2 digits */

							/* log (-1, "FC proposal checksum (%u)", fc_checksum); */

                           	tprintf("F> %X\n", (int)fc_checksum);
						}
						else
#endif
                      tprintf("F>\n");

                      // Process an incoming FS and receive messages.
                      /* no need to save index file position as we will close(idx) shortly */
                      rc = fbbdofs(msglst, f, msgcnt, &fwdarea->mindeferred, &fwdarea->maxdeferred, idx);

                      free(savefsline);
                      savefsline = j2strdup(m->line);

                      // Reset counter.
                      msgcnt  = 0;

                      if(!rc) {
                         FBBok = FALSE;
                         FBBdone = TRUE;
                      } else
                      if(rc == 1) {
                         FBBok = TRUE;
                         FBBdone = FALSE;
#ifdef EXPIRY
                         if (Eproc) FBBdone = TRUE;  /* quit early so expire can run */
#endif
                      } else
                      if(rc == 2) {
                         FBBok = TRUE;
                         FBBdone = TRUE;
                      }
                   } // end if()
               } // end if()
           } // end if()
           close(idx);
           idx = 0;

           // If we got an error from dofs() or the response
           // from dofs() was 2, we're done with this message
           // transfer. If we got a 1 from dofs() we can send
           // more messages now so we don't need to exit.
           if(FBBdone) {
             break;
           }
        } // end if()

        // Done with the area. Close the *.txt file if it was open.
        if(m->mfile) {
            fclose(m->mfile);
            m->mfile = NULL;
        }

        if(fwdarea->mindeferred != 0) {
           m->lastread    = 0;
           m->newlastread = 0; /* we should use the msgid of the msg preceding fwdarea->mindeferred */
        }

        if(FBBdone)
           break;

        // Do next area.
        fwdarea = fwdarea->next;
    } // end while()

#ifdef FBB_OLD_SCANNER
    /* N5KNX: Play it safe, and always scan from first msg in an area.
       Enable this if optimized scanning has unforseen flaws */
    m->lastread = m->newlastread = 0;
#else
    // An error occured. Reset m->lastread so we'll process this area again.
    if(!FBBok) {
        m->lastread    = 0;
        m->newlastread = FirstRead;
    }
#endif

    // change back to original message area.
    if(*oldarea != '\0')
        changearea(f->m,oldarea);


    // set up the result from fbbdofs() call.
    if(savefsline) {
       strcpy(m->line, savefsline);
       free(savefsline);
    }

    if(!FBBok)
       // We had an error.
       return 3;

    if(TMsgCnt == 0) {
#ifdef FBBDEBUG
       if(Mtrace)
          PUTS("dofbbsend: No Data to send.");
#endif
       // We had no data.
       return 2;
    }

    return 1;
}


// This is the main entry point for FBB forwarding.
int dofbbfwd (int argc, char **argv, void *p)
{
    struct fwd f;
    struct mbx *m;
    struct fbbpacket *msglst;

    int i;
    int Done;
    int rc;
    int FBBRdone;              // Receiving system has no more data.
    int NeedData;
    int FBBfwd;

    struct fwdarealist *fwdareas, *curareas;

    f.m = (struct mbx *)p;

    m = f.m;

    memset(&f.ind,0,sizeof(struct mailindex));

    logmbox (f.m->user, f.m->name, "forwarding");

    // First verify that this is a FBB type system.
    if (!(f.m->sid & MBX_FBBFWD))
	{
       tprintf ("Huh?\n");
       usflush (m->user);
       return -1;
    }

#ifdef FBBCMP
    // Get data buffers for LZHUF transfers.
    if ((f.m->sid & MBX_FBBCMP) && !AllocDataBuffers(&f))
	{
        log (f.m->user, "FBBFWD: insufficient memory for buffers");
        j2tputs ("FQ\n");  /* for good measure, before we disconnect */
        usflush (m->user);
        return -1;
    }
#endif

    // Start with fresh copy of FwdAreas list
    f.FwdAreas = NULL;

    // Get memory for the msglst array.
    f.msglst = (struct fbbpacket *)callocw(FBBMAXMSGS,sizeof(struct fbbpacket));
    msglst = f.msglst;

    // Clear out the msglst array.
    for(i=0;i<FBBMAXMSGS;i++)
	{
       msglst[i].to        = NULLCHAR;
       msglst[i].from      = NULLCHAR;
       msglst[i].messageid = NULLCHAR;
       msglst[i].sline     = NULLCHAR;
       msglst[i].rewrite_to= NULLCHAR;
       msglst[i].size      = 0;
    }

    NeedData = FALSE;
    Done     = FALSE;
    FBBfwd   = FALSE;

    if (argc == 0)
	{
       f.m->state = MBX_FORWARD; // We're in send mode.

       FBBRdone   = FALSE;
       NeedData   = TRUE;
       FBBfwd     = TRUE;
    }
#ifdef	B2F
	else if (!strnicmp (argv[0], "FC", 2))
	{
		if (argc != 6)
		{
			tprintf ("Huh?\n");
			usflush (m->user);
			Done = TRUE;
		}
		else
		{
			sprintf (f.m->line, "FC %s %s %s %s %s",
				argv[1], argv[2], argv[3], argv[4], argv[5]);
/*
 * 16Jul2018, Maiko, NO UPPER CASING - this destroys the case of the incoming
 * BID, what we should be doing is having the checksum function itself make a
 * working copy of the line, strupr the working copy, leave original alone !
 */
#ifdef DONT_COMPILE
			/* mbx_parse forced lowercase, we need uppercase for checksum */
			strupr (f.m->line);
#endif
			/* indicate we are in receive mode */

			f.m->state = MBX_REVB2F;	/* 24Mar08, Maiko, New B2F flag */
			FBBRdone = FALSE;
		}
	}
#endif
    else if ((strnicmp (argv[0], "FB", 2) == 0) ||
			 (strnicmp (argv[0], "FA", 2) == 0))
	{
		if (argc != 7)
		{
          tprintf("Huh?\n");
          usflush(m->user);
          Done     = TRUE;
		}
		else
		{
  			/* treat FB as FA ??? - from original pre 111f jnos code */
			sprintf (f.m->line, "FA %s %s %s %s %s %s",
				argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
/*
 * 16Jul2018, Maiko, NO UPPER CASING - this destroys the case of the incoming
 * BID, what we should be doing is having the checksum function itself make a
 * working copy of the line, strupr the working copy, leave original alone !
 */
#ifdef DONT_COMPILE
  			/* mbx_parse forced lowercase, but we need uppercase for cksum */
			strupr (f.m->line);
#endif
          // indicate that we're in receive mode.
          f.m->state = MBX_REVFWD;
          FBBRdone = FALSE;
		}
	}
	else
	{
       // Must have received a FF from the remote system.
       f.m->state = MBX_FORWARD; // We're in send mode.
       FBBRdone = TRUE;
	}

    while (!Done)
	{
		pwait(NULL);

		/* 24Mar2008, Maiko, New B2F handling as well for WinLink */
		if (f.m->state == MBX_REVFWD
#ifdef B2F
		 || f.m->state == MBX_REVB2F
#endif
		)
		{
#ifdef FBBDEBUG
          if(Mtrace)
             PUTS("FBBFWD: Receiving data from remote system.");
#endif
          if(NeedData) {
             // null out the line.
             f.m->line[0] = 0;
          }

          // Receive data from remote system.
          // Process FB....F> block.
          rc = dofbbrecv(&f);
          if(rc == 0) {
             // An error occured.
             Done = TRUE;
             if (*m->line)
                logmbox (m->user, m->name, "FBB error, last read : %s", m->line);
          }
          else
             if(rc == 2)
                // Remote system sent us a FF
                FBBRdone = TRUE;
          else
             if(rc == 3) {
                // Remote system sent us a FQ so break; and disconnect.
                Done = TRUE;
             }

          // Change status.
          f.m->state  = MBX_FORWARD;
       } else {
#ifdef FBBDEBUG
          if(Mtrace)
             PUTS("FBBFWD: Sending data to remote system.");
#endif

          NeedData = FALSE;
          // Change status.
#ifdef EXPIRY
          FBBSendingCnt++;    /* lock out expire() */
#endif
          rc = dofbbsend(&f);
#ifdef EXPIRY
          if(--FBBSendingCnt == 0) j2psignal(&FBBSendingCnt, 1);
#endif /* EXPIRY */
          if(rc == 3)
             // An error occured.
             break;
          if((rc == 0) || (rc == 2)) {
             // We had no data for remote system.
             if(FBBRdone) {
                // They have no more data for us....
                // So we break out of this loop and send our FQ and disconnect.
#ifdef FBBDEBUG
                if(Mtrace)
                   PUTS("FBBSEND: No Data to send. No Data to receive.");
#endif
                break;
             } else {
                // Tell them that we don't have any data for them.
#ifdef FBBDEBUG
                if(Mtrace)
                   PUTS("FBBSEND: No Data to send. Sending FF");
#endif
                tprintf("FF\n");
                usflush(m->user);
                NeedData = TRUE;
             }
          }

          // Change status.
          f.m->state  = MBX_REVFWD;
       }
    } /* endwhile */

    // free anything in the msglst array.
    for(i=0;i<FBBMAXMSGS;i++) {
       free(msglst[i].to);
       free(msglst[i].rewrite_to);
       free(msglst[i].from);
       free(msglst[i].messageid);
       free(msglst[i].sline);
    }

    // Now free the msglst array.
    free(msglst);

    // Free our FwdAreas list
    fwdareas = f.FwdAreas;
    while(fwdareas) {
        free(fwdareas->name);
        free(fwdareas->opt_dest);
        curareas = fwdareas;
        fwdareas = fwdareas->next;
        free(curareas);
    }

#ifdef FBBCMP
    // Free our buffers.
    if (f.m->sid & MBX_FBBCMP)
        FreeDataBuffers(&f);
#endif

	/*
	 * 13May2010, Maiko, I have noticed that if we get an FQ from
	 * the remote system, that we are sending an FQ back !!! I don't
	 * believe that is correct !!! Why would we want too ?
	 */
	//if (rc != 3)
	/* Latter part of May, 2016, only do this if we get FF ??? */
	if (rc == 2)
	{
    	// We're done. Send our FQ and we're out of here.
    	tprintf("FQ\n");
	}

    usflush(m->user);

    if(FBBfwd)
       exitfwd(m);
    else
       return domboxbye(0,NULL,m);
    return 0;
}

#ifdef FBBCMP

/* 21Dec2004, Maiko, Replaces the use of GOTO eol_ctlz label */
static void do_eol_ctlz (int check_r, struct mbx *m)
{
#ifdef RLINE
	if (check_r)
	{
		/* Hmm, this means we never finished the R: headers
		 * tmp file still open !
		 */
		MYFCLOSE(m->tfp);
	}
#endif
}

extern int MbRecvd;

#ifdef B2F
	/*
	 * 29Mar2008, Maiko (VE4KLM), all the mail header values are taken
	 * from the payload in the B2F technique. Use 'stype = E' to tell us
	 * that it is a B2F format in affect. See example payload below :
	 *
		MID: 1065_VE4PKT
		Date: 2008/03/29 19:07
		Type: Private
		From: VE4PKT
		To: ve4bbs
		To: smtp:maiko@pcs.mb.ca
		To: ve4klm
		Cc: sysop
		Subject: testing multiple recipients from Airmail
		Mbo: VE4PKT
		Body: 53
		File: 247482 kitegirls1.jpg
		File: 307789 kitegirls2.jpg
	 *
	 * NOTE : multiple 'To:' and 'Cc:' fields. Nice !
	 */
/*
 * 06Apr2008, Maiko (VE4KLM), After some farting around I have decided to
 * put the B2F accept into it's own function, instead of trying to hack it
 * into the existing dofbbacceptnote () function. Easier to read, and the
 * processing is different enough to warrant doing this. The R: line stuff
 * does not appear to be used in the more 'modern' AirMail and WL2K progs
 * anyway, and just gets in the way.
 */

#define MAXAFILES 10

typedef struct {
	char *name;
	int	size;
} AFILES;

static AFILES afiles[MAXAFILES]; /* 07Apr2008, Maiko, Multiple attachments */

static int debugB2F = 0, numafiles = 0;

static int doB2Facceptnote (struct mbx *m, char *infile)
{
	int cnt, fromfilledin = 0, tofilledin = 0, bodysize = 0, filesize = 0;

    char *obuf, *host, fullfrom[MBXLINE], *rhdr = NULLCHAR;

    struct list *cclist = NULLLIST;

    FILE *fpInFile, *ofp;

	/* 09Feb2011, Maiko - oops, need to put in the rewrite code !!! */
	char *r_rewrite_to = NULLCHAR;
	char *r_to = NULLCHAR;
	char *r_from = NULLCHAR;

    if ((m->tfile = tmpfile ()) == NULLFILE)
        return 0;

    if ((fpInFile = fopen (infile, "r+b")) == NULLFILE)
	{
        log (m->user, "doB2Facceptnote - error %d opening %s", errno, infile);
        return 0;
    }

	m->state = MBX_DATA;

	numafiles = 0;	/* important - zero attachments to start with */

	/* Process header information first */

	while (fgets (m->line, MBXLINE, fpInFile))
	{
		rip (m->line);

		if (debugB2F)
			log (m->user, "%s", m->line);

		if (m->line[0] == 0)
			break;
		/*
		 * 15Aug2010, Maiko, Need to somehow fix this 1 main recipient JNOS
		 * limitation. If there are more than 1 main recipient, then the best
		 * I can do right now is make the others carbon copy recipients. At
		 * least they'll get their mail, better than nothing.
		 *
		 * 10Jun2020, Maiko (VE4KLM), Now that I got multiple mailbox download
		 * working, it has dawned on me, there is no need to process subsequent
		 * 'To:' fields and fake them out with Cc:, which just causes duplicate
		 * messages to everyone involved. Winlink sends separate proposals for
		 * all the main 'To:' recipients anyways, so scrap the Cc: fakeout !
		 */
		if (!strncmp (m->line, "To: ", 4))
		{
			if (!tofilledin)
			{
				/* origto not set since rewrite had nothing to use */
				r_to = j2strdup (m->line + 4);

				/* 10Jun2020, Maiko (VE4KLM), doesn't hurt to note this */
				log (m->user, "main (To:) recipient [%s]", m->line + 4);
	
				/* current JNOS code only allows 1 main recipient */
				tofilledin = 1;
			}
			else 
#ifdef	FAKEOUT_THE_TO_WITH_CC
			{
				log (m->user, "main recipient [%s] CC'd instead", m->line + 4);
				addlist (&cclist, m->line + 4, 0, NULLCHAR);
			}
#else
				/* 10Jun2020, Maiko (VE4KLM), just note it now */
				log (m->user, "subsequent (To:) recipient [%s] noted", m->line + 4);
#endif
		}

		/* 01Sep2010, Maiko, Oops, Need to set who it is from */
		if (!strncmp (m->line, "From: ", 6))
		{
			if (!fromfilledin)
			{
				char *ftemp;

				r_from = j2strdup (m->line + 6);

				log (m->user, "original [%s]", r_from);

			/*
			 * 12Nov2013, Maiko (VE4KLM), Use some of Lantz code in wpage.c
			 * to better format the 'From: ' field, this is a bit of a wakeup
		 	 * call here, nowhere in JNOS do we check for " or < delimiters,
			 * the assumption is that (from the very old days), it is simply
			 * formatted like 'name@company.com' - that doesn't work now :(
			 */
				if (*r_from == '"')
				{
					r_from++;
					if ((ftemp = strchr (r_from, '"')) != NULLCHAR)
						r_from = ftemp+1;
				}
				if ((ftemp = strchr (r_from, '<')) != NULLCHAR)
				{
					r_from = ftemp+1;
					if ((ftemp = strchr (r_from, '>')) != NULLCHAR)
						*ftemp = 0;
				}
				if ((ftemp = strpbrk (r_from, " \t")) != NULLCHAR)
                	*ftemp = 0;
/*
 * 11Jun2020, Maiko (VE4KLM), lets scrap this stripping section
 * for a while, I never liked this, perhaps just leave it alone
 * and make some observations for the time being, after which we
 * can figure out a strategy IF it even needs to be figured out.
 */
#ifdef DONT_COMPILE

			/*
			 * 27Apr2011, Maiko (VE4KLM), I would like to stay consistent with
			 * the 'from' info we get from the older style FBB forwarding, so
			 * it is very important to strip off any suffixes here. I might
			 * have to fool around with this abit. I discovered this during
			 * testing, the final 'From:' field was getting way too crazy !
			 */
				if ((fptr = strpbrk (r_from, "@")))
					*fptr = '\0';

				/* 12Nov2013, Maiko, Strip off 'internal' bbs addressing */
				if ((fptr = strpbrk (r_from, "%")))
					*fptr = '\0';

				log (m->user, "stripped [%s]", r_from);
#endif
				fromfilledin = 1;
			}
		}

		if (!strncmp (m->line, "Cc: ", 4))
			addlist (&cclist, m->line + 4, 0, NULLCHAR);

		if (!strncmp (m->line, "Body: ", 6))
			bodysize = atoi (m->line + 6);

		if (!strncmp (m->line, "File: ", 6))
		{
			if (numafiles == MAXAFILES)
			{
				log (m->user, "max # of attachments (%d) exceeded", MAXAFILES);
			}
			else
			{
				char *ptr = m->line + 6;

				afiles[numafiles].size = atoi (ptr);
				ptr = strchr (ptr, ' ') + 1;
				afiles[numafiles].name = j2strdup (ptr);

				if (debugB2F)
				{
					log (m->user, "size %d name %s",
						afiles[numafiles].size,
							afiles[numafiles].name);
				}

				numafiles++;
			}
		}
	}

	/*
	 * 25Jul2014, Maiko, If 'To:' and/or 'From:' have not been found
	 * then we need to stop, or else JNOS will crash and even then the
	 * message is malformed, so it should not even go any further.
	 */
	if (!tofilledin)
	{
		log (m->user, "email header missing To: field");
		return 0;
	}

	if (!fromfilledin)
	{
		log (m->user, "email header missing From: field");
		return 0;
	}

	/*
	 * 09Feb2011, Maiko (VE4KLM), We need to setup a few items similar to
	 * what is done just before the call to the regular dofbbacceptnote() or
	 * else things don't get rewritten properly or put into proper areas.
	 */
	r_rewrite_to = rewrite_address (r_to, REWRITE_TO);

	if (r_rewrite_to != NULLCHAR)
	{
		strlwr (m->to = j2strdup (r_rewrite_to));
		strlwr (m->origto = j2strdup (r_to));
	}
	else strlwr (m->to = j2strdup (r_to));

	if (validate_address (m->to) == 0)
		log (m->user, "validate_address [%s] failed", m->to);

	strlwr (m->tofrom = j2strdup (r_from));

	if (debugB2F)
		log (m->user, "writing headers");

	/* write regular smtp headers */
	mbx_data (m, cclist, rhdr);

	/* finish smtp headers */
	fprintf (m->tfile, "\n");

	if (debugB2F)
		log (m->user, "writing body");

	obuf = malloc (9999);

	/* log (-1, "starting bodysize %d", bodysize); */

	while (bodysize > 0)
	{
		if (fgets (obuf, MBXLINE, fpInFile) == NULL)
		{
			log (m->user, "body ended earlier than expected - not an error");

			/* 10May2010, Maiko, Should check for EOF, since it *seems* that
			 * the Body: value is not always correct (2 more than actual data)
			 * which I've reported to Rick to see what he thinks about it.
			 */
			break;
		}
	/*
	 * 10May2010, Maiko, It should be noted that the decode (decompressed)
	 * file from RMS Express or WL2K have lines terminated with 0x0d 0x0a,
	 * which counts as two bytes, and ARE included in the 'Body:' value, so
	 * the rip() call needs to be done AFTER the bodysize calculations ...
	 */
		bodysize -= strlen (obuf);

		/* log (-1, "bodysize %d", bodysize); */

		if (bodysize < 0)
			log (-1, "**** NEGATIVE BODY SIZE ***");

		rip (obuf);

		fputs (obuf, m->tfile);
		fputs ("\n", m->tfile);
	}

	for (cnt = 0; cnt < numafiles; cnt++)
	{
		fgets (m->line, MBXLINE, fpInFile);	/* separated by CR */

		rip (m->line);

		if (debugB2F)
			log (m->user, "separator [%s]", m->line);

		if (m->line[0] != 0)
			log (-1, "expecting newline separator");

		pwait (NULL);

		if (debugB2F)
			log (m->user, "writing attachment %d", cnt+1);
		/*
		 * 15May08, Maiko (VE4KLM), changed to use the path from ftpusers,
		 * previously I had started off using /tmp as the root directory,
		 * but really we should use the homedir for the current user.
		 */
		if (m->path)
		{
			extern char *firstpath (char*);

			char *fp = firstpath (m->path);
			sprintf (obuf, "%s/%s", fp, afiles[cnt].name);
			free (fp);
		}
		else
			sprintf (obuf, "/tmp/%s", afiles[cnt].name);

		log (-1, "attachment file [%s]", obuf);

		free (afiles[cnt].name);	/* allocated earlier using j2strdup */

		/* 07Apr2008, Maiko, recipient needs to know where file is */
		fprintf (m->tfile, "\nattachment : %s\n", obuf);

		if ((ofp = fopen (obuf, "wb")) == NULLFILE)
			log (-1, "unable to open attachment file");

		filesize = afiles[cnt].size;

		log (-1, "writing to file - filesize %d", filesize);

		while (filesize > 0)
		{
			size_t flen, nummem;

			nummem = (size_t)(min (filesize, 9990));

			flen = fread (obuf, 1, nummem, fpInFile);

  		/* 13Jan2018, Maiko, Oops forgot about end of file or error */
			if (flen == 0) break;

			else if (flen != nummem)
			{
				log (-1, "short read %d/%d", (int)flen, (int)nummem);
			}
			else if (!flen)
			{
				log (-1, "forget it - nothing read");
				break;
			}

			filesize -= (int)flen;

   			if (fwrite (obuf, 1, flen, ofp) != flen)
			{
				log (-1, "forget it - fwrite failed");
				break;
			}
		}

		fclose (ofp);
	}

    fclose (fpInFile);

	free (obuf);

    free (rhdr);

/*
    log (-1, "m->to [%s] m->origto [%s] m->tofrom [%s]",
	m->to, m->origto, m->tofrom);
*/

    if ((host = strrchr(m->to,'@')) == NULLCHAR) {
       host = Hostname;        /* use our hostname */
       if(m->origto != NULLCHAR) {
          /* rewrite_address() will be called again by our
           * SMTP server, so revert to the original address.
           */
          free(m->to);
          m->to = m->origto;
          m->origto = NULLCHAR;
       }
    }
    else
       host++; /* use the host part of address */

	/* log (-1, "origbbs [%s]", m->origbbs); */

    /* make up full from name for work file */
    if(m->tofrom != NULLCHAR)
        sprintf(fullfrom,"%s%%%s@%s",m->tofrom, (m->origbbs!=NULLCHAR)?m->origbbs:m->name, Hostname);
    else
       sprintf(fullfrom,"%s@%s",m->name,Hostname);
    if(cclist != NULLLIST && stricmp(host,Hostname) != 0) {
       fseek(m->tfile,0L,0);   /* reset to beginning */
       queuejob(m->tfile,Hostname,cclist,fullfrom);
       del_list(cclist);
       cclist = NULLLIST;
    }
    addlist(&cclist,m->to,0,NULLCHAR);
    fseek(m->tfile,0L,0);
    queuejob(m->tfile,host,cclist,fullfrom);
    del_list(cclist);
    MYFCLOSE(m->tfile);
    MbRecvd++;

    return 1;
}
#endif	/* B2F */

/*****************************************************************************\
 * This code is called after a FBB type message has been decoded. It it based
 * on the dosend() code in mboxmail.c. This code reads the message from a file
 * instead of a user's socket so no user interaction is performed.
 *
 * return codes:   0 => fatal error, should disconnect
 *                 1 => all OK
 *
 *
 * It should be possible to use/modify this code ( in the future ) to process
 * FBB style import message files.
 *
 * Also for code simplicty and code reduction.... I'd like to see the dosend()
 * code call this to process a file received from a user.
 *
\*****************************************************************************/

static int dofbbacceptnote(struct mbx *m, char *infile)
{
    FILE   *fpInFile;
    char   *cp2;
    char   *host, *cp, fullfrom[MBXLINE], *rhdr = NULLCHAR;
    struct list *cclist = NULLLIST;

#ifdef RLINE
    struct tm t;
#define  ODLEN   16
#define  OBLEN   32
    char tmpline[MBXLINE];
    char fwdbbs[NUMFWDBBS][FWDBBSLEN+1];
    int  myfwds  = 0;
    int  i;
    int  zulu=0;
    int  check_r = 0;
    int  found_r = 0;
    char origdate[ODLEN+1];
    char origbbs[OBLEN+1];
    int  loops   = 0;
    char Me[15];

    origdate[0]  = '\0';
    origbbs[0]   = '\0';
#endif

    /* Now check to make sure we can create the needed tempfiles - WG7J */
    if((m->tfile = tmpfile()) == NULLFILE)
        return 0;

    if ((fpInFile = fopen(infile, "r+b")) == NULLFILE) {
        log(m->user, "dofbbacceptnote: error %d opening %s", errno, infile);
        return 0;  /* exitbbs() will close m->tfile */
    }

#ifdef RLINE
    /* Only accept R: lines from bbs's */
    if((Rdate || Rreturn || Rfwdcheck || Mbloophold)){
        /* Going to interpret R:headers,
         * we need another tempfile !
         */
        if((m->tfp = tmpfile()) == NULLFILE) {
            /* disconnect to avoid the other bbs to think that we already have
             * the message !
             */
            fclose(fpInFile);
            return 0;    /* exitbbs() will close m->tfile */
        }
        /* Now we got enough :-) */
        check_r = 1;
        Checklock++;
        /* Set the call, used in loop detect code - WG7J */
        if(Mbloophold) {
           strncpy(Me,(Mbhaddress?Mbhaddress:Hostname),sizeof(Me));
           Me[sizeof(Me)-1] = '\0';

           if((cp = strchr(Me,'.')) != NULLCHAR)
              *cp = '\0'; /* use just the callsign */
        }
    }
#endif

#ifdef RLINE
	if (!check_r)
	{
#endif
		mbx_data(m,cclist,rhdr);
		/*Finish smtp headers*/
		fprintf(m->tfile,"\n");
#ifdef RLINE
	}
#endif

    m->state = MBX_DATA;


    while(fgets(m->line, MBXLINE, fpInFile))
	{

       // Strip off any trailing EOL chars.
       rip(m->line);

		if(m->line[0] != CTLZ && stricmp(m->line, "/ex"))
		{
#ifdef RLINE
			if(check_r)
			{
             /* Check for R: lines to start with */
             if(!strncmp(m->line,"R:",2))
			 {
				 /*found one*/
                found_r = 1;
                /*Write this line to the second tempfile
                 *for later rewriting to the real one
                 */
                fprintf(m->tfp,"%s\n",m->line);
                /* Find the '@[:]CALL.STATE.COUNTRY'or
                 * or the '?[:]CALL.STATE.COUNTRY' string
                 * The : is optional.
                 */
                if(((cp=strchr(m->line,'@')) != NULLCHAR) ||
                   ((cp=strchr(m->line,'?')) != NULLCHAR) ) {
                   if((cp2=strpbrk(cp," \t\n")) != NULLCHAR)
                      *cp2 = '\0';
                   /* Some bbs's send @bbs instead of @:bbs*/
                   if(*++cp == ':')
                      cp++;
                   /* if we use 'return addres'
                    * copy whole 'domain' name
                    */
                   if(Rreturn)
                      if(strlen(cp) <= OBLEN)
                         strcpy(origbbs,cp);
                   /* Optimize forwarding ? */
                   if(Rfwdcheck || Mbloophold) {
                      /*if there is a HADDRESS, cut off after '.'*/
                      if((cp2=strchr(cp,'.')) != NULLCHAR)
                         *cp2 = '\0';
                      if(Mbloophold)
                         /* check to see if this is my call ! */
                         if(!stricmp(Me,cp))
                            loops++;
                      /*cross-check with MyFwds list*/
                      if(Rfwdcheck) {
                         for(i=0;i<Numfwds;i++) {
                             if(!strcmp(MyFwds[i],cp)) {
                                /*Found one !*/
                                strcpy(fwdbbs[myfwds++],cp);
                                break;
                             }
                         }
                      }
                   }
                }
                if(Rdate) {
                   /* Find the 'R:yymmdd/hhmmz' string */
                   if((cp=strchr(m->line,' ')) != NULLCHAR) {
                      *cp = '\0';
                      if(strlen(m->line+2) <= ODLEN)
                         strcpy(origdate,m->line+2);
                   }
                }
        	}
			else
			{
                /* The previous line was last R: line
                 * so we're done checking
                 * now write the smtp headers and
                 * all saved R: lines to the right tempfile
                 */
                check_r = 0;
                Checklock--;
                /*Did we actually find one ?*/
                if(found_r) {
                   if(Rreturn)
                      m->origbbs = j2strdup(strlwr(origbbs));
                   if(Rdate) {
                      if((cp=strchr(origdate,'/')) != NULLCHAR) {
                         if((*(cp+5) == 'z') || (*(cp+5) == 'Z')) {
                            *(cp+5) = '\0';
                            zulu = 1;
                         }
                         t.tm_min = atoi(cp+3);
                         *(cp+3) = '\0';
                         t.tm_hour = atoi(cp+1);
                         *cp = '\0';
                         t.tm_mday = atoi(&origdate[4]);
                         origdate[4] = '\0';
                         t.tm_mon = (atoi(&origdate[2]) - 1);
                         origdate[2] = '\0';
                         t.tm_year = atoi(origdate);
                         /* Set the date in rfc 822 format */
                         if((unsigned)t.tm_mon < 12) {  /* bullet-proofing */
                             m->date = mallocw(40);
                             sprintf(m->date,"%.2d %s %02d %02d:%02d:00 %.3s\n",
                                 t.tm_mday, Months[t.tm_mon], t.tm_year,
                                 t.tm_hour, t.tm_min, zulu ? "GMT" : "");
                         }
                      }
                   }
                }
                /* Now write the headers,
                 * possibly adding Xforwarded lines for bulletins,
                 * or anything that has a BID.
                 * Add the X-Forwarded lines and loop detect
                 * headers FIRST,
                 * this speeds up forwarding...
                 */
                if(Mbloophold && loops >= Mbloophold)
                    fprintf(m->tfile,"%sLoop\n",Hdrs[XBBSHOLD]);
                if(Rfwdcheck && found_r && ((m->stype == 'B') || (m->tomsgid)) ){
                   /*write Xforwarded headers*/
                   for(i=0;i<myfwds;i++) {
                       fprintf(m->tfile,"%s%s\n",Hdrs[XFORWARD],fwdbbs[i]);
                   }
                }
           	    /*write regular headers*/
           	    mbx_data(m,cclist,rhdr);
           	    /* Finish smtp headers */
           	    fprintf(m->tfile,"\n");
                /* Now copy the R: lines back */
                if(found_r) {
                   rewind(m->tfp);
                   while(fgets(tmpline,sizeof(tmpline),m->tfp)!=NULLCHAR)
                      fputs(tmpline,m->tfile);
                }
                MYFCLOSE(m->tfp);

                /* And add this first non-R: line */
                fprintf(m->tfile,"%s\n",m->line);
                if(m->line[strlen(m->line)-1] == CTLZ)
				{
					/* replaces GOTO eol_ctlz label */
					do_eol_ctlz (check_r, m);
					continue;
				}
             }
          }
		  else
#endif
             fprintf(m->tfile,"%s\n",m->line);

          if(m->line[strlen(m->line)-1] == CTLZ)
		  {
			/* replaces the GOTO eol_ctlz label */
			do_eol_ctlz (check_r, m);
		  }
       }
	   else
	   {
			/* replaces the GOTO eol_ctlz label */
			do_eol_ctlz (check_r, m);
       }
    } // End While

    fclose(fpInFile);
    free(rhdr);
/*
    log (-1, "m->to [%s] m->origto [%s] m->tofrom [%s]",
		m->to, m->origto, m->tofrom);
*/
    if((host = strrchr(m->to,'@')) == NULLCHAR) {
       host = Hostname;        /* use our hostname */
       if(m->origto != NULLCHAR) {
          /* rewrite_address() will be called again by our
           * SMTP server, so revert to the original address.
           */
          free(m->to);
          m->to = m->origto;
          m->origto = NULLCHAR;
       }
    }
    else
       host++; /* use the host part of address */

    /* make up full from name for work file */
    if(m->tofrom != NULLCHAR)
        sprintf(fullfrom,"%s%%%s@%s",m->tofrom, (m->origbbs!=NULLCHAR)?m->origbbs:m->name, Hostname);
    else
       sprintf(fullfrom,"%s@%s",m->name,Hostname);
    if(cclist != NULLLIST && stricmp(host,Hostname) != 0) {
       fseek(m->tfile,0L,0);   /* reset to beginning */
       queuejob(m->tfile,Hostname,cclist,fullfrom);
       del_list(cclist);
       cclist = NULLLIST;
    }
    addlist(&cclist,m->to,0,NULLCHAR);
    fseek(m->tfile,0L,0);
    queuejob(m->tfile,host,cclist,fullfrom);
    del_list(cclist);
    MYFCLOSE(m->tfile);
    MbRecvd++;
/* dosend() invoked smtptick() [with a slight delay] at this point, to start
   processing the msgs we just added to the queue.  We could do likewise, or
   invoke smtptick() from exitbbs().  The competing concepts are: 1) speed up
   handling of the queue, so the bids get written to the history file, and
   2) minimize memory and cpu demand by delaying the smtp processing until we
   are finished.
*/

    return 1;
}
#endif /* FBBCMP */
#endif // FBBFWD
