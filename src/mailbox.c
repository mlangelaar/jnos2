/*
 * JNOS 2.0
 *
 * $Id: mailbox.c,v 1.1 2015/04/22 01:51:45 root Exp root $
 *
 * NOTE: because of size, the previous 'mailbox.c' and 'mailbox2.c'
 * have been split in 5 parts:
 * mboxcmd.c, containing the 'mbox' subcommands,
 * mailbox.c, containing general mailbox user commands,
 * mboxfile.c, containing file mailbox user commands,
 * mboxmail.c, containing mail mailbox user commands, and
 * mboxgate.c, containing gateway mailbox user commands.
 * 940215 - WG7J
 *
 * There is only one function in this mailbox code that depend on the
 * underlying protocol family, namely mbx_getname(). All the other
 * functions can hopefully be used without modification on other stream
 * oriented protocols than AX.25 or NET/ROM.
 *
 * SM0RGV 890506, most work done previously by W9NK
 *
 *** Changed 900114 by KA9Q to use newline mapping features in stream socket
 *      interface code; everything here uses C eol convention (\n)
 *
 *      Numerous new commands and other changes by SM0RGV, 900120
 *
 * Gateway function now support outgoing connects with the user's call
 * with inverted ssid. Users can connect to system alias as well...
 * See also several mods in socket.c,ax25.c and others
 * 11/15/91, WG7J/PA3DIS
 *
 * Userlogging, RM,VM and KM commands, and R:-line interpretation
 * added 920307 and later, Johan. K. Reinalda, WG7J/PA3DIS
 *
 * Inactivity timeout-disconnect added 920325 and later - WG7J
 *
 * Mods by VE4KLM for JNOS 2.0 project
 */
#include <time.h>
#include <ctype.h>
#ifdef MSDOS
#include <alloc.h>
#endif
#ifdef  UNIX
#include <sys/types.h>
#include <sys/stat.h>
#endif
#include "global.h"
#ifdef MAILBOX
#include "timer.h"
#include "proc.h"
#include "socket.h"
#include "usock.h"
#include "session.h"
#include "smtp.h"
#include "dirutil.h"
#include "telnet.h"
#include "ftp.h"
#include "ftpserv.h"
#include "commands.h"
#include "netuser.h"
#include "files.h"
#include "bm.h"
#include "pktdrvr.h"
#include "ax25.h"
#include "mailbox.h"
#include "ax25mail.h"
#include "nr4mail.h"
#include "cmdparse.h"
#include "mailfor.h"
#include "mailutil.h"
#include "index.h"

#ifdef SYSEVENT
#include "sysevent.h"	/* 02Feb2008, Maiko - New notification scripts */
#endif

#ifdef B2F
#include "b2f.h"	/* 08Mar2009, Maiko, Don't forget B2F prototypes */
#endif

#ifdef HTTPVNC
#include "bbs10000.h"	/* 03Jul2009, Maiko, Browser based VNC for JNOS */
#endif

#ifdef	J2MFA
#include "j2KLMlists.h"	/* 21Nov2020, Maiko (VE4KLM), MFA exclusion list */	
#endif

#define FLUSHWAIT 20

#if defined (NETROM) && defined (NRR)
extern int donrr (int, char**, void*);
#endif

struct mbx *Mbox;
int BbsUsers;
int Totallogins;
int MbMaxUsers;
extern int MbShowAliases;
extern struct cmds DFAR Cmds[];

#ifdef WPAGES
extern int dombwpages (int argc, char **argv, void *p);
extern int wpage_options (char *user, int mode);
#ifdef DONT_COMPILE		/* 12Mar2012, Maiko, Leave this out for now */
extern void add_WPUpdate (char *origcall, char *home, char *name, char which);
#endif
#endif

static int mbx_getname __ARGS((struct mbx *m));
static int dombhelp __ARGS((int argc,char *argv[],void *p));
static int dombexpert __ARGS((int argc,char *argv[],void *p));
static int dombipheard __ARGS((int argc,char *argv[],void *p));
#ifdef AX25
static int dombjheard __ARGS((int argc,char *argv[],void *p));
#endif
#if defined FOQ_CMDS && defined TTYLINKSERVER
static int dochat __ARGS((int argc,char *argv[],void *p));
#endif
#if defined FOQ_CMDS && defined CALLCLI
static int dombcallbook __ARGS((int argc,char *argv[],void *p));
#endif

#ifdef RSYSOPSERVER
static char DFAR RSysopbanner[] = "\nRemote Login at %s - %s\n\n";
#endif
char Loginbanner[] = "\nJNOS (%s)\n\n";

/*
 * 05Jun2005, I've never liked this extraneous stuff, especially
 * on a slow AX25 link. No point really (in my opinion anyways),
 * so by default it is now disabled (ie, #undef WELCOMEUSERS).
 */
#ifdef	WELCOMEUSERS
char Mbwelcome[] = "\nWelcome %s,\n";
#ifdef MAILCMDS
char Mbbanner[] = "to the %s TCP/IP Mailbox (JNOS %s).\n";
#else
char Mbbanner[] = "to the %s TCP/IP Server (JNOS %s).\n";
#endif
char CurUsers[] = "Currently %d user%s.\n";
#endif
  
#if defined MAILCMDS || defined FILECMDS
char Howtoend[] = "End with /EX or ^Z in first column (^A aborts):\n";
char MsgAborted[] = "Msg aborted\n";
#endif
  
#ifdef MAILCMDS
char MbCurrent[] = "Current msg# %d.\n";
#endif
  
char Mbmenu[] = "?,"
#ifdef MAILCMDS
"A,"
#endif
"B,"
#ifdef GATECMDS
#if defined NETROM || defined AX25
"C,"
#endif
#endif
#ifdef CONVERS
"CONV,"
#endif
#ifdef FILECMDS
"D,"
#endif
#ifdef GATECMDS
"E,"
#endif
#ifdef FOQ_CMDS
"F,"
#endif
"H,I,IH,IP,"
#ifdef AX25
"J,"
#endif
#ifdef MAILCMDS
"K,L,"
#endif
"M,"
#if defined GATECMDS && defined NETROM
"N,NR,"
#endif
#if defined FOQ_CMDS && defined TTYLINKSERVER
"O,"
#endif
#ifdef GATECMDS
#ifdef AX25
"P,"
#endif
"PI,"
#endif
#if defined FOQ_CMDS && defined CALLCLI
"Q,"
#endif
#ifdef MAILCMDS
"R,S,"
#endif
#ifdef GATECMDS
"T,"
#endif
#ifdef FILECMDS
"U,"
#endif
#ifdef MAILCMDS
"V,"
#endif
#ifdef FILECMDS
"W,"
#endif
"X"
#ifdef FILECMDS
",Z"
#endif
" >\n";
  
extern char Mbnrid[];

#ifdef	BLACKLIST_BAD_LOGINS
extern void addtaccess (int32, unsigned int, int16, int16, int16, int);
extern int MbBlackList;
#endif
  
char Longmenu[] =
#ifdef MAILCMDS
"Mail   : Area Kill List Read Send Verbose\n"
#endif
  
#ifdef GATECMDS
  
"Gateway:"
#if defined AX25 || defined NETROM
" Connect"
#endif
" Escape"
#ifdef NETROM
" Nodes NRroute"
#endif
#ifdef AX25
" Ports"
#endif
" PIng Telnet\n"
  
#endif /* GATECMDS */
  
#ifdef FILECMDS
"File   : Download Upload What Zap\n"
#endif
  
"General: ?-Help Bye"
#ifdef CONVERS
" CONVers"
#endif
#ifdef FOQ_CMDS
" Finger"
#endif
" Help Info IHeard\n"
"         IProute"
#ifdef AX25
" Jheard"
#endif
" Mbox"
#if defined FOQ_CMDS && defined TTYLINKSERVER
" Operator"
#endif
#if defined FOQ_CMDS && defined CALLCLI
" Query"
#endif
" Xpert\n\n";

/*
 * 18Sep2008, Maiko (VE4KLM), One function to log MBOX access and use. I am
 * doing this so that all log entries regarding MBOX use will be consistent
 * in look and feel. It will be easier to interpret the logs that way, and
 * cuts down on excess use of STRINGS in the code. Before this, log entries
 * had no set standard in how they were being formatted for output.
 */
void logmbox (int s, char *name, char *format, ...)
{
	char logstr[100];

	va_list args;
  
	va_start (args, format);
	vsprintf (logstr, format, args);
	va_end (args);

	rip (logstr);	/* 14Feb2016, Maiko (VE4KLM), tidy up logs */

	log (s, "MBOX (%s) %s", name, logstr);
}

/* This is called by the finger-daemon */
void
listusers(s)
int s;
{
    int outsave;
    struct mbx m;
  
    m.privs = 0;
    m.stype = ' ';
  
    outsave = Curproc->output;
    Curproc->output = s;
    dombusers(0,NULLCHARP,&m);
    Curproc->output = outsave;
}
  
struct mbx *
newmbx()
{
    struct mbx *m,*new;
  
    if (MbMaxUsers && BbsUsers >= MbMaxUsers)
        return NULLMBX;
    if((new = (struct mbx *) calloc(1,sizeof(struct mbx))) == NULLMBX)
        return NULLMBX;
    BbsUsers++;
    /* add it the list */
    if((m=Mbox) == NULLMBX)
        Mbox = new;
    else {
        while(m->next)
            m=m->next;
        m->next = new;
    }
    return new;
}

#ifdef MBX_CALLCHECK
extern int callcheck (char*);   /* 08Mar2009, Maiko, added prototype */
#endif

#ifdef	J2MFA

/*
 * 28Nov2020, Maiko, Putting exclude code into a function where it belongs.
 */

static int mfa_excluded (struct mbx *m)
{
	char *tptr, *inetaddr, tmp[MAXSOCKSIZE];

	int excluded = 0, len = MAXSOCKSIZE;

	if (j2getpeername (m->user, tmp, &len) != -1)
	{
		inetaddr = tptr = psocket (tmp);

		while (*tptr && *tptr != ':')
			tptr++;

		*tptr = 0;	

		excluded = ip_exclude_MFA (inetaddr);

		if (excluded)
			logmbox (m->user, m->name, "auth excluded");
	}

	return excluded;
}

