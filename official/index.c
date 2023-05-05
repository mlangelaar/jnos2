/* Mail Index File routines for JNOS and others.
 * (C) 1993, Johan. K. Reinalda, WG7J/PA3DIS
 * Free for non-commercial use if this notice is retained.
 *
 * You should be able to patch this into you mailer program easily.
 * There are two public functions.
 *
 * int MsgsInMbx(char *name);
 *      this routine returns the number of messages in a mailbox.
 *
 *      Calling convention:
 *      'char *name' is the name of the mailbox, in 'directory format'.
 *      this means that subdirectories should be represented by '/'
 *      characters, and not '.' or '\' . Also, the '.txt' extension
 *      should not be used !
 *
 *      Return value:
 *      It will return -1 if it detects an error in the index file.
 *      Otherwise the number of messages will be returned.
 *
 *      Use:
 *      JNOS uses it to dynamically size the structure that tracks messages.
 *
 * int IndexFile(char *name,int verbose);
 *      This routines creates an index file for the mailbox given.
 *
 *      Calling convention:
 *      'char *name'
 *      'char *name' is the name of the mailbox, in 'directory format'.
 *      this means that subdirectories should be represented by '/'
 *      characters, and not '.' or '\' . Also, the '.txt' extension
 *      should not be used ! The 'mailspool' directory will be pre-pended,
 *      and should not be included in the mailbox name. Eg. the file
 *      '/spool/mail/users/johan.txt' should be indexed as 'users/johan'
 *
 *      'int verbose'
 *      if set, the index will be printed before it is written.
 *
 *      Return values:
 *      0  if no error occured
 *      -2 if it can not open the mailbox for reading binary mode
 *      -3 if it can not open the index file for writing
 *      -10 on other errors
 *
 *      Notes:
 *      -you should make sure the mailbox file is properly locked before
 *       calling this routine.
 *
 *      -you need to provide the following variables:
 *       char *Mailspool, should be "/spool/mail"
 *       char *Arealist, should be "/spool/areas"
 *
 *      -you also need to provide a dummy function pwait() in your sources,
 *       to avoid getting an unknown function error when linking.
 *       void pwait(void *p) {};
 *       should do it...
 */
#ifdef MSDOS
#include <io.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#ifdef MSDOS
#include <dir.h>
#include <dos.h>
#endif
#include "global.h"
#include "socket.h"
#include "index.h"
#include "mailutil.h"
#include "mailbox.h"
#include "smtp.h"
#include "files.h"
#include "bm.h"
#ifdef UNIX
#include "unix.h"
#include "dirutil.h"
#endif
  
#ifdef MAIL2IND
#undef fopen
#define tprintf printf
#define tputc putchar
#endif
  
/* return the number of messages in a given mailbox.
 * name needs to be in directory format.
 * Returns:
 * -1 if index file is corrupt,
 *  otherwize the number of messages in the mailbox
 */
int MsgsInMbx(char *name) {
    int ind;
    struct indexhdr hdr;
    char buf[FILE_PATH_SIZE];
  
    sprintf(buf,"%s/%s.ind",Mailspool,name);
    if((ind = open(buf,READBINARY)) == -1)
        return 0;
  
    /* Read the header */
    if(read_header(ind,&hdr) == -1)
        hdr.msgs = 0;
  
    close(ind);
    return hdr.msgs;
}
  
/* replace terminating end of line marker(s) with null */
void
rip(s)
register char *s;
{
    register char *cp;
  
    if((cp = strpbrk(s,"\r\n")) != NULLCHAR)	/* n5knx: was "\n" */
        *cp = '\0';
}
  
char *skipwhite(char *cp) {
  
    while((*cp != '\0') && (*cp == ' ' || *cp == '\t'))
        cp++;
    return cp;
}
  
char *skipnonwhite(char *cp) {
  
    while(*cp != '\0' && *cp != ' ' && *cp != '\t')
        cp++;
    return cp;
}
  
/* Given a string of the form <user@host>, extract the part inside the
 * angle brackets and return a pointer to it.
 */
char *
getname(cp)
char *cp;
{
    char *cp1;
  
    if ((cp = strchr(cp,'<')) == NULLCHAR)
        return NULLCHAR;
    cp++;   /* cp -> first char of name */
    cp = skipwhite(cp);
    if((cp1 = strchr(cp,'>')) == NULLCHAR)
        return NULLCHAR;
    *cp1 = '\0';
    return cp;
}
  
/* Parse a string in the "Text: Text <user@host>" or "Text: user@host (Text)"
 * formats for the address user@host.
 */
char *
getaddress(string,cont)
char *string;
int cont;       /* true if string is a continued header line */
{
    char *cp, *ap = NULLCHAR;
    int par = 0;

#ifdef IND_DEBUG
    log (-1, "string [%s] continued %d", string, cont);
#endif

    if((cp = getname(string)) != NULLCHAR) /* Look for <> style address */
        return cp;
  
    cp = string;
    if(!cont)
	{
        if((cp = strchr(string,':')) == NULLCHAR)   /* Skip the token */
            return NULLCHAR;
        else
            ++cp;
	}
    for(; *cp != '\0'; ++cp)
	{
        if(par && *cp == ')') {
            --par;
            continue;
        }
        if(*cp == '(')      /* Ignore text within parenthesis */
            ++par;
        if(par)
            continue;
        if(*cp == ' ' || *cp == '\t' || *cp == '\n' || *cp == ',') {
            if(ap != NULLCHAR) {
                *cp='\0';  /* end from-string early */
                break;
            }
            continue;
        }
        if(ap == NULLCHAR)
            ap = cp;
    }

#ifdef IND_DEBUG
    log (-1, "ap [%s]", ap);
#endif

    return ap;
}
  
