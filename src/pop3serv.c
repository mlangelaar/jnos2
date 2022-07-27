/* POP3 Server state machine - see RFC 1225
 *
 *      Jan 92  Erik Olson olson@phys.washington.edu
 *              Taken from POP2 server code in NOS 910618
 *              Rewritten/converted to POP3
 *      Feb 92  William Allen Simpson
 *              integrated with current work
 *      Aug-Oct 92      Mike Bilow, N1BEE, mikebw@ids.net
 *              Extensive bug fixes; changed uses of Borland stat()
 *              to fsize() in order to fix intermittent crashes;
 *              corrected confusion of sockmode()
 *
 *  "Need-to" list: XTND XMIT (to get WinQVTnet to work)
 *
 *  Support for Mail Index Files, June/July 1993, Johan. K. Reinalda, WG7J
 *  LZW support by Dave Brown, N2RJT.
 *  Reimplement using indices to better advantage, James Dugal, N5KNX 11/95.
 *  Add APOP cmd [RFC1725] (when MD5AUTHENTICATE is defined) - n5knx 10/96.
 */
  
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#ifdef UNIX
#include <sys/types.h>
#endif
#if     defined(__STDC__) || defined(__TURBOC__)
#include <stdarg.h>
#endif
#include <ctype.h>
#include <setjmp.h>
#include "global.h"
#ifdef POP3SERVER
#include "mbuf.h"
#include "cmdparse.h"
#include "socket.h"
#include "proc.h"
#include "files.h"
#include "smtp.h"
#include "dirutil.h"
#include "mailutil.h"
#include "bm.h"
#include "index.h"
#ifdef UNIX
extern int close __ARGS((int));
extern int unlink __ARGS((char *));
extern int lseek __ARGS((int, long, int));
#endif
#ifdef MD5AUTHENTICATE
#include "md5.h"
#endif
#include "commands.h"
#ifdef LZW
#include "lzw.h"

#ifdef MAILCLIENT
extern int poplzw;
#else
static int poplzw = TRUE;   /* no way yet to reset this */
#endif
#endif /* LZW */

#ifndef POP_TIMEOUT
#define POP_TIMEOUT  600  /* seconds (must be >= 600 per RFC1725) */
#endif

static int EolLen;

/* ---------------- common server data structures ---------------- */
/* POP message pointer element */
  
struct pop_msg {
    long len;
    long pos;
    int deleted;
#ifdef	J2_CAPA_UIDL
	/*
	 * A few days earlier, VE4KLM (Maiko), Added CAPA and UIDL support ...
 	 *
	 * 30Jan2017, Confirmed with thunderbird, this seems to work, so we can now
	 * use the 'leave messages on server' option with JNOS, even deleting them,
	 * when deleted on the thunderbird side. At this time using 'msgid' as the
	 * working UIDL value, not sure about that, but all I have to work with :(
	 *
	 * Bizarre - had to restart thunderbird to recognize it's new settings !
	 */
	long msgid;
#endif
};

/* POP server control block */
  
struct pop_scb {
    int socket;             /* socket number for this connection */
    char state;             /* server state */
#define      LSTN       0
#define      AUTH       1
#define      TRANS      2
#define      UPDATE     3
#define      DONE       5
  
    char    locked;         /* true when folder is locked against updates */
    char    folder_modified;/* mail folder contents modified flag */
    char    buf[TLINELEN];  /* input line buffer */
    char    username[64];   /* user/folder name */
    int     folder_len;     /* number of msgs in current folder */
    int     high_num;       /* highest message number accessed */
    FILE    *txt;           /* folder file pointer */
    struct  pop_msg *msg;   /* message database array */
#ifdef MD5AUTHENTICATE
    int32   challenge;
#endif
};

#ifdef STATUSWIN
int PopUsers;
#endif
  
#define NULLSCB  (struct pop_scb *)0
  
/* Response messages -- '\n' is converted to '\r\n' by the socket code */
  
static char DFAR    count_rsp[]     = "+OK you have %d messages\n";
static char DFAR    error_rsp[]     = "-ERR %s\n";
#ifdef MD5AUTHENTICATE
static char DFAR    greeting_msg[]  = "+OK POP3 V10.96 ready <%x@%s>\n";
#else
static char DFAR    greeting_msg[]  = "+OK %s POP3 V11.95 ready\n";
#endif
#if defined(LZW)
static char DFAR    xlzw_rsp[]      = "+OK lzw %d %d\n";
#endif
static char DFAR    user_rsp[]      = "+OK user\n";
static char DFAR    stat_rsp[]      = "+OK %d %ld\n";
static char DFAR    list_single_rsp[]  = "+OK %d %ld\n";
static char DFAR    list_multi_rsp[]   = "+OK %d messages (%ld octets)\n";
static char DFAR    retr_rsp[]      = "+OK %ld octets\n";
static char DFAR    multi_end_rsp[] = ".\n";
static char DFAR    dele_rsp[]      = "+OK message %d deleted\n";
static char DFAR    noop_rsp[]      = "+OK\n";
static char DFAR    last_rsp[]      = "+OK %d\n";
static char DFAR    signoff_msg[]   = "+OK Bye, bye-bye, bye now, goodbye\n";

