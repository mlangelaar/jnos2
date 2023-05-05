/*
 * JNOS 2.0
 *
 * $Id: mboxmail.c,v 1.1 2015/04/22 01:51:45 root Exp root $
 *
 * These are the MAILCMDS
 *
 * Mods by VE4KLM
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
#ifdef MAILCMDS
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
#include "mailutil.h"
#include "ax25mail.h"
#include "nr4mail.h"
#include "cmdparse.h"
#include "mailfor.h"

#ifdef B2F
/* 02Feb2012, Maiko, To resolve j2noB2F() prototype */
#include "b2f.h"
#endif 

#include "domain.h"

#include "activeBID.h"	/* added 23Oct2020 */

#define	CC_HIER

/* By setting the fp to NULL, we can check in exitbbs()
 * wether a tempfile has been closed or not - WG7J
 */
#define MYFCLOSE(x) { fclose(x); x = (FILE *) 0; }
  
char CcLine[] = "Cc: ";
char Mbwarning[] = "Third Party mail is not permitted.\n";
char InvalidNameChars[] = "?*<>[],;:+=\"";
  
int MbSent;
int MbRead;
int MbRecvd;
  
#ifdef MBFWD
int MbForwarded;
extern char *Mbhaddress;
#else
#define Mbhaddress NULL
#endif

#ifdef DEBUG_SID
int debug_sid_user = -1;	/* 31Aug2013, Maiko (VE4KLM) */
#endif

extern int MbrewriteToHdr;	/* 29Dec2013, Maiko, Hdr[TO] rewrite option */

/* 14Apr2016, Maiko, prototype (should use string.h with GNU_SOURCE, but */ 
extern char *strcasestr (const char*, const char*);

/* 14Apr2016, Maiko, prototype, see ufgets.c source file */
extern char* ufgets (int);

#ifdef	J2_SID_CAPTURE

/*
 * 12May2016, Maiko (VE4KLM), a prototype 'SID from where' database, for now
 * just start with the mailbox callsign and the heard SID - add to it later !
 * 13May2016, Maiko (VE4KLM), change SID primary, MBOX secondary - a new LL
 */

typedef	struct xbmdis2j {	/* 13May2016, decided make mbox secondary */

	char *mbox;		/* 06Nov2020, Maiko, Leave this as connecting call sign !!! */

	/*
	 * 06Nov2020, Maiko, I got this all wrong, the mbox field was originally
	 * intended for the connecting callsign, not this new feature I had added
	 * after the fact, giving extra information about the host involved.
	 * 
	 * In using mbox to now hold this extra info, I inadvertantly removed the
	 * ability for the SYSOP to see the connecting call, which frankly is more
	 * important. That was what the first prototype was doing - so we now put
	 * this extra information (if there is any) in this new 'resolution' var.
	 */
	char *resolution;

	time_t when;		/* 13Jun2016, Maiko, add last time seen */

	struct xbmdis2j *next;

} J2SIDMBX;

static J2SIDMBX *mclptr;	/* 13May2016 */

typedef	struct pacdis2j {
	//char *mbox;		/* 13May2016, decided to make mbox secondary */
	J2SIDMBX *mbox;
	char *sid;
	struct pacdis2j *next;
} J2SIDCAP;

static J2SIDCAP *sclptr, *j2sidcaplist = (J2SIDCAP*)0;

static void capture_sid (struct mbx *m, char *argv)
{
	/* 19Nov2019, Maiko, more important to know system name, not user name
	 struct sockaddr fsocket;
 	*/
	char fsocket[MAXSOCKSIZE];	/* 06Nov2020, Maiko, do it this way instead, don't ask */

	char *psock, *tptr;

	int i = MAXSOCKSIZE;	/* 06Nov2020, Someone (myself, Maiko), forgot to initialize !!! */

	/* 13Jun2016, Maiko, TYPO +2 should be outside strlen, not on argv :( */
	char *sidcopy = malloc (strlen (argv) + 2);

	*sidcopy = m->stype; strcpy (sidcopy + 1, argv);

	strupr (sidcopy);	/* 14May2015, Maiko */

	/* 19Nov2019, Maiko, for netrom connects, we want system name, not user */
	if (j2getpeername (m->user, fsocket, &i) != -1)
	{
		psock = psocket (fsocket);	/* keep in mind, psock is just the extra information */

		// log (m->user, "SID DB - psock [%s] user [%s]", psock, m->name);

#ifdef DONT_COMPILE
		// netrom
		if ((tptr = strchr (psock, '@')))
			psock = tptr;	// include the @ to denote the node
		else
#endif
		/*
		 * 06Nov2020, Maiko, We just have to worry about telnet, netrom
		 * and port can stay the way it is, so just use entire peername
		 * for those. Worry about other possible weird stuff later.
		 */
		if ((tptr = strchr (psock, ':')))
		{
			*tptr = 0;		/* just give dotted string representation */

			// try and get a hostname
			if ((tptr = resolve_a (aton (psock), 0)))
			{
				psock = tptr;

#ifdef	DONT_COMPILE	/* 06Nov2020, Maiko, No need to trim, we have room now */

				// see if we can trim down some variations for display
				if ((tptr = strstr (tptr, ".ampr")))
					*tptr = 0;	// just show prefix of ampr.org portion

				else psock = m->name;
#endif
			}
			/* 06Nov2020, Maiko, If hostname resolution fails, then just
			 * leave the original dotted string representation alone !
			 *	
		 	 * else psock = m->name;
			 */
		}
#ifdef	DONT_COMPILE
		// 06Nov2020, Maiko, Check for ports as well
		else if ((tptr = strstr (psock, "port")))
		{
			psock = tptr;
		}
		else psock = m->name;	/* if no extra info, just put in the callsign */	
#endif
	}
	else psock = m->name;	/* if not able to get extra info, stick with the call */

	/* see if we're on the list already, if not, add, if so, leave */
	for (sclptr = j2sidcaplist ; sclptr; sclptr = sclptr->next)
		if (!strcmp (sidcopy, sclptr->sid))
			break;

	if (!sclptr)
	{
		/*
		 * 30Oct2020, Maiko, log calls smack in the middle of a link
		 * list processing can cause problems, since log() can give a
		 * thread the chance to hand the CPU off to someone else, so
		 * when it comes back, pointers may have changed. Pretty sure
		 * this can happen, so the logs are strictly for debugging !

		log (m->user, "SID DB - adding [%s mbox [%s]", sidcopy, m->name);

		 */

		/* put it at the top of the list */
		sclptr = malloc (sizeof(J2SIDCAP));
		sclptr->sid = sidcopy;

		sclptr->mbox = (J2SIDMBX*)0;

		sclptr->next = j2sidcaplist;
		j2sidcaplist = sclptr;
	}
	else free (sidcopy);	/* only free if not adding to the list */

	/* 13May2016, see if MBOX is on the list for this SID, if not then add */

	for (mclptr = sclptr->mbox; mclptr; mclptr = mclptr->next)
		if (!strcmp (m->name /* should never have been psock */ , mclptr->mbox))
			break;

	/* 13May2016, see if MBOX is on the list for this SID, if not then add */

	if (!mclptr)
	{
		/*
		 * 30Oct2020, Maiko, See above comment about using log() inside of link lists

		log (m->user, "SID DB - adding mbox [%s] [%s] to [%s", m->name, psock, sclptr->sid);

		 */

		/* put it at the top of the list */
		mclptr = malloc (sizeof(J2SIDMBX));

		/* 06Nov2020, Maiko, Do this properly now !!! */
		mclptr->mbox = j2strdup (m->name);

#ifdef	MOVED_OUT_UPDATE_EACH_TIME
		/* only fill in resolution if psock is different then mbox name */
		if (strcmp (m->name, psock))
			mclptr->resolution = j2strdup (psock);
		else
			mclptr->resolution = (char*)0;
#endif
		mclptr->next = sclptr->mbox;
		sclptr->mbox = mclptr;
	}

	/*
	 * Only fill in resolution if psock is different then mbox name,
	 * and if you think about it, not a bad idea to keep it updated,
	 * since the connecting station may change the way they connect.
	 */
	if (strcmp (m->name, psock))
		mclptr->resolution = j2strdup (psock);
	else
		mclptr->resolution = (char*)0;

	time (&(mclptr->when));	/* 13Jun2016, Maiko, update each time seen */
}

/* 13May2016, Maiko (VE4KLM), display in a tree what we've captured */
int dosidmbxpast (int argc, char **argv, void *p)
{
	int days;

	time_t	now = time (NULL);	/* 03Jul2016, Maiko, get days since */

	for (sclptr = j2sidcaplist ; sclptr; sclptr = sclptr->next)
	{
		tprintf ("[%s\n", sclptr->sid);

		for (mclptr = sclptr->mbox; mclptr; mclptr = mclptr->next)
		{
			tprintf (" %-6.6s %-9.9s", mclptr->mbox, ctime (&(mclptr->when)) + 11);

			/* 03Jul2016, Maiko, get days since */
			if ((days = (now - mclptr->when) / 86400) > 0)
				tprintf (" %dd", days);

			/* 06Nov2020, Maiko, New field for extra information */
			if (mclptr->resolution)
				tprintf (" %s", mclptr->resolution);

			tprintf ("\n");
		}

		// tprintf ("\n");
	}
	return 0;
}

#endif

