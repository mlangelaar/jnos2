/*
 *
 * NNTP Server/Client - See RFC977
 * Jeffrey R. Comstock. - NR0D - Bloomington, Minnesota USA
 * Copyright 1990 Jeffrey R. Comstock, All Rights Reserved.
 * Permission granted for non-commercial copying and use, provided
 * this notice is retained.
 *
 * DB3FL 9107xx: heavily rewritten and bug fixing in file-handling
 * ported to NOS by PE1NMB - 920120
 *
 * DB3FL: ihave, read & lzw ported to NOS by YC1DAV 920804
 *
 * Mods by PA0GRI
 *
 * OH2BNS 9311xx: Fixed LIST, STAT, HEAD and BODY. Implemented POST.
 *                Added 'nntp create'. Small fixes here and there...
 *
 * OH2BNS 9401xx: Rewrote STAT, HEAD, BODY, ARTICLE. Implemented
 *                XHDR, XOVER, 'nntp access'. Rewrote garbled().
 *
 * N5KNX  950626: NEWGROUPS and DATE added, from G8FSL uknos.
 * N5KNX  960703: minimal news2mail added, influenced by G8FSL uknos.
 * N5KNX  960828: added statnew(), for when NEWNEWS cmd isn't supported.
 * 
 * VE4KLM 06Jun2021 - stability mods, checking for null pointers as well as
 * possible corrupt remote group strings and history file entries. Also the
 * size of remote group strings received is no longer a concern, the buffer
 * size is now dynamically allocated, previously was 'char groups[512]', so
 * that should fix reports of JNOS crashing if it got too many groups.
 *
 * VE4KLM 16Jun2021 - the software wars have begun, so now protecting sysops
 * from themselves, not my choice, but to appease the NNTP police out there.
 *
 */

#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include <setjmp.h>
#ifdef MSDOS
#include <dos.h>
#include <dir.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#ifdef UNIX
#include <fcntl.h>
#endif
#ifdef MSDOS
#include <io.h>
#endif
#include "global.h"

#ifdef NNTPS
#include "domain.h"
#include "mbuf.h"
#include "cmdparse.h"
#include "socket.h"
#include "iface.h"
#include "proc.h"
#include "smtp.h"
#include "commands.h"
#include "dirutil.h"
#include "ftp.h"
#include "netuser.h"
#include "nntp.h"
#include "session.h"
#include "files.h"
#include "smtp.h"
#include "bm.h"
#ifdef LESS_WHINING
#include "pc.h"
#include "nr4.h"
#include "netrom.h"
#include "udp.h"
#include "tcp.h"
#include "ip.h"
#include "usock.h"
#endif
#ifdef LZW
#include "lzw.h"
#endif

#define	NNTP_ONLY_44NET		/* 16Jun2021, Maiko (VE4KLM), Should be permanent !!! */
  
FILE *open_file __ARGS((char *name,char *mode,int s,int t));
FILE *temp_file __ARGS((int s,int t));          /* NMB */
  
static int near make_dir __ARGS((char *path,int s));
static int check_file __ARGS((char *path));
static int make_path __ARGS((char *group));
static int check_blank __ARGS((char *bp));
static int dup_f __ARGS((FILE *in,FILE *out,struct nntpsv *mp));
static int update_list __ARGS((struct nntpsv *a));
static int get_path2 __ARGS((struct article *art));
static int dofwd __ARGS((struct nntpsv *mp,FILE *f,FILE *history));
static int getreply __ARGS((struct nntpsv *cb));
static int newnews __ARGS((char *string,struct nntpsv *mp,FILE *f));
static int recv_file __ARGS((FILE *fp,int s));
static int check_article __ARGS((char *id));
static int xfer_article2 __ARGS((FILE *f,struct nntpsv *mp));
static int retr_by_mid __ARGS((char *mid,struct nntpsv *mp));
static int retr_by_num __ARGS((char *buf,struct nntpsv *mp));
static int doarticlecmd __ARGS((char *buf,struct nntpsv *mp,int retcode));
static int doheadcmd __ARGS((char *buf,struct nntpsv *mp));
static int dobodycmd __ARGS((char *buf,struct nntpsv *mp));
static void sendpart __ARGS((FILE *fp,struct nntpsv *mp,int flag));
static int dostatcmd __ARGS((char *buf,struct nntpsv *mp));
static int doxhdrcmd __ARGS((char *buf,struct nntpsv *mp));
static int doxovercmd __ARGS((char *buf,struct nntpsv *mp));
static int restreql __ARGS((register char *w,register char *s));
static int check_spaces __ARGS((register char *str, int count));
static int32 make_nntime __ARGS((struct date *d,struct time *t,char *str));
static int ngmatcha __ARGS((int (*func)(char *,char *),int dflt,struct g_list *ngspec,struct g_list *matchlist));
static int get_path __ARGS((char *group,struct nntpsv *mp));
static int get_pointer __ARGS((char *group,struct nntpsv *mp));
static FILE *open_message __ARGS((struct nntpsv *mp,FILE *f));
static int get_id __ARGS((char *bp,struct nntpsv *mp));
static int garbled __ARGS((FILE *inf,FILE *outf));
static int donewnews __ARGS((char *string,struct nntpsv *mp));
static int donewgroups __ARGS((char *string,struct nntpsv *mp));
static int dogroup __ARGS((struct nntpsv *mp,char *buf));
static int check_grp __ARGS((struct nntpsv *mp));
static int get_next __ARGS((struct nntpsv *mp));
static int get_last __ARGS((struct nntpsv *mp));
static void dopoll __ARGS((void *p));
static int get_article2 __ARGS((struct nntpsv *mp,char *id));
/*static int post_article __ARGS((struct nntpsv *mp));*/
static int donnprofile __ARGS((int argc,char *argv[],void *p));
static int chk_access __ARGS((int s));
static int donnfirstpoll __ARGS((int argc,char *argv[],void *p));
static void make_time_string(char *string,time_t *timep);
static void news2mail __ARGS((FILE *f, char *nwsgrps));
static int statnew __ARGS((FILE *artidf,struct nntpsv *cb,struct Servers *sp,char *buf));
static void nntppoll __ARGS((int unused, void *cb1, void *p));
static void nntpserv __ARGS((int s, void *unused, void *p));
static int prompting_read __ARGS((char *prompt, char *buf, int buflen));
  
/***************************************************************************/
  
#undef CONTROL                  /* reverse NNTP function (not implemented yet) */
#define LINE    80
#ifdef LZW
int LzwActive = 1;
#endif

extern long UtcOffset;     /* UTC - localtime, in secs */  
static int16 Nntpquiet = 0;
static int16 NnIhave = 0;
int Nntpaccess = 0;
static int Nntpfirstpoll = 5;
int Filecheck = 0;              /* flag if file system has been checked */
int NntpUsers = 0;		/* count of active connections */
static int Nntpmaxcli = 0;      /* max simultaneous nntp clients, or 0 */
static int Nntpclicnt = 0;      /* current client count */
  
#ifndef NNTPS_TIMEOUT
#define NNTPS_TIMEOUT  3600  /* seconds */
#endif
#ifndef NNTP_TIMEOUT
#define NNTP_TIMEOUT  900  /* seconds */
#endif

struct post *Post = NULLPOST;
struct Servers *Nntpserver = NULLSERVER;
  
static char *Host       = NULLCHAR;
static char NEol[]      = ".\n";
static char replyto[]   = "Reply-To: ";
/* RFC1036 required headers */
static char frm[]       = "From: ";
static char dte[]       = "Date: ";
static char ngrps[]     = "Newsgroups: ";
static char subj[]      = "Subject: ";
static char msgid[]     = "Message-ID: ";
static char pth[]       = "Path: ";
#ifdef MBFWD
static char ATBBS[]     = "@bbs>";
#endif
  
/*############################ NNTP COMMANDS #################################*/
  
/* Command table */
static char * commands[] = {
    "article",
#define ARTICLE_CMD     0
    "body",
#define BODY_CMD        1
    "debug",
#define DEBUG_CMD       2
    "group",
#define GROUP_CMD       3
    "head",
#define HEAD_CMD        4
    "help",
#define HELP_CMD        5
    "ihave",
#define IHAVE_CMD       6
    "last",
#define LAST_CMD        7
    "list",
#define LIST_CMD        8
    "newnews",
#define NEWNEWS_CMD     9
    "next",
#define NEXT_CMD        10
    "post",
#define POST_CMD        11
    "quit",
#define QUIT_CMD        12
    "slave",
#define SLAVE_CMD       13
    "stat",
#define STAT_CMD        14
    "xhdr",
#define XHDR_CMD        15
    "xinfo",
#define XINFO_CMD       16
    "xlzw",
#define XLZW_CMD        17
    "xover",
#define XOVER_CMD       18
    "newgroups",
#define NEWGROUPS_CMD   19
    "date",
#define DATE_CMD	20
    NULLCHAR
};
  
static char artmsg[]    = " Article retrieved - ";
/*static char info[]      = "100 %s Info:\n";*/
static char xinfo[]     = "100 No info available\n";
static char debug[]     = "100 Debug %s\n";
static char nnversion[] = "20%s %s NNTP version %s ready at %.24s %s\n";
static char slave[]     = "202 Slave %s\n";
static char closing[]   = "205 Closing\n";
static char listarticle[]= "211 %u %u %u%s\n";
static char listgroups[]= "215 List of newsgroups follows\n";
static char newgroups[]	= "231 List of new newsgroups follows\n";
static char retrieve[]  = "220 %u%s%shead and body follow\n";
static char head[]      = "221 %u%s%sHead\n";
static char xheader1[]  = "221 %s fields follow\n";
static char xheader2[]  = "221 %u %s header of article %s\n";
static char body[]      = "222 %u%s%sBody\n";
static char statistics[]= "223 %u%s%sStatistics\n";
static char sepcmd[]    = "223 %u %s%srequest text separately\n";
static char xover[]     = "224 data follows\n";
static char newnews_t[] = "230 New news by message id follows\n";
static char transok[]   = "235 Thanks\n";
static char postok[]    = "240 Article posted ok\n";
static char sendart[]   = "335 Send article, end with .\n";
static char sendpost[]  = "340 Send article to be posted, end with .\n";
static char nogroup[]   = "411 No such newsgroup\n";
static char noselect[]  = "412 No newsgroup selected\n";
static char nonext[]    = "421 No next article\n";
static char noprev[]    = "422 No previous article\n";
static char noart[]     = "430 No such article\n";
static char notwanted[] = "435 Article not wanted - do not send it\n";
static char transnotok[]= "437 Article rejected - header garbled - do not try again\n";
static char notallowed[]= "440 Posting not allowed\n";
static char postfailed[]= "441 Posting failed\n";
static char postfailedgrbl[]= "441 Posting failed - header garbled\n";
static char notrecognd[]= "500 Command not recognized\n";
static char badsyntax[] = "501 Syntax error\n";
static char noaccess[]  = "502 I'm not allowed to talk to you\n";
static char error[]     = "503 Command not performed\n";
static char nospace[]   = "503 Insufficient memory\n";
/*static char lowmem[]    = "503 System overloaded\n";*/
static char fatal[]     = "503 Fatal error FILE %s\n";
static char quitcmd[]   = "QUIT\n";
  
/* main directory-creating routine
 * handles special chars in pathname - especially for MSDOS
 * returncode: -1 error; 0 success
 */