#define	ACMAXNMBRS 10	/* 28Nov2020, Maiko, Moved up here, needed for fixed code */

/*
 * 23Nov2020, Maiko, If an incoming telnet session is for a BBS, then
 * we don't email them a code, we expect a specific code specified by
 * the sysop using this function below. Right now all incoming telnet
 * forwards from BBS users use the same code, perhaps down the road I
 * will make this more flexible, but right now i just want to release
 * this as a prototype.
 */

static char *fixed_MFA_code = (char*)0;

int domfafixed (int argc, char **argv, void*p)
{
	if (argc < 2)
	{
		tprintf ("BBS Authentication Code : ");

		if (fixed_MFA_code)
			tprintf ("%s\n", fixed_MFA_code);
		else
			tprintf ("not defined\n");

		return 0;
    }

	if (strlen (argv[1]) > ACMAXNMBRS)
	{
		tprintf ("Maximum %d characters allowed\n", ACMAXNMBRS);

		return 0;
	}

	if (fixed_MFA_code)
		free (fixed_MFA_code);

	fixed_MFA_code = j2strdup (argv[1]);

    return 0;
}

/*
 * 16Nov2020, Maiko, 'Multi Factor Authentication' for JNOS, why not :)
 */

int mfa_validation  (struct mbx *m)
{
	char *msgcmd, *savemline, *ptr, authcode[ACMAXNMBRS+1];

	int cnt;

	/*
	 * 20Nov2020, Maiko, Use a fixed code for BBS systems,
	 * as to not complicate the forwarding scripts for any
	 * of those system using telnet to forward ? Try it.
	 */
	if (m->privs & IS_BBS)
	{
		if (!fixed_MFA_code)
		{
			logmbox (m->user, m->name, "sysop did not configure auth code");
			return 0;
		}

		/* 23Nov2020, Maiko, SYSOP defined */
		strcpy (authcode, fixed_MFA_code);

		tprintf ("\nAuth: ");
	}
	else
	{
		/*
		 * 13Dec2020, Maiko, Oops, this will crash if user is NOT registered,
		 * because I forgot to check for m->IPemail == NULLCHAR, fixed below.
		 */
		if (m->IPemail == NULLCHAR)
		{
			logmbox (m->user, m->name, "not MFA registered");

			/*
			 * 19Dec2020, Maiko (VE4KLM), Instead of kicking the user out, we
			 * should give them the option to REGISTER don't you think ? Most
			 * of these won't even be in ftpusers, so minimum permissions are
			 * in affect (univperm) for these types of logins anyways, really
			 * should not be a big deal, right ? So return 1, instead of 0 ?
			 */
			tprintf ("\nThis is an MFA enabled system - please consider registering.\n\n");
			return 1;
		}

		/* 18Nov2020, Generate Auth Code using Random Numbers */

		srand (time (NULL));	/* important to seed with ever changin value !!! */

		for (ptr = authcode, cnt = 0; cnt < ACMAXNMBRS; cnt++)
			*ptr++ = random () % 26 + 'A';

		*ptr = 0;	/* make sure to terminate the authcode string */

/*
	mfamail[0] = (char*)0;
	mfamail[1] = j2strdup (m->IPemail);
	mfamail[2] = "JNOS Authentication Code";
	mfamail[3] = j2strdup (authcode);
	domailmsg (4, mfamail, 0); 
 */
		msgcmd = mallocw (40 + strlen (m->IPemail) + strlen (authcode));

		sprintf (msgcmd, "mailmsg %s \"JNOS Authentication Code\" %s", m->IPemail, authcode);

		cmdparse (Cmds, msgcmd, NULL);	/* 19Nov2020, Maiko, Proper way to do it */

		free (msgcmd);	/* make sure to free up the command buffer */

		/* tprintf ("\nTesting Code [%s] Email [%s]", authcode, m->IPemail); */
 	
		/*
		 * 21Dec2020, Maiko, Oops, we need to kick smtp queue, or the user
		 * might be waiting a long time to get their email, the queue only
		 * gets kicked during an FBB forwarding session, so better do it !
		 */
		smtptick (NULL);

		tprintf ("\nAuth (check your mail): ");
	}

	tflush();

	savemline = j2strdup (m->line);		/* save the password */
	
	if (mbxrecvline(m) == -1)
		return 0;

	if (strcmp (m->line, authcode))
	{
		free (savemline);
		return 0;
	}

	strcpy (m->line, savemline);
	free (savemline);
	tprintf ("\n");

	return 1;
}

#endif	/* end of J2MFA */
  
static int
mbx_getname(m)
struct mbx *m;
{
    char *cp;
    union sp sp;
    char tmp[MAXSOCKSIZE];
    int len = MAXSOCKSIZE;
    int anony = 0;
    int oldmode;
    int founddigit=0;
    int count=0;
#ifdef AX25
    int32 flags = 0;
    int ax_25 = 0;
    struct usock *up;
#endif

#ifdef MD5AUTHENTICATE
	time_t md5seed;	/* 16Nov2009, Maiko, new bridge var for time() function */
    int32 md5t;		/* easier to locate md5t, then just plain old t */
#endif
  
    sp.p = tmp;
    sp.sa->sa_family = AF_LOCAL;    /* default to AF_LOCAL */
    j2getpeername(m->user,tmp,&len);
    m->family = sp.sa->sa_family;
    m->path = mallocw(MBXLINE);

#ifdef MBX_MORE_PROMPT
//   Allow -more- prompt by default, during
//   BBS interaction. Same as 'xm 20' command.
//   K6FSH  2010-02-24
    m->morerows = 20;  /* formerly just for telnet ... now default for all */
#endif

