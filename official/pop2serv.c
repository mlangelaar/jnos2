/* POP2 Server state machine - see RFC 937
 *
 *  also see other credits in popcli.c
 *  10/89 Mike Stockett wa7dyx
 *  Modified 5/27/90 by Allen Gwinn, N5CKP, for later NOS releases.
 *  Added to NOS by PA0GRI 2/6/90 (and linted into "standard" C)
 *
 *      Aug-Oct 92      Mike Bilow, N1BEE, mikebw@ids.net
 *              Extensive bug fixes, changed uses of Borland stat()
 *              to fsize() in order to fix intermittent crashes
 *
 *  Support for Mail Index Files, June/July 1993, Johan. K. Reinalda, WG7J
 *  Locking, pwait's, delayed flushes by N5KNX 2/95 (idea from Selcuk in POP3)
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
#ifdef POP2SERVER
#include "mbuf.h"
#include "cmdparse.h"
#include "socket.h"
#include "proc.h"
#include "files.h"
#include "smtp.h"
#include "mailcli.h"                    /* N1BEE */
#include "dirutil.h"
#include "index.h"
#include "bm.h"
  
#ifdef UNIX
extern int unlink __ARGS((char *));
#endif
  
#ifndef POP_TIMEOUT
#define POP_TIMEOUT  600  /* seconds */
#endif

#undef DEBUG                            /* N1BEE */
  
  
/* ---------------- common server data structures ---------------- */
  
/* POP server control block */
  
struct pop_scb {
    int     socket;         /* socket number for this connection */
    char    state;          /* server state */
#define         LSTN    0
#define         AUTH    1
#define         MBOX    2
#define         ITEM    3
#define         NEXT    4
#define         DONE    5
    char    buf[TLINELEN+1];        /* input line buffer */
    char    count;          /* line buffer length */
    char    username[32];   /* user/folder name */
    FILE    *wf;            /* work folder file pointer */
    int     folder_len;     /* number of msgs in current folder */
    int     msg_num;        /* current msg number */
    long    msg_len;        /* length of current msg */
    int     msg_status_size; /* size of the message status array */
    long    curpos;         /* current msg's position in file */
    long    folder_file_size; /* length of the current folder file, in bytes */
    long    nextpos;        /* next msg's position in file */
    char    folder_modified; /* mail folder contents modified flag */
    int16   *msg_status;    /* message status array pointer */
};
  
#ifdef STATUSWIN
#ifdef POP3SERVER
extern
#endif
int PopUsers;		/* for status window */
#endif

#define NULLSCB         (struct pop_scb *)0
  
/* Response messages */
  
static char     count_rsp[]    = "#%d messages in this folder\n",
error_rsp[]    = "- ERROR: %s\n",
greeting_msg[] = "+ POP2 %s\n",
/*              length_rsp[]   = "=%ld bytes in this message\n", */
length_rsp[]   = "=%ld characters in Message #%d\n",
no_mail_rsp[]  = "+ No mail, sorry\n",
no_more_rsp[]  = "=%d No more messages in this folder\n",
signoff_msg[]  = "+ Bye, thanks for calling\n";
  
static struct pop_scb *create_scb __ARGS((void));
static void delete_scb __ARGS((struct pop_scb *scb));
static void popserv __ARGS((int s,void *unused,void *p));
static int poplogin __ARGS((char *pass,char *username));
  
static void pop_sm __ARGS((struct pop_scb *scb));
  
/* Start up POP receiver service */
int
pop2start(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;
  
    if(argc < 2)
        port = IPPORT_POP2;
    else
        port = atoi(argv[1]);
  
    return start_tcp(port,"POP2 Server",popserv,1536);
}
  
/* Shutdown POP2 service (existing connections are allowed to finish) */
int
pop2stop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;
  
    if(argc < 2)
        port = IPPORT_POP2;
    else
        port = atoi(argv[1]);
    return stop_tcp(port);
}