char *Months[12] = { "Jan","Feb","Mar","Apr","May","Jun",
"Jul","Aug","Sep","Oct","Nov","Dec" };
  
char *Hdrs[] = {
    "Approved: ",
    "From: ",
    "To: ",
    "Date: ",
    "Message-Id: ",
    "Subject: ",
    "Received: ",
    "Sender: ",
    "Reply-To: ",
    "Status: ",
    "X-BBS-Msg-Type: ",
    "X-Forwarded-To: ",
    "Cc: ",
    "Return-Receipt-To: ",
    "Apparently-To: ",
    "Errors-To: ",
    "Organization: ",
    "Newsgroups: ",
    "Path: ",
    "X-BBS-Hold: ",
    NULLCHAR,
};
  
  
/* return the header type, taking into account the type of the previous header */
int
htype(s, prevtype)
char *s;
int *prevtype;
{
    register char *p;
    register int i;
  
    p = s;
    /* check to see if there is a ':' before any white space */
    while (*p > ' ' && *p < '\177' && *p != ':')
        p++;
    if (*p != ':' || p==s)  { /* continuation line, or illegal syntax */
       if (*s == ' ' || *s == '\t') return (*prevtype);  /* continuation */
       *prevtype = NOHEADER;
       return NOHEADER;
    }  
    for (i = 0; Hdrs[i] != NULLCHAR; i++) {
        if (strnicmp(Hdrs[i],s,strlen(Hdrs[i])-1) == 0) /* don't require a SP after colon */
            break;
    }
    *prevtype = i;  /* == UNKNOWN if not in Hdrs[] */
    return i;
}
  
int get_index(int msg,char *name,struct mailindex *ind) {
    int idx;
    struct indexhdr hdr;
    char buf[FILE_PATH_SIZE];
  
    sprintf(buf,"%s/%s.ind",Mailspool,name);
    if((idx=open(buf,READBINARY)) == -1) {
        return -1;
    }
  
    if((read_header(idx,&hdr) == 0) && (msg <= hdr.msgs)) {
        while(msg--) {
            default_index(name,ind);
            read_index(idx,ind);
        }
        close(idx);
        return 0;
    }
    close(idx);
    return -1;
}
  
/* set_index() will parse an input SMTP-style header line and update the
   corresponding field in the mailindex.  While continued header lines are
   recognized, they aren't always processed properly, due to insufficient
   state retained between calls to set_index.  -- n5knx
*/
void set_index(char *buf,struct mailindex *index, int hdrtype) {
    int continued;
    char *s,*cp;
    struct cclist *cc;
    struct fwdbbs *bbs;
  
    rip(buf);
    continued = (*buf == ' ' || *buf == '\t');
    if (continued) buf = skipwhite(buf);

#ifdef DONT_COMPILE
	{
		extern int IsUTF8 ();

		char *newbuf = j2strdup (buf);

		if (*newbuf && IsUTF8 (newbuf))
		{
			log (-1, "detected utf-8 formatting");
			strcpy (buf, newbuf);
			free (newbuf);
		}
	}
#endif

    if (*buf) switch(hdrtype) {
        case APPARTO:
            if(index->to)
                break;
        /* Notice fall-through */
        case TO:
            free(index->to);
            index->to = j2strdup(getaddress(buf,continued));

#ifdef MOVE_THIS_INTO_SETINDEX

/*
 * 10Aug2014, Maiko (VE4KLM), following code used to be in smtpserv.c, but I've
 * moved it into here because index rebuild wasn't consistent with the original
 * processing done when the message came in via smtp server. This showed up in
 * the CCLIST issues (ie, the LA command) during tests done by N6MEF (Fox), so
 * hopefully this is it, and the entire thing is fixed once and for all.
 */
            if ((cp=strchr(index->to,'@')) != NULLCHAR &&
					!stricmp(cp+1,Hostname))
            {
                *cp='\0'; /* but strip @our_hostname */

#ifdef IND_DEBUG
                log (-1, "smtpserv strip TO [%s]", index->to);
#endif

                if ((cp = strrchr(index->to,'%')) != NULLCHAR)
					*cp = '@';

#ifdef IND_DEBUG
				log (-1, "smtpserv add @ ? [%s]", index->to);
#endif
            }
/*
 * 10Aug2014, End of moved code
 */
#endif
            break;
        case STATUS:          /* Jnos-defined */
            if(buf[8] == 'R')
                index->status |= BM_READ;
            break;
        case XBBSHOLD:
            index->status |= BM_HOLD;
            break;
        case FROM:
            free(index->from);
            index->from = j2strdup(getaddress(buf,continued));
            break;
        case REPLYTO:
            free(index->replyto);
            index->replyto = j2strdup(getaddress(buf,continued));
            break;
        case SUBJECT:
            if (continued)
			{
                s = mallocw(strlen(index->subject) + strlen(buf) + 2);
                strcpy(s, index->subject);
                strcat(s, " ");
                strcat(s, buf);
                free(index->subject);
                index->subject = s;
            }
            else
			{
                free(index->subject);
                buf = skipwhite(buf+8);
			/*
			 * 20Jul2014, Maiko (VE4KLM), ugly bug stumbled across by Fox, his
			 * index rebuild would crash cause there was a misformed 'Subject:'
			 * header entry immediately terminated with a CR nothing else, so
			 * after stripping (rip function), this would result in 0 len fld,
			 * and so the following j2strdup would not occur, and when it was
			 * 'continued' in the code above, strlen (null) would go BOOM !!!
			 *
			 * Simple solution, always initial index->subject, even if 0 len.
			 *
                if (*buf) index->subject = j2strdup(buf);
			 */

#ifdef DONT_COMPILE
           /* 26Jul2014, Maiko, This warning is now annoying and not useful */

				if (!(*buf))
					log (-1, "developer warning - malformed subject ???");
#endif

                index->subject = j2strdup(buf);
            }
            break;
        case BBSTYPE:
            index->type = buf[16];  /* Jnos-defined */
            break;
        case MSGID:
            free(index->messageid);
            index->messageid = j2strdup(getaddress(buf,continued));
            break;
        case XFORWARD:              /* Jnos-defined */
            bbs = mallocw(sizeof(struct fwdbbs));
            strcpy(bbs->call,&buf[16]);
            bbs->next = index->bbslist;
            index->bbslist = bbs;
            break;
        case CC:
            if (!continued) s = skipwhite(buf+3);
            else s=buf;
            while(*s) {
                if((cp=strchr(s,','))!=NULL){
                    *cp = '\0';
                } else
                    cp = s + strlen(s) - 1;
                buf=getaddress(s, 1);
                if (buf && *buf) {  /* ignore blank fields */
                    cc = mallocw(sizeof(struct cclist));
                    cc->to = j2strdup(buf);
                    cc->next = index->cclist;
                    index->cclist = cc;
                }
                s = cp + 1;
            }
            break;
        case DATE:
        /* find age from ARPA style date */
            if (!continued) buf = skipwhite(buf+5);
            index->date = mydate(buf);
            break;
    } /* switch */
}
  