static struct pop_scb *create_scb __ARGS((void));
static void delete_scb __ARGS((struct pop_scb *scb));
static void popserv __ARGS((int s,void *unused,void *p));
static int poplogin __ARGS((struct pop_scb *scb,char *pass));
static void pop_sm __ARGS((struct pop_scb *scb));
static void state_error(struct pop_scb *,char *);
static void fatal_error(struct pop_scb *,char *);
static void open_folder(struct pop_scb *);
static void close_folder(struct pop_scb *);
static void do_cleanup(struct pop_scb *);
static void stat_message(struct pop_scb *);
#ifdef	J2_CAPA_UIDL
static void list_message (struct pop_scb*, int);
#else
static void list_message(struct pop_scb *);
#endif
static void retr_message(struct pop_scb *);
static void dele_message(struct pop_scb *);
static void noop_message(struct pop_scb *);
static void last_message(struct pop_scb *);
static void rset_message(struct pop_scb *);
static void top_message(struct pop_scb *);
static struct pop_msg *goto_msg(struct pop_scb *,int );
  
  
/* Start up POP receiver service */
int
pop3start(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;
  
    if(argc < 2)
        port = IPPORT_POP3;
    else
        port = atoi(argv[1]);
  
    EolLen = strlen(Eol);  /* smaller code if we store this locally */

    return start_tcp(port,"POP3 Server",popserv,1536);
}
  
/* Shutdown POP3 service (existing connections are allowed to finish) */
  
int
pop3stop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;
  
    if(argc < 2)
        port = IPPORT_POP3;
    else
        port = atoi(argv[1]);
    return stop_tcp(port);
}

#ifdef	MAXTASKS

/*
 * 13May2014, Maiko, A form of rate limiting I suppose. The idea is to restrict
 * the number of spawned processes running simultaneously. This came about due
 * to a recent heavy and sustained POP3 attack on my JNOS system. Originally I
 * wanted to have this in the start_tcp() and i_upcall() functions in socket.c,
 * but one can't track when a proc dies in there, so for now, we'll have to do
 * it specific to each service (ie, pop, telnet, http, ftp, and so on) ...
 */

int popserv_spawned = 0;

/*
 * But I still don't like this, because at this point newproc() will have
 * already created a process, so this is also a bad idea. Go back to the
 * original idea of start_tcp () and i_upcall (). I could create funtion
 * to return the proc count kept in the respective service module and have
 * i_upcall call the function (see immediately below). Yup, that's what I'm
 * going to do now. So start_tcp will need a 'pointer' to this function.
 */

int proccnt_popserv ()
{
	return popserv_spawned;
}

#endif


/* 29Dec2004, New function to replace GOTO 'quit' LABEL */
static void do_quit (struct pop_scb *scb)
{  
    log(scb->socket,"close POP3");
    close_s(scb->socket);
#ifdef STATUSWIN
    PopUsers--;
#endif
#ifdef	MAXTASKS
	popserv_spawned--;
#endif
  
    delete_scb(scb);
}

