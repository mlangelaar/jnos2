/*
 * JNOS 2.0
 *
 * $Id: smtpserv.c,v 1.8 2014/10/12 20:31:36 ve4klm Exp ve4klm $
 *
 * SMTP Server state machine - see RFC 821
 *  enhanced 4/88 Dave Trulli nn2z
 *
 * Mods by G1EMM and PA0GRI
 *
 * Index file support by WG7J
 *
 * Various Mods by VE4KLM
 *
 * --------------------------------------------------------
 *
 *  Making JNOS 2.0 MIME aware for local mailbox delivery :]
 *
 *  21Apr2022, Maiko (VE4KLM), Actually written from scratch
 *
 *  This particular date is significant, successfully tested
 *  an email from a PINE mail client, sending text plus three
 *  attachments, jpeg, jpg, and a pdf - I just need to put in
 *  the base64 decode now, then my first prototype is ready.
 *
 * 22Apr2022, Maiko (VE4KLM), AND SUCCESS, base64 decode of
 * all my test files works now, this is an excellent start.
 *
 * 29Apr2022, Maiko (VE4KLM), code to handle thunderbird bug
 * where it puts newline between Content-Type and boundary !
 * Also tested with text/html - should maybe provide option
 * to ignore (not write text/html) to the mailbox file ?
 *
 *
 * 04May2022, Maiko (VE4KLM), the mailit function is now mime
 * aware (experimental, but seems to work), rewrites the BODY
 * of any mail sent to local mailboxes. Attachments are saved
 * to local files, path defined in ftpusers per user, and any
 * BASE64 encoded content is decoded (within saved files) !
 *
 * So far - only text/plain and text/html are left as is ...
 *
 * ----------------------------------------------------------
 *
 */

#include <time.h>
#include "global.h"
#ifdef UNIX
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#ifdef HOLD_PARSER
#include <sys/wait.h>
#endif
#else
#include <dir.h>
#endif
#if defined(__STDC__) || defined(__TURBOC__)
#include <stdarg.h>
#endif
#include <ctype.h>
#include <setjmp.h>
#include "mbuf.h"
#include "cmdparse.h"
#include "socket.h"
#ifdef  LZW
#include "lzw.h"
#endif
#include "iface.h"
#include "proc.h"
#include "smtp.h"
#include "commands.h"
#include "dirutil.h"
#include "mailbox.h"
#include "mailutil.h"
#include "bm.h"
#include "domain.h"
#include "session.h"
#include "files.h"
#include "index.h"
#ifdef  NNTPS
#include "nntp.h"
#endif

#ifdef	KLM_MAILIT_MIMEAWARE		/* 04May2022, Maiko (VE4KLM), official now */

/* seems broken, and it's a non standard function everyone says
#define _GNU_SOURCE
#include <string.h>
 */
extern char *strcasestr (const char*, const char*);

#endif

char *Days[7] = {  "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
extern char *Months[];
#ifdef SMTPTRACE
extern unsigned short Smtptrace;
#endif
#ifdef HOLD_LOCAL_MSGS
extern int MbHoldLocal;
#endif

#ifdef WPAGES
/* 02Feb2012, Maiko, White Pages code from Lantz TNOS 240 source */
extern int wpserver (FILE *fp, const char *from);
extern void wpageAdd (char *entry, int bbs, int updateit, char which);
extern int MbWpages;
#endif
  
static struct list *expandalias __ARGS((struct list **head,char *user,char *origuser));
static int  getmsgtxt __ARGS((struct smtpsv *mp));
static struct smtpsv *mail_create __ARGS((void));
static void mail_clean __ARGS((struct smtpsv *mp));
static int mailit __ARGS((FILE *data,char *from,struct list *tolist,int requeue_OK));
static int router_queue __ARGS((FILE *data,char *from,struct list *to));
static void smtplog __ARGS((char *fmt,...));
static void smtpserv __ARGS((int s,void *unused,void *p));

/* 31Aug2020, Maiko (VE4KLM), already defined in mailbox.h */
// extern int msgidcheck __ARGS((char *string));
  
#ifdef SMTPSERVER
  
/* Command table */
static char *commands[] = {
    "helo",
#define HELO_CMD    0
    "noop",
#define NOOP_CMD    1
    "mail from:",
#define MAIL_CMD    2
    "quit",
#define QUIT_CMD    3
    "rcpt to:",
#define RCPT_CMD    4
    "help",
#define HELP_CMD    5
    "data",
#define DATA_CMD    6
    "rset",
#define RSET_CMD    7
    "expn",
#define EXPN_CMD    8
    "vrfy",
#define VRFY_CMD    9
#ifdef  LZW
    "xlzw",
#define XLZW_CMD    10
#endif
	/* 27Oct2020, Maiko (VE4KLM), Added EHLO, thought it was there already */
    "ehlo",
#define EHLO_CMD    11
    NULLCHAR
};
  
#ifdef STATUSWIN
int SmtpUsers;
#endif
  
int Smtpservcnt;
int Smtpmaxserv;

/* Reply messages */
static char Banner[] = "220 %s SMTP ready\n";
static char Closing[] = "221 Closing\n";
static char Ok[] = "250 Ok\n";
static char Reset[] = "250 Reset state\n";
static char Sent[] = "250 Sent\n";
static char Ourname[] = "250 %s, Share and Enjoy!\n";

/* 27Oct2020, Maiko (VE4KLM), Added EHLO, thought it was there already */
#ifdef  LZW
static char LZWOk[] = "250 %d %d LZW Ok\n";
static char Help[] = "214-Commands:\n214-HELO NOOP MAIL QUIT RCPT HELP DATA RSET EXPN VRFY XLZW EHLO\n214 End\n";
#else
static char Help[] = "214-Commands:\n214-HELO NOOP MAIL QUIT RCPT HELP DATA RSET EXPN VRFY EHLO\n214 End\n";
#endif

static char Enter[] = "354 Enter mail, end with .\n";
static char Ioerr[] = "452 Temp file write error\n";
#ifdef DEBUG500
/*static char Badcmd[] = "500 Command unrecognized: %s";*/
static char BadcmdNL[] = "500 Command unrecognized: %s\n";
#else
static char Badcmd[] = "500 Command unrecognized\n";
#endif
static char Syntax[] = "501 Syntax error\n";
static char Needrcpt[] = "503 Need RCPT (recipient)\n";
static char Unknown[] = "550 <%s> address unknown\n";

/* 22Sep2008, Maiko (VE4KLM), Relay is relay and not 'address unknown' */
#ifdef SMTP_DENY_RELAY
static char NoRelay[] = "550 <%s> relaying denied\n";
#endif
  
extern int32 SmtpServTdisc;
  
/* Start up SMTP receiver service */
int
smtp1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;
  
    if(argc < 2)
        port = IPPORT_SMTP;
    else
        port = atoi(argv[1]);

/*
 * 15Oct2009, Maiko, Indications show curses issues, STACK might not
 * be big enough for the amount of mail I have coming in !!! Let's try
 * to bump this up to 4096 just for the fun of it.
#define SMTPSTK 2048
 */  
#define SMTPSTK 4096

#if defined(NNTPS) && defined(NEWS_TO_MAIL)
#ifdef UNIX
#define SMTPSTK2 1000
#else
#define SMTPSTK2 80
#endif /* UNIX */
#else
#define SMTPSTK2 0
#endif /* defined(NNTPS) && defined(NEWS_TO_MAIL) */

#ifdef SMTP_VALIDATE_LOCAL_USERS
#define SMTPSTK3 150
#else
#define SMTPSTK3 0
#endif /* SMTP_VALIDATE_LOCAL_USERS */

#if defined(MAILCMDS) && defined(HOLD_LOCAL_MSGS)
#define SMTPSTK4 150
#else
#define SMTPSTK4 0
#endif /* defined(MAILCMDS) && defined(HOLD_LOCAL_MSGS) */

#define SMTPSTKALL	(SMTPSTK+SMTPSTK2+SMTPSTK3+SMTPSTK4)
    return start_tcp(port,"SMTP Server",smtpserv,SMTPSTKALL);
}
  
/* Shutdown SMTP service (existing connections are allowed to finish) */
int
smtp0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;
  
    if(argc < 2)
        port = IPPORT_SMTP;
    else
        port = atoi(argv[1]);
    return stop_tcp(port);
}

/* 29Dec2004, Maiko, replaced GOTO 'quit:' with this function */
static void do_quit (struct smtpsv *mp, int delay_retry)
{
    log(mp->s,"close SMTP");
    close_s(mp->s);
    mail_clean(mp);

    /* N5KNX: If we tried to deliver to an offline/unready printer, or to a
       locked mailbox, we sent back code 452/Ioerr and the job remains in the
       queue.  If we invoke smtptick() now to reprocess the queue, we'll have
       a tight loop awaiting a successful delivery.
       Instead, we'll just process the queue later, in due course [provided
       smtp timer was set].  The down side is we are less responsive processing
       non-local email if sent in the same session that incurred an Ioerr.
    */
    if (!delay_retry)
		smtptick(NULL);         /* start SMTP daemon immediately */
}

#if defined (SMTP_DENY_RELAY) && defined (SDR_EXCEPTIONS)

/*
 * 17Sep2008, Maiko (VE4KLM), By default we will deny SMTP relay, however
 * there may be outside subnets that we want to allow for. For example, I
 * use my Thunderbird email client at work to check my JNOS system for new
 * mail. It would be nice if I could reply to those systems outside my JNOS
 * box, but the existing SMTP_DENY_RELAY code prevents that. So, this new
 * code allows me to create an SDR exception list (ip/netmask) to allow.
 *
 * 02Oct2008, Maiko, Completed this code (added delete and list functions
 * to the SDR exception list). Originally I just started with sdre_add() to
 * get this working. Now you can remove entries and list on the fly.
 */

struct sdrex {

   int32 addr;
   int32 netmask;

   struct sdrex *next;
};

static struct sdrex *sdre = (struct sdrex*)0;

#ifdef	SGW_EXCEPTIONS
/* 14Dec2011, Maiko, Exception list for NEW smtp gateway mode */
static struct sdrex *sgwe = (struct sdrex*)0;
#endif

/*
 * 14Dec2011, Maiko (VE4KLM), Expand original functions so that I can use
 * them outside of the smtp deny relay exceptions, for instance I also need
 * an exception list for the new smtp gateway mode functionality.
 */
void ipexception_add (struct sdrex **lptr, char *ip_s, char *nm_s)
{
	struct sdrex *ptr = (struct sdrex*)callocw(1, sizeof(struct sdrex));

	ptr->next = *lptr;
	*lptr = ptr;

	ptr->addr = resolve (ip_s);
	ptr->netmask = htol (nm_s);
}

/*
 * 19Sep2008, Maiko (VE4KLM), Now have a link list to hold exceptions
 * 14Dec2011, Maiko, Now a stub to the new multipurpose ipexception_add ()
 */
void sdre_add (char *ip_s, char *nm_s)
{
	ipexception_add (&sdre, ip_s, nm_s);
}