/* Update the index file */
int WriteIndex(int idx, struct mailindex *ind) {
    long startoffset;
    int len, xlen;
    struct indexhdr hdr;
    struct fwdbbs *bbs;
    struct cclist *cc;
    char null = '\0';
  
    if ((startoffset = lseek(idx,0,SEEK_CUR)) == -1L) return -1;
  
    /* write length bytes */
    write(idx,&len,(size_t)sizeof(len));
  
    /* write message id */
    write(idx,&ind->msgid,(size_t)sizeof(ind->msgid));
    xlen = sizeof(ind->msgid);
  
    /* write message type */
    write(idx,&ind->type,(size_t)sizeof(ind->type));
    xlen += sizeof(ind->type);
  
    /* write message status */
    write(idx,&ind->status,(size_t)sizeof(ind->status));
    xlen += sizeof(ind->status);
  
    /* write message size */
    write(idx,&ind->size,(size_t)sizeof(ind->size));
    xlen += sizeof(ind->size);
 
#ifdef IND_DEBUG
	log (-1, "TO (%d) [%s]", strlen(ind->to), ind->to);
#endif
 
    /* write to-address */
    if(ind->to) {
        write(idx,ind->to,(size_t)strlen(ind->to));
        xlen += strlen(ind->to);
    }
    write(idx,&null,(size_t)sizeof(null));
    xlen += sizeof(null);
  
    /* write from-address */
    if(ind->from){
        write(idx,ind->from,(size_t)strlen(ind->from));
        xlen += strlen(ind->from);
    }
    write(idx,&null,(size_t)sizeof(null));
    xlen += sizeof(null);
  
    /* write subject field */
    if(ind->subject){
        write(idx,ind->subject,(size_t)strlen(ind->subject));
        xlen += strlen(ind->subject);
    }
    write(idx,&null,(size_t)sizeof(null));
    xlen += sizeof(null);
  
    /* write reply-to field */
    if(ind->replyto){
        write(idx,ind->replyto,(size_t)strlen(ind->replyto));
        xlen += strlen(ind->replyto);
    }
    write(idx,&null,(size_t)sizeof(null));
    xlen += sizeof(null);
  
    /* write message-id field */
    if(ind->messageid){
        write(idx,ind->messageid,(size_t)strlen(ind->messageid));
        xlen += strlen(ind->messageid);
    }
    write(idx,&null,(size_t)sizeof(null));
    xlen += sizeof(null);
  
    /* write received date */
    write(idx,&ind->mydate,(size_t)sizeof(ind->mydate));
    xlen += sizeof(ind->mydate);
  
    /* write date */
    write(idx,&ind->date,(size_t)sizeof(ind->date));
    xlen += sizeof(ind->date);
  
    /* write all Cc addressees */
    for(cc=ind->cclist;cc;cc=cc->next){
        write(idx,cc->to,(size_t)strlen(cc->to)+1);
        xlen += strlen(cc->to)+1;
    }
    write(idx,&null,(size_t)sizeof(null));
    xlen += sizeof(null);
  
    /* write all forwarded bbs's */
    for(bbs=ind->bbslist;bbs;bbs = bbs->next){
        write(idx,bbs->call,(size_t)strlen(bbs->call)+1);
        xlen += strlen(bbs->call)+1;
    }
    /* terminate with null character */
    write(idx,&null,(size_t)sizeof(null));
    xlen += sizeof(null);
  
    /* Now update the length */
    len = (int) (lseek(idx,0,SEEK_CUR) - startoffset - sizeof(len));
    lseek(idx,startoffset,SEEK_SET);    /* regain start pos */
    if (len != xlen) {  /* serious write or seek error */
        write(idx, &null, 0);    /* truncate index */
        return -1;
    }
        
    if (write(idx,&len,(size_t)sizeof(len)) != (ssize_t)sizeof(len)) return -1;
  
    /* Now update the number of records */
    if (read_header(idx,&hdr) == 0) {
        hdr.msgs++;
        if(!(ind->status & (BM_HOLD+BM_READ)))
            hdr.unread++;
        xlen=write_header(idx,&hdr);
    }
    else xlen = -1;
  
    /* Go to end of file again */
    lseek(idx,0,SEEK_END);
  
    return xlen;
}
  
