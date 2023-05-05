/* Some of the code in this file was originally based on the following file:
 * gateway.c : Paul Healy, EI9GL, 900818
 *
 * Rewrote forwarding mechanism to use "X-Forwarded-To" paradigm instead of
 * "X-BBS-To", added timer support, etc.  Anders Klemets, SM0RGV, 901009.
 */
 /* Mods by G1EMM and WG7J */
#include <ctype.h>
#include <time.h>
#include "global.h"
#ifdef MBFWD
#include "bm.h"
#include "mailbox.h"
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
#include "commands.h"
#ifdef FWDFILE
#include "session.h"
#endif
#ifdef UNIX
#include "unix.h"

/*
 * 07Nov2013, Maiko, Some debugging to help me out, printing to the
 * console is many a time a big pain in the neck, rather use logs.
 */
#ifdef	DEBUG_FORWARD_BBS
#define PRINTF(x, args...) log (-1, x, args)
#else
/* n5knx: Avoid stdout so line endings are handled right */
#define PRINTF tcmdprintf
#endif

#define PUTS(x) tcmdprintf("%s%c",x,'\n')
#define PUTCHAR(x) tcmdprintf("%c",x)
#else
#define PRINTF printf
#define PUTS puts
#define PUTCHAR putchar
#endif

#ifdef	HFDD
#include "hfdd.h"
#endif

#ifdef WINLINK_SECURE_LOGIN
/* 03Dec2020, Maiko, MULTIPLE_WL2K_CALLS removed from makefile, changed to WINLINK_SECURE_LOGIN */
/* 19Jun2020, Maiko (VE4KLM), callsigns lists functions in new source file */
#include "j2KLMlists.h"
#endif

/* 03Dec2020, removed HARGROVE_VE4KLM_FOX_USE_BASE36 from makefile, permanent ! */
/* 30Aug2020, Maiko (VE4KLM), Seriously time to extend BID limitation !!! */
#include "j2base36.h"

/* 06Jul2021, Maiko (VE4KLM), Scripting via forward.bbs mechanism */
#define J2_SCRIPT_VIA_FORWARD_FILE

#define J2_COMMENT_ALLOWED_FWDCONN	/* 29Jun2016, Maiko, Permanent fix */

extern long UtcOffset;
extern int MbForwarded;
extern struct cmds DFAR Cmds[];

#define ISPROMPT(s) (strlen(s) > 1 && s[strlen(s)-2] == '>')
static struct timer fwdtimer;
static struct proc *FwdProc;
  
static char *findident __ARGS((char *str, int n, char *result));
static char *fwdanybbs __ARGS((struct mbx *m,int *poll));
static int timeok __ARGS((char *line));
static void fwdtick __ARGS((void *v));
static void fwdproc __ARGS((char *target));
static int isconnbbs __ARGS((struct mbx *m));
static void startfwd __ARGS((int a,void *v1,void *v2));
static int openconn __ARGS((int argc,char *argv[],void *p));
static int sendmsgtobbs __ARGS((struct fwd *f,int msgn,char *dest));
static int16 calc_crc __ARGS((char *buf, unsigned int len, char *buf2));
  

/* Compute CCITT CRC-16 over a buffer */
static int16
calc_crc(buf,len,buf2)
char *buf,*buf2;
unsigned int len;
{
/* 16 bit CRC-CCITT stuff. Extracted from Bill Simpson's PPP */

#define FCS_START       0xffff  /* Starting bit string for FCS calculation */
#define FCS(fcs, c)     (((fcs) >> 8) ^ fcstab[((fcs) ^ (c)) & 0x00ff])

    extern int16 DFAR fcstab[];
    int16 crc;

    crc = FCS_START;
    if(len == 0)
        len = strlen(buf);

    while(len--)
        crc = FCS(crc,*buf++);

    if (buf2 != NULLCHAR) {   /* include another buffer segment in crc? */
        len = strlen(buf2);
        while (len--)
            crc = FCS(crc,*buf2++);
    }
    crc ^= 0xffff;
    return crc;
}

/***************************************************************************
   findident copies the 'n'th alphanumeric sequence from 'str' to result.
   It returns a ptr to result. It returns "\0" for missing identifier etc.
   Uses isalnum macro to decide on alphanumeric/non-alnum status.
*/
static char *
findident(str, n, result)
char *str, *result;
int n;
{
    int count=0; /* current identifier */
    char *rp = result;

    *rp = '\0';
    while ( (count<n) && (*str!='\0') ) { /* Process alnum or non alnum seq */
        while ( (*str!='\0') && (!isalnum(*str)) ) /* Get rid of ';:.@%"# etc */
            str++;
        if ( (*str!='\0') /* (redundant) && isalnum(*str)*/ ) { /* this is an alnum seq */
            count++;
            while ( (*str!='\0') && (isalnum(*str) || (*str=='_')) )
                if (count==n)
                    *rp++ = *str++;
                else str++;
            if (count==n)
                *rp = '\0';
        }
    }
    return result;
}
/**************************************************************************/
/* sendmsg() modified to send the R: line conditioned on Mbheader.
 * also added some additional strings like qth and zipcode etc. to R: line.
 * Original SMTP headers get forwarded optionally.  920114 - WG7J
 * N5KNX 2/95: if we can't send a complete msg, return code -1, otherwise
 * return code 0.
 */
extern char *Mbhaddress;
extern char *Mbfwdinfo;
extern char *Mbqth;
extern char *Mbzip;
extern int Mbsmtptoo;
extern int Mbheader;
extern char shortversion[];

/*
 * 01Dec2004, Maiko, New function replaces the use of the 'early_quit' and
 * all the GOTO calls made to it. New compilers no longer recognize the
 * use of these types of labels. The GOTO should not be used anyways.
 */
int do_early_quit (struct mbx *m, int msgn, long start)
{  
	int result = 0;

    if(m->mfile == NULLFILE || ferror(m->mfile) || feof(m->mfile) ||
        (ftell(m->mfile)-start < m->mbox[msgn].size)) {
#ifdef JPDEBUG
        log(m->user,"forward aborted: truncated %s msg %d would result from err %d",m->area,msgn,errno);
#endif
        usputs(m->user,"\n*** Cannot find complete message body!\n");
        usflush(m->user);  /* in case disconnect still yields a truncated msg */
#if defined(MAILERROR) && defined(JPDEBUG)
        mail_error("MBX FWD %s: Cannot find body for %s msg %d", \
            m->name,m->area,msgn);
#endif
/* OK, we were unable to send a complete msg body.  We rely on returning an
   error code to result in a disconnect rather than a completed msg.
*/
        m->user = -1;  /* this will fail subsequent recvline(m->user...) calls */
        result = -1;   /* this will prevent a /ex or ^Z from being sent */
    }

    fclose(m->mfile);
    m->mfile = NULL;
    return result;
}

int
sendmsg(f,msgn)
struct fwd *f;
int msgn;
{
    struct mbx *m = f->m;
    int i, rheader=0;
    long start = 0;	/* should probably initialize this ! */
    char buf[LINELEN];

#ifdef	DONT_COMPILE
	char *b36bid;
#endif
    /* If the data part of the message starts with "R:" the RFC-822
     * headers will not be forwarded. Instead we will add an R:
     * line of our own.
     */
    /* Forward with our "R:" line conditional upon Mbheader - 921201, WG7J */
    if(Mbheader) {
        /* First send recv. date/time and bbs address */
        usprintf(m->user,"R:%s",mbxtime(f->ind.mydate));
        /* If exists, send H-address */
        if(Mbhaddress != NULLCHAR)
            usprintf(m->user," @:%s",Mbhaddress);
        /* location, if any */
        if(Mbqth != NULLCHAR)
            usprintf(m->user," [%s]",Mbqth);
        /*if there is info, put it next */
        if(Mbfwdinfo != NULLCHAR)
            usprintf(m->user," %s",Mbfwdinfo);

#ifdef	DONT_COMPILE	/* 03Sep2020, Maiko, Gus is of the opinion this serves no purpose */

  /* 03Dec2020, removed HARGROVE_VE4KLM_FOX_USE_BASE36 from makefile, permanent ! */

	/* 30Aug2020, Maiko (VE4KLM), Seriously time to extend BID limitation */
	b36bid = j2base36 (f->ind.msgid);
	// log (-1, "[%s] [%ld]", b36bid, f->ind.msgid);
        usprintf(m->user," #:%s", b36bid);
	free (b36bid);

#endif	/* end of DONT_COMPILE */

        /* The BID, is any */
        if(f->bid[0] != '\0')
            usprintf(m->user," $:%s",&f->bid[1]);
        /* zip code of the bbs */
        if(Mbzip != NULLCHAR)
            usprintf(m->user," Z:%s",Mbzip);
        usputc(m->user,'\n');
    }
  
    /* Open the mailbox file */
    sprintf(buf,"%s/%s.txt",Mailspool,m->area);
    if((m->mfile = fopen(buf,READ_TEXT)) == NULLFILE)
		return (do_early_quit (m, msgn, start));
  
    /* point to start of this message in file */
    start = m->mbox[msgn].start;
    fseek(m->mfile,start,SEEK_SET);
    if (ferror(m->mfile))
		return (do_early_quit (m, msgn, start));

    /* If we also send the smtp headers, now see if the message
     * has any R: headers. If so, send them first.
     */
    if(Mbsmtptoo) {
        while(fgets(buf,sizeof(buf),m->mfile) != NULLCHAR) {
            if(*buf == '\n')
                break;          /* End of smtp headers */
        }
        if(feof(m->mfile) || ferror(m->mfile))
			return (do_early_quit (m, msgn, start));
        /* Found start of msg text, check for R: lines */
        while(fgets(buf,sizeof(buf),m->mfile) != NULLCHAR &&
        !strncmp(buf,"R:",2)) {
            rheader = 1;
            usputs(m->user,buf);
        }
        /* again point to start of this message in file */
        fseek(m->mfile,start,SEEK_SET);
        if(ferror(m->mfile))
			return (do_early_quit (m, msgn, start));
    }
  
    /* Go past the SMTP headers to the data of the message.
     * Check if we need to forward the SMTP headers!
     * 920114 - WG7J
     */
    if(Mbsmtptoo && (rheader || Mbheader))
        usputc(m->user,'\n');
    i = NOHEADER;
    while(fgets(buf,sizeof(buf),m->mfile) != NULLCHAR && *buf != '\n') {
        if(Mbsmtptoo) {
            /* YES, forward SMTP headers TOO !*/
            switch(htype(buf, &i)) {
                case XFORWARD: /* Do not forward the "X-Forwarded-To:" lines */
                case STATUS:   /* Don't forward the "Status:" line either */
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
                    usputc(m->user,'>');
                /*note fall-through*/
                default:
                    if(!strncmp(buf,"From ",5))
                        usputc(m->user,'>');
                    usputs(m->user,buf);
            }
        }
    }
    if(feof(m->mfile) || ferror(m->mfile))
		return (do_early_quit (m, msgn, start));
  
    /* Now we are at the start of message text.
     * the rest of the message is treated below.
     * Remember that R: lines have already been sent,
     * if we sent smtp headers !
     */
    i = 1;
  
    while(fgets(buf,sizeof(buf),m->mfile) != NULLCHAR &&
    strncmp(buf,"From ",5)) {
        if(i) {
            if(!strncmp(buf,"R:",2)) {
                if(Mbsmtptoo) continue;
            } else {
                i = 0;
                if(*buf != '\n')
                    /* Ensure body is separated from R: line */
                    usputc(m->user,'\n');
            }
        }
        usputs(m->user,buf);
    }

    if(feof(m->mfile)) clearerr(m->mfile);  /* only place EOF is acceptable */

	return (do_early_quit (m, msgn, start));
}
  