    /* This is one of the two parts of the mbox code that depends on the
     * underlying protocol. We have to figure out the name of the
     * calling station. This is only practical when AX.25 or NET/ROM is
     * used. Telnet users have to identify themselves by a login procedure.
     */
    switch(sp.sa->sa_family){
#ifdef  AX25
        case AF_AX25:
        /* If this is not to the convers call, and this port is
         * set for NO_AX25, then disconnect ! - WG7J
         */
            if ((m->type != CONF_LINK) && ((up = itop (m->user)) != NULLUSOCK))
			{
				/*
				 * 23Nov2010, Maiko (VE4KLM), Make sure connection has actually
				 * been 'completed'. Robert (KD1ZD) was getting regular crashes
				 * when forwarding to another system, he provided me with a GDB
				 * dump that was quite clear on where the crash was happening.
				 *
				 * I think the 'jump-start' concept (see lapb.c) is why this
				 * particular scenario is possible - so important to check !
				 *
				 * In reality all this code is simply for the 'mbox noax25'
				 * feature. If NO one ever uses it, then why bother with all
				 * of this in the first place - leave it alone for now :)
				 */
				if (up->cb.p == NULLCHAR)
				{
					log (-1, "connection never completed");
					return -1;
				}

                if ((flags=up->cb.ax25->iface->flags) & NO_AX25)
                    return -1;
            }
            ax_25 = 1;
        /* note fallthrough */
        case AF_NETROM:
        /* NETROM and AX25 socket address structures are "compatible" */
        /* Save user call, in case user wants to use gateway function */
            memcpy(m->call,sp.ax->ax25_addr,AXALEN);
            m->call[ALEN] &= 0xfe;/*Make sure E-bit isn't set !*/
            pax25(m->name,sp.ax->ax25_addr);
            cp = strchr(m->name,'-');
            if(cp != NULLCHAR)                      /* get rid of SSID */
                *cp = '\0';
        /* SMTP wants the name to be in lower case */
#ifdef __TURBOC__
            (void)strlwr(m->name);
#else
            cp = m->name;
            while(*cp){
                if(isupper(*cp))
                    *cp = tolower(*cp);
                ++cp;
            }
#endif
            anony = 1;
        /* Try to find the privileges of this user from the userfile */
            if((m->privs = userlogin(m->name,m->line,&m->path,MBXLINE,
              &anony,ax_25 ? "ax25perm" : "nrperm")) == -1){
                m->privs = 0;
                free(m->path);
                m->path = NULLCHAR;
            }
            if(m->privs & EXCLUDED_CMD)
                return -1;
            if(ax_25)
                if(((flags & USERS_ONLY) && (m->privs & IS_BBS)) ||
                    ((flags & BBS_ONLY) && !(m->privs & IS_BBS)) ||
                    ((flags & SYSOP_ONLY) && !(m->privs & SYSOP_CMD)))
                    return -1;

#ifdef MBX_CALLCHECK
		/*
		 * 19Oct2010, Maiko, Note that K6FSH added callcheck() here, for ax25
		 * connects. When I first implemented call checks, it was for incoming
		 * telnet (internet) connections only - I did not worry about ax25.
		 */
            if (!callcheck (m->name))    /* function courtesy of K2MF */
            {
                j2tputs("Invalid Callsign\n");
                logmbox(m->user, m->name, "bad AX.25 login");
                *m->name = '\0';  /* wipe any garbage */
                return -1;
            }
#endif

#ifdef AX25PASSWORD
            /* KF5MG - non-bbses get LOGIN: PASSWORD: prompts. */
            if(m->privs & IS_BBS
#ifndef NRPASSWORD
               || !ax_25
#endif
              )
#endif /* AX25PASSWORD */
            return 0;
#ifdef AX25PASSWORD
            if(m->path == NULLCHAR) m->path = mallocw(MBXLINE);
#ifdef NRPASSWORD
            ax_25++;   /* 1 => NetRom, 2 => ax.25 */
#endif
            /* Note we fall into the AF_LOCAL case */
#endif /* AX25PASSWORD */
#endif /* AX25 */
        case AF_LOCAL:
        case AF_INET:
            m->state = MBX_LOGIN;
#ifdef RSYSOPSERVER
            if(m->type == RSYSOP_LINK)
               tprintf(RSysopbanner,Hostname,Version);
            else {
#endif
            tprintf(Loginbanner,Hostname);
            if(Mtmsg != NULLCHAR)
                j2tputs(Mtmsg);
#ifdef RSYSOPSERVER
            }
#endif
            for(;;){
            /* Maximum of 3 tries - WG7J */
                if(count++ == 3)
                    return -1;
                oldmode = sockmode(m->user,SOCK_ASCII);
                j2tputs("login: ");
                tflush();
                if(mbxrecvline(m) == -1)
                    return -1;
                if(*m->line == 4) /* Control-d */
                    return -1;
                if(*m->line == '\0')
                    continue;
            /* Chop off after name lenght */
                m->line[MBXNAME] = '\0';
  
            /* add a little test to avoid 'Mailfile busy' syndrome - WG7J
             * Check for characters illegal in MS-DOS file names.
             */
                for(cp = m->line;*cp != '\0';cp++) {
#ifdef MSDOS
                    if(dosfnchr(*cp) == 0)
                        break;
#endif
#ifdef UNIX
                    if(*cp == '/') break;
#endif
#ifndef __TURBOC__
                    *cp = tolower(*cp);   /* use lowercase henceforth */
#endif
                }
                if(*cp != '\0')
                    continue;
#ifdef __TURBOC__
                (void)strlwr(m->line);   /* use lowercase henceforth */
#endif
                strcpy(m->name,m->line);
			/*
			 * 06Apr2008, Maiko (VE4KLM), Okay, I think I've figured
			 * out the Airmail telnet client. The '.' character that
		 	 * preceeds the login user name will help me set CRONLY so
			 * that the mbx_parse works properly. CRONLY is needed or
			 * else the first byte of each command gets lost. Wonder
			 * if we should also disable IAF check (not yet) ...
		 	 *
			 * May2016, Maiko (VE4KLM), It appears bpq does this as well, we
			 * can't use a CRONLY telnet port, because the login and password
			 * appear to be full CR/LF, but switches to CRONLY after that.
			 */
				if (*(m->line) == '.')
				{
					struct usock *up;
					log (m->user, "possible airmail or bpq telnet client ...");
					strcpy (m->name, m->line+1);
					if ((up=itop(m->user)) != NULLUSOCK)
						strcpy(up->eol,"\r");  /* just CR */
				}

#ifdef MD5AUTHENTICATE
                time (&md5seed);
				md5t = (int32)md5seed;
#endif
#ifdef AX25PASSWORD
                /* KF5MG - send a password: prompt to ax.25 users */
                if(ax_25) {
                   anony = 0;
#ifdef MD5AUTHENTICATE
                   tprintf ("Password [%x] : ", md5t);
#else
                   j2tputs("Password: ");
#endif /* MD5AUTHENTICATE */
                   tflush();
                   if(mbxrecvline(m) == -1)
                       return -1;
                } else {
#endif /* AX25PASSWORD */
#ifdef MD5AUTHENTICATE
                tprintf ("Password [%x] : %c%c%c", md5t, IAC, WILL, TN_ECHO);
#else
                tprintf("Password: %c%c%c",IAC,WILL,TN_ECHO);
#endif
                SET_OPT(m,TN_ECHO);
                tflush();
                sockmode(m->user,SOCK_BINARY);
                if(mbxrecvline(m) == -1)
                    return -1;
                tprintf("%c%c%c",IAC,WONT,TN_ECHO);
                RESET_OPT(m,TN_ECHO);
                sockmode(m->user,oldmode);
                tputc('\n');
                tflush();
#ifdef AX25PASSWORD
                }
#endif
            /* This is needed if the password was sent before the
             * telnet no-echo options were received. We need to
             * flush the old sequence from the input buffers, sigh
             */
                if(socklen(m->user,0))/* discard any remaining input */
                    recv_mbuf(m->user,NULL,0,NULLCHAR,0);

#ifdef MBX_CALLCHECK
				/*
				 * 10Jul07, Maiko, Finally !!! Callsign validation. Note that
				 * I still allow a password to be entered BEFORE I check the
				 * validity of the Login callsign. This is intentional ! I do
				 * not want to give a potential *intruder* any idea as to why
				 * they are not able to login - less they know, the better.
				 */
				if (!callcheck (m->name))	/* function courtesy of K2MF */
				{
					j2tputs("Login incorrect\n");
					logmbox(m->user, m->name, "bad login");
                	*m->name = '\0';	/* wipe any garbage */

#ifdef	BLACKLIST_BAD_LOGINS

		/* 23Jan2015, Maiko (VE4KLM), Getting tired of hackers :) */

					if (MbBlackList)
					{
						struct socket fsocket;

						int i = SOCKSIZE;

						if (j2getpeername (m->user, (char*)&fsocket, &i) != -1)
						{
							char *tptr, *inetaddr = tptr = psocket (&fsocket);

							while (*tptr && *tptr != ':')
								tptr++;

							*tptr = 0;	

							log (-1, "blacklisting [%s]", inetaddr);

							/* 23Jan2015 - put on the very top of the list */	
				/*
				 * 25Feb2015, Maiko (VE4KLM), use new 99998 value for the
				 * index (forces setting of age for expiry), this had to be
				 * done, since manual insertion via 'tcp access' was causing
				 * the age to be set as well, creating possibility of expiry.
				 *
							addtaccess (aton (inetaddr), 32, 0, 0, -1, 0);
				 */
							addtaccess (aton (inetaddr), 32, 0, 0, -1, 99998);

							return -1;	/* don't allow 3 chances !!! */
						}
					}
#endif
					continue;	/* allow 3 chances like regular login */
				}
#endif
                cp = "tcpperm";   /* alternative to univperm */
#ifdef AX25PASSWORD
                if(ax_25) cp = "ax25perm";
#ifdef NRPASSWORD
                if(ax_25==1) cp = "nrperm";
#endif /* NRPASSWORD */
#endif /* AX25PASSWORD */
#ifdef TIPSERVER
                if(m->type==TIP_LINK) cp = "tipperm";
#endif
#ifdef MD5AUTHENTICATE
  				/* hide it in path */
                memcpy(m->path, (char *)&md5t, sizeof(md5t));
#endif
                if((m->privs = userlogin(m->name,m->line,&m->path,MBXLINE, &anony,cp)) != -1)
				{
#ifdef	J2MFA_MOVED_IGNORE	/* moved outside mbx_getname() 19Nov2020 */

 					/* 17Nov2020, Maiko, MFA for JNOS, why not :) */
					if (mfa_validation (m))
					{
#endif

#ifdef SYSEVENT
					/* 02Feb2008, New notification scripts - VE4KLM */
					j2sysevent (bbsconn, m->name);
#endif
                    if(anony)
                        logmbox (m->user, m->name, "login, password (%s)", m->line);
                    else
                        logmbox (m->user, m->name, "login");

                    if(m->privs & EXCLUDED_CMD)
                        return -1;
#ifdef AX25
            /*try to set the name as the user-call.
             *this is a very crude test! Be careful...
             *Login must have at leat 1 digit (0-9) in it,
             *and it must be possible to convert it to a call.
             *if this doesn't work, disallow the gateway command,
             *no matter if this was allowed by priviledges or not.
             *Be careful, some one with login name '4us' and
             *permission set to allow gateway/netrom, will
             *go out as '4us-15' or '4us-0' !!!!!
             *11/15/91 WG7J/PA3DIS
             */
                    for(cp=m->name;*cp != '\0';cp++)
                        if(isdigit((int)*cp))
                            break;
                    if(*cp != '\0')
                        founddigit = 1;
                    if( (setcall(m->call,m->name) == -1) || (!founddigit) ) {
                        m->privs &= ~(AX25_CMD|NETROM_CMD);
                    }
#else
                    m->privs &= ~(AX25_CMD|NETROM_CMD);
  
#endif /* AX25 */
                    return 0;
                }
                j2tputs("Login incorrect\n");
                logmbox (m->user, m->name, "login failed");
#ifdef MAILERROR
                mail_error("MBOX Login failed: %s, pw %s",m->name,m->line);
#endif
                *m->name = '\0';        /* wipe any garbage */

#ifdef	J2MFA_MOVED_IGNORE
				}
#endif
            }
    }
    return 0;
}
  
/* put up the prompt */
void
putprompt(m)
struct mbx *m;
{
    char area[64];
    char *cp1,*cp2;
  
#ifdef MAILCMDS
#ifdef FBBFWD
    if(m->sid & MBX_FBBFWD) {
       /* do nothing */
    } else
#endif
    if(m->sid & MBX_SID)
        j2tputs(">\n");
    else {
#endif
        if(m->sid & MBX_NRID)
            j2tputs(Mbnrid);
#ifdef MAILCMDS
        if(m->sid & MBX_AREA) {
            cp1 = m->area;
            cp2 = area;
            /* Convert / and \ into . */
            while(*cp1 != '\0') {
                if(*cp1=='/')
                    *cp2 = '.';
                else
                    *cp2 = *cp1;
                cp1++;
                cp2++;
            }
            *cp2 = '\0';
            tprintf("Area: %s ",area);
        }
#endif
        if(m->sid & MBX_EXPERT) {
#ifdef MAILCMDS
            tprintf("(#%d) ",m->current);
#endif
            j2tputs(">\n");
        }
        else {
#ifdef MAILCMDS
            tprintf(MbCurrent,m->current);
#endif
            if(MbShowAliases && AliasList) {
                struct alias *a;
  
                for(a=AliasList;a;a=a->next)
                    tprintf("%s,",a->name);
            }
            j2tputs(Mbmenu);
        }
  
#ifdef MAILCMDS
    }
#endif
}
  
#if defined (HFDD) || defined (HTTPVNC)

/* 16Nov2009, Maiko, new function to split up multiple #ifdef situations */

static int login_more_direct (struct mbx *m, char *gpasswd, int *anony)
{
	int retval = -1;

	m->path = mallocw(MBXLINE);	/* 13Feb2005, forgot this !!! */

	if ((m->privs = userlogin (m->name, gpasswd,
		&m->path, MBXLINE, anony, "ax25perm")) != -1)
	{
		if (m->type == TTY_LINK)
			logmbox (m->user, m->name, "hf login");
		else
			logmbox (m->user, m->name, "browser login");

		retval = 0;
	}
	else exitbbs (m);

	return retval;
}

#endif	/* end of HFDD or HTTPVNC */

