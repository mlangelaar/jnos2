/*
 *  Client routines for Network News Tranfer Protocol ala RFC977
 *
 *  Copyright 1990 Anders Klemets - SM0RGV, All Rights Reserved.
 *  Permission granted for non-commercial copying and use, provided
 *  this notice is retained.
 *
 *  Changes copyright 1990 Bernie Roehl, All Rights Reserved.
 *  Permission granted for non-commercial copying and use, provided
 *  this notice is retained.
 *
 *  Revision history:
 *
 *     May 11, 1990 - br checked for invalid chars in news filenames
 *
 *     May 10, 1990 - br changed date stamp in 'From ' lines to
 *            seconds since GMT (to make parsing and expiry easier)
 *
 *     May 9, 1990 - br added locking of nntp.dat and history files,
 *            second parameter to NNTP DIR, fixed bug in updating of
 *            nntp.dat
 *
 *     early May, 1990 -- br added NNTP TRACE, NNTP DIR,
 *            server-specific newsgroups and connection windows,
 *            locking of newsgroup files using mlock() and rmlock(),
 *            date stamping of 'From ' lines, increased stack space,
 *            updating of nntp.dat only on successful sessions.
 *
 *     July 19, 1990 pa0gri Delinted and cleaned up. (calls and includes)
 *
 *     Jun 30, 1994  n5knx  Added timestamp to history file to aid expiration,
 *            and build index if newsgroup is stored under Mailspool.  Create
 *            ng.inf timestamp file if ng is in areas file.
 *     Jul 1994  n5knx  reimplemented newsgroup name mapping.  Disallow kicking
 *            a running process.  Avoid --more-- wait when !nntpquiet tracing.
 *            Add LZW support by N8FOW.  Compensate for GMT.
 *     Aug 1996  n5knx  add NN_USESTAT code to handle a rejected NEWNEWS cmd.
 *           Also check for duplicates against ax.25 bulletin bids.
 */
#include <sys/types.h>
#include <time.h>
#include <sys/timeb.h>
#include <ctype.h>
#ifdef  __TURBOC__
#include <dir.h>
#endif
#include "global.h"
#ifdef  NNTP
#include "timer.h"
#include "cmdparse.h"
#include "commands.h"
#include "socket.h"
#include "usock.h"
#include "netuser.h"
#include "proc.h"
#include "session.h"
#include "smtp.h"
#include "mailutil.h"
#include "files.h"
#include "bm.h"
#include "index.h"
#ifdef LZW
#include "lzw.h"
#endif
  
#ifndef NNTP_TIMEOUT
#define NNTP_TIMEOUT 900 /* in seconds */
#endif

#ifdef STATUSWIN
#ifdef NNTPS
extern
#endif
int NntpUsers;
#endif // STATUSWIN
extern int UtcOffset;

#define NNTPMAXLEN  512
  
struct nntpservers {
    struct timer nntpcli_t;
    char *name;
    char *groups;
    int lowtime, hightime;  /* for connect window */
    struct nntpservers *next;
};
  
#define NULLNNTP    (struct nntpservers *)NULL

struct ngmap {
    char *prefix;        /* e.g. comp, rec.radio, net, talk, alt ... */
    char *newname;       /* what prefix should be changed to */
    struct ngmap *next;  /* link to next entry */
};
  
static struct nntpservers *Nntpservers = NULLNNTP;
static char *Nntpgroups = NULLCHAR;
static unsigned short nntptrace = 1;
static int nntpquiet = 0;
static int Nntpfirstpoll = 5;
static char *News_spool = NULL;
static int np_all = 0;  /* non-zero if Newsdir is a malloc'ed space */
static struct ngmap *ngmaphead = NULL;
  
static char validchars[] = "abcdefghijklmnopqrstuvwxyz0123456789-_.";
static char WOpenMsg[] = "NNTP window to '%s' not open\n";
static char CFailMsg[] = "NNTP %s: Connect failed: %s";
static char BadBannerMsg[] = "NNTP %s: bad reply on banner (response was %d)";
static char NoLockMsg[] = "NNTP %s: Cannot lock %s";
static char NoOpenMsg[] = "NNTP %s: Cannot open %s";
static char NoUpdateMsg[] = "NNTP %s: Could not update %s";
static char ProtoErrMsg[] = "NNTP %s: Protocol error (response was %d)";
static char GTFailMsg[] = "NNTP %s: Giving up: gettxt() failure";
static char GAFailMsg[] = "NNTP %s: Giving up: could not get article";
  
static void nntptick __ARGS((void *tp));
static void nntp_job __ARGS((int i1,void *tp,void *v1));
static int gettxt __ARGS((int s,FILE *fp));
static int getreply __ARGS((int s, char *buf));
static int getarticle __ARGS((int s,char *msgid, char *nglist));
static int dogroups __ARGS((int argc,char *argv[],void *p));
static int doadds __ARGS((int argc,char *argv[],void *p));
static int dodrops __ARGS((int argc,char *argv[],void *p));
static int dokicks __ARGS((int argc,char *argv[],void *p));
static int dolists __ARGS((int argc,char *argv[],void *p));
static int donntrace __ARGS((int argc,char *argv[],void *p));
static int donnquiet __ARGS((int argc,char *argv[],void *p));
static int dondir __ARGS((int argc,char *argv[],void *p));
static int donnfirstpoll __ARGS((int argc,char *argv[],void *p));
static void make_time_string __ARGS((char *string,time_t *timep));
static int statnew __ARGS((FILE *artidf,int s,char *nglist,struct nntpservers *np,char *buf));
static void logerr __ARGS((int s,int severity,char *ErrMsg,char *Arg1,char *Arg2));