/* 29Dec2004, Replace GOTO 'quit' with function */
static void do_quit (struct pop_scb *scb)
{  
    log(scb->socket,"close POP2");
    close_s(scb->socket);
#ifdef STATUSWIN
    PopUsers--;
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
  
    sockowner(s,Curproc);           /* We own it now */
    log(s,"open POP2");
#ifdef STATUSWIN
    PopUsers++;
#endif
  
    if((scb = create_scb()) == NULLSCB) {
        j2tputs(Nospace);
        log(s,"close POP2 - no space");
        close_s(s);
#ifdef STATUSWIN
	PopUsers--;
#endif
        return;
    }
  
    scb->socket = s;
    scb->state  = AUTH;
  
    (void) usprintf(s,greeting_msg,Hostname);
 
	while (1)	/* replace GOTO 'loop:' with while loop */
	{
 
    	j2alarm(POP_TIMEOUT * 1000);
    	if ((scb->count = recvline(s,scb->buf,TLINELEN)) == -1)
		{
        	/* He closed on us or timeout occurred */
			break;
    	}
    	j2alarm(0);
  
    	rip(scb->buf);

    	if (strlen(scb->buf) == 0)      /* Ignore blank cmd lines */
			continue;
  
#ifdef DEBUG
    	if(Mailtrace >= 3)
		{
        	tprintf("POP2SERV(popserv): Processing line <%s>\n",scb->buf);
        	/* getch(); */
    	}
#endif /* DEBUG */
  
    	/* Convert lower, and mixed case commands to UPPER case - Ashok */
    	for(cp = scb->buf;*cp != ' ' && *cp != '\0';cp++)
        	*cp = toupper(*cp);
  
    	pop_sm(scb);

    	if (scb->state == DONE)
			break;
 
	}	/* end of while loop that replaces GOTO 'loop:' label */
  
	return (do_quit (scb));
}
  
/* Create control block, initialize */
  
static struct
pop_scb *create_scb()
{
    register struct pop_scb *scb;
  
    if((scb = (struct pop_scb *)calloc(1,sizeof (struct pop_scb))) == NULLSCB)
        return NULLSCB;
  
    scb->username[0] = '\0';
    scb->msg_status = NULL;
    scb->wf = NULL;
  
    scb->count = scb->folder_file_size = scb->msg_num = 0;
  
    scb->folder_modified = FALSE;
    return scb;
}
  
  
/* Free resources, delete control block */
  
static void
delete_scb(scb)
register struct pop_scb *scb;
{
  
    if (scb == NULLSCB)
        return;
    if (scb->wf != NULL)
        fclose(scb->wf);
    if (scb->msg_status  != NULL)
        free((char *)scb->msg_status);
  
    free((char *)scb);
}
  
/* --------------------- start of POP server code ------------------------ */
  
#define BITS_PER_WORD   16
  
#define isSOM(x)        ((strncmp(x,"From ",5) == 0))   /* Start Of Message */
  
/* Command string specifications */
  
static char     ackd_cmd[] = "ACKD",
acks_cmd[] = "ACKS",
#ifdef POP_FOLDERS
fold_cmd[] = "FOLD ",
#endif
login_cmd[] = "HELO ",
nack_cmd[] = "NACK",
quit_cmd[] = "QUIT",
read_cmd[] = "READ",
retr_cmd[] = "RETR";
  
static void
pop_sm(scb)
struct pop_scb *scb;
{
    char password[30];
#ifndef __TURBOC__
    static
#endif
    void state_error(struct pop_scb *,char *);
#ifndef __TURBOC__
    static
#endif
    void open_folder(struct pop_scb *);
#ifndef __TURBOC__
    static
#endif
    void do_cleanup(struct pop_scb *);
#ifndef __TURBOC__
    static
#endif
    void read_message(struct pop_scb *);
#ifndef __TURBOC__
    static
#endif
    void retrieve_message(struct pop_scb *);
#ifndef __TURBOC__
    static
#endif
    void deletemsg(struct pop_scb *,int);
#ifndef __TURBOC__
    static
#endif
    void get_message(struct pop_scb *,int);
#ifndef __TURBOC__
    static
#endif
    void print_message_length(struct pop_scb *);
#ifndef __TURBOC__
    static
#endif
    void close_folder(struct pop_scb *);
#ifdef POP_FOLDERS
#ifndef __TURBOC__
    static
#endif
    void select_folder(struct pop_scb *);
#endif
  