#undef J2_GLOBAL_MAILBOX_SYSTEM

#ifdef J2_GLOBAL_MAILBOX_SYSTEM
sync2gms ()
{
	tprintf ("checking gms ...\n");
}
#endif

/* Incoming mailbox session */
void mbx_incom (int s, void *t, void *p)
{
    struct mbx *m;
    struct alias *a;
    int rval;
    FILE *fp;
    int newpriv = 0;
    int skip_mbx_parse = 0;

#if defined (WELCOMEUSERS) && (defined (AX25) || defined (NETROM))
	char tmp[AXBUF];
#endif

#if defined (HFDD)
	char *cp;
#endif
#if defined (HFDD) || defined (HTTPVNC)
	char *gpasswd;
	int anony = 0;
#endif
	long t_l = (long)t; /* 01Oct2009, Maiko, Bridge var for 64 bit warning */
/*
    struct usock *up;
    if((up = itop(s)) != NULLUSOCK)
    if(up->iface->flags & NO_AX25)
 */
    sockmode(s,SOCK_ASCII);
    sockowner(s,Curproc);   /* We own it now */
    /* Secede from the parent's sockets, and use the network socket that
     * was passed to us for both input and output. The reference
     * count on this socket will still be 1; this allows the domboxbye()
     * command to work by closing that socket with a single call.
     * If we return, the socket will be closed automatically.
     */
    close_s(Curproc->output);
    close_s(Curproc->input);
    Curproc->output = Curproc->input = s;
  
    if((m = newmbx()) == NULLMBX){
#ifdef MBOX_FULL_MSG
        j2tputs(MBOX_FULL_MSG);
#else
        j2tputs("Too many mailbox sessions\n");
#endif
        rval = FLUSHWAIT;   /* limit attempts to flush msg */
        while(socklen(s,1) > 0 && rval--)
            j2pause(500);
        return;
    }
  
    /* We'll do our own flushing right before we read input */
    j2setflush(s,-1);
  
    m->proc = Curproc;
    m->user = s;
    m->escape = 20;     /* default escape character is Ctrl-T */
    m->type = (t == NULL) ? TELNET_LINK : (int)t_l;
  
#ifdef TIPSERVER
#ifdef XMODEM
    if(m->type == TIP_LINK) {
        tip = (struct tipcb *) p;
        tip->raw=0;
        m->tip=tip;
    }
#endif
#endif
  
    /* discard any remaining input */
    /*
    while(socklen(s,0))
    recv_mbuf(s,NULL,0,NULLCHAR,0);
    */

#ifdef HFDD

	/* 05Jan2005, Maiko, Support for HF Digital Device */

	if (m->type == TTY_LINK)
	{
		gpasswd = NULL;
		anony = 1;

		strcpy (m->name, (char*)p);
		cp = strchr (m->name,'-');
		if (cp != NULLCHAR)			/* get rid of SSID */
               *cp = '\0';

		m->family = AF_HFDD;	/* make sure mbox list shows proper info */

		/* 16Nov2009, Maiko, Helps split up multiple ifdef situations */
		if (login_more_direct (m, gpasswd, &anony) == -1)
			return;
	}
	else

#endif	/* end of HFDD */

#ifdef HTTPVNC

	/* 03Jul2009, Maiko, Browser based VNC sessions */

	if (m->type == WEB_LINK)
	{
		PASSINFO *passinfo = (PASSINFO*)p;

		strcpy (m->name, passinfo->name);
		gpasswd = j2strdup (passinfo->pass);

		m->family = AF_VNC;		/* make sure mbox list shows proper info */

		// log (-1, "mbx [%s] [%s]", m->name, gpasswd);

		/* 16Nov2009, Maiko, Helps split up multiple ifdef situations */
		if (login_more_direct (m, gpasswd, &anony) == -1)
			return;

    	m->morerows = 0;  /* 19Nov2010, Maiko, No -more- prompt on browser */
	}
	else

#endif	/* end of HTTPVNC */

 /* get the name of the remote station */
	if (mbx_getname (m) == -1)
	{
		exitbbs(m);
		return;
	}

    Totallogins++;

#ifdef	J2MFA
    if (m->family == AF_INET)
    	logmbox (s, m->name, "open (MFA)");
	else
#endif
    logmbox (s, m->name, "open");
  
#ifdef RSYSOPSERVER
    if(m->type == RSYSOP_LINK) {
       m->state = MBX_CMD;         /* start in command state */
       tputc ('\n');
       dosysop (1, (char **)0, (void *)m);
       logmbox (m->user, m->name, "exit");
       exitbbs(m);
       return;
    }
#endif

    if(m->privs & IS_BBS)
        m->sid |= MBX_SID; /*force bbs status*/
    else if(m->privs & IS_EXPERT)
        m->sid |= MBX_EXPERT;
#ifdef FBBCMP
    if(m->privs & NO_FBBCMP)
        m->sid |= MBX_FOREGO_FBBCMP;
#endif
  
    loguser(m);

#ifdef	J2MFA
	/*
	 * 17Nov2020, Maiko, originally called inside mbx_getname() but seeing
	 * we need 'IPemail', which is set in the loguser() call, so now moving
	 * this out of mbx_getname() and putting it here instead on 19Nov2020.
	 *
	 * 20Nov2020, Maiko, was thinking we should leave BBS users out of this,
	 * because the forwarding scripts would break, they're not that advance,
	 * but then if we leave them out, what will stop someone from using those
	 * calls (fake login attempt) from external sources, so NO, each telnet
	 * needs go through this process. Perhaps BBS users can be given a fixed
 	 * code ? just make sure I support string comparision in forward.bbs !
	 * 
	if (!anony && !(m->privs & IS_BBS) && !mfa_validation (m))
	 *
	 * 21Nov2020, Maiko (VE4KLM), adding subnet exclusions, there's no
	 * reason to prompt MFA for internal networks or the 'bbs' console
	 * command for that matter, will most likely annoy many sysops :)
	 *
	 * 26Nov2020, Maiko, Oops I misinterpreted the use of &anony, after
	 * discovering a non telnet connect made it's way into here, anony
	 * simply tells you if ftpusers has a '*' password entry or not. It
	 * has nothing to do with whether it's telnet or NETROM or ax25, so
	 * use the m->family instead to identify as an 'internet' proto.
	 *
	if (!anony && !exclude && !mfa_validation (m))
	 */

	if ((m->family == AF_INET) && !mfa_excluded (m) && !mfa_validation (m))
	{
		logmbox (m->user, m->name, "auth rejected");
		exitbbs (m);
		return;
	}

#endif

    m->state = MBX_CMD;     /* start in command state */

#ifdef MAILCMDS
#if defined(MBFWD) && defined(FBBFWD)
#ifdef FBBCMP
#ifdef	B2F
	if (Mfbb == 3 && !(m->sid & MBX_FOREGO_FBBCMP))	
	{
		/*
		 * 24Apr2008, Maiko, If this station is in our noB2F list, then
		 * just advertise the regular FBBCMP in the SID, not B2F. It seems
		 * that putting '2' in the SID for incoming FBB 7.00 forwards messes
		 * up compressed forwarding (FBB 'seems' to insert 2 extra bytes).
		 */
		if (j2noB2F (m->name))
        	j2tputs (MboxIdFC);
		else
			j2tputs (MboxIdB2F);
	}
	else
#endif
    if (Mfbb == 2 && !(m->sid & MBX_FOREGO_FBBCMP))
        j2tputs (MboxIdB1F);	/* j2tputs (MboxIdFC); 27Jun2016 */
    else
#endif
    if (Mfbb)
        j2tputs (MboxIdF);
    else
#endif
    j2tputs (MboxId);
#endif
  
    /* Say 'hello' only if user is not a bbs - WG7J */
#ifdef MAILCMDS
    if(!(m->sid & (MBX_SID+MBX_EXPERT))) {
#endif
 
/* 05Jun2005, Added option to leave out extraneous welcome stuff */
#ifdef	WELCOMEUSERS 

#ifdef USERLOG
        tprintf(Mbwelcome,m->username ? m->username : m->name);
#else
        tprintf(Mbwelcome,m->name);
#endif
  
#if defined AX25 || defined NETROM
        if(m->family == AF_INET)
#endif
            tprintf(Mbbanner,Hostname,Version);
#if defined AX25 || defined NETROM
        else
            tprintf(Mbbanner,pax25(tmp,Bbscall),Version);
#endif
  
        /* How many users are there currently ? */
        tprintf(CurUsers,BbsUsers,BbsUsers == 1 ? "" : "s");

#endif	/* WELCOMEUSERS */
  
#ifdef MAILCMDS
        /* Do we accept third party mail ? */
        if(!ThirdParty)
            j2tputs(Mbwarning);
#endif
        tputc('\n');
  
        /* Is there a message of the day ? */
        if((fp = fopen(Motdfile,READ_TEXT)) != NULLFILE) {
            sendfile(fp,m->user,ASCII_TYPE,0, m);
            fclose(fp);
        }
  
#ifdef MAILCMDS
    }
#ifdef J2_GLOBAL_MAILBOX_SYSTEM
   /*
    * 15Apr2011, Maiko (VE4KLM), Time to start this concept of a global
    * mailbox system (ability to just use callsigns, no suffix needed),
    * when delivering mail on a world wide scale - very experimental.
    */
    sync2gms ();
#endif
    if(!(m->sid & MBX_SID)) {
        char *aargv[2];
        /* Enable our local message area,
         * only if we're not a bbs - WG7J
         */
        aargv[1] = m->name;
        doarea(2,aargv,m);
#ifdef USERLOG
        /* Tell about new arrived mail in message areas - WG7J */
        if(Mbnewmail)
            listnewmail(m,1);
  
#ifdef REGISTER
        /* See if the username is empty. If so, the user hasn't
         * registerd yet, so we need to beep and remind.
         */
        if(MbRegister && !m->username)
            tprintf("\n\007Please type 'REGISTER' at the > prompt.\n");
#endif /* REGISTER */
#endif /* USERLOG */
    }
#endif /* MAILCMDS */
  
    /* Send prompt */
    putprompt(m);
  
    /* now get commands */
    while(mbxrecvline(m) != -1){

	/* log (-1, "%s", m->line); */

#ifdef MAILCMDS
        /* Only tell about new mail when in our own area - WG7J */
        if(!(m->sid & MBX_SID)){
            if(isnewprivmail(m) > 0L)
                newpriv = 1;
            else
                newpriv = 0;
            /* Do not check mailfile if we're bbs - WG7J*/
            scanmail(m);
        }
#endif
        skip_mbx_parse = 0;

        /* check for an alias - WG7J */
        if((a=findalias(m->line)) != NULL)
		{
            strcpy(m->line,a->cmd);
            if(m->line[0]=='@') /* VK2ANK: expansion begins with '@' ? */
			{
			 	/* then execute as a console cmd */
                cmdparse(Cmds, m->line+1, NULL);

                skip_mbx_parse = 1;	/* replaces GOTO 'skip_mbx_parse' */
            }
        }
		if (!skip_mbx_parse)
 		{ 
        	if((rval = mbx_parse(m)) == -2)
            	break;
        	if(rval == 1)
            	j2tputs("Bad syntax.\n");
		}

#ifdef MAILCMDS
        if(newpriv) {
            j2tputs("You have new mail. ");
            if(m->areatype != PRIVATE)
                tprintf("Change area with 'A %s'. ", m->name);
            j2tputs("Please Kill when read!\n");
        }
#endif
        putprompt(m);
        m->state = MBX_CMD;
    }

#ifdef SYSEVENT
	/* 02Feb2008, New notification scripts - VE4KLM */
	j2sysevent (bbsdisc, m->name);
#endif

    logmbox (m->user, m->name, "exit");	/* N5KNX: log exits */

    exitbbs(m);
}
  