#ifdef LZW
static int donnlzw __ARGS((int argc,char *argv[],void *p));
int LzwActive = 1;
#endif
  
/* Tracing levels:
    0 - no tracing
    1 - serious errors reported
    2 - transient errors reported
    3 - session progress reported
    4 - actual received articles displayed
 */
  
static struct cmds DFAR Nntpcmds[] = {
    "addserver",    doadds,     0,  3,
        "nntp addserver <nntpserver> <interval> [<groups>]",
    "directory",    dondir,     0,  0,  NULLCHAR,
    "dropserver",   dodrops,    0,  2,  "nntp dropserver <nntpserver>",
    "firstpoll",    donnfirstpoll, 0, 0, NULLCHAR,
    "groups",       dogroups,   0,  0,  NULLCHAR,
    "kick",         dokicks,    0,  2,  "nntp kick <nntpserver>",
    "listservers",  dolists,    0,  0,  NULLCHAR,
#ifdef LZW
    "lzw",          donnlzw,    0,  0,  NULLCHAR,
#endif
    "quiet",        donnquiet,  0,  0,  NULLCHAR,
    "trace",        donntrace,  0,  0,  NULLCHAR,
    NULLCHAR,		NULL,		0,  0,	NULLCHAR
};
  
int
donntp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Nntpcmds,argc,argv,p);
}
  
static int
doadds(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct nntpservers *np;
    for(np = Nntpservers; np != NULLNNTP; np = np->next)
        if(stricmp(np->name,argv[1]) == 0)
            break;
    if (np == NULLNNTP) {
        np = (struct nntpservers *) callocw(1,sizeof(struct nntpservers));
        np->name = j2strdup(argv[1]);
        np->next = Nntpservers;
        Nntpservers = np;
        np->groups = NULLCHAR;
        np->lowtime = np->hightime = -1;
        np->nntpcli_t.func = nntptick;  /* what to call on timeout */
        np->nntpcli_t.arg = (void *)np;
    }
    if (argc > 3) {
        int i;
        if (np->groups == NULLCHAR) {
            np->groups = mallocw(NNTPMAXLEN);
            *np->groups = '\0';
        }
        for (i = 3; i < argc; ++i) {
            if (isdigit(*argv[i])) {
                int lh, ll, hh, hl;
                sscanf(argv[i], "%d:%d-%d:%d", &lh, &ll, &hh, &hl);
                np->lowtime = lh * 100 + ll;
                np->hightime = hh * 100 + hl;
            } else if ((strlen(np->groups)+strlen(argv[i])+2) >= NNTPMAXLEN)
                tprintf("Group list too long!  Group '%s' ignored!\n", argv[i]);
            else {  /* it's a group, and it fits... add it to list */
                if (*np->groups != '\0')
                    strcat(np->groups, ",");
                strcat(np->groups, argv[i]);
            }
        }
        if (*np->groups == '\0') {  /* No groups specified? */
            free(np->groups);
            np->groups = NULLCHAR;
        }
    }
    /* set timer duration */
	/* 11Oct2009, Maiko, set_timer uses uint32, not atol */
    set_timer(&np->nntpcli_t,(uint32)atoi(argv[2])*1000);
    start_timer(&np->nntpcli_t);        /* and fire it up */
    return 0;
}
  
static int
dodrops(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct nntpservers *np, *npprev = NULLNNTP;
    for(np = Nntpservers; np != NULLNNTP; npprev = np, np = np->next)
        if(stricmp(np->name,argv[1]) == 0) {
            stop_timer(&np->nntpcli_t);
            free(np->name);
            if (np->groups)
                free(np->groups);
            if(npprev != NULLNNTP)
                npprev->next = np->next;
            else
                Nntpservers = np->next;
            free((char *)np);
            return 0;
        }
    j2tputs("No such server enabled.\n");
    return 0;
}
  
static int
dolists(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct nntpservers *np;
    for(np = Nntpservers; np != NULLNNTP; np = np->next) {
        char tbuf[80];
        if (np->lowtime != -1 && np->hightime != -1)
            sprintf(tbuf, " -- %02d:%02d-%02d:%02d", np->lowtime/100, np->lowtime%100, np->hightime/100, np->hightime%100);
        else
            tbuf[0] = '\0';
        tprintf("%-32s (%d/%d%s) %s\n", np->name,
        read_timer(&np->nntpcli_t) /1000,
        dur_timer(&np->nntpcli_t) /1000,
        tbuf, np->groups ? np->groups : "");
    }
    return 0;
}
  
#ifdef LZW
/* Sets LzwActive flag */
static int
donnlzw(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&LzwActive,"NNTP lzw",argc,argv);
}
#endif /* LZW */
  
static int donntrace(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    return setshort(&nntptrace,"NNTP tracing",argc,argv);
}
  