/* 14Dec2011, Maiko (VE4KLM), function to add smtp gateway exceptions */
#ifdef	SGW_EXCEPTIONS
void sgwe_add (char *ip_s, char *nm_s)
{
	ipexception_add (&sgwe, ip_s, nm_s);
}
#endif

/*
 * 02Oct2008, Maiko, Now we can remove an exception from the list ...
 *
 * 14Dec2011, Maiko (VE4KLM), Expand original functions so that I can use
 * them outside of the smtp deny relay exceptions, for instance I also need
 * an exception list for the new smtp gateway mode functionality.
 */
void ipexception_delete (struct sdrex **lptr, char *ip_s, char *nm_s)
{
	struct sdrex *prev, *ptr;

	int32 addr, netmask;

	addr = resolve (ip_s);
	netmask = htol (nm_s);
	
	for (prev = ptr = *lptr; ptr; prev = ptr, ptr = ptr->next)
	{
		if (ptr->netmask == netmask && ptr->addr == addr)
			break;
	}

	if (ptr)
	{
		if (ptr == *lptr)
			*lptr = (struct sdrex*)0;
		else
 			prev->next = ptr->next;

		free (ptr);
	}
	else tprintf ("no such entry found\n");
}

/* 14Dec2011, Maiko, Now a stub to the new multipurpose ipexception_delete () */
void sdre_delete (char *ip_s, char *nm_s)
{
	ipexception_delete (&sdre, ip_s, nm_s);
}

/* 14Dec2011, Maiko (VE4KLM), function to delete smtp gateway exceptions */
#ifdef	SGW_EXCEPTIONS
void sgwe_delete (char *ip_s, char *nm_s)
{
	ipexception_delete (&sgwe, ip_s, nm_s);
}
#endif

/*
 * 02Oct2008, Maiko, Now we can display the list of exceptions ...
 *
 * 14Dec2011, Maiko (VE4KLM), Expand original functions so that I can use
 * them outside of the smtp deny relay exceptions, for instance I also need
 * an exception list for the new smtp gateway mode functionality.
 */
void ipexception_list (struct sdrex **lptr)
{
	struct sdrex *ptr;

	for (ptr = *lptr; ptr; ptr = ptr->next)
		tprintf ("%s / 0x%08x\n", inet_ntoa (ptr->addr), ptr->netmask);
}

/* 14Dec2011, Maiko, Now a stub to the new multipurpose ipexception_list () */
void sdre_list ()
{
	ipexception_list (&sdre);
}

/* 14Dec2011, Maiko (VE4KLM), function to list smtp gateway exceptions */
#ifdef	SGW_EXCEPTIONS
void sgwe_list ()
{
	ipexception_list (&sgwe);
}
#endif

/* 14Dec2011, Maiko (VE4KLM), new function to check for exception, actually
 * going to use it for a new sdre_exception() as well. When I wrote the orig
 * function to check for exception I just put the code into place, never did
 * create a function to do it. Now have one for both deny relay and gateway.
 */
int ipexception_check (struct sdrex **lptr, int32 ipaddr)
{
	struct sdrex *ptr;

	/*
	 * 19Sep2008, Maiko (VE4KLM), Have as many exceptions as you want
	 * 14Dec2011, Maiko, take original code and put into own function,
	 * then use stubs to call, passing pointer to pointer of list
	 */
	for (ptr = *lptr; ptr; ptr = ptr->next)
	{
		if (ptr->netmask && ((ptr->addr & ptr->netmask) == (ipaddr & ptr->netmask)))
			break;
	}

	return (ptr != (struct sdrex*)0);
}

/* 14Dec2011, Maiko, New function to replace code previously written */
int sdre_check (int32 ipaddr)
{
	int retval = ipexception_check (&sdre, ipaddr);

	if (retval) log (-1, "smtp [%s] allow relay", inet_ntoa (ipaddr));

	return retval;
}

#ifdef	SGW_EXCEPTIONS
/* 14Dec2011, Maiko, New function to check for gateway exceptions */
int sgwe_check (int32 ipaddr)
{
	extern int smtp_debug;	/* 11May2014, Maiko, In smtpcli.c now */

	int retval = ipexception_check (&sgwe, ipaddr);

	if (retval && smtp_debug)
		log (-1, "smtp [%s] bypass gateway", inet_ntoa (ipaddr));

	return retval;
}
#endif

#endif	/* End of SMTP_DENY_RELAY && SDR_EXCEPTIONS */