void default_header(struct indexhdr *hdr) {
    hdr->msgs = 0;
    hdr->unread = 0;
}
  
void default_index(char *name, struct mailindex *ind) {
    struct fwdbbs *cbbs,*nbbs;
    struct cclist *cc,*ncc;
  
    ind->msgid = 0;
  
#ifdef MAILCMDS
#ifdef MAILBOX
    if(isarea(name))
        ind->type = 'B';
    else
#endif
#endif
        ind->type = 'P';
  
    ind->status = 0;
  
    ind->size = 0;
  
    free(ind->to);
    ind->to = NULL;
  
    free(ind->from);
    ind->from = NULL;
  
    free(ind->subject);
    ind->subject = NULL;
  
    free(ind->replyto);
    ind->replyto = NULL;
  
    free(ind->messageid);
    ind->messageid = NULL;
  
    ind->mydate = 0;
    ind->date = 0;
  
    for(cbbs = ind->bbslist;cbbs;cbbs = nbbs) {
        nbbs = cbbs->next;
        free(cbbs);
    }
    ind->bbslist = NULL;
  
    for(cc = ind->cclist;cc;cc = ncc) {
        ncc = cc->next;
        free(cc->to);
        free(cc);
    }
    ind->cclist = NULL;
  
}
  
/* mydate(s) converts date string s, assumed to be in one of these 4 formats:
 * (a) RFC 822 (preferred) - Sun, 06 Nov 1994 08:49:37 GMT
 * (b) RFC 850             - Sunday, 06-Nov-94 08:49:37 GMT
 * (c) NNTP                - 06 Nov 94 08:49:37 GMT
 * (d) as above, but 1-digit day of month.
 *
 * See also ptime() in smtpserv.c, which must be compatible.
 */