static void
popserv(s,unused,p)
int s;
void *unused;
void *p;
{
    struct pop_scb *scb;
    char *cp;

#ifdef MD5AUTHENTICATE
	time_t md5seed;	/* 16Nov2009, Maiko, Bridge variable */	
#endif

#ifdef	MAXTASKS

#ifdef	THIS_PART_DONE_IN_START_TCP
	if (popserv_spawned >= 3)
	{
		log (s, "busy POP3 - max procs (3)");
       	close_s (s);
		return;
	}
#endif
	popserv_spawned++;
#endif
 
    sockowner(s,Curproc);           /* We own it now */
    log(s,"open POP3");
#ifdef STATUSWIN
    PopUsers++;
#endif
  
    if((scb = create_scb()) == NULLSCB) {
        j2tputs(Nospace);
        log(s,"close POP3 - no space");
#ifdef STATUSWIN
	PopUsers--;
#endif
#ifdef	MAXTASKS
	popserv_spawned--;
#endif
        close_s(s);
        return;
    }
  
    scb->socket = s;
    scb->state  = AUTH;
  
    sockmode(s,SOCK_ASCII);         /* N1BEE */

#ifdef MD5AUTHENTICATE
    time(&md5seed);
    scb->challenge = (int32)md5seed;
    usprintf(s,greeting_msg,scb->challenge,Hostname);
#else
    usprintf(s,greeting_msg,Hostname);
#endif
 
	while (1)	/* use while loop to replace GOTO 'loop:' */
	{ 

    	j2alarm(POP_TIMEOUT * 1000);
    	if (recvline(s,scb->buf,sizeof(scb->buf)) == -1)
		{
        	/* He closed on us or timer expired */
			break;
    	}
    	j2alarm(0);
  
    	rip(scb->buf);

    	if (!*(scb->buf))      /* Ignore blank cmd lines */
        	continue;
  
    	/* Convert lower, and mixed case commands to UPPER case - Ashok */
    	for(cp = scb->buf;*cp != ' ' && *cp ;cp++)
        	*cp = toupper(*cp);
  
    	pop_sm(scb);

    	if (scb->state == DONE)
			break;
	}	/* end of the while loop that replaces GOTO */  

	return (do_quit (scb));
}
  
/* Create control block, initialize */
  
static struct
pop_scb *create_scb()
{
    register struct pop_scb *scb;
  
    if((scb = (struct pop_scb *)calloc(1,sizeof (struct pop_scb))) == NULLSCB)
        return NULLSCB;
  
    return scb;
}
  
  
/* Free resources, delete control block */
  
static void
delete_scb(scb)
register struct pop_scb *scb;
{
    if (scb == NULLSCB)
        return;
    if (scb->txt != NULL)
        fclose(scb->txt);
    if (scb->locked)
        rmlock(Mailspool,scb->username);
    if (scb->msg  != NULL)
        free(scb->msg);

    free((char *)scb);
}
  
/* --------------------- start of POP server code ------------------------ */
  
/* Command string specifications */
  
static char
user_cmd[] = "USER ",
pass_cmd[] = "PASS ",
#ifdef MD5AUTHENTICATE
apop_cmd[] = "APOP ",
#endif
quit_cmd[] = "QUIT",
stat_cmd[] = "STAT",
list_cmd[] = "LIST",
#if defined(LZW)
xlzw_cmd[] = "XLZW ",
#endif
retr_cmd[] = "RETR",
dele_cmd[] = "DELE",
noop_cmd[] = "NOOP",
rset_cmd[] = "RSET",
top_cmd[]  = "TOP",
#ifdef	J2_CAPA_UIDL
uidl_cmd[] = "UIDL",
capa_cmd[] = "CAPA",
#endif
last_cmd[] = "LAST";         /* LAST is deprecated in RFC1725 */

static void
pop_sm(scb)
struct pop_scb *scb;
{
    char password[41];
#if defined(LZW)
    int lzwmode, lzwbits;
#endif
	int validate_acct = 0;
  
  
#if defined(LZW)
    if (strncmp(scb->buf,xlzw_cmd,strlen(xlzw_cmd)) == 0){
        sscanf(scb->buf,"XLZW %d %d", &lzwbits,&lzwmode);
        if((lzwmode == 0 || lzwmode == 1)
           && (lzwbits > 8 && lzwbits < 17) && poplzw) {
               (void) usprintf(scb->socket,xlzw_rsp,lzwbits,lzwmode);
               lzwinit(scb->socket,lzwbits,lzwmode);
        } else if (!poplzw)
            state_error(scb,"(AUTH) LZW compression disabled");
        else
            state_error(scb,"LZW bits or mode invalid");
        return;
    }
#endif

#ifdef	DEBUG_NOT_FOR_PRODUCTION_SYSTEM
	log (scb->socket, "%d %s", scb->state, scb->buf);
#endif