static void
smtpserv(s,unused,p)
int s;
void *unused;
void *p;
{
    struct smtpsv *mp;
    char **cmdp,buf[LINELEN],*arg,*cp,*cmd,*newaddr,*origaddr;
    struct list *ap,*list;
    int cnt, delay_retry=0;
    char address_type;
#ifdef  LZW
    extern int Smtpslzw;
    int lzwbits, lzwmode;
#endif
  
    sockmode(s,SOCK_ASCII);
    sockowner(s,Curproc);       /* We own it now */
    log(s,"open SMTP");
  
    if((mp = mail_create()) == NULLSMTPSV){
        j2tputs(Nospace);
        log(s,"close SMTP - no space or toomany");
        close_s(s);
        return;
    }
    mp->s = s;
  
    (void) usprintf(s,Banner,Hostname);

	while (1)	/* replaces GOTO 'loop:' */
	{
    /* Time-out after some inactivity time - WG7J */
    j2alarm (SmtpServTdisc*1000);
    if ((cnt = recvline(s,buf,sizeof(buf)-1)) == -1){
        /* He closed on us */
		return (do_quit (mp, delay_retry));
    }

	if (cnt > 200)
		log (mp->s, "recvline (smtpserv) len %d", cnt);

    j2alarm(0);

    rip(buf);
    cmd = buf;
  
#ifndef SMTP_ALLOW_MIXEDCASE
    /* Translate entire buffer to lower case */
#ifdef __TURBOC__
    (void)strlwr(cmd);
#else
    for(cp = cmd;*cp != '\0';cp++)
        *cp = tolower(*cp);
#endif
#endif /* SMTP_ALLOW_MIXEDCASE */
  
    /* Find command in table; if not present, return syntax error */
    for(cmdp = commands;*cmdp != NULLCHAR;cmdp++)
        if(strnicmp(*cmdp,cmd,strlen(*cmdp)) == 0)
            break;

    // log (mp->s, "command [%s]", *cmdp);

    if(*cmdp == NULLCHAR){
#ifdef DEBUG500
        usprintf(mp->s,BadcmdNL,cmd);
#else
        (void) usputs(mp->s,Badcmd);
#endif
        continue;
    }
    arg = &cmd[strlen(*cmdp)];
    /* Skip spaces after command */
    while(*arg == ' ')
        arg++;

    /* pscurproc ();	12Oct2009, Maiko, Log (debug) stack utilization */

     /* log (mp->s, "command [%s] switch [%d]", *cmdp, cmdp - commands); */

    /* Execute specific command */
    switch(cmdp-commands)
	{
#ifdef  LZW
        case XLZW_CMD:
            if(!Smtpslzw) {
#ifdef DEBUG500
                usprintf(mp->s,BadcmdNL,cmd);
#else
                usputs(mp->s,Badcmd);
#endif
            } else {
                lzwmode = lzwbits = 0;
                sscanf(arg,"%d %d",&lzwbits,&lzwmode);
                if(!((lzwmode == 0 || lzwmode == 1)
                && (lzwbits > 8 && lzwbits < 17))) {
                    lzwmode = LZWCOMPACT;
                    lzwbits = LZWBITS;
#ifdef DEBUG500
                    usprintf(mp->s,BadcmdNL,cmd);
#else
                    usputs(mp->s,Badcmd);
#endif
                } else {
                    usprintf(mp->s,LZWOk,lzwbits,lzwmode);
                    lzwinit(mp->s,lzwbits,lzwmode);
                }
            }
            break;
#endif
		/* 27Oct2020, Maiko (VE4KLM), Added EHLO, thought it was there already */
        case EHLO_CMD:
			/* flow through */
        case HELO_CMD:
            free(mp->system);
            mp->system = j2strdup(arg);
            (void) usprintf(mp->s,Ourname,Hostname);
            break;
        case NOOP_CMD:
            (void) usputs(mp->s,Ok);
            break;
        case MAIL_CMD:
            if((cp = getname(arg)) == NULLCHAR){
                (void) usputs(mp->s,Syntax);
                break;
            }
            free(mp->from);
#ifdef TRANSLATEFROM
            /* rewrite FROM address if possible */
            if((mp->from = rewrite_address(cp,REWRITE_FROM)) != NULLCHAR){
                if(!strcmp(mp->from,"refuse")){  /*n5knx: refusal needed? */
                    (void)usprintf(mp->s, Unknown, cp);
                    break;
                }
            }
            else
#endif
                mp->from = j2strdup(cp);
            (void) usputs(mp->s,Ok);
            break;
        case QUIT_CMD:
            (void) usputs(mp->s,Closing);
			return (do_quit (mp, delay_retry));
        case RCPT_CMD:  /* Specify recipient */
            if((cp = getname(arg)) == NULLCHAR || !*cp){
                (void) usputs(mp->s,Syntax);
                break;
            }
        /* rewrite address if possible */
#ifdef SMTP_REFILE
/* rewrite to: address based on <from>|<to> mapping in Refilefile */
            cmd=mallocw(strlen(mp->from)+strlen(cp)+2);
            sprintf(cmd,"%s|%s",mp->from,cp);
            if((newaddr = rewrite_address(cmd,REWRITE_FROM_TO)) == NULLCHAR
            &&
#else
            if(
#endif

		/* 16Jun2021, Maiko (VE4KLM), new send specific rules in rewrite, so we need to pass FROM field */
               //(newaddr = rewrite_address(cp,REWRITE_TO)) == NULLCHAR)
               (newaddr = rewrite_address_new(cp,REWRITE_TO, mp->from)) == NULLCHAR){
#ifdef SMTP_REFILE
                free(cmd);
#endif
                origaddr=j2strdup(cp);
            } else {  /* one of the rewrites was successful */
#ifdef SMTP_REFILE
                free(cmd);
#endif
                if(!strcmp(newaddr,"refuse")){  /*n5knx: refusal needed? */
                    (void)usprintf(mp->s, Unknown, cp);
                    free(newaddr);
                    break;
                }
                origaddr=j2strdup(cp);
                strcpy(buf,newaddr);
                cp = buf;
                free(newaddr);
            }
  
        /* check if address is ok (and short enough for us to handle) */
            if ((address_type = validate_address(cp)) == BADADDR ||
              (address_type==LOCAL && strlen(cp) > FILE_PATH_SIZE - strlen(Mailspool) - 6)){
                (void) usprintf(mp->s,Unknown,cp);
                free(origaddr);
                break;
            }
        /* if a local address check for an alias */
            if (address_type == LOCAL) {
                expandalias(&mp->to, cp, origaddr);
#ifdef SMTP_VALIDATE_LOCAL_USERS
                if(mp->to == NULLLIST) {  /* id was invalid */
                    (void) usprintf(mp->s,Unknown,cp);
                    free(origaddr);
                    break;
                }
#endif
            } else {
#ifdef SMTP_DENY_RELAY
                /* refuse to send off-host unless connecting host is in our net*/
                struct iface *ifp;
                struct sockaddr_in fsock;

                cnt = sizeof(fsock);
                if(j2getpeername(mp->s,(char *)&fsock,&cnt) == 0 && cnt != 0 && fsock.sin_family==AF_INET)
				{
                    for (ifp=Ifaces; ifp != NULLIF; ifp=ifp->next)
					{
                        if(ifp->netmask && ((ifp->addr & ifp->netmask) == ((int32)fsock.sin_addr.s_addr & ifp->netmask)))
                            break;  /* same subnet as our interface */
                    }

                    if (ifp == NULLIF)
					{
#ifdef	SDR_EXCEPTIONS
					/* 14Dec2011, Maiko (VE4KLM), new function replaces code */
						if (!sdre_check ((int32)fsock.sin_addr.s_addr))
						{
#endif
							log (mp->s, "smtp deny relay");  /* 17Sep08 New */
							/* 22Sep2008, Maiko, Use NoRelay, not Unknown */
                        	(void) usprintf(mp->s,NoRelay,cp);
                        	free(origaddr);
                        	break;
#ifdef	SDR_EXCEPTIONS
						}
#endif
                    }
                }
#endif
                /* a remote address is added to the list */
                addlist(&mp->to, cp, address_type, NULLCHAR);
            }
  
            (void) usputs(mp->s,Ok);
            free (origaddr);
            break;
        case HELP_CMD:
            (void) usputs(mp->s,Help);
            break;
        case DATA_CMD:
            if(mp->to == NULLLIST)
                (void) usputs(mp->s,Needrcpt);
            else if ((mp->data = tmpfile()) == NULLFILE)
                (void) usputs(mp->s,Ioerr), ++delay_retry;
            else
                if ((cnt=getmsgtxt(mp)) == 2) ++delay_retry;
                else if (cnt == 1) {
                    log(mp->s, "Early disconnect or timeout");
					return (do_quit (mp, delay_retry));
                }
            break;
        case RSET_CMD:
            del_list(mp->to);
            mp->to = NULLLIST;
            (void) usputs(mp->s,Reset);
            break;
        case EXPN_CMD:
        case VRFY_CMD:    /* treat same as expn (sendmail does this too) */
            if (*arg == '\0'){
                (void) usputs(mp->s,Syntax);
                break;
            }
  
            list = NULLLIST;
        /* rewrite address if possible */
		/* 16Jun2021, Maiko (VE4KLM), new send specific rules in rewrite, so we need to pass FROM field */
            //if((newaddr = rewrite_address(arg,REWRITE_TO)) != NULLCHAR)
            if((newaddr = rewrite_address_new(arg,REWRITE_TO, mp->from)) != NULLCHAR){
                if(!strcmp(newaddr,"refuse")){  /*n5knx: refusal needed? */
                    (void)usprintf(mp->s, Unknown, arg);
                    free(newaddr);
                    break;
                }
                strcpy(buf,newaddr);
                arg = buf;
                free(newaddr);
            }
            list = NULLLIST;
            expandalias(&list,arg,NULLCHAR);
#ifdef SMTP_VALIDATE_LOCAL_USERS
            if (list == NULLLIST) {
                (void)usprintf(mp->s, Unknown, arg);
                break;
            }
#endif
            ap = list;
            while (ap->next != NULLLIST){
                (void) usprintf(mp->s,"250-%s\n",ap->val);
                ap = ap->next;
            }
            usprintf(mp->s,"250 %s\n",ap->val);
            del_list(list);
            break;
    }

	} /* end of while loop that replaces GOTO 'loop:' */
}
  
extern char shortversion[];
extern char *Mbfwdinfo;
  
/* read the message text
 * This now checks for Message-Id's that could be bids
 * and deletes the message if so. - WG7J
 * Also adds Message-Id and Date headers if not already present - G8FSL
 * Returns: 0 => no errors, 1 => permanent error, 2 => temporary error (Ioerr)
 */
static int
getmsgtxt(mp)
struct smtpsv *mp;
{
    char buf[LINELEN];
    char *p;
    int  mailed_ok=0;
    int  headers=1, flag=0;
    int  prevtype = NOHEADER;
    int  continuation, continued=FALSE;
    long t;
    char *cp;
#ifdef WPAGES
    char *cp2;
	int found_R_lines = 0;	/* 13Mar2012, Maiko */
#endif
#ifdef MBFWD
    int idnotfound, len;
    char bid[LINELEN];
#endif
  
    /* Add timestamp; ptime adds newline */
    time(&t);
    fprintf(mp->data, "%s", Hdrs[RECEIVED]);  /* 05Jul2016, Maiko, compiler */
    if(mp->system != NULLCHAR)
        fprintf(mp->data,"from %s ",mp->system);
#ifdef MBFWD
    fprintf(mp->data,"by %s (%s) with SMTP\n\tid AA%ld ; %s",
    Hostname, (Mbfwdinfo != NULLCHAR) ? Mbfwdinfo : shortversion, \
    get_msgid(), ptime(&t));
#else
    fprintf(mp->data,"by %s (%s) with SMTP\n\tid AA%ld ; %s",
    Hostname, shortversion, get_msgid(), ptime(&t));
#endif
    if(ferror(mp->data)){
        (void) usputs(mp->s,Ioerr);
        return 2;  /* retryable */
    } else {
        (void) usputs(mp->s,Enter);
    }
#ifdef MBFWD
    idnotfound = 1;
#endif
    while(1){
        /* Timeout after inactivity - WG7J */
        j2alarm (SmtpServTdisc*1000);
/*17oct2009, Maiko, Hmmmm, I don't like that sizeofbuf is used, where
 * is the room for the extra null termination ??? It's like that all over
 * the place in JNOS !!!
        if(recvline(mp->s,(p=buf),sizeof(buf)) == -1){
            return 1;
        }
 */
        if((len = recvline(mp->s,(p=buf),sizeof(buf)-1)) == -1)
		return 1;

	if (len > 200)
		log (mp->s, "recvline (getmsgtxt) len %d", len);

        j2alarm(0);
        continuation = continued;  /*  handle lines longer than buf */
        cp = strchr(p, '\n');
        if(cp) { *cp = '\0'; continued = FALSE; }
        else continued = TRUE;
        /* check for end of message ie a . or escaped .. */
        if (*p == '.' && !continuation){
            if (*++p == '\0'){
#ifdef MBFWD
                /* Also send appropriate response */
                /* if duplicate BID, we just ignore the dup message.  We should NOT
                   trouble the sender when it's OUR problem, not theirs!  We get dups
                   when the same bulletin is queued 2+ times before smtpserv runs to
                   process the queue.  Since we store bids after processing, in just
                   this process, we couldn't refuse duplicates of the msg when first
                   proposed in the ax.25 forwarding process. -- N5KNX
                */
                if(mp->dupbid) {
                    (void) usputs(mp->s,Sent);  /* pretend we delivered it */
                    mailed_ok=0;
                }
                else
#endif
#ifndef RELIABLE_SMTP_BUT_SLOW_ACK
                    if(strcmp(mp->system, Hostname)) {  /* Processing our queue? */
                        (void) usputs(mp->s,Sent); /* no, send immediate positive reply */
                        if (mailit(mp->data,mp->from,mp->to, TRUE) != 0) /* then store or requeue if error */
                            log(-1,"smtpserv: msg from %s to %s+ lost; queueing error", mp->from, mp->to->val);
                        mailed_ok=0;
                    }
                    else  /* yes, we work with our own queue, so never requeue */
#endif
                    {
                        if(mailit(mp->data,mp->from,mp->to, FALSE) != 0) {
                            (void) usputs(mp->s,Ioerr);
                            mailed_ok=2;
                        }
                        else
                            (void) usputs(mp->s,Sent);
                    }
                fclose(mp->data);
                mp->data = NULLFILE;
                del_list(mp->to);
                mp->to = NULLLIST;
#ifdef MBFWD
                /* If there is a BID set, save it in the history file - WG7J */
                if(mp->bid != NULLCHAR) {
                    if(mailed_ok==0) {
                        storebid(mp->bid);
                    }
                    /* Free this bid ! */
                    free(mp->bid);
                    mp->bid = NULL;
                }
#endif
                return mailed_ok;
            }
            else if (*p != '.')
                p--;  /* bad syntax, as ALL leading periods should be doubled */
        }
        /* examine headers for Date and Message-ID lines, very important! -- G8FSL */
        if (headers) {
            int h  =  htype(p, &prevtype);

            if (h == DATE)
                flag |= 1;
            else if (h == MSGID)
                flag |= 2;
            else if (!continuation && *p == 0) {  /* end of headers? */
                if (!(flag & 1)) {
                    time(&t);
                    fprintf(mp->data,"%s%s",Hdrs[DATE],ptime(&t)); /* ptime() adds NL */
                }
                if (!(flag & 2)) {
                    fprintf(mp->data,"%s<%ld@%s>\n",Hdrs[MSGID],get_msgid(),Hostname);
                }
                headers = 0;
            }
        }
        /* for UNIX mail compatiblity */
        if (!continuation && strncmp(p,"From ",5) == 0)
            (void) putc('>',mp->data);
        /* Append to data file */
#ifdef MSDOS
        /* Get rid of Control-Z's in the line */
        cp = p;
        while(*cp) {
            if(*cp == CTLZ)
                *cp = '\n';
            cp++;
        }
#endif
        if(fputs(p,mp->data) == EOF){
            (void) usputs(mp->s,Ioerr);
            return 2;
        }
        if (!continued) putc('\n',mp->data);
        pwait(NULL);  /* let other processes run after each write to tmpfile */
#ifdef MBFWD
        /* Check for Message-Id string - WG7J */

        if(headers && idnotfound && !strnicmp(p,Hdrs[MSGID],11)) {
            if((cp = getname(p)) == NULLCHAR)
                continue;
            idnotfound = 0;
            strcpy(bid,cp);
            if((cp = strchr(bid,'@')) == NULLCHAR)
                continue;
            /* A trailing ".bbs" indicates that the Message-ID was generated
             * from a BBS style message, and not a RFC-822 message.
             */
            if(stricmp(&bid[strlen(bid) - 4], ".bbs") == 0) {
                *cp = '\0'; /*retain the bid given by user*/
                bid[12] = '\0'; /* BIDs should be no longer than 12 bytes */
                /* now check it, and save if not duplicate - WG7J */
                if((mp->dupbid = msgidcheck(mp->s, bid)) == 0)
                    mp->bid = j2strdup(bid);
            }
        }
#endif

#ifdef WPAGES
	/* 02Feb2012, Maiko, More code from Lantz TNOS 240 WPAGES */
	if (MbWpages && (!strnicmp (p, "R:", 2)) && (*(p + 8) == '/'))
	{
            /* Find the '@[:]CALL.STATE.COUNTRY'or
             * or the '?[:]CALL.STATE.COUNTRY' string
             * The : is optional.     */

		if (((cp = strchr (p, '@')) != NULLCHAR) ||
			((cp = strchr (p, '?')) != NULLCHAR))
		{
                if ((cp2 = strpbrk (cp, " \n\t")) != NULLCHAR)
                    *cp2 = '\0';
                /* Some bbs's send @bbs instead of @:bbs*/
                if (*++cp == ':')
                    cp++;
                pwait (NULL);   /* just to be nice to others */
                wpageAdd (cp, 1, 1, 'I');

				found_R_lines = 1;	/* 13Mar2012, Maiko */
		}
	}

	/*
	 * 13Mar2012, Maiko, Sometimes a 'From:' field surfaces immediately
	 * after the group of 'R: lines', so why not update user white pages
	 * while we have the chance to do it - makes sense, right ?
	 */
	if (found_R_lines && MbWpages && (!strnicmp (p, "From: ", 6)))
	{
		char tmpfline[80], *fptr = tmpfline;

		strcpy (tmpfline, p + 6);

		while (*fptr && *fptr != '@')
			fptr++;

		if (*fptr == '@')	/* very important to validate data */
		{
			fptr++;

			while (*fptr && *fptr != '.')
				fptr++;

			if (*fptr == '.')	/* very important to validate data */
			{
				*fptr = 0;	/* terminate and add to USER wp */

                wpageAdd (tmpfline, 0, 1, 'I');
			}
		}

		found_R_lines = 0;	/* only try this out one time */
	}

#endif	/* end of WPAGES */

    }
}
  
/* Create control block, initialize */
static struct smtpsv *
mail_create()
{
    register struct smtpsv *mp;
  
    if (Smtpmaxserv && Smtpservcnt >= Smtpmaxserv)
        return NULLSMTPSV;
    mp = (struct smtpsv *)calloc(1,sizeof(struct smtpsv));
    if (mp) {
        mp->from = j2strdup("");  /* Default to null From address */
        Smtpservcnt++;
#ifdef STATUSWIN
        SmtpUsers++;
#endif
    }
    return mp;
}
  
/* Free resources, delete control block */
static void
mail_clean(mp)
register struct smtpsv *mp;
{
    if (mp == NULLSMTPSV)
        return;
    Smtpservcnt--;
#ifdef STATUSWIN
    SmtpUsers--;
#endif
    free(mp->system);
    free(mp->from);
    free(mp->bid);
    if(mp->data != NULLFILE)
        fclose(mp->data);
    del_list(mp->to);
    free((char *)mp);
}
#endif /* SMTPSERVER */
  
/* General mailit function. It takes a list of addresses which have already
** been verified and expanded for aliases. Base on the current mode the message
** is place in an mbox, the outbound smtp queue or the rqueue interface
** Return 0 if successful, 1 if failure.
*/
/* Modified to touch the timestamp file for new-message tracking on local
 * deliveries... - WG7J.
 */
/* Supports mail index files - WG7J */
static int
mailit(data,from,tolist,requeue_OK)
FILE *data;
char *from;
struct list *tolist;
int requeue_OK;   /* TRUE when client host is another system */
{
    struct list *ap, *dlist = NULLLIST;
    FILE *fp;
    char mailbox[FILE_PATH_SIZE], *cp, *host, *qhost;
    int c, fail = 0;
    time_t t;
    extern int Smtpquiet;
    int index,type = 0,continued,continuation;
    long start;
    struct mailindex ind;
    char buf[LINELEN];
#if defined(MAILCMDS) && defined(HOLD_LOCAL_MSGS) && defined(UNIX) && defined(HOLD_PARSER)
    int external_hold = 0;
#endif

#ifdef	MAIL_LOG_EXTRAS
	/* 23Feb2016, Maiko, logging subject */
	char *subject = NULLCHAR;
	char *msgid = NULLCHAR;
#endif

#ifdef	KLM_MAILIT_MIMEAWARE		/* 04May2022, Maiko (VE4KLM), official now */

	/* 15Apr2022, Maiko (VE4KLM), Trying MIME decode AGAIN :) */
	char *mime_boundary = (char*)0;
	int get_mime_headers = 0;

	/* 20Apr2022, Maiko */
    char *mime_file_name = (char*)0;
    char *mime_file_code = (char*)0;
	int init_mime_file = 0;
	FILE *fp_mime_file = (FILE*)0;

	/* 21Apr2022, Maiko */
	char *mime_file_path = (char*)0;
	char *mime_temp_ptr;

#endif	/* KLM_MAILIT_MIMEAWARE */
  
    if ((Smtpmode & QUEUE) != 0)
        return(router_queue(data,from,tolist));
  
    /* scan destinations, and requeue those going to the same off-site host */
    do {
        qhost = NULLCHAR;
        for(ap = tolist;ap != NULLLIST;ap = ap->next)
            if (ap->type == DOMAIN){
                if ((host = strrchr(ap->val,'@')) != NULLCHAR)
                    host++;
                else
                    host = Hostname;
                if(qhost == NULLCHAR)
                    qhost = host;
                if(stricmp(qhost,host) == 0){
                    ap->type = BADADDR;
                    addlist(&dlist,ap->val,0,NULLCHAR);
                }
            }
        if(qhost != NULLCHAR){
            rewind(data);
            queuejob(data,qhost,dlist,from);
            del_list(dlist);
            dlist = NULLLIST;
        }
    } while(qhost != NULLCHAR);
  
#ifdef  NNTPS
    for(ap = tolist;ap != NULLLIST;ap = ap->next){
        if(ap->type != NNTP_GATE)
            continue;
        nnGpost(data,from,ap);
        ap->type = BADADDR;
    }
#endif
  
#if defined(MAILCMDS) && defined(HOLD_LOCAL_MSGS) && defined(UNIX) && defined(HOLD_PARSER)
    if (MbHoldLocal) {
        int pid, ac;
    
        rewind(data);
        switch(pid=fork()) {
        case -1:
            break;   /* fork failed */
        case 0:   /* child ... invoke HOLD_PARSER */
            if (dup2(fileno(data), 0) != -1)  /* make stdin==fileno(data) */
                execl(HOLD_PARSER, HOLD_PARSER);
            _exit(0);  /* pretend nothing found worth holding this msg */
            /*break;*/
        default:  /* parent */
            while (!waitpid (pid, &ac, WNOHANG)) {
                j2pause(100);  /* let other Jnos processes run */
            }
            if (WIFEXITED(ac)) {   /* normal exit? */
                if (WEXITSTATUS(ac))  /* non-zero exit code? */
                    external_hold=BM_HOLD;
            }
        }  /* end switch */
    }
#endif

    /* Clear the index to start */
    memset(&ind,0,sizeof(ind));
  
    index = 0;
    for (ap = tolist;ap != NULLLIST;ap = ap->next,index++)
	{
        if(ap->type != LOCAL)
		{
            ap->type = DOMAIN;
            continue;
        }

#ifdef WPAGES
	/*
	 * 13Mar2012, I see 'ap->val' is always 'ww', so this code will
	 * never get called, remember WP@WW can get rewritten to WW, so
	 * now have to figure out how to intercept the WP@WW bulls,
	 *
	 * 14Mar2012, Good, after looking around a bit, it seems the
	 * 'ap->aux' field contains the original value before it passes
	 * through REWRITE and ALIASE. So looks like I'll be able to use
	 * this part of the Lantz code after all - very nice !
	 *
	log (-1, "ap->aux [%s] ap->val [%s]", ap->aux, ap->val);
	 *
	 * 02Feb2012, Maiko, More code from Lantz TNOS 240 WPAGES except we
	 * will use 'ap->aux' instead of 'ap->val' - see above comments. The
	 * only thing I should note is I assume WP updates will always come
	 * in as a 'To: wp@ww' - might want to watch for that (14Mar2012).
	 *
	 * 28Oct2019, Maiko, Okay, I've decided to define a new virtual area
	 * called 'whitepages', basically something to trigger JNOS into doing
	 * white page (WP) processing. Makes total sense to use the flexibility
	 * offered with the rewrite file to help process WP information. Using
	 * the 'ww' area will confuse and possible complicate things, since it
	 * is a physically real area devoted to bulletins of all kinds.
	 *
	 * --- other notes ---
	 *
	 * take advantage of rewrite, since I have been getting wp@eu, wp@ww,
	 * wp@ve3cgr.#scon.on.can.noam, and my own wp@ve4klm.#wpg.mb.noam, and
	 * probably other combos. SO perhaps best way to tell JNOS to process
	 * instead of forward is to create a generice 'whitepage' rewrite area,
 	 * then tell JNOS how to process stuff, yeah I like that, so for my
	 * system, ie :
	 *
	 *  wp@eu       whitepages
	 *  wp@ww       whitepages
	 *  wp@ve4klm*  whitepages
	 *
	 * In the code I would look @ 'val' field and trigger on 'whitepages',
	 * simple enough. I think the earlier code actually did that for 'ww',
	 * so really all I'm doing is changing the rewrite area to look for.
	 *
	 * If local WP, then leave here (check for NULL ap->aux value)
	 *  if (ap->aux && !stricmp (ap->aux, "wp@ww"))
	 *
	 */

        if (ap->val && !stricmp (ap->val, "whitepages"))
	{
            if (wpserver (data, from))
                continue;   /* if handled by wpserver() */
        }
#endif
        rewind(data);
        /* strip off host name of LOCAL addresses */
        if ((cp = strchr(ap->val,'@')) != NULLCHAR)
            *cp = '\0';
  
        /* replace '\' and '.' with '/', and create subdirs.
         * this allows mailing into subdirs of spool/mail - WG7J
         */
        for(cp=ap->val;*cp != '\0'; cp++)
            if((*cp == '.') || (*cp == '\\') || *cp == '/') {
                /* Now create sub directories in the message name - WG7J */
                *cp = '\0';
                sprintf(buf,"%s/%s",Mailspool,ap->val);
#ifdef UNIX
                mkdir(buf, (mode_t)0755);
#else
                mkdir(buf);
#endif
                *cp = '/';
            }
  
        /* if mail file is busy, either (1) save it in our smtp queue
         * and let the smtp daemon try later [yields a tight retry loop
         * if we were processing our own queue, hence the requeue_OK flag]
         * or (2) mark it as failed, so sender will retry later on.  This
         * could yield dupes for msgs with multiple destinations.
         */
        if (mlock(Mailspool,ap->val)){
            if (requeue_OK) {
                addlist(&dlist,ap->val,0,NULLCHAR);
                fail = queuejob(data,Hostname,dlist,from);
                del_list(dlist);
                dlist = NULLLIST;
            }
            else
                fail=1;
        } else {
            int tocnt = 0, prevtype = NOHEADER;

            SyncIndex(ap->val); /* ensure index file is current */
            sprintf(mailbox,"%s/%s.txt",Mailspool,ap->val);
#ifndef AMIGA
            if((fp = fopen(mailbox,APPEND_TEXT)) != NULLFILE){
#else
                if((fp = fopen(mailbox,"r+")) != NULLFILE){
#endif
		// log (-1, "15Apr2022 file [%s]", mailbox);
  
                    default_index(ap->val,&ind);
                    time(&t);
                    fseek(fp,0,SEEK_END);
                    start = ftell(fp);
                    fprintf(fp,"From %s %s",from,ctime(&t));
                    host = NULLCHAR;
  
		// log (-1, "15Apr2022 headers");

                    c = continued = 0;
                    while(fgets(buf,sizeof(buf),data) != NULLCHAR){
                        continuation = continued;
                        continued = (strchr(buf,'\n') == NULLCHAR);
                        c++;   /* count header lines (roughly) */
                        if(!continuation && buf[0] == '\n'){
                        /* End of headers */
                            if(tocnt == 0) {
                                fprintf(fp,"%s%s\n", Hdrs[APPARTO], ap->val);
                                free(ind.to);
                                ind.to = j2strdup(ap->val);
                            }
                            fputc('\n',fp);
                            break;
                        }
                        type = htype(buf, &prevtype);
                    /* assure a unique Message-id for each To: addr - from KO4KS/KD4CIM */
                        if(index && (type == MSGID)) {  /* since we may use it for bids */
                            if((cp=strstr(buf,Hostname))!=NULLCHAR && *--cp=='@') /* if it came from us */
                                sprintf(buf,"%s<%ld@%s>\n",Hdrs[MSGID],get_msgid(),Hostname);
                        }
                        fputs(buf,fp);
                        set_index(buf,&ind,type);
                        switch(type){
                            case RECEIVED:
                                if (c==2 && buf[0]=='\t') {  /* only want our Received: line, first continuation */
                                    if((cp=strstr(buf,"AA")) != NULLCHAR)
                                        ind.msgid = atol(cp+2); /* message-number follows AA */
                                    if((cp=strchr(buf,';')) != NULLCHAR)
                                        ind.mydate = mydate(cp+2);
                                }
                                break;
                            case TO:
                                /* For multiple-addressees (to/cc) we should not use the first To:
                                 * address in the index.  But ap->val is an area, so see if we kept
                                 * the original value before rewrite().
                                 */


#ifndef	WHY_ARE_WE_DOING_THIS
                                if (ap->aux) { /* n5knx: original destination preferred here */


                                    free(ind.to);
                                    ind.to = j2strdup(ap->aux);
									// log (-1, "smtpserv TO set to aux [%s]", ap->aux);
                                    /* we ought to fudge To: and Cc: lines in case sysop re-indexes
                                       but for now let's ignore that little problem */
                                }
#endif
								// log (-1, "smtpserv TO [%s]", ind.to);

#ifndef MOVE_THIS_INTO_SETINDEX
                                if ((cp=strchr(ind.to,'@')) != NULLCHAR && !stricmp(cp+1,Hostname)) {
                                    *cp='\0'; /* but strip @our_hostname */
									// log (-1, "smtpserv strip TO [%s]", ind.to);
                                    if ((cp = strrchr(ind.to,'%')) != NULLCHAR) *cp = '@';
									// log (-1, "smtpserv add @ ? [%s]", ind.to);
                                }
#endif
                                /* fall into CC: case */
                            case CC:
                                ++tocnt;
                                break;
#ifdef TRANSLATEFROM
                            case FROM:
                                if ((cp = getaddress(from,1)) != NULLCHAR) {
                                    free(ind.from);
                                    ind.from = j2strdup(cp);
                                }
                                break;
#endif
#ifdef	MAIL_LOG_EXTRAS
							/* 23Feb2016, Maiko, logging subject in maillog */
                            case SUBJECT:
				/* 01Sep2016, Maiko (VE4KLM), Oops, should not be freeing these (crash)
                                	free (subject);
 				*/
                                subject = j2strdup (buf);
                                break;
#endif
                            case RRECEIPT:
                                if((cp = getaddress(buf,0)) != NULLCHAR){
                                    free(host);
                                    host = j2strdup(cp);
                                }
                                break;

#ifdef	KLM_MAILIT_MIMEAWARE		/* 04May2022, Maiko (VE4KLM), official now */

					/*
					 * 15Apr2022, Maiko (VE4KLM), Trying once again to support
					 * decoding of MIME formatted emails. I actually began the
					 * process in the summer of 2021, trying to support base64
					 * encoded emails from android, which worked, but only for
					 * very specific environments. Then I lost interest. Other
					 * things in life seemed more important at the time ...
					 */
							case CONTENT_TYPE:

								// log (-1, "15Apr2022 content type [%s]", buf);
							/*
							 * due to the way the entry appears in the headers
							 * this CONTENT_TYPE case is passed through twice,
							 * first time to set content type, second time to
							 * grab the boundary string that separates all of
							 * the MIME encoded portions of the body. NOT !
							 */
								if (strcasestr (buf, "multipart"))
								{
									int safety = 2;	/* 04May2022, Maiko */

									/*
									 * 29Apr2022, Maiko (VE4KLM), Thunderbird for some reason is putting
									 * a newline between Content-Type and boundary in the email header,
									 * similar to the DKIM signature S/MIME bug from some time ago ?
								 	 *
 									 * So my solution is to immediately do another fgets if boundary
									 * is missing, in the hope it will be there, that's why this while
									 * loop was introduced - this works (for now).
									 *
									 * 04May2022, Maiko (VE4KLM), Added a safety breakout counter :|
									 */ 
									while (safety > 0)
									{
										if ((cp = strcasestr (buf, "boundary=")))
										{
											if ((cp = strchr (cp, '\"')))
											{
												mime_boundary = j2strdup (cp+1);

												if (!strtok (mime_boundary, "\""))
													log (-1, "15Apr2022 MIME boundary missing end delimiter");
											}
											else
												log (-1, "15Apr2022 MIME boundary missing start delimiter");

											break;	/* make sure to break out of while loop if boundary found */
										}
										else
										{
											log (-1, "15Apr2022 MIME boundary missing or thunderbird bug");
                    						fgets (buf, sizeof(buf), data);

											safety--;	/* 04May2022, Maiko, only one chance at it */
										}
									}

									log (-1, "15Apr2022 MIME boundary [%s]", mime_boundary);
								}

								break;
#endif	/* KLM_MAILIT_MIMEAWARE */

                            case MSGID:
#ifdef	MAIL_LOG_EXTRAS
								/* 23Feb2016, Maiko, logging msgid in maillog */
				/* 01Sep2016, Maiko (VE4KLM), Oops, should not be freeing these (crash)
                                	free (msgid);
				 */
                                msgid = ind.messageid;     /* actually set by set_index (), don't use j2strdup (buf); */
#endif

#if defined(MAILCMDS) && defined(HOLD_LOCAL_MSGS)
                                if(MbHoldLocal) {
                                    if(isheldarea(ap->val) &&
                                       (cp = getaddress(buf,0)) != NULLCHAR){
                                        if(strcmp(cp+strlen(cp)-4,".bbs")) /* not from a bbs */
                                            ind.status |= BM_HOLD;
                                       }
#if defined(UNIX) && defined(HOLD_PARSER)
                                    /* Apply possible 'external' hold regardless of msg origin */
                                    ind.status |= external_hold;
#endif  /* HOLD_PARSER */
                                }
#endif
                                break;
                        }
                        pwait(NULL);  /* let other processes run */
                    }

				// log (-1, "15Apr2022 end of headers, body");

                /* Now the remaining data */

#ifdef	KLM_MAILIT_MIMEAWARE		/* 04May2022, Maiko (VE4KLM), official now */

                    while(fgets(buf,sizeof(buf),data) != NULLCHAR)
					{

					/*
					 * 15Apr2022, Maiko, and NOW I remember why I had so much trouble
					 * with this last summer, I kept forgetting fread is used here, so
					 * buf contains BLOCKS of data, multiple lines, and possibility of
					 * incomplete lines at the end of the block. Not line by line, so
					 * data that I am looking for might be fragmented between reads,
					 * and that caused me major grief. Debugging shows this clearly.
					 * 
					 * Using debugs during development is quite educational, you learn
					 * by getting tossed into the fire, I tend to design this way, and
					 * not using ven diagrams or logical step diagrams and so no :|
					 *
					 * You can clearly see the presence of '\n' in the debugging, so
					 * will need to add some code to deal with the new lines, and any
					 * continuation of partials lines - if that makes any sense, OR
					 * just replace fread with fgets (which 'should' work) ...
					 *
					 */
						// log (-1, "15Apr2022 body line [%s]", buf);

						if (mime_boundary && strcasestr (buf, mime_boundary))
						{
							log (-1, "15Apr2022 mime boundary");
							get_mime_headers = 1;

							fputs ("-------------------------\n", fp);	/* 21Apr2022, Maiko */
						}
						else if (get_mime_headers == 1)
						{
							fputs (buf, fp);	/* write all the MIME headers to mailbox actually */

							if (strcasestr (buf, "Content-Type: "))
							{
								char *ctptr = buf + 14;
						/*
						 * 19Apr2022, Maiko, some examples from a PINE client
						 *	

					Content-Type: TEXT/PLAIN; format=flowed; charset=US-ASCII
					Content-Type: APPLICATION/octet-stream; name=DSC_0369_proc.jpg

					Content-Type: IMAGE/JPEG; name=TTT.jpg
					Content-Transfer-Encoding: BASE64
					Content-ID: <alpine.LNX.2.11.2204192214100.23731@slackware.localdomain>
					Content-Description: ttt comment
					Content-Disposition: attachment; filename=TTT.jpg
                     [ space denotes end of mime headers ]

						 *
						 */
								if (strcasestr (ctptr, "application/octet-stream"))
									log (-1, "15Apr2022 unknown binary file");

								if (strcasestr (ctptr, "image/"))
									log (-1, "15Apr2022 image");

								if (strcasestr (ctptr, "text/"))
									log (-1, "15Apr2022 text");

								/* 20Apr2022, Maiko */
								if ((mime_temp_ptr = strcasestr (ctptr, "name=")))
								{
									log (-1, "20Apr2022 name [%s]", mime_temp_ptr + 5);

									/* You have to do this, or crash later */
									mime_file_name = j2strdup (mime_temp_ptr + 5);

									rip (mime_file_name);	/* 21Apr2022, Maiko, remove new line, cr, lf */
								}
							}

							/* 20Apr2022, Maiko */
							else if ((mime_temp_ptr = strcasestr (buf, "Content-Transfer-Encoding: ")))
							{
									log (-1, "20Apr2022 encoding [%s]", mime_temp_ptr + 27);

									/* same thing, you have to do this, or crash later */
									mime_file_code = j2strdup (mime_temp_ptr + 27);

									rip (mime_file_code);	/* 21Apr2022, Maiko, remove new line, cr, lf */
							}

							/* 20Apr2022, blank link separates mime headers from mime content */
							else if (*buf == '\n')
							{
								log (-1, "20Apr2022 end of mime headers");
								get_mime_headers = 0;
							}
							else
								log (-1, "20Apr2022 unknown mime header");
						}
						else	/* 20Apr2022, Maiko, deal with sole or mime body */
						{
							if (mime_file_name)		/* if files, attachments, whatever, we should
													 * still tell them where the file is, and we
													 * need to create and dump to that file now.
													 */
							{
								if (!init_mime_file)
								{
									char *directory = (char*)0;	/* 21Apr2022, Maiko */
									int dirlen = 0;				/* 22Apr2022, Maiko */
									char *sptr;

									log (-1, "20Apr2022 create new mime file");

									/* 04May2022, Maiko, 4th arg is pointer to long, use NULL, not NULLCHARP */
								    if (userlookup (ap->val, NULLCHARP, &directory, NULL, NULL))
									{
										if (directory)
											dirlen = strlen (directory);
									}
									else log (-1, "22Apr2022 saved to JNOS root area, no user path");

									sptr = mime_file_path = mallocw (dirlen + strlen (mime_file_name) + 5);

									if (directory)
										sptr += sprintf (sptr, "%s/", directory);

									sprintf (sptr, "%s", mime_file_name);

									log (-1, "21Apr2022 saving to [%s]", mime_file_path);
									fp_mime_file = fopen (mime_file_path, "w");

									/* 21Apr2022, Maiko, indicate existence of new file in mailbox msg */
									fprintf (fp, "Saved to : %s\n", mime_file_path);

									/*
									 * 22Apr2022, Maiko, I need the path for later, incase I need to read
									 * the file again for base64 or whatever decoding.
									 *
									 free (mime_file_path);
									 */

									init_mime_file = 1;
								}

								if (*buf == '\n')
								{
									log (-1, "20Apr2022 end of mime data, close mime file");
									fclose (fp_mime_file);

									free (mime_file_name);		/* important !!! */
									mime_file_name = (char*)0;

									init_mime_file = 0;		/* 21Apr2022, Maiko, very important */

									/* 21Apr2022, Afternoon Time - get BASE64 decode working */
									if (mime_file_code && strcasestr (mime_file_code, "BASE64"))
									{
										FILE *tmpRfp, *tmpWfp;

										char *mime_file_path_tmp;	/* 22Apr2022, Maiko */

										log (-1, "22Apr2022 base64 decoding file contents");

										/* 22Apr2022, just rename the saved file to a .tmp, easiest */
										mime_file_path_tmp = mallocw (strlen (mime_file_path) + 5);
										sprintf (mime_file_path_tmp, "%s.tmp", mime_file_path);

										if (rename (mime_file_path, mime_file_path_tmp))
											log (-1, "22Apr2022, error renaming file for base64 decode");

										if ((tmpRfp = fopen (mime_file_path_tmp, "r")) == (FILE*)0)
											log (-1, "22Apr2022 can't open base64 read file");

										if ((tmpWfp = fopen (mime_file_path, "w")) == (FILE*)0)
											log (-1, "22Apr2022 can't open base64 write file");

										/* Use fget instead of fread, so we can decode in small chunks */
                    					while (fgets (buf, sizeof (buf), tmpRfp) != NULLCHAR)
										{
    										char *decoded;
											size_t decodedlen;

											extern int base64_decode_alloc (const char*, size_t, char**out, size_t*);

											// log (-1, "base64 buf [%s]", buf);

											rip (buf);	/* 22Apr2022, Maiko, the newline will cause decode failure :] */

											// log (-1, "base64 buf [%s]", buf);

											if (!base64_decode_alloc (buf, (size_t)(strlen(buf)), &decoded, &decodedlen))
												log (-1, "21Apr2022 damn, not able to decode base64");
											else
											{	
												fwrite (decoded, 1, decodedlen, tmpWfp);	/* BUT write binary using fwrite */
												free (decoded);
											}
										}

            							fclose (tmpRfp);
            							fclose (tmpWfp);

										free (mime_file_code);		/* important !!! */
										mime_file_code = (char*)0;

									/* 22Apr2022, Maiko, Important to remove tmp file and free memory */
										unlink (mime_file_path_tmp);
										free (mime_file_path_tmp);
									}

									free (mime_file_path);	/* free this regardless of base64 decode or not */
								}
								else
									fputs (buf, fp_mime_file);
							}
							else fputs (buf, fp);	/* only write human readable text */
						}

                        pwait(NULL);  /* let other processes run */
                    }

#else	/* KLM_MAILIT_MIMEAWARE */

                    while((c = fread(buf,1,sizeof(buf),data)) > 0)
					{
                        if(fwrite(buf,1,(size_t)c,fp) != (size_t)c)
                    	   break;

						pwait (NULL);	/* let other processes run */
					}

#endif	/* KLM_MAILIT_MIMEAWARE */

					// log (-1, "15Apr2022 end of body, blank line");

                    if(ferror(fp))
                        fail = 1;
                    else
                    /* Leave a blank line between msgs */
                        fputc('\n',fp);

                    ind.size = ftell(fp) - start;
                    type = 1;   /* assume disk file, not a printer device */
#ifdef PRINTEROK
#if defined(MSDOS)
/* we now allow a printer device to be opened for output (see newfopen() in
   main.c.  If we've just now written to a printer, there's no point in
   writing the index, and in fact we should emit a FormFeed.  -- n5knx
*/
                    c = ioctl(fileno(fp), 0 /* get status */);
                    if (c != -1 && (c&0x9f) == 0x80) {  /* device, console,clock,nul flags */
                        type=0;     /* NOT a disk file, DON'T write the index */
                        fputc('\f', fp);
                    }
#elif defined(UNIX)
                    {
#include <sys/stat.h>
/*#include <unistd.h>*/
                        struct stat statbuf;


                        if (fstat(fileno(fp), &statbuf)==0 && !S_ISREG(statbuf.st_mode)) {
                            type=0;     /* NOT a disk file, DON'T write the index */
                            fputc('\f', fp);
                        }
                    }
#endif
#endif	/* PRINTEROK */
		// log (-1, "15Apr2022 close file");
                    fclose(fp);
#ifdef SMTPTRACE
                  if (Smtptrace) {
/* If we use tprintf here, instead of printf, flowcontrol
 * in the command screen is used; if the system is unattended for
 * more then 24 messages coming in, it will lock up mail delivery.
 * Make sure this only goes to the command screen - WG7J
 */
#ifdef UNIX
/* true, but we defeat that when using the trace interface anyway.  KF8NH */
                    tcmdprintf("New mail for %s from <%s>%c\n",ap->val,from,(Smtpquiet? ' ': '\007'));
#else
                    if(Current->output == Command->output)
                        printf("New mail for %s from <%s>%c\n",ap->val,from, Smtpquiet ? ' ' : '\007');
#endif /* UNIX */
                  }
#endif /* SMTPTRACE */
                  if(host != NULLCHAR){
                      rewind(data); /* Send return receipt */
                      mdaemon(data,host,NULLLIST,0);
                      free(host);
                  }
                } else
                    fail = 1;

                if (fail) {
                    (void) rmlock(Mailspool,ap->val);
                    break;
                }
  
#ifdef USERLOG
            /* Now touch the timestamp file if it's an area - WG7J */
                if(isarea(ap->val)) {
                    sprintf(mailbox,"%s/%s.inf",Mailspool,ap->val);
                    fclose(fopen(mailbox,"w"));
                }
#endif
            /* Update the index file */
                if (type && write_index(ap->val,&ind) == -1)
                    log(-1,"smtpserv: can't update index for %s", ap->val);
  
            (void) rmlock(Mailspool,ap->val);

            /* make a log entry */
#ifdef	MAIL_LOG_EXTRAS
				/*
				 * 23Feb2016, Maiko (VE4KLM), asked to log subject and msgid.
				 * 10Mar2016, Maiko (VE4KLM), changed by request, and now using
				 * a new MAIL_LOG_EXTRAS definition (configured in config.h).
				 */
                smtplog ("deliver Msg-Id: %s To: %s From: %s %s", msgid, ap->val, from, subject);
#else
				smtplog("deliver: To: %s From: %s",ap->val,from);
#endif
            }
        }
  
    /* Free remaining data in index structure */
        default_index("",&ind);
  
        return fail;
    }
  
/* Return Date/Time in Arpanet format in passed string */
/* Result must be parsable by mydate() */
    char *
    ptime(t)
    long *t;
    {
    /* Print out the time and date field as
     *      "DAY, day MONTH year hh:mm:ss ZONE"
     */
        register struct tm *ltm;
        static char str[40];
#ifdef TIMEZONE_OFFSET_NOT_NAME
        time_t offset;
#endif
    /* Read the system time */
        ltm = localtime(t);
  
    /* I hope your runtime DST changeover dates match your government's ! */
  
    /* rfc 822 format */
#ifdef TIMEZONE_OFFSET_NOT_NAME     /* G8FSL */
        offset = (timezone - (ltm->tm_isdst * 3600L)) / ( -36L);
#ifndef MSDOS
        if (offset % 100L)  /* not integral hours? */
            offset = (100L * (offset/100L)) + (60L * (offset % 100L))/100L;
#endif
        sprintf(str,"%s, %.2d %s %04d %02d:%02d:%02d %+05ld\n",
#else
        sprintf(str,"%s, %.2d %s %04d %02d:%02d:%02d %.3s\n",
#endif
        Days[ltm->tm_wday],
        ltm->tm_mday,
        Months[ltm->tm_mon],
        ltm->tm_year+1900,  /* or just take it %100 and use %02d format */
        ltm->tm_hour,
        ltm->tm_min,
        ltm->tm_sec,
#ifdef TIMEZONE_OFFSET_NOT_NAME
        offset);
#else
        tzname[ltm->tm_isdst]);
#endif
        return(str);
    }
  
    long
    get_msgid()
    {
        char sfilename[LINELEN];
        char s[20];
        register long sequence = 0;
        FILE *sfile;
  
#ifdef UNIX
        int lfd, cnt = 0;
        long pid;
  
    /*
     * I have a filter (u2j) which injects messages into JNOS for SMTP
     * delivery.  It's a good idea to make sure the sequence file is locked
     * while we update it, so JNOS/Linux and u2j don't get into a race for
     * the next message ID.
     */
        sprintf(sfilename, "%s/sequence.lck", Mailqdir);
        while ((lfd = open(sfilename, O_WRONLY|O_CREAT|O_EXCL, 0600)) == -1)
        {
            if (errno != EEXIST || ++cnt == 5)
            {
                log(-1, "can't lock sequence file %s: %s", sfilename,
                strerror(errno));
                where_outta_here(1,0);
            }
            if ((lfd = open(sfilename, O_RDONLY)) != -1)
            {
                sfile = fdopen(lfd, "r");
                fscanf(sfile, "%ld", &pid);
                fclose(sfile);
                if (kill(pid, 0) == -1 && errno == ESRCH)
                {
                    unlink(sfilename);
                    continue;
                }
            }
            j2pause(500);
        }
        sprintf(sfilename, "%10u\n", getpid());
        write(lfd, sfilename, (size_t)strlen(sfilename));
        close(lfd);
#endif /* UNIX */
  
        sprintf(sfilename,"%s/sequence.seq",Mailqdir);
        sfile = fopen(sfilename,READ_TEXT);
  
    /* if sequence file exists, get the value, otherwise set it */
        if (sfile != NULL){
            (void) fgets(s,sizeof(s),sfile);
            sequence = atol(s);
        /* Keep it in range of an 8 digit number to use for dos name prefix.
         * The bbs spec states that msg#'s on the R: line should be 0-99999
         * inclusive; this is enforced by use of modulus in sendmsg() -- N5KNX 1.10i
         */
#ifdef UNIX
            if (sequence < 1L || sequence > 999999999L)
#else
            if (sequence < 1L || sequence > 99999999L )
#endif
                sequence = 1;
            fclose(sfile);
        }
  
    /* increment sequence number, and write to sequence file */
        sfile = fopen(sfilename,WRITE_TEXT);
        fprintf(sfile,"%ld",++sequence);
        fclose(sfile);
#ifdef UNIX
        sprintf(sfilename, "%s/sequence.lck", Mailqdir);
        unlink(sfilename);
#endif
        return sequence;
    }
  
/* test if mail address is valid */
    int
    validate_address(s)
    char *s;
    {
        char *cp;
        int32 addr;
  
#ifdef  NNTPS
        if(*s == '!'){
            if((cp = strpbrk(s,"%@.,/")) != NULLCHAR)
                *cp = '\0';
            return NNTP_GATE;
        }
#endif
    /* if address has @ in it then check dest address */
        if ((cp = strrchr(s,'@')) != NULLCHAR)
		{
            cp++;
        /* 1st check if it is our hostname.
        * if not then check the hosts file and see if we can
        * resolve the address to a known site or one of our aliases.
        */
            if(stricmp(cp,Hostname) != 0)
			{
                if ((addr = mailroute(cp)) == 0L)
				{
                    if ((Smtpmode & QUEUE) == 0)
                        return BADADDR;
                    else
                        return LOCAL;   /* use external router */
				}
                if (ismyaddr(addr) == NULLIF)
                    return DOMAIN;
            }
  
        /* on a local address remove the host name part */
            *--cp = '\0';
        }
  
    /* if using an external router leave address alone */
        if ((Smtpmode & QUEUE) != 0)
            return LOCAL;
  
    /* check for the user%host hack */
        if ((cp = strrchr(s,'%')) != NULLCHAR){
            *cp = '@';
            cp++;
        /* reroute based on host name following the % separator */
            if (mailroute(cp) == 0)
                return BADADDR;
            else
                return DOMAIN;
        }
  
#ifdef MSDOS    /* dos file name checks */
    /* Check for characters illegal in MS-DOS file names */
        for(cp = s;*cp != '\0';cp++){
        /* Accept '.', '/', and '\' !
         * that way we can mail into subdirs - WG7J
         */
            if(*cp == '.' || *cp == '\\' || *cp == '/')
                continue;
            if(dosfnchr(*cp) == 0){
                return BADADDR;
            }
        }
#endif
#ifndef  NNTPS
        /* disallow nntp-gate syntax in local deliveries with no nntp-gate */
        if(*s == '!')
            return BADADDR;
#endif
        return LOCAL;
    }
  
/* place a mail job in the outbound queue.  Return 0 if successful, else 1 */
    int
    queuejob(dfile,host,to,from)
    FILE *dfile;
    char *host;
    struct list *to;
    char *from;
    {
        FILE *fp;
        struct list *ap;
        char tmpstring[50], prefix[9], buf[LINELEN];
        register int cnt;
  
        sprintf(prefix,"%ld",get_msgid());
        mlock(Mailqdir,prefix);
        sprintf(tmpstring,"%s/%s.txt",Mailqdir,prefix);
        if((fp = fopen(tmpstring,WRITE_TEXT)) == NULLFILE){
            (void) rmlock(Mailqdir,prefix);
            return 1;
        }
        while((cnt = fread(buf, 1, LINELEN, dfile)) > 0) {
            if(fwrite(buf, 1, (unsigned)cnt, fp) != (unsigned)cnt)
                break;
            pwait(NULL);   /* allow other processes to run */
        }
        if(ferror(fp)){
            fclose(fp);
            (void) rmlock(Mailqdir,prefix);
            return 1;
        }
        fclose(fp);
        sprintf(tmpstring,"%s/%s.wrk",Mailqdir,prefix);
        if((fp = fopen(tmpstring,WRITE_TEXT)) == NULLFILE){
            (void) rmlock(Mailqdir,prefix);
            return 1;
        }
        fprintf(fp,"%s\n%s\n",host,from);
        for(ap = to; ap != NULLLIST; ap = ap->next){
            fprintf(fp,"%s\n",ap->val);
            smtplog("queue job %s To: %s From: %s",prefix,ap->val,from);
        }
        fclose(fp);
        (void) rmlock(Mailqdir,prefix);
        return 0;
    }
  
/* Deliver mail to the appropriate mail boxes.  Return 0 if successful, else 1 */
    static int
    router_queue(data,from,to)
    FILE *data;
    char *from;
    struct list *to;
    {
        int c;
        register struct list *ap;
        FILE *fp;
        char tmpstring[50];
        char prefix[9];
  
        sprintf(prefix,"%ld",get_msgid());
        mlock(Routeqdir,prefix);
        sprintf(tmpstring,"%s/%s.txt",Routeqdir,prefix);
        if((fp = fopen(tmpstring,WRITE_TEXT)) == NULLFILE){
            (void) rmlock(Routeqdir,prefix);
            return 1;
        }
        rewind(data);
        while((c = getc(data)) != EOF)
            if(putc(c,fp) == EOF)
                break;
        if(ferror(fp)){
            fclose(fp);
            (void) rmlock(Routeqdir,prefix);
            return 1;
        }
        fclose(fp);
        sprintf(tmpstring,"%s/%s.wrk",Routeqdir,prefix);
        if((fp = fopen(tmpstring,WRITE_TEXT)) == NULLFILE){
            (void) rmlock(Routeqdir,prefix);
            return 1;
        }
        fprintf(fp,"From: %s\n",from);
        for(ap = to;ap != NULLLIST;ap = ap->next){
            fprintf(fp,"To: %s\n",ap->val);
        }
        fclose(fp);
        (void) rmlock(Routeqdir,prefix);
        smtplog("rqueue job %s From: %s",prefix,from);
        return 0;
    }
  
/* add an element to the front of the list pointed to by head
** return NULLLIST if out of memory.
*/
    struct list *
    addlist(head,val,type,aux)
    struct list **head;
    char *val;
    int type;
    char *aux;
    {
        register struct list *tp;
  
        tp = (struct list *)callocw(1,sizeof(struct list));
  
        tp->next = NULLLIST;
  
    /* allocate storage for the char string */
        tp->val = j2strdup(val);
        tp->aux = (aux == NULLCHAR) ? NULLCHAR : j2strdup(aux);
        tp->type = type;
  
    /* add entry to front of existing list */
        if (*head != NULLLIST)
            tp->next = *head;
        *head = tp;
        return tp;
  
    }
  
#ifdef SMTP_VALIDATE_LOCAL_USERS
/* return 0 if userid is in the first field of some line in DBfile,
          1 if not in any line,
         -1 if file can't be opened
   File lines begin with # if a comment, and the first field is terminated
   by SP, HT, NL or ':'.  Case is not important.  No continuations are allowed.
*/
static int firstfield(char *userid, char *DBfile)
{
    FILE *fp;
    char buf[LINELEN], *p;
    int ret=1;
    
    if ((fp = fopen(DBfile, READ_TEXT)) == NULLFILE) return -1;
    while (fgets(buf,LINELEN,fp) != NULLCHAR){
        if ( *buf == '#' )
            continue;
        if ((p=strpbrk(buf," 	:\n")) != NULLCHAR) *p='\0';
        if ((ret=stricmp(buf,userid)) == 0) break;
    }
    fclose(fp);
    return ret?1:0;
}
#endif /* SMTP_VALIDATE_LOCAL_USERS */

#define SKIPWORD(X) while(*X && *X!=' ' && *X!='\t' && *X!='\n' && *X!= ',') X++;
#define SKIPSPACE(X) while(*X ==' ' || *X =='\t' || *X =='\n' || *X == ',') X++;
  
/* check for an alias and expand alias into an address list */
/* Note that the returned list can be NULL if SMTP_VALIDATE_LOCAL_USERS is defined */
    static struct list *
    expandalias(head, user, origuser)
    struct list **head;
    char *user;
    char *origuser;
    {
        FILE *fp;
        register char *s,*p;
        struct rr *rrp, *rrlp;
        int inalias = 0;
        struct list *tp = (struct list*)0;
        char buf[LINELEN];
  
        if ((fp = fopen(Alias, READ_TEXT)) == NULLFILE){
        /* no alias file found, so try MB, MG or MR domain name records */
            rrlp = rrp = resolve_mailb(user);
            while(rrp != NULLRR){
                if(rrp->rdlength > 0){
                /* remove the trailing dot */
                    rrp->rdata.name[rrp->rdlength-1] = '\0';
                /* replace first dot with @ if there is no @ */
                    if(strchr(rrp->rdata.name,'@') == NULLCHAR
                        && (p = strchr(rrp->rdata.name,'.')) != NULLCHAR)
                            *p = '@';
                    if(strchr(rrp->rdata.name,'@') != NULLCHAR)
                        tp = addlist(head,rrp->rdata.name,DOMAIN,NULLCHAR);
                    else
                        tp = addlist(head,rrp->rdata.name,LOCAL,NULLCHAR);
                    ++inalias;
                }
                rrp = rrp->next;
            }
            free_rr(rrlp);
        }
        else {  /* process the alias file */
            while (fgets(buf,LINELEN,fp) != NULLCHAR){
                p = buf;
                if ( *p == '#' || *p == '\0')
                    continue;
                rip(p);
  
            /* if not in a matching entry skip continuation lines */
                if (!inalias && isspace(*p))
                    continue;
  
            /* when processing an active alias check for a continuation */
                if (inalias){
                    if (!isspace(*p))
                        break;  /* done */
                } else {
                    s = p;
                    SKIPWORD(p);
                    *p++ = '\0';    /* end the alias name */
                    if (strcmp(s,user) != 0)
                        continue;   /* no match go on */
                    inalias = 1;
                }
  
            /* process the recipients on the alias line */
                SKIPSPACE(p);
                while(*p != '\0' && *p != '#'){
                    s = p;
                    SKIPWORD(p);
                    if (*p != '\0')
                        *p++ = '\0';
  
                /* find hostname */
#ifdef  NNTPS
                    if(*s == '!')
                        tp = addlist(head,s,NNTP_GATE,NULLCHAR);
                    else
#endif
#ifdef	ALIASEXTFILE
                    if (*s == '&') {    /* &xxx => replace by lines from file xxx */
                        char buf2[LINELEN], *cp2;
                        FILE *fp2;

                        if ((fp2 = fopen((cp2=rootdircat(++s)), READ_TEXT)) != NULLCHAR) {
                            while (fgets(buf2, sizeof(buf2), fp2) != NULLCHAR) {
                                rip(buf2);
                                if (*buf2 == '#') continue;
                                else if (strchr(buf2, '@') != NULLCHAR)
                                    tp = addlist(head,buf2,DOMAIN,NULLCHAR);
                                else
                                    tp = addlist(head,buf2,LOCAL,NULLCHAR);
                            }
                            fclose(fp2);
                        }
                        if (s != cp2) free(cp2);
                    }
                    else
#endif /* ALIASEXTFILE */
                        if (strchr(s,'@') != NULLCHAR)
                            tp = addlist(head,s,DOMAIN,NULLCHAR);
                        else
                            tp = addlist(head,s,LOCAL,NULLCHAR);
                    SKIPSPACE(p);
                }
            }
            (void) fclose(fp);
        }

        if (inalias)    /* found and processed an alias */
            return tp;
        else {           /* no alias found; treat as a local address */
#ifdef SMTP_VALIDATE_LOCAL_USERS
            if (firstfield(user,Userfile)  /* in ftpusers file? */
#ifdef MAILCMDS
                && firstfield(user,Arealist) /* mbox S-cmd area destination? */
#endif
#if defined(USERLOG) && !defined(SMTP_VALIDATE_FTPPOP_ONLY)
                && firstfield(user,UDefaults) /* ever connected? */
#endif
#if defined(POP2SERVER) || defined(POP3SERVER)
                && firstfield(user,Popusers) /* in popusers file? */
#endif
               ) return *head;  /* no one we know, so ignore user */
#endif /* SMTP_VALIDATE_LOCAL_USERS */
            return addlist(head, user, LOCAL, origuser);
        }
    }
  
#if defined(ANSIPROTO)

    /* 10Mar2016, Maiko (VE4KLM), looks like ANSIPROTO is defined now */

    static void
    smtplog(char *fmt, ...)
    {
        va_list ap;
        char *cp;
        long t;
        FILE *fp;
  
        if ((fp = fopen(Maillog,APPEND_TEXT)) == NULLFILE)
            return;
        time(&t);
        cp = ctime(&t);
        fprintf(fp,"%.24s ",cp);
        va_start(ap,fmt);
        vfprintf(fp,fmt,ap);
        va_end(ap);
        fprintf(fp,"\n");
        fclose(fp);
    }
  
#else
  
    static void
    smtplog(fmt,arg1,arg2,arg3,arg4)
    char *fmt;
    int arg1,arg2,arg3,arg4;
    {
        char *cp;
        long t;
        FILE *fp;
  
        if ((fp = fopen(Maillog,APPEND_TEXT)) == NULLFILE)
            return;
        time(&t);
        cp = ctime(&t);
        fprintf(fp,"%.24s ",cp);
        fprintf(fp,fmt,arg1,arg2,arg3,arg4);
        fprintf(fp,"\n");
        fclose(fp);
    }
#endif
  
/* send mail to a single user. Called by mdaemon(), to implement the
** return-mail/return-receipt function (from addr will be "").  Also called by
** domailmsg() and news2mail().  We will queue msg unless from="", so that bid
** checks are done and news2mail() can't produce a local dupe msg.  -- n5knx 11/96
** Return 0 if successful, 1 if permanent failure.
*/
    int
    mailuser(data,from,to)
    FILE *data;
    char *from;
    char *to;
    {
  
        int address_type, ret;
        struct list *tolist = NULLLIST;
        char *newaddr, *origaddr;
  
	/* 16Jun2021, Maiko (VE4KLM), new send specific rules in rewrite, so we need to pass FROM field */
        //if((newaddr = rewrite_address(to,REWRITE_TO)) != NULLCHAR)
        if((newaddr = rewrite_address_new(to,REWRITE_TO, from)) != NULLCHAR){
            if(!strcmp(newaddr,"refuse")) {
                free(newaddr);
                return 1;
            }
            origaddr = to;
            to = newaddr;
        }
        else origaddr=NULLCHAR;
  
        /* check if address is ok */
        if ((address_type = validate_address(to)) == BADADDR){
            free (newaddr);
            return 1;
        }
        /* if a local address check for an alias */
        if (address_type == LOCAL)
            expandalias(&tolist, to, origaddr);
        else
            /* a remote address is added to the list */
            addlist(&tolist, to, address_type, NULLCHAR);

        /* Do direct delivery when from="", else just queue the msg */
        if (*from) {
            rewind(data);
            ret = queuejob(data,Hostname,tolist,from);
        }
        else
            ret = mailit(data,from,tolist,FALSE);
        del_list(tolist);
        free (newaddr);
        return ret;
  
    }
  
/* Mailer daemon return mail mechanism */
    int
    mdaemon(data,to,lp,bounce)
    FILE *data;     /* pointer to rewound data file */
    char *to;       /* Overridden by Errors-To: line if bounce is true */
    struct list *lp;    /* error log for failed mail */
    int bounce;     /* True for failed mail, otherwise return receipt */
    {
        time_t t;
        FILE *tfile;
        char buf[LINELEN], *cp, *newto = NULLCHAR;
        int cnt;

        if(to == NULLCHAR || (to != NULLCHAR && *to == '\0') || bounce){
            cnt = NOHEADER;
            while(fgets(buf,sizeof(buf),data) != NULLCHAR){
                if(buf[0] == '\n')
                    break;
            /* Look for Errors-To: */
                if(htype(buf,&cnt) == ERRORSTO &&
                (cp = getaddress(buf,(*buf==' ' || *buf=='\t'))) != NULLCHAR){
                    free(newto);
                    newto = j2strdup(cp);
                    break;
                }
            }
            if(newto == NULLCHAR && ((to != NULLCHAR && *to == '\0') ||
                to == NULLCHAR))
                return -1;
            rewind(data);
        }
        if((tfile = tmpfile()) == NULLFILE)
            return -1;

        time(&t);

	/* 12May2015, Maiko (VE4KLM), You must have a RECEIVED header, or
	 * else the index is not properly updated for the mailbox. This was
	 * actually discovered while trying to figure out why only bounced
	 * notification emails were getting default expired 'for no reason'.
	 */
        fprintf(tfile,"%sby %s with SMTP\n\tid AA%ld ; %s",
            Hdrs[RECEIVED], Hostname, get_msgid(), ptime(&t));

        fprintf(tfile,"%s%s",Hdrs[DATE],ptime(&t));
        fprintf(tfile,"%s<%ld@%s>\n",Hdrs[MSGID],get_msgid(),Hostname);
        fprintf(tfile,"%sMAILER-DAEMON@%s (Mail Delivery Subsystem)\n",
        Hdrs[FROM],Hostname);
        fprintf(tfile,"%s%s\n",Hdrs[TO],newto != NULLCHAR ? newto : to);
        fprintf(tfile,"%s%s\n\n",Hdrs[SUBJECT],
        bounce ? "Failed mail" : "Return receipt");
        if(bounce){
            fprintf(tfile,"  ===== transcript follows =====\n\n");
            for (; lp != NULLLIST; lp = lp->next)
                fprintf(tfile,"%s\n",lp->val);
            fprintf(tfile,"\n");
        }
        fprintf(tfile,"  ===== %s follows ====\n",
        bounce ? "Unsent message" : "Message header");
  
        while(fgets(buf,sizeof(buf),data) != NULLCHAR){
            if(buf[0] == '\n')
                break;
            fputs(buf,tfile);
        }
        if(bounce){
            fputc('\n',tfile);
            while((cnt = fread(buf,1,sizeof(buf),data)) > 0)
                fwrite(buf,1,cnt,tfile);
        }
        fseek(tfile,0L,0);
    /* A null From<> so no looping replys to MAIL-DAEMONS */
        (void) mailuser(tfile,"",newto != NULLCHAR ? newto : to);
        fclose(tfile);
        free(newto);
        return 0;
    }
  
  
#ifdef MAILMSG
/* Command to email a file or msg to a user */
/* mailmsg <to> [<subject>] <msg|/filepath> (from {MSGUSER|sysop}@Hostname) */
int
domailmsg(argc,argv,p)
int argc;
char *argv[];
void *p;
{
        time_t t;
        FILE *tfile,*sfile;
        int cnt;
        char buf[LINELEN], *from, *fromid, *to=argv[1];

        if((tfile = tmpfile()) == NULLFILE)
            return -1;

        if((fromid = getenv("MSGUSER")) == NULLCHAR)   /* G6OPM */
            fromid = "sysop";

        time(&t);
        fprintf(tfile,"%sby %s with SMTP\n\tid AA%ld ; %s",
            Hdrs[RECEIVED], Hostname, get_msgid(), ptime(&t));
        fprintf(tfile,"%s%s",Hdrs[DATE],ptime(&t));
        fprintf(tfile,"%s<%ld@%s>\n",Hdrs[MSGID],get_msgid(),Hostname);
        sprintf(from=mallocw(strlen(Hostname)+strlen(fromid)+2), "%s@%s", fromid, Hostname);
        fprintf(tfile,"%s%s\n",Hdrs[FROM],from);
        fprintf(tfile,"%s%s\n",Hdrs[TO],argv[1]);
        if (argc > 3)
            fprintf(tfile,"%s%s\n\n",Hdrs[SUBJECT],argv++[2]);
        else
            fputc('\n',tfile);

#ifdef UNIX
        if (*argv[2] == '/') {
#else
        if (*argv[2] == '/' || *argv[2] == '\\' || argv[2][1] == ':') {
#endif
            if((sfile=fopen(argv[2],READ_TEXT))==NULLFILE) {
                tprintf("Can't open %s\n", argv[2]);
                fclose(tfile);
                free(from);
                return 1;
            }
            while((cnt = fread(buf,1,sizeof(buf),sfile)) > 0)
                fwrite(buf,1,cnt,tfile);
            fclose(sfile);
        }
        else
            fprintf(tfile,"%s\n",argv[2]);
        if(ferror(tfile))
            cnt=1;
        else {
            fseek(tfile,0L,0);
            cnt=mailuser(tfile,from,to);
        }
        fclose(tfile);
        free(from);
        return cnt;  /* 0 if successful, 1 if failure */
}
#endif /* MAILMSG */

#ifdef HOLD_LOCAL_MSGS
/* Returns 1 if name is listed in Holdlist, 0 otherwise. Similar to isarea(). */
int
isheldarea(name)
char *name;
{
    FILE *fp;
    char *area;
    char buf[LINELEN];
  
    if((fp = fopen(Holdlist,READ_TEXT)) == NULLFILE)
        return 0;
    area = j2strdup(name);
    dotformat(area);
    while(fgets(buf,sizeof(buf),fp) != NULLCHAR) {
        /* The first word on each line is all that matters */
        firsttoken(buf);
        dotformat(buf);
        if(stricmp(area,buf) == 0) {    /* found it */
            fclose(fp);
            free(area);
            return 1;
        }
    }
    fclose(fp);
    free(area);
    return 0;
}
#endif