long mydate(char *s) {
    struct tm t;
    char *cp;

	/* 12Mar2016, Maiko (VE4KLM) */
	int isthisgmt = 0;
    long time_final;

/* 04Apr2016, Maiko (VE4KLM), no need for this in production */
#ifdef	DEBUG_GMT_AND_INDEX
	time_t curt = 0;
	char *curcp;
#endif

    extern long UtcOffset;     /* UTC - localtime, in secs */  
 
    while(*s == ' ')
        s++;
    /* check to see if there is a "Day, " field */
    if((cp=strchr(s,',')) != NULLCHAR)  {
        /* probably standard ARPA style header */
        cp += 2; /* get past header and DAY field */
    } else {
        /* probably a NNTP style message, that has no
         * "Day, " part in the date line
         */
        cp = s;
    }
  
    while(*cp == ' ')
        cp++;

    /* now we should be at the start of the
     * "14 Apr 92 08:14:32" string
     */
    if(strlen(cp) < 17)
	{
		/* 04Mar2016, Maiko (VE4KLM), never dawned on me to check this */
		log (-1, "incoming [%s] problem ?", cp);
        return 0;
	}

    t.tm_mday = atoi(cp);
    /* (fails with 1-digit mday values): cp += 3; hop to month */
    while (*cp++ != ' ');  /* hop to month */

    for(t.tm_mon=0; t.tm_mon < 12; t.tm_mon++)
        if(strnicmp(Months[t.tm_mon],cp,3) == 0)
            break;
    if(t.tm_mon == 12)
	{
		/* 04Mar2016, Maiko (VE4KLM), never dawned on me to check this */
		log (-1, "mydate (%s) invalid month ?", cp);
        return 0; /* invalid */
	}
    cp += 4;
    t.tm_year = atoi(cp);
    /* Modification by VE4WTS for 4-character year string; N5KNX for 21st century */
    if(t.tm_year>=1900)       /* yyyy assumed OK, we just remove 1900 bias */
        t.tm_year-=1900;
    else if (t.tm_year < 70)  /* yy < 70 => 20yy */
        t.tm_year+=100;
    else if (t.tm_year > 99)  /* yyy (and yyyy < 1900) is invalid */
	{
		/* 04Mar2016, Maiko (VE4KLM), never dawned on me to check this */
		log (-1, "mydate (%s) invalid year ?", cp);
        return 0;
	}
    while(*cp++ != ' ');  /* skip past year digits, to hour digits */

    t.tm_hour=atoi(cp);
    t.tm_min=atoi(cp+3);
    t.tm_sec=atoi(cp+6);
/*
 * 15Nov2015, Maiko (VE4KLM), I think the date written to the index needs to
 * always be in the local timezone, so if the timezone of the incoming message
 * is NOT the same as ours, we need to adjust the date written to the index.
 *
 * I need to stress we are NOT altering the date in the message itself. No, we
 * are simply making sure all messages are indexed to the local timezone ! This
 * came about due to concerns about improper message expiry (future messages).
 *
 *    http://stackoverflow.com/questions/1223947/
 *       time-zone-conversion-c-api-on-linux-anyone
 *
 *    http://www.catb.org/esr/time-programming/
 *
 * Oh my word - what a bloody mess !!!
 *
 * What makes this easier (?) is PBBS forwarding always uses GMT (supposed to).
 *
 */
	/*
 	 * 04Mar2016, Maiko (VE4KLM), Lets get creative in how we can translate
	 * the GMT time which is incoming to our current local time !
	 * 12Mar2016, Maiko (VE4KLM), final version use code from mbxtime ().
	 */
	if (!strncmp ("GMT", cp+9, 3))
	{
/* 04Apr2016, Maiko (VE4KLM), no need for this in production */
#ifdef	DEBUG_GMT_AND_INDEX
    	time (&curt); curcp = ctime (&curt);

		log (-1, "incoming [%s] %02d:%02d:%02d [LOCAL] %9.9s",
			cp + 9, t.tm_hour, t.tm_min, t.tm_sec, curcp + 11);
#endif
		isthisgmt = 1;
	}

    t.tm_isdst=-1;  /* negative => we don't know */

#ifdef DEBUGDATE
    tprintf("mydate: %d %d %d, %d %d %d\n",
            t.tm_mday,t.tm_mon,t.tm_year,t.tm_hour,t.tm_min,t.tm_sec);
#endif

	time_final = mktime (&t);

#ifdef UNIX
	/* Silly problem arises when we convert a 02:nn value on the day DST changes! */
	if (time_final == -1L)
	{
		t.tm_isdst=0;  /* fudge it */
		time_final = mktime(&t);  /* try to do better */
	}
#endif

	// log (-1, "currtime %ld time_final %ld", curt, time_final);
	if (isthisgmt)
	{
/* 04Apr2016, Maiko (VE4KLM), no need for this in production */
#ifdef	DEBUG_GMT_AND_INDEX
		log (-1, "adjusting index to local time");
#endif
    	time_final -= UtcOffset;   /* timezone value is unreliable in BC++3.1 */
	}

	// log (-1, "currtime %ld time_final %ld", curt, time_final);

	return time_final;
}
  
/* Read a file containing messages,
 * and build the index file from scratch from the smtp headers.
 * Parameters:
 *    char *name should be the full area filename in dir format,
 *       without ending '.txt' ! Eg: '/spool/mail/johan'
 *    int verbose ; if set, the index is printed before written
 *                  verbose is only set in mail2ind presently.
 */