void
exitbbs(m)
struct mbx *m;
{
    struct mbx *mp,*pp;
    int i;
  
    /* Since we've added an ax.25 T2 timer, which will delay the flushing of any
       queued output, and we could lose some of this output when we close the
       user's socket [this sends an immediate disconnect and frees queued o/p]
       we'll try to force out anything queued now.  But we must be careful not
       to get stuck here forever (ie, circuit dropped out but we have infinite
       retries set). Main idea is to shutdown, but flush if we can! n5knx/k5jb
     */
    i = FLUSHWAIT;

  /* wait for I frame to be ACKed - K5JB */
    while(socklen(m->user,1) > 0 && i--)
        j2pause(500);

#ifdef	HFDD 
/*
 * 06Dec08, Maiko, If this is an HFDD session, then the above flush delay
 * is useless, since the HFDD code is using twin socket pairs to move data
 * between the mailbox and HFDD hostmode driver. Therefore, the above will
 * always return immediately (since m->user is not a 'direct' socket to the
 * HFDD driver code). So we have to put in an HFDD specific delay below.
 *
 * This has been the source of a problem that has plagued me from day one,
 * that being - why is the FQ that JNOS ends with, never sent to Airmail,
 * resulting in Airmail not disconnecting properly. Solved by this !!!
 *
 * Right now I just give the connection a bit of time, technically I should
 * check the status of the number of echo bytes left instead. This works for
 * now (the beta version). This problem is FINALLY solved ! It has been an
 * issue since I started the HFDD stuff a few years ago. Now that this part
 * has been properly fixed, the HFDD code is in a sense - finally done.
 *
 * 10Apr2014, Maiko (VE4KLM), NOPE, this was not solved, we have to wait
 * till the connection is toast (or reasonably considered toast) before we
 * go any further.
 */
	if (m->family == AF_HFDD)
	{
		int flushcnt;	/* believe it or not 3 minutes is reasonable */

		extern int hfdd_conn_flag;	/* In the 'hfddrtns.c' module */

		log (-1, "HFDD - waiting 4 disconnect");

		for (flushcnt = 180; hfdd_conn_flag == 1 && flushcnt; flushcnt--)
			j2pause (1000);

		if (flushcnt)
			log (-1, "HFDD - now disconnected");
		else
			log (-1, "HFDD - what happened ?");
	}
#endif

    /* Moving the socket close call to here
     * will send a disconnect to the user before cleaning up
     * the user's data structure. This gives a faster response perception
     * to the user - WG7J
     * N5KNX: but we must be careful to reset Curproc->{input,output} since
     * otherwise killproc() will try again to close these sockets, and by
     * then some other process may own it.  This sure is a kludge!
     */
    close_s(Curproc->output);
    if (itop(Curproc->output) == NULLUSOCK) {
        if (Curproc->input == Curproc->output) Curproc->input = -1;
        Curproc->output = -1;
    }
  
  
#ifdef MAILCMDS

#ifdef DONT_COMPILE		/* 12Mar2012, Maiko, Leave this out for now */

#ifdef WPAGES
#ifdef TNOS_240
	if (m->change & CHG_WP)
#endif
		if (!m->homebbs || m->homebbs[0] != '-') /* should ALWAYS do this */
			add_WPUpdate (m->name, m->homebbs, m->username, 'U');
#endif

#endif	/* end of DONT_COMPILE */

    closenotes(m);
    free(m->to);
    free(m->tofrom);
    free(m->origto);
    free(m->origbbs);
    free(m->subject);
    free(m->date);
    free(m->tomsgid);
#endif
    free(m->path);
    free(m->startmsg);
#ifdef USERLOG
    free(m->username);
    free(m->IPemail);
    free(m->homebbs);
#endif
#ifdef MAILCMDS
    /* Close the tempfiles if they are not nullpointers - WG7J */
    if(m->tfile != (FILE *) 0)
        fclose(m->tfile);
    if(m->tfp != (FILE *) 0)
        fclose(m->tfp);
    if(m->mfile != (FILE *) 0)
        fclose(m->mfile);
    if(m->stdinbuf != NULLCHAR)
        free(m->stdinbuf);
    if(m->stdoutbuf != NULLCHAR)
        free(m->stdoutbuf);
    free((char *)m->mbox);
#endif
    /* now free it from list */
    for(mp=Mbox,pp=NULLMBX;mp && mp!=m;pp=mp,mp=mp->next);
    if(!mp)
        /* what happened ??? */
        return;
    if(pp==NULLMBX)     /* first one on list */
        Mbox = Mbox->next;
    else
        pp->next = m->next;
#ifdef FBBCMP
/* If this was an FBB system capable of compression, we most likely have msgs
   in the SMTP queue that we delayed processing until now.
*/
    if (m->sid & MBX_FBBCMP) smtptick(NULL);    /* start processing SMTP queue */
#endif
    free((char *)m);
    BbsUsers--;
}
  
/**********************************************************************/

#ifdef  APRSD
extern int dombaprs (int argc, char *argv[], void *p);
#endif
 
static struct cmds DFAR Mbcmds[] = {
#ifdef MAILCMDS
    { "",             doreadnext,     0, 0, NULLCHAR },
#endif
    { "?",            dombhelp,       0, 0, NULLCHAR },
#ifdef MAILCMDS
    { "area",     doarea,     0, 0, NULLCHAR },
#endif
    { "alias",    dombalias,  0, 0, NULLCHAR },
#ifdef  APRSD
	{ "aprsc",	dombaprs,	0,	0,	NULLCHAR },
#endif
    { "bye",      domboxbye,  0, 0, NULLCHAR },
#ifdef GATECMDS
#if defined AX25 || defined NETROM
    { "connect",  dombconnect,0, 0, NULLCHAR },
#ifdef NOINVSSID_CONN
    { "connisc",  dombconnect,0, 0, NULLCHAR },
#endif
#endif
#endif
#ifdef CONVERS
    { "convers",  dombconvers,0, 0, NULLCHAR },
#endif
#ifdef FILECMDS
#ifdef XMODEM
    { "download", dodownload, 0, 0, NULLCHAR },
#else
    { "download", dodownload, 0, 0, NULLCHAR },
#endif
#endif
#ifdef GATECMDS
    { "escape",       dombescape,     0, 0, NULLCHAR },
#endif
#ifdef FOQ_CMDS
    { "finger",       dombfinger,     0, 0, NULLCHAR },
#endif
    { "help",         dombhelp,       0, 0, NULLCHAR },
    { "info",         dombhelp,       0, 0, NULLCHAR },
    { "iheard",   dombipheard,0, 0, NULLCHAR },
    { "iproute",  dombiproute,0, 0, NULLCHAR },
#ifdef  AX25
    { "jheard",   dombjheard, 0, 0, NULLCHAR },
#endif
#ifdef MAILCMDS
    { "kill",     dodelmsg,   0, 0, NULLCHAR },
    { "list",     dolistnotes,0, 0, NULLCHAR },
#endif
    { "mboxuser", dombusers,  0, 0, NULLCHAR },
#if defined GATECMDS && defined NETROM
    { "nodes",    dombnrnodes,0, 0, NULLCHAR },
    { "nroutes",  dombnrneighbour, 0, 0, NULLCHAR },
#if defined (NETROM) && defined (NRR)
    { "nrr",	donrr, 0, 0, NULLCHAR },
#endif
#endif
#if defined FOQ_CMDS && defined TTYLINKSERVER
    { "operator", dochat,     0, 0, NULLCHAR },
#endif
#if defined GATECMDS && defined AX25
    { "ports",    dombports,  0, 0, NULLCHAR },
#endif
#ifdef GATECMDS
    { "ping",     dombping,   0, 2, "PI <host> [<len>] [<timeout>]" },
#endif
#if defined FOQ_CMDS && defined CALLCLI
    { "query", dombcallbook, 0, 2, "Q callsign\nMultiple callsigns allowed per line" },
#endif
#ifdef MAILCMDS
    { "read",     doreadmsg,  0, 0, NULLCHAR },
    { "send",         dosend,         0, 0, NULLCHAR },
#if defined USERLOG && defined REGISTER
    { "register",     doregister,     0, 0, NULLCHAR },
#endif
#endif
#ifdef GATECMDS
    { "telnet",   dombtelnet, 0, 2, "T hostname" },
#endif
#ifdef FILECMDS
#ifdef XMODEM
    { "upload",   dombupload, 0, 2, "U[X] <filename>" },
#else
    { "upload",   dombupload, 0, 2, "U <filename>" },
#endif
#endif
#ifdef MAILCMDS
    { "verbose",  doreadmsg,  0, 0, NULLCHAR },
#endif
#ifdef FILECMDS
    { "what",     dowhat,     0, 0, NULLCHAR },
#endif
#ifdef WPAGES
    { "wpages",   dombwpages, 0, 2, "WP call [@bbs]" },
#endif
    { "xpert",    dombexpert, 0, 0, NULLCHAR },
#ifdef FILECMDS
    { "zap",      dozap,      0, 2, "Z filename" },
#endif
#ifdef MAILCMDS
    { "[",        dosid,      0, 0, NULLCHAR },
#ifdef MBFWD
    { "f>",       dorevfwd,   0, 0, NULLCHAR },
#ifdef FBBFWD
#ifdef B2F
    { "fc",	dofbbfwd,   0, 0, NULLCHAR },
#endif
#ifdef FBBCMP
    { "fa",       dofbbfwd,   0, 0, NULLCHAR },
#endif
    { "fb",       dofbbfwd,   0, 0, NULLCHAR },
    { "ff",       dofbbfwd,   0, 0, NULLCHAR },
    { "fq",       domboxbye,  0, 0, NULLCHAR },
#endif
#endif
#endif
    { "@",        dosysop,    0, 0, NULLCHAR },
    { "***",      dostars,    0, 0, NULLCHAR },
    { ";",        dombsemicolon, 0, 0, NULLCHAR },
    { NULLCHAR,   NULLFP((int,char **,void *)), 0, 0, "Huh?" }
};
  