    if (scb == NULLSCB)     /* be certain it is good -- wa6smn */
        return;
  
    switch(scb->state) {
        case AUTH:
#ifdef DEBUG
            if(Mailtrace >= 3)
                j2tputs("POP2SERV(pop_sm): Entering case AUTH\n");
#endif /* DEBUG */
            if (strncmp(scb->buf,login_cmd,strlen(login_cmd)) == 0){
                sscanf(scb->buf,"HELO %31s %29s",scb->username,password);
#ifdef DEBUG
                if(Mailtrace >= 3) {
                    tprintf("POP2SERV(pop_sm): Processing USER %s PASS %s\n",scb->username,password);
                    tprintf("POP2SERV(pop_sm): Calling poplogin() for %s:%s:\n",scb->username,password);
                }
#endif /* DEBUG */
  
                if (!poplogin(scb->username,password)) {
                    log(scb->socket,"POP2 access DENIED to %s",
                    scb->username);
                    state_error(scb,"Access DENIED!!");
                    return;
                }
  
                log(scb->socket,"POP2 access granted to %s",
                scb->username);
                open_folder(scb);
            } else if (strncmp(scb->buf,quit_cmd,strlen(quit_cmd)) == 0){
                do_cleanup(scb);
            } else
                state_error(scb,"(AUTH) Expected HELO or QUIT command");
#ifdef DEBUG
            if(Mailtrace >= 3)
                j2tputs("POP2SERV(pop_sm): Leaving case AUTH\n");
#endif /* DEBUG */
            break;
  
        case MBOX:
            if (strncmp(scb->buf,read_cmd,strlen(read_cmd)) == 0)
                read_message(scb);
  
#ifdef POP_FOLDERS
            else if (strncmp(scb->buf,fold_cmd,strlen(fold_cmd)) == 0)
                select_folder(scb);
  
#endif
  
            else if (strncmp(scb->buf,quit_cmd,strlen(quit_cmd)) == 0) {
                do_cleanup(scb);
            } else
                state_error(scb,
#ifdef POP_FOLDERS
                "(MBOX) Expected FOLD, READ, or QUIT command");
#else
            "(MBOX) Expected READ or QUIT command");
#endif
            break;
  
        case ITEM:
            if (strncmp(scb->buf,read_cmd,strlen(read_cmd)) == 0)
                read_message(scb);
  
#ifdef POP_FOLDERS
  
            else if (strncmp(scb->buf,fold_cmd,strlen(fold_cmd)) == 0)
                select_folder(scb);
#endif
  
            else if (strncmp(scb->buf,retr_cmd,strlen(retr_cmd)) == 0)
                retrieve_message(scb);
            else if (strncmp(scb->buf,quit_cmd,strlen(quit_cmd)) == 0)
                do_cleanup(scb);
            else
                state_error(scb,
#ifdef POP_FOLDERS
                "(ITEM) Expected FOLD, READ, RETR, or QUIT command");
#else
            "(ITEM) Expected READ, RETR, or QUIT command");
#endif
            break;
  
        case NEXT:
            if (strncmp(scb->buf,ackd_cmd,strlen(ackd_cmd)) == 0){
                /* ACKD processing */
                deletemsg(scb,scb->msg_num);
                scb->msg_num++;
                get_message(scb,scb->msg_num);
            } else if (strncmp(scb->buf,acks_cmd,strlen(acks_cmd)) == 0){
                /* ACKS processing */
                scb->msg_num++;
                get_message(scb,scb->msg_num);
            } else if (strncmp(scb->buf,nack_cmd,strlen(nack_cmd)) == 0){
                /* NACK processing */
                fseek(scb->wf,scb->curpos,SEEK_SET);
            } else {
                state_error(scb,"(NEXT) Expected ACKD, ACKS, or NACK command");
                return;
            }
  
            print_message_length(scb);
            scb->state  = ITEM;
            break;
  
        case DONE:
            do_cleanup(scb);
            break;
  
        default:
            state_error(scb,"(TOP) State Error!!");
            break;
    }
  