/* A date and time in Arpanet format was converted by mydate() (relative to
 * current TZ) to a time_t.  We must now return it in mailbox format
 * (yymmdd/hhmmz) (note change to Zulu, not local).
 */
char *
mbxtime(time_t date) {
    time_t cdate;   /* Date corrected for timezone offset */
    extern char *Months[];
    static char buf[13];
    char *cp;
    int month;
  
    cdate = date;
	//log (-1, "cdate %ld", cdate);
    /* adjust for GMT/UTC time (since ctime() will subtract it)- WG7J/n5knx */
    cdate += UtcOffset;   /* timezone value is unreliable in BC++3.1 */
	//log (-1, "cdate %ld", cdate);
    cp = ctime(&cdate);
#ifdef UNIX
    if (*(cp+8) == ' ') *(cp+8) = '0';  /* 04 Feb, not b4 Feb */
#endif
  
    /* Check month */
    for(month=0; month < 12; ++month)
        if(strnicmp(Months[month],cp+4,3) == 0)
            break;
    if(month == 12)
        return NULL;
    month++;
  
    sprintf(buf,"%2.2s%02d%2.2s/%2.2s%2.2sz",cp+22,month,cp+8,cp+11,cp+14);
	//log (-1, "mbxtime [%s]", buf);
    return buf;
}
  