static int near
make_dir(path,s)
char *path;
int s;
{
#ifdef MSDOS
    register char *cp;
#endif
  
    if(path == NULLCHAR)
        return -1;
  
#ifdef MSDOS
    while((cp = strchr(path,'\\')) != NULLCHAR)
        *cp = '/';
#endif
  
    if (access(path,0)) {
#ifdef UNIX
        if (mkdir(path,0755)) {              /* 0755 is drwxr-xr-x */
#else
            if (mkdir(path)) {
#endif
                tprintf("Can't create %s: %s\n",path,strerror(errno));
                if(s)
                    usprintf(s,fatal,path);
                return -1;
            }
        }
        return 0;
    }
  
/* main message-opening routine
 * returncode: NULLFILE if error; filepointer success */
  
    static FILE *
    open_message(mp,f)
    struct nntpsv *mp;
    FILE *f;
    {
        char line[LineLen];
  
    /* mp already checked */
  
        if(f != NULLFILE)
            fclose(f);
  
        sprintf(line,"%s/%u",mp->path,mp->pointer);
  
        if ((f = open_file(line,READ_TEXT,0,0)) == NULLFILE)
            usputs(mp->s,"430 Error opening message - no such article.\n");
  
        return f;
    }
  
/* file-receiving routine
 * returncode: -1 if error or 'recvline' faults; 0 success; 1 if blank line */
  
    static int
    recv_file(fp,s)
    FILE *fp;
    int s;
    {
        char line[LineLen];
        char *p;
        int check = 0;
        int continued = 0;  /* n5knx - accommodate lines > LineLen chars long */
  
        for (;;) {
#if NNTP_TIMEOUT
            j2alarm(NNTP_TIMEOUT*1000);
#endif
            if (recvline(s,line,LineLen) == -1) {
#if NNTP_TIMEOUT
                j2alarm(0);
#endif
                return -1;
            }
#if NNTP_TIMEOUT
            j2alarm(0);
#endif
            p = line;
            if (!continued && *p == '.')
                if (*++p == '\n')
                    return 0;  /* else leading .. now a single . */

            continued = (strchr(p,'\n') == NULLCHAR);
            rip(p);
  
            if(!check) {                    /* only enabled on first line! */
                check = 1;
                if (*p == '\0')          /* check for blank line */
                    return 1;
            }
            fprintf(fp,"%s",p);
            if (!continued) fputc('\n', fp);
        }
    }
  
/* checks incoming article-id against existing articles
 * returncode: -1 if error; 1 if article exists; 0 no article found.
 * We may modify the input ID string iff MBFWD and it is from a pbbs.
 */
  
/* Now also makes a crude format check - OH2BNS */
  
    static int
    check_article(id)
    char *id;
    {
        char *p, *p1, line[LineLen];
        FILE *f;
        register int retcode;
  
        if(id == NULLCHAR || (p = strchr(id,'<')) == NULLCHAR
            || (f = open_file(History,READ_TEXT,0,1)) == NULLFILE)
            return -1;
  
        p1 = p;
        while (*p1 != '\0' && *p1 != '>' && !isspace(*p1))
            p1++;
        if (*p1 != '>' || *++p1 != '\0')
            return -1;
  
#ifdef MBFWD
        /* KA1NNN/N5KNX: Prevent dupes from pbbs msgs arriving by different paths */
        if((p1 = strchr(p,'@')) != NULLCHAR) {
            /* A trailing ".bbs" indicates that the Message-ID was generated
             * from a PBBS style message, and not an RFC-822 message.
             */
            if(stricmp(p1+strlen(p1)-5, ".bbs>") == 0) {
                strcpy(p1,ATBBS);  /* NOTE WE MODIFIED THE INPUT ID STRING */
            }
        }
#endif
        for(;;) {
            if (fgets(line,LineLen,f) == NULL) {
                retcode=0;
                break;
            }
  
            if (!strncmp(line,p,strlen(p))) {
                retcode=1;
                break;
            }
            pwait(NULL);     /* let other processes run */
        }
        fclose(f);
        return retcode;
    }
  
/* checks for not valid chars in a line
 * returncode: 0 if valid; 1 if invalid */
    static int
    check_blank(bp)
    char *bp;
    {
        if (strpbrk(bp,"!@#$%^&*()_+=<>,./?~`[]{}\\|0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") == NULL)
            return 1;
        return 0;
    }
  
/* main file-checking routine
 * returncode: -1 if error; 0 success */
    static int
    check_file(path)
    char *path;
    {
        if(path == NULLCHAR)
            return -1;
        if(access(path,0))
#ifdef UNIX
            return close(creat(path,0644));             /* 0644 is -rw-r--r-- */
#else
        return close(creat(path,S_IWRITE));
#endif
        return 0;
    }
  
/* checks if sufficient spaces exist in given string
 * returncode: -1 if error; 1 success */
    static int
    check_spaces(str, count)
    register char *str;
    int count;
    {
        register char *cp;
  
        if(str == NULLCHAR || (cp = strchr(str,' ')) == NULLCHAR)
            return -1;
 
        for (; count > 1; count--) {
            cp++;
            if (strchr(cp,' ') == NULLCHAR)
                return -1;
        }  
        return 1;
    }

/* 29Dec2004, Maiko, Replaces GOTO 'quit' LABEL */
static int do_quit ()
{
   Filecheck = 0;
   j2tputs("Error in NNTP file system\n");
   return -1;
}
  
/* checks the file-system used for NNTP
 * returncode: -1 if error; 0 success - and "Filecheck" is set to 1 */
    static int
    check_system(void)
    {
        FILE *f;
        char line[LineLen];
        int error = 0;
  
        if (Post == NULLPOST)
            donnprofile(10,NULL,NULL);
  
        if(Filecheck)
            return 0;
  
        error  = make_dir(Forward,0);
        error |= check_file(Pointer);
        error |= check_file(History);
        error |= check_file(Active);
        error |= check_file(Poll);
        error |= check_file(Naccess);
  
        if(error)
            return (do_quit ());
  
        sprintf(line,"%s/fwd.seq",Newsdir);
        if (access(line,0)) {
            if((f = open_file(line,"w+",0,1)) == NULLFILE) {
            	return (do_quit ());
            }
            fputs("1\n",f);
            fclose(f);
        }
  
        sprintf(line,"%s/sequence.seq",Mailqdir);
        if (access(line,0)) {
            if((f = open_file(line,"w+",0,1)) == NULLFILE) {
            	return (do_quit ());
            }
            fputs("1\n",f);
            fclose(f);
        }
  
        Filecheck = 1;

        return 0;
    }
  
/* handles the response code of an incoming msg
 * returncode: -1 error; 0 no code; value of response code on success */
  
    static int
    getreply(cb)
    struct nntpsv *cb;
    {
        int response;
  
#if NNTP_TIMEOUT
        while(j2alarm(NNTP_TIMEOUT*1000),(recvline(cb->s,cb->buf,LineLen) != -1)) {
#else
        while(recvline(cb->s,cb->buf,LineLen) != -1) {
#endif
#if NNTP_TIMEOUT
            j2alarm(0);
#endif
        /* skip informative messages and blank lines */
            if(cb->buf[0] == '\0' || cb->buf[0] == '1')
                continue;
  
            sscanf(cb->buf,"%d",&response);
            return (response < 500) ? response : -1;
        }
#if NNTP_TIMEOUT
        j2alarm(0);
#endif
        return -1;
    }

/* 07Jun2021, Maiko (VE4KLM), Error logging (cuts duplicate code) */
void nntp_err_log (int soc, char *msg, char *data)
{
	log (soc, "NNTP: %s", msg);
	log (soc, "[%s]", data);
}
  
/* returncode: -1 error; 1 success; 0 no entry */
  
    static int
    get_path(group,mp)
    char *group;
    struct nntpsv *mp;
    {
        FILE *f;
        char line[LineLen], *cp, *cp1;
  
        if(group == NULLCHAR || mp == NULLNNTPSV
            || (f = open_file(Pointer,READ_TEXT,0,1)) == NULLFILE)
            return -1;
  
        group++;
        for (;;) {
            if (fgets(line,LineLen,f) == NULL)
                break;
  
            if (strcspn(line," ") != strlen(group))
                continue;
  
            if (strnicmp(group,line,strlen(group)) == 0)
	    {
		/* 07Jun2021, Maiko (VE4KLM), Adding more error checks */
                // cp = (strchr(line,' ')) + 1;
                cp = strchr(line,' ');
		if (cp == NULLCHAR)
		{
			nntp_err_log (mp->s, "get_path, missing delimiter", line);
			continue;
		}
		cp++;
		if ((cp1 = strchr(cp, ' ')) != NULLCHAR)
                    *cp1 = '\0';  /* drop date created */
  
                if (mp->path != NULLCHAR)
                    free(mp->path);
  
                mp->path = j2strdup(cp);
                rip(mp->path);
                fclose(f);
                return 1;
            }
        }
        fclose(f);
        return 0;
    }
  
/* checkes if path to given article exists
 * returncode: -1 error; 1 success; 0 no path */
  
    static int
    get_path2(art)
    struct article *art;
    {
        FILE *f;
        char line[LineLen], *p, *cp1;
  
        if(art->group == NULLCHAR
            || (f = open_file(Pointer,READ_TEXT,0,1)) == NULLFILE)
            return -1;
  
        p = art->group;
        art->path = NULLCHAR;
  
        for (;;) {
            if (fgets(line,LineLen,f) == NULL)
                break;
  
            if (strcspn(line," ") != strlen(p))
                continue;
  
            if (strnicmp(p,line,strlen(p)) == 0)
	    {
		/* 07Jun2021, Maiko (VE4KLM), Adding more error checks */
                // p = (strchr(line,' ')) + 1;
                p = strchr(line,' ');
		if (p == NULLCHAR)
		{
			nntp_err_log (-1, "get_path2, missing delimiter", line);
			continue;
		}
		p++;

		if ((cp1 = strchr(p, ' ')) != NULLCHAR)
                    *cp1 = '\0';  /* drop date created */
  
                if(art->path != NULLCHAR)
                    free(art->path);
  
                art->path = j2strdup(p);
                rip(art->path);
                fclose(f);
                return 1;
            }
        }
        fclose(f);
        return 0;
    }
  
/* returncode: -1 if error; 1 success; 0 no pointer */
  
    static int
    get_pointer(group,mp)
    char *group;
    struct nntpsv *mp;
    {
        FILE *f;
        char line[LineLen], *p;
  
        if(group == NULLCHAR || mp == NULLNNTPSV
            || (f = open_file(Active,READ_TEXT,0,1)) == NULLFILE)
            return -1;
  
        group++;
  
        for (;;) {
            if (fgets(line,LineLen,f) == NULL)
                break;
  
            if (strcspn(line," ") != strlen(group))
                continue;
  
            if (strnicmp(group,line,strlen(group))==0)
	    {
		/* 07Jun2021, Maiko (VE4KLM), Adding more error checks */
                p = strchr(line,' ');
		if (p == NULLCHAR)
		{
			nntp_err_log (mp->s, "get_pointer, missing delimiter", line);
			continue;
		}
                mp->last = (unsigned)atoi(p);
                p++;
                mp->first = (unsigned)atoi(strchr(p,' '));
                mp->pointer = (mp->first > mp->last ) ? 0 : mp->first;
                fclose(f);
                return 1;
            }
        }
        fclose(f);
        return 0;
    }
  
/* creating path to a new newsgroup
 * handling of "." and "\" in pathnames especially MSDOS */
/* returncode: -1 error; 0 success */
  
    static int
    make_path(group)
    char *group;
    {
        FILE *f;
        char *cp, *cp1, *cp2;
        time_t currtime;
        char time_string[15];
        int got_it;
  
        if (group == NULLCHAR
            || (f = open_file(Pointer,APPEND_TEXT,0,1)) == NULLFILE)
            return -1;
  
        got_it = 0;
        cp = mallocw(strlen(Newsdir)+strlen(group)+2);  /* '/' + '\0' */
        cp1=j2strdup(group);
  
        for(;;) {
            if((cp2 = strchr(cp1,'.')) != NULLCHAR)
                *cp2 = '\0';
            else
                got_it = 1;
  
            sprintf(cp,"%s/%s",Newsdir,cp1);
  
            if(make_dir(cp,0)) {
                fclose(f);
                free(cp);
                free(cp1);
                return -1;
            }
  
            if(got_it) {
                time(&currtime);
                make_time_string(time_string,&currtime);  /* uses localtime() */
                fprintf(f,"%s %s %s\n",group,cp,time_string);
                fclose(f);
                free(cp);
                free(cp1);
                return 0;
            }
            *cp2 = '/';
        }
    }
  
/* 29Dec2004, Replaces the GOTO 'errxit' LABEL */  
static int doerrxit ()
{
  rmlock(Active, NULLCHAR);
  return -1;
}
    static int
    update_list(a)
    struct nntpsv *a;
    {
        FILE *f, *t;
        char *p, *p1, l2[LineLen];
        int x=0;
  
        while(mlock(Active, NULLCHAR)) { /* since Active is updated elsewhere */
            j2pause(1000);
            if (++x == 60) {
                log(-1, "NNTP: can't update %s due to lock", Active);
                return -1;   /* can't lock, so reject (what else? n5knx) */
            }
        }

        if((f = open_file(Active,READ_TEXT,a->s,0)) == NULLFILE)
			return (doerrxit ());
  
        if ((t = temp_file(0,1)) == NULLFILE) {
            fclose(f);
			return (doerrxit ());
        }
  
        p1 = a->ap->group;
        a->ap->number = 0;
  
        for (;;) {
            if (fgets(a->buf,LineLen,f) == NULL)
                break;
  
            a->ap->tmpu = strcspn(a->buf," ");
		/* 07Jun2021, Maiko (VE4KLM), Not sure I would call this an error check, but makes sense still */
		if ((a->ap->tmpu) < 1)
		{
			nntp_err_log (a->s, "update_list, missing delimiter", a->buf);
			continue;
		}
            strncpy(l2,a->buf,a->ap->tmpu);
            l2[a->ap->tmpu] = '\0';
  
		/* 07Jun2021, Maiko (VE4KLM), Adding more error checks */
            //p = strrchr(a->buf,' ') + 1;    /* the char after the last space */
            p = strrchr(a->buf,' ');
		if (p == NULLCHAR)
		{
			nntp_err_log (a->s, "update_list, missing delimiter", a->buf);
			continue;
		}
			p++;
            if (*p == 'n') {            /* posting to this group not allowed */
                fputs(a->buf,t);
                continue;
            }
  
            if (strcmp(p1,l2) == 0)
	    {
		/* 07Jun2021, Maiko (VE4KLM), Adding more error checks */
                //p = strchr(a->buf,' ') + 1;
                p = strchr(a->buf,' ');
		if (p == NULLCHAR)
		{
			nntp_err_log (a->s, "update_list, missing delimiter", a->buf);
			continue;
		}
		p++;
                a->ap->number = (unsigned)atoi(p);
                (a->ap->number)++;
                p = strchr(p,' ');
		/* 07Jun2021, Maiko (VE4KLM), Adding more error checks */
		if (p == NULLCHAR)
		{
			nntp_err_log (a->s, "update_list, missing delimiter", a->buf);
			continue;
		}
                fprintf(t,"%s %5.5u%s",p1,a->ap->number,p);
            } else
                fputs(a->buf,t);
        }
  
        fclose(f);
        if (ferror(t))
		{
            fclose(t);
			return (doerrxit ());
		}
        rewind(t);
  
        if ((f = open_file(Active,WRITE_TEXT,a->s,0)) == NULLFILE) {
            fclose(t);
			return (doerrxit ());
        }
  
        for(;;) {
            pwait(NULL);
            if (fgets(a->buf,LineLen,t) == NULL)
                break;
  
            fputs(a->buf,f);
        }
  
        fclose(t);
  
/* Creating newsgroups by posting is now denied - OH2BNS
 *
 *   if (a->ap->number == 0) {
 *       make_path(a->ap->group);
 *       a->ap->number = 1;
 *       fprintf(f,"%s 00001 00001 y\n",a->ap->group);
 *   }
 */
  
        fclose(f);
        rmlock(Active, NULLCHAR);
        return (a->ap->number);
    }
  
/* returncode: -1 error; 0 success */
  
    static int
    dup_f(in,out,mp)
    FILE *in;
    FILE *out;
    struct nntpsv *mp;
  
/* Path: bug in col 1 of article body bug fixed by G4JEC 901019....*/
  
    {
        char *p;
        int blank_line_flag = 1;
  
        for(;;) {
            pwait(NULL);
            if (fgets(mp->buf,LineLen,in) == NULL)
                return 0;
  
            if (blank_line_flag)
                if (strnicmp(mp->buf,pth,5) == 0)
				{
                    //p = strchr(mp->buf,' ') + 1;
                    p = strchr(mp->buf,' ');
		/* 07Jun2021, Maiko (VE4KLM), Adding more error checks */
		if (p == NULLCHAR)
			nntp_err_log (mp->s, "dup_f, missing delimiter", mp->buf);
		else
		{
					p++;
                    fprintf(out,"%s%s!",pth,Host);  /* making longer line, */
                    fputs(p, out);     /* so allow for total len > LineLen */
		}
                    continue;
                }
  
#ifdef notdef
        /* oh oh - nntpserv is modifying articles....*/
        /* n5knx: this fails since line would be ".\n" ... let's just forget it */
  
            if (strlen(mp->buf) == 1 && mp->buf[0] == '.')
                continue;
#endif
  
            fputs(mp->buf,out);
  
        /* ! removed. Now dup_f searches the whole header for "Path: ",
         * not only the first line - OH2BNS */
  
            if(check_blank(mp->buf))
                blank_line_flag = 0;
        }
    }
  
/* returncode: < 1 if error; 1 success */
  
    static int
    dofwd(mp,f,history)
    struct nntpsv *mp;
    FILE *f;
    FILE *history;
    {
        FILE *fwd;
  
        if(mp == NULLNNTPSV)
            return -1;
  
        sprintf(mp->buf,"%s/fwd.seq",Newsdir);
  
        if ((fwd = open_file(mp->buf,"r+",mp->s,0)) == NULLFILE)
            return -1;
  
        fgets(mp->buf,LineLen,fwd);
        mp->hold_i = atoi(mp->buf) + 1;
        fprintf(history," JUNK/%u",mp->hold_i);
        rewind(fwd);
        fprintf(fwd,"%u",mp->hold_i);
        fclose(fwd);
  
        sprintf(mp->buf,"%s/%u",Forward,mp->hold_i);
  
        if ((fwd = open_file(mp->buf,WRITE_TEXT,mp->s,0)) == NULLFILE)
            return -2;
  
        rewind(f);
        dup_f(f,fwd,mp);
        fclose(fwd);
        return 1;
    }
  
  
  
#ifdef CONTROL
  
    static int
    docontrol(f,mp)
    FILE *f;
    struct nntpsv *mp;
    {
        struct head *h;
  
        if(f == NULLFILE || mp == NULLNNTPSV
            || (h = (struct head *)callocw(1,sizeof(struct head))) == NULLHEAD))
            return -1;
  
        rewind(f);
        h->subject = h->from = h->reply_to = h->id = NULLCHAR;
  
        for(;;) {
            pwait(NULL);
            if ((fgets(mp->buf,LineLen,f)) == NULL)
                break;
  
            if (check_blank(mp->buf))
                break;
  
            rip(mp->buf);
  
            if (strncmp(mp->buf,subj,9) == 0)
                h->subject = j2strdup(mp->buf);
  
            if (strncmp(mp->buf,frm,6) == 0)
                h->from = j2strdup(mp->buf);
  
            if (strnicmp(mp->buf,replyto,10) == 0)
                h->reply_to = j2strdup(mp->buf);
  
            if (strnicmp(mp->buf,msgid,12) == 0)
                h->id = j2strdup(strchr(mp->buf,'<'));
        }
  
        if (h->subject != NULLCHAR)
            if (strncmp(h->subject,"Subject: sendme ",16) == 0)
                dosendme(h);
  
        if (h->subject != NULLCHAR)
            free(h->subject);
  
        if (h->from != NULLCHAR)
            free(h->from);
  
        if (h->reply_to != NULLCHAR)
            free(h->reply_to);
  
        if (h->id != NULLCHAR)
            free(h->id);
  
        free((char *)h);
        return 0;
    }
  
#endif

/* 29Dec2004, Replaces the GOTO 'quit' LABEL */
static int do_quit2 (FILE *history, char *from, char *group, struct nntpsv *mp)
{  
   fputc('\n',history);
   fclose(history);
   rmlock(History, NULLCHAR);

   free(from);
   free(group);
   free((char *)mp->ap);

   return (mp->hold_i);
}
    static int
    xfer_article2(f,mp)
    FILE *f;
    struct nntpsv *mp;
    {
        char line[LineLen*2], *p, *group = NULLCHAR, *p1, *from = NULLCHAR;
        FILE *fptr, *history;
        int x=0;
  
#ifdef CONTROL
        int control = 0;
#endif
  
        long currtime;
  
        if(f == NULLFILE || mp == NULLNNTPSV)
            return -1;
  
        while(mlock(History, NULLCHAR)) {
            j2pause(1000);
            if (++x == 60) {
                log(-1, "NNTP: NEWS REJECTED due to history lock");
                return -1;   /* can't lock, so reject (what else? n5knx) */
            }
        }

    /* one last time check the message-id for duplicates */
        if(check_article(mp->id) ||
        (history = open_file(History,APPEND_TEXT,0,1)) == NULLFILE) {
            rmlock(History, NULLCHAR);
            return -1;
        }
  
        for (;;) {     /* obtain From: and Newsgroups: fields from article */
            if (fgets(line,sizeof(line),f) == NULL)
                break;
  
            rip(line);
  
		/* 07Jun2021, Maiko (VE4KLM), Darn it, there's tons of these strchr without error checks !!! */
            if (strnicmp(line,frm,6) == 0)
			{
                //p = strchr(line,' ') + 1;
                p = strchr(line,' ');
		if (p == NULLCHAR)
		{
			nntp_err_log (mp->s, "xfer_article2, missing delimiter", line);
			continue;
		}
				p++;
                from = j2strdup(p);
                if (group != NULLCHAR) break;
                continue;
            }
  
            if (strnicmp(line,ngrps,12) == 0)
			{
                //p = strchr(line,' ') + 1;
                p = strchr(line,' ');
		if (p == NULLCHAR)
		{
			nntp_err_log (mp->s, "xfer_article2, missing delimiter", line);
			continue;
		}
				p++;
                group = j2strdup(p);
                if (from != NULLCHAR) break;
                continue;
            }
        }
  
        time(&currtime);
        make_time_string(line,&currtime);
        fprintf(history,"%s %s", mp->id, line);
  
        mp->hold_i = 0;
        p = group;
  
        if((mp->ap = (struct article *)callocw(1,sizeof(struct article))) == NULLARTICLE)
			return (do_quit2 (history, from, group, mp));
  
        x = 1;
  
        while (x) {
            if ((p1 = strchr(p,',')) != NULLCHAR) {
                p1 = line;
  
                while (*p != ',')
                    *(p1++)=*(p++);
  
                *p1 = '\0';
                p++;
                mp->ap->group = j2strdup(line);
            } else {
                mp->ap->group = j2strdup(p);
                x = 0;
            }
  
            update_list(mp);
  
            if (mp->ap->number > 0) {
                get_path2(mp->ap);
                mp->hold_i = 1;
                sprintf(line,"%s/%u",mp->ap->path,mp->ap->number);
                rewind(f);
  
                if ((fptr = open_file(line,WRITE_TEXT,0,1)) == NULLFILE) {
                    free(mp->ap->group);
					return (do_quit2 (history, from, group, mp));
                }
  
                dup_f(f,fptr,mp);
                fclose(fptr);
                fprintf(history," %s/%d",mp->ap->group,mp->ap->number);
  
#ifdef CONTROL
                if (strcmp(mp->ap->group,"control") == 0)
                    control = 1;
#endif
  
                free(mp->ap->path);
            }
            free(mp->ap->group);
        }
  
        if (mp->hold_i == 0) {
            dofwd(mp,f,history);
            mp->hold_i = 0;
        }
  
        time(&currtime);
  
        if(Nntpquiet < 2) {
            j2tputs("New mail in newsgroup ");
            j2tputs((mp->hold_i) ? group : "JUNK");  /* strlen(group) can be > SOBUF */
            tprintf("\n  from <%s> at %s%s",from,ctime(&currtime),
                (Nntpquiet < 1)  ? "\007" : "");
        }
  
#ifdef NEWS_TO_MAIL
        news2mail(f, group);
#endif

#ifdef CONTROL
        if (control == 1)
            docontrol(f,mp);
#endif
		return (do_quit2 (history, from, group, mp));
    }

/* 29Dec2004, Replace the use of GOTO 'quit' LABEL */
static void do_quit3 (FILE *pf, struct nntpsv *cb, struct Servers *sp)
{
        fclose(pf);
        log(cb->s,"Disconnect NNTP");    /* KA1NNN 4/95 */
        close_s(cb->s);
        NntpUsers--;                     /* KA1NNN 4/95 */
        free(cb->newnews);
        free((char *)cb);
        start_timer(&sp->nntpt);
}
    static void
    nntppoll(unused,cb1,p)
    int unused;
    void *cb1;
    void *p;
    {
        char *cp, line[LineLen];
        struct sockaddr_in fsocket;
        struct nntpsv *cb;
        struct tm  *ltm;
        FILE *f, *f1, *pf;
        long t;
        int r, now, ret;
        struct Servers *sp = (struct Servers *) cb1;
        long currtime;
        char open_time[15];
  
        if (!Filecheck)
            if(check_system())
                return;
  
#ifndef UNIX
        if(availmem() < Memthresh) {
            start_timer(&sp->nntpt);
            return;
        }
#endif
  
    /* check connection window */
  
        time(&currtime);
        ltm = localtime(&currtime);
        now = ltm->tm_hour * 100 + ltm->tm_min;
  
        if (sp->lowtime < sp->hightime) {
        /* doesn't cross midnight */
            if (now < sp->lowtime || now >= sp->hightime) {
                start_timer(&sp->nntpt);
                return;
            }
        } else {
            if (now < sp->lowtime && now >= sp->hightime) {
                start_timer(&sp->nntpt);
                return;
            }
        }
  
        if((pf = open_file(Poll,"r+",0,1)) == NULLFILE ||
        (cb = (struct nntpsv *)callocw(1,sizeof(struct nntpsv))) == NULLNNTPSV) {
            fclose(pf);  /* OK if NULL */
            start_timer(&sp->nntpt);
            return;
        }
  
        cb->fname   = NULLCHAR;
        cb->newnews = NULLCHAR;
        rewind(pf);
  
        for(t = 0L; fgets(line,LineLen,pf) != NULLCHAR; t = ftell(pf)) {
            if((cp = strchr(line,' ')) == NULLCHAR)
                continue;           /* something wrong with this line, skip it */
  
            *cp = '\0';
  
            if(strnicmp(line,sp->name,strcspn(line," ")-1) == 0) {
                rip(cp+1);
                cb->newnews = j2strdup(cp+1);
            /* Prepare to write back the current host and date */
                fseek(pf,t,0);
                break;
            }
        }
  
	if(cb->newnews == NULLCHAR) {
		if (Nntpfirstpoll < 0) {
			cb->newnews = j2strdup("900101 000000");
		} else {	/* from G8FSL */
			cb->newnews = mallocw(15);
			time(&currtime);
			currtime -= Nntpfirstpoll*86400L;
			make_time_string(cb->newnews,&currtime);
		}
	}
  
        fsocket.sin_family = AF_INET;
        fsocket.sin_addr.s_addr = cb->dest = sp->dest;
        fsocket.sin_port = IPPORT_NNTP;
  
        if((cb->s = j2socket(AF_INET,SOCK_STREAM,0)) == -1) {
            NntpUsers++;    /* Because we decrement on quit - KA1NNN 4/95 */
			return (do_quit3 (pf, cb, sp));
        }
  
        sockmode(cb->s,SOCK_ASCII);
  
        if(j2connect(cb->s,(char *)&fsocket,SOCKSIZE) == -1) {
            NntpUsers++;    /* Because we decrement on quit - KA1NNN 4/95 */
			return (do_quit3 (pf, cb, sp));
        }

        log(cb->s,"Connect NNTP");
        NntpUsers++;
        time(&currtime);
        currtime -= 300L;	/* 5 mins rewind bodge against clock errs, G8FSL */
        make_time_string(open_time, &currtime);
  
        if(getreply(cb) == -1)          /* throw away any hello msg */
		{
        	usputs(cb->s,quitcmd);
			return (do_quit3 (pf, cb, sp));
		}
  
    /* IHAVE preparation */

        if(NnIhave && (cb->ihave = tmpfile()) != NULLFILE) {
            sprintf(line,"%s %s",NnIhave == 2 ? "*" : sp->newsgroups,cb->newnews);
  
            if(newnews(line,cb,cb->ihave) < 1) {
                fclose(cb->ihave);
                cb->ihave = NULLFILE;
            }
        }
  
#ifdef LZW
        if (LzwActive)   {
            usprintf(cb->s,"XLZW %d %d\n",Lzwbits,Lzwmode);
            if((ret = getreply(cb)) == 235)   {       /* eat negative response */
                lzwinit(cb->s,Lzwbits,Lzwmode);
            }
        }
#endif
  
        if(NnIhave && cb->ihave != NULLFILE) {
            rewind(cb->ihave);
  
            while(fgets(line,sizeof(line),cb->ihave),!feof(cb->ihave)) {
                pwait(NULL);
                usprintf(cb->s,"IHAVE %s",line);
  
                if((ret = getreply(cb)) == -1)
                    break;
  
                if(ret != 335)
                    continue;
  
                rip(line);
  
                if(doarticlecmd(line,cb,0) == 0) {
  
                    if((ret = getreply(cb)) == -1)
                        break;
  
                    continue;
                }
                else    /* error sending article, we must terminate it and hope recipient finds it garbled */
                    usputs(cb->s,NEol);
  
                if((ret = getreply(cb)) != 235)
                    continue;
            }
  
            fclose(cb->ihave);
        }
  
#ifdef NN_INN_COMPAT
        usputs(cb->s,"mode reader\n");   /* cause INNd to spawn nnrpd */
        getreply(cb);                    /* throw away any response */
#endif

#ifdef XXX
        usputs(cb->s,"SLAVE\n");
        if(getreply(cb) != 202)
		{
        	usputs(cb->s,quitcmd);
			return (do_quit3 (pf, cb, sp));
		}
#endif
  
        cb->slave = 1;
  
        if ((f = temp_file(0,1)) == NULLFILE)
		{
        	usputs(cb->s,quitcmd);
			return (do_quit3 (pf, cb, sp));
		}
  
#ifdef TESTNN_USESTAT
        if (1) {
#else
        usprintf(cb->s,"NEWNEWS %s %s%s\n",
            (sp->newsgroups==NULLCHAR ? "*" : sp->newsgroups), cb->newnews,
            (UtcOffset ? "" : " GMT") );
  
        if(getreply(cb) != 230) {
#endif
#ifdef NN_USESTAT
/* if NEWNEWS isn't supported, we can try {GROUP, STATi} foreach group */
            if (statnew(f, cb, sp, line) == -1)
#endif /* NN_USESTAT */
			{
        		fclose(f);
        		usputs(cb->s,quitcmd);
				return (do_quit3 (pf, cb, sp));
			}
        }
  
        else if(recv_file(f,cb->s) == -1) {
       		fclose(f);
       		usputs(cb->s,quitcmd);
			return (do_quit3 (pf, cb, sp));
        }
  
        if ((f1 = temp_file(cb->s,1)) == NULLFILE) {
       		fclose(f);
       		usputs(cb->s,quitcmd);
			return (do_quit3 (pf, cb, sp));
        }
  
        rewind(f);
  
        for(;;) {    /* foreach article-id in file (f) */
            if (fgets(cb->buf,LineLen,f) == NULL)
                break;
  
            rip(cb->buf);
  
            if (strcmp(cb->buf,".") == 0)
                break;
  
#ifdef MBFWD
            /* check_article() might modify the art id, which we must avoid */
            cp=j2strdup(cb->buf);
            if (check_article(cp) != 0) {
                free(cp);
                continue;
            }
            free(cp);
#else
            if (check_article(cb->buf) != 0)
                continue;
#endif

            usprintf(cb->s,"ARTICLE %s\n",cb->buf);
  
            for (;;) {
#if NNTP_TIMEOUT
                j2alarm(NNTP_TIMEOUT*1000);
#endif
                if ((r = recvline(cb->s,cb->buf,LineLen)) == -1) {
#if NNTP_TIMEOUT
                    j2alarm(0);
#endif
                    break;
                }
#if NNTP_TIMEOUT
                j2alarm(0);
#endif
  
                rip(cb->buf);
  
                if(!isdigit(cb->buf[0])) {
                    r = -1;
                    continue;
                } else {
                    r = atoi(cb->buf);
                    break;
                }
            }
  
            if(r == -1)
                break;
  
            if (r == 220) {
                recv_file(f1,cb->s);
                rewind(f1);
                for ( ;; ) {
                    if (fgets(cb->buf,LineLen,f1) == NULL)
                        break;
  
                    rip(cb->buf);
  
                    if (strnicmp(cb->buf,msgid,12) == 0) {
                        cb->id = j2strdup((strchr(cb->buf,' ')) + 1);
                        break;
                    }
                }
  
                rewind(f1);
                xfer_article2(f1,cb);
                free(cb->id);
            }
  
            fclose(f1);
  
            if ((f1 = temp_file(cb->s,1)) == NULLFILE)
			{
        		fclose(f);
        		usputs(cb->s,quitcmd);
				return (do_quit3 (pf, cb, sp));
			}
            pwait(NULL);
        }
        fclose(f1);
    /*
     * update pollfile, writing back the opening date and time
     */
        fprintf(pf,"%s %s\n", sp->name, open_time);
  
        fclose(f);
        usputs(cb->s,quitcmd);

        /* if (recvline(cb->s,cb->buf,LineLen) == -1); pointless, right? */

		return (do_quit3 (pf, cb, sp));
    }
  
    static int
    ngmatcha(func,dflt,ngspec,matchlist)
    int     (*func)(char *,char *);
    int     dflt;
    struct g_list *ngspec;
    struct g_list *matchlist;
    {
        register int    match;
        char   *cp;
        struct g_list   *m,*n;
  
        match = dflt;
        m = matchlist;
        for(;;) {
            if ((cp = strchr(m->str,'/')) != NULLCHAR)
                *cp = '\0';
            n = ngspec;
            for (;;) {
                if (n->str[0] == '!') {      /* Handle negation */
                    if ((*func)(n->str+1, m->str)) {
                        match = 0;
                    }
                } else {
                    if ((*func)(n->str, m->str)) {
                        match = 1;
                    }
                }
                if (n->next == NULLG)
                    break;
                else
                    n=n->next;
            }
            if (m->next == NULLG)
                break;
            else
                m=m->next;
        }
        return (match);
    }
  
  
  
    static int
    restreql(w, s)
    register char *w;
    register char *s;
    {
        while (*s && *w) {
            switch (*w) {
                case '*':
                    for (w++; *s; s++)
                        if (restreql(w, s))
                            return 1;
                    break;
                default:
                    if (*w != *s)
                        return 0;
                    w++, s++;
                    break;
            }
        }
        if (*s)
            return 0;
  
        while (*w)
            if (*w++ != '*')
                return 0;
  
        return 1;
    }
  
/* converts timestring to unix-compatible structure
 * str -> YYMMDD HHMMSS [GMT]
 * returncode: 0 (!!) if error; > 0 success */
  
    static int32
    make_nntime(d,t,str)
    struct date *d;
    struct time *t;
    char *str;
  
    {
  
        register char *cp;
        char tmp[3];
        int32 tim;

        if(str == NULLCHAR)
            return 0L;
        tmp[2] = '\0';
        cp = str;
        strncpy(tmp,cp,2);
        d->da_year = atoi(tmp)+1900;
        if (d->da_year < 1950)
            d->da_year += 100;  /* next century */
        if (d->da_year < 1980)
            d->da_year = 1980;
        cp+=2;
        strncpy(tmp,cp,2);
        d->da_mon = atoi(tmp);
        if (d->da_mon < 1 || d->da_mon > 12)
            return 0L;
        cp+=2;
        strncpy(tmp,cp,2);
        d->da_day = atoi(tmp);
        if (d->da_day < 1 || d->da_day > 32)
            return 0L;
        cp+=3;
        strncpy(tmp,cp,2);
        t->ti_hour = atoi(tmp);
        if (((t->ti_hour >=1) && (t->ti_hour <= 23)) || (t->ti_hour==0))
	{
            cp+=2;
            strncpy(tmp,cp,2);
            t->ti_min = atoi(tmp);
        }
	else
	{
            return 0L;
        }
        if (((t->ti_min >= 1) && (t->ti_min <= 59)) || (t->ti_min==0))
	{
            cp+=2;
            strncpy(tmp,cp,2);
            t->ti_sec = atoi(tmp);
        }
	else
	{
            return 0L;
        }
        if (((t->ti_sec >= 1) && (t->ti_sec <= 59)) || (t->ti_sec==0))
	{
            t->ti_hund = 0;
            tim = dostounix(d,t);
            if (*(cp+3) == 'G')
		tim -= UtcOffset;   /* remove bias applied by dostounix */
            return tim;
        }
	else
	{
            return 0L;
        }
    }
  
#ifdef NN_USESTAT
/* returncode: -1 if error, else 0 */
static int
statnew(artidf, cb, sp, buf)
FILE *artidf;
struct nntpsv *cb;
struct Servers *sp;
char *buf;              /* LineLen chars long */
{
    char *p, *pp, *cp, *cp1;
    long t;
    unsigned long hinumber, lonum, hinum;
    FILE *f;

    if (sp->newsgroups == NULLCHAR) return -1;
    p=cp=j2strdup(sp->newsgroups);
    for (;cp;cp=cp1) {  /* for each cp->group_name */
        if ((cp1 = strchr(cp,',')) != NULLCHAR ) {
            *cp1++ = '\0';
        }

        if (get_path(cp-1, cb) == 1) {  /* alloc path to ng dir in cb->path */
            hinumber = 0UL;
            sprintf(buf,"%s/host.rc",cb->path);
            /* the read-update open works for the POLL file...no locks needed?! */
            if((f = open_file(buf,"r+",0,0)) != NULLFILE) {
                for(t=0L; fgets(buf,LineLen,f) != NULLCHAR; t=ftell(f)) {
                    if ((pp = strchr(buf, ' ')) == NULLCHAR) continue;  /* bad fmt */
                    *pp++ = '\0';
                    if(stricmp(buf,sp->name) == 0) {  /* hosts match */
                        hinumber = (unsigned long)atol(pp);
                        /* Prepare to write back the current host and highest article# */
                        fseek(f,t,0);
                        break;
                    }
                }
            }
            else if((f = open_file(buf,WRITE_TEXT,0,1)) == NULLFILE)  /* create? */
                continue;

            usprintf(cb->s, "GROUP %s\n", cp);
            if(getreply(cb) == 211) {
                /*
				05Jul2016, Maiko, compiler warning assign suppress + len mod
				 sscanf(cb->buf, "%*d %*lu %lu %lu", &lonum, &hinum);
				*/
                sscanf(cb->buf, "%*d %*u %lu %lu", &lonum, &hinum);
                if (hinumber != hinum) {  /* work to do... */
                    if ((hinumber > hinum)  /* ng was reset? */
                        || (lonum > hinumber)) /* articles lost/expired? */
                        hinumber = lonum;
                    for(;hinumber <= hinum;hinumber++) {  /* for each new article */
                        usprintf(cb->s, "STAT %lu\n", hinumber);
                        if(getreply(cb) == 223) {
                            if ((pp = strchr(cb->buf,'>')) == NULLCHAR) continue;
                            *(++pp) = '\0';
                            if ((pp = strchr(cb->buf,'<')) == NULLCHAR) continue;
                            fprintf(artidf,"%s\n", pp);  /* save article-id in file */
                        }
                    }
                    hinumber--;
                }
            }
  
            fprintf(f,"%s %010lu\n", sp->name, hinumber);
            fclose(f);
        } /* end processing for ng */
    }  /* for next ng name */

    free (p);
    return 0;
}
#endif /* NN_USESTAT */

/* 29Dec2004, Replacing GOTO 'quit' LABELS with functions */
static int do_quit4 (int all, struct g_list *histp, struct g_list *ngp,
						struct nntpsv *mp, int newsavail)
{
	struct g_list *ptr;

	if (!all)
	{
		while (histp != NULLG)
		{
           ptr = histp->next;
           free(histp->str);
           free(histp);
           histp = ptr;
		}

		while (ngp != NULLG)
		{
           ptr = ngp->next;
           free(ngp->str);
           free(ngp);
           ngp = ptr;
		}
	}

	free(mp->datest);
	free(mp->timest);

	return newsavail;
}

/* returncode: -1 if error; 0 if no new news; 1 new news available */
  
    static int
    newnews(string,mp,f)
    char *string;
    struct nntpsv *mp;
    FILE *f;
  
    {
        register int i,j;
        char *cp, *cp1, line[LineLen];
	char *groups; /* 06Jun2021, Maiko (VE4KLM), replaces groups[LineLen]; */
        struct g_list *ng, *hist, *ngp=NULLG, *histp=NULLG, *ptr;
        FILE *f1;
        int all=1, newsavail=0, len = 0;
  
        if (check_spaces(string,2) == -1)
            return -1;
  
        cp = string;
        i = 1;
  
        while (*(cp++) > 32)
            i++;
        if (strlen(cp) < 13)
            return -1;

	/*
	 * 06Jun2021, Maiko (VE4KLM), no more local buffer having
	 * a hardcoded length at compile time, this is better way,
	 * so we don't have to worry about buffer overflows, etc.
	 *  (and make sure to free groups pointer - done later)
	 *
	 * The log entry is just because I am curious now ...
	 *
	 * 09Jun2021, Maiko (VE4KLM), bump logging up to 500+
	 */
	len = strlen (string);

	if (len > 500 || i > 500)
		log (mp->s, "large remote group string %d (%d)", len, i);

	groups = malloc (i);
	memcpy (groups, string, i-1);
        groups[i-1] = '\0';

        if(strcmp(groups,"*") != 0)
            all = 0;
  
        mp->datest = (struct date *)callocw(1,sizeof(struct date));
        mp->timest = (struct time *)callocw(1,sizeof(struct time));
        j2gettime(mp->timest);
        j2getdate(mp->datest);
  
        if ((mp->unixtime = make_nntime(mp->datest,mp->timest,cp)) == 0
            || (mp->unixtime == -1L))
			return (do_quit4 (all, histp, ngp, mp, newsavail));
  
        if (!all) {
            ng = ngp = (struct g_list *)callocw(1,(sizeof(struct g_list)));
            cp = groups;
  
            for (;;) {

                if ((cp1 = strchr(cp,',')) == NULLCHAR ) {
                    ng->str = j2strdup(cp);
                    ng->next = NULLG;
                    break;
                }
                j = strcspn(cp,",");
		/* 06Jun2021, Maiko (VE4KLM), perhaps the COMMA is missing (or corrupt stream) ? */
		if (j < 1)
		{
			nntp_err_log (mp->s, "remote groups, missing delimiter", groups);
			break;
		}

                ng->str = (char *)callocw(1,j+1);
                strncpy(ng->str,cp,j);
                ng->str[j] = '\0';
                ng->next = (struct g_list *)callocw(1,sizeof(struct g_list));
                ng = ng->next;
                cp1 = strchr(cp,',');
                if (cp1 != NULLCHAR)
                    cp = cp1 + 1;
                else
                    break;
            }
        }

	free (groups);	/* 06Jun2021, Maiko (VE4KLM), no more local buffer */
  
        if ((f1 = open_file(History,READ_TEXT,0,1)) == NULLFILE)
			return (do_quit4 (all, histp, ngp, mp, newsavail));
  
        for (;;) {
            pwait(NULL);
            if (fgets(line,LineLen,f1) == NULL)
                break;
            rip(line);
  
            if (!all) {
                while (histp != NULLG) {
                    ptr = histp->next;
                    free(histp->str);
                    free(histp);
                    histp = ptr;
                }

                for(i = 3, cp = line; i; --i)
		{
                    cp = strchr(cp,' ');
		/*
		 * 06Jun2021, Maiko (VE4KLM), People should be checking for null
		 * pointers on the strchr() call, never mind not on 'that + 1'.
		 *
		 * Could a 'bad' history file entry be causing a crash ? Confirmed
		 * by Jean later in the evening - note the lack of a space delimiter
		 * between the 161020 and ampr.check in the log submission below :
		 *
           21:49:21  44.76.0.194:1096 - large remote group string 418 (405)
           21:49:22  44.76.0.194:1096 - history file - missing space delimiter, corrupt entry ?
           21:49:22  44.76.0.194:1096 - entry [<16804680@jnos.ve2pkt.ampr.org> 210604 161020ampr.check/89]
		 *
		 * Question is WHY or HOW did it make it to the history file ?
		 *
 		 * Thanks Jean (VE2PKT) for reporting this, for all your GDB dumps,
		 * and for spending your 'entire day' testing the new code :]
		 */
			if (cp == NULLCHAR)
				break;
                    cp++;
		}

		/* 06Ju2021, Maiko (VE4KLM), check for NULL, corrupt entry will crash */
		if (cp == NULLCHAR)
		{
			nntp_err_log (mp->s, "history entry, missing delimiter", line);
			continue;
		}

                histp = (struct g_list *)callocw(1,sizeof(struct g_list));
                hist = histp;
  
                for (;;) {
                    if ((cp1 = strchr(cp,' ')) == NULLCHAR) {
                        hist->str = j2strdup(cp);
                        hist->next = NULLG;
                        break;
                    }
  
                    j = strcspn(cp," ");
		/* 06Jun2021, Maiko (VE4KLM), perhaps the space is missing */
		if (j < 1)
		{
			nntp_err_log (mp->s, "history entry, missing delimiter", line);
			break;
		}
                    hist->str = (char *)callocw(1,j+1);
                    strncpy(hist->str,cp,j);
                    hist->str[j] = '\0';
                    hist->next = (struct g_list *)callocw(1,sizeof(struct g_list));
                    hist = hist->next;
                    cp = cp1 + 1;
                }
                if (!ngmatcha(restreql,0,ngp,histp))
                    continue;
            }

            //cp = strchr(line,' ') + 1;
            cp = strchr(line,' ');
	if (cp == NULLCHAR)
	{
			nntp_err_log (mp->s, "history entry, missing spaces", line);
			break;
	}
	cp++;

            if ((mp->ftime = make_nntime(mp->datest,mp->timest,cp)) == 0) {
                break;  /* bad syntax in History file */
            }

            if ((mp->ftime - mp->unixtime) > 0) {
                cp = line;
                while ( *cp > 32 )
                    fputc(*(cp++),f);
                fputc('\n',f);
                newsavail=1;
            }
        }

        fclose(f1);

		return (do_quit4 (all, histp, ngp, mp, newsavail));
    }

/* handles incoming newgroups-cmd (n5knx, adapted from g8fsl) */
  
    static int
    donewgroups(string,mp)
    char *string;
    struct nntpsv *mp;
  
    {
        char *cp, line[LineLen];
        FILE *f;
  
        cp = string;

        if (check_spaces(cp,1) == -1 || strlen(cp) < 13) {
            usputs(mp->s,badsyntax);
            return -1;
        }

        mp->datest = (struct date *)callocw(1,sizeof(struct date));
        mp->timest = (struct time *)callocw(1,sizeof(struct time));

        if ((mp->unixtime = make_nntime(mp->datest,mp->timest,cp)) == 0 || mp->unixtime == -1L) {
            usputs(mp->s,badsyntax);
            return -1;
        }
        /* mp->unixtime is in UTC       adsb*/

        usputs(mp->s, newgroups);   /* list to follow */

        if ((f = open_file(Pointer,READ_TEXT,0,1)) != NULLFILE) {
            for (;;) {
                if (fgets(line,LineLen,f) == NULL)
                    break;

                rip(line);
                /* skip past ng name & path to time-created */
                if ((cp = strchr(line,' ')) == NULLCHAR)
                    continue;
                if ((cp = strchr(++cp,' ')) == NULLCHAR)
                    continue;
                cp++;

                /* Get time newsgroup created. Entries in file are in local time,
                   but make_nntime() compensates */

                if ((mp->ftime = make_nntime(mp->datest,mp->timest,cp)) == 0)
                    continue;

                if ((mp->ftime - mp->unixtime) > 0) {
                    cp = line;
                    while ( *cp > ' ' )
                        usputc(mp->s,*(cp++));
                    usputc(mp->s,'\n');
                }
            }
            fclose(f);
            free(mp->datest);
            free(mp->timest);
        }

        usputs(mp->s, NEol);      /* end of (possibly-empty) list */
        return 0;
    }

/* handles incoming newnews-cmd
 * returncode: -1 if error; 0 no groups or bad syntax; 1 success */
  
    static int
    donewnews(string,mp)
    char *string;
    struct nntpsv *mp;
  
    {
        FILE *f;
        int ret;
  
        if((f = temp_file(mp->s,1)) == NULLFILE)
            return -1;
  
        ret = newnews(string,mp,f);
  
        switch(ret) {
            case -1:                        /* error in "newnews" routine */
                ret = 0;
                usputs(mp->s,badsyntax);
                break;
            case 0:                         /* no new news */
                usputs(mp->s,newnews_t);
                usputs(mp->s,NEol);
                break;
            default:                        /* new news available, send news file */
                rewind(f);
                usputs(mp->s,newnews_t);
                sendfile(f,mp->s,ASCII_TYPE,0,NULL);
                usputs(mp->s,NEol);
                ret = 1;
                break;
        }
        fclose(f);
        return ret;
    }
  
/* change current newsgroup
 * returncode: -1 if error; 0 success; 1 no newsgroup */
  
    static int
    dogroup(mp,buf)
    struct nntpsv *mp;
    char *buf;
    {
        char *p;
        int er;
  
        if((p = strchr(buf,' ')) == NULLCHAR) {
            usputs(mp->s,badsyntax);
            return -1;
        }
  
        er = get_path(p,mp);
  
        switch (er) {
  
            case 0:
                usputs(mp->s,nogroup);
                return 1;
  
            default:
                p = strchr(buf,' ');
                if (get_pointer(p,mp) == 1) {
                    usprintf(mp->s,listarticle,
                    mp->last - mp->first + 1,mp->first,mp->last,strchr(buf,' '));
                    return 0;
                }
                /* else fall into -1 case to return an error code */

            case -1:
                usputs(mp->s,error);
                return -1;
        }
    }
  
/* checks if newsgroup is selected
 * returncode: -1 no group selected; 0 success */
  
    static int
    check_grp(mp)
    struct nntpsv *mp;
    {
        if(mp == NULLNNTPSV || mp->path == NULLCHAR) {
            usputs(mp->s,noselect);
            return -1;
        }
        return 0;
    }
  
/* get id-number of message
 * returncode: 0 if no message-id; 1 success */
  
    static int
    get_id(bp,mp)
    char *bp;
    struct nntpsv *mp;
    {
        FILE *f;
        char tmp[LineLen];
        int continuation=0;
  
        if ((f = open_message(mp,NULLFILE)) == NULLFILE)
            return 0;
  
        for (;;) {
            if (fgets(tmp,LineLen,f) == NULL)
                break;
  
            if (!continuation) {
                if (check_blank(tmp))
                    break;
  
                if (strnicmp(tmp,msgid,12)==0) {
                    fclose(f);
                    strcpy(bp,strchr(tmp,' '));
                    rip(bp);
                    return 1;
                }
            }
            continuation = (strchr(tmp,'\n') == NULLCHAR);
        }
        fclose(f);
        strcpy(bp,"\0");
        usputs(mp->s, error);  /* Message-id missing */
        return 0;
    }
  
/* gets next news of newsgroup
  
 * returncode: -1 if error; 0 no news; 1 success */
  
    static int
    get_next(mp)
    struct nntpsv *mp;
  
    {
        FILE *f;
  
        for (;;) {
            if (mp->pointer == 0 ) {
                usputs(mp->s,nonext);
                return 0;
            }

            if (++(mp->pointer) > mp->last) {
                mp->pointer--;
                usputs(mp->s,nonext);
                return 0;
            }

            sprintf(mp->buf,"%s/%u",mp->path,mp->pointer);
            if ((f = open_file(mp->buf,READ_TEXT,mp->s,0)) != NULLFILE) {
                fclose(f);
                return 1;
            }
        }
    }
  
/* gets last news of newsgroup
  
 * returncode: -1 if error; 0 no news; 1 success */
  
    static int
    get_last(mp)
    struct nntpsv *mp;
    {
        FILE *f;
  
        for (;;) {
            if (mp->pointer == 0) {
                usputs(mp->s,noprev);
                return 0;
            }
  
            if (--(mp->pointer) < mp->first) {
                mp->pointer++;
                usputs(mp->s,noprev);
                return 0;
            }
  
            sprintf(mp->buf,"%s/%u",mp->path,mp->pointer);
            if ((f = open_file(mp->buf,READ_TEXT,mp->s,0)) != NULLFILE) {
                fclose(f);
                return 1;
            }
        }
    }
  
  
/*
 * Retrieves article by message-id. Sets mp->pointer and mp->path.
 * Return: success 0, no article 1, error -1.
 */
  
    static int
    retr_by_mid(mid,mp)
    char *mid;
    struct nntpsv *mp;
    {
        FILE *f;
        char *p,*p1,line[LineLen];
  
        if((f = open_file(History,READ_TEXT,mp->s,0)) == NULLFILE)
            return -1;
  
#ifdef MBFWD
        /* in check_article() we trimmed "@callsign.bbs" from the Message-id we
           stored into Historyfile, since we are interested in the invariant, BID
           portion of the mid.  But the original message-id is stored into the
           article header, and is provided by the XOVER cmd.  So we try to
           compensate here by fudging the mid string. -- n5knx 1.11x5
        */
        if((p = strchr(mid,'@')) != NULLCHAR) {
            /* A trailing ".bbs" indicates that the Message-ID was generated
             * from a BBS style message, and not a RFC-822 message.
             */
            if(stricmp(p+strlen(p)-5, ".bbs>") == 0) {
                strcpy(p,ATBBS); /* use <bid@bbs> */
            }
        }
#endif
        for(;;) {
            if(fgets(line,LineLen-1,f) == NULLCHAR) {
                usputs(mp->s,noart);
                fclose(f);
                return 1;
            }
            if(strncmp(line,mid,strlen(mid)) == 0)
                break;
        }
        fclose(f);
  
        p = strchr(line,' ') + 14;  /* point to the space before first newsgroup */
        p1 = strchr(p,'/');
        *p1 = '\0';
        p1++;
        mp->pointer = atoi(p1);                       /* get the article number */
  
        if(mp->path != NULLCHAR)
            free(mp->path);
  
        if(strcmp(p,"JUNK") == 0)
            mp->path = j2strdup(Forward);
        else
            if (get_path(p,mp) == -1) {
                usputs(mp->s,error);
                return -1;
            }
  
        return 0;
    }
  
  
/*
 * Retrieves article by article number. Sets mp->pointer.
 * Return: success 0, no article 1, error -1.
 */
  
    static int
    retr_by_num(buf,mp)
    char *buf;
    struct nntpsv *mp;
    {
        int n;
        char *p;
  
        if(check_grp(mp))
            return 1;
  
        if((p = strchr(buf,' ')) == NULLCHAR || check_blank(p))
            return 0;
  
        p++;
        n = atoi(p);
        if(!n || (unsigned int)n > mp->last || (unsigned int)n < mp->first) {
            usputs(mp->s,noart);
            return 1;
        }
        mp->pointer = n;
        return 0;
    }
  
  
/*
 * Do ARTICLE command. Return: success 0, no article 1, error -1.
 */
  
    static int
    doarticlecmd(buf,mp,retcode)
    char *buf;
    struct nntpsv *mp;
    int retcode;        /* OH2BNS: make "220 ... Article retrieved..." msg optional */
    {
        FILE *f;
        char *p,*hold_s;
        int ret,hold_i,hold = 0;
  
        if((p = strchr(buf,'<')) != NULLCHAR) {
            hold = 1;
            hold_i = mp->pointer;
            hold_s = mp->path;
            mp->path = NULLCHAR;
            if((ret = retr_by_mid(p,mp)) != 0)
                return ret;
        }
        else if((ret = retr_by_num(buf,mp)) != 0)
            return ret;
  
        if (get_id(mp->buf,mp) != 1)
            return -1;
  
        f = NULL;   /* WG7J */
        if ((f = open_message(mp,f)) == NULLFILE)
            return -1;
  
        if(retcode)
            usprintf(mp->s,retrieve,mp->pointer,mp->buf,artmsg);
        sendpart(f,mp,0);
  
        if(hold) {
            mp->pointer = hold_i;
            free(mp->path);
            mp->path = hold_s;
        }
        fclose(f);
        return 0;
    }
  
  
/*
 * Do HEAD command. Return: success 0, no article 1, error -1.
 */
  
    static int
    doheadcmd(buf,mp)
    char *buf;
    struct nntpsv *mp;
    {
        FILE *f;
        char *p,*hold_s;
        int ret,hold_i,hold = 0;
  
        if((p = strchr(buf,'<')) != NULLCHAR) {
            hold = 1;
            hold_i = mp->pointer;
            hold_s = mp->path;
            mp->path = NULLCHAR;
            if((ret = retr_by_mid(p,mp)) != 0)
                return ret;
        }
        else if((ret = retr_by_num(buf,mp)) != 0)
            return ret;
  
        if (get_id(mp->buf,mp) != 1)
            return 1;
  
        f = NULL; /* WG7J */
        if ((f = open_message(mp,f)) == NULLFILE)
            return -1;
  
        usprintf(mp->s,head,mp->pointer,mp->buf,artmsg);
        sendpart(f,mp,2);
  
        if(hold) {
            mp->pointer = hold_i;
            free(mp->path);
            mp->path = hold_s;
        }
        fclose(f);
        return 0;
    }
  
  
/*
 * Do BODY command. Return: success 0, no article 1, error -1.
 */
  
    static int
    dobodycmd(buf,mp)
    char *buf;
    struct nntpsv *mp;
    {
        FILE *f;
        char *p,*hold_s;
        int ret,hold_i,hold = 0;
  
        if((p = strchr(buf,'<')) != NULLCHAR) {
            hold = 1;
            hold_i = mp->pointer;
            hold_s = mp->path;
            mp->path = NULLCHAR;
            if((ret = retr_by_mid(p,mp)) != 0)
                return ret;
        }
        else if((ret = retr_by_num(buf,mp)) != 0)
            return ret;
  
        if (get_id(mp->buf,mp) != 1)
            return 1;
  
        f = NULL; /* WG7J */
        if ((f = open_message(mp,f)) == NULLFILE)
            return -1;
  
        usprintf(mp->s,body,mp->pointer,mp->buf,artmsg);
        sendpart(f,mp,3);
  
        if(hold) {
            mp->pointer = hold_i;
            free(mp->path);
            mp->path = hold_s;
        }
        fclose(f);
        return 0;
    }
  
  
/* send either all, the head, or the body of a message */
static void
sendpart(fp,mp,flag)
FILE *fp;
struct nntpsv *mp;
int flag;		/* 0=all 2=head 3=body */
{
    int in_part=2;
    int continuation=0;

    while ((fgets(mp->buf,LineLen,fp)) != NULLCHAR) {
        if (!continuation && check_blank(mp->buf)) {
            in_part=3;
            if (flag == 2)
                break;
            else if (flag == 3)
                continue;
        }
        if (!flag || flag==in_part) {
            if (!continuation && *mp->buf == '.')
                usputc(mp->s,'.');
            usputs(mp->s,mp->buf);
        }
        continuation = (strchr(mp->buf,'\n') == NULLCHAR);
    }
    usputs(mp->s,NEol);
}

/*
 * Do STAT command. Return: success 0, no article 1, error -1.
 */
  
    static int
    dostatcmd(buf,mp)
    char *buf;
    struct nntpsv *mp;
    {
        char *p,*hold_s;
        int ret,hold_i,hold = 0;
  
        if((p = strchr(buf,'<')) != NULLCHAR) {
            hold = 1;
            hold_i = mp->pointer;
            hold_s = mp->path;
            mp->path = NULLCHAR;
            if((ret = retr_by_mid(p,mp)) != 0)
                return ret;
        }
        else if((ret = retr_by_num(buf,mp)) != 0)
            return ret;
  
        if (get_id(mp->buf,mp) != 1)
            return 1;
  
        usprintf(mp->s,statistics,mp->pointer,mp->buf,artmsg);
  
        if(hold) {
            mp->pointer = hold_i;
            free(mp->path);
            mp->path = hold_s;
        }
        return 0;
    }
  
  
/*
 * Do XHDR command. Return: success 0, no article 1, error -1.
 */

/* 29Dec2004, Replace GOTO 'quit' LABELS with function */
static int do_quit5 (struct nntpsv *mp, int pointer, char *path, int ret)
{
   mp->pointer = pointer;
   free(mp->path);
   mp->path = path;
   return ret;
}
    static int
    doxhdrcmd(buf,mp)
    char *buf;
    struct nntpsv *mp;
    {
        FILE *f;
        char *path,*fld,*p,*mid,line[LineLen];
        int pointer,start,stop,ret = 0;
  
        if((fld = strchr(buf,' ')) == NULLCHAR || check_blank(fld)) {
            usputs(mp->s,badsyntax);
            return -1;
        }
        while(isspace(*++fld));
  
        pointer = start = stop = mp->pointer;
        path = mp->path;
        mid = mp->path = NULLCHAR;
  
        if((p = strchr(fld,' ')) != NULLCHAR && !check_blank(p)) {
            *p = '\0';
            while(isspace(*++p));
  
            if(*p == '<') {
                if((ret = retr_by_mid(p,mp)) != 0)
					return (do_quit5 (mp, pointer, path, ret));
                start = stop = mp->pointer;
                mid = p;
            } else {
                start = stop = atoi(p);
                if((p = strchr(p,'-')) != NULLCHAR) {
                    while(isspace(*++p));
                    if (*p)
                        stop = atoi(p);
                    else
                        stop = mp->last;
                }
            }
        }
  
        if(!mid) {
            mp->path = j2strdup(path);
            if(check_grp(mp)) {
                ret = 1;
				return (do_quit5 (mp, pointer, path, ret));
            }
            usprintf(mp->s,xheader1,fld);
        } else
            usprintf(mp->s,xheader2,0,fld,p);
  
        while(start <= stop) {
            if(mid || ((unsigned int)start >= mp->first && (unsigned int)start <= mp->last)) {
                sprintf(line,"%s/%u",mp->path,start);
  
                if((f = open_file(line,READ_TEXT,0,0)) != NULLFILE) {
                    for(;;) {
                        if(fgets(line,LineLen-1,f) == NULLCHAR
                        || check_blank(line)) {    /* no match was found */
                            if(mid)
                                usprintf(mp->s,"%s (none)\n",mid);
                            else
                                usprintf(mp->s,"%d (none)\n",start);
                            break;
                        }
  
                        if((p = strchr(line,':')) != NULLCHAR)
                            *p = '\0';
                        if(stricmp(line,fld) == 0) {
                            if(mid)
                                usprintf(mp->s,"%s %s",mid,p + 2);
                            else
                                usprintf(mp->s,"%d %s",start,p + 2);
                            break;
                        }
                    }
                    fclose(f);
                }
            }
            start++;
        }
        usputs(mp->s,NEol);
  
		return (do_quit5 (mp, pointer, path, ret));
    }
  
/*
 * Do XOVER command. Return: success 0, error -1.
 */
  
    static int
    doxovercmd(buf,mp)
    char *buf;
    struct nntpsv *mp;
    {
        FILE *f;
        char *subject,*from,*date,*mid,*ref,*xref,*p,line[LineLen];
        int lines,size,start,stop,got,continuation,continued;
  
        if((p = strchr(buf,' ')) != NULLCHAR) {
            start = stop = atoi(p);
            if((p = strchr(buf,'-')) != NULLCHAR) {
                while(isspace(*++p));
                if (*p)
                    stop = atoi(p);
                else
                    stop = mp->last;
            }
        } else
            start = stop = mp->pointer;
  
        usputs(mp->s,xover);
  
        while(start <= stop) {
            if((unsigned int)start >= mp->first && (unsigned int)start <= mp->last) {
                sprintf(line,"%s/%u",mp->path,start);
                if((f = open_file(line,READ_TEXT,0,0)) != NULLFILE) {
                    subject = from = date = mid = ref = xref = NULLCHAR;
                    lines = size = got = continued = 0;
                    while(fgets(line,LineLen-1,f) != NULLCHAR) {
                        continuation = continued;
                        continued = (strchr(line,'\n') == NULLCHAR);
                        rip(line);
                        if(got == 0) {
                            if(strnicmp(line,subj,9) == 0)
                                subject = j2strdup(strchr(line,' ') + 1);
                            else if(strnicmp(line,frm,6) == 0)
                                from = j2strdup(strchr(line,' ') + 1);
                            else if(strnicmp(line,dte,6) == 0)
                                date = j2strdup(strchr(line,' ') + 1);
                            else if(strnicmp(line,msgid,12) == 0)
                                mid = j2strdup(strchr(line,' ') + 1);
                            else if(strnicmp(line,"Lines: ",7) == 0)
                                lines = atoi(strchr(line,' ') + 1);
                            else if(strnicmp(line,"References: ",12) == 0)
                                ref = j2strdup(strchr(line,' ') + 1);
                        } else
                            size += strlen(line);
                        if(!continuation && *line == '\0')
                            got = 1;
                    }
                    usprintf(mp->s,"%u\t%s\t%s\t%s\t%s\t",
                        start,subject,from,date,mid); /* these must be present */
                    if (ref) usputs(mp->s, ref);
                    usprintf(mp->s,"\t%u\t%u\t%s\n", size,lines,""); /* last field not (yet) implemented */
  
                    free(subject);
                    free(from);
                    free(date);
                    free(mid);
                    free(ref);
                    fclose(f);
                }
            }
            start++;
        }
        usputs(mp->s,NEol);
        return 0;
    }
  
  
/* Checks and rewrites message headers if needed. Returns -1 if error,
 * 0 if all RFC1036 headers are present and 1 if a subset of RFC1036
 * headers is present. (1 means that IHAVE should be rejected but
 * POST is ok.)
 */
  
    static int
    garbled(inf,outf)
    FILE *inf;
    FILE *outf;
    {
        char line[LineLen],*cp;
        int got,ok,lines,ret,continued,continuation;
        long currtime;
  
        got = ok = lines = continued = 0;
  
    /* first scan the headers */
        rewind(inf);
        while(fgets(line,LineLen,inf) != NULLCHAR) {
            continuation = continued;
            continued = (strchr(line,'\n') == NULLCHAR);
            if(got) {
                lines++;
                continue;
            }
            rip(line);
            if (!continuation && check_blank(line)) {   /* end of headers */
                got = 1;
                continue;
            }
  
            if (line[0] == ' ' || line[0] == '\t' || continuation)  /* continuation header */
                continue;
  
            cp = line;
            while (*cp != '\0' && *cp != ' ' && *cp != ':')
                cp++;
            if (*cp != ':' || *++cp != ' ')
                return -1;                      /* bad header; reject article */
  
            cp++;
            if (check_blank(cp))                              /* empty header */
                continue;
  
            if (!strnicmp(line,frm,6))
                ok |= 1;
            if (!strnicmp(line,subj,9))
                ok |= 2;
            if (!strnicmp(line,ngrps,12))
                ok |= 4;
            if (!strnicmp(line,dte,6))
                ok |= 8;
            if (!strnicmp(line,msgid,12))
                if (check_article(cp) == 0)
                    ok |= 16;
            if (!strnicmp(line,pth,6))
                ok |= 32;
            if (!strnicmp(line,"Lines: ",7))
                ok |= 64;
        }
  
        if((ok & 7) != 7)           /* From, Subject and Newsgroups not found */
            return -1;
  
        if((ok & 63) == 63)
            ret = 0;                                         /* everything ok */
        else
            ret = 1;
  
    /* then rewrite if necessary */
        got = continued = 0;
        rewind(inf);
        while(fgets(line,LineLen,inf) != NULLCHAR) {
            pwait(NULL);
            continuation = continued;
            continued = (strchr(line,'\n') == NULLCHAR);
            if(got) {
                fputs(line,outf);
                continue;
            }
            if(!continuation && check_blank(line)) {
                got = 1;
                if(!(ok & 64))
                    fprintf(outf,"Lines: %d\n",lines);
                fputc('\n',outf);
                continue;
            }
  
            if(!continuation && line[0] != ' ' && line[0] != '\t') {
                cp = strchr(line,':');
                cp++;
                if(check_blank(cp))              /* empty header; do not copy */
                    continue;
            }
            if(!strnicmp(line,"Xref: ",6) ||
                !strnicmp(line,"Date-Received: ",15) ||
                !strnicmp(line,"Posted: ",8) ||
                !strnicmp(line,"Posting-Version: ",17) ||
                !strnicmp(line,"Received: ",10) ||
                !strnicmp(line,"Relay-Version: ",15)) {
                continued = -continued;  /* don't copy any continuation */
                continue;                        /* do not copy these headers */
            }
  
            if (continuation < 1)   /* if !continuation, or continuation of desired hdr */
                fputs(line,outf);
            rip(line);
  
        /* no path? */
            if (!(ok & 32) && strnicmp(line,frm,6) == 0) {
                fprintf(outf,"%snot-for-mail\n",pth);
                ok |= 32;
            }
  
            if (strnicmp(line,subj,9) == 0) {
            /* no msgid? */
                if (!(ok & 16)) {
                    fprintf(outf,"%s<%ld@%s>\n",msgid,get_msgid(),Hostname);
                    ok |= 16;
                }
            /* no date? */
                if (!(ok & 8)) {
                    time(&currtime);
                    fprintf(outf,"%s%s",dte,ptime(&currtime));
                    ok |= 8;
                }
            }
        }
  
        rewind(outf);
        return ret;
    }

/* 29Dec2004, Maiko, Replacing GOTO 'quit' LABELS with function */ 
static int do_quit6 (FILE *f, FILE *f1, int ret)
{ 
   fclose(f);
   fclose(f1);
   return ret;
}

/* returncode: -1 if error; 0 success */
  
    static int
    get_article2(mp, id)
    struct nntpsv *mp;
    char *id;
    {
        FILE *f,*f1;
        int ret = -1, posting, i;
        char *p;

        if((f = temp_file(0,1)) == NULLFILE)
            return -1;
        if((f1 = temp_file(0,1)) == NULLFILE) {
            fclose(f);
            return -1;
        }
  
        if (id == NULLCHAR) {  /* POST command */
            posting=1;
            mp->id = j2strdup(">");  /* we'll try to do better, below... */
	} else {  /* IHAVE command */
            posting=0;
            mp->id = (*id) ? j2strdup(id) : j2strdup(">");
        }

        usputs(mp->s,(posting?sendpost:sendart));
  
        if(recv_file(f,mp->s) == (-1)) {
			return (do_quit6 (f, f1, ret));
        }
  
        if ((i=garbled(f,f1)) < 0 || (!posting && i>0)) {
            usputs(mp->s,(posting?postfailedgrbl:transnotok));
			return (do_quit6 (f, f1, ret));
        }
  
        if (posting) {
            rewind(f1);
            while (fgets(mp->buf,LineLen,f1) != NULL) {
                rip(mp->buf);
                if (strnicmp(mp->buf,msgid,12) == 0) {
                    if ((p=strchr(mp->buf,'<')) != NULLCHAR) {
                        free(mp->id);
                        mp->id = j2strdup(p);
                    }
                    break;
                }
            }
            rewind(f1);
        }

        if(xfer_article2(f1,mp) < 1)
            usputs(mp->s,(posting?postfailed:transnotok));
        else {
            ret = 0;
            usputs(mp->s,(posting?postok:transok));
        }
  
		return (do_quit6 (f, f1, ret));
    }
  
    static void
    dopoll(p)
    void *p;
    {
  
        if (newproc("NNTP Client", 2350, nntppoll, 0, p, NULL, 0) == NULLPROC)
            start_timer(&((struct Servers *)p)->nntpt);    /* N5KNX: retry later */

    }
  
  
    static int
    chk_access(s)
    int s;
    {
        struct sockaddr fsocket;
        FILE *f;
        int trans,verb,i;
        int access = -1;                            /* default is no access */
        char *cp,*cp1,name[80], line[80];
  
        trans = DTranslate;                           /* save the old state */
        verb = DVerbose;
        DTranslate = 1;      /* force the output of psocket() to be literal */
        DVerbose = 1;
        i = SOCKSIZE;
  
        if(j2getpeername(s,(char *)&fsocket,&i) != -1) {
            strcpy(name,psocket(&fsocket));
            if((cp = strchr(name,':')) != NULLCHAR)
                *cp = '\0';
        }
  
        DTranslate = trans;                        /* restore the old state */
        DVerbose = verb;
  
        if((f = open_file(Naccess,READ_TEXT,s,0)) != NULLFILE) {
            while(fgets(line,79,f) != NULLCHAR) {
                if(*line == '#')
                    continue;
                if((cp = strchr(line,':')) != NULLCHAR)
                    *cp = '\0';
                if(wildmat(name,line,NULLCHARP)) {
                    if((cp1 = strchr(++cp,':')) != NULLCHAR)
                        *cp1 = '\0';
                    if(strchr(cp,'R'))                         /* read only */
                        access = 0;
                    if(strchr(cp,'P'))                     /* read and post */
                        access = 1;
                    break;
                }
            }
            fclose(f);
        }
        return access;
    }

/* 29Dec2004, Replacing GOTO 'exit_early' LABEL with function */
static void do_exit_early (int s)
{
	close_s(s);
	NntpUsers--;
	return;
}

/* 29Dec2004, Replacing GOTO 'quit' LABEL with function */
static void do_quit7 (struct nntpsv *mp)
{
    usputs(mp->s,closing);
    log(mp->s,"close NNTP");
    close_s(mp->s);
	NntpUsers--;
    Nntpclicnt--;

    if(mp->path != NULLCHAR)
        free(mp->path);
  
    if(mp->newnews != NULLCHAR)
        free(mp->newnews);
  
    if(mp->fname != NULLCHAR)
        free(mp->fname);
  
    if(mp->id != NULLCHAR)
        free(mp->id);
  
    free((char *)mp);
}

    static void
    nntpserv(s,unused,p)
    int s;
    void *unused;
    void *p;
    {
        struct nntpsv *mp;
        FILE *fp;
        long t;
        struct tm *server_time;
        int cnt, postallowed = 1;
        char **cmdp, *arg, *cp, buf[LineLen];
  
#ifdef LZW
        int lbits, lmode;
#endif
  
        sockmode(s,SOCK_ASCII);
        sockowner(s,Curproc);           /* We own it now */
        log(s,"open NNTP");
        NntpUsers++;
  
        if (!Filecheck)
            if(check_system()) {
                usprintf(s,fatal,"STRUCTURE");
                log(s,"NNTP error - FILE STRU");
				return (do_exit_early (s));
            }
  
        if(Nntpaccess)
            if((postallowed = chk_access(s)) < 0) {
                usputs(s,noaccess);
				return (do_exit_early (s));
            }
  
        if ((Nntpmaxcli && Nntpclicnt >= Nntpmaxcli) ||
		((mp = (struct nntpsv *)calloc(1,sizeof(struct nntpsv))) == NULLNNTPSV))
		{
            usputs(s,nospace);
			return (do_exit_early (s));
        }
  
        Nntpclicnt++;
        mp->s = s;
        mp->debug = 0;
        mp->path = NULLCHAR;
  
        time(&t);
        cp = ctime(&t);
  
        usprintf(s,nnversion,postallowed ? "0" : "1",Host,Version,cp,
        postallowed ? "(posting ok)" : "(no posting)");
 
		while (1)	/* replace the 'loop:' label for GOTO */
		{
  
        j2alarm(NNTPS_TIMEOUT * 1000);
        if ((cnt = recvline(s,buf,LineLen)) == -1)
		{
        /* He closed on us or timer expired */
			return (do_quit7 (mp));
        }
        j2alarm(0);
  
 		/* check for empty line, do nothing */
        if(check_blank(buf))
            continue;   /* replaces the GOTO loop; */
  
        rip(buf);
  
#ifdef notdef
    /* Translate first word in buffer to lower case [unneeded...use strnicmp!] */
  
        for(cp = buf; *cp != '\0' ;cp++) {
            if ( *cp == ' ' ) break;
            *cp = tolower(*cp);
        }
#endif
  
    /* Find command in table; if not present, return syntax error */
    /* If not present, return 500 command unrecognized */
  
        for(cmdp = commands; *cmdp != NULLCHAR; cmdp++) {
            if(strnicmp(*cmdp,buf,strlen(*cmdp)) == 0)
                break;
        }
  
        if(*cmdp == NULLCHAR){
            usputs(mp->s,notrecognd);
            continue;	/* replaces the GOTO loop; */
        }

        arg = &buf[strlen(*cmdp)];
  
    /* Skip spaces after command */
  
        while(*arg == ' ')
            arg++;
  
    /* Execute specific command */
  
        switch(cmdp - commands) {
  
            case XLZW_CMD:
  
#ifdef LZW
                if (LzwActive)   {
                    usputs(mp->s,transok);
                    cp = strchr(arg,' ');
                    *cp++ = '\0';
                    lbits = atoi(arg); /*get lzwbits*/
                    lmode = atoi(cp);
                    lzwinit(mp->s,lbits,lmode);
                } else
#endif
                    usputs(mp->s,error);
  
                break;
  
            case QUIT_CMD:
  
				return (do_quit7 (mp));
  
            case NEWNEWS_CMD:
  
                if (*arg)   /* anything left after blanks are skipped? */
                    donewnews(arg,mp);
                else
                    usputs(mp->s,badsyntax);
                break;
  
            case IHAVE_CMD:
  
                if ((cp = strchr(buf,'<')) == NULLCHAR) {
                    usputs(mp->s,badsyntax);
                    break;
                }
  
                if (check_article(cp) != 0) {
                    usputs(mp->s,notwanted);
                    break;
                }
  
                if(get_article2(mp,cp))
					return (do_quit7 (mp));
  
                break;
  
            case POST_CMD:
  
                if (!postallowed) {
                    usputs(mp->s,notallowed);
                    break;
                }
  
                get_article2(mp,NULLCHAR);
                break;
  
            case HELP_CMD:
  
                usprintf(mp->s,"100 %s - help follows:\n",Host);
  
                if ((fp = open_file(Nhelp,READ_TEXT,0,0)) != NULLFILE ) {
                    sendpart(fp,mp,0);
                    fclose(fp);
                } else {
                    usprintf(mp->s,"\nKA9Q NOS NNTP Server, version %s\n\n",Version);
                    for(cnt = 1;commands[cnt] != NULLCHAR;cnt++)
                        usprintf(mp->s,"%-9s%c",commands[cnt],(cnt%7) ? ' ' : '\n');
                    usputs(mp->s,"\n");
                    usputs(mp->s,NEol);
                }
  
                break;
  
            case XINFO_CMD:
  
                if ((fp = open_file(NInfo,READ_TEXT,0,0)) != NULLFILE ) {
                    usprintf(mp->s,"100 %s - xinfo follows:\n",Host);
                    sendpart(fp,mp,0);
                    fclose(fp);
                } else {
                    usputs(mp->s,xinfo);
                    usputs(mp->s,NEol);
                }
  
                break;
  
            case LIST_CMD:
  
                if ((fp = open_file(Active,READ_TEXT,mp->s,0)) != NULLFILE) {
                    usputs(mp->s,listgroups);
                    sendpart(fp,mp,0);
                    fclose(fp);
                }
                break;
  
            case NEWGROUPS_CMD:

                if (*arg)   /* anything left after blanks are skipped? */
                    donewgroups(arg,mp);
                else
                    usputs(mp->s,badsyntax);
                break;

            case GROUP_CMD :
  
                dogroup(mp,buf);
                break;
  
            case HEAD_CMD :
  
                doheadcmd(buf,mp);
                break;
  
            case BODY_CMD :
  
                dobodycmd(buf,mp);
                break;
  
            case STAT_CMD :
  
                dostatcmd(buf,mp);
                break;
  
            case ARTICLE_CMD :
  
                doarticlecmd(buf,mp,1);
                break;
  
            case XHDR_CMD :
  
                doxhdrcmd(buf,mp);
                break;
  
            case XOVER_CMD :
  
                if (check_grp(mp))
                    break;
  
                doxovercmd(buf,mp);
                break;
  
            case NEXT_CMD :
  
                if (check_grp(mp))
                    break;
  
                if (get_next(mp) == 1) {
                    if (get_id(buf,mp) == 1)
                        usprintf(mp->s,sepcmd,mp->pointer,&buf[1],artmsg);
                    break;
                }
  
                break;
  
            case DATE_CMD :
                time(&t);
                server_time=gmtime(&t);
                usprintf(mp->s,"111 %04d%02d%02d%02d%02d%02d\n",
                  (server_time->tm_year)+1900,(server_time->tm_mon)+1,server_time->tm_mday,
                  server_time->tm_hour,server_time->tm_min,server_time->tm_sec);
                break;

            case LAST_CMD :
  
                if (check_grp(mp))
                    break;
  
                if (get_last(mp) == 1 ) {
                    if (get_id(buf,mp) == 1)
                        usprintf(mp->s,sepcmd,mp->pointer,&buf[1],artmsg);
                    break;
                }
  
                break;
  
    /* This two following cmds currently are not used for much */
  
            case DEBUG_CMD:
  
                mp->debug = (mp->debug == 0) ? 1 : 0;
                usprintf(mp->s,debug,(mp->debug == 0) ? "OFF" : "ON");
                break;
  
            case SLAVE_CMD :
  
                mp->slave = (mp->slave == 0) ? 1 : 0;
                usprintf(mp->s,slave,(mp->slave == 0) ? "OFF" : "ON");
                break;
  
        }

		}	/* end of while loop for former 'loop:' label */
  
		/* return (do_quit7 (mp)); This is never reached !!! */
    }
  
/* ---------------------------- NNTP-GATE --------------------------- */
/* Handles msgs addressed to !news!group!name ... see smtpserv.c */  
  
  
    static char * near
    mkreplypath(FILE *data)
    {
        char buf[LineLen], *cp, *cp1, *path;
        int  type, prevtype=NOHEADER, pathlen;
  
        sprintf(buf,"%sNNTP_GATE@%s",pth,Hostname);
        path = j2strdup(buf);
        rewind(data);
  
        while((fgets(buf,LineLen,data)) != 0) {
            type = htype(buf, &prevtype);
            if(type == DATE)
                break;
  
            if(type == RECEIVED) {
                cp = strpbrk(buf," \t");  /* look for whitespace then "by" */
                while (*cp != '\0') {
                    cp++;
                    if(!strnicmp(cp,"by",2)) {
                        cp = strchr(cp, ' ');
                        cp++;
                        cp1 = strpbrk(cp, ". ;\n");
                        *cp1 = '\0';
                        if ((pathlen = strlen(path)+strlen(cp)+2) < LineLen) {
                            cp1 = mallocw(pathlen);
                            sprintf(cp1,"%s!%s",path,cp);
                            free(path);
                            path = cp1;
                        }
                        break;
                    }
                }
            }
        }
#ifdef NN_REMOVE_R_LINES
        type=0;  /* search for blank line ending the headers */
        while((fgets(buf,LineLen,data)) != 0) {
            if (type==0 && !strcmp(buf,"\n"))
                type=1;  /* R: line may follow */
            else if (type==1) {
               if(!strncmp(buf,"R:",2)) {  /* genuine R: line */
                   if((cp=strchr(buf,'@'))!=NULLCHAR) {
                       if(*++cp == ':') cp++;  /* skip over ':' */
                       if ((cp1=strpbrk(cp,". \t\n")) != NULLCHAR)
                           *cp1='\0';
                        if ((pathlen = strlen(path)+strlen(cp)+2) < LineLen) {
                            cp1 = mallocw(pathlen);
                            sprintf(cp1,"%s!%s",path,cp);
                            free(path);
                            path = cp1;
			}
                        else break;  /* quit early ... out of room */
		   }
               }
               else break;
	    }
        }
#endif

        return(path);
    }
  
  
    int
    nnGpost(FILE *data,char *from,struct list *le)
    {
        struct nntpsv *mp;
        char buf[LineLen], *cp;
        FILE *f, *idf;
        int32 id;
        int strl;
        int prevtype;
        long currtime;
  
        if (!Filecheck)
            if(check_system())
                return -1;
  
        if ((f = temp_file(0,1)) == NULLFILE)
            return -1;
  
        mp = (struct nntpsv *)callocw(1,sizeof(struct nntpsv));
  
    /* build postuser */
  
        cp = mkreplypath(data);
        fprintf(f,"%s\n",cp);
        free((char *)cp);
        fprintf(f,"%s%s\n",frm,from);
  
   /*-----------------------------------------------------------------*
    * build newsgroup                                                 *
    *-----------------------------------------------------------------*/
  
        if((cp = strchr(le->val,'@')) != NULLCHAR)
            *cp = '\0';
  
        strcpy(buf,le->val);
        cp=buf;
        strl = strlen(cp);
  
        while(*cp != '\0') {
            if(*cp == '!') *cp = '.'; /* change ! into .  - yc1dav */
            else if(*cp == '+') *cp = ','; /* + into , permits cross-posts  - g8fsl */
            cp++;
        }
  
        fprintf(f,"%s%s\n",ngrps,(cp-strl+1));  /* skip the first "." */
  
   /*-----------------------------------------------------------------*
    * find the subject
    *-----------------------------------------------------------------*/
  
        rewind (data);
        prevtype=NOHEADER;
        while((fgets(buf,sizeof(buf),data))!=0)   {
            if (htype(buf,&prevtype) == SUBJECT)
                break;
            *buf = 0;
        }
  
        fputs((*buf == 0) ? "Subject: (none)\n" : buf,f);
  
   /*--------------------------------------------------------------------*
    * use msgid of original message unless it arrived via BBS forwarding
    *--------------------------------------------------------------------*/
  
        rewind(data);
  
        prevtype=NOHEADER;
        while((fgets(buf,sizeof(buf),data))!=0)   {
            if (htype(buf,&prevtype) == MSGID)
                break;
            *buf = 0;
        }
  
        if (*buf == 0)   {
            sprintf(buf,"%s/sequence.seq",Mailqdir);
            idf = open_file(buf,"r+",0,1);
		/* 11Oct2009, Maiko, use atoi() and "%d" for int32 vars */
            id = atoi(fgets(buf,LineLen,idf));
            rewind(idf);
            fprintf(idf,"%d",id+1);
            fclose(idf);
            fprintf(f,"%s<%d@%s>\n",msgid,id,Hostname);
            sprintf(mp->buf,"<%d@%s>",id,Hostname);
        } else {
            rip(buf);
            cp = strchr(buf,'<');
            strcpy(mp->buf,cp);

			/* mp->buf may be "adjusted" if a pbbs msg */
            if (check_article(mp->buf))
			{
        		fclose(f);
        		free(mp);
        		return 0;
			}
            fputs(buf,f);
            fputc('\n',f);
        }
  
        rewind (data);
        time(&currtime);
        fprintf(f,"Sender: NNTP@%s\n",Hostname);
  
   /*--------------------------------------------------------------------*
    * message follows                                                    *
    *--------------------------------------------------------------------*/
  
        fputs("Comments: Article created from mail\n",f);
        prevtype=NOHEADER;
        while((fgets(buf,sizeof(buf),data))!=0)   {
            switch(htype(buf,&prevtype))   {
  
                case FROM:
                case MSGID:
                case RECEIVED:
                case SUBJECT:
                case BBSTYPE:
                case XFORWARD:
                case XBBSHOLD:
                case RRECEIPT:
                    continue;  /* don't copy this header line */
            }
#ifdef NN_REMOVE_R_LINES
            if(!strncmp(buf,"R:",2))   /* simplistic...suppose it in in the msg body? */
                continue;
#endif  
            fputs(buf,f);
        }
  
        rewind(f);
        mp->id = j2strdup(mp->buf);
        xfer_article2(f,mp);
  
/*
 *
 *  log(-1,"NNGT: transfer: Msg %s",mp->id);
 *
 */
        fclose(f);
        free(mp);
        return 0;
    }
  
  
  
/* ---------------------------- Servercmd --------------------------- */
  
  
  
/* Start up NNTP receiver service */
    int
    nntp1(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        int16 port;
  
        if(check_system() == -1)
            return -1;
  
        if(argc < 2)
            port = IPPORT_NNTP;
        else
            port = atoi(argv[1]);
  
        return start_tcp(port,"NNTP Server",nntpserv,2350);
    }
  
/* Shutdown NNTP service (existing connections are allowed to finish) */
    int
    nntp0(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        int16 port;
  
        if(argc < 2)
            port = IPPORT_NNTP;
        else
            port = atoi(argv[1]);
        return stop_tcp(port);
    }
  
/* ---------------------------- Subcmds --------------------------- */
/* lists active newsgroups
 * returncode: -1 if error; 0 success */
  
    static int
    donnactive(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        FILE *fp;
        char line[80], *cp;
  
        if((fp = open_file(Active,READ_TEXT,0,1)) == NULLFILE)
            return -1;
  
        j2tputs("Last# First PostOK newsgroup\n");
  
        for(;;) {
            if (fgets(line,sizeof(line),fp) == NULL)
                break;
            if((cp = strchr(line,' ')) == NULLCHAR)
                break;
            *cp = '\0';
            rip(++cp);
            tprintf("%s      %s\n",cp,line);
            pwait(NULL);
        }
        fclose(fp);
        return 0;
    }
  
  
    static int
    donnaccess(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        return setbool(&Nntpaccess,"NNTP access",argc,argv);
    }
  
  
  
/* add nntp servers to list */
    static int
    donnadds(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        struct Servers *np;

#ifdef	NNTP_ONLY_44NET
	char *destA;	/* 16Jun2021, Maiko (VE4KLM), protecting sysops from themselves */
#endif
        for(np = Nntpserver; np != NULLSERVER; np = np->next)
            if(stricmp(np->name,argv[1]) == 0)
                break;
        if (np == NULLSERVER) {
            np = (struct Servers *)callocw(1,sizeof(struct Servers));
            if((np->dest = resolve(argv[1])) == 0) {
                tprintf(Badhost,argv[1]);
                free((char *)np);
                return -1;
            }

#ifdef	NNTP_ONLY_44NET

		/*
		 * 16Jun2021, Maiko (VE4KLM), I am getting a LOT of flack because
		 * of a recent NNTP 'storm' involving direct internet group posting
		 * with foul language and very inappropriate content, to me this is
		 * a case of 'sysop not understanding', instead of some saying that
		 * this is the fault of JNOS 2.0, great 'software wars', whatever.
		 *
		 * I suppose I could put in protection such that NNTP servers outside
		 * the 44 network will not 'be allowed', just to cover my ass, sorry.
		 *
		 * And I'll probably get flack for this one too, you just can't win :(
		 *  (yup, supposedly protecting sysops from themselves)
		 */

		destA = inet_ntoa (np->dest);

		if (*destA && strncmp (destA, "44", 2))
		{
                	tprintf ("Only 44 net servers allowed\n");
                	free (np);
                	return -1;
		}
#endif
            np->name = j2strdup(argv[1]);
            np->next = Nntpserver;
            Nntpserver = np;
            np->newsgroups = NULLCHAR;
            np->lowtime = np->hightime = -1;
            np->nntpt.func = dopoll;          /* what to call on timeout */
            np->nntpt.arg = (void *)np;
        }

        if (argc > 3) {
            int i;
            if (np->newsgroups == NULLCHAR) {
                np->newsgroups = callocw(1,LineLen);
                *np->newsgroups = '\0';
            }

            for (i = 3; i < argc; ++i) {
                if (isdigit(*argv[i])) {
                    int lh, ll, hh, hl;
                    sscanf(argv[i], "%d:%d-%d:%d", &lh, &ll, &hh, &hl);
                    np->lowtime = lh * 100 + ll;
                    np->hightime = hh * 100 + hl;
                } else if ((strlen(np->newsgroups)+strlen(argv[i])+2) >= LineLen)
                    tprintf("Too many groups, '%s' ignored\n", argv[i]);
                else {  /* it's a group, and it fits... add it to list */
                    if (*np->newsgroups != '\0')
                        strcat(np->newsgroups, ",");
                    strcat(np->newsgroups, argv[i]);
                }
            }

            if (*np->newsgroups == '\0') {  /* No groups specified? */
                free(np->newsgroups);
                np->newsgroups = NULLCHAR;
            }
        }
    /* set timer duration */
		/* 11Oct2009, Maiko, set_timer expects uint32, not atol() */
        set_timer(&np->nntpt,(uint32)atoi(argv[2])*1000);
        start_timer(&np->nntpt);                /* and fire it up */
        return 0;
    }
  
static int
donnfirstpoll(argc,argv,p)    /* from G8FSL */
int argc;
char *argv[];
void *p;
{
	return setint(&Nntpfirstpoll,"NNTP polls new server for news over last <n> days",argc,argv);
}

/* 29Dec2004, Maiko, Replace 'quit' GOTO with function */
static int do_quit8 (FILE *f, FILE *t)
{
   fclose(f);
   fclose(t);
   rmlock(Active, NULLCHAR);
   return 0;
}
  
/* create a new newsgroup */
    static int
    donncreate(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        FILE *f,*t;
        char line[LineLen];
        int n=0;
  
        while(mlock(Active, NULLCHAR)) { /* since Active is updated elsewhere */
            j2pause(1000);
            if (++n == 60) {
                tprintf("NNTP: can't create due to %s lock", Active);
                return -1;   /* can't lock, so reject (what else? n5knx) */
            }
        }

        if ((f = open_file(Active,READ_TEXT,0,1)) == NULL && errno!=ENOENT)
			return (doerrxit ());
  
        if ((t = temp_file(0,1)) == NULL) {
            fclose(f);
			return (doerrxit ());
        }
  
        while (f && fgets(line,LineLen,f) != NULL) {
            fputs(line,t);
            n = strcspn(line," ");
            line[n] = '\0';
  
            if (strcmp(line,argv[1]) == 0) {
                tprintf("Newsgroup %s already exists.\n",argv[1]);
				return (do_quit8 (f, t));
            }
        }
  
        fclose(f);
        if (ferror(t))
		{
			fclose(t);
			return (doerrxit ());
		}
        rewind(t);
  
        if (argc > 2 && *argv[2] == 'n')
            n = 1;
        else
            n = 0;
  
        if (!make_path(argv[1]))
		{
            if ((f = open_file(Active,WRITE_TEXT,0,1)) == NULL)
			{
                fclose(t);
				return (doerrxit ());
            }
  
            while (fgets(line,LineLen,t) != NULL)
                fputs(line,f);
  
            fprintf(f,"%s 00000 00001 %c\n",argv[1],(n ? 'n' : 'y'));
        }
  
		return (do_quit8 (f, t));
    }
  
/* drops nntp servers from list */
  
    static int
    donndrops(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        struct Servers *np, *npprev = NULLSERVER;
  
        for(np = Nntpserver; np != NULLSERVER; npprev = np, np = np->next)
            if(stricmp(np->name,argv[1]) == 0) {
                stop_timer(&np->nntpt);
                free(np->name);
                if (np->newsgroups)
                    free(np->newsgroups);
                if(npprev != NULLSERVER)
                    npprev->next = np->next;
                else
                    Nntpserver = np->next;
                free((char *)np);
                return 0;
            }
        j2tputs("No such server enabled.\n");
        return -1;
    }
  
  
#ifdef NEWS_TO_MAIL
/* Send news article off via SMTP to specified recipient (could be an alias) */
static void
news2mail(FILE *f, char *group)
{
    FILE *fp;
    long ctm;
    char *buf, *cp, *cp1, *cp2, *smtp_to=NULLCHAR, *smtp_from=NULLCHAR;
    int inheaders, prevtype;
    int continued = 0;     /* accommodate lines > LineLen chars long */
    int skip_mailing = 0;     /* !0 => don't email article */

    /* Check that Newstomail file exists */
    if ((fp=fopen(Newstomail,READ_TEXT)) == NULLFILE)
        return;

    strlwr(group);

    /* Look for a match between the newsgroup name(s) and the first field
     * of the Newstomail file.  The second field contains the SMTP To: address
     */
    buf = mallocw(LineLen);
    while ((fgets(buf,LineLen,fp)) != 0) {
        if (*buf == '#')
            continue;
        rip(buf);
        cp=skipnonwhite(buf);
        if (*cp) {
            *cp++ = '\0';
            /* does newsgroups line contain a name matching that from Newstomail (possibly a starname)? */
#ifdef notdef
/* formerly we declared a match if all chars of the first field matched */
            if ((cp1=strstr(group, buf)) != NULLCHAR
                && (cp1 == group || *(cp1-1) == ',')) {
                free(smtp_to);
                smtp_to=j2strdup(getaddress(cp,1));
                break;
            }
#endif
            for (cp1=group; cp1; cp1=cp2) {
                if ((cp2 = strchr(cp1,',')) != NULLCHAR)  /* stop compare at comma */
                    *cp2++ = '\0';
                if (restreql(buf, cp1)) {  /* match (starnames OK) ? */
                    free(smtp_to);
                    smtp_to=j2strdup(getaddress(cp,1));
                    break;
                }
            }
            if (cp1) break;  /* match found, no need to read further */
        }
    }
    fclose(fp);

    /* Must have an smtp_to entry */
    if (smtp_to && ((fp = tmpfile()) != NULLFILE)) {
        inheaders = 1;
        prevtype = NOHEADER;
        rewind(f);
        time(&ctm);
        fprintf(fp,"%sby %s with NNTP\n\tid AA%ld ; %s",
            Hdrs[RECEIVED], Hostname, get_msgid(), ptime(&ctm));

        while ((fgets(buf,LineLen,f)) != NULL) {
            if (!continued && strncmp(buf,"From ",5) == 0)
                fputc('>',fp);  /* escape UUCP From line */

            continued = (strchr(buf,'\n') == NULLCHAR);
            if (inheaders) {
                if (*buf == '\n') {
                    inheaders = 0;
                    sprintf(buf,"%s sysop\n%s%s\n\n",Hdrs[ERRORSTO],Hdrs[TO],smtp_to);  /* last 2 header lines */
                }
                else switch (htype(buf,&prevtype)) {
                     /* Headers to drop */
                case PATH:
#ifndef NN_RISK_LOOPS
                    cp=buf;
                    while( (cp=strstr(cp,"NNTP_GATE@")) != NULLCHAR) {
                        cp += 10;  /* advance past NNTP_GATE@ and check hostname */
                        if (!strncmp(cp,Hostname,strlen(Hostname)))
                            /* Oops, article arrived via gate, DON'T pass it back */
                            skip_mailing++;
                    }
#endif
                case TO:        /* going to rebuild the To: header */
                case RRECEIPT:
                /* case NEWSGROUPS: */
                case SENDER:
                case ERRORSTO:
                case UNKNOWN:
                    continue;  /* repeat while loop (don't copy this line) */

                case FROM:
                    free(smtp_from);
                    smtp_from = j2strdup(getaddress(buf, 0));
                    strcat(buf,"\n");   /* KLUDGE: getaddress() truncates NL */
                    break;
                }

            }
            fputs(buf,fp);
        }

        if (!skip_mailing && smtp_from!=NULLCHAR && *smtp_from!='\0') /* Disallow if nul ... else loop could exist */
            (void)mailuser(fp, smtp_from, smtp_to);   /* since mailuser() bypasses smtpserv if NUL from */
        fclose(fp);
        free(smtp_to);
        free(smtp_from);
    }
    free(buf);
    return;
}

#else

/* copies news from given newsgroup to the mailbox */

/* 29Dec2004, Replace GOTO 'error' and 'error_ff' LABELS with functions */

static int do_error (char *line, char *path)
{
   free(line);
   free(path);
   return 0;
}

static int do_error_ff (char *line, char *path, struct ffblk *blk)
{
#ifdef UNIX
    findlast(blk);   /* be sure we free any allocs */
#endif
	return (do_error (line, path));
}
    static int
    donndump(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        FILE *t, *f, *o;
        long ctm;
        char *path, *line, *cp, newsname[25], *cp1;
        struct ffblk blk;
  
        if (!Filecheck)
            if(check_system())
                return -1;
  
        if ((f = open_file(Pointer,READ_TEXT,0,1)) == NULLFILE)
            return 0;

        path = mallocw(LineLen);
        line = mallocw(LineLen);
        *path = *line = '\0';

        for(;;) {
            if (fgets(line,LineLen,f) == NULL)
                break;
            if (strcspn(line," ") != strlen(argv[1]))
                continue;
            if (!strnicmp(line,argv[1],strlen(argv[1]))) {
                cp = strchr(line,' ') + 1;
		if ((cp1 = strchr(cp, ' ')) != NULLCHAR)
                    *cp1 = '\0';  /* drop date created */
                strcpy(path,cp);
                break;
            }
            pwait(NULL);
        }
        fclose(f);
  
        if (*path == '\0') {
            tprintf("No newsgroup %s\n",argv[1]);
			return (do_error (line, path));
        }
  
        rip(path);
        cp = strrchr(path,'/');
		/* 07Jun2021, Maiko (VE4KLM), Adding more error checks */
		if (cp == NULLCHAR)
		{
            tprintf("Missing slash in path %s\n", path);
			nntp_err_log (-1, "nndump, missing slash in path", path);
			return (do_error (line, path));
		}
        strcpy(newsname, cp);

        strcpy(line,path);
        strcat(line,"/*.*");
        if(findfirst(line,&blk,0))
		{
            j2tputs("No news in newsgroup\n");
			return (do_error_ff (line, path, &blk));
        }
        if(argc > 2)
            sprintf(newsname,"/%s",argv[2]);
  
        sprintf(line,"%s%s.txt",Mailspool,newsname);
        if ((o = open_file(line,"a+",0,1)) == NULLFILE)
			return (do_error_ff (line, path, &blk));
        if(!(mlock(Mailspool,newsname))) {
            tprintf("Newsgroup dump to %s\n",line);
            for (;;) {
                if((t = temp_file(0,1)) == NULLFILE)
				{
                    fclose(o);
					return (do_error_ff (line, path, &blk));
                }
                sprintf(line,"%s/%s",path,blk.ff_name);
            /* Open the article */
                if ((f = open_file(line,READ_TEXT,0,1)) == NULLFILE)
				{
                    fclose(t);
                    fclose(o);
					return (do_error_ff (line, path, &blk));
                }
                pwait(NULL);
                tputc('.'); /* One dot/article processed */
                tflush();
  
                for (;;) {
                    if (fgets(line,LineLen,f) == NULL)
                        break;
                    fputs(line,t);
                    if (!strnicmp(line,frm,6)) {
                        cp = strchr(line,' ')+1;
                        fprintf(o,"From %s",cp);
                    }
                }
                rewind(t);

                time(&ctm);
                fprintf(o,"%sby %s with NNTP\n\tid AA%ld ; %s",
                    Hdrs[RECEIVED], Hostname, get_msgid(), ptime(&ctm));

                for(;;) {
                    if (fgets(line,LineLen,t) == NULL)
                        break;
                    if(!strncmp(line,"From ",5))  /* 'From XXX' line in article? */
                        fputc('>', o);  /* maintain UNIX mail compatibility */
                    fputs(line,o);
                }
                fputc('\n',o);
                fclose(t);
                fclose(f);
                if (findnext(&blk))
                    break;
            }
            rmlock(Mailspool,newsname);
        } else
            j2tputs("Mailfile is busy, try later");
        fclose(o);
        j2tputs("\n");
  
		return (do_error_ff (line, path, &blk));
    }
#endif  /* NEWS_TO_MAIL */
  
    static int
    donnkick(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        struct Servers *np;
  
        for(np = Nntpserver; np != NULLSERVER; np = np->next)
            if(!stricmp(np->name,argv[1])) {
            /* If the timer is not running, the timeout function has
             * already been called and we don't want to call it again.
             */
                if(run_timer(&np->nntpt) || dur_timer(&np->nntpt) == 0) {
                    stop_timer(&np->nntpt);
                    dopoll((void *)np);
                }
                return 0;
            }
        j2tputs("No server enabled\n");
        return 0;
    }
  
  
#ifdef LZW
/* sets LzwActive flag */
    static int
    donnlzw(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        return setbool(&LzwActive,"NNTP LZW",argc,argv);
    }
#endif
  
  
/* list nntp servers */
    static int
    donnlists(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        struct Servers *np;
        char tbuf[80];
  
        for(np = Nntpserver; np != NULLSERVER; np = np->next) {
            if (np->lowtime != -1 && np->hightime != -1)
                sprintf(tbuf, " -- %02d:%02d-%02d:%02d",
                np->lowtime/100, np->lowtime%100,
                np->hightime/100, np->hightime%100);
            else
                tbuf[0] = '\0';
            tprintf("%-32s (%d/%d%s)\n   Groups: %s\n", np->name,
            	read_timer(&np->nntpt) / 1000, dur_timer(&np->nntpt) / 1000,
            		tbuf, np->newsgroups ? np->newsgroups : "");
        }
        return 0;
    }

/* 29Dec2004, Replace GOTO 'done' with this function instead */ 
static int do_done (FILE *f, struct nntpsv *mp, struct session *sp)
{ 
   if(f != NULLFILE)
       fclose(f);
   keywait(NULLCHAR,1);
   free((char *) mp);
   freesession(sp);
   return 0;
}
  
/* manually entering new news
 * returncode: -1 if error; 0 success */
    static int
    donnpost(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        struct session *sp;
        struct nntpsv *mp;
        char buf[LineLen];
        long id;
        FILE *f, *idf, *ufp;
        long currtime;
		int loopagn = 0;
  
        if (!Filecheck)
            if(check_system())
                return -1;
  
        if((sp = newsession("Post",NN,1)) == NULLSESSION)
            return -1;
  
        mp = (struct nntpsv *)callocw(1,sizeof(struct nntpsv));
  
        for (;;) {
            if ((f = temp_file(0,1)) == NULLFILE)
				return (do_done (f, mp, sp));
  
            id = get_msgid();
            while (Post->user == NULLCHAR) {
                if(prompting_read("User id? ",buf,LineLen) < 0)
					return (do_done (f, mp, sp));
                rip(buf);
                if (strpbrk(buf," !") == NULLCHAR) Post->user = j2strdup(buf);
            }
            fprintf(f,"%s%s\n",pth,Post->user);
            fprintf(f,"%s%s@%s",frm,Post->user,Hostname);
            if (Post->fullname != NULLCHAR)
                fprintf(f," (%s )",Post->fullname);
            fputc('\n',f);
  
            if (prompting_read("Newsgroup? ",buf,LineLen) < 0)
				return (do_done (f, mp, sp));
            rip(buf);
            if (check_blank(buf))
				return (do_done (f, mp, sp));
            fprintf(f,"%s%s\n",ngrps,buf);
  
            if (prompting_read("Subject? ",buf,LineLen) < 0)
				return (do_done (f, mp, sp));
            fprintf(f,"%s%s",subj,buf);
            fprintf(f,"%s<%ld@%s>\n",msgid,id,Hostname);
            time(&currtime);
            fprintf(f,"Date: %s",ptime(&currtime));
            fprintf(f,"Sender: NNTP@%s\n",Hostname);
  
            if (Post->reply != NULLCHAR)
                fprintf(f,"%s%s\n",replyto,Post->reply);
  
            if (Post->organ != NULLCHAR)
                fprintf(f,"Organization: %s\n",Post->organ);
  
            fputc('\n',f);
            j2tputs("Enter message - end with .\n");
  
            for (;;) {
                recvline(sp->input,buf,LineLen);  /* no timeout */
                if(strcmp(buf,".u\n") == 0
                || strcmp(buf,".r\n") == 0) {
                    if(prompting_read("Filename? ",buf,LineLen)<0)
						return (do_done (f, mp, sp));
                    rip(buf);
                    if((ufp = open_file(buf,READ_TEXT,0,1)) != NULLFILE) {
                        while(fgets(buf,LineLen,ufp) != NULL)
                            fputs(buf,f);
                        fclose(ufp);
                    }
                    j2tputs("(continue)\n");
                }
                if(strcmp(buf,".\n") == 0
                    || strcmpi(buf,"***END\n") == 0
                    || strcmpi(buf,"/EX\n") == 0)
                    break;
                fputs(buf,f);
            }
  
            if (Post->sig != NULLCHAR) {
                sprintf(buf,"%s",Post->sig);
                if ((idf = open_file(buf,READ_TEXT,0,1)) != NULLFILE ) {
                    while(fgets(buf,LineLen,idf) != NULL)
                        fputs(buf,f);
                    fclose(idf);
                }
            }
 
			while (1)	/* replaces 'loop:' label used by GOTO */
			{ 

            if(prompting_read("\n[Send, Abort, Exit, List] ",buf,LineLen) < 0)
				return (do_done (f, mp, sp));

			loopagn = 0;

            switch(tolower(buf[0]))
			{
                case 's':
                    rewind(f);
                    sprintf(mp->buf,"<%ld@%s>",id,Hostname);
                    mp->id = j2strdup(mp->buf);
                    if (xfer_article2(f,mp) < 1)
                        j2tputs("\007Posting failed\n");
                    break;
  
                case 'l':
                    rewind(f);
                    for(;;) {
                        if (fgets(buf,LineLen,f) == NULL)
                            break;
                        j2tputs(buf);
                    }
                    rewind(f);
                    loopagn = 1;
 					break;
 
                case 'e':
                    fclose(f);
					return (do_done (f, mp, sp));
					break;
  
                case 'a':
                    break;
  
                default:
                    loopagn = 1;
					break;
            }

			if (!loopagn)	/* break out of while loop */
				break;

			}	/* end of while loop from former 'loop:' GOTO label */

            fclose(f);
            prompting_read("Post another? ",buf,LineLen);

            if (tolower(buf[0]) == 'n')
				break;
        }
  
		return (do_done (f, mp, sp));
    }
  
    static int
    donnquiet(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        return setintrc(&Nntpquiet,"NNTP quiet",argc,argv,0,2);
    }
  
/* -------------------- Profile subcmds -------------------- */
  
    static int
    donnuser(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        if(argc < 2 && Post->user != NULLCHAR)
            tprintf("%s\n",Post->user);
        else {
            free(Post->user);
            Post->user = j2strdup(argv[1]);
        }
        return 0;
    }
  

    static int
    donnsig(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        if(argc < 2 && Post->sig != NULLCHAR)
            tprintf("%s\n",Post->sig);
        else {
            if(access(argv[1],0) == 0) {
                free(Post->sig);
                Post->sig = j2strdup(argv[1]);
            } else {
                j2tputs("No such signature file\n");
                return -1;
            }
        }
        return 0;
    }
  
  
    static int
    donnfull(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        if(argc < 2 && Post->fullname != NULLCHAR)
            tprintf("%s\n",Post->fullname);
        else {
            free(Post->fullname);
            Post->fullname = j2strdup(argv[1]);
        }
        return 0;
    }
  
    static int
    donnhost(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        if(argc < 2 && Host != NULLCHAR)
            tprintf("%s\n",Host);
        else {
            free(Host);
            Host = j2strdup(argv[1]);
        }
        return 0;
    }
  
    static int
    donnihave(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        return setintrc(&NnIhave,"NNTP Ihave",argc,argv,0,2);
    }
  
    static int
    donnmaxcli(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        return setint(&Nntpmaxcli,"NNTP max clients",argc,argv);
    }
  
    static int
    donnorgan(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        if(argc < 2 && Post->organ != NULLCHAR)
            tprintf("%s\n",Post->organ);
        else {
            free(Post->organ);
            Post->organ = j2strdup(argv[1]);
        }
        return 0;
    }
  
    static int
    donnread(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        FILE *f;
        struct session *sp;
        struct article *art;
        char cp[LineLen], buf[81];
        int number, row, flag = argc;
        extern int Numrows, StatusLines;
		int werdone = 0;
  
        if(Curproc->input != Command->input /* must be console user */
           || (art = (struct article *)mallocw(sizeof(struct article))) == NULLARTICLE)
            return -1;
  
        art->group = j2strdup(argv[1]);
        if(get_path2(art) == 1) {
            if(argc > 2) {
                number = atoi(argv[2]);
            } else
                number = 1;
  
            sprintf(cp,"%s/news.rc",art->path);
            if(flag < 3 && (f = fopen(cp,READ_TEXT)) != NULLFILE) {
                if((fgets(buf,sizeof(buf),f)) != 0) {
                    number = atoi(buf);
                    number++;
                }
                fclose(f);
            }
            if((sp = newsession("NNTP read",NN,0)) != NULLSESSION)
			{
                sp->ttystate.echo = sp->ttystate.edit = 0;
                for(;;)
				{
                    if(number < 1)
                        number = 1;
                    sprintf(cp,"%s/%d",art->path,number);
 
					werdone = 0; 

                    if((f = fopen(cp,READ_TEXT)) != NULLFILE)
					{
                        row = Numrows - 1 - StatusLines;
                        tprintf("Msg #%d\n",number);
                        while(fgets(buf,sizeof(buf),f),!feof(f))
						{
                            j2tputs(buf);
                            if(--row == 0)
							{
                                row = keywait(TelnetMorePrompt,0);
                                switch(row)
								{
                                    case -1:
                                    case 'q':
                                        werdone = 1;
										break;
                                    case '\n':
                                    case '\r':
                                        row = 1;
                                        break;
                                    default:
                                        row = Numrows - 1 - StatusLines;
                                }
								if (werdone)	/* break out of while loop */
									break;
                            }
                        }
                        fclose(f);
                    }
					else
					{
                        number--;
                        j2tputs("No more news");
                    }

                    row = keywait("\nRead next/previous? (n/p/q)",0);

					werdone = 0;

                    switch(row)
					{
                        case -1:
                        case 'q':
                            werdone = 1;
							break;
                        case 'p':
                            flag = 3;
                            if(--number < 1)
							{
                            	werdone = 1;
								break;
							}
                            continue;
                        default:
                            number++;
                            continue;
                    }

					if (werdone)	/* break out of for loop */
						break;
                }

                if(flag < 3)
				{
                    sprintf(cp,"%s/news.rc",art->path);
                    if((f = fopen(cp,WRITE_TEXT)) != NULLFILE) {
                        fprintf(f,"%d\n",number);
                        fclose(f);
                    }
                }
                keywait(NULLCHAR,1);
                freesession(sp);
            }
        } else {
            tprintf("No such newsgroup %s\n",art->group);
        }
        free(art->path);
        free(art->group);
        free(art);
        return 0;
    }
  
    static int
    donnreply(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        if(argc < 2 && Post->reply != NULLCHAR)
            tprintf("%s\n",Post->reply);
        else {
            free(Post->reply);
            Post->reply = j2strdup(argv[1]);
        }
        return 0;
    }
  
  
  
/* subcmd parser */
  
    static int
    donnprofile(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        static struct cmds DFAR Prof[] = {
            { "fullname",     donnfull,       0, 0, NULLCHAR },
            { "host",         donnhost,       0, 0, NULLCHAR },
            { "organ",        donnorgan,      0, 0, NULLCHAR },
            { "reply",        donnreply,      0, 0, NULLCHAR },
            { "sig",          donnsig,        0, 0, NULLCHAR },
            { "user",         donnuser,       0, 0, NULLCHAR },
            { NULLCHAR,		NULL,			0, 0, NULLCHAR }
        };
  
        if(Post == NULLPOST) {
            Post = (struct post *)callocw(1,sizeof(struct post));
            Post->user = Post->reply = Post->sig = Post->organ = Post->fullname = NULLCHAR;
        }
  
        if (Host == NULLCHAR) {
            Host = j2strdup(Hostname);
        }
  
        return (argc == 10) ? 0 : (subcmd(Prof,argc,argv,p));
    }
  
  
  
/* cmd parser */
  
    int
    donntp(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
  
        static struct cmds DFAR Nntp[] = {
            { "active",  donnactive, 0, 0, NULLCHAR },
            { "access",  donnaccess, 0, 0, NULLCHAR },
            { "add",     donnadds,   0, 3, "nntp add <nntpserv> <interval> [<groups>]" },
            { "create",  donncreate, 0, 2, "nntp create <newsgroup> [y|n]" },
            { "drop",    donndrops,  0, 2, "nntp drop <nntpserv>" },
#ifndef NEWS_TO_MAIL
            { "dump",    donndump,   0, 2, "nntp dump <newsgroup> [<mailbox>]" },
#endif
            { "firstpoll", donnfirstpoll,	0, 0, NULLCHAR },
            { "ihave",   donnihave,  0, 0, NULLCHAR },
            { "kick",    donnkick,   0, 2, "nntp kick <server>" },
            { "list",    donnlists,  0, 0, NULLCHAR },
#ifdef LZW
            { "lzw",     donnlzw,    0, 0, NULLCHAR },
#endif
            { "maxclients", donnmaxcli,  0, 0, NULLCHAR },
            { "post",    donnpost,    2024, 0, NULLCHAR },
            { "profile", donnprofile, 0,    0, NULLCHAR },
            { "read",    donnread,    1024, 2, "nntp read <newsgroup> [number]" },
            { "quiet",   donnquiet,   0,    0, NULLCHAR },
            { NULLCHAR,	NULL,		0, 		0, NULLCHAR }
        };
  
        return (subcmd(Nntp,argc,argv,p));
    }
  
/* main file-opening routine
 * options: s = socketnumber, if given an error msg is printed to the socket
 * returncode: NULLFILE if error; filepointer success
 */
    FILE *
    open_file(name,mode,s,t)
    char *name;
    char *mode;
    int s;
    int t;
    {
        FILE *f;
#ifdef MSDOS
        register char *cp;
#endif
  
        if(name == NULLCHAR || mode == NULLCHAR)
            return NULLFILE;
#ifdef MSDOS
        while((cp = strchr(name,'\\')) != NULLCHAR)
            *cp = '/';
#endif
        if((f = fopen(name,mode)) == NULLFILE) {
            if(s)
                usprintf(s,fatal,name);
            if(t)
                tprintf("Can't open %s: %s\n",name,strerror(errno));
        }
        return f;
    }
  
/* main tempfile-opening routine
 * returncode: NULLFILE if error; filepointer success
 */
  
    FILE *
    temp_file(s,t)
    int s;
    int t;
    {
        FILE *f;
  
        if((f = tmpfile()) == NULLFILE) {
            if(t) {
                tprintf("Can't open TMP: %s\n",strerror(errno));
            }
            if(s)
                usprintf(s,fatal,"TMP");
        }
        return f;
    }
  
static void
make_time_string(char *string,time_t *timep)
{
	struct tm *stm;

	stm = localtime(timep);
	sprintf(string,"%02d%02d%02d %02d%02d%02d",
	  stm->tm_year%100,stm->tm_mon + 1,stm->tm_mday,stm->tm_hour,
	  stm->tm_min,stm->tm_sec);
}

static int
prompting_read(char *prompt, char *buf, int buflen)
{
    int nread;

    j2tputs(prompt);
    tflush();

/*    j2alarm(10000);*/
    nread = recvline(Curproc->input,buf,buflen);  /* no timeout */
/*    j2alarm(0);*/

    return nread;
}

#endif /* NNTPS */