    switch(scb->state)
	{
        case AUTH:
			validate_acct = 0;
#ifdef J2_CAPA_UIDL
            if (strncmp(scb->buf,capa_cmd,strlen(capa_cmd)) == 0)
			{
				/* we will flush it explicitly */
        		int oldf = j2setflush(scb->socket,-1);

           		usprintf(scb->socket, "+OK List of capabilities follows\n");
           		usprintf(scb->socket, "UIDL\n");
           		usprintf(scb->socket, ".\n");
        		usflush(scb->socket);
        		j2setflush(scb->socket,oldf);
			}
			else
#endif
            if (strncmp(scb->buf,user_cmd,strlen(user_cmd)) == 0)
			{
                sscanf(scb->buf,"USER %s",scb->username);
                (void) usputs(scb->socket,user_rsp);
  
#ifdef MD5AUTHENTICATE
            }
			else if (strncmp(scb->buf,apop_cmd,strlen(apop_cmd)) == 0)
			{
                sscanf(scb->buf,"APOP %s %s",scb->username,password);
                validate_acct = 1;
#endif /* MD5AUTHENTICATE */

            }
			else if (strncmp(scb->buf,pass_cmd,strlen(pass_cmd)) == 0)
			{
                sscanf(scb->buf,"PASS %s",password);
				validate_acct = 1;
            }
			else if (strncmp(scb->buf,quit_cmd,strlen(quit_cmd)) == 0)
			{
                do_cleanup(scb);
            }
			else
                state_error(scb,"(AUTH) expected USER, PASS or QUIT");

			if (validate_acct)
			{
				char logstr[72]; /* 01Oct2019, Maiko, compiler format overflow warning, bumped from 70 to 72 (7+64+null) */
				sprintf (logstr, "POP3 (%s)", scb->username);
				if (!poplogin(scb,password))
				{
					strcat (logstr, " DENIED");
                    log (scb->socket, logstr);
                    state_error (scb, "Access DENIED!!");
                    return;
                }
		        log (scb->socket, logstr);
                open_folder(scb);
			}

            break;
  
        case TRANS:
            if (strncmp(scb->buf,stat_cmd,strlen(stat_cmd)) == 0)
                stat_message(scb);
  
#ifdef	J2_CAPA_UIDL
            else if (strncmp(scb->buf,list_cmd,strlen(list_cmd)) == 0)
                list_message(scb, 0);
            else if (strncmp(scb->buf,uidl_cmd,strlen(uidl_cmd)) == 0)
                list_message(scb, 1);
#else
            else if (strncmp(scb->buf,list_cmd,strlen(list_cmd)) == 0)
                list_message(scb);
#endif
            else if (strncmp(scb->buf,retr_cmd,strlen(retr_cmd)) == 0)
                retr_message(scb);
  
            else if (strncmp(scb->buf,dele_cmd,strlen(dele_cmd)) == 0)
                dele_message(scb);
  
            else if (strncmp(scb->buf,noop_cmd,strlen(noop_cmd)) == 0)
                noop_message(scb);
  
            else if (strncmp(scb->buf,last_cmd,strlen(last_cmd)) == 0)
                last_message(scb);
  
            else if (strncmp(scb->buf,top_cmd,strlen(top_cmd)) == 0)
                top_message(scb);
  
            else if (strncmp(scb->buf,rset_cmd,strlen(rset_cmd)) == 0)
                rset_message(scb);
  
            else if (strncmp(scb->buf,quit_cmd,strlen(quit_cmd)) == 0)
                do_cleanup(scb);
  
            else
                state_error(scb,"(TRANS) unsupported/unrecognized command");
            break;
  
        case DONE:
            break;
  
        default:
            fatal_error(scb,"(TOP) State Error!!");
            break;
    }
}
  
static void
do_cleanup(scb)
struct pop_scb *scb;
{
    close_folder(scb);
    (void) usputs(scb->socket,signoff_msg);
    scb->state = DONE;
}
  
static void
state_error(scb,msg)
struct pop_scb *scb;
char *msg;
{
    (void) usprintf(scb->socket,error_rsp,msg);
    /* scb->state = DONE; */  /* Don't automatically hang up */
}
  
static void
fatal_error(scb,msg)
struct pop_scb *scb;
char *msg;
{
    (void) usprintf(scb->socket,error_rsp,msg);
    scb->state = DONE;
    log(scb->socket, "POP error with %s: %s (errno=%d)", scb->username, msg, errno);
}
  
static void
close_folder(scb)
struct pop_scb *scb;
{   /* n5knx: Much code borrowed from expire() in expire.c */
    long start,pos,msgsize;
    struct pop_msg *msg;
    FILE *new;
    int idx,idxnew,i,err;
    char file[FILE_PATH_SIZE];
    char newfile[FILE_PATH_SIZE];
    struct indexhdr hdr;
    struct mailindex index;
  
#ifdef DEBUG_HDR
    static char IRMsg[] = "POP: err %d reading index for msg %d of %s";
    static char IWMsg[] = "POP: err %d writing index for msg %d of %s";
    static char TRMsg[] = "POP: err %d reading msg %d of %s";
    static char TWMsg[] = "POP: err %d writing msg %d to %s";
#endif
  
    if (scb->txt == NULL)
        return;
  