/* Makes a command line and returns -1 if the message cannot be sent */
int
makecl(f, msgn, dest, line, subj, bul)
struct fwd *f;
int msgn;               /* Message number */
char *dest;             /* Destination address to use instead of To: line */
char *line, **subj;      /* Buffers to keep command line and subject */
int *bul;       /* True if message is in public message area */
{
    struct mbx *m = f->m;
    struct let *cmsg = &m->mbox[msgn];
    struct mailindex *ind = &f->ind;
    int bulletin = *bul;
    int foundbid = 0;
    char *cp, *tmp;
    char *to,*atbbs,*from;
    char *bid;
#ifdef FBBFWD
    char bid2[25];   /* room for atol(LONG_MAX,10) */
#endif
  
    if((cmsg->status & BM_HOLD) || (!bulletin && (cmsg->status & BM_READ)))
        return -1;      /* the message was on hold or already read */
  
    /* The following code tries to parse the "To: " line where the
     * address looks like "to", "to@atbbs", "to%atbbs@host" or
       (UUCP-style) "...!athost!toid"
     */
    if(dest != NULL && *dest != '\0')
        to = j2strdup(dest);           /* use replacement destination */
    else
        to = j2strdup(ind->to);
    if((atbbs = strchr(to,'%')) != NULL) {
        *atbbs++ = '\0';    /* "to" ends at the '%' character */
        /* Now get rid of the following '@host' field */
        if((cp = strchr(atbbs,'@')) != NULL)
            *cp = '\0';
        atbbs=j2strdup(atbbs);
    }
    if((cp = strchr(to,'@')) != NULL) {
        *cp = '\0';   /* "to" ends at the '@' character */
        if(!atbbs)
            atbbs = j2strdup(cp + 1);
    }
    else if((cp = strrchr(to,'!')) != NULL) {  /* DL1BJL (but @ takes precedence) */
        tmp = cp + 1;
        *cp = '\0';
        free(atbbs);
        atbbs = j2strdup(((cp = strrchr(to, '!')) != NULL) ? cp + 1 : to);
        tmp=j2strdup(tmp);
        free(to);
        to = tmp;
    }

    // log (-1, "ALEN (possibly stupid) check - to [%s] atbbs [%s]", to, atbbs);
  
    /* "to" or "atbbs" should not be more than 6 characters (ALEN).
     * If "to" is too long, it might simply be because the area name
     * is longer than 6 characters, but it might also be because
     * the address on the To: line is in an obscure format that we
     * failed to parse.
     */
    if(strlen(to) > ALEN) {
        /* Play safe and set "to" and "atbbs" to the area name */
        free(to);
        to = j2strdup(m->area);
        free(atbbs);
        atbbs = j2strdup(m->area);
        if(strlen(to) > ALEN)
            to[ALEN] = '\0';    /* Maximum length is 6 */
    }

    /* Only if the BBS supports "hierarchical routing designators"
     * is the atbbs field allowd to be longer than 6 characters and
     * have dots in it.
     */
    if((m->sid & MBX_HIER_SID) == 0) {
        if(atbbs && strlen(atbbs) > ALEN)
            atbbs[ALEN] = '\0';    /* 6 character limit */
        if(atbbs && (cp = strchr(atbbs,'.')) != NULLCHAR)
            *cp = '\0';       /* cut "atbbs" at first dot */
    }
  
    from = ind->from;
    if(from) {
        if((cp=strpbrk(from,"@%")) != NULL) {
            *cp++ = '\0';
            if((tmp=strpbrk(cp,".@")) != NULL)
                *tmp='\0';  /* truncate from hostname at first dot */
            if(!stricmp(cp,m->name)) {
                /* This message came from the connected BBS, so Abort */
                free(to);
                free(atbbs);
                return -1;
            }
        }

        if((cp=strrchr(from,'!')) != NULL)
            from = cp + 1;

        if(strlen(from) > ALEN)
            *(from+ALEN) = '\0';      /* 6 character limit */
    }
  
    if(*to == '\0' || *from == '\0') {
        free(to);
        free(atbbs);
        return -1;
    }
  
    /* The following code distinguishes between three different types
     * of Message-IDs: abcde@callsign.bbs, abcde@otherhost.domain,
     * and abcde@ourhost.domain.
     * The first type is converted to $abcde and the second to
     * $abcde_host.domain. The last to $abcde_first-part-of-H-address.
     * This preserves compability with BBSes.
     * 2/95 N5KNX: in ourhost.domain case, use msgid%100000 for bid #
     * 6/95 N5KNX: use <from> not our Hostname after the CRC in some bids.
     */
    f->bid[0] = '\0';
    bid = mallocw(max(strlen(ind->messageid)+2, 60));  /* at least 60 bytes */
    strcpy(bid+1, ind->messageid);

#ifdef	BID_DEBUGGING
    log (m->user, "messageid [%s] msgid [%ld]", ind->messageid, ind->msgid);
#endif

    /* Is there a @hostname part ? RFC822 requires it. */
    if((cp = strrchr(bid+1,'@')) != NULL) {
        *bid = '$';
        if(stricmp(cp+strlen(cp) - 4, ".bbs") == 0) {
            /*retain the bid given by user*/
            *cp = '\0';
            foundbid = 1; /* Indicate we found a 'real' bid - WG7J */
#ifdef	BID_DEBUGGING
    		log (m->user, "retained [%s]", bid+1);
#endif
        } else {
            if(strcmp(cp+1,Hostname)) {

		/*
		 * 21Oct2020, Maiko (VE4KLM), this scenario rarely happens I suspect, I can go
		 * back several years in the logs and there is NOTHING in there, except in the
		 * case where I was using an email client to talk with JNOS, but even then, my
		 * client was coming in from outside the usual 'JNOS' subnet or my Hostname is
		 * aliased (actually is), for example ve4klm.ampr.org = winnipeg.ampr.org ...
		 */

#ifdef	BID_DEBUGGING
    		log (m->user, "not from us [%s] [%s]", cp+1, Hostname);
#endif
                /* This is NOT one of our messages!  The Message-ID is quite
                 * likely too long to work as an AX.25 bid, so we'll
                 * use a CRC value computed from the Message-ID and ToAddr,
                 * then take the userid part from the from address to form our bid.
                 * This eliminates dups when multiple Jnos systems send the same
                 * smtp msg out as a bull.  We must save the bid into Historyfile so
                 * we don't see dups ourselves.  We also distinguish the bid format
                 * by leaving off the underscore to be sure it's unique (since TRANSLATEFROM
                 * can have surprising consequences).
                 *  - KF5MG/N5KNX
                 */
                int16 crc = calc_crc(bid,0,to);

		/*
 		 * 14Oct2020, Maiko, crc might not have enough range and loop back, maybe
		 * revisit this code down the road IF it becomes a problem, not sure now,
		 * really now that the base36 is here, why not just it instead of a crc?
		 */
                sprintf(bid, "$%u%s", crc, from /* was: (Mbhaddress)?Mbhaddress:Hostname */);

		/*
		 * 14Oct2020, Maiko, Make sure BID generated by us is uppercase, to be consistent
		 * see the 15Oct2020 comments in the section below, just shaking my head right now,
		 * this entire BID process needs a major revamp, base36 just gives us leeway, nothing
		 * more, and the fact that smtp server manipulates the BID just blows my mind :(
	  	*/

		strupr (bid);

                if (msgidcheck(m->user, bid+1) == 0)  /* bid not on file */
                    storebid(bid+1); /* Save BID */
            }
            else
		{

#ifdef	BID_DEBUGGING
                log (m->user, "Mbhaddress %d %s %s", Mbhaddress, Mbhaddress, Hostname);
#endif

	/* 03Dec2020, removed HARGROVE_VE4KLM_FOX_USE_BASE36 from makefile, permanent ! */

				char *b36msgid;
				/*
				 * 30Aug2020, Maiko (VE4KLM), Time to base36 the 5 character BID,
				 * this extends the current base10 'limit' of 99999 to a bit over
				 * over 60 million, hopefully provides us with plenty of leeway!
				 */
				b36msgid = j2base36 (f->ind.msgid);
				// log (-1, "[%s] [%ld]", b36msgid, f->ind.msgid);
                sprintf (bid, "$%s_%s", b36msgid, (Mbhaddress)?Mbhaddress:Hostname);
				free (b36msgid);

		strupr (bid);	/* 15Oct2020, Maiko (VE4KLM), Not everyone uses Mbhaddress,
							and BID cases are not consistent, just did a search of
							my logs, and seeing too many _call vs _CALL - damn !!!
						 */
            }
            cp = bid;

            /* This is now either $msg#_my_h-address, $msg#_myhostname, or $crc#_fromadr
             * Make this BID style '$msg#_host'
             * ie. cut off after first period - WG7J
             */
            if((cp = strchr(cp,'.')) != NULLCHAR)
                *cp = '\0';
        }
        *(bid+13) = '\0';     /* BIDs should be no longer than 13 bytes (includes $)*/
        strcpy(f->bid,bid);
    }

#ifdef	BID_DEBUGGING
    log (m->user, "debug BID [%s]", bid);
#endif
 
    free(bid);
    if(line != NULL) {

#ifdef B2F
	/* 11Apr2008, Maiko (VE4KLM), Adding B2F support for sending (outgoing) */
        if (m->sid2 & MBX_B2F)
		{
			/*
			 * Remember with B2F all the mail headers are in the payload,
			 * so we just start off with FC proposal code and EM string to
			 * denote the WL2K encapsulated mode. This is then followed by
			 * the BID which is put in further below ...
			 */
			sprintf (line, "FC EM ");
		}
		else
#endif
#ifdef FBBCMP
        if(m->sid & MBX_FBBCMP) {
           if(atbbs)
              sprintf(line, "FA %c %s %s %s ", ind->type, from, atbbs, to);
           else
#ifdef use_local
              sprintf(line, "FA %c %s %s %s ", ind->type, from, "local", to);
#else
              sprintf(line, "FA %c %s %s %s ", ind->type, from, m->name, to);
#endif
        } else
#endif
#ifdef FBBFWD
        if(m->sid & MBX_FBBFWD) {
           if(atbbs)
              sprintf(line, "FB %c %s %s %s ", ind->type, from, atbbs, to);
           else
#ifdef use_local
              sprintf(line, "FB %c %s %s %s ", ind->type, from, "local", to);
#else
              sprintf(line, "FB %c %s %s %s ", ind->type, from, m->name, to);
#endif
        } else
#endif
        if(atbbs)
            sprintf(line, "S%c %s @ %s < %s ", ind->type, to, atbbs, from);
        else
            sprintf(line, "S%c %s < %s ", ind->type, to, from);
/*
 * 29Jun2018, Maiko, Put the entire proposal UPPERCASE, except the BID (next).
 */
       strupr(line);

        /* Add the bid to bulletins,
         * AND ALSO to anything that came in with a bid !
         * Takes care of duplicate 'SP SYSOP@xxx $BID' problems.
         * ALSO add to ALL messages if the remote system supports MID's - WG7J
         */
        if((m->sid & MBX_MID) || ((bulletin || foundbid) & (m->sid & MBX_SID)))
		{
#ifdef FBBFWD
            if(m->sid & MBX_FBBFWD) {
                cp = &f->bid[1];
                if(*cp) strcat(line,cp);
                else return -1;  /* MUST have a bid */
            } else
#endif
            strcat(line,f->bid);
		}
#ifdef B2F
	/* 11Apr2008, Maiko (VE4KLM), Adding B2F support for sending (outgoing) */
        if (m->sid2 & MBX_B2F)
		{
			FILE *fp, *tfile;

			long uncompressed_sz, compressed_sz;

			extern int B2Fsendmsg (struct fwd*, int, FILE*);

			/*
			 * With B2F, after the bid we have uncompressed / compressed
			 * size values of the payload. This could be a challenge because
			 * at this point of the code, I don't have the compressed info.
			 *
			 * 16Apr2008, Maiko (VE4KLM), ARGGGG!!! Looks like we might have
			 * to prepare (fbbsendmsg) BEFORE we send the proposal, instead of
			 * the current method where it's prepared AFTER we get the FS back
			 * from the remote system. I wonder if the size values are actually
			 * being used at this point.
			 *
			 * 18Apr2008, Maiko (VE4KLM), Interesting, I tried to fake the
			 * exact size values and it appears they are not used. As a matter
			 * of fact, I can even specify hyphens with no affect (yet) !!!
			 *
			 * Success !!! Able to send a B2F message TO airmail now :-)
			 *
			 * 28Apr2010, Maiko, RMS Express will crash if we send faked out
			 * values, and we should probably do this properly anyways. This
			 * means calling B2Fsendmsg() BEFORE we create FC proposal (here),
			 * which goes against how JNOS has always done it for earlier FA
			 * and FB proposals. In order to fill in the payload uncompressed
			 * and compressed values, we'll have to build the tfile NOW and
			 * call Encode to get the compressed information - ARGGG :(
			 *
			strcat (line, " - - 0");
			 *
			 */

			/*
			 * 28Apr2010, Maiko, This code is from fbbfwd.c, but in the case
			 * of B2F *ONLY* we will call it NOW instead of waiting for the FS
			 * from the remote system.
			 */
			strcpy (f->iFile, j2tmpnam (NULL));

			if ((tfile = fopen (f->iFile, "w+b")) == NULLFILE)
			{
				log (m->user, "FBBFWD - FC proposal errno %d opening %s",
					errno, f->iFile);
				return -1;
			}
			if (B2Fsendmsg (f, msgn, tfile) == -1)
			{
				log (m->user, "FBBFWD - FC proposal - B2Fsendmsg failed");
				return -1;
			}

			strcpy (f->oFile, j2tmpnam (NULL));

			/*
			 * at this point, tfile is the payload, now Encode to get the
			 * compressed size - code portion taken from lzhuf.c source,
			 * for now it means calling Encode() twice for B2F, I don't
			 * want to try and economize the code for the prototype.
			 */

			if (!AllocDataBuffers (f))
			{
				log (m->user, "FBBFWD - FC proposal - AllocDataBuffers failed");
				return -1;
			}

			f->lzhuf->codesize = 0;
			f->lzhuf->getbuf   = 0;
			f->lzhuf->getlen   = 0;
			f->lzhuf->putbuf   = 0;
			f->lzhuf->putlen   = 0;
			f->lzhuf->code     = 0;
			f->lzhuf->len      = 0;

			if (Encode (-1, f->iFile, f->oFile, f->lzhuf, 1))
			{
				log (m->user, "FBBFWD - FC proposal - Encode failed");
				return -1;
			}

			/* Get size of iFile and oFile and concatonate to FC proposal */

			/*
			 * 05May2010, Maiko, Okay, After analyzing the data stream with
			 * an incoming mail from RMS Express, the compressed value in the
			 * FC proposal is indeed the STX (recv_yapp) data length(s), and I
			 * have confirmed that I am also sending the correct value here.
			 */
			fp = fopen (f->oFile, "rb");
			fseek (fp, 0L, SEEK_END);
			compressed_sz = ftell (fp);
			fclose (fp);

			/*
			 * 05May2010, Maiko, Same thing, I've analyzed the data stream
			 * and with extra debugging in lzhuf.c, the uncompressed value in
			 * the FC proposal is the size of the output file from Decode,
			 * which SHOULD be the size of the input going into Encode, so
			 * why then is this not moving along ???
			 */
			fp = fopen (f->iFile, "rb");
			fseek (fp, 0L, SEEK_END);
			uncompressed_sz = ftell (fp);
			fclose (fp);

			/* log (-1, "uncompressed [%ld] compressed [%ld]",
				uncompressed_sz, compressed_sz);
			 05Sep2010, Maiko (VE4KLM), remove debugging */

			sprintf (line + strlen (line), " %ld %ld 0",
				uncompressed_sz, compressed_sz);
		}
		else
#endif
#ifdef FBBFWD
        if(m->sid & MBX_FBBFWD) {
            strcat(line," ");
            sprintf(bid2,"%ld", ind->size);
            strcat(line,bid2);
        }
#endif
        strcat(line,"\n");
    }
    if(subj)
        *subj = j2strdup(ind->subject);
    free(to);
    free(atbbs);
    return 0;
}