#ifdef DEBUG
    if(Mailtrace >= 3)
        tprintf("POP2SERV(pop_sm): Leaving state machine; state %u\n",scb->state);
#endif /* DEBUG */
}
  
static void
do_cleanup(scb)
struct pop_scb *scb;
{
#ifndef __TURBOC__
    static
#endif
    void close_folder(struct pop_scb *);
  
    close_folder(scb);
    (void) usputs(scb->socket,signoff_msg);
    scb->state = DONE;
}
  
static void
state_error(scb,msg)
struct pop_scb *scb;
char *msg;
{
    if(Mailtrace >= 2)
        tprintf(error_rsp,msg);
    (void) usprintf(scb->socket,error_rsp,msg);
    scb->state = DONE;
}
  
#ifdef POP_FOLDERS
  
static void
select_folder(scb)
struct pop_scb  *scb;
{
    sscanf(scb->buf,"FOLD %s",scb->username);
  
    if (scb->wf != NULL)
        close_folder(scb);
  
    open_folder(scb);
}
  
#endif
  
static int
isdeleted(scb,msg_no)
struct pop_scb *scb;
int msg_no;
{
    int16 mask = 1,offset;
  
    msg_no--;
    offset = msg_no / BITS_PER_WORD;
    mask <<= msg_no % BITS_PER_WORD;
    return (((scb->msg_status[offset]) & mask)? TRUE:FALSE);
}
  
static void
close_folder(scb)
struct pop_scb *scb;
{
    char folder_pathname[FILE_PATH_SIZE];
    char line[TLINELEN+1];
    FILE *fd;
    int deleted = FALSE;
    int msg_no = 0;
    int prevtype=NOHEADER;  
    int prev;
    long start;
    char *cp;
    struct mailindex ind;
  
#ifndef __TURBOC__
    static
#endif
    int newmail(struct pop_scb *);
#ifndef __TURBOC__
    static
#endif
    int isdeleted(struct pop_scb *,int);
  
    if (scb->wf == NULL)
        return;
  
    if (!scb->folder_modified) {
        /* no need to re-write the folder if we have not modified it */
  
        fclose(scb->wf);
        scb->wf = NULL;
  
        free((char *)scb->msg_status);
        scb->msg_status = NULL;
        return;
    }
  
  
    sprintf(folder_pathname,"%s/%s.txt",Mailspool,scb->username);
    if(mlock(Mailspool,scb->username)) {
        state_error(scb,"Can't lock the mail folder");
        return;
    }
  
    if (newmail(scb)) {
        /* copy new mail into the work file and save the
           message count for later */
  
        if ((fd = fopen(folder_pathname,"r")) == NULL) {
            state_error(scb,"Unable to add new mail to folder");
            rmlock(Mailspool,scb->username);
            return;
        }
  
        fseek(scb->wf,0,SEEK_END);
        fseek(fd,scb->folder_file_size,SEEK_SET);
        while (!feof(fd)) {
            fgets(line,TLINELEN,fd);
            if (fputs(line,scb->wf) == EOF) {
                state_error(scb,"Unable to add new mail to work folder");
                rmlock(Mailspool,scb->username);
                return;
            }
        }
  
        fclose(fd);
    }
  
    /* now create the updated mail folder */
  
    if ((fd = fopen(folder_pathname,"w")) == NULL){
        state_error(scb,"Unable to update mail folder");
        rmlock(Mailspool,scb->username);
        return;
    }
  