    if (scb->folder_modified) {
        sprintf(file,"%s/%s.ind",Mailspool,scb->username);
        if((idx=open(file,READBINARY)) == -1) {    /* Open the index file */
            fatal_error(scb,"Opening folder index");
            return;
        }

        /* Create new text file */
        sprintf(file,"%s/%s.new",Mailspool,scb->username);
        if((new=fopen(file,WRITE_TEXT)) == NULLFILE) {
            close(idx);
            fatal_error(scb,"Opening new folder");
            return;
        }
  
        /* Create the new index file */
        sprintf(file,"%s/%s.idn",Mailspool,scb->username);
        if((idxnew=open(file,CREATETRUNCATEBINARY,CREATEMODE)) == -1) {
            close(idx);
            fclose(new);
            fatal_error(scb,"Opening new folder index");
            return;
        }
  
        memset(&index,0,sizeof(index));
        start = pos = 0L;
        err = 0;
  
        /* Write a default header to the new index file */
        default_header(&hdr);
        if (write_header(idxnew,&hdr) == -1) {
#ifdef DEBUG_HDR
            log(-1, IWMsg, errno, -1, scb->username);
#endif
            err++;
        }
  
        /* Read the header from the index file */
        if (read_header(idx,&hdr) == -1) {
#ifdef DEBUG_HDR
            log(-1, IRMsg, errno, -1, scb->username);
#endif
            err++;
        }
  
        /* Now read all messages, discarding deleted ones */
        msg = scb->msg;
        for(i = 1; i <= scb->folder_len && !err; i++) {
            msg++;
            default_index(scb->username,&index);
            if (read_index(idx,&index) == -1) {
#ifdef DEBUG_HDR
                log(-1,IRMsg,errno,i,scb->username);
#endif
                err++;
                break;
            }
            pwait(NULL);
            msgsize = index.size;
            if(!msg->deleted) {
                /* This one we should keep ! Copy it from txt to new */
                fseek(scb->txt,start,SEEK_SET);
                pos = ftell(new);
                /* Read the first line, should be 'From ' line */
                if (bgets(scb->buf,sizeof(scb->buf),scb->txt) == NULL) {
#ifdef DEBUG_HDR
                    log(-1, TRMsg, errno,i,scb->username);
#endif
                    err++;
                    break;
                }
                if (strncmp(scb->buf,"From ",5)) {
                    log(-1, "POP: index damage (#%d) in %s.txt",i,scb->username);
                    err++;
                    break;
                }

                /* Now copy to output until we find another 'From ' line or
                 * reach end of file.
                 */
                do {
                    if (fprintf(new,"%s\n",scb->buf) == -1) {
#ifdef DEBUG_HDR
                        log(-1, TWMsg,errno,i,scb->username);
#endif
                        err++;
                        break;  /* copy of msg will have shrunk ...  */
                    }
                    if (bgets(scb->buf,sizeof(scb->buf),scb->txt) == NULL) break;
                    pwait(NULL);
                } while (strncmp(scb->buf,"From ",5));
                /* write the index for the new copy of the message */
                index.size = ftell(new) - pos;
                if (index.size < 0 || pos < 0) {
#ifdef DEBUG_HDR
                    log(-1, "POP: ftell err %d for %s", errno, scb->username);
#endif
                    err++;
                    break;
                }
                if (WriteIndex(idxnew,&index) == -1) {
#ifdef DEBUG_HDR
                    log(-1,IWMsg,errno,i,scb->username);
#endif
                    err++;
                    break;
                }
            }

            start += msgsize;    /* starting offset of the next message */
        }
        default_index("",&index);
        close(idx);
        close(idxnew);
        pos=ftell(new);        /* how long is new area */
        fclose(new);
        fclose(scb->txt);
        scb->txt = NULLFILE;

        if (!err) {
            sprintf(file,"%s/%s.txt",Mailspool,scb->username);
            sprintf(newfile,"%s/%s.new",Mailspool,scb->username);
            unlink(file);
            if(pos)
                rename(newfile,file);
            else
                /* remove a zero length file */
                unlink(newfile);
            sprintf(file,"%s/%s.ind",Mailspool,scb->username);
            sprintf(newfile,"%s/%s.idn",Mailspool,scb->username);
            unlink(file);
            if(pos)
                rename(newfile,file);
            else {
                /* remove a zero length file */
                unlink(newfile);
            }
        }

    }
    if(scb->txt) fclose(scb->txt);
    scb->txt = NULL;
    rmlock(Mailspool,scb->username);
    scb->locked = FALSE;
    free(scb->msg);
    scb->msg=NULL;
}
  