int dosid (int argc, char **argv, void *p)
{
    struct mbx *m;
    char *cp;
 
    char *whichargv;
 
    m = (struct mbx *)p;

	/*
	 * debugging
	 *

	log (m->user, "argc [%d] sid [%s] [%s]", argc, argv[1], argv[2]);

	log (m->user, "name [%s] privs [%lx]", m->name, m->privs);

	*
	*/

#ifdef	J2_SID_CAPTURE
	/* 12May2016, Maiko (VE4KLM), let's start a 'SID from where' database */
	capture_sid (m, argv[1]);
#endif

#ifndef	J2_DONT_ENFORCE_BBS_USER	/* 24Dec2020, Maiko, for Red (PE1RRR) */

	/*
	 * 27Oct2020, Maiko (VE4KLM), After seeing some mods from Brian (N1URO), I
	 * think the better approach is that if we see an incoming SID AND the user
	 * hasn't been configured as a BBS in our ftpusers, then we should just do
	 * the disconnect 'now' ...
	 *
	 * There really is no security to prevent a rogue connect from manually
	 * entering a SID when they connect and forward malicious content ! Even
	 * an ignorant user attempting to do a forward because they may not know
	 * any better, is something we should protect against.
	 *
	 * Anyone you forward with 'should' be configured.
	 *
	 * We don't care if we initiate the forward, privs will always be 0 then.
	 *
	 */

	if (m->privs && !(m->privs & IS_BBS))
	{
		tprintf ("*** User NOT configured for BBS operation\n");

    	logmbox (m->user, m->name, "SID from non BBS user, discouraged");

		return 1;
	}

#endif	/* J2_DONT_ENFORCE_BBS_USER	*/

	/* kludge to help handle the space in the author_id part of sid
	 * 13Apr2010, Maiko, trying to forward with RMS Express
	 */
	if (argc == 3)
		whichargv = argv[2];
	else
		whichargv = argv[1];

    if ((argc == 1) || (whichargv[strlen(whichargv)-1] != ']')) /* must be a SID */
        return 1;

    /* Other bbs's */
    m->sid |= MBX_SID;
 
#ifdef	B2F
	/* 25Mar2008, Maiko (VE4KLM), New flag field (for WinLink B2F) */
	m->sid2 = 0;
#endif 

    /* Now check to see if this is an RLI board.
     * As usual, Hank does it a bit differently from
     * the rest of the world.
     */
    if(m->stype == 'R' && strncmp(whichargv,"li",2) == 0)/* [RLI] at a minimum */
        m->sid |= MBX_RLI_SID;
    /* Or maybe it is F6FBB ? */
    else if(m->stype == 'F')
        m->sid |= MBX_FBB;

#ifdef	DEBUG_SID
	log (debug_sid_user, "SID in [%s", whichargv);
#endif

    /* Check to see if the BBS supports a kludge called "hierarchical
     * routing designators."
     *
     * No need to check for ']' -- it must be there or this is not
     * a valid mbox id -- it is checked earlier (fix de OH3LKU)
     *
     * Sid format is [BBSTYPE-VERSION-OPTIONS]
     * check for LAST -, to allow for version portion. - WG7J
     */
    if((cp = strrchr(whichargv,'-')) != NULLCHAR)
	{
        cp++;

        if((strchr(cp,'h') != NULLCHAR) && (strchr(cp,'$') != NULLCHAR))
            m->sid |= MBX_HIER_SID;

        if(strchr(cp,'m') != NULLCHAR)
            m->sid |= MBX_MID;

	/* 27Mar2008, Maiko (VE4KLM), Reorganize this code for B2F (WinLink) */

#if defined (MBFWD) && defined(FBBFWD)
		if (Mfbb)
			if (strchr (cp, 'f') != NULLCHAR)
				m->sid |= MBX_FBBFWD;
#endif

#if defined (MBFWD) && defined(FBBCMP)
		if (Mfbb > 1 && !(m->sid & MBX_FOREGO_FBBCMP))
		{
			if (strchr (cp, 'b') != NULLCHAR)
			{
				m->sid |= MBX_FBBCMP;

				/* 27Aug2013, Maiko (VE4KLM), Flag this as being B1F */
				if (strchr (cp, '1') != NULLCHAR)
					m->sid |= MBX_FBBCMP1;
			}
#ifdef	B2F
		/* 10Jun2016, Maiko (VE4KLM), no more of this 'mbox fbb 3' crap */
			if (strchr (cp, '2') != NULLCHAR)
			{
				/*
				 * 14May2021, Maiko (VE4KLM), Gus says some of the BPQ systems
				 * are now presenting B2F in their SID, but I only wrote this
				 * for Winlink initially, so best to downgrade to B1F if any
				 * system besides Winlink CMS comes at us with a B2F prompt.
				 *  (for now anyways, till I am able to test with BPQ)
				 *
				 * For general forwarding I don't see the benefit of using B2F,
				 * since it's just B1F with an extra checksum value, but entire
				 * message content is now encapsulated in the payload, so in a
				 * way the proposals really contain nothing terribly useful.
				 *
				 * 22May2021, Maiko (VE4KLM), Need to allow RMS clients B2F
				 *  (testing vara access mode with Winlink Express client)
				 */
    				if (m->stype == 'W' && strstr (whichargv, "in") == 0)  /* [WIN] at a minimum */
					m->sid2 |= MBX_B2F;
    				else if (m->stype == 'R' && strstr (whichargv, "ms") == 0)  /* [RMS] at a minimum */
					m->sid2 |= MBX_B2F;
				else
				{
					log (m->user, "downgrading B2F offer (for now), sorry");
					m->sid |= MBX_FBBCMP1;
				}
			}
#endif
		}
#endif

#ifdef	NO_MORE_MBOX_FBB_3_JUST_A_STUPID_IDEA

 /* 10Jun2016, Maiko (VE4KLM), B2F is only for Winklink use, let's face it,
  * having a Mbox fbb 3 makes no sense, so just check for the '2' in the SID
  * regardless of Mfbb level, we only call out to Winlink never the other way.
  */

#if defined (MBFWD) && defined(FBBCMP) && defined(B2F)
	/* 25Mar2008, Maiko (VE4KLM), New WinLink options, new flag added */
		if (Mfbb > 2 && !(m->sid & MBX_FOREGO_FBBCMP))
		{
			if (strchr (cp, '2') != NULLCHAR)
				m->sid2 |= MBX_B2F;
#ifdef	DONT_COMPILE

		/* 29Aug2013, Maiko (VE4KLM), I can confirm JNOS now supports B1F !!! */

		/*
		 * 20Jan2011, Maiko (VE4KLM), It would appear that JNOS has always
		 * ignored the '1', treating B1F as BF - they are NOT the same ! This
		 * is only a problem IF we are running B2F and the following happens :
		 *
		 * A B1F capable station (winfbb) connects to our system, we send the
		 * station a SID with B2F, they reply with a SID of B1F, since that is
		 * the highest they can go. Since B1F is 'ignored' by JNOS, it proceeds
		 * to forward using the BF format. This can crash JNOS, but more likely
		 * drives up hard disk and CPU usage, generating HUGE temporary files,
		 * and leaving users with HUGE messages containing garbled text.
		 *
		 * Until I get this fixed properly, the only thing I can do about	
		 * this is WARN the sysop, and force a disconnection !
		 */
			if (strchr (cp, '1') != NULLCHAR)
			{
				if (!j2noB2F (m->name))
				{
					log (-1, "Station [%s] is B1F capable, which is not supported yet", m->name);
					log (-1, "Add 'mbox nob2f %s' to autoexec.nos for a workaround", m->name);
					close_s (m->user);
				}
			}
#endif	/* DONT_COMPILE */
		}
#endif	/* end of B2F */

#endif	/* end of NO_MORE_MBOX_FBB_3_JUST_A_STUPID_IDEA */

    }

    return 0;
}
  
int
doarea(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
    FILE *fp;
    int cnt;
    char buf[MBXLINE];
  
    m = (struct mbx *) p;
  
    if(argc < 2){
#ifdef USERLOG
        if(m->stype == 'N') {
            listnewmail(m,0);
            return 0;
        }
#endif
        tprintf("Current message area is: %s\n\n",m->area);
        tprintf("Available areas are:\n\n%-10s",m->name);
        if(m->stype == 'F')
            j2tputs("  Your private mail area\n");
        else
            tputc('\n');
        if((fp = fopen(Arealist,READ_TEXT)) == NULLFILE)
            return 0;
        if(m->stype == 'F')
            sendfile(fp,m->user,ASCII_TYPE,0,m);
        else {
            /* send only the area names, not the description.  Multi-columns by WA7TAS */
            cnt=0;
            while(fgets(buf,MBXLINE,fp) != NULLCHAR) {
                if(buf[0] != '#') { /* skip comments */
                    firsttoken(buf);
                    tprintf("%-10s",buf);
                    if(++cnt > 5 ) {
                        cnt=0;
                        tprintf("\n");
                    }
                }
            }
            if(cnt) tprintf("\n");
            j2tputs("\nType AF to get description of areas.\nAF <name> tells more about that area.\n\n");
        }
        fclose(fp);
        return 0;
    } else {
        dotformat(argv[1]);
//#ifdef AREADOCS
        if(m->stype == 'F' && isarea(argv[1])) { /* AF areaname => display areaname.doc */
            dirformat(argv[1]);
            sprintf(buf, "%s/%s.doc", Mailspool, argv[1]);
            if ((fp = fopen(buf, READ_TEXT)) != NULLFILE) {
                sendfile(fp,m->user,ASCII_TYPE,0,m);
                fclose(fp);
            }
            else j2tputs("No additional info found.\n");
        } else
//#endif /* AREADOCS */
        if(!strcmp(m->name,argv[1]) || isarea(argv[1]) ||
           ((m->privs&SYSOP_CMD) && strpbrk(argv[1],InvalidNameChars)==NULLCHAR
            && *(argv[1]+strlen(argv[1])-1)!='.' )){
            changearea(m,argv[1]);
            if(m->areatype == PRIVATE)
                j2tputs("You have ");
            else
                tprintf("%s: ",m->area);
            if(m->nmsgs){
#ifdef USERLOG
                tprintf("%d message%s -  %d new.\n",m->nmsgs,
                m->nmsgs == 1 ? " " : "s ", m->newmsgs);
#else
                if(m->areatype == PRIVATE)
                    tprintf("%d message%s -  %d new.\n", m->nmsgs,
                    m->nmsgs == 1 ? " " : "s ", m->newmsgs);
                else
                    tprintf("%d message%s.\n", m->nmsgs,
                    m->nmsgs == 1 ? "" : "s");
#endif
            } else
                j2tputs("0 messages.\n");
        } else
            tprintf("No such message area: %s\n",argv[1]);
    }
    return 0;
}
  