    /* This simply rewrites the whole mail folder,
     * so also simply recreate the index file - WG7J
     */
    delete_index(scb->username);
    memset(&ind,0,sizeof(struct mailindex));
  
    rewind(scb->wf);
    prev = 0;
    start = 0;
    while(fgets(line,TLINELEN,scb->wf) != NULLCHAR) {
        pwait(NULL);
        if (isSOM(line)){
            if(prev && !deleted) {
                /* write the index for the previous message */
                ind.size = ftell(fd) - start;
                write_index(scb->username,&ind);
            } else
                prev = 1;
            msg_no++;
            if (msg_no <= scb->folder_len)
                deleted = isdeleted(scb,msg_no);
            else
                deleted = FALSE;
            if(!deleted) {
                /* read the smtp header first to create the index */
                start = ftell(fd);  /* Starting offset of this message */
                fputs(line,fd);     /* put the from line back */
  
                default_index(scb->username,&ind);
  
                /* read the 'Received...' and 'ID... lines'
                 * to get the msgid
                 */
                fgets(line,TLINELEN,scb->wf);
                fputs(line,fd);
                fgets(line,TLINELEN,scb->wf);
                fputs(line,fd);
                if((cp=strstr(line,"AA")) != NULLCHAR)
                    /*what follows is the message-number*/
                    ind.msgid = atol(cp+2);
  
                /* now scan rest of headers */
                while(fgets(line,TLINELEN,scb->wf) != NULLCHAR) {
                    fputs(line,fd);
                    if(*line == '\n')
                        break; /* end of headers */
                    set_index(line,&ind,htype(line,&prevtype));
                }
            }
        } else if (!deleted) {
  
            fputs(line,fd);
        }
    }
  
    /* Update the last message handled */
    if(prev && !deleted) {
        ind.size = ftell(fd) - start;
        write_index(scb->username,&ind);
    }
  
    fclose(fd);
  
    /* trash the updated mail folder if it is empty */
  
    if(fsize(folder_pathname) == 0L)                /* N1BEE */
        unlink(folder_pathname);
    rmlock(Mailspool,scb->username);
  
    fclose(scb->wf);
    scb->wf = NULL;
  
    free((char *)scb->msg_status);
    scb->msg_status = NULL;
}
  
static void
open_folder(scb)
struct pop_scb  *scb;
{
    char folder_pathname[FILE_PATH_SIZE];
    char line[TLINELEN+1];
    FILE *fd;
/*      FILE *tmpfile();                should not declare here -- N1BEE */
    struct stat folder_stat;
  
  
    sprintf(folder_pathname,"%.45s/%.8s.txt",Mailspool,scb->username);
#ifdef DEBUG
    if(Mailtrace >= 3) {
        tprintf("POP2SERV(open_folder): will open %s\n",folder_pathname);
    }
#endif /* DEBUG */
    scb->folder_len       = 0;
    scb->folder_file_size = 0;
  
    /* Ordinarily, we would call stat() to find out if the file exists
       and get its size at the same time.  However, there is a bug in
       Borland's stat() code which crashes DesqView and OS/2 (!) if
       stat() is called on a file which does not exist.  -- N1BEE
    */
  
    /* if (stat(folder_pathname,&folder_stat)){ */
    if((folder_stat.st_size = fsize(folder_pathname)) == -1L) { /* N1BEE */
#ifdef DEBUG
        if(Mailtrace >= 3) {
            j2tputs("POP2SERV(open_folder): folder not found (empty)\n");
        }
#endif /* DEBUG */
        (void) usputs(scb->socket,no_mail_rsp);
  
        /* state remains AUTH, expecting HELO or QUIT */
        return;
    }
  
    scb->folder_file_size = folder_stat.st_size;
    if ((fd = fopen(folder_pathname,"r")) == NULL){
        state_error(scb,"POP2SERV(open_folder): Unable to open mail folder");
        return;
    }
  
    if ((scb->wf = tmpfile()) == NULL) {
        state_error(scb,"POP2SERV(open_folder): Unable to create work folder");
        return;
    }
  