/* "twocmds" defines the MBL/RLI two-letter commands, eg. "SB", "SP" and so on.
 * They have to be treated specially since cmdparse() wants a space between
 * the actual command and its arguments.
 * "SP FOO" is converted to "s  foo" and the second command letter is saved
 * in m->stype. Longer commands like "SEND" are unaffected, except for
 * commands starting with "[", i.e. the SID, since we don't know what it will
 * look like.
 */
static char twocmds[] =
#ifdef MAILCMDS
"aklrsv"
#endif
#ifdef FILECMDS
"du"
#endif
"[mx";
  
int
mbx_parse(m)
struct mbx *m;
{
    char *cp, *isSBfwd;		/* 19Jul2018, Maiko, Identify non FBB fwd */
    int i;
    char *newargv[2];

#ifdef	DEBUG_SID
	extern int debug_sid_user;	/* 31Aug2013, Maiko (VE4KLM) */
	debug_sid_user = m->user;
#endif  

   /* log (m->user, "mbx_parse [%s]", m->line); */

/*
 * 16Jul2018, Maiko (VE4KLM), Yet another 'hack', the lower casing is messing
 * up a few things in the FBB functions, particulary the case of the BID, so
 * bit of a kludge, but for starters do NOT lower case the proposals ! To be
 * frank, just pass these direct to cmdparse(), and skip the BS below. If we
 * really need to manipulate the case specific to FBB, then do it in the FBB
 * code, not here !!! Leave the SID alone, that's another can of worms :(
 */
      if (strnicmp (m->line, "FA", 2) &&
              strnicmp (m->line, "FB", 2) &&
              strnicmp (m->line, "FC", 2) &&
              strnicmp (m->line, "FF", 2))
	{
		/*
         * 19Jul2018, Maiko, look for fbbbid, IF it exists then only lowercase
         * the buffer up to the BID, leave BID case alone ! This will catch the
		 * SB (no compression) direct call to mbx_parse() from fbbfwd.c ! Don't
		 * want to NOT lowercase the entire buffer, since I don't know what the
		 * affect may be on the standard mailbox functionality ...
         */
        isSBfwd = strstr (m->line, "$fbbbid");
        if (isSBfwd)
                isSBfwd = strstr (isSBfwd, "->") + 2;

    /* Translate entire buffer to lower case */
#ifdef __TURBOC__
    (void)strlwr(m->line);
#else
    for (cp = m->line; *cp != '\0' && cp != isSBfwd; ++cp)
        if(isupper(*cp))
            *cp = tolower(*cp);
#endif

    /* Skip any spaces at the begining */
    for(cp = m->line;isspace(*cp);++cp)
        ;
    m->stype = ' ';
    if(*cp != '\0' && *(cp+1) != '\0') {
        for(i=0; i< (int)strlen(twocmds); ++i){
            if(*cp == twocmds[i] && (isspace(*(cp+2)) || *(cp+2) == '\0'
            || *cp == '[')){
                if(islower(*(++cp)))
                    m->stype = toupper(*cp); /* Save the second character */
                else
                    m->stype = *cp;
		/*
		 * 12May2016, Maiko (VE4KLM), Why is there a need to overwrite
		 * the command line (2nd byte of) with a SPACE character ? I've
		 * noticed this several times over the years, most recently in
		 * my attempt to write a SID Capture function, the first letter
		 * of the SID (the software type) is blank, really, like why ?
		 *
		 * guessing cause KM, KA, RM, RA, whatever don't get caught
		 * as commands, but 'K ', 'R ', do, that makes sense, but I
		 * would rather not alter the original mline, what to do ?
		 *
		 */
                *cp = ' ';

                break;
            }
        }
    }
		}	/* end of kludge to hand off FBB stuff direct to cmdparse () */

    /* log (m->user, "final mbx_parse [%s] [%c]", m->line, m->stype); */

#ifdef MAILCMDS
    /* See if the input line consists solely of digits */
    cp = m->line;
    for(cp = m->line;isspace(*cp);++cp)
        ;
    newargv[1] = cp;
    for(;*cp != '\0' && isdigit(*cp);++cp)
        ;
    if(*cp == '\0' && strlen(newargv[1]) > 0) {
        newargv[0] = "r";
        return doreadmsg(2,newargv,(void *)m);
    } else
#endif
        return cmdparse(Mbcmds,m->line,(void *)m);
}
  
/* This works like recvline(), but telnet options are answered and the
 * terminating newline character is not put into the buffer. If the
 * incoming character equals the value of escape, any queued input is
 * flushed and -2 returned.
 *
 * mbxrecvline() now can gobble up suboptions - 94/02/14 VE4WTS
 * OH2BNS/N5KNX: don't test for telnet options unless TCP link type
 */
int
mbxrecvline(m)
struct mbx *m;
{
    int s = m->user;
    int escape = m->escape;
    char *buf = m->line;
    int c, cnt = 0, opt,cl;
 
    int save_errno;
 
    if(buf == NULLCHAR)
        return 0;
    tflush();
    j2alarm(Mbtdiscinit*1000);   /* Start inactivity timeout - WG7J */
    while((c = recvchar(s)) != EOF){
        if(c == IAC &&          /* Telnet command escape */
           (m->type == TELNET_LINK || m->type == TIP_LINK || m->type == RSYSOP_LINK)) {  /* OH2BNS: makes sense? */
            if((c = recvchar(s)) == EOF)
                break;
            if(c >= 250 && c < 255 && (opt = recvchar(s)) != EOF){
                if (opt <= MAXTELNETOPT)  /*  range we can handle ? */
                  switch(c){
                    case SB:
                        opt=recvchar(s); /* Get the real option */
                        if(opt==EOF)
                            break;
                        cl=opt;
                        c=recvchar(s);
                    /* Gobble up until we see IAC SE */
                        while((c!=EOF) && !(cl==IAC && c==SE)){
                        /* maybe check for timeout here, in case someone
                           happened to send a binary file with the IAC SB
                           sequence in it. */
                            cl=c; /* keep track of second last char read */
                            c=recvchar(s);
                        }
                    /* and tell the client where to go... */
                    /* tprintf("%c%c%c",IAC,WONT,opt); */
                        break;
                    case WILL:
                        if(opt==TN_LINEMODE){
                        /* we WANT linemode */
                            if (!OPT_IS_DEFINED(m,opt) || !OPT_IS_SET(m,opt)) {
                                tprintf("%c%c%c",IAC,DO,opt);
                                SET_OPT(m,opt);
                            }
                        /* Tell client to do editing */
                                tprintf("%c%c%c%c%c%c%c",IAC,SB,TN_LINEMODE,1,1,IAC,SE);
                        } else if (!OPT_IS_DEFINED(m,opt) || !OPT_IS_SET(m,opt)) {
                            tprintf("%c%c%c",IAC,DONT,opt);
                            RESET_OPT(m,opt);
                        }
                        break;
                    case WONT:
                        if (!OPT_IS_DEFINED(m,opt) || OPT_IS_SET(m,opt)) {
                            tprintf("%c%c%c",IAC,DONT,opt);
                            RESET_OPT(m,opt);
                        }
                        break;
                    case DO:
                        if (!OPT_IS_DEFINED(m,opt) || !OPT_IS_SET(m,opt)) {
                            tprintf("%c%c%c",IAC,WONT,opt);
                            RESET_OPT(m,opt);
                        }
                        break;
                    case DONT:
                        if (!OPT_IS_DEFINED(m,opt) || OPT_IS_SET(m,opt)) {
                            tprintf("%c%c%c",IAC,WONT,opt);
                            RESET_OPT(m,opt);
                        }
                        /*break;*/
                }
                tflush();
                j2alarm(Mbtdiscinit*1000);   /* restart inactivity timeout */
                continue;
            }
            else if(c != IAC) {  /* telnet non-option cmd "c" or EOF read */
                j2alarm(Mbtdiscinit*1000);   /* restart inactivity timeout */
                continue;
            }
        }
        /* ordinary character or escaped IAC */
        if(c == '\r' || c == '\n')
            break;
        if(uchar(c) == escape && !(m->sid & MBX_SID)){ /* treat esc from BBS as data */
            if(socklen(s,0)) /* discard any remaining input */
                recv_mbuf(s,NULL,0,NULLCHAR,0);
            cnt = -2;
            break;
        }
        /* Handle <bs> and <del> chars - from wa7tas */
        if((c == 8 || c == 127) && cnt > 0) {
            *--buf = 0;
            cnt--;
        } else if (c) {  /* discard NULs */
            *buf++ = c;
            ++cnt;
        }
        if(cnt == MBXLINE - 1)
            break;
    }
	save_errno = errno;