int IndexFile(char *name,int verbose)
{
    FILE *fp;
    int ptype=NOHEADER, previous, idx;
    long start, pos;
    struct indexhdr hdr;
    struct mailindex ind;
    char buf[FILE_PATH_SIZE], *cp;

    int validfrom = 0;	/* 29Jun2015, Maiko */

#ifdef IND_DEBUG
	int fromrecs = 0;
#endif
  
    sprintf(buf,"%s/%s.txt",Mailspool,name);
    if((fp = fopen(buf,READ_BINARY)) == NULL)
        return NOMBX;
  
    /* Create new index file */
    sprintf(buf,"%s/%s.ind",Mailspool,name);
    if((idx = open(buf,CREATETRUNCATEBINARY,CREATEMODE)) == -1) {
        fclose(fp);
        return NOIND;
    }

    setvbuf(fp, NULLCHAR, _IOFBF, 2048);       /* N5KNX: use bigger input buffer if possible */

    /* set number of msgs to 0 */
    default_header(&hdr);
    if (write_header(idx,&hdr) == -1)
	{
		fclose(fp);
		close(idx);
		return ERROR;
	}
  
    start = pos = 0;
    previous = 0;
    memset(&ind,0,sizeof(ind));
    default_index(name,&ind);

#ifdef IND_DEBUG
	log (-1, "(re)building index [%s]", buf);
#endif
 
    while(bgets(buf, sizeof(buf), fp) != NULL)
	{
        pwait(NULL);    /* Be nice to others :-) */

        /* search 'From ' line */
        if(!strncmp(buf,"From ",5)) {

#ifdef IND_DEBUG
		log (-1, "found [%s]", buf);
		fromrecs++;
#endif

            /* Start of next message */
            if(previous) {
                /* Write index for previous message */
                ind.size = pos - start;
#ifdef MAIL2IND
                if(verbose)
                    print_index(&ind);
#endif
                if(WriteIndex(idx,&ind) == -1)
				{
                    default_index("",&ind);
                    fclose(fp);
                    close(idx);
                    return ERROR;
                }
            } else
                previous = 1;
  
            /* Clear the index for this message */
            default_index(name,&ind);
            start = pos;
  
            /* Read the 'Received...' and 'ID... lines'
             * to get the msgid - WG7J
             */
            if(bgets(buf, sizeof(buf), fp) == NULL) {   /* "Received " line */
                default_index("",&ind);
                fclose(fp);
                close(idx);
                return ERROR;
            }
            if(bgets(buf, sizeof(buf), fp) == NULL) {   /* 'id' line */
                default_index("",&ind);
                fclose(fp);
                close(idx);
                return ERROR;
            }
            if((cp=strstr(buf,"AA")) != NULL)
                /* what follows is the message-number */
                ind.msgid = atol(cp+2);
            if((cp=strchr(buf,';')) != NULL)
                ind.mydate = mydate(cp+2);
            pos = ftell(fp);

			validfrom = 0;	/* 29Jun2015, Maiko */

            /* While in the SMTP header, set index fields */
            while(bgets(buf,sizeof(buf),fp) != NULLCHAR )
			{
                if (*buf == '\0' && validfrom)
                    break;
				/*
				 * 29Jun2015, Maiko (VE4KLM), We have to also check for DKIM
				 * signature and other odd duck fields in this index rebuild,
				 * only then once we know we've got the FROM field, chances
				 * are it's far past the DKIM-Signature blank lines !
                 */
                if (!strnicmp (buf, Hdrs[FROM],strlen(Hdrs[FROM])))
                    validfrom = 1;

                set_index(buf,&ind,htype(buf, &ptype));
            }
#if defined(TRANSLATEFROM) && !defined(MAIL2IND)
            if(ind.from != NULLCHAR &&
               (cp=rewrite_address(ind.from,REWRITE_FROM)) != NULLCHAR) {
                free(ind.from);
                ind.from=cp;  /* should not have to worry about 'refuse' */
            }
#endif
        }
        pos = ftell(fp);
    }

#ifdef IND_DEBUG
	if (fromrecs)
		log (-1, "%d records indexed", fromrecs);
	else
		log (-1, "Last index had NO records !!!");
#endif

    if(ferror(fp)) {
        default_index("",&ind);
        fclose(fp);
        close(idx);
        return ERROR;
    }
    if(previous) {
        ind.size = pos - start;
#ifdef MAIL2IND
        if(verbose)
            print_index(&ind);
#endif
        if(WriteIndex(idx,&ind) == -1) {/* Write index for previous message */
            default_index("",&ind);
            fclose(fp);
            close(idx);
            return ERROR;
        }
    }
    default_index("",&ind);
    fclose(fp);
    close(idx);
    return 0;
}
  
/* Update the index file */
int write_index(char *name, struct mailindex *ind) {
    int idx, err;
    struct indexhdr hdr;
    char buf[FILE_PATH_SIZE];
  
    sprintf(buf,"%s/%s.ind",Mailspool,name);
    if((idx=open(buf,READWRITEBINARY)) == -1) {
        /* Index file doesn't exist, create it */
        if((idx=open(buf,CREATETRUNCATEBINARY,CREATEMODE)) != -1){
            default_header(&hdr);
            if (write_header(idx,&hdr) == -1) return -1;
        } else
            return -1;
    }
    lseek(idx,0,SEEK_END);
    err=WriteIndex(idx,ind);
    close(idx);
    return err;
}
  
void delete_index(char *filename) {
    char idxfile[FILE_PATH_SIZE];
  
    sprintf(idxfile,"%s/%s.ind",Mailspool,filename);
    unlink(idxfile);
}
  
int write_header(int idx,struct indexhdr *hdr) {
  
    lseek(idx,0,SEEK_SET);
    hdr->version = INDEXVERSION;
    return (write(idx,hdr,(size_t)sizeof(struct indexhdr)) == (ssize_t)sizeof(struct indexhdr) ? 0 : -1);
}
  
int read_header(int idx,struct indexhdr *hdr)
{
    ssize_t val;
  
    lseek(idx,0,SEEK_SET);
    val = read(idx,hdr,(size_t)sizeof(struct indexhdr));
    if(val != (ssize_t)sizeof(struct indexhdr) || hdr->version != INDEXVERSION)
        return -1;
    return 0;
}
  
/* Read an index from the index file. Assumes the file is opened
 * for binary reads, and that the filepointer is pointing to the
 * start of the message to read...
 * Returns 0 if valid, -1 if error
 */