    while(!feof(fd)) {
        pwait(NULL);  /* let other processes run a while */
        fgets(line,TLINELEN,fd);
  
        /* scan for begining of a message */
  
        if (isSOM(line)) {
            scb->folder_len++;
        }
  
        /* now put  the line in the work file */
  
        if (fputs(line,scb->wf) == EOF) {
             state_error(scb,"Unable to write to work file.");
            return;
        }
    }
  
    fclose(fd);
  
    scb->msg_status_size = (scb->folder_len) / BITS_PER_WORD;
  
    if ((((scb->folder_len) % BITS_PER_WORD) != 0) ||
        (scb->msg_status_size == 0))
        scb->msg_status_size++;
  
    if ((scb->msg_status = (int16 *) callocw(scb->msg_status_size,
    sizeof(int16))) == NULL) {
        state_error(scb,"Unable to create message status array");
        return;
    }
  
    (void) usprintf(scb->socket,count_rsp,scb->folder_len);
  
    scb->state  = MBOX;
  
#ifdef DEBUG
    if(Mailtrace >= 3)
        j2tputs("POP2SERV: open_folder() completed successfully.\n");
#endif /* DEBUG */
}
  
static void
read_message(scb)
struct pop_scb  *scb;
{
#ifndef __TURBOC__
    static
#endif
    void get_message(struct pop_scb *,int);
#ifndef __TURBOC__
    static
#endif
    void print_message_length(struct pop_scb *);
  
    if (scb == NULLSCB)     /* check for null -- wa6smn */
        return;
    if (scb->buf[sizeof(read_cmd) - 1] == ' ')
        scb->msg_num = atoi(&(scb->buf[sizeof(read_cmd) - 1]));
    else
        scb->msg_num++;
  
    get_message(scb,scb->msg_num);
    print_message_length(scb);
    scb->state  = ITEM;
}
  
static void
retrieve_message(scb)
struct pop_scb  *scb;
{
    char line[TLINELEN+1];
    int oldf;
    long cnt;
  
    if (scb == NULLSCB)     /* check for null -- wa6smn */
        return;
    if (scb->msg_len == 0) {
        state_error(scb,"Attempt to access a DELETED message!");
        return;
    }
  
    cnt  = scb->msg_len;
    oldf = j2setflush(scb->socket,-1); /* we will flush it explicitly */
    while(!feof(scb->wf) && (cnt > 0)) {
        fgets(line,TLINELEN,scb->wf);
        rip(line);
  
        (void) usputs(scb->socket,line);	/* per KD1SM */
        (void) usputc(scb->socket,'\n');
        cnt -= (strlen(line)+2);        /* Compensate for CRLF */
    }
    usflush(scb->socket);
    j2setflush(scb->socket,oldf);

    scb->state = NEXT;
}
  
static void
get_message(scb,msg_no)
struct pop_scb  *scb;
int msg_no;
{
    char line[TLINELEN+1];
    long ftell();
  
    if (scb == NULLSCB)     /* check for null -- wa6smn */
        return;
    scb->msg_len = 0;
    if (msg_no > scb->folder_len) {
        scb->curpos  = 0;
        scb->nextpos = 0;
        return;
    } else {
        /* find the message and its length */
  
        rewind(scb->wf);
        while (!feof(scb->wf) && (msg_no > -1)) {
            if (msg_no > 0)
                scb->curpos = ftell(scb->wf);
  
            fgets(line,TLINELEN,scb->wf);
            rip(line);
  
            if (isSOM(line)) {
                msg_no--;
                pwait(NULL);
            }
  
            if (msg_no != 0)
                continue;
  
            scb->nextpos  = ftell(scb->wf);
            scb->msg_len += (strlen(line)+2);       /* Add CRLF */
        }
    }
  
    if (scb->msg_len > 0)
        fseek(scb->wf,scb->curpos,SEEK_SET);
  
    /* we need the pointers even if the message was deleted */
  
    if  (isdeleted(scb,scb->msg_num))
        scb->msg_len = 0;
}
  