static void
open_folder(scb)
struct pop_scb *scb;
{
    char folder_pathname[FILE_PATH_SIZE];
    long pos;
    int  i=0;
    int  idx;
    struct pop_msg *msg;
    struct indexhdr hdr;
    struct mailindex ind;
  
    while(mlock(Mailspool,scb->username)) {
        j2pause(2000);  /* wait 2 secs */
        if (++i > 15) { 
            fatal_error(scb,"Can't lock the mail folder");
            return;
        }
    }
    scb->locked = TRUE;
  
    SyncIndex(scb->username);  /* make sure index is in sync */

    sprintf(folder_pathname,"%s/%s.ind",Mailspool,scb->username);
    if ((idx = open(folder_pathname,READBINARY)) != -1) { /* exists */
        strcpy(folder_pathname+strlen(folder_pathname)-3,"txt");
        if ((scb->txt = fopen(folder_pathname, READ_BINARY)) == NULLFILE) {
            close(idx);
            fatal_error(scb,"Unable to open folder");
            return;
        }

        /* Get the number of messages in the mailbox */
        lseek(idx,0L,SEEK_SET);
        if (read_header(idx,&hdr) == -1) {
            close(idx);
            fatal_error(scb,"reading folder index hdr");
            return;
        }
        scb->folder_len = hdr.msgs;  /* number of msgs in mail folder */
        msg=scb->msg=calloc(sizeof(struct pop_msg),1+hdr.msgs); /* create msg array */
        if (msg==NULL) {
            close(idx);
            fatal_error(scb,"Unable to create pointer list");
            return;
        }

        pos=0L;
        memset(&ind,0,sizeof(ind));
        default_index("",&ind);
        for(i=1;i<=hdr.msgs;i++) {
            pwait(NULL);
            msg++;
            if (read_index(idx,&ind) == -1) {    /* should never happen */
                close(idx);
                sprintf(folder_pathname, "index damage (#%d)",  i);
                fatal_error(scb,folder_pathname);
                return;
            }

            msg->pos=pos;
            msg->len=ind.size;
#ifdef J2_CAPA_UIDL
            msg->msgid=ind.msgid;
#endif
            pos += ind.size;
            default_index("",&ind);
        }
        close(idx);
    }
  
    (void) usprintf(scb->socket,count_rsp,scb->folder_len);
    scb->state  = TRANS;
    return;         /* OK if no file */
}
  
static char no_msg_spec[] = "No message specified";
static char no_such_msg[] = "non-existent or deleted message";
  
static void
stat_message(scb)
struct pop_scb *scb;
{
    long total=0;
    int count=0;
    int i;
    struct pop_msg *msg;
  
    if (scb->folder_len) {    /* add everything up */
        msg=scb->msg;
        for (i=1; i<=scb->folder_len; i++) {
            msg++;
            if (!msg->deleted)
                total += msg->len, ++count;
        }
    }
  
    (void) usprintf(scb->socket,stat_rsp,count,total);
}
  
#ifdef	J2_CAPA_UIDL
static void list_message (struct pop_scb *scb, int uidl)
#else
static void
list_message(scb)
struct pop_scb *scb;
#endif
{
    struct pop_msg *msg;
    int msg_no=0, oldf, i;
    long total=0;
    char *cp;
  
#ifdef	J2_CAPA_UIDL
	if (uidl)
    	cp = skipwhite(&(scb->buf[sizeof(uidl_cmd) - 1]));
		 /* accomodate UIDL <sp> as UIDL */
	else
#endif
    cp = skipwhite(&(scb->buf[sizeof(list_cmd) - 1]));
			/* accomodate LIST <sp> as LIST */

    if (*cp)
    {
        msg_no = atoi(cp);
        msg=goto_msg(scb,msg_no);
        if (msg!=NULL)
		{
#ifdef	J2_CAPA_UIDL
			if (uidl)
   		       	usprintf(scb->socket,list_single_rsp, msg_no, msg->msgid);
			else
#endif
            	usprintf(scb->socket,list_single_rsp,msg_no,msg->len);
		}
    } else  /* multiline */
    {
        if (scb->folder_len) {            /* add everything */
            msg=scb->msg;
            for (i=1; i<=scb->folder_len; i++) {
                msg++;
                if (!msg->deleted)
                    total += msg->len,++msg_no;
            }
        }
  
        oldf = j2setflush(scb->socket,-1); /* we will flush it explicitly */
        (void) usprintf(scb->socket,list_multi_rsp,msg_no,total);
  
        if (scb->folder_len) {
            msg=scb->msg;
            for (i=1; i<=scb->folder_len; i++) {
                msg++;
                if (!msg->deleted)
				{
#ifdef	J2_CAPA_UIDL
					if (uidl)
                    	usprintf(scb->socket,"%d %ld\n",i,msg->msgid);
					else
#endif
                    	usprintf(scb->socket,"%d %ld\n",i,msg->len);
				}
            }
        }
        (void) usputs(scb->socket,multi_end_rsp);
        usflush(scb->socket);
        j2setflush(scb->socket,oldf);
    }
}