static int /* 0 = ok, -1 = problem so disc */
sendmsgtobbs(f, msgn, dest)
struct fwd *f;
int msgn;
char *dest;             /* Optional destination address to override To: line */
{
    int result = -1;
    struct mbx *m = f->m;
    struct fwdbbs *bbs;
    int bulletin = (m->areatype == AREA);
    char *subj = NULL;
    char line[MBXLINE+1];
  
    /* Check x-forwarded-to fields */
    for(bbs=f->ind.bbslist;bbs;bbs=bbs->next)
        if(!stricmp(bbs->call,m->name))
            return 0;
  
    if(makecl(f, msgn, dest, line, &subj, &bulletin) == -1)
        return 0;       /* do not forward this particular message */

/*
 * 29Jun2018, Maiko, NO UPPER CASING !!!
 */
#ifdef	DONT_COMPILE
	/*
	 * 08Feb2015, Maiko (VE4KLM), be consistent, this proposal should also
 	 * be in UPPER CASE since the one's we send in forward.c already are. I
	 * am not sure what to do about the issue of BPQ and case sensitivity of
	 * the MID/BID comparisons, keep an eye on the 'conversation' for now !
	 */
	strupr(line);
#endif
	/* 02Apr2016, Maiko (VE4KLM), log outgoing proposal */
	logmbox (m->user, m->name, "proposal %s", line);

    j2tputs(line);          /* Send mail offer to bbs */
/*    rip(line); */
    usflush(m->user);
    if(
#ifdef FWDFILE
       m->family == AF_FILE ||
#endif
       recvline(m->user, m->line, MBXLINE) != -1)
	 {
    	if(Mtrace)
			log (-1, "about to send [%s]", m->line);

        if(m->line[0] == 'O' || m->line[0] == 'o' || (m->sid & MBX_SID) == 0)
		{
            /* Got 'OK' or any line if the bbs is unsofisticated */
            tprintf("%s\n", subj);

            if (sendmsg(f,msgn) != -1)   /* send the message */
#ifdef FWDCTLZ
                /* Some bbs code doesn't like /EX too well... */
#ifdef FWDFILE
                if (m->family == AF_FILE)
                    j2tputs("/EX\n");
                else
#endif /* FWDFILE */
                j2tputs("\032\n");
#else
                j2tputs("/EX\n"); /* was 0x1a */
#endif /* FWDCTLZ */

            usflush(m->user);

            /* get F> for a good deliver */
            while (
#ifdef FWDFILE
                   (m->family == AF_FILE && m->user != -1) ||
#endif
                   recvline (m->user, m->line, MBXLINE) != -1)
				{
                if(
#ifdef FWDFILE
                   m->family == AF_FILE ||
#endif
                   ISPROMPT(m->line))
					{
    				if(Mtrace)
						log (-1, "looks like sent [%s]", m->line);
                    rip(line);  /* N5KNX: now drop NL for nicer log entry */
                    logmbox (m->user, m->name, "sent %s", f->bid);
                    if(m->areatype == AREA)
                        m->mbox[msgn].status |= BM_FORWARDED;
                    else
                        m->mbox[msgn].status |= BM_DELETE;
                    m->change = 1;
                    result = 0;
                    MbForwarded++;
                    break;
                }
            }
        }
		else
		{
			 /* OK response not received from bbs */

            if (m->line[0] == 'N' || m->line[0] == 'n')
			{
				 /* 'NO' respone */
                rip(m->line);   /* N5KNX: nicer log entry */
                logmbox (m->user, m->name, "refused %s", m->line);
                /* Mark refused message as forwarded if it is a bulletin.
                 * The message was probably a duplicate. Non-bulletin
                 * messages are sent without BID, so they cannot be dected
                 * as duplicates. The reason why it was refused is probably
                 * because the address was invalid. Retry later.
                 *
                 * After lots of complaining, this behaviour is changed:
                 * ALL messages that get the NO reply are marked
                 * as forwarded ! - WG7J 930124
                 */
                if(m->areatype == AREA)
                    m->mbox[msgn].status |= BM_FORWARDED;
                else
                    m->mbox[msgn].status |= BM_DELETE;
                m->change = 1;
                /* Count this as forwarded ! - WG7J */
                MbForwarded++;
            }
            /* should get a F> here */
            while (recvline (m->user, m->line, MBXLINE) != -1 ) {
                if (ISPROMPT(m->line)) {
                    result = 0;
                    break;
                }
            }
        }
    } /* OK or NO here */
    free(subj);
    return result;
}
  
int FwdUsers;
void exitfwd(struct mbx *m) {
    FwdUsers--;
    if(m->state != MBX_TRYING)
		logmbox (m->user, m->name, "fwd exit");
    exitbbs(m);
}


/* This is the main entry point for reverse forwarding. It is also used
 * for normal, "forward", forwarding.
 */
int
dorevfwd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char *cp, *dp;
    int i, idx, err = 0;
    struct fwd f;
    struct indexhdr hdr;
    char fn[FILE_PATH_SIZE];
    char oldarea[64];
  
    f.m = (struct mbx *)p;
    memset(&f.ind,0,sizeof(struct mailindex));
  
    logmbox (f.m->user, f.m->name, "reverse forwarding mail");
    /* indicate we are doing reverse forwarding, if we are not already
     * doing normal forwarding.
     */
    if(f.m->state != MBX_FORWARD)
        f.m->state = MBX_REVFWD;
  
    if(fwdinit(f.m) != -1) {
        strcpy(oldarea,f.m->area);
        while(!err && fgets(f.m->line,MBXLINE,f.m->tfile) != NULLCHAR) {
            pwait(NULL);
            if(*f.m->line == '-')     /* end of record reached */
                break;
            cp = f.m->line;
            rip(cp);           /* adds extra null at end */
            /* skip spaces */
            while(*cp && (*cp == ' ' || *cp == '\t'))
                cp++;
            if(strchr(FWD_SCRIPT_CMDS,*cp)!=NULLCHAR)
                continue;	/* ignore empty or connect-script lines */
            /* find end of area name, and beginning of optional destination string */
            for (dp=cp; *dp && *dp != ' ' && *dp != '\t' && *dp != '\n'; dp++) ;
            if (*dp) *dp++ = '\0';
            changearea(f.m,cp);
            /* Now create the index filename */
            sprintf(fn,"%s/%s.ind",Mailspool,cp);
  
            /* strip leading blanks from dest */
            cp=dp;
            while(*cp && (*cp == ' ' || *cp == '\t'))
                cp++;
            /* find end of optional destination */
            for (dp=cp; *dp && *dp != ' ' && *dp != '\t' && *dp != '\n'; dp++) ;
            if (*dp) *dp = '\0';

            cp = j2strdup(cp);
            /* open the index file */
            if((idx=open(fn,READBINARY)) != -1) {
                /* check if there are any messages in this area
                 * that need to be forwarded.
                 */
                if(read_header(idx,&hdr) != -1)
		{
                    for(i=1; i<=f.m->nmsgs; i++)
			{

                        //pwait(NULL); /* 25Oct2009, Maiko, Too aggressive ! */
			j2pause(200);

                        if(read_index(idx,&f.ind) == -1) {
                            err = 1;
                            break;
                        }
                        if(sendmsgtobbs(&f, i, cp) == -1) {
                            err = 1;        /* abort */
                            break;
                        }
#ifdef USERLOG
                        f.m->newlastread = f.ind.msgid;  /* remember last processed OK */
#endif
                        /* Done with this index, clear it */
                        default_index(f.m->area,&f.ind);
                        scanmail(f.m);
                    }
                }
                close(idx);
                idx = 0;
            }
            if(f.m->mfile) {
                fclose(f.m->mfile);
                f.m->mfile = NULL;
            }
            free(cp);
        }
        fclose(f.m->tfile);
        f.m->tfile = NULLFILE;
        changearea(f.m,oldarea);  /* 1.11a: oldarea can be "" */
    }
    default_index("",&f.ind);
    if(f.m->state == MBX_FORWARD)
        return err;
    j2tputs("*** Done\n");
    return err;
}
  
/* Read the forward file for a record for the connected BBS. If found,
 * return 1 if this is the right time to forward, m->tfile is left pointing
 * at the first message area to be forwarded.
 */
int
fwdinit(m)
struct mbx *m;
{
    char host[80];
    int start = 1;
#if defined(EXPIRY) && defined(FBBFWD)
extern int Eproc;

    if (Mfbb && (m->sid & MBX_FBBFWD) && Eproc) {
        log(m->user, "FBB outgoing fwding suspended during Expiry");
        return -1;  /* don't FBB-forward while expire is running */
                    /* since our saved group of 5 indices is unreliable */
    }
#endif /* EXPIRY && FBBFWD */

    if((m->tfile = fopen(Forwardfile,READ_TEXT)) == NULLFILE)
        return -1;
  
    while(fgets(m->line,MBXLINE,m->tfile) != NULLCHAR) {
        if(*m->line == '\n' || *m->line == '#')  /* comment or empty line */
            continue;
        /* lines starting with '-' separate the forwarding records */
        if(*m->line == '-') {
            start = 1;
            continue;
        }
        if(start) {
            start = 0;
            /* get the name of this forwarding record */
            findident(m->line,1,host);
            if(stricmp(m->name,host) == 0) {
                if(!timeok(m->line))
                    continue;  /* N5KNX: too early or late.  Continue scan, */
                         /* thus allowing multiple time-differentiated entries for same host */
#ifdef J2_COMMENT_ALLOWED_FWDCONN
	/*
	 * 29Jun2016, Maiko (VE4KLM), watch out for comments, technically one
	 * should be able to put them where ever they want, but previous versions
	 * of this code expect the connect sequence immediately after identifier,
	 * so this little addition allows for '#' comment entries. Thanks to Bob,
     * VE3TOK for reporting this, he accidently ran into when commenting out
	 * one of his connect methods for a particular entry. Many folks typically
	 * have multiple ways of getting to a system, comment out is convenient.
	 *
	 * before this fix, nothing would happen, would say connected, but wasn't !
	 *
	 */
            	while ((fgets(m->line,MBXLINE,m->tfile) != NULLCHAR) &&
					(*(m->line) == '#'));
#else
                /* eat the connect command line */
                fgets(m->line,MBXLINE,m->tfile);
#endif
                return 0;
            }
        }
    }
    fclose(m->tfile);
    m->tfile = NULLFILE;
    return -1;
}
/* Read the forward file for a record for the next sequential BBS. If found,
 * determine if this is the right time to forward, and return the command
 * line to establish a forwarding connection. m->tfile is left pointing
 * at the first message area to be forwarded.
 */