    j2alarm(0);    /* disable inactivity timeout */

    if(c == EOF && cnt == 0)
	{
		if (save_errno == ETIME)
			log (s, "mbxrecvline timed out");	/* 21Jan2015 */

        return -1;
	}
    *buf = '\0';
    return cnt;
}
  
/* New forwarding option, simply ignore all data - WG7J */
int
dombsemicolon(int argc,char *argv[],void *p) {
  
    return 0;
}
  
/* Determine what type of prompt is optimal, ie, can we read just one char? */
int
charmode_ok(m)
struct mbx *m;
{
#ifndef LINEMODE_PROMPT_ALWAYS
    if (m->type == TELNET_LINK || m->type == TIP_LINK) {
        if (!(m->sid & MBX_LINEMODE)) return 1;   /* char mode OK (see XP cmd) */
    }
#endif /* LINEMODE_PROMPT_ALWAYS */
    return 0;
}

  
int
domboxbye(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
  
    m = (struct mbx *)p;
  
#ifdef USERLOG
    updatedefaults(m);       /* moved here so we also track BBSes */

#ifdef MAILCMDS
    setlastread(m);
#endif
#endif /* USERLOG */

    /* for bbs's, just disconnect */
    if(m->sid & MBX_SID)
        return -2;
  
    /* Now say goodbye */
    if(!(m->privs & IS_EXPERT))
        tprintf("\nThank you %s, for calling %s JNOS.\n",
#ifdef USERLOG
        m->username ? m->username : m->name,
#else
        m->name,
#endif
        Hostname);
#ifdef TIPSERVER
    if(m->type == TIP_LINK)
        j2tputs("Please hang up now.\n");
#endif
    tflush();
    return -2;      /* signal that exitbbs() should be called */
}
  
static int
dombhelp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char buf[255];
    int i;
    FILE *fp;
    struct mbx *m = (struct mbx *)p;
    struct alias *a;
  
    if(*argv[0] == '?') {
        if(MbShowAliases && AliasList) {
  
            j2tputs("Aliases:");
            for(a=AliasList;a;a=a->next)
                tprintf(" %s",a->name);
            tputc('\n');
        }
        j2tputs(Longmenu);
        return 0;
    }
  
    buf[0] = '\0';
    if(argc > 1) {
        for(a=AliasList;a;a=a->next)
            if(!stricmp(a->name,argv[1])) {
                sprintf(buf,"%s/%s.hlp",Helpdir,argv[1]); /* use lower-case */
                break;
            }
        if (! a)
          for(i=0; Mbcmds[i].name != NULLCHAR; ++i)
            if(!strncmp(Mbcmds[i].name,argv[1],strlen(argv[1]))) {
                sprintf(buf,"%s/%s.hlp",Helpdir,Mbcmds[i].name);
                break;
            }
    }
    if(buf[0] == '\0')
	{
        if(*argv[0] == 'i')
		{
            /* INFO command */
            tprintf(Nosversion,Version);
            sprintf(buf,"%s/info.hlp",Helpdir);
        }
		else
			sprintf(buf,"%s/help.hlp",Helpdir);
	}
  
    if((fp = fopen(buf,READ_TEXT)) != NULLFILE) {
        sendfile(fp,Curproc->output,ASCII_TYPE,0,m);
        fclose(fp);
    } else {
        if(*argv[0]!='i')
            j2tputs("No help available.\n");
    }
    return 0;
}
  
extern void dumproute __ARGS((struct route *rp,char *p));
extern char RouteHeader[];
  
/* Show non-private routes only */
int
dombiproute(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int i,bits;
    struct route *rp;
    struct mbx *m = (struct mbx *)p;
    char buf[85];
  
    if(m->privs & NO_LISTS) {
        j2tputs(Noperm);
        return 0;
    }
  
    j2tputs(RouteHeader);
    for(bits=31;bits>=0;bits--){
        for(i=0;i<HASHMOD;i++){
            for(rp = Routes[bits][i];rp != NULLROUTE;rp = rp->next){
                if(!(rp->flags & RTPRIVATE)) {
                    dumproute(rp,buf);
                    if(tprintf("%s\n",&buf[4]) == EOF)
                        return 0;
                }
            }
        }
    }
    if(R_default.iface != NULLIF && !(R_default.flags & RTPRIVATE)) {
        dumproute(&R_default,buf);
        if(tprintf("%s\n",&buf[4]) == EOF)
            return 0;
    }
    return 0;
}
  
  
static int
dombexpert(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
  
    m = (struct mbx *)p;
  
	switch(m->stype)
	{
#ifdef	WPAGES
	/* 19Mar2012, Maiko, User can now set WP preferences */
        case 'W':
            if (argc == 1)
			{
				int opt = wpage_options (m->name, 0);
				tprintf ("WP options for 'SP' - 1=disable, 2=show/prompt, 3=show/noprompt, 4=force it\n");
				if (!opt)
					tprintf ("you have not defined an option yet, or it's corrupt\n");
				else
                	tprintf ("option %d is currently set\n", opt);
			}
			else wpage_options (m->name, atoi (argv[1]));
            break;
#endif
        case 'M':
            if(argc == 1)
                tprintf("-more- after %d lines\n",m->morerows);
            else {
                m->morerows = atoi(argv[1]);
            }
            break;
        case 'A':
            m->sid ^= MBX_AREA;
            break;
        case 'N':
            m->sid ^= MBX_NRID;
            break;
        case 'P':
            m->sid ^= MBX_LINEMODE;
            tprintf("LINEMODE is now %sabled\n", m->sid&MBX_LINEMODE ? "en" : "dis");
            break;
  
#if defined USERLOG && defined REGISTER
        case 'R':
            if(argc > 1) {
            /* Change the state of the 'Reply-to' header */
                if(!stricmp(argv[1],"on"))
                    m->sid |= MBX_REPLYADDR;
                else if(!stricmp(argv[1],"off"))
                    m->sid &= ~MBX_REPLYADDR;
            }
            tprintf("'Reply-to: %s' header is %sadded when sending mail.\n",
            ( m->IPemail ?
            ((m->sid & MBX_REPLYADDR) ? m->IPemail : "") :
            "" ),
            (m->sid & MBX_REPLYADDR) ? "" : "not " );
            if((m->sid & MBX_REPLYADDR) && !m->IPemail)
                tprintf("Please 'register' to set your email reply-to address!\n");
            break;
#endif
  
  
#if defined(ENCAP) && defined(MBOX_DYNIPROUTE)
        case 'G':
            /* XG 44-net-addr   -- add encap route to user's gateway system.
             * Allows g/w with dynamic addr to "register" an encapped route
             * with us (for 15 mins or so) [see DYNIPROUTE_TTL]. -- n5knx
             */
            if(m->privs & XG_ALLOWED && argc > 1 && m->family == AF_INET)  {
                struct sockaddr_in insock;
                struct route *rp;
                int len,logit;
                unsigned int nbits;
                int32 ampr_addr;
                char *p;
#ifndef DYNIPROUTE_TTL
#define DYNIPROUTE_TTL 900L
#endif

                len = sizeof(insock);
                if ((p=strchr(argv[1],'/')) != NULLCHAR) {
                    *p++='\0';
                    nbits = atoi(p);
                    if (nbits < 16) nbits=16;  /* surely a mistake! */
                }
                else nbits=32;
                if ((ampr_addr = resolve(argv[1])) == 0L || ampr_addr == Loopback.addr)
                    tprintf(Badhost,argv[1]);
                else if (j2getpeername(m->user,(char *)&insock,&len) == 0 && len != 0) {
                    /* replace only routes to Loopback, or to encap with a running timer */
                    if ((rp=rt_lookup(ampr_addr))!=NULLROUTE && (rp->iface!=&Encap
                        || !dur_timer(&rp->timer) || !run_timer(&rp->timer)))
					{
                		j2tputs("XG invalid.\n");  /* Don't be too precise */
            			break;
					}
                    logit=(!rp || rp->gateway != insock.sin_addr.s_addr);/* G/W changed? */
                    if(rt_add(ampr_addr,nbits,insock.sin_addr.s_addr,&Encap,1,DYNIPROUTE_TTL,1) == NULLROUTE)
                        j2tputs("Can't add encap route.\n");
                    else {
                        if (logit) {/* GW changed? */
                            char adest[25];
                            strcpy(adest,inet_ntoa(ampr_addr)); /* log() overwrites static buf */
                            log(m->user,"XG %s/%d", adest, nbits);
                        }
#ifdef  RSPF_EVEN_IF_PRIVATE /* unlikely to be useful */
                        rspfrouteupcall(ampr_addr,nbits,insock.sin_addr.s_addr); /* Do an RSPF upcall */
#endif  /* RSPF */
                    }
                }
                /* else j2tputs(Badsocket); Unlikely to happen; if it does j2tputs() fails too */
            }
            else
                j2tputs("XG invalid.\n");  /* Don't be too precise */
            break;
#endif /* ENCAP && MBOX_DYNIPROUTE */

        default:
            m->sid ^= MBX_EXPERT;
            break;
    }
    return 0;
}
  
#if defined FOQ_CMDS && defined TTYLINKSERVER
extern char SysopBusy[];
  
static int
dochat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char buf[8], *newargv[3];
    struct mbx *m;
  
    m = (struct mbx *)p;
  
    if (MAttended) {
        m->state = MBX_CHAT;
        newargv[0] = "C";
        newargv[1] = Hostname;
        sprintf(buf,"%d",IPPORT_TTYLINK);
        newargv[2] = buf;
        m->startmsg = mallocw(50);
        sprintf(m->startmsg,"*** MBOX Chat with %s\n",m->name);
        return dombtelnet(3,newargv,p);
    } else {
        j2tputs(SysopBusy);
    }
    /* It returns only after a disconnect or refusal */
    return 0;
}
#endif /* TTYLINKSERVER */
  
static int
dombipheard(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m = (struct mbx *)p;
  
    if(m->privs & NO_LISTS) {
        j2tputs(Noperm);
        return 0;
    }
    return doipheard(argc,argv,NULL);
}
  