static int
poplogin(username,pass)
char *pass;
char *username;
{
    char buf[80];
    char *cp;
    char *cp1;
    FILE *fp;
  
#ifdef DEBUG
    if(Mailtrace >= 3)
        tprintf("POP2SERV(poplogin): Opening POP users file %s\n",Popusers);
#endif /* DEBUG */
  
    if((fp = fopen(Popusers,"r")) == NULLFILE) {
        /* User file doesn't exist */
        tprintf("POP2 users file %s not found\n",Popusers);
        return(FALSE);
    }
  
#ifdef DEBUG
    if(Mailtrace >= 3)
        tprintf("POP2SERV(poplogin): Login request from %s:%s:\n",username,pass);
#endif /* DEBUG */
  
    while(fgets(buf,sizeof(buf),fp),!feof(fp)) {
        if(buf[0] == '#')
            continue;       /* Comment */
  
        if((cp = strchr(buf,':')) == NULLCHAR)
            /* Bogus entry */
            continue;
  
        *cp++ = '\0';           /* Now points to password */
        if(strcmp(username,buf) == 0)
            break;          /* Found user name */
    }
  
    if(feof(fp)) {
#ifdef DEBUG
        if(Mailtrace >= 3)
            j2tputs("POP2SERV(poplogin): username not found in POPUSERS\n");
#endif /* DEBUG */
        /* User name not found in file */
  
        fclose(fp);
        return(FALSE);
    }
    fclose(fp);
  
    if ((cp1 = strchr(cp,':')) == NULLCHAR) {
#ifdef DEBUG
        if(Mailtrace >= 3)
            j2tputs("POP2SERV(poplogin): No second ':' in POPUSERS entry\n");
#endif /* DEBUG */
        return(FALSE);
    }
  
    *cp1 = '\0';
    if(strcmp(cp,pass) != 0) {
#ifdef DEBUG
        if(Mailtrace >= 3)
            tprintf("POP2SERV(poplogin): Wrong password (%s) from user %s, expecting %s\n",pass,username,cp);
#endif /* DEBUG */
        /* Password required, but wrong one given */
        return(FALSE);
    }
  
    /* whew! finally made it!! */
#ifdef DEBUG
    if(Mailtrace >= 3)
        tprintf("POP2SERV(poplogin): %s authenticated\n",username);
#endif /* DEBUG */
  
    return(TRUE);
}
  
static void
deletemsg(scb,msg_no)
struct pop_scb *scb;
int msg_no;
{
    int16 mask = 1,offset;
  
    if (scb == NULLSCB)     /* check for null -- wa6smn */
        return;
    msg_no--;
    offset = msg_no / BITS_PER_WORD;
    mask <<= msg_no % BITS_PER_WORD;
    scb->msg_status[offset] |= mask;
    scb->folder_modified = TRUE;
}
  
static int
newmail(scb)
struct pop_scb *scb;
{
    char folder_pathname[FILE_PATH_SIZE];
    struct stat folder_stat;
  
    sprintf(folder_pathname,"%s/%s.txt",Mailspool,scb->username);
  
    /* if (stat(folder_pathname,&folder_stat)) { */
    if((folder_stat.st_size = fsize(folder_pathname)) == -1L) { /* N1BEE */
        state_error(scb,"Unable to get old mail folder's status");
        return(FALSE);
    } else
        return ((folder_stat.st_size > scb->folder_file_size)? TRUE:FALSE);
}
  
static void
print_message_length(scb)
struct pop_scb *scb;
{
    char *print_control_string;
  
    if (scb == NULLSCB)     /* check for null -- wa6smn */
        return;
    if (scb->msg_len > 0)
        print_control_string = length_rsp;
    else if (scb->msg_num <= scb->folder_len)
        print_control_string = length_rsp;
    else
        print_control_string = no_more_rsp;
  
    (void)usprintf(scb->socket,print_control_string,scb->msg_len,scb->msg_num);
}
  
#endif