int read_index(int idx,struct mailindex *ind) {
    int len;
    char *buf,*curr;
    struct cclist *newcc;
    struct fwdbbs *newbbs;
  
    /* Get the size of this index entry, ensuring it is reasonable */
    if(read(idx,&len,(size_t)sizeof(len)) != (ssize_t)sizeof(len) || (unsigned)len > 8192 )
        return -1;
  
    /* Now read the index */
    if((buf = mallocw((unsigned)len)) == NULL)
        return -1;
  
    if(read(idx,buf,(size_t)len) != (ssize_t)len) {
        free(buf);
        return -1;
    }
  
    /* And copy all elements to the index */
    curr = buf;
  
    /* Get the message id */
    ind->msgid = *(long *)curr;
    curr += sizeof(ind->msgid);
  
    ind->type = *curr++;
    ind->status = *curr++;
  
    ind->size = *(long*)curr;
    curr += sizeof(ind->size);
  
    ind->to = j2strdup(curr);
    curr += strlen(curr) + 1;
  
    ind->from = j2strdup(curr);
    curr += strlen(curr) + 1;
  
    ind->subject = j2strdup(curr);
    curr += strlen(curr) + 1;
  
    ind->replyto = j2strdup(curr);
    curr += strlen(curr) + 1;
  
    ind->messageid = j2strdup(curr);
    curr += strlen(curr) + 1;
  
    ind->mydate = *(long *)curr;
    curr += sizeof(ind->mydate);
  
    ind->date = *(long *)curr;
    curr += sizeof(ind->date);
  
    while(*curr) {
        newcc = mallocw(sizeof(struct cclist));
        newcc->to = j2strdup(curr);
        newcc->next = ind->cclist;
        ind->cclist = newcc;
        curr += strlen(curr) + 1;
    }
    curr++;
  
    while(*curr) {
        newbbs = mallocw(sizeof(struct fwdbbs));
        strcpy(newbbs->call,curr);
        newbbs->next = ind->bbslist;
        ind->bbslist = newbbs;
        curr += strlen(curr) + 1;
    }

    free(buf);  /* but buf variable retains its value */

    /* As a final check against damaged indices, be sure lengths are consistent */
    if ((++curr - buf) != len) {  /* read garbage, no doubt */
        default_index("", ind);  /* free what we j2strdup'ed */
        return -1;
    }
  
    return 0;
  
};
  
#ifdef MAIL2IND
/* At present print_index() is only invoked by the mail2ind utility via calling
   IndexFile() with the verbose flag set.  We save space by not compiling it
   in unless we have to!  -- n5knx 1.11b
*/
void print_index(struct mailindex *ind) {
    struct fwdbbs *bbs;
    struct cclist *cc;
  
    tprintf("\nSize %ld, ID %ld, flags: %c%c%c\n",ind->size,ind->msgid,ind->type,
    ind->status & BM_READ ? 'R' : 'N',ind->status & BM_HOLD ? 'H' : ' ');
    tprintf("To: %s\n",ind->to);
    tprintf("Cc:");
    for(cc=ind->cclist;cc;cc=cc->next)
        tprintf(" %s",cc->to);
    tputc('\n');
    tprintf("From: %s\n",ind->from);
    tprintf("Received: %s",ctime(&ind->mydate));
    tprintf("Reply-to: %s\n",ind->replyto ? ind->replyto : "");
    tprintf("Subject: %s\n",ind->subject);
    tprintf("Message-Id: %s\n",ind->messageid);
    tprintf("Date: %s",ctime(&ind->date));
    tprintf("X-Forwarded-To:");
    for(bbs=ind->bbslist;bbs;bbs=bbs->next)
        tprintf(" %s",bbs->call);
    tputc('\n');
    tputc('\n');
}
#endif  
  
void dotformat(char *area) {
  
    while(*area) {
        if(*area == '\\' || *area == '/')
            *area = '.';
        area++;
    }
}
  
void dirformat(char *area) {
  
#ifdef UNIX
    if(*area) area++;  /* This allows a leading dot in a pathname, so ./spool works */
#endif
    while(*area) {
        if(*area == '\\' || *area == '.')
            *area = '/';
        area++;
    }
}
  
void firsttoken(char *line) {
  
    while(*line) {
        if(*line == ' ' || *line == '\n' || *line == '\t') {
            *line = '\0';
            break;
        }
        line++;
    }
}
  