static char *
fwdanybbs(m,poll)
struct mbx *m;
int *poll;
{
    char host[80];
    int start = 1,i;
  
    if(m->tfile == NULLFILE && (m->tfile = fopen(Forwardfile,READ_TEXT))
        == NULLFILE)
        return NULLCHAR;
    *poll = 0;  /* Default to no polling */
    while(fgets(m->line,MBXLINE,m->tfile) != NULLCHAR) {
        if(*m->line == '\n' || *m->line == '#')  /* comment or empty line */
            continue;
        /* lines starting with '-' separate the forwarding records */
        if(*m->line == '-') {
            start = 1;
            continue;
        }
        if(start) {
            start = 0;
            /* get the name of this forwarding record */
            findident(m->line,1,host);
            strcpy(m->name,host);
            if(!timeok(m->line))
                continue;       /* too late or too early */
            /* Check for polling - WG7J */
            i=2;
            while(*findident(m->line,i++,host)) {
                if(*host == 'P' || *host == 'p') {
                    *poll = 1;
                    break;
                }
            }
#ifdef J2_COMMENT_ALLOWED_FWDCONN
	/*
	 * 29Jun2016, Maiko (VE4KLM), watch out for comments, technically one
	 * should be able to put them where ever they want, but previous versions
	 * of this code expect the connect sequence immediately after identifier,
	 * so this little addition allows for '#' comment entries. Thanks to Bob,
     * VE3TOK for reporting this, he accidently ran into when commenting out
	 * one of his connect methods for a particular entry. Many folks typically
	 * have multiple ways of getting to a system, comment out is convenient.
	 *
	 * before this fix, nothing would happen, would say connected, but wasn't !
	 *
	 */
            	while ((fgets(m->line,MBXLINE,m->tfile) != NULLCHAR) &&
					(*(m->line) == '#'));
#else
            	/* get the connect command line */
            	fgets(m->line,MBXLINE,m->tfile);
#endif
            if (!strnicmp(m->line,"skip",4))
                continue;  /* skip this host: we never initiate a fwd to him */
            return j2strdup(m->line);
        }
    }
    fclose(m->tfile);
    m->tfile = NULLFILE;
    return NULLCHAR;
}
  
/* get any groups of four digits that specify the begin and ending hours of
 * forwarding. Returns 1 if forwarding may take place.
 */
static int
timeok(line)
char *line;
{
    char hours[80], *now;
    long t;
    int t1, t2, pos = 2;
    int h, rangecount=0;

    time(&t);
    now = ctime(&t) + 11;
    *(now + 2) = '\0';
    h=atoi(now);

    while(*findident(line,pos++,hours)) {
        if(*hours == 'P' || *hours == 'p')
            continue;
        t1 = (*hours - '0') * 10 + (*(hours+1) - '0');
        t2 = (*(hours+2) - '0') * 10 + (*(hours+3) - '0');
        if(h >= t1 && h <= t2)
            return TRUE;            /* right in time */
        rangecount++;
    }
    return (rangecount == 0);
    /* no digits (but perhaps a P) means 0023, ie, anytime so return true */
}
  
int
dombtimer(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2){
        tprintf("Forwarding timer: %d/%d server %s.\n",
        read_timer(&fwdtimer)/1000,
        dur_timer(&fwdtimer)/1000,
        FwdProc != NULLPROC ? "started":"stopped");
        return 0;
    }
    fwdtimer.func = (void (*)__ARGS((void *)))fwdtick;/* what to call on timeout */
    fwdtimer.arg = NULL;            /* dummy value */
    set_timer(&fwdtimer,(uint32)atoi(argv[1])*1000); /* set timer duration */
    pwait(NULL);
    if (FwdProc != NULLPROC)    /* if someone is listening */
        start_timer(&fwdtimer);     /* fire it up */
    else
        if(dur_timer(&fwdtimer) != 0)
            j2tputs("Warning: forward server not started.\n");
    return 0;
}
  
int
dombkick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if (FwdProc == NULLPROC) {
        j2tputs("Forward server not started\n");
        return 1;
    }
    if (argc > 1) { /* kick just one target bbs */
        if (fwdtimer.arg) {  /* one at a time! */
            j2tputs("mbox kick busy - try later\n");
            return 1;
        }
        fwdtimer.arg = j2strdup(argv[1]);
    }
    j2psignal(&fwdtimer,0);
    return 0;
}
  
/* fwdtick() is called when the forward timer expires.  We just signal on
 * &fwdtimer to awaken the server proc fwdstart(), then restart the timer.
 */
static void
fwdtick(v)
void *v;
{
    j2psignal(&fwdtimer,0);           /* awake the forwarder */
    start_timer(&fwdtimer);     /* and restart the timer */
}
  
/* the main process for the mailbox forwarder */
int
fwdstart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if (FwdProc != NULLPROC)
        return 0;       /* already started */
  
    FwdProc = Curproc;      /* set our flag */
  
    j2psignal(Curproc,0);     /* don't wait on us */
  
    start_timer(&fwdtimer);     /* start timer (ignored if 0) */
  
    for (;!pwait(&fwdtimer);) {  /* wait for someone to tell us to try */
#ifndef UNIX
        if (availmem() > Memthresh)
#endif
            fwdproc((char *)fwdtimer.arg);
#ifndef UNIX
        else if(Mtrace)
            j2tputs("fwd: forwarding skipped due to low memory\n");
#endif
        free(fwdtimer.arg);
        fwdtimer.arg = NULL;
    }
  
    FwdProc = NULLPROC;     /* we are exiting */
    return 0;           /* alerted from somewhere */
}
  
/* (attempt to) kill the forwarder process */
int
fwd0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int i, max;         /* max attempts */
  
    stop_timer(&fwdtimer);      /* no more timer awakes */
    max = 1;            /* Maximum attempts */
    if (argc > 2)
        setint(&max,NULLCHAR,argc,argv);
  
    for(i=0;i<max && FwdProc != NULLPROC;i++) {
        alert(FwdProc,1);   /* signal regardless of location */
        pwait(NULL);        /* let it see the alert */
    }
  
    stop_timer(&fwdtimer);      /* in case timer tick restarted it */
    return 0;
}
  
/* Routine that does all the work for the forwarding process, fwdstart() */
static void
fwdproc(target)
char *target;
{
    char *cc, *cp, *fp;
    struct mbx *m;
    struct fwdbbs *bbs;
    struct indexhdr hdr;
    struct mailindex ind;
    int i, idx, bulletin, poll,skip = 0;
    char fn[FILE_PATH_SIZE+MBXLINE+1]; /* 01Oct2019, Maiko, compiler format overflow warning, so added +MBXLINE+1 bytes */
  
    if(Mtrace)
        j2tputs("mbox: fwd started\n");
    if((m = newmbx()) == NULLMBX){
        if(Mtrace)
            j2tputs("fwd: no new mbox\n");
        return;
    }
    m->user = Curproc->output;
    m->state = MBX_TRYING;
    memset(&ind,0,sizeof(struct mailindex));
    while((cc = fwdanybbs(m,&poll)) != NULLCHAR) {
        if (target)
		{
            if(stricmp(target,m->name))
                skip=1;    /* skip unless we have a name match */
            else
                poll=(isupper(*target) || isupper(*(target+1)));  /* force a poll if uppercase */
		}
        if(!skip && isconnbbs(m)) { /* already connected to this BBS, skip it */
            skip = 1;
            if(Mtrace)
                tprintf("fwd: %s is connected or already trying\n",m->name);
        }
#ifndef UNIX
        if (availmem() < Memthresh) {  /* don't even try if we're low */
            skip=1;
            if(Mtrace)
                tprintf("fwd: forwarding to %s skipped due to low memory\n", m->name);
        }
#endif

        /* If we poll, there is no need to check message area, since this
         * is also done later. It will speed things up here - WG7J
         */
        if(!skip && poll) {
            if(Mtrace)
                tprintf("fwd: polling %s\n",m->name);
            newproc("Mbox forwarding", 2048,startfwd, 0, (void *)cc,
                (void *)j2strdup(m->name),0);
            cc = NULLCHAR;
            skip = 1;
        }
        if(!skip && Mtrace)
            tprintf("fwd: %s - checking for messages\n",m->name);
        while(fgets(m->line,MBXLINE,m->tfile) != NULLCHAR) {
            pwait(NULL);
            if(*m->line == '-') {   /* end of record reached */
                skip = 0;
                break;
            }
            if((cp = strpbrk(m->line," \t")) != NULLCHAR)
                *cp = '\0';
            if(skip || strchr(FWD_SCRIPT_CMDS,*m->line)!=NULLCHAR)
                continue;
            rip(m->line);
            bulletin = isarea(m->line);     /* public area */
            sprintf(fn,"%s/%s.ind",Mailspool,m->line);
            if((idx=open(fn,READBINARY)) != -1) {
                /* check if there are any messages in this area
                 * that need to be forwarded.
                 */
                if(read_header(idx,&hdr) == -1)
                    hdr.msgs = 0;
                for(i=1; i<=hdr.msgs; i++) {
                    pwait(NULL);
                    /* Done with this index, clear it */
                    default_index("",&ind);
                    if(read_index(idx,&ind) == -1)
                        break; /* Should not happen ! */
                    /* Apply same tests as in makecl() */
                    if(ind.status & BM_HOLD) continue;
                    if(bulletin) {
                        for(bbs = ind.bbslist;bbs;bbs=bbs->next)
                            if(!stricmp(bbs->call,m->name))
                                break;
                        if(bbs)
                            continue;
                    } else if(ind.status & BM_READ)
                        continue;
                    if(ind.from) {
                        if((cp=strpbrk(ind.from,"@%")) != NULL) {
                            if((fp=strpbrk(++cp,".@")) != NULL)
                                *fp='\0';  /* truncate from hostname at first dot */
                            if(!stricmp(cp,m->name))
                                continue; /* Don't fwd back to originating host */
                        }
                    }
                    if(Mtrace) {
                        sprintf(fn,"fwd: starting %s (%s,#%d)",m->name, m->line, i);
                        j2tputs(fn);  tputc('\n');
                        log(-1,fn);  /* log for the benefit of remote sysops */
                    }
                    newproc("Mbox forwarding", 2048, startfwd, 0, (void *)cc,
                            (void *)j2strdup(m->name),0);
                    skip = 1;
                    cc = NULLCHAR;
                    break;
                }
                /* Done with this index, clear it */
                default_index("",&ind);
                close(idx);
            }
        }
        free(cc);
    }
    default_index("",&ind);
    usesock(Curproc->output);   /* compensate for close_s() in exitbbs */
    exitbbs(m);
}
  