static void
retr_message(scb)
struct pop_scb *scb;
{
    char *p;
    long cnt;
    int msg_no, oldf, continued;
    struct pop_msg *msg;
  
    p = &(scb->buf[sizeof(retr_cmd) - 1]);
    if (*p != ' ')
    {
        state_error(scb,no_msg_spec);
        return;
    }
    msg_no = atoi(p);
    msg=goto_msg(scb,msg_no);
    if (msg==NULL) return;
  
    cnt  = msg->len;
    oldf = j2setflush(scb->socket,-1); /* we will flush it explicitly */
    (void) usprintf(scb->socket,retr_rsp,cnt);
    fseek(scb->txt,msg->pos,SEEK_SET);  /* Go there */
  
    /* Message should begin with "From sender@host date" which should NOT
       be sent as it is used in Jnos to separate messages in the mail folder
       but is not part of the message proper. -- n5knx */
    if (fgets(scb->buf,sizeof(scb->buf),scb->txt) != NULLCHAR) {
        if(strncmp(scb->buf,"From ",5))
            fseek(scb->txt,msg->pos,SEEK_SET);  /* missing ... Go back */
        else
            cnt -= (strlen(scb->buf)+EolLen-1);  /* assumed to never be continued! */
    }  /* note our reported msg len will be too big...tant pis! */
    continued = 0;
    while(fgets(scb->buf,sizeof(scb->buf),scb->txt), !feof(scb->txt)&&(cnt > 0)) {
        pwait(NULL);
        if (!continued) {
            if ( *scb->buf == '.' ) {
                (void) usputc(scb->socket,'.');
                /*cnt--;*/
            }
        }
        continued = (strchr(scb->buf,'\n') == NULLCHAR);
        rip(scb->buf);
        (void) usputs(scb->socket,scb->buf);
        cnt -= (strlen(scb->buf));
        if(!continued) {
            (void) usputc(scb->socket,'\n');
            cnt -= EolLen;  /* CRLF or LF or CR or ?? */
        }
    }
    (void) usputs(scb->socket,".\n");
    usflush(scb->socket);
    j2setflush(scb->socket,oldf);
    if (msg_no >= scb->high_num)
        scb->high_num=msg_no;     /* bump high water mark */
}
  
static void
noop_message(scb)
struct pop_scb *scb;
{
    (void) usputs(scb->socket,noop_rsp);
}
  
static void
last_message(scb)
struct pop_scb *scb;
{
    (void) usprintf(scb->socket,last_rsp,scb->high_num);
}
  
static void
rset_message(scb)
struct pop_scb *scb;
{
    struct pop_msg *msg;
    long total=0;
    int i;
  
    if (scb->folder_len) {
        msg=scb->msg;
        for (i=1; i<=scb->folder_len; i++) {
            msg++;
            msg->deleted=FALSE;
            total+=msg->len;
       }
    }
  
    scb->high_num=0;  /* reset last */
    scb->folder_modified=FALSE;
    (void) usprintf(scb->socket,list_multi_rsp,scb->folder_len,total);
}
  
static void
top_message(scb)
struct pop_scb *scb;
{
    char *ptr,*p;
    struct pop_msg *msg;
    int msg_no=0,lines=0,oldf;
    long total=0;
  
  
    p = &(scb->buf[sizeof(top_cmd) - 1]);
    if (*p != ' ')
    {
        state_error(scb,no_msg_spec);
        return;
    }
    ptr=skipwhite(p+1);
    for ( ; *ptr!=' ' && *ptr !='\0'; ++ptr);
        /* token drop */
    msg_no = atoi(p);
    lines = atoi(++ptr);  /* Get # lines to top */
    if (lines < 0) lines=0;
  
    msg=goto_msg(scb,msg_no);
    if (msg==NULL) return;