#ifdef AX25
static int
dombjheard(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct iface *ifp;
    struct mbx *m = (struct mbx *)p;
  
    if(m->privs & NO_LISTS) {
        j2tputs(Noperm);
        return 0;
    }
  
    if(argc > 1){
        if( ((ifp = if_lookup(argv[1])) == NULLIF) || (ifp->type != CL_AX25) ||
        ((ifp->flags & HIDE_PORT) && !(m->privs & SYSOP_CMD)) ) {
            tprintf(Badinterface,argv[1]);
            return 0;
        }
        axheard(ifp);
        return 0;
    }
    for(ifp = Ifaces;ifp != NULLIF;ifp = ifp->next){
        if((ifp->flags & LOG_AXHEARD)  && ( !(ifp->flags & HIDE_PORT) || m->privs&SYSOP_CMD) )
            if(axheard(ifp) == EOF)
                break;
    }
    return 0;
}
#endif
  
#if defined FOQ_CMDS && defined CALLCLI
static int
dombcallbook(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
    char buf[8], *newargv[3];
    extern char *Callserver;
    int req, ret = 0;
  
    m = (struct mbx *) p;
  
    sprintf(buf,"%d",IPPORT_CALLDB);
    newargv[0] = "Q";
    newargv[1] = Callserver;
    newargv[2] = buf;
  
    for (req = 1; req < argc; req++)  {
        if(argv[req] == NULLCHAR)
            return ret;
        m->startmsg = mallocw(80);  /* is freed each time by gw_connect()         */
        sprintf(m->startmsg,"%s\n", argv[req]);
        logmbox (m->user, m->name, "callbook %s", argv[req]);
        tprintf("Looking for \" %s \" in the callbook at %s\n",argv[req],Callserver);
        ret = dombtelnet(3,newargv,p);
    }
    return ret; /* It looks like all possible returns are zero anyway!    */
}
#endif /* CALLCLI */
  
/*Password protection added - 920118, WG7J */
int
dosysop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
    int c;
    int len,pwdc[5],i,valid=0;
    char *cp;
  
    m = (struct mbx *) p;
    logmbox (m->user, m->name, "sysop attempt");
  
    /*If you want anyone with the password to go sysop-mode
     *comment out the next 4 line ! -WG7J
     */
    if(!(m->privs & SYSOP_CMD)){
        j2tputs(Noperm);
#ifdef MAILERROR
        mail_error("%s: SYSOP denied!\n",m->name);
#endif
        return 0;
    }
  
    /*only if set,
     *check for the password before letting users proceed
     */
    m->state = MBX_SYSOPTRY;
    if((len = strlen(Mbpasswd)) != 0) {;
        for (i=0;i<5;i++)
            tprintf("%d ",(pwdc[i]=RANDOM(len))); /*print the random chars*/
        tputc('\n');
        while(1) {
            c = mbxrecvline(m);
            if(c == EOF || c == -2)
                return 0;
            if(*m->line == '\0')
                break;
            cp = m->line;
            for(i=0;i<5;i++)
                if(*cp++ != Mbpasswd[pwdc[i]])
                    break;
            if (i == 5)
                valid = 1;
        }
        if(!valid)
            return 0;
    }
  
    logmbox (m->user, m->name, "now sysop");
    m->state = MBX_SYSOP;
    j2tputs("\n\007Type 'exit' to return\n");
  
    for(;;){
#ifdef EDITOR
        struct usock *up;

        if ((up=itop(Curproc->output)) == NULLUSOCK)
            break;  /* weird error ... disconnect? */
        c = up->refcnt;  /* remember current refcnt */
#endif
#ifdef	DONT_COMPILE
	/* 05Sep2010, Maiko, Useless function in linux, and wrong value */
        tprintf("%lu Jnos> ",farcoreleft());
#else
        tprintf("Jnos> ");
#endif
        /* tflush(); already done in mbxrecvline() */
        if(mbxrecvline(m) < 0)
            break;
        logmbox (m->user, "sysop", "%s", m->line);
        if(cmdparse(Cmds,m->line,NULL) == -2)
            break;
#ifdef EDITOR
        /* editor reads Curproc->input, so we can't call mbxrecvline() until he's
         * finished reading stdin.  If we had the process # we could pwait but
         * that would require a more substantial change to cmdparse, or much
         * more work here. At present only the editor requires special care.
         * [the --more-- prompt's read in morecmd() works by accident, as it's
         * LIFO] -- n5knx
         */
        if(*(m->line) && !strnicmp(m->line, "editor", strlen(m->line))) { /* very special case */
            while(up->refcnt != c)
                j2pause(5000);  /* test every 5s if editor process has quit */
        }
#endif /* EDITOR */
    }
    /* remove potential remote traces - WG7J */
    removetrace();
    return 0;
}
  
/* Handle the "*** Done" command when reverse forwarding ends or the
 * "*** LINKED to" command.
 */
int
dostars(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
    int anony = 1;
    int founddigit = 0;
    long oldprivs;
    char *cp;
  
    m = (struct mbx *)p;
  
    /* Allow 'linked to' from anyone, but reset SYSOP priviledges
     * when the sysop-password is not set !
     * Also try to set the new call !
     * Inspired by Kurt, wb5bbw
     * Check for the strange TEXNET linked message !
     * 920220 - WG7J
     */
    if((argc >= 4) && !strcmp(argv[1],"linked") && !strcmp(argv[2],"to")) {
        if(m->privs & NO_LINKEDTO) {
            j2tputs(Noperm);
#ifdef GWTRACE
            logmbox (m->user, m->name, "LINKED, permission denied");
#endif
            return 0;
        }
#ifdef GWTRACE
        logmbox (m->user, m->name, "LINKED, changed to %s", argv[3]);
#endif
#ifdef USERLOG
#ifdef MAILCMDS
        setlastread(m);
#endif
        updatedefaults(m);
#endif
        strcpy(m->name,argv[3]);
        oldprivs = m->privs; /*Save this !*/
        /* Try to find the privileges of this user from the userfile */
        if((m->privs = userlogin(m->name,NULLCHAR,&m->path,MBXLINE,
            &anony,"linkperm")) == -1)
                {
                    m->privs = 0;
                    free(m->path);
                    m->path = NULLCHAR;
                }
        if(m->privs & EXCLUDED_CMD)
            return domboxbye(0,NULLCHARP,p);
#ifdef AX25
    /* Set the call */
        for(cp=m->name;*cp != '\0';cp++)
            if(isdigit((int)*cp))
                break;
        if(*cp != '\0')
            founddigit = 1;
        if( (setcall(m->call,m->name) == -1) || (!founddigit) ) {
            m->privs &= ~(AX25_CMD|NETROM_CMD);
        }
#else
        m->privs &= ~(AX25_CMD|NETROM_CMD);
#endif
        /*Kill ssid in name, if any*/
        if((cp=strchr(m->name,'-')) != NULLCHAR)
            *cp = '\0';
        /* Check if sysop password is set,
         * if not, disallow sysop privs no matter what !
         */
        if(*Mbpasswd == '\0')
            m->privs &= ~SYSOP_CMD;
        /* Check to see if any of NO_READ,NO_SEND or NO_3PARTY were set,
         * if so, dis-allow those no matter what
         * (so that users cannot get priviledges by issuing a ***linked)
         * 920220 - WG7J
         */
        if(oldprivs & NO_SENDCMD)
            m->privs |= NO_SENDCMD;
        if(oldprivs & NO_READCMD)
            m->privs |= NO_READCMD;
        if(oldprivs & NO_3PARTY)
            m->privs |= NO_3PARTY;
        if(oldprivs & NO_CONVERS)
            m->privs |= NO_CONVERS;
        if(oldprivs & NO_LISTS)
            m->privs |= NO_LISTS;
  
        /* Log this new user in */
        loguser(m);
  
#ifdef USERLOG
        tprintf("Oh, hello %s.\n",m->username ? m->username : m->name);
#else
        tprintf("Oh, hello %s.\n",m->name);
#endif
  
#ifdef MAILCMDS
        changearea(m,m->name);
#endif
        return 0;
    }
  
    if(argc > 1 && (m->sid & MBX_SID))      /* "*** Done" or similar */
        return -2;
    return -1;
}
  
#ifdef FOQ_CMDS
int
dombfinger(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
    char *host, *user = NULLCHAR, buf[8], *newargv[3];
  
    if(argc > 2){
        j2tputs("Usage: F user@host  or  F @host  or  F user.\n");
        return 0;
    }
    host = Hostname;
    if(argc == 2){
        if((host = strchr(argv[1], '@')) != NULLCHAR){
            *host = '\0';
            host++;
        } else
            host = Hostname;
        user = argv[1];
    }
    m = (struct mbx *) p;
    m->startmsg = mallocw(80);
    if(user != NULLCHAR)
        sprintf(m->startmsg,"%s\n",user);
    else
        strcpy(m->startmsg,"\n");
#ifdef MBOX_FINGER_ALLOWED
    newargv[0] = "Q";	/* treat Finger like Query: allow regardless of TELNET_CMD permission */
#else
    newargv[0] = "";
#endif
    newargv[1] = host;
    sprintf(buf,"%d",IPPORT_FINGER);
    newargv[2] = buf;
    return dombtelnet(3,newargv,p);
}
#endif /* FOQ_CMDS */
  
#ifdef  CONVERS
extern int Mbconverse;
extern int CDefaultChannel;
  
int
dombconvers(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m = (struct mbx *)p;
    int channel = 0;
  
    if(m->privs & NO_CONVERS) {
        j2tputs(Noperm);
#ifdef MAILERROR
        mail_error("%s: converse denied\n",m->name);
#endif
        return 0;
    }
    if(!Mbconverse) {
        j2tputs("Mailbox Convers server not enabled\n");
        return 0;
    }
    m->state = MBX_CONVERS;
    if(argc > 1)
        channel = atoi(argv[1]);
    else
        channel = CDefaultChannel;
#ifdef GWTRACE
    logmbox (m->user, m->name, "CONVERS");
#endif
    mbox_converse(m,channel);
    return 0;
}
#endif /* CONVERS */
  
#endif /* MAILBOX */