/* returns 1 if m->name matches the name of another connected mailbox. */
static int
isconnbbs(m)
struct mbx *m;
{
    struct mbx *mp;
    int tries=0;
  
    for(mp=Mbox;mp;mp=mp->next)
        if( (stricmp(mp->name,m->name) == 0) &&
            ((mp->state != MBX_TRYING) || tries++) )
                return 1;
    return 0;
}
  
/* possible commands on the command line in the forwarding file */
static struct cmds cfwdcmds[] = {
    { "tcp",          openconn,       0, 0, NULLCHAR },
#ifdef	IPV6
/* 26Mar2023, Maiko, Allow forwarding with IPV6 systems */
    { "6",          openconn,       0, 0, NULLCHAR },
#endif
    { "telnet",       openconn,       0, 0, NULLCHAR },
#ifdef AX25
    { "ax25",         openconn,       0, 0, NULLCHAR },
    { "connect",      openconn,       0, 0, NULLCHAR },
#endif
#ifdef NETROM
    { "netrom",       openconn,       0, 0, NULLCHAR },
#endif
#ifdef FWDFILE
    { "file",         openconn,       0, 0, NULLCHAR },
#endif
#ifdef  EA5HVK_VARA
    { "vara",           openconn,       0, 0, NULLCHAR },
#endif
    { NULLCHAR,		NULL,		0, 0, NULLCHAR }
};

/* 01Dec2004, Maiko, New function replaces 'fin0_fwd' GOTO & labels */
static void do_fin0_fwd (struct mbx *m)
{
	exitfwd (m);
	return;
}

/* 01Dec2004, Maiko, New function replaces 'fin2_fwd' GOTO & labels */
static void do_fin2_fwd (char *continuestr)
{
#ifdef FWD_COMPLETION_CMD
    if (*continuestr) {  /* Run end-of-fwd cmd, if any */
        Curproc->output = Command->output;   /* so tprintf works */
        usesock(Curproc->output);
#ifdef REDIRECT
        Curproc->input = Command->input;   /* so redirection works */
        usesock(Curproc->input);
#endif
        cmdparse(Cmds, continuestr, NULL);
    }
#endif /* FWD_COMPLETION_CMD */
    return;
}

/* 01Dec2004, Maiko, New function replaces 'fin_fwd' GOTO & labels */
static void do_fin_fwd (struct mbx *m, char *continuestr)
{
    exitfwd(m);

	return (do_fin2_fwd (continuestr));
}

#ifdef	WINLINK_SECURE_LOGIN
/* 03Dec2020, Maiko, MULTIPLE_WL2K_CALLS removed from makefile, changed to WINLINK_SECURE_LOGIN */

/*
 * 04Jun2020, Maiko (VE4KLM), instead of duplicating code, move existing
 * code into it's own function, since I need to call this more then once.
 *
 * Pass the logging socket, the callsign, and PQ Challenge code.
 *
 */
char *wl2k_build_response (int user, char *thecall, char *PQchallenge)
{
	extern char *J2ChallengeResponse (char*, char*);

	extern char *j2userpasswd (int, char*);

	char *mcptr, *crstr;

	log (user, "[%s] requesting secure login [%s] PQchallenge", thecall, PQchallenge);

	/* 26Feb2020, Maiko, Error check was missing, need to have this !!! */
	if ((mcptr = j2userpasswd (1, thecall)) == (char*)0)
	{
		log (user, "error - did you create a Winlink CMS user ?");

		return (char*)0;
	}

	/* 22Oct2015, Maiko (VE4KLM), build response, send after my SID */
	crstr = J2ChallengeResponse (PQchallenge, mcptr);

	free (mcptr);	/* 26Feb2020, Maiko, Need to free this up when done with it */

	log (user, "[%s] challenge response", crstr);

	return crstr;
}

#endif