/* subroutine to do the actual switch from one area to another */
/* USERLOGGING added by WG7J */
void
changearea(m,area)
struct mbx *m;
char *area;
{
    if (m->area[0]) {    /* current area non-null? */
#ifdef USERLOG
        setlastread(m);
#endif
        closenotes(m);
    }
    dotformat(area);
    strcpy(m->area,area);
    /* Check areas first, so regular users loging in with areaname
     * cannot gain permission to kill messages in areas
     * (if univperm in ftpusers doesn't allow that)
     */
    m->areatype = USER;
    if (m->area[0]) {  /* non-null area name */
        if(isarea(area))
            m->areatype = AREA;
        else if(!strcmp(m->name,area))
            m->areatype = PRIVATE;
        /* else was Sysop checking someone else's area; leave areatype==USER */
        
  
#ifdef USERLOG
        /* only read last read message-id if this IS a bbs, or
         * current area is a public area and not 'help'
         * or area starts with 'sys' or we have (sysop or no_lastread) privs.
         */
        if((m->sid & MBX_SID) ||
#ifdef FWDFILE
            m->family == AF_FILE ||
#endif
            (strcmp(area,"help") && m->areatype == AREA) ||
            !strncmp(m->area,"sys",3) ||
            (m->privs & (SYSOP_CMD|NO_LASTREAD)) )
                getlastread(m);
#endif
  
        scanmail(m);
    }
}
  
/* States for send line parser state machine */
#define         LOOK_FOR_USER           2
#define         IN_USER                 3
#define         AFTER_USER              4
#define         LOOK_FOR_HOST           5
#define         IN_HOST                 6
#define         AFTER_HOST              7
#define         LOOK_FOR_FROM           8
#define         IN_FROM                 9
#define         AFTER_FROM              10
#define         LOOK_FOR_MSGID          11
#define         IN_MSGID                12
#define         FINAL_STATE             13
#define         ERROR_STATE             14
  
/* Prepare the addressee.  If the address is bad, return -1, otherwise
 * return 0
 */
int
mbx_to(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char *cp;
    int state, i;
    char *user, *host, *from, *msgid;
    int userlen = 0, hostlen = 0, fromlen = 0, msgidlen = 0;
    struct mbx *m;
  
    m = (struct mbx *)p;
    /* Free anything that might be allocated
     * since the last call to mbx_to() or mbx_reply()
     */
    free(m->to);
    m->to = NULLCHAR;
    free(m->tofrom);
    m->tofrom = NULLCHAR;
    free(m->tomsgid);
    m->tomsgid = NULLCHAR;
    free(m->origto);
    m->origto = NULLCHAR;
    free(m->origbbs);
    m->origbbs = NULLCHAR;
    free(m->subject);
    m->subject = NULLCHAR;
    free(m->date);
    m->date = NULLCHAR;
  
    if(argc == 1)
        return -1;
    i = 1;
    cp = argv[i];
    state = LOOK_FOR_USER;
    while(state < FINAL_STATE){
#ifdef MBDEBUG
        tprintf("State is %d, char is %c\n", state, *cp);
#endif
        switch(state){
            case LOOK_FOR_USER:
                if(*cp == '@' || *cp == '<' || *cp == '$'){
                    state = ERROR_STATE;            /* no user */
                } else {
                    user = cp;                      /* point at start */
                    userlen++;                      /* start counting */
                    state = IN_USER;
                }
                break;
            case IN_USER:
            switch(*cp){
                case '\0':
                    state = AFTER_USER;             /* done with username */
                    break;
                case '@':
                    state = LOOK_FOR_HOST;          /* hostname should follow */
                    break;
                case '<':
                    state = LOOK_FOR_FROM;          /* from name should follow */
                    break;
                case '$':
                    state = LOOK_FOR_MSGID; /* message id should follow */
                    break;
                default:
                    userlen++;                      /* part of username */
            }
                break;
            case AFTER_USER:
            switch(*cp){
                case '@':
                    state = LOOK_FOR_HOST;          /* hostname follows */
                    break;
                case '<':
                    state = LOOK_FOR_FROM;          /* fromname follows */
                    break;
                case '$':
                    state = LOOK_FOR_MSGID; /* message id follows */
                    break;
                default:
                    state = ERROR_STATE;
            }
                break;
            case LOOK_FOR_HOST:
                if(*cp == '@' || *cp == '<' || *cp == '$'){
                    state = ERROR_STATE;
                    break;
                }
                if(*cp == '\0')
                    break;
                host = cp;
                hostlen++;
                state = IN_HOST;
                break;
            case IN_HOST:
            switch(*cp){
                case '\0':
                    state = AFTER_HOST;             /* found user@host */
                    break;
                case '@':
                    state = ERROR_STATE;            /* user@host@? */
                    break;
                case '<':
                    state = LOOK_FOR_FROM;          /* fromname follows */
                    break;
                case '$':
                    state = LOOK_FOR_MSGID; /* message id follows */
                    break;
                default:
                    hostlen++;
            }
                break;
            case AFTER_HOST:
            switch(*cp){
                case '@':
                    state = ERROR_STATE;            /* user@host @ */
                    break;
                case '<':
                    state = LOOK_FOR_FROM;          /* user@host < */
                    break;
                case '$':
                    state = LOOK_FOR_MSGID; /* user@host $ */
                    break;
                default:
                    state = ERROR_STATE;            /* user@host foo */
            }
                break;
            case LOOK_FOR_FROM:
                if(*cp == '@' || *cp == '<' || *cp == '$'){
                    state = ERROR_STATE;
                    break;
                }
                if(*cp == '\0')
                    break;
                from = cp;
                fromlen++;
                state = IN_FROM;
                break;
            case IN_FROM:
            switch(*cp){
                case '\0':
                    state = AFTER_FROM;             /* user@host <foo */
                    break;
                case '<':
                    state = ERROR_STATE;            /* user@host <foo< */
                    break;
                case '$':
                    state = LOOK_FOR_MSGID; /* message id follows */
                    break;
                default:
                    fromlen++;
            }
                break;
            case AFTER_FROM:
            switch(*cp){
                case '@':                               /* user@host <foo @ */
                case '<':                               /* user@host <foo < */
                    state = ERROR_STATE;
                    break;
                case '$':
                    state = LOOK_FOR_MSGID; /* user@host <foo $ */
                    break;
                default:
                    state = ERROR_STATE;            /* user@host foo */
            }
                break;
            case LOOK_FOR_MSGID:
                if(*cp == '\0')
                    break;
                msgid = cp;
                msgidlen++;
                state = IN_MSGID;
                break;
            case IN_MSGID:
                if(*cp == '\0')
                    state = FINAL_STATE;
                else
                    msgidlen++;
                break;
            default:
            /* what are we doing in this state? */
                state = ERROR_STATE;
        }
        if(*(cp) == '\0'){
            ++i;
            if(i < argc)
                cp = argv[i];
            else break;
        } else
            ++cp;
    }
    if(state == ERROR_STATE || state == LOOK_FOR_HOST
        || state == LOOK_FOR_FROM || state == LOOK_FOR_MSGID)
        return -1;              /* syntax error */
  
    m->to = mallocw((unsigned)userlen + hostlen + 2);
  
    strncpy(m->to, user, (unsigned)userlen);
    m->to[userlen] = '\0';
  
    if(hostlen){
        m->to[userlen] = '@';
        strncpy(m->to + userlen + 1, host, (unsigned)hostlen);
        m->to[userlen + hostlen + 1] = '\0';
    }
    if(fromlen){
        m->tofrom = mallocw((unsigned)fromlen + 1);
        strncpy(m->tofrom, from, (unsigned)fromlen);
        m->tofrom[fromlen] = '\0';
    }
    if(msgidlen){
        m->tomsgid = mallocw((unsigned)msgidlen + 1);
        strncpy(m->tomsgid, msgid, (unsigned)msgidlen);
        m->tomsgid[msgidlen] = '\0';
#ifdef FBBFWD
        if(strncmp(m->tomsgid, "fbbbid",6) == 0) {
            m->origto = mallocw((unsigned)msgidlen + 1 - 7);
            strcpy(m->origto, &msgid[7]);
            cp = strstr(m->origto,"->");
            *cp = '\0';
        }
#endif
    }
    return 0;
}