    fseek(scb->txt,msg->pos,SEEK_SET);  /* Go there */
    total=msg->len;  /* Length of current message */
    oldf = j2setflush(scb->socket,-1); /* we will flush it explicitly */
    (void) usputs(scb->socket,noop_rsp);  /* Give OK */
    do {  /* first print all header lines */
        if (fgets(scb->buf,sizeof(scb->buf),scb->txt) == NULLCHAR)
            break;  /* can't happen! */
        rip(scb->buf);
        if ( *scb->buf == '.' ) {
            (void) usputc(scb->socket,'.');
            /*total--;*/
        }
        total -= (strlen(scb->buf)+EolLen);
        (void) usputs(scb->socket,scb->buf);
        (void) usputc(scb->socket,'\n');
    } while (*scb->buf!='\0' && total>0);
    for ( ; total > 0 && lines; --lines) {
        if (fgets(scb->buf,sizeof(scb->buf),scb->txt) == NULLCHAR)
            break;  /* can't happen! */
        rip(scb->buf);
        if ( *scb->buf == '.' ) {
            (void) usputc(scb->socket,'.');
            /*total--;*/
        }
        total -= (strlen(scb->buf)+EolLen);
        (void) usputs(scb->socket,scb->buf);
        (void) usputc(scb->socket,'\n');
    }
    (void) usputs(scb->socket,multi_end_rsp);
    usflush(scb->socket);
    j2setflush(scb->socket,oldf);
}
  
static int
poplogin(scb,pass)
struct pop_scb *scb;
char *pass;
{
    char buf[80];
    char *cp;
    char *cp1;
    FILE *fp;
#ifdef MD5AUTHENTICATE
    MD5_CTX md;
    int i,j;
    char buf2[64];
#endif

    if((fp = fopen(Popusers,"r")) == NULLFILE) {
        /* User file doesn't exist */
        tprintf("POP users file %s not found\n",Popusers);
        return(FALSE);
    }
  
    while(fgets(buf,sizeof(buf),fp),!feof(fp)) {
        if(buf[0] == '#')
            continue; /* Comment */
  
        if((cp = strchr(buf,':')) == NULLCHAR)
            /* Bogus entry */
            continue;
  
        *cp++ = '\0';  /* Now points to password */
        if(strcmp(scb->username,buf) == 0)
            break;  /* Found user name */
    }
  
    if(feof(fp)) {
        /* User name not found in file */
  
        fclose(fp);
        return(FALSE);
    }
    fclose(fp);
  
    if ((cp1 = strchr(cp,':')) == NULLCHAR)
        return(FALSE);
  
    *cp1 = '\0';
    if(strcmp(cp,pass) != 0) {
        /* Password required, but wrong one given */
#ifdef MD5AUTHENTICATE
        /* See if it is the response to a challenge */
        if(strlen(pass) == 32) {
            MD5Init(&md);
            sprintf(buf2,"<%x@%s>", scb->challenge,Hostname);
            MD5Update(&md,(unsigned char *)buf2,strlen(buf2));
            MD5Update(&md,(unsigned char *)cp,strlen(cp));
            MD5Final(&md);
#ifdef MD5DEBUG
            printf("auth: challenge %x, passwd %s\nMD5 sum is ", challenge, cp);
            for(i=0; i<16; i++)
                printf("%02x",md.digest[i]);
            printf("\n");
#endif
            for(cp=pass,i=0; i<16; cp+=2,i++) {
                sscanf(cp,"%2x",&j);
                if (md.digest[i] != (unsigned char)j)
                    return FALSE;   /* give up at first miscompare */
            }
        }
        else return(FALSE);
#else
        return(FALSE);
#endif /* MD5AUTHENTICATE */
    }
  
    /* whew! finally made it!! */
  
    return(TRUE);
}
  
static void
dele_message(scb)
struct pop_scb *scb;
{
    struct pop_msg *msg;
    int msg_no;
  
    if (scb->buf[sizeof(retr_cmd) - 1] != ' ')
    {
        state_error(scb,no_msg_spec);
        return;
    }
    msg_no = atoi(&(scb->buf[sizeof(retr_cmd) - 1]));
    msg=goto_msg(scb,msg_no);
    if (msg==NULL) return;

    msg->deleted=TRUE;
    scb->folder_modified = TRUE;
    (void) usprintf(scb->socket,dele_rsp,msg_no);
}
  
static struct pop_msg *
goto_msg(struct pop_scb *scb,int msg_no)
{
    int i;
    struct pop_msg *msg;
  
    if (!scb->folder_len || msg_no < 1 || msg_no > scb->folder_len)
	{
	    state_error(scb,no_such_msg);
        return NULL;
    }
    for (msg=scb->msg, i=0; i!=msg_no; msg++, i++) ;
    if(msg->deleted)
	{
	    state_error(scb,no_such_msg);
        return NULL;
	}
    return msg;
}
  
#endif