/* this function is called whenever the forwarding timer expires */
static void
startfwd(a,v1,v2)
int a;
void *v1, *v2;
{
    char Continue[MBXLINE];
    int rval, go_on = 0;
    char *cc, *cp, op;
    struct mbx *m;
    int32 timeout;
#ifdef  J2_SCRIPT_VIA_FORWARD_FILE
    int script;
    FILE *script_fp;
#endif

#ifdef	WINLINK_SECURE_LOGIN
#ifdef	DONT_COMPILE	/* 20June2020, Maiko (VE4KLM), now configured with calls list code */
	/* 23Feb2020, Maiko, Used to be defined as B2F, clean this up */
	char MyCall[AXBUF], *mcptr;
#endif
	char *crstr = NULL;
#endif
#ifdef	WINLINK_SECURE_LOGIN
/* 03Dec2020, Maiko, MULTIPLE_WL2K_CALLS removed from makefile, changed to WINLINK_SECURE_LOGIN */
	char *PQchallenge, callsokay;
#endif
    cc = (char*)v1;
    if ((m = newmbx ()) == NULLMBX)
	{
        free (cc);
        free ((char*)v2);
        return;
    }
    FwdUsers++;
    strcpy (m->name, (char*)v2);
    free ((char*)v2);
    m->state = MBX_TRYING;

#ifdef	HFDD
	if (*cc == 'h')
	{
		m->family = AF_HFDD;	/* make sure mbox list shows proper info */

		m->user = hfdd_connect (cc);

		if (m->user == -1)
		{
        	free (cc);
        	usesock (Curproc->output);
        	return (do_fin0_fwd (m));
		}
	}
	else	
	{
#endif

#ifdef	J2_SCRIPT_VIA_FORWARD_FILE
	/*
	 * 06Jul2021, Maiko (VE4KLM), Modifying the use of forward.bbs to allow
	 * sysops to connect to a remote system, and then just run a series of
	 * commands (to collect information on routing and nodes) and save the
	 * information to a file for parsing afterwards -  Charles (N2NOV) was
	 * asking me for this. Saves me from having to write it from scratch.
	 *
	 * Scripts will be identified with a trailing _script, ie :
	 *
          -------
          n2nov_script
          ax25 newyork n2nov-4
          .nr
          +>
          *5
          -------
          k5dat_script
          net k5dat-7
          .ro
          +>
          *5
          -------
	 *
	 * Note : BPQ has the RO command, JNOS has the NR command, both provide
	 * some form of netrom routing information to any BBS user connecting.
 	 *
	 * This is somewhat rudemamentary, nothing fancy, we can build on it.
	 */

	script = 0;
	script_fp = (FILE*)0;

	if (strstr (m->name, "_script"))
	{
		/* 06Jul2021, Maiko (VE4KLM), Log responses from remote to some dat file */
		char *sfn = malloc (strlen (m->name)+5);
		sprintf (sfn, "%s.dat", m->name);
		script = 1;
		script_fp = fopen (sfn, WRITE_TEXT);
		free (sfn);
	}
#endif
    	/* open the connection, m->user will be the new socket */
    	if(cmdparse(cfwdcmds,cc,(void *)m) == -1)
		{
        	free(cc);
			/* compensate for close_s() in exitbbs */
        	usesock(Curproc->output);
        	return (do_fin0_fwd(m));
    	}
#ifdef	HFDD
	}
#endif

	logmbox (m->user, m->name, "connected");	/* 19Sep2008, Maiko, New */

    free(cc);
    m->state = MBX_FORWARD;
    sockowner(m->user,Curproc);
   	close_s(Curproc->output);
   	close_s(Curproc->input);

   	/* m->user will be closed automatically when this process exits */
   	Curproc->output = Curproc->input = m->user;

   	/* We'll do our own flushing right before we read input */
   	j2setflush(m->user,-1);

    if(fwdinit(m) == -1) {
        /* it is probably not the right time to forward anymore */
        return (do_fin0_fwd(m));
    }
    /* read the connect script. Lines starting with a dot will be sent
     * to the remote BBS.
     */
    Continue[0] = '\0';

    while (!go_on && (cp=fgets(m->line,MBXLINE,m->tfile)) != NULLCHAR)
	{
        /* Expanded to do timeouts, and return string recognition - WG7J */
        switch((op=*m->line))
		{
            case '.':               /* send this line */
                j2tputs(m->line + 1);
                if(Mtrace)
                    PRINTF("fwd: %s > %s\n",m->name,m->line+1);
                Continue[0] = '\0';         /* reset reply string */
                break;
            case '#':               /* comment line, ignore */
                break;
            case '!':               /* forego FBBCMP if offered */
#ifdef FBBCMP
                m->sid |= MBX_FOREGO_FBBCMP;
#endif
                break;
            case '+':               /* string that must match to continue */
                strcpy(Continue,m->line+1);
                rip(Continue);              /* get rid of \n */
                break;
            case '&':               /* Wait a certain number of seconds */
                timeout = atoi(m->line+1);	/* alarm bell */
                j2pause(timeout * 1000);
                break;
            case '*':               /* read many lines with timeout, comparing each */
                if (! *Continue) {
                    if(Mtrace)
                        PRINTF("fwd: %s: script err, * without + (ignored)\n",m->name);
                    break;
                }
                /* Fall into '@' case */
            case '@':               /* read 1 line with timeout, then compare */
                timeout = atoi(m->line+1);	/* alarm bell */
                if(timeout)                 /* if a valid conversion */
                    timeout *= 1000;        /* in ms ! */
                else
                    timeout = 90*1000;     /* default to 1.5 minutes */

				while (1)	/* 01Dec2004, Maiko, replaces 'reread:' LABEL */
				{
                	if(Mtrace)
					{
						/* alarm bell */
                    	PRINTF("fwd: %s, wait %d < %s\n",
							m->name,timeout/1000,Continue);
					}

            	/* Now do the actual response interpretations */
               		j2alarm(timeout);
               		rval = recvline(m->user,m->line,MBXLINE);
               		j2alarm(0);

#ifdef	J2_SCRIPT_VIA_FORWARD_FILE
					if (script && op == '*')
					{
						if (rval < 0)
							break;

						if (script_fp)
							fputs (m->line, script_fp);

						continue;
					}
#endif
            	/* Did we timeout, or connection disappear ? */
                	if(Mtrace)
					{
                    	PRINTF("fwd: %s, rx %d",m->name,rval);
                    	if(rval >= 0)
                        	PRINTF(", %s",m->line);
                    	else
                        	PUTCHAR('\n');
                	}

				/* more debugging 07Nov2013 Maiko */
					if (Mtrace)
					{
						log (-1, "Continue [%s] m->line [%s] rval %d",
							Continue, m->line, rval);
					}

					/*
					 * 07Nov2013, Maiko, I need to change how this works, some
 					 * systems like Xnet will give you a > prompt, but problem
					 * is there is no Newline, so recvline() above will timeout
					 * regardless of the timeout value, it'll never get an NL,
					 * so a timeout is not a bad thing IF m->line contains any
					 * content, which it will, so only abort if rval < 0 AND if
					 * recvline() returns nothing (null string).
					 *
					 * 01Dec2020, Maiko, Correction, JNOS does the same damn thing,
					 * the reason I put in the code below is because of cases where
					 * you get a 'login: ', or 'Password: ', or 'Auth: ', note they
					 * all STOP at a dangling space, there is NO carriage return,so
					 * one HAS TO depend on the timeout mechanism to get to the next
					 * step in the process.
					 *
					 * Please refer to my latest forward.bbs example which you have
					 * to use to telnet forward to another 2.0m.5E or later JNOS with
					 * the Multi Factor Authentication (J2MFA) feature compiled in.
					 *
					 */

					if ((rval < 0) && !(*(m->line)))
					{
						/* 07Nov2013, Maiko, People should know if forward.bbs
						 * script aborted, there is no indication in the logs,
						 * unless people have 'mbox trace on', so perhaps note
						 * this in the log, and suggestion trace on option !
						 */
						log (m->user, "forward.bbs (%s) aborted, use 'mbox trace on' to debug", m->name);  

                    	if(Mtrace)
                        	PRINTF("fwd: %s, aborted!\n",m->name);

        				return (do_fin0_fwd(m));
					}

                	if (strstr (m->line, Continue) == NULLCHAR)
					{
						/* 01Dec2004, Maiko, 'continue' replaces 'goto' */
                    	if (op=='*') continue;	/* nomatch; retry */

						/* 07Nov2013, Maiko, People should know if forward.bbs
						 * script aborted, there is no indication in the logs,
						 * unless people have 'mbox trace on', so perhaps note
						 * this in the log, and suggestion trace on option !
						 */
						log (m->user, "forward.bbs (%s) aborted, use 'mbox trace on' to debug", m->name);  

                    	if(Mtrace)
                        	PRINTF("fwd: %s, aborted!\n",m->name);

        				return (do_fin0_fwd(m));
                	}

                	Continue[0] = '\0';         /* reset reply string */

					break;	/* force break out of while loop */
				}
                break;

            default:        /* must be the end of the script */
				go_on = 1;
				break;
        }

 		if (!go_on) 
        	usflush(m->user);   /* send it, if any */
    }

    /* Now we've past all in-between stuff, go talk to the bbs ! */
    usflush(m->user);
    fclose(m->tfile);
    m->tfile = NULLFILE;
  
    /* make sure there is something left ! */
    if(cp == NULLCHAR) {
        if(Mtrace)
            PUTS("fwd: forward.bbs error!");
		return (do_fin_fwd (m, Continue));
    }
    if(Mtrace)
        PRINTF("fwd: %s, script done\n",m->name);

#ifdef	J2_SCRIPT_VIA_FORWARD_FILE
/* 06Jul2021, Maiko (VE4KLM), using forward.bbs mechanism to script remote commands */
	if (script)
	{
		log (m->user, "just running commands, no forwarding");
		exitfwd (m);
		if (script_fp)
			fclose (script_fp);
        	return;
	}
#endif
  
#ifdef FWDFILE
    if (m->family == AF_FILE) {   /* forwarding to a file? */
        dorevfwd(0,NULL,(void *)m);
#ifdef FWD_COMPLETION_CMD
		return (do_fin_fwd (m, Continue));
#else
        exitfwd(m);
        if (Continue[0]) {  /* Run end-of-fwd cmd, if any */
            Curproc->output = Command->output;   /* so tprintf works */
            usesock(Curproc->output);
#ifdef REDIRECT
            Curproc->input = Command->input;   /* so redirection works */
            usesock(Curproc->input);
#endif
            cmdparse(Cmds, Continue, NULL);
        }
        return;
#endif /* FWD_COMPLETION_CMD */
    }
#endif /* FWDFILE */

    /* read the initial output from the bbs, looking for the SID */

    /* log (-1, "waiting for SID"); */

    for(;;)
	{
        if(recvline(m->user,m->line,MBXLINE) == -1)
		{
			return (do_fin_fwd (m, Continue));
        }

		/* log (-1, "got [%s]", m->line); */

        if(ISPROMPT(m->line))
            break;

        if(*m->line == '[')	/* parse the SID */
		{
            rip(m->line);
            mbx_parse(m);
            continue;
        }

#ifdef	WINLINK_SECURE_LOGIN

/*
 * 12Oct2019, See comments in config.h.default (config.h) and lzhuf.c !!!
 *
 * B2F is now a permanent define, we now use the new WINLINK_SECURE_LOGIN
 * definition in config.h if we want to use the feature, which necessitates
 * installation of the openssl development packages in order to compile.
 *
 */
		/* 21Oct2015, Maiko (VE4KLM), Time to support Winlink Secure Login */
			if (strnicmp (m->line, ";PQ: ", 5) == 0)
			{
				extern char *J2ChallengeResponse (char*, char*);

				extern char *j2userpasswd (int, char*);

#ifdef	DONT_COMPILE	/* 20June2020, Maiko (VE4KLM), now configured with calls list code */

				// extern char *Wl2kcall; /* 10Dec2015, VE4KLM, arrggggg crash */

				extern char Wl2kcall[];	/* one of the niceties of 'C', NOT */
#endif

		    rip(m->line); /* 22Oct2015, Maiko, newline will mess up hash ! */

#ifdef	DONT_COMPILE	/* 20June2020, Maiko (VE4KLM), now configured with calls list code */

				/* 09Dec2015, Maiko (VE4KLM), use a new dedicated winlink call */
			pax25 (MyCall, Wl2kcall);

			/* 02Nov2015, Maiko (VE4KLM), strip off any SSID */
			mcptr = MyCall;
			while (*mcptr && *mcptr != '-') mcptr++;
			*mcptr=0;
#endif

#ifdef	WINLINK_SECURE_LOGIN
/* 03Dec2020, Maiko, MULTIPLE_WL2K_CALLS removed from makefile, changed to WINLINK_SECURE_LOGIN */
			/*
			 * 04Jun2020, Maiko (VE4KLM), instead of duplicating code, move the
			 * stuff below into a function, which I'll need later on as well !
			 *
			 * Pass logging socket, the callsign, and PQ Challenge code.
			 *
			 */

			PQchallenge = j2strdup (m->line+5);		/* need this for the ;FW line */

			if ((crstr = wl2k_build_response (m->user, wl2kmycall () /* MyCall */, PQchallenge)) == (char*)0)
			{
				free (PQchallenge);	/* Need to free this up when done with it */

				return (do_fin_fwd (m, Continue));
			}
#else
			log (m->user, "[%s] requesting secure login", wl2kmycall () /* MyCall */);

			/* 26Feb2020, Maiko, Error check was missing, need to have this !!! */
			if ((mcptr = j2userpasswd (1, wl2kmycall () /* MyCall */)) == (char*)0)
			{
				log (m->user, "error - did you create a Winlink CMS user ?");

				return (do_fin_fwd (m, Continue));
			}

			/* 22Oct2015, Maiko (VE4KLM), build response, send after my SID */
			crstr = J2ChallengeResponse (m->line + 5, mcptr);

			free (mcptr);	/* 26Feb2020, Maiko, Need to free this up when done with it */
#endif
			continue;
		}
#endif
    }
    /* Now sync the two ends as telnet password messes them up */
    if(socklen(m->user,0))          /* discard any remaining input */
        recv_mbuf(m->user,NULL,0,NULLCHAR,0);

    /* send our SID if the peer announced its SID */

#ifdef FBBFWD

#ifdef	B2F
	/* 25Mar2008, Maiko (VE4KLM), Only send B2F in SID if remote supports */
    if (m->sid2 & MBX_B2F)
	{
#ifdef	WINLINK_SECURE_LOGIN
	/* 22Oct2015, Maiko (VE4KLM), Send challenge response if we got asked */
		if (crstr)
		{
			/* 11May2016, Maiko (VE4KLM), Oops FW should not be hardcoded */
			tprintf (";FW: %s", wl2kmycall () /* MyCall */);

			/*
			 * 04Jun2020, Maiko (VE4KLM), request download of multiple mailboxes. I get
			 * a little bit elegant and reuse some of the code I wrote 18 years ago for
			 * the APRS callsign lists, and pass a pointer to our build function :]
			 */
			callsokay = wl2kmulticalls (wl2k_build_response, m->user, PQchallenge);

			free (PQchallenge);	/* Need to free this up when done with it */

			if (!callsokay)
				return (do_fin_fwd (m, Continue));

			tprintf ("\n");
		}
#endif
       j2tputs(MboxIdB2F);

#ifdef	WINLINK_SECURE_LOGIN
	/* 22Oct2015, Maiko (VE4KLM), Send challenge response if we got asked */
		if (crstr)
		{
			/* log (-1, "sending challenge response"); */
			tprintf (";PR: %s\n", crstr);
		}
#endif
      	usflush(m->user);
	}
    else
#endif
#ifdef FBBCMP
	/* 28Aug2013, Maiko (VE4KLM), I don't know why we never supported B1F */
    if (m->sid & MBX_FBBCMP1)
       j2tputs (MboxIdB1F);
	else if (m->sid & MBX_FBBCMP)
       j2tputs (MboxIdFC);
    else
#endif
    if(m->sid & MBX_FBBFWD) {
       // All we do is send a SID and start forwarding.
       // The remote box doesn't send any OK prompts.
       j2tputs(MboxIdF);
    } else
#endif
    if(m->sid & MBX_SID)
	{
        j2tputs(MboxId);

        usflush(m->user);

   		if(Mtrace)
			log (-1, "waiting for prompt from remote bbs");

        for(;;) {
            if(recvline(m->user,m->line,MBXLINE) == -1) {
				return (do_fin_fwd (m, Continue));
            }
            if(ISPROMPT(m->line))
                break;
        }
    }

    /* start the actual forwarding */

	// log (-1, "start actual forwarding");

#ifdef FBBFWD
    if(m->sid & MBX_FBBFWD) {
       if (dofbbfwd(0,NULL,(void *)m) < 0)   /* -1 is startup err, -2 is FQ rec'd or fatal read err */
			return (do_fin_fwd (m, Continue));
       else return (do_fin2_fwd (Continue)); /* exitfwd() call already given */
    }
#endif

//	log (-1, "start reverse forwarding");

    dorevfwd(0,NULL,(void *)m);

    /* ask for reverse forwarding or just disconnect */
    if((m->sid & MBX_SID) && j2tputs("F>\n") != -1 ) {
        usflush(m->user);
        /* parse the commands that are are received during reverse
         * forwarding.
         */
        while(recvline(m->user,m->line,MBXLINE) > 0) {
            rip(m->line);
            if(mbx_parse(m) < 0)   /* -2 => "*** Done" rec'd, -1 => cmd not in Mbcmds, or other err */
                break;
            j2tputs("F>\n");
            usflush(m->user);
        }
    }

	return (do_fin_fwd (m, Continue));
}
  