#ifdef WPAGES 
extern void wpageAdd (char *entry, int bbs, int updateit, char which);
#endif

#ifdef MAILCMDS
#ifdef DEBUG_MBX_HDRS
/* 11Dec2013, Function to dump MBX information to help me debug !!! */
void mbx_dump (char *where, struct mbx *m)
{
	char *nstr = "null";

	log (m->user, "mbx_dump [%s]", where);

	log (-1, "      to [%s]", m->to ? m->to : nstr);
	log (-1, "  origto [%s]", m->origto ? m->origto : nstr);
	log (-1, "  tofrom [%s]", m->tofrom ? m->tofrom : nstr);
	log (-1, " origbbs [%s]", m->origbbs ? m->origbbs : nstr);
}
#endif
#endif

/* This opens the data file and writes the mail header into it.
 * Returns 0 if OK, and -1 if not.
 */
int
mbx_data(m,cclist,extra)
struct mbx *m;
struct list *cclist;    /* list of carbon copy recipients */
char *extra;        /* optional extra header lines */
{
    time_t t;
    struct list *ap;
    int cccnt = 0;

	char buf[100];	/* 02Feb2012, Maiko, For WPAGES code from TNOS 240 */

	int wl2k_smtp = 0;

	char *tfile_to_hdr, *newmto; /* 29Dec2013, dealing with Hdrs[TO] field */

	char *new1cc;	/* 25Jul2014, rewriting of CCLIST local addresses if necessary */

#ifdef DEBUG_MBX_HDRS
	mbx_dump ("mbx_data", m);
#endif
    /* If m->date is set, use this one (comes from bbs-forwarded mail) */
    time(&t);
    if(m->date != NULLCHAR) {
        fprintf(m->tfile,"%s%s",Hdrs[DATE],m->date);
#ifdef RLINE
        if(Rholdold && m->stype=='B' &&
          (t - mydate(m->date)) > 86400L*Rholdold)  /* too old? */
            fprintf(m->tfile, "%sAge\n", Hdrs[XBBSHOLD]);
#endif
    }
    else {
        fprintf(m->tfile,"%s%s",Hdrs[DATE],ptime(&t));
    }

 	/* 16Jul2013, Maiko, Support for 3rd party emails via Winlink */
	if (m->origto)
	{
		// log (-1, "TO [%s]", m->origto);

		if (strcasestr (m->origto, "smtp:"))
			wl2k_smtp = 1;
	}
  
    /* Bulletin ID, if any */
    fprintf(m->tfile, "%s", Hdrs[MSGID]); /* 05Jul2016, Maiko, compiler */

/*
 * 17Jul2013, Actually, lets set MSGID to 'N@winlink.org', that way the
 * code in forward.c generates a more useful MID in the FC proposal when
 * the B2F forwarding comes into play. GOOD - just tested this, I did get
 * the email to my 'maiko@pcs.mb.ca' account, and this time the Message-ID
 * in the email header includes the full JNOS call (it's not cutoff, due to
 * the '_' character one sees if MSGID contains the JNOS hostname instead).
 */
    if (wl2k_smtp)
        fprintf(m->tfile,"<%ld@%s>\n",get_msgid(), "winlink.org");
    else if(m->tomsgid)
        fprintf(m->tfile,"<%s@%s.bbs>\n", m->tomsgid, m->name);
    else
        fprintf(m->tfile,"<%ld@%s>\n",get_msgid(), Hostname);

    /* From : , could use 'real bbs address', if origbbs is set */
    fprintf(m->tfile, "%s", Hdrs[FROM]); /* 05Jul2016, Maiko, compiler */

 	/* 27Jul2013, Use the BBS user name */
	if (wl2k_smtp)
		sprintf (buf, "%s@winlink.org\n", m->name);
    else
	/* 02Feb2012, Maiko, In order to incorporate the WPAGES code from
	 * the Lantz TNOS 240 project, I need to use sprintf (buf) instead
	 * of fprintf (m->tfile), then just do one fprintf later on ...
	 */
    if(m->tofrom) {  /* BBS style '< call' */
        if(m->origbbs != NULLCHAR)
            sprintf(buf,"%s@%s\n",m->tofrom,m->origbbs);
        else
            sprintf(buf,"%s%%%s.bbs@%s\n",m->tofrom, m->name, Hostname);
    } else {
        if(m->origbbs != NULLCHAR)
            sprintf(buf,"%s@%s\n",m->name,m->origbbs);
        else {
            int found = 0;
            char *cp,*cp2;
            FILE *fp;
            char line[128];
  
            if((fp = fopen(Pdbase,READ_TEXT)) != NULLFILE) {
                while(fgets(line,128,fp) != NULLCHAR) {
                    rip(line);
                    cp = skipwhite(line);
                    if(*cp == '#')
                        continue;
                    /* Now find end of fist entry */
                    cp2 = cp;
                    while(*cp2 && *cp2 != ' ' && *cp2 != '\t')
                        cp2++;
                    if(*cp2 == '\0') /* No additional data */
                        continue;
                    *cp2 = '\0';    /* terminate first entry */
                    if(!stricmp(cp,m->name)) {
                        /* Found one, now get the name */
                        cp = skipwhite(++cp2);
                        if(*cp) {
                            found = 1;
                            sprintf(buf,"%s <%s@%s>\n",cp,m->name,Hostname);
                        }
                    }
                }
                fclose(fp);
            }
            if(!found)
                sprintf(buf,"%s@%s\n",m->name,Hostname);
        }
    }

	fputs (buf, m->tfile);	/* 02Feb2012, Maiko, Done cause of WPAGES code */

#ifdef WPAGES
	/* 02Feb2012, Maiko, From Lantz TNOS 240 source */
	if (m->origbbs != NULLCHAR)
	{
		char *cptr = strdup (buf);
		rip (cptr);
		// log (-1, "wpageAdd [%s]", cptr);
		wpageAdd (cptr, 0, 1, 'G');	/* update users file */
		free (cptr);
	}
#endif

#if defined USERLOG && defined REGISTER
    if((m->sid & MBX_REPLYADDR) && m->IPemail)
        fprintf(m->tfile,"%s%s\n",Hdrs[REPLYTO],m->IPemail);
#endif /* REPLY-TO HEADER */

	/*
	 * 29Dec2013, just a note, since me maintaining JNOS and before 2004, JNOS
	 * has always set the TO field to the original (pre-rewrite) value, a new
	 * option has been added so people can have a post-rewrite value instead,
	 * sort of requested by N6MEF (Fox) when he 'discovered' a CCLIST issue,
	 * so this requires a big of reorganization and some new code.
	 */

	if (MbrewriteToHdr)
		tfile_to_hdr = m->to;
	else if (m->origto != NULLCHAR)
		tfile_to_hdr = m->origto;
	else
		tfile_to_hdr = m->to;

	/*
	 * 27Dec2013, Maiko, If the 'TO' recipient is local, but ANY of the CC
	 * recipients are not local (ie, contain '@'), then we should append
	 * our hostname to the TO recipient, or else any of the CC recipients
	 * will see 'local@<hostname of that recipient>' in their headers,
	 * which is simply not a good thing !
	 *
	 * 29Dec2013, Maiko, originally I was only doing this if origto was
	 * not set (ie, no rewrite of TO field), but that's a separate issue,
	 * so we should do this cclist thing for all cases I think, that is
	 * why you see the new 'tfile_to_hdr' variable so I can use common
	 * code for the multiple sources of the TO (m->to or m->origto).
	 *
	 * I have tested all 4 scenarios of this code using the 'SC' command
	 * at the BBS user prompt, and it works just fine. Forwarding still
	 * looks like it works properly, it definitely should, since CCLIST
	 * are not used in any forwarding between systems (except FC stuff).
	 *
	 * 25Jul2014, Maiko, Silly me, if exthosts exist, I need to check 
	 * both To: AND the CCLIST for any LOCAL addresses, not just the
	 * the To: field, so loop the cclist twice, first time to determine
	 * if any external hosts are present, then second time to locate any
	 * localhosts in the CCLIST and process them like I do To: field !
	 */

    if (cclist != NULLLIST)
	{
		int	exthosts = 0;

		/* any external hosts in the 'Cc:' list ? */
		for (ap = cclist; ap != NULLLIST; ap = ap->next)
		{
#ifdef DEBUG_CCLIST
			log (-1, "CCLIST [%s]", ap->val);
#endif
			if (strchr (ap->val, '@') != NULLCHAR)
			{
#ifdef CC_HIER
				if (strchr (ap->val, '#') == NULLCHAR)
#endif
				exthosts++;
				/* break; 10Aug2014, We can break, we just need
					to see if there are external hosts, but by
					not breaking, the external count is accurate :)
				*/
			}
		}

		/* any external host in the 'To:' field ? */
		if (strchr (tfile_to_hdr, '@') != NULLCHAR)
#ifdef CC_HIER
			if (strchr (tfile_to_hdr, '#') == NULLCHAR)
#endif
			exthosts++;

		if (exthosts)
		{
#ifdef DEBUG_CCLIST
			log (-1, "CCLIST %d externals, to [%s]", exthosts, tfile_to_hdr);
#endif
			/* check if the To: field is a local address or not */
			if (strchr (tfile_to_hdr, '@') == NULLCHAR)
			{
				/*
				 * 15Jul2013, Maiko, this can cause a crash in most unexpected
				 * parts of the code, because I forgot to allocate space for a
				 * string NULL termination (was +1, should be +2), damn it !
				 */
				newmto = malloc (strlen (tfile_to_hdr) + strlen (Hostname) + 2);

				sprintf (newmto, "%s@%s", tfile_to_hdr, Hostname);

#ifdef DEBUG_CCLIST
				log (-1, "CCLIST orig to [%s] new to [%s]",
					tfile_to_hdr, newmto);
#endif
				if (MbrewriteToHdr)
				{
					free (m->to);
					tfile_to_hdr = m->to = newmto;
				}
				else if (m->origto != NULLCHAR)
				{
					free (m->origto);
					tfile_to_hdr = m->origto = newmto;
				}
				else
				{
					free (m->to);
					tfile_to_hdr = m->to = newmto;
				}
			}

			/* check if any of the cclist is a local addresses or not */
			for (ap = cclist; ap != NULLLIST; ap = ap->next)
			{
#ifdef DEBUG_CCLIST
				log (-1, "CCLIST cc [%s]", ap->val);
#endif
				if (strchr (ap->val, '@') == NULLCHAR)
				{
					new1cc = malloc (strlen (ap->val) + strlen (Hostname) + 2);

					sprintf (new1cc, "%s@%s", ap->val, Hostname);
#ifdef DEBUG_CCLIST
					log (-1, "CCLIST orig cc [%s] new cc [%s]",
						ap->val, new1cc);
#endif
					free (ap->val);
					ap->val = new1cc;
				}
			}
		}
	}

#ifdef DEBUG_MBX_HDRS
	log (-1, "tfile Hdrs[TO] = [%s]", tfile_to_hdr);
#endif

	/*
	 * 29Dec2013, code reorganized and modified to accomodate a couple of
	 * new features (making sure local recipients have hostname appended if
	 * anyone in the CCLIST is not local, and allow rewrite of TO hdr in the
	 * tfile created within this very same function.
	 */
   	fprintf (m->tfile, "%s%s\n", Hdrs[TO], tfile_to_hdr);

    /* Write Cc: line */
    for(ap = cclist; ap != NULLLIST; ap = ap->next) {
        if(cccnt == 0){
            fprintf(m->tfile,"%s",Hdrs[CC]);
            cccnt = 4;
        }
        else {
            fprintf(m->tfile,", ");
            cccnt += 2;
        }
        if(cccnt + strlen(ap->val) > 80 - 3) {
            fprintf(m->tfile,"\n    ");
            cccnt = 4;
        }
#ifdef DEBUG_MBX_HDRS
	log (-1, "Writing tfile Hdrs[CC] %s", ap->val);
#endif
        fputs(ap->val,m->tfile);
        cccnt += strlen(ap->val);
    }
    if(cccnt)
        fputc('\n',m->tfile);
    fprintf(m->tfile,"%s%s\n",Hdrs[SUBJECT],m->subject);
    if(!isspace(m->stype) && ((m->stype != 'R' && m->stype != 'F') ||
        (m->sid & MBX_SID) !=0))
        fprintf(m->tfile,"%s%c\n", Hdrs[BBSTYPE],m->stype);
	/*
	 * 26Jul2005, Maiko, New header field for user-port tracing,
	 * so that the recipient knows what user id and on what port
	 * that user was connected on when this message was composed.
	 */
#ifdef MAIL_HDR_TRACE_USER_PORT
	{
		extern void singlembuser (struct mbx*, struct mbx*, FILE*);

		fprintf (m->tfile, "X-JNOS-User-Port: ");

		singlembuser (m, m, m->tfile);
	}
#endif

	/* 29Dec2013, If rewrite TO is enabled, and original exists, note it !!! */
	if (MbrewriteToHdr && (m->origto != NULLCHAR))
		fprintf (m->tfile, "X-Original-To: %s\n", m->origto);

    if(extra != NULLCHAR)
        fprintf(m->tfile, "%s", extra);	/* 05Jul2016, Maiko, compiler */

    return 0;
}
  