static int donnquiet(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    return setbool(&nntpquiet,"NNTP quiet",argc,argv);
}
  
static int
donnfirstpoll(argc,argv,p)	/* from G8FSL */
int argc;
char *argv[];
void *p;
{
	return setint(&Nntpfirstpoll,"NNTP polls new server for news over last <n> days",argc,argv);
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

static int dondir(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    struct ngmap *ngp, *prev_ngp;

    if (argc < 2) {
        int i;
        tprintf("spool: %s\n", News_spool ? News_spool : Mailspool);
        tprintf("control: %s\n", Newsdir);

        for (ngp = ngmaphead; ngp != NULL; ngp=ngp->next)
             if (ngp->prefix) tprintf("%s = %s\n", ngp->prefix, ngp->newname);
    } else {
        char *p;
        if ((p = strchr(argv[1], '=')) != NULLCHAR) {  /* set a prefix mapping */
            int i;
            *p++ = '\0';
            for (ngp=ngmaphead, prev_ngp=NULL; ngp != NULL; prev_ngp=ngp, ngp=ngp->next)
                if (ngp->prefix)
                    if (!stricmp(ngp->prefix, argv[1])) {
                        if (ngp->newname) {
                            free(ngp->newname);
                            ngp->newname = NULLCHAR;
                        }
                        if (*p == '=') {
                            free(ngp->prefix);
                            ngp->prefix = NULLCHAR;
                        } else
                            ngp->newname = j2strdup(p);
                        return 0;
                    }
            if (*p == '=')  /* trashing a mapping that's not there */
                return 0;
            ngp=mallocw(sizeof (struct ngmap));
            ngp->prefix = j2strdup(argv[1]);
            ngp->newname = j2strdup(p);
            ngp->next = (struct ngmap *)NULL;
            if (prev_ngp) prev_ngp->next = ngp;
            else ngmaphead = ngp;
            return 0;
        }
        else  /* no '=', so just set default */
        {
            if (News_spool)
                free(News_spool);
            News_spool = j2strdup(argv[1]);
        }
        if (argc > 2) {  /* they specified a newsdir as well */
            if (np_all)
                free(Newsdir);
            Newsdir = j2strdup(argv[2]);
            np_all = 1;
        }
    }
    return 0;
}
  
static int
dokicks(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct nntpservers *np;
    for(np = Nntpservers; np != NULLNNTP; np = np->next)
        if(stricmp(np->name,argv[1]) == 0) {
            /* If the timer is not running, the timeout function has
            * already been called, so we don't want to start another process.
            */
            if(run_timer(&np->nntpcli_t)) {
                stop_timer(&np->nntpcli_t);
                nntptick((void *)np);
            }
            return 0;
        }
    j2tputs("No such server enabled.\n");
    return 1;
}
  
static int
dogroups(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int i;
    if(argc < 2) {
        if(Nntpgroups == NULLCHAR || (Nntpgroups != NULLCHAR && strcmp(Nntpgroups,"*") == 0))
            j2tputs("All groups are currently enabled.\n");
        else
            tprintf("Currently enabled newsgroups:\n%s\n",Nntpgroups);
        return 0;
    }
    if(Nntpgroups == NULLCHAR)
        Nntpgroups = mallocw(NNTPMAXLEN);
    *Nntpgroups = '\0';
    for(i=1; i < argc; ++i) {
        if(i > 1)
            strcat(Nntpgroups,",");
        strcat(Nntpgroups,argv[i]);
    }
    return 0;
}
  
/* This is the routine that gets called every so often to connect to
 * NNTP servers.
 */
static void
nntptick(tp)
void *tp;
{
    if (newproc("NNTP client", 3072, nntp_job, 0, tp, NULL,0) == NULLPROC)
        start_timer(&((struct nntpservers *)tp)->nntpcli_t);    /* N5KNX: retry later */

}

/* 29Dec2004, Maiko, This function replaces GOTO 'quit' LABEL */
static void do_quit (int s, struct nntpservers *np)
{
    if (nntptrace >= 3)
        j2tputs("NNTP daemon exiting\n");
    close_s(s);
#ifdef STATUSWIN
    NntpUsers--;
#endif
    /* Restart timer */
    start_timer(&np->nntpcli_t);
    return;
}

static void
nntp_job(i1,tp,v1)
int i1;
void *tp, *v1;
{
    FILE *fp, *tmpf;
    int s = -1, i;
/*  long pos; */
    struct tm *ltm;
    time_t t;
    int now;
    struct nntpservers *np = (struct nntpservers *) tp;
    struct sockaddr_in fsocket;
    char tbuf[NNTPMAXLEN], buf[NNTPMAXLEN], *cp, *lastdate = NULLCHAR;
    char currdate[20];
    char GMT[] = " GMT";
#ifdef LZW
    int lzwmode, lzwbits, ret;
    extern int16 Lzwbits;
    extern int Lzwmode;
#endif /* LZW */

    if (nntptrace >= 3)
        tprintf("NNTP daemon entered, target = %s\n",np->name);
#ifndef UNIX
    if(availmem() < Memthresh + 4000L){
        if (nntptrace >= 2)
            j2tputs("NNTP daemon quit -- low memory\n");
        /* Memory is tight, don't do anything */
        start_timer(&np->nntpcli_t);
        return;
    }
#endif
  
    time(&t);   /* more portable than gettime() */
    ltm = localtime(&t);
    now = ltm->tm_hour * 100 + ltm->tm_min;
    if (np->lowtime < np->hightime) {  /* doesn't cross midnight */
        if (now < np->lowtime || now >= np->hightime) {
            if (nntptrace >= 3)
                tprintf(WOpenMsg, np->name);
            start_timer(&np->nntpcli_t);
            return;
        }
    } else {
        if (now < np->lowtime && now >= np->hightime) {
            if (nntptrace >= 3)
                tprintf(WOpenMsg, np->name);
            start_timer(&np->nntpcli_t);
            return;
        }
    }
  
    fsocket.sin_addr.s_addr = resolve(np->name);
    if(fsocket.sin_addr.s_addr == 0) {  /* No IP address found */
        if (nntptrace >= 2)
            tprintf("NNTP can't resolve host '%s'\n", np->name);
        /* Try again later */
        start_timer(&np->nntpcli_t);
        return;
    }
    fsocket.sin_family = AF_INET;
    fsocket.sin_port = IPPORT_NNTP;
  
    s = j2socket(AF_INET,SOCK_STREAM,0);
    sockmode(s,SOCK_ASCII);
#ifdef STATUSWIN
    NntpUsers++;
#endif
    if(j2connect(s,(char *)&fsocket,SOCKSIZE) == -1){
        cp = sockerr(s);
        if (!cp) cp="";
        logerr(s,2,CFailMsg,psocket(&fsocket),cp);
	return (do_quit (s, np));
    }
    /* Eat the banner */
    i = getreply(s,buf);
    if(i == -1 || i >= 400) {
        logerr(s,1,BadBannerMsg,psocket(&fsocket),(char *)i);
	return (do_quit (s, np));
    }
  
#ifdef NN_INN_COMPAT
    usputs(s,"mode reader\n");   /* cause INNd to spawn nnrpd */
    getreply(s,buf);             /* throw away any response */
#endif

#ifdef LZW
    if(LzwActive) {
        usprintf(s,"XLZW %d %d\n",Lzwbits,Lzwmode);
        usflush(s);
        if(recvline(s,buf,NNTPMAXLEN) == -1)
			return (do_quit (s, np));
        rip(buf);

        ret = lzwmode = lzwbits = 0;
        sscanf(buf,"%d %d %d",&ret,&lzwbits,&lzwmode);

        /* Eat negative response */
        if((ret >= 200) && (ret < 300)) {
            if(lzwmode != Lzwmode || lzwbits != Lzwbits) {
                lzwmode = LZWCOMPACT;
                lzwbits = LZWBITS;
            }
            lzwinit(s,lzwbits,lzwmode);
        }
        /* else not supported */
    }
#endif /* LZW */

    if (mlock(Newsdir, "nntp")) {
        logerr(s,2,NoLockMsg, psocket(&fsocket), "nntp.dat");
		return (do_quit (s, np));
    }
    sprintf(buf,"%s/nntp.dat",Newsdir);
    if((fp = fopen(buf,APPEND_TEXT)) == NULLFILE) {
        logerr(s,1,NoOpenMsg,psocket(&fsocket),buf);
        rmlock(Newsdir, "nntp");
		return (do_quit (s, np));
    }
    rewind(fp);
/*  for(pos=0L; fgets(buf,NNTPMAXLEN,fp) != NULLCHAR;pos=ftell(fp)) { */
    for(; fgets(buf,NNTPMAXLEN,fp) != NULLCHAR;) {
        if((cp = strchr(buf,' ')) == NULLCHAR)
            continue;   /* something wrong with this line, skip it */
        *cp++ = '\0';
        if(stricmp(buf,np->name) == 0) {
            rip(cp);
            lastdate = j2strdup(cp);
            break;
        }
    }
    fclose(fp);
    rmlock(Newsdir, "nntp");
  
    if(lastdate == NULLCHAR) {
        if (Nntpfirstpoll < 0)
            lastdate = j2strdup("900101 000000");
        else {    /* from G8FSL */
            lastdate = mallocw(15);
            time(&t);
            t -= Nntpfirstpoll*86400L;
            make_time_string(lastdate,&t);
        }
    }

    /* snapshot the time for use later in re-writing nntp.dat */
    time(&t);
/* N5KNX: we must tell the nntp server if our reference time is UTC/GMT or local time. */
#ifdef MSDOS
    ltm = localtime(&t);   /* localtime calls tzset() which sets timezone */
    if (UtcOffset == 0L) strcpy(GMT, " GMT");  /* you get EST if TZ env var is unset */
    else GMT[0] = '\0';
#else
    ltm = gmtime(&t);   /* BSD vs SysV vs Linux vs Apple vs ??? */
    i = ltm->tm_hour;
    ltm = localtime(&t);
    if (i == ltm->tm_hour) strcpy(GMT, " GMT");
    else GMT[0] = '\0';
#endif

    sprintf(currdate,"%02d%02d%02d %02d%02d%02d",ltm->tm_year%100,ltm->tm_mon+1,
        ltm->tm_mday,ltm->tm_hour,ltm->tm_min,ltm->tm_sec);
  
    if((tmpf = tmpfile()) == NULLFILE) {
        logerr(s,1,NoOpenMsg, psocket(&fsocket), "tmpfile");
		return (do_quit (s, np));
    }

    /* Get a list of new message-id's */
    if (np->groups) cp=np->groups;
    else cp=Nntpgroups != NULLCHAR ? Nntpgroups : "*";
    if (nntptrace >= 3)
        tprintf("==>NEWNEWS %s %s%s\n", cp, lastdate, GMT);
#ifdef TESTNN_USESTAT
    if (1) {
        free(lastdate);
#else
    usprintf(s,"NEWNEWS %s %s%s\n", cp, lastdate, GMT);
    free(lastdate);

    /* Get the response */
    if((i = getreply(s,buf)) != 230) { /* NEWNEWS rejected? */
#endif /* TESTNN_USESTAT */
#ifdef NN_USESTAT
/* if NEWNEWS isn't supported, we can try {GROUP, STATi} foreach group */
        if (statnew(tmpf, s, cp, np, buf) == -1)
#endif /* NN_USESTAT */
        {
            logerr(s,1,ProtoErrMsg,psocket(&fsocket),(char *)i);
            fclose(tmpf);
			return (do_quit (s, np));
        }
    }
    else if(gettxt(s,tmpf) == -1) {
        logerr(s, 1, GTFailMsg, psocket(&fsocket),"");
        fclose(tmpf);
		return (do_quit (s, np));
    }
  
    /* Open the history file */
    i=0;
    while (mlock(History, NULLCHAR)) {
        j2pause(1000);
        if (++i == 60) {
            logerr(s, 1, NoLockMsg, psocket(&fsocket), History);
            fclose(tmpf);
			return (do_quit (s, np));
        }
    }
    if((fp = fopen(History,APPEND_TEXT)) == NULLFILE) {
        logerr(s,1,NoOpenMsg, psocket(&fsocket), History);
        fclose(tmpf);
		return (do_quit (s, np));
    }
    /* search through the history file for matching message id's */
    rewind(tmpf);
    while(fgets(tbuf,NNTPMAXLEN,tmpf) != NULLCHAR) {
#ifdef MBFWD
        char *p;
        int from_ax25_fwding;
#endif

        rip(tbuf);
#ifdef MBFWD
        from_ax25_fwding = 
#endif
        i = 0;
        rewind(fp);
        while(fgets(buf,NNTPMAXLEN,fp) != NULLCHAR) {
            if(strnicmp(buf,tbuf,strlen(tbuf)) == 0) {
                i = 1;  /* found a match */
                break;
            }
            pwait(NULL);
        }
#ifdef MBFWD
        /* Ignore news article if we have it already via ax.25 forwarding */
        if(i == 0) {
            if((p = strchr(tbuf,'@')) != NULLCHAR) {
                /* A trailing ".bbs" indicates that the Message-ID was generated
                 * from a BBS style message, and not a RFC-822 message.
                 */
                if(stricmp(p+strlen(p)-5, ".bbs>") == 0) {
                    *p = '\0';
                    from_ax25_fwding++;
                    if (msgidcheck(s, &tbuf[1]))  /* already in bid history file? */
                        i=1;  /* yes, do not xfer article */
                    *p = '@';
                }
            }
        }
#endif
        if(i == 0) {        /* not found, get the article */
            if(getarticle(s,tbuf,cp) == -1) {
                logerr(s,2,GAFailMsg,psocket(&fsocket),"");
                fclose(fp);
                rmlock(History, NULLCHAR);
                fclose(tmpf);
				return (do_quit (s, np));
            }
            fprintf(fp,"%s %s\n",tbuf,currdate); /* add the new message id + timestamp */
#ifdef MBFWD
            if (from_ax25_fwding) {
                *p = '\0';
                storebid(&tbuf[1]); /* Save BID */
            }
#endif
        }
    }
    fclose(fp);
    rmlock(History, NULLCHAR);
    fclose(tmpf);
    if (nntptrace >= 3)
        j2tputs("==>QUIT\n");
    usputs(s,"QUIT\n");
    /* Eat the response */
    getreply(s,buf);
    /* NOW, update the nntp.dat file */
    if (mlock(Newsdir, "nntp")) {
        if (nntptrace >= 2)
            tprintf(NoLockMsg, psocket(&fsocket), "nntp.dat (update)\n");
		return (do_quit (s, np));
    }
    sprintf(buf,"%s/nntp.dat",Newsdir);
    fp = fopen(buf,READ_TEXT);
    sprintf(buf, "%s/nntp.tmp",Newsdir);
    if ((tmpf = fopen(buf, WRITE_TEXT)) == NULLFILE)
        if (nntptrace >= 1)
            tprintf(NoOpenMsg,psocket(&fsocket),buf), tputc('\n');
    if (fp == NULLFILE || tmpf == NULLFILE) {
        logerr(s,2,NoUpdateMsg, psocket(&fsocket), buf);
        if (fp)
            fclose(fp);
        if (tmpf)
            fclose(tmpf);
        rmlock(Newsdir, "nntp");
		return (do_quit (s, np));
    }
    while (fgets(tbuf, sizeof(tbuf), fp))
        if (strnicmp(tbuf, np->name, strlen(np->name)))
            fputs(tbuf, tmpf);
    fprintf(tmpf,"%s %s\n",np->name,currdate);
    fclose(fp);
    fclose(tmpf);
    sprintf(buf, "%s/nntp.dat", Newsdir);
    sprintf(tbuf, "%s/nntp.tmp", Newsdir);
    unlink(buf);
    rename(tbuf, buf);
    rmlock(Newsdir, "nntp");

	return (do_quit (s, np));
}
  
static int
gettxt(s,fp)
int s;
FILE *fp;
{
    char buf[NNTPMAXLEN];
    int nlines=0;
    int continued=0;

#if NNTP_TIMEOUT
    while (j2alarm(NNTP_TIMEOUT*1000), (recvline(s,buf,NNTPMAXLEN) != -1)) {
#else
    while (recvline(s,buf,NNTPMAXLEN) != -1) {
#endif
#if NNTP_TIMEOUT
        j2alarm(0);
#endif
        if (nntptrace >= 4)
            tprintf("<==%s", buf);
        if (!continued) {
            if(strcmp(buf,".\n") == 0) {
                if (nntptrace >= 3)
                    tprintf("NNTP received %d lines\n", nlines);
                return 0;
            }
            /* check for beginning '.' character */
            if(*buf == '.')
                memmove(buf, buf+1, strlen(buf));

            ++nlines;
        }
        fputs(buf,fp);
        continued = (strchr(buf,'\n') == NULLCHAR);
    }
#if NNTP_TIMEOUT
    j2alarm(0);
#endif
    if (nntptrace >= 1)
        tprintf("NNTP receive error after %d lines\n", nlines);
    return -1;
}
  
static int
getreply(s,buf)
int s;
char *buf;   /* NNTPMAXLEN bytes long */
{
    int response;
#if NNTP_TIMEOUT
    j2alarm(NNTP_TIMEOUT*1000);
#endif
    while(recvline(s,buf,NNTPMAXLEN) != -1) {
#if NNTP_TIMEOUT
        j2alarm(0);
#endif
        /* skip informative messages and blank lines */
        if(buf[0] == '\0' || buf[0] == '1')
            continue;
        sscanf(buf,"%d",&response);
        if (nntptrace >= 3)
            tprintf("<==%s\n", buf);
        return response;
    }
#if NNTP_TIMEOUT
    j2alarm(0);
#endif
    if (nntptrace >= 3)
        j2tputs("==No response\n");
    return -1;
}
  
static int
getarticle(s,msgid,nglist)
int s;
char *msgid, *nglist;
{
    char buf[NNTPMAXLEN], *froml=NULLCHAR, *newgl=NULLCHAR;
    char ng[FILE_PATH_SIZE], *cp, *cp1;
    FILE *fp, *tmpf;
    int i,continued,continuation;
    extern int Smtpquiet;
    time_t t;
    long start;
    struct mailindex ind;
  
    if (nntptrace >= 3)
        tprintf("==>ARTICLE %s\n", msgid);
    usprintf(s,"ARTICLE %s\n", msgid);
    i = getreply(s,buf);
    if(i == -1 || i >= 500)
        return -1;
    if(i >= 400)
        return 0;
    if((tmpf = tmpfile()) == NULLFILE) {
        if (nntptrace >= 1)
            j2tputs("NNTP Cannot open temp file for article\n");
        return -1;
    }
    if(gettxt(s,tmpf) == -1) {
        fclose(tmpf);
        return -1;
    }
    /* convert the article into mail format */
    rewind(tmpf);
    continuation=0;
    while(fgets(buf,NNTPMAXLEN,tmpf) != NULLCHAR) {
        if(strncmp(buf,"From: ",6) == 0) {
            rip(&buf[6]);
            froml=j2strdup(&buf[6]);
            if(newgl != NULLCHAR)
                break;
        }
        if(strncmp(buf,"Newsgroups: ",12) == 0) {
            newgl=j2strdup(&buf[12]);
            if(froml != NULLCHAR)
                break;
        }
        if(!continuation && strcmp(buf,"\n") == 0 && (froml == NULLCHAR || newgl == NULLCHAR)) {
            /* invalid article - missing 'From:' line or 'Newsgroups:' line */
/*          fclose(fp); */
            fclose(tmpf);
            free(froml);
            free(newgl);
            return 0;
        }
        continuation = (strchr(buf,'\n') == NULLCHAR);
    }
    /* Clear the index to start */
    memset(&ind,0,sizeof(ind));
  
    for(i=0,cp=newgl;*cp;++cp)
	{
        if(*cp == ',' || *cp == '\n')
		{
            char *tempdir=NULLCHAR, *prefix, *p;
            struct ngmap *ngp;

            ng[i] = '\0';

            /* is this article's newsgroup in our list? */
            for(cp1=nglist; *cp1; )
			{
                int j, ending_star=0;

                j=strcspn(cp1,",");
                if (*(cp1+j-1) == '*')
                    j--, ending_star++;
                if (!strnicmp(cp1, ng, j) && (ending_star || j==i))
                    break;   /* ng matches up to comma, or asterisk */
                cp1 += j+ending_star;
                if (*cp1 == ',') cp1++;
            }

            if (!*cp1)
			{
            	if (*cp == '\n')
                	break;
            	i=0;
            	continue;
			}

            /* map name of newsgroup as required */
            for (ngp=ngmaphead; ngp!=NULL; ngp=ngp->next) {
                int j;
                if (ngp->prefix) {
                    if ((j=strlen(ngp->prefix)) > i) continue;
                    if (strnicmp(ng, ngp->prefix, j)) continue;
                    cp1=j2strdup(&ng[j]);
                    strcpy(ng, ngp->newname);
                    strcat(ng, cp1);
                    free(cp1);
                    i=strlen(ng);
                    /* we could 'break' here, but might be useful to allow further remaps */
                }
            }

            /* make dirs associated with ng */
            sprintf(buf,"%s/",News_spool ? News_spool : Mailspool);
            for(cp1=ng; *cp1; ) {
                if ((p=strchr(cp1,'.')) != NULLCHAR) {
                    *p = '\0';
                    strcat(buf, cp1);
#ifdef __TURBOC__
                    mkdir(buf); /* create a subdirectory, if necessary */
#else
                    mkdir(buf,0755); /* create a subdirectory, if necessary */
#endif
                    *p = '/';
                    cp1=p;
                }
                else {
                    prefix=cp1;
                    if (*prefix == '/') prefix++;   /* first char after dot->slash */
                    break;
                }
            }

            if ( (i=strlen(buf)) > 1 && buf[--i] == '/') buf[i] = '\0';
            tempdir=j2strdup(buf);
            strcat(buf,"/");
            strcat(buf,prefix);
            if (mlock(tempdir, prefix)) {
                if (nntptrace >= 2)
                    tprintf("NNTP group '%s' is locked\n", ng);
                free(froml);
                free(newgl);
                free(tempdir);
                fclose(tmpf);
                return -1;
            }
            if (! News_spool) SyncIndex(ng); /* ensure index file is current */

            strcat(buf,".txt");
            /* open the mail file */
            if (nntptrace >= 3)
                tprintf("Writing article to '%s'\n", buf);
            if((fp = fopen(buf,APPEND_TEXT)) != NULLFILE) {
                default_index(ng,&ind);
                fseek(fp,0,SEEK_END);
                start = ftell(fp);
                time(&t);
                fprintf(fp, "From %s %ld\n", froml, t);
                ind.mydate=t;
#ifdef USERLOG
                /* If the userlog code is enabled, we need a
                 * "Received: " line to get the message id
                 * that is used in it - WG7J
                 */
                ind.msgid = get_msgid();
                fprintf(fp,Hdrs[RECEIVED]);
                fprintf(fp,"by %s with NNTP\n\tid AA%ld ; %s",
                    Hostname, ind.msgid, ptime(&t));
#endif
                rewind(tmpf);
                {
                 int prevtype = NOHEADER, x;
                 char c;

                 continued = 0;
                 while(fgets(buf,NNTPMAXLEN,tmpf) != NULLCHAR) {
                     continuation = continued;
                     continued = (strchr(buf,'\n') ==  NULLCHAR);
                     if(!continuation && buf[0] == '\n') {    /* End of headers */
                        if(!ind.date) {  /* What! No date: header line? */
                            ind.date = t;
                            fprintf(fp, "%s%s", Hdrs[DATE],ptime(&t)); /* ptime() adds NL */
                        }
                        putc('\n',fp);
                        break;
                    }
                    /* omit headers that could cause later trouble */
                    if (continuation) x=prevtype;  /* very long header? */
                    else x=htype(buf,&prevtype);
                    if (x==TO || x==CC || x==APPARTO || x==RRECEIPT) continue;
                    /* Long headers (> LINELEN) cause problems in expire, closenotes, etc. */
                    for(cp=buf; strlen(cp) > 250; cp+=250) {  /* arbitrary split point */
                        c = cp[250]; cp[250]='\0';  /* we should split at whitespace...*/
                        fputs(cp,fp); fputs("\n ",fp);
                        cp[250] = c;
                    }
                    fputs(cp,fp);

                    set_index(buf,&ind,x);
                 }
                }
                /* Now the remaining data */
                continuation=0;
                while(fgets(buf,NNTPMAXLEN,tmpf) != NULLCHAR) {
                    /* for UNIX mail compatiblity */
                    if(!continuation && strncmp(buf,"From ",5) == 0)
                        putc('>',fp);
                    fputs(buf,fp);
                    continuation = (strchr(buf,'\n') == NULLCHAR);
                }
                putc('\n',fp);
                ind.size = ftell(fp) - start;
                fclose(fp);
                if(! News_spool) {   /* working under Mailspool? */
#ifdef USERLOG
                    /* Now touch the timestamp file if it's an area */
                    if(isarea(ng)) {     /* ve3lum 051494, n5knx */
                        sprintf(buf,"%s/%s.inf", tempdir, prefix);
                        fclose(fopen(buf,"w"));
                    }
#endif
                    /* Update the index file */
                    if (write_index(ng,&ind) == -1)
                        log(s,"NNTP can't update index for %s", ng);
                }
            }
            rmlock(tempdir, prefix);
            free(tempdir);

            if (*cp == '\n')
                break;
            i=0;
            continue;
        }
        ng[i++] = strchr(validchars, tolower(*cp)) ? *cp : '_';
    }

    default_index("",&ind);    /* Free remaining data in index structure */
    fclose(tmpf);
    rip(newgl);         /* remove trailing new-line */
    if(!nntpquiet) {
/* If we use tprintf here, instead of printf, flowcontrol
 * in the command screen is used; if the system is unattended for
 * more than 24 articles coming in, it will lock up NNTP.
 * Make sure this only goes to the command screen - WG7J/N5KNX
 */
#ifdef UNIX
/* true, but we defeat that when using the trace interface anyway.  KF8NH */
                    tcmdprintf("New news arrived: %s, article %s%c\n",newgl,msgid,Smtpquiet?' ':'\007');
#else
                    if(Current->output == Command->output)
                        printf("New news arrived: %s, article %s%c\n",newgl,msgid,Smtpquiet?' ':'\007');
#endif
    }
    free(froml);
    free(newgl);
    return 0;
}

#ifdef NN_USESTAT
/* returncode: -1 if error (=> nothing written to artidf), else 0 */
static int
statnew(artidf, s, nglist, np, buf)
FILE *artidf;
int s;
char *nglist;
struct nntpservers *np;
char *buf;              /* NNTPMAXLEN chars long */
{
    char *p, *pp, *cp, *cp1, *xp;
    long t;
    unsigned long hinumber, lonum, hinum;
    struct ngmap *ngp;
    FILE *f;

    if (strcmp(nglist,"*")==0) return -1;
    p=cp=j2strdup(nglist);
    for (;cp;cp=cp1) {  /* for each cp->group_name */
        if ((cp1 = strchr(cp,',')) != NULLCHAR ) {
            *cp1++ = '\0';
        }

        /* map name of newsgroup as required, to an area name */
        strcpy(buf,cp);  /* current newsgroup name */
        for (ngp=ngmaphead; ngp!=NULL; ngp=ngp->next) {
            int j;
            if (ngp->prefix) {
                if ((j=strlen(ngp->prefix)) > strlen(buf)) continue;
                if (strnicmp(buf, ngp->prefix, j)) continue;
                pp=j2strdup(buf+j);
                strcpy(buf, ngp->newname);
                strcat(buf, pp);
                free(pp);
                /* we could 'break' here, but might be useful to allow further remaps */
            }
        }
        dirformat(buf);
        pp=j2strdup(buf);
        sprintf(buf,"%s/%s.rc",News_spool ? News_spool : Mailspool, pp);
        free(pp);

        for (pp=strchr(buf,'/'); pp; pp=strchr(pp+1,'/')) {  /* create subdirs */
            *pp='\0';
#ifdef __TURBOC__
            mkdir(buf);
#else
            mkdir(buf,0755);
#endif
            *pp='/';
        }

        hinumber = 0UL;
        if((f = fopen(buf,"r+")) != NULLFILE) {
            for(t=0L; fgets(buf,NNTPMAXLEN,f) != NULLCHAR; t=ftell(f)) {
                if ((pp = strchr(buf, ' ')) == NULLCHAR) continue;  /* bad fmt */
                *pp++ = '\0';
                if(stricmp(buf,np->name) == 0) {  /* hosts match */
                    if ((xp = strchr(pp, ' ')) == NULLCHAR) continue;  /* bad fmt */
                    *xp++ = '\0';
                    if(stricmp(pp,cp) == 0) {  /* newgroup name matches */
                        hinumber = (unsigned long)atol(xp);
                        /* Prepare to write back the current host, newsgroup and highest article# */
                        fseek(f,t,0);
                        break;
                    }
                }
            }
        }
        else if((f = fopen(buf,WRITE_TEXT)) == NULLFILE) {  /* create? */
            logerr(s,1,NoOpenMsg, "creation failed", buf);
            continue;
        }

        usprintf(s, "GROUP %s\n", cp);
        if(getreply(s,buf) == 211) {
            sscanf(buf, "%*d %*lu %lu %lu", &lonum, &hinum);
            if (hinumber != hinum) {  /* work to do... */
                if ((hinumber > hinum)  /* ng was reset? */
                    || (lonum > hinumber)) /* articles lost/expired? */
                    hinumber = lonum;
                for(;hinumber <= hinum;hinumber++) {  /* for each new article */
                    usprintf(s, "STAT %lu\n", hinumber);
                    if(getreply(s,buf) == 223) {
                        if ((pp = strchr(buf,'>')) == NULLCHAR) continue;
                        *(++pp) = '\0';
                        if ((pp = strchr(buf,'<')) == NULLCHAR) continue;
                        fprintf(artidf,"%s\n", pp);  /* save article-id in file */
                    }
                }
                hinumber--;
            }
        }

        fprintf(f,"%s %s %010lu\n", np->name, cp, hinumber);
        fclose(f);
    }  /* for next ng name */

    free (p);
    return 0;
}
#endif /* NN_USESTAT */

/* Save code by using a common error notifier routine */
static void
logerr(int s,int severity,char *ErrMsg, char *Arg1, char *Arg2)
{
    log(s,ErrMsg, Arg1, Arg2);
    if (nntptrace >= severity)
        tprintf(ErrMsg,Arg1,Arg2), tputc('\n');
}
#endif  /* NNTP */