/* open a network connection based upon information in the cc line.
 * m->user is set to the socket number.
 */
static int
openconn(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
    char sock[MAXSOCKSIZE], alias[AXBUF];
    struct nrroute_tab *rp;
    union sp sp;
    int len, narg;
    char *remote;

#ifdef  EA5HVK_VARA
   extern int vara_connect (char *src, char *dst);     /* access mode function (vara.c) */
#endif

/* 12Jun2016, Maiko (VE4KLM), Default to original JNOS functionality */
#ifdef	JNOS_DEFAULT_NOIAC
/* #warning "default NO IAC processing in affect" */
	struct usock *up;
#endif
  
    m = (struct mbx *)p;
    sp.p = sock;
    if(argc < 2) {
        if(Mtrace)
            j2tputs("fwd: connect command error\n");
        return -1;
    }
    remote = argv[1];
    switch(*argv[0])
    {
#ifdef  EA5HVK_VARA
       /*
        * 01Sep2021, Maiko, the vara connect should be in this function, and
        * we should pull both source and destination calls from forward.bbs,
        * and making sure the proper number of arguments actually exists.
        */
        case 'v':
             if (argc < 3)
             {
                     log (-1, "syntax error in forward.bbs - use 'vara src dst'");
                     return -1;
             }
             m->user = vara_connect (argv[1], argv[2]);
             return m->user; /* immediately exit !!! socket comes from external connect */
             break;
#endif
        case 't':

#ifdef IPV6
	/*
	 * 26Mar2023, Maiko, Allow forwarding with IPV6 systems
	 * 14Apr2023, Maiko, Get rid of '6' option, now that we have new
	 * resolve6() function, do this like in telnet and ping code, try
	 * ipv6 first, then go back to ipv4, should make that an option.
	 */
		/* 14Apr2023, Maiko, Now using new resolve6() in domain.c */

			/*
			 * 13Apr2023, Maiko, wrote new resolve6() function in domain.c, so
			 * let's try and use this instead of the previous code
			 */
			copyipv6addr (resolve6 (argv[1]), sp.in6->sin6_addr.s6_addr);

			if (sp.in6->sin6_addr.s6_addr[0] != 0x00)	/* go to ipv4 if this is 0x00 */
			{
           		m->family = sp.in6->sin6_family = AF_INET6;

            	len = sizeof(*sp.in6);
			}
			else
			{
#endif
            if((sp.in->sin_addr.s_addr = resolve(argv[1])) == 0) 
			{
                if(Mtrace)
                    tprintf("fwd: telnet - unknown host %s\n",argv[1]);
                return -1;
            }

            m->family = sp.in->sin_family = AF_INET;

            len = sizeof(*sp.in);
#ifdef	IPV6
		}
#endif

        /* get the optional port number */
            if(argc > 2)
                sp.in->sin_port = atoip(argv[2]);
            else
                sp.in->sin_port = IPPORT_TELNET;

            if((m->user = j2socket(m->family, SOCK_STREAM,0)) == -1)
			{
                if(Mtrace)
                    j2tputs("fwd: unable to open telnet socket\n");
                return -1;
            }

/* 12Jun2016, Maiko (VE4KLM), Default to original JNOS functionality */
#ifdef	JNOS_DEFAULT_NOIAC
			if ((up = itop (m->user)) == NULLUSOCK)
			{
				log (-1, "telnet (itop failed) unable to set options");
				break;
			}
			up->flag |= SOCK_NO_IAC;
#endif
		/*
		 * 18Apr2016, Maiko (VE4KLM), rewrote this entire parsing section so
		 * options can now be passed by themselves and not dependant on other
		 * options having been specified first (ie, the noiac option).
		 */
			if (argc > 3)
			{
/*
 * 12Jun2016, Maiko (VE4KLM), Move the code here if we decide against the
 * default original JNOS functionality.
 */
#ifndef	JNOS_DEFAULT_NOIAC
				struct usock *up;

				if ((up = itop (m->user)) == NULLUSOCK)
				{
					log (-1, "telnet (itop failed) unable to set options");
					break;
				}
#endif
				for (narg = 3; narg < argc; narg++)
				{
					// log (-1, "processing arg [%s]", argv[narg]);

					/* just CR */
					if (!stricmp(argv[narg],"cronly"))
                    		strcpy (up->eol, "\r");

					/* 14Apr2016, Maiko (VE4KLM), new NO IAC option (winlink) */
            		if (!stricmp(argv[narg],"noiac"))
						up->flag |= SOCK_NO_IAC;
				/*
				 * 12Jun2016, Maiko (VE4KLM), allow disabling of the flag, in
				 * case the sysop opts to preserve JNOS default behavior, which
				 * I think should be the case anyways. Hopefully these options
				 * will make both camps happy ...
				 */
            		if (!stricmp(argv[narg],"iac"))
						up->flag &= ~SOCK_NO_IAC;
				}
			}
            break;

#ifdef AX25
        case 'a':
        case 'c':       /* allow 'c' for 'connect' as well */
            if(argc < 3) {
                if(Mtrace)
                    tprintf("fwd: connect syntax error - %s %s ?\n",argv[0],argv[1]);
                return -1;
            }
            sp.ax->sax_family = AF_AX25;
#ifdef JNOS20_SOCKADDR_AX
		/* 30Aug2010, Maiko, New way to reference interface information */
			memset (sp.ax->filler, 0, ILEN);
			sp.ax->iface_index = if_indexbyname (argv[1]);
#else
            strncpy(sp.ax->iface,argv[1],ILEN); /* the interface name */
#endif
            setcall(sp.ax->ax25_addr,argv[2]); /* the remote callsign */

            if(argc > 3)  /* process digipeater string; do auto-ax25-route-add */
                if(connect_filt(argc,argv,sp.ax->ax25_addr,if_lookup(argv[1])) == 0)
                    return -1;

            if((m->user = j2socket(AF_AX25,SOCK_STREAM,0)) == -1) {
                if(Mtrace)
                    j2tputs("fwd: Unable to open ax25 socket\n");
                return -1;
            }
            len = sizeof(*sp.ax);
            m->family = AF_AX25; /*So the user list will be correct! - WG7J */
            remote = argv[2];
            break;
#endif /* AX25 */
#ifdef NETROM
        case 'n':
    /* See if the requested destination could be an alias, and
     * use it if it is.  Otherwise assume it is an AX.25
     * address.
     */
            putalias(alias,argv[1],0);
            strupr(argv[1]);
            if ((rp = find_nrboth(alias,argv[1])) == NULLNRRTAB)  {
                if(Mtrace)
                    tprintf("fwd: Netrom route unavailable - %s\n",argv[1]);
                return -1;
            }
    /* Setup the local side of the connection */
            sp.nr->nr_family = AF_NETROM;
            len = sizeof(*sp.nr);
            if((m->user = j2socket(AF_NETROM,SOCK_SEQPACKET,0)) == -1) {
                if(Mtrace)
                    tprintf("fwd: unable to open netrom socket - %s\n",argv[1]);
  
                return -1;
            }
            memcpy(sp.nr->nr_addr.user,Nr4user,AXALEN);
            memcpy(sp.nr->nr_addr.node,Nr_iface->hwaddr,AXALEN);
            j2bind(m->user,sp.p,len);
  
    /* Now the remote side */
            memcpy(sp.nr->nr_addr.node,rp->call,AXALEN) ;
    /* The user callsign of the remote station is never
         * used by NET/ROM, but it is needed for the psocket() call.
         */
            memcpy(sp.nr->nr_addr.user,rp->call,AXALEN) ;
  
            m->family = AF_NETROM; /*So the user list will be correct! - WG7J */
            break;
#endif /* NETROM */
#ifdef FWDFILE
        case 'f':
            if((m->user = sockfopen(argv[1],APPEND_TEXT)) == -1){
                if(Mtrace)
                    tprintf("fwd: can't open socket to %s\n",argv[1]);
                return -1;
            }
            m->family = AF_FILE; /*So the user list will be correct! - WG7J */
            m->sid = MBX_MID|MBX_HIER_SID;  /* a few useful flags */
            return m->user;  /* we don't need to connect, and mode defaults to ASCII */
            /* break;*/
#endif
        default:
            if(Mtrace)
                tprintf("fwd: Invalid connect mode - %s\n",argv[0]);
  
            return -1;
    }
    sockmode(m->user,SOCK_ASCII);
    if(j2connect(m->user,sp.p,len) == -1) {
        logmbox (m->user, remote, "fwd failed - %s errno %d", sockerr(m->user),errno);
        if(Mtrace)
            tprintf("fwd: Connection failed to %s\n",remote);
        close_s(m->user);
        return -1;
    }
    return m->user;
}
  
#endif /*MBFWD*/