/*
 * Returns true if string is in history file or if string appears to be a
 * message id generated by our system.
 *
 * 30Aug2020, Maiko (VE4KLM), Good grief !!! Why does SMTP even need to
 * look at the damn BID which is a forwarding entity, nothing to do with
 * any SMTP in general. Anyways need to make sure BASE36 is accomodated.
 *
 * 31Aug2020, Maiko (VE4KLM), Reorganized code to add verbose warnings to
 * help me better understand how all of this stuff actually works ? added
 * the socket parameter so that I can call the nos_log function properly.
 */
int msgidcheck (int s, char *string)
{
    FILE *fp;

    char *cp, buf[LINELEN];
 
    if(string == NULLCHAR)
        return 0;

	/* 31Aug2020, Maiko (VE4KLM), reuse 'buf' to building warning messages */
	sprintf (buf, "[%s] ", string);
  
    /* BID's that we have generated ourselves are not kept in the history
     * file. Such BID's are in the nnnnn_hhhhhh form, where hhhhhh is a part of
     * our hostname or mbHaddress, truncated at the first period so that the
     * BID is no longer than 12 characters.  [see makecl() in forward.c]
     */

	if ((cp = strchr (string,'_')) != NULLCHAR)
	{
 		if (*(cp+1) != '\0')
		{
        		if (strnicmp (cp+1, (Mbhaddress?Mbhaddress:Hostname), strlen(cp+1)) == 0)
			{
        			if(*((Mbhaddress?Mbhaddress:Hostname)+strlen(cp+1)) == '.')
				{
					if (
						/* 03Dec2020, removed HARGROVE_VE4KLM_FOX_USE_BASE36 from makefile, permanent ! */
        					strspn(string,"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ") == (size_t)(cp-string)
					)
						return 1;
#ifdef	BID_DEBUGGING
					else strcat (buf, "not base36");
#endif
				}
#ifdef	BID_DEBUGGING
				else strcat (buf, "no host");
#endif
			}
#ifdef	BID_DEBUGGING
			else strcat (buf, "not from us");
#endif
		}
#ifdef	BID_DEBUGGING
		else strcat (buf, "blank");
#endif
	}
#ifdef	BID_DEBUGGING
	else strcat (buf, "not nnn_host format");

	log (s, "%s - checking bid history", buf);	/* log the reason why we are checking history file */
#endif

    if((fp = fopen(Historyfile,READ_TEXT)) == NULLFILE)
        return 0;

    setvbuf(fp, NULLCHAR, _IOFBF, 2048);       /* N5KNX: use bigger input buffer if possible */

    while(fgets(buf,LINELEN,fp) != NULLCHAR) {
        firsttoken(buf);
        if(stricmp(string,buf) == 0) {    /* found */
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);

    return 0;
}
  
#ifdef MBFWD

static char *hferrstr = "could not %s history file - BID not saved !!!!";

int
storebid(bid)
char *bid;
{
    long now;
    FILE *fp;
    int i;

    /* Save it in the history file - WG7J */
    /* n5knx: lock it first.  Competition from makecl() and Oldbidtick() ... */
    for (i=30; i>0; i--) {  /* try to lock bid History file */
        if(!mlock(Historyfile,NULLCHAR)) break;
        j2pause(2000L);
    }
    if (i)
	{
        if ((fp = fopen(Historyfile,APPEND_TEXT)) != NULLFILE)
		{
            /* Timestamp added to allow automatic expiry of bid file - WG7J */
            time(&now);
            fprintf(fp,"%s %ld\n",bid,now); /* Save BID */
            fclose(fp);

		/*
		 * 22Oct2020, Maiko (VE4KLM), BID is safely stored, so we can now
		 * remove it from the global pool of forwarding now being tracked
		 * by my new chk4activebid() functionality in fbbfwd.c
		 *
		 * I expect this to cut down on DUPES for those systems that refuse
		 * to stagger their forwards, which is really the only way for older
		 * versions of JNOS to avoid acceptance of identical messages coming
		 * in from various systems at the same time or within seconds of.
		 */
			delactivebid (-1, bid);
        }
        else
		{
			 i=0;  /* open error */

			/* 24Oct2020, Maiko (VE4KLM), this needs to be logged !!! */
			log (-1, "%s%s", hferrstr, "open");
		}

        (void)rmlock(Historyfile,NULLCHAR);
    }
	else	/* 24Oct2020, Maiko (VE4KLM), this needs to be logged !!! */
	{
		log (-1, "%s%s", hferrstr, "lock");
	}

    return i;  /* 0 => unable to store bid */
}
#endif /* MBFWD */

/* Attempt to determine if this is third-pary mail. */
int
thirdparty(m)
struct mbx *m;
{
    char buf[MBXLINE], *cp, *rp;
    FILE *fp;
  
    if(strpbrk(m->to,"@%!") != NULLCHAR)
        return 0;
  
    rp = j2strdup(Hostname);
  
    if((cp = strchr(rp, '.')) != NULLCHAR)
        *cp = '\0';
  
    if(stricmp(m->to,rp) == 0){
        free(rp);
        return -1;
    }
    free(rp);
  
    if(stricmp(m->to,"sysop") == 0)
        return -1;
  
    if((fp = fopen(Arealist,READ_TEXT)) == NULLFILE)
        return 0;
  
    while(fgets(buf,MBXLINE,fp) != NULLCHAR){
        /* The first word on each line is all that matters */
        firsttoken(buf);
        if(stricmp(m->to,buf) == 0){
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 0;
}
  
/* Move messages from current area to another */
int
dombmovemail(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    long pos;
    int num,i;
    int move[NARG];
    char *to;
    struct mbx *m;
    FILE * Mfile;           /* file to move to */
    int not_area;           /* is to-file an area or a regular file? */
    struct let *cmsg;
    int thisone=0;
    int start;
    int end;
    char *area;
    struct mailindex ind;
    char buf[MBXLINE];
  
    m = (struct mbx *)p;
  
    if(argc == 1) {
        j2tputs("Syntax: MM area - moves current message\n"
        "        MM n1 [n2...] [n3-n4] area - move message n1 (n2...) (n3 to n4)\n");
        return 0;
    }
    if(argc == 2) {
        /* NO message #, use current message */
        num = 1;
        to = argv[1];
        move[0] = m->current;
    } else {
        /* n5knx: to specify a range, put start and (-end) into the move[] array */
        num=0;
        for(i=1; i<argc-1 && num<NARG; i++) {  /* scan all but last arg (the destination) */
            start = atoi(argv[i]);
            if(start < 1 || start > m->nmsgs) {
                tprintf(Badmsg,start);
                continue;
            }
            move[num++] = start;
            to = strchr(argv[i],'-'); /* N5KNX: allow from-to msg specification */

            if (to != NULLCHAR && num<NARG) {
                end = atoi(++to);
                if (end < start || end > m->nmsgs) {
                    tprintf(Badmsg,end);
                    num--;
                    continue;
                }
                move[num++] = -end;
            }
        }
        to = argv[argc-1];
    }

#ifdef UNIX  
    if (*to == '/') {  /* not area if begins with slash */
#else
    if (*to == '/' || *to == '\\' || *(to+1) == ':') {  /* not area if begins with slash or drive spec */
#endif
        not_area=1;
        strncpy(buf, to, sizeof(buf));  buf[sizeof(buf)-1] = '\0';
    }
    else {
        int c;

        not_area=0;
        dotformat(to);
        if (!isarea(to)) {
            tprintf("%s is not a public area... Are you sure? ",to);
            if(charmode_ok(m))
                c = tkeywait("Move(N=no)?",0,
                        (int)(OPT_IS_DEFINED(m,TN_LINEMODE)&OPT_IS_SET(m,TN_LINEMODE)));
            else  /* For AX.25 and NET/ROM connects */
                c = mykeywait("Move(N=No)?",m);
            if(c == -1 || c == 'n' || c == 'N') {
                j2tputs("Aborted.\n");
                return 0;
            }
        } /* endif */
        dirformat(to);

        /* Now try to lock the destination file */
        if(mlock(Mailspool,to) == -1) {
            tprintf("Can't lock '%s', please try later\n",to);
            return 0;
        }
        sprintf(buf,"%s/%s.txt",Mailspool,to);
    }
    /* open the destination file for appending, and set file pos for ftell() */
    if( ((Mfile=fopen(buf,"a+")) == NULLFILE) || fseek(Mfile, 0L, SEEK_END)) {
        tprintf("Can't open/seek '%s'\n",buf);
    	if (!not_area)
		rmlock(Mailspool,to);
    	return 0;
    }
  
    /* Open the mailbox file for reading */
    area = j2strdup(m->area);
    dirformat(area);
    sprintf(buf,"%s/%s.txt",Mailspool,area);
    if((m->mfile=fopen(buf,"rt")) == NULLFILE) {
        tprintf("Can't open '%s'\n",buf);
    	fclose(Mfile);
    	free(area);
		if (!not_area)
			rmlock(Mailspool,to);
	    return 0;
    }
    memset(&ind,0,sizeof(struct mailindex));
  
    /* Okay, let's do the work */
    for(i=0;i<num;i++) {
        if ((start = move[i]) < 0) {  /* range in progress */
            thisone++;
            if(thisone > -start)
                continue;  /* end of range */
            else
                i--;  /* don't advance the index into move[] array */
        }
        else
            thisone = start;

        cmsg = &m->mbox[thisone];
  
        /* find start of this message */
        fseek(m->mfile,cmsg->start,0);
  
        /* Get the index for this message */
        get_index(thisone,area,&ind);
  
        /* now read this message */
        fgets(buf,MBXLINE,m->mfile);    /* The 'From ' line */
        pos = ftell(Mfile);
        fputs(buf,Mfile);
        while(fgets(buf,MBXLINE,m->mfile)!= NULL) {
            if(!strncmp(buf,"From ",5))
                break;
            /*if(!strnicmp(buf,Hdrs[STATUS],strlen(Hdrs[STATUS]))) continue; * Don't copy a STATUS: R hdr */
            fputs(buf,Mfile);
        }
        if (!not_area) {
            /* Update the index with the new message */
            ind.size = ftell(Mfile) - pos;
            ind.status &= ~BM_READ;  /* mark as not read yet */
            if (write_index(to,&ind) == -1)
                log(m->user, "index update failed for %s", to);
        }
  
        if((m->stype == 'C') || (m->stype == 'c')) {
            tprintf("Message %d copied...\n",thisone);
        } else {
            /* delete this message */
            cmsg->status |= BM_DELETE;
            m->change = 1;
            tprintf("Message %d moved...\n",thisone);
        }
    }
    fclose(m->mfile);
    m->mfile = NULL;

    fclose(Mfile);
    free(area);
  
    if (!not_area)
	rmlock(Mailspool,to);

    return 0;
}

#ifdef WPAGES
extern int wpage_lookup (char **src, char *name, struct mbx *m);
#endif
  
/*Some additional security - WG7J
 *NO_3PARTY =  disallow all 3rd party mail
 *NO_SENDCMD = only allow mail to sysop
 */
int
dosend(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int cccnt = 0, fail = 0;
    char *host, *cp, fullfrom[MBXLINE], sigwork[LINELEN], *rhdr = NULLCHAR;
    struct list *ap, *cclist = NULLLIST;
    struct mbx *m;
    FILE *fp;
    int done = 0;
    char *cp2;
    int c, do_eol_ctlz = 0;
#if defined(EDITOR) && defined(ED) && defined(SEND_EDIT_OK)
    char tmpfname[L_tmpnam+20];
#define S_QUERY "Send(N=no,E=edit)?"
#else
#define S_QUERY "Send(N=no)?"
#endif
  
#ifdef RLINE
    struct tm t;
#define ODLEN   16
#define OBLEN   32
    char tmpline[MBXLINE];
    char fwdbbs[NUMFWDBBS][FWDBBSLEN+1];
    int myfwds = 0;
    int i;
    int zulu=0;
    int check_r = 0;
    int found_r = 0;
    char origdate[ODLEN+1];
    char origbbs[OBLEN+1];
    int loops = 0;
    char Me[15];
  
    origdate[0] = '\0';
    origbbs[0] = '\0';
#endif
  
    m = (struct mbx *)p;
  
    if((m->stype != 'R' || (m->sid & MBX_SID)) && mbx_to(argc,argv,m)
    == -1){
        j2tputs((m->sid & MBX_SID) ? "NO - syntax error\n" : \
        "S command syntax error - format is:\n" \
        "S[C|F] name [@ host] [< from_addr] [$bulletin_id]\n" \
        "SR [number]\n");
#ifdef MAILERROR
        mail_error("%s: MBOX S syntax error - %s\n",m->name,cmd_line(argc,argv,m->stype));
#endif
        return 0;
    }
  
    /* N5KNX: Only seven chars are legal after the S ... reject any others */
    if(strchr(" BPTCFR", m->stype) == NULLCHAR) {
        j2tputs((m->sid & MBX_SID) ? "NO - syntax error\n" : \
        "S command syntax error.\n");
#ifdef MAILERROR
        mail_error("%s: MBOX S syntax error - %s\n",m->name,cmd_line(argc,argv,m->stype));
#endif
        return 0;
    }

    /*Check for send permission */
    if(m->privs & NO_SENDCMD) { /*is this to 'SYSOP' or 'sysop' ?*/
        if(m->stype == 'R' || stricmp(m->to,"sysop")) {
            j2tputs((m->sid & MBX_SID) ? "NO - permission denied\n" : \
            "Sorry, only mail to 'sysop' allowed!\n");
#ifdef MAILERROR
            mail_error("%s: no mail permission - %s\n",m->name,(m->to?m->to:"SR"));
#endif
            return 0;
        }
    }
  
#ifdef FBBFWD
    if(m->tomsgid && strncmp(m->tomsgid, "fbbbid",6) == 0) {
       // This has already been processed by the fbb forwarding code.
       // Swap m->origto and m->to.
       cp        = m->origto;
       m->origto = m->to;
       m->to     = cp;

       // Strip off anything up to and including a "->" on the BID.
       cp        = strstr(m->tomsgid,"->");
       cp++;
       cp++;
       strcpy(m->tomsgid,cp);
#ifdef FBBDEBUG2
       log(m->user,"Sending %s", m->tomsgid);
#endif
    }
    else {
#endif

	/* 02Apr2016, Maiko (VE4KLM), try to log the non-fbb forwards better */
    if (m->sid & MBX_SID)
		logmbox (m->user, m->name, "incoming proposal %s", cmd_line(argc,argv,m->stype));

    /* Check for a BID on bulletins from other bbs's - WG7J */
#ifdef	DONT_COMPILE
    if((m->sid & MBX_SID) && !NoBid && (m->stype == 'B') && (m->tomsgid == NULLCHAR))
#else
	/*
	 * 16Oct2019, Maiko, Putting in a fix suggested by Brian (N1URO), this should
	 * prevent ANY type of message going out without a BID - done @ 3 pm. Suppose
	 * we could go even one step further, and simply get rid of the option which
	 * allows a sysop to send bulletins without a BID, but leave it alone 4 now.
	 */
    if ((m->sid & MBX_SID) && !NoBid && (m->tomsgid == NULLCHAR))
#endif
	{
        j2tputs("NO - No BID!\n");
        log(m->user,"MBOX %s: SB without BID - %s",m->name,m->to);
#ifdef MAILERROR
        mail_error("MBOX %s: SB without BID - %s",m->name,m->to);
#endif
        return 0;
    }

	/*
	 * 06May2022, Maiko (VE4KLM), lower case BID kludge for Charles
	 *
		if (strcasestr (m->name, "n3hym"))
	 *
	 * 14May2022, Maiko (VE4KLM), Just do it for all calls sending 'sB'
	 *  (apparently both bpq and xrouter are doing this)
	 *
	 * 07Jun2022, Maiko (VE4KLM), Oops, SB will crash this, need to
	 *  make sure 'tomsgid' is not NULL, and has an actualy string.
	 */
	if ((m->stype == 'B') && (m->tomsgid != NULLCHAR))
	{
		log (m->user, "lower case kludge");
		strupr (m->tomsgid);
	}
  
    if(m->stype != 'R' && msgidcheck(m->user, m->tomsgid)) {
        if(m->sid & MBX_SID)
            j2tputs("NO - ");
        tprintf("Already have %s\n",m->tomsgid);

		/* 02Apr2016, Maiko (VE4KLM), try to log the non-fbb forwards better */
    	if (m->sid & MBX_SID)
			logmbox (m->user, m->name, "already have %s", m->tomsgid);

        return 0;
    }
    if(m->stype == 'R' && !(m->sid & MBX_SID) &&
    mbx_reply(argc,argv,m,&cclist,&rhdr) == -1) {
        j2tputs("Can not reply\n");
        return 0;
    }

#ifdef WPAGES
	/* 12Mar2012, Maiko, if a solitary callsign, let's try white pages */
	if (strchr (m->to, '@') == NULLCHAR)
		wpage_lookup (&m->to, m->name, m);
#endif

    if((cp = rewrite_address(m->to,REWRITE_TO)) != NULLCHAR)
	{
        if(strcmp(m->to,cp) != 0){
            free(m->origto);     /* n5knx: in case non-null */
            m->origto = m->to;
            m->to = cp;
        }
        else
            free(cp);
	}
  
    /* refuse any mail that gets rewritten into 'refuse' - WG7J */
    if(!strcmp(m->to,"refuse")) {
        j2tputs((m->sid & MBX_SID) ? "NO - refused\n" : \
        "Bad user or hostname,  please mail 'sysop' for help\n");

		/* 02Apr2016, Maiko (VE4KLM), try to log the non-fbb forwards better */
    	if (m->sid & MBX_SID)
			logmbox (m->user, m->name, "refused %s", m->tomsgid);

        return 0;
    }
  
    if( (!ThirdParty && !(m->privs & SYSOP_CMD)) || (m->privs & NO_3PARTY) )
        if(thirdparty(m) == 0){
            j2tputs(Mbwarning);
#ifdef MAILERROR
            mail_error("%s: 3rd party mail refused - %s\n",m->name,m->to);
#endif
            return 0;
        }
  
    /* Send the new 'To:' line to sysops only - WG7J */
    if((m->privs&SYSOP_CMD) && (m->origto != NULLCHAR || m->stype == 'R') \
        && !(m->sid & MBX_SID))
        tprintf("To: %s\n", m->to);
    if(validate_address(m->to) == BADADDR){
        j2tputs((m->sid & MBX_SID) ? "NO - bad address\n" : \
        "Bad user or hostname,  please mail 'sysop' for help\n");
        free(rhdr);
        del_list(cclist);
        /* We don't free any more buffers here. They are freed upon
         * the next call to mbx_to() or to domboxbye()
         */
        return 0;
    }
#ifdef FBBFWD
    }
#endif
    /* Display the Cc: line (during SR command) */
    for(ap = cclist; ap != NULLLIST; ap = ap->next) {
        if(cccnt == 0){
            tprintf("%s",Hdrs[CC]);
            cccnt = 4;
        }
        else {
            j2tputs(", ");
            cccnt += 2;
        }
        if(cccnt + strlen(ap->val) > 80 - 3) {
            j2tputs("\n    ");
            cccnt = 4;
        }
        j2tputs(ap->val);
        cccnt += strlen(ap->val);
    }
    if(cccnt)
        tputc('\n');
  
    /* If the the command was 'SC' then read the Cc: list now - WG7J */
    if((m->stype == 'C') && !(m->sid & MBX_SID))
	{
		char *unlimited_cc_line;

        m->stype = 'P'; /* make everything private */
        j2tputs(CcLine);
	/*
	 * 14Oct2014, Brand new function that allows us to read an unlimited size
	 * string without the need for a fixed length input buffer, it simply gives
	 * us a string to play with. This CC part is now independent of m->line !!
 	 */
		if ((unlimited_cc_line = ufgets (m->user)) != NULL)
		{
            if (strlen (unlimited_cc_line))
			{
                if (*unlimited_cc_line == 0x01)	/* CTRL-A, abort */
				{
					free(unlimited_cc_line);
                    free(rhdr);
                    del_list(cclist);
                    j2tputs(MsgAborted);
                    return 0;
                }

                cp = unlimited_cc_line;

        /* get all the Cc addresses, separated by commas, tabs or blanks */
                while((cp2=strpbrk(cp,", \t")) != NULLCHAR)
				{
					 /* Allow , and whitespace separators */
                    *cp2 = '\0';

            /*get rid of leading spaces or tabs*/
                    while(*cp == ' ' || *cp == '\t')
                        cp++;

                    if(strlen(cp))
                        addlist(&cclist,cp,0,NULLCHAR);

                    cp = cp2 + 1;
                }

        /* Do the last or only one */
        /* get rid of leading spaces or tabs*/
                while(*cp == ' ' || *cp == '\t')
                    cp++;
                if(strlen(cp))
                    addlist(&cclist,cp,0,NULLCHAR);
            }

			free(unlimited_cc_line);
        }
		else
		{
            free(rhdr);
            del_list(cclist);
            return 0;
        }
    }
  
    /* Now check to make sure we can create the needed tempfiles - WG7J */
#if defined(EDITOR) && defined(ED) && defined(SEND_EDIT_OK)
    if((m->tfile = fopen(tmpnam(tmpfname),"w+b")) == NULLFILE) {
#else
    if((m->tfile = tmpfile()) == NULLFILE) {
#endif
        free(rhdr);
        del_list(cclist);
/*
    j2tputs((m->sid & MBX_SID) ? "NO - no temp file\n" : \
        "Can't create temp file for mail\n");
    return 0;
 */
    /* instead of saying NO and have the other bbs think we already
     * have the message, disconnect !
     */
        if(m->sid & MBX_SID)
            return -2;
    /* tell regualr users about it */
        j2tputs("Can't create temp file for mail\n");
        return 0;
    }
#ifdef RLINE
    /* Only accept R: lines from bbs's */
    if((m->sid & MBX_SID)&&(Rdate || Rreturn || Rfwdcheck || Mbloophold)){
    /* Going to interpret R:headers,
     * we need another tempfile !
     */
        if((m->tfp = tmpfile()) == NULLFILE) {
            free(rhdr);
            del_list(cclist);
            MYFCLOSE(m->tfile);
#if defined(EDITOR) && defined(ED) && defined(SEND_EDIT_OK)
            unlink(tmpfname);  /* since we didn't use tmpfile() */
#endif
/*
        j2tputs("NO - no temp file\n");
        return 0;
 */
        /* disconnect to avoid the other bbs to think that we already have
         * the message !
         */
            return -2;
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
  
    m->state = MBX_SUBJ;
    if(m->stype != 'R' || (m->sid & MBX_SID)) {
#ifdef FBBFWD
       if(!(m->sid & MBX_FBBFWD))
          /* Don't display the OK or Subject: tag on FBB forwards */
#endif
        j2tputs((m->sid & MBX_SID) ? "OK\n" : "Subject:\n");
        if(mbxrecvline(m) == -1) {
#ifdef RLINE
            if(check_r) {
                MYFCLOSE(m->tfp);
                Checklock--;
            }
#endif
            return 0;
        }
    }
    else                            /* Replying to a message */
        tprintf("Subject: %s\n",m->line);
  
    m->subject = j2strdup(m->line);
  
#ifdef RLINE
    if(!check_r) {
#endif
        mbx_data(m,cclist,rhdr);
    /*Finish smtp headers*/
        fprintf(m->tfile,"\n");
#ifdef RLINE
    }
#endif
    m->state = MBX_DATA;
    if(!(m->sid & MBX_SID) && m->stype != 'F')
        tprintf("Enter message.  %s",Howtoend);
  
    if(m->stype != 'F' || (m->sid & MBX_SID) != 0) {
        while(mbxrecvline(m) != -1){
            if(m->line[0] == 0x01 && !(m->sid & MBX_SID)){  /* CTRL-A */
                MYFCLOSE(m->tfile);
#if defined(EDITOR) && defined(ED) && defined(SEND_EDIT_OK)
                unlink(tmpfname);  /* since we didn't use tmpfile() */
#endif
#ifdef RLINE
                if(check_r)
                    MYFCLOSE(m->tfp);
#endif
                j2tputs(MsgAborted);
                free(rhdr);
                del_list(cclist);
                return 0;
            }
			do_eol_ctlz = 0;
            if(m->line[0] != CTLZ && stricmp(m->line, "/ex"))
			{
#ifdef RLINE
                if(check_r)
				{
            /* Check for R: lines to start with */
                    if(!strncmp(m->line,"R:",2)) { /*found one*/
                        found_r = 1;
            /*Write this line to the second tempfile
             *for later rewriting to the real one
             */
                        fprintf(m->tfp,"%s\n",m->line);
            /* Find the '@[:]CALL.STATE.COUNTRY'or
             * or the '?[:]CALL.STATE.COUNTRY' string
             * The : is optional.
             */
                        if( ((cp=strchr(m->line,'@')) != NULLCHAR) ||
                        ((cp=strchr(m->line,'?')) != NULLCHAR) ) {
                            if((cp2=strpbrk(cp," \t\n")) != NULLCHAR)
                                *cp2 = '\0';
                /* Some bbs's send @bbs instead of @:bbs*/
                            if (*++cp == ':')
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
                    } else {
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
                        if(Rfwdcheck && found_r && \
                        ((m->stype == 'B') || (m->tomsgid)) ){
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
                            do_eol_ctlz = 1;
                    }
                } else
#endif
                    fprintf(m->tfile,"%s\n",m->line);

				if (!do_eol_ctlz)
				{
                	if(m->line[strlen(m->line)-1] == CTLZ)
                    	do_eol_ctlz = 1;
				}
            }
			else
				do_eol_ctlz = 1;

			if (do_eol_ctlz)
			{
#ifdef RLINE
                if(check_r) {
            /* Hmm, this means we never finished the R: headers
             * tmp file still open !
             */
                    MYFCLOSE(m->tfp);
                }
#endif
                done = 1; /* To indicate the difference between
               * mbxrecvline() returning -1 and /ex ! - WG7J
               * Now also used to indicate if the message should
               * be sent or not !
               */
        /* Now ask users if they want to send this ! - WG7J */
                if( Mbsendquery && !(m->sid & MBX_SID))
				{
				  while (1)
				  {
                    if(charmode_ok(m))
                        c = tkeywait(S_QUERY,0,
                            (int)(OPT_IS_DEFINED(m,TN_LINEMODE)&OPT_IS_SET(m,TN_LINEMODE)));
                    else  /* For AX.25 and NET/ROM connects */
                        c = mykeywait(S_QUERY,m);

                    if(c == -1 || c == 'n' || c == 'N')
					{
                        done = 0;   /* signal delete of message */
                        j2tputs(MsgAborted);
                    }
#if defined(EDITOR) && defined(ED) && defined(SEND_EDIT_OK)
                    else if (c == 'e' || c == 'E')
					{
                        char *argv[3];

                        if(m->privs & SYSOP_CMD)
                            argv[1]="--";
                        else
                            argv[1]="-rd";   /* restricted to cmdline filespec */
                        argv[2] = tmpfname;
                        fflush(m->tfile);
                        close(dup(fileno(m->tfile)));  /* set bytecount */
                        /* If we invoke editor directly, it is possible that the
                         * telnet options resulting from tkeywait() will confuse
                         * the first cmd line read in editor(). We thus read one
                         * more line, which consumes telopts so editor is happy.
                         */
                        j2tputs("Invoking Unix ed(itor); use Q to quit without"
                              " saving changes.\nNow press return to begin:");
                        tflush();
                        if(mbxrecvline(m) != -1)
                            editor(3,argv,m);
                        fseek(m->tfile,0,SEEK_END);  /* be sure at EOF */
                        continue; /* replaces GOTO repeat_query; */
                    }
#endif
					break;	/* force break out of loop */

                  }	/* end of WHILE that replaces REPEAT_QUERY label */
				}
                break;  /* all done */
            }
        }
        if(!done) {
        /* We did NOT get ^Z or /EX, but mbxrecvline returned -1 !!!
         * This means the connection is gone ! - WG7J
         * Now can also mean that the user doesn't want to send msg !
         */
            MYFCLOSE(m->tfile);
#if defined(EDITOR) && defined(ED) && defined(SEND_EDIT_OK)
            unlink(tmpfname);  /* since we didn't use tmpfile() */
#endif
#ifdef RLINE
            if(check_r)
                MYFCLOSE(m->tfp);
#endif
            del_list(cclist);
            free(rhdr); /*Just in case*/
            return 0;
        }
    } else {
        fprintf(m->tfile,"----- Forwarded message -----\n\n");
        msgtofile(m);
        fprintf(m->tfile,"----- End of forwarded message -----\n");
    }
  
    free(rhdr);

    /* Insert customised signature if one is found */

    if(!(m->sid & MBX_SID))	/* not a forwarding BBS */
	{
        sprintf(sigwork,"%s/%s.sig",Signature,
			m->tofrom ? m->tofrom : m->name);

        if((fp = fopen(sigwork,READ_TEXT)) != NULLFILE)
		{
            while(fgets(sigwork,LINELEN,fp) != NULLCHAR)
                fputs(sigwork,m->tfile);
            fclose(fp);
        }

#ifdef	DEFAULT_SIGNATURE
	/*
	 * 23Jul2005, Maiko, New option to allow appending of a default
	 * signature file to all messages that leave the system.
	 */
        sprintf (sigwork, "%s/default.sig",Signature);

        if ((fp = fopen (sigwork, READ_TEXT)) != NULLFILE)
		{
            while (fgets (sigwork, LINELEN, fp) != NULLCHAR)
                fputs (sigwork, m->tfile);

            fclose (fp);
        }
#endif
    }
  
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

#ifdef DEBUG_MBX_HDRS
    mbx_dump ("dosend A", m);
    log (m->user, "Host [%s]", host);
#endif

    /* make up full from name for work file */
    if(m->tofrom != NULLCHAR)
        sprintf(fullfrom,"%s%%%s@%s",m->tofrom, (m->origbbs!=NULLCHAR)?m->origbbs:m->name, Hostname);
    else
        sprintf(fullfrom,"%s@%s",m->name,Hostname);

#ifdef DEBUG_MBX_HDRS
    log (m->user, "fullfrom [%s]", fullfrom);
#endif

    if(cclist != NULLLIST && stricmp(host,Hostname) != 0) {
        fseek(m->tfile,0L,0);   /* reset to beginning */
        fail = queuejob(m->tfile,Hostname,cclist,fullfrom);
        del_list(cclist);
        cclist = NULLLIST;
    }
    addlist(&cclist,m->to,0,NULLCHAR);
    fseek(m->tfile,0L,0);
    fail += queuejob(m->tfile,host,cclist,fullfrom);
    del_list(cclist);
    MYFCLOSE(m->tfile);
#if defined(EDITOR) && defined(ED) && defined(SEND_EDIT_OK)
    unlink(tmpfname);  /* since we didn't use tmpfile() */
#endif
    if(fail){
        if(!(m->sid & MBX_SID)) /* only when we're not a bbs */
            j2tputs("Couldn't queue msg!\n");
    } else {
        if(m->sid & MBX_SID)
		{
            MbRecvd++;

		/*
		 * 02Apr2016, Maiko (VE4KLM), try to log the non-fbb forwards better
		 * 11Jan2018, Maiko, crap, forgot to pass an argument, not many people
		 * are doing non FBB forwarding, so rare to see a crash. I think this
		 * is Ray's issue, I hope this resolves his string of crashes !
		 *
			logmbox (m->user, m->name, "msg %s queued %s", m->tomsgid);
	 	 */
			logmbox (m->user, m->name, "msg %s queued", m->tomsgid);
		}
        else {
            j2tputs("Msg queued\n");
            MbSent++;
        }
    /* BID is now saved in the smtp server ! - WG7J */
    }
    /* Instead of kicking off the smtp now, let it happen in a short time.
     * That way the user gets the prompt back immediately, and the smtp kick
     * will run when the user most likely is working on typing the next command.
     * this *should* improve the user's perception of system's speed, and
     * might speed up bbs forwarding a bit too... :-) - WG7J
     */
    {
        #define WAITTM 5000
        extern struct timer Smtpcli_t;
        int32 t,orig_dur;
  
        stop_timer(&Smtpcli_t);
        orig_dur = Smtpcli_t.duration;
        t = read_timer(&Smtpcli_t);
        if(t <= 0  || t > WAITTM)
            set_timer(&Smtpcli_t,WAITTM);  /* set timer duration */
        if(Smtpcli_t.func == NULL) {    /* in case not set yet */
            Smtpcli_t.func = (void (*)__ARGS((void*)))smtptick;/* what to call on timeout */
            Smtpcli_t.arg = NULL;       /* dummy value */
        }
        start_timer(&Smtpcli_t);        /* and fire it up */
        Smtpcli_t.duration = orig_dur;  /* then back to previous interval */
    }
  
    return 0;
}
 
#endif /* MAILCMDS */