/* Returns 1 if name is a public message Area, 0 otherwise */
int
isarea(name)
char *name;
{
    FILE *fp;
    char *area;
    char buf[LINELEN];
  
    if((fp = fopen(Arealist,READ_TEXT)) == NULLFILE)
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
  
/* Get a line from the text file that was opened in binary mode.
 * Delete the CR/LF at the end !
 */
char *bgets(char * buf,int size,FILE *fp) {
    char *cp;
  
    if(fgets(buf,size,fp) == NULL)
        return NULL;
    cp = buf;
    while(*cp) {
        if(*cp == 0xd || *cp == 0xa)
            *cp = '\0';
        cp++;
    }
    return buf;
}
  
#ifdef notdef
void readareas __ARGS((char *name,char *mpath));
int is_area __ARGS((char *name));
int isarea;
  
struct areas {
    struct areas *next;
    char *name;
};
struct areas *Areas = NULL;
  
void readareas(char *name,char *mpath) {
    FILE *fp;
    struct areas *ca,*na;
    char *cp,mp[FILE_PATH_SIZE],buf[FILE_PATH_SIZE];
  
    if((fp=fopen(name,READ_TEXT)) == NULL)
        return;
  
    while(fgets(buf,sizeof(buf),fp) != NULL) {
        if(buf[0] == '#')   /* comment line */
            continue;
        cp = buf;
        /* get first token on line */
        while(*cp) {
            if(*cp == '\n' || *cp == ' ' || *cp == '\t') {
                *cp = '\0';
                break;
            }
            cp++;
        }
        if(buf[0] != '\0') {
            cp = buf;
            while(*cp) {
                if(*cp == '.' || *cp == '\\')
                    *cp = '/';
                cp++;
            }
            sprintf(mp,"%s/%s.txt",mpath,buf);
			/* 22Dec2005, Maiko, Changed malloc() to mallocw() instead ! */
            na = mallocw(sizeof(struct areas));
            na->name = j2strdup(mp);
            na->next = NULL;
            if(Areas == NULL) {
                Areas = na;
            } else {
                ca->next = na;
            }
            ca = na;
        }
    }
    fclose(fp);
}
  
int is_area(char *name) {
    struct areas *a;
  
    a = Areas;
    while(a) {
        if(stricmp(a->name,name) == 0)
            return 1;
        a = a->next;
    }
    return 0;
}
  
void showareas() {
    struct areas *a;
  
    a = Areas;
    while(a) {
        tprintf("%s\n",a->name);
        a = a->next;
    }
}
#endif
  
#ifndef MAIL2IND
  
void UpdateIndex(char *path,int force) {
    char *wildcard,*newpath,*fullname;
    struct ffblk ff,iff;
    int done;
  
	/* 22Dec2005, Maiko, Changed malloc() to mallocw() instead ! */
    if((wildcard = mallocw(3*FILE_PATH_SIZE)) == NULL) {
        return;
    }
    newpath = wildcard+FILE_PATH_SIZE;
    fullname = newpath+FILE_PATH_SIZE;
  
    /* First check all the files */
    if(!path)
        sprintf(wildcard,"%s/*.txt",Mailspool);
    else
        sprintf(wildcard,"%s/%s/*.txt",Mailspool,path);

    done = findfirst(wildcard,&ff,0);

    while(!done){
        if(!path)
            strcpy(fullname,ff.ff_name);
        else
            sprintf(fullname,"%s/%s",path,ff.ff_name);
  
        /* get rid of extension */
        *(fullname+strlen(fullname)-4) = '\0';
        if(force) {
            /* Attempt to lock the mail file! */
            if(!mlock(Mailspool,fullname)) {
                IndexFile(fullname,0);
                rmlock(Mailspool,fullname);
            }
        } else {
            /* Now find the index file */
            sprintf(wildcard,"%s/%s",Mailspool,fullname);
            strcat(wildcard,".ind");
            /* If there is no index, or the index is older then the text file.
             * Go create a new index file !
             */
            if( findfirst(wildcard,&iff,0) != 0 ||
#ifdef UNIX
                (mktime(&ff.ff_ftime) > mktime(&iff.ff_ftime))
#else
                (ff.ff_fdate > iff.ff_fdate) ||
                ((ff.ff_fdate == iff.ff_fdate) && ff.ff_ftime > iff.ff_ftime)
#endif
            ) {
                /* Attempt to lock the mail file! */
                if(!mlock(Mailspool,fullname)) {
                    IndexFile(fullname,0);
                    rmlock(Mailspool,fullname);
                }
            }
#ifdef UNIX
            findlast(&iff);   /* free allocations since we don't call findnext() */
#endif
        }
        done = findnext(&ff);
    }
    /* Now check for sub-directories */
    if(!path)
        sprintf(wildcard,"%s/*.*",Mailspool);
    else
        sprintf(wildcard,"%s/%s/*.*",Mailspool,path);

    done = findfirst(wildcard,&ff,FA_DIREC);

    while(!done){
        if(strcmp(ff.ff_name,".") && strcmp(ff.ff_name,"..")) {
            /* Not the present or 'mother' directory, so create new path,
             * and recurs into it.
             */
            if(!path)
                strcpy(newpath,ff.ff_name);
            else
                sprintf(newpath,"%s/%s",path,ff.ff_name);
            UpdateIndex(newpath,force);
        }
        done = findnext(&ff);
    }
    free(wildcard);
    return;
}
  
/* Make sure the index file associated with the mailbox 'name' is current.
 * The mailbox should be locked before calling SyncIndex.
 * Parameters:
 *    char *name should be the area filename, without ending '.txt' !
 *   Eg: 'johan'
 * Returns:
 *  NOERROR (== 0) if index was current, or has been made current.
 *  others if unable to update the index file.
 */
int SyncIndex(char *name)
{
    int mail,ind;
    struct ffblk mff,iff;
    char *area;
    char buf[FILE_PATH_SIZE];
  
    area = j2strdup(name);    /* ensure mbox name is in dir format */
    dirformat(area);
  
    /* Check the index file */
    sprintf(buf,"%s/%s.txt",Mailspool,area);
    mail = findfirst(buf,&mff,0);
  
    sprintf(buf,"%s/%s.ind",Mailspool,area);
    ind = findfirst(buf,&iff,0);
  
#ifdef UNIX
    if(!mail)
        findlast(&mff);   /* free allocs */
    if(!ind)
        findlast(&iff);   /* free allocs */
#endif

    if(mail != 0 && ind == 0) {
        ind=unlink(buf);    /* remove index file if mbox nonexistent */
    }
    else if(mail == 0) {
        if(ind != 0 ||
#ifdef UNIX
            mktime(&mff.ff_ftime) > mktime(&iff.ff_ftime)
#else
            mff.ff_fdate > iff.ff_fdate ||
            (mff.ff_fdate == iff.ff_fdate && mff.ff_ftime > iff.ff_ftime)
#endif
        ) {
           ind=IndexFile(area,0);
        }
        else ind=NOERROR;
    }
    else ind=NOERROR;
  
    free(area);
    return(ind);
}
  
#endif /* MAIL2IND */
  
