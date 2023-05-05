/* The following code will broadcast 'Mail for:' beacons
 * for private users with unread mail on a regular interval
 *
 * Also contains the commands to set the R: header read options
 * with the 'bulletin' command.
 *
 * original: 920306
 * rewritten: 920326
 * Copyright 1992, Johan. K. Reinalda, WG7J/PA3DIS
 * Permission granted for non-commercial use only!
 *
 * Modified to use the new mailbox index file - 930624 WG7J
 * mailfor watch added by PE1DGZ/N5KNX
 */
#ifdef MSDOS
#include <dir.h>
#include <dos.h>
#endif
#ifdef UNIX
#include "unix.h"
#endif
#include "global.h"
#include "files.h"
#include "dirutil.h"
#include "bm.h"
#include "cmdparse.h"
#include "timer.h"
#include "pktdrvr.h"
#include "ax25.h"
#include "mailfor.h"
#include "socket.h"
#include "commands.h"
#include "index.h"
  
#ifdef __GNUC__
struct no_mf;  /* forward ref to keep GCC happy */
#endif

#ifdef MAILFOR
#ifdef AX25
  
static int mf_member __ARGS((char *name, struct no_mf *head));
static int checknewmail __ARGS((char *area));
static int setmailfor __ARGS((void));
static void ax_mf __ARGS((struct iface *ifp));
static void Mftick __ARGS((void *v));
  
#define MAXMFLEN 256
static struct timer Mftimer;
static char ax_mftext[MAXMFLEN+1] = "Mail for:";
#define DEFMFLEN 9
int mflen = DEFMFLEN; /*Initial lenght of mail-for string*/
  
/* List of area names to exclude from mail-for beacon */
struct no_mf {
    struct no_mf *next;
    char area[9];
};
#define NULLMF (struct no_mf *)0
struct no_mf *No_mf = NULLMF;

#ifdef STATUSWIN
int Mail_Received = 0;	/* accessed by StatusLine1() */
struct no_mf *watch_list = NULLMF;
#endif
  
/* Read a private message area, searching for unread mail
 * this is indicated by the status character in the index file.
 */
static int
checknewmail(area)
char *area;
{
    int idx;
    struct indexhdr hdr;
    char buf[128];
  
    /* Since external mail programs could modify the mailbox, we should first
       try to sync the index with the mailbox -- n5knx */
    if (!mlock(Mailspool, area)) {  /* Lock first, then sync */
        SyncIndex(area);       /* ensure index file is current */
        rmlock(Mailspool, area);
    }
    
    /* Open area index file */
    sprintf(buf,"%s/%s.ind",Mailspool,area);
    if((idx = open(buf,READBINARY)) != -1) {
        /* Read header */
        read_header(idx,&hdr);
        close(idx);
        return hdr.unread;
    }
    return 0;
}
  
/* Check name with exclude/watch list;
 * returns 1 if found, 0 if not
 */
static int
mf_member(name, head)
char *name;
struct no_mf *head;
{
    struct no_mf *nm;
  
    /*Now check the 'exclude' list*/
    for(nm=head;nm!=NULLMF;nm=nm->next) {
        if(!stricmp(nm->area,name))
            return 1;
    }
    return 0;
}
  
static int
setmailfor(void)
{
    char buf[80];
    struct ffblk ff;
  
    sprintf(buf,"%s/*.txt",Mailspool);
    if (findfirst(buf, &ff, 0) == 0) {
        do {
            pwait(NULL);    /* Let others run */
            *(strchr(ff.ff_name,'.')) = '\0';
            /*must be private mail area, and not on exclude list !*/
            if(!isarea(ff.ff_name)) {
              if(!mf_member(ff.ff_name, No_mf)) {
                if((strlen(ax_mftext) + strlen(ff.ff_name)) > MAXMFLEN - 1)
#ifdef STATUSWIN
                    ; else  /*checknewmail if room left, then test watch list, below */
#else
                    break; /* That's all folks */
#endif
                if(checknewmail(ff.ff_name)) {
                    strcat(ax_mftext," ");
                    strcat(ax_mftext,ff.ff_name);
                }
              }
#ifdef STATUSWIN
              if(mf_member(ff.ff_name, watch_list)) {  /* on watch list? */
                  if(checknewmail(ff.ff_name)) Mail_Received++;
              }
#endif
            }
        } while (findnext(&ff) == 0);
    }
#ifdef UNIX
    findlast(&ff);   /* free allocs ... needed if af_mftext filled and we quit early */
#endif
    return strlen(ax_mftext);
}
  
/*This is the low-level broadcast function.*/
static void
ax_mf(ifp)
struct iface *ifp;
{
    struct mbuf *bp;
  
    /* prepare the header */
    if((bp = alloc_mbuf(mflen)) == NULLBUF)
        return;
  
    /*copy the data into the packet*/
    bp->cnt = mflen;
    memcpy(bp->data,ax_mftext,(unsigned)mflen);
  
    /* send it */
    (*ifp->output)(ifp, Ax25multi[MAILCALL], ifp->ax25->bbscall,PID_NO_L3, bp);
}
  
static void
Mftick(v)
void *v;
{
    struct iface *ifp = Ifaces;
  
    stop_timer(&Mftimer); /* in case this was 'kicked' with a 'mailfor now'*/
#ifdef STATUSWIN
    Mail_Received = 0;  /* reset flag, tested in StatusLine1() */
#endif

    /*Now find private mail areas with unread mail.
     *add these to the info-line
     */
    ax_mftext[DEFMFLEN] = '\0'; /* Back to only 'Mail for:' again*/
    if((mflen=setmailfor()) < DEFMFLEN+1) {
        start_timer(&Mftimer);
        return; /* No unread mail */
    }
  
    /*broadcast it on all condifured interfaces*/
    for(ifp=Ifaces;ifp != NULL;ifp=ifp->next)
        if(ifp->flags & MAIL_BEACON)
            ax_mf(ifp);
  
    /* Restart timer */
    start_timer(&Mftimer) ;
}
  
int
dombmailfor(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register int i;
    struct no_mf *nm, **hp;
  
    if(argc < 2){
        tprintf("Mail-for timer: %d/%d\n",
        read_timer(&Mftimer)/1000,
        dur_timer(&Mftimer)/1000);
        if(mflen > DEFMFLEN)
            tprintf("%s\n",ax_mftext);
        return 0;
    }
  
    if(*argv[1] == 'n') { /*send mailfor 'now' !!*/
        Mftick(NULL);
        return 0;
    }
  
    if(*argv[1] == 'e'
#ifdef STATUSWIN
       || *argv[1] == 'w'
#endif
      ) { /* exclude or watch list */
        if (*argv[1] == 'e')        /*the exclude subcommand*/
            hp = &No_mf;
#ifdef STATUSWIN
        else hp = &watch_list;    /* the watch subcommand */
#endif
        if(argc == 2) { /*just list them*/
            for(nm=*hp;nm!=NULLMF;nm=nm->next)
                tprintf("%s ",nm->area);
            tputc('\n');
        } else { /*add some call(s)*/
            for(i=0;i<argc-2;i++) {
                if(strlen(argv[i+2]) > 8) {
                    tprintf("Invalid: %s\n",argv[i+2]);
                    continue;
                }
                nm = callocw(1,sizeof(struct no_mf));
                strcpy(nm->area,argv[i+2]);
                /* add to list */
                nm->next = *hp;
                *hp = nm;
            }
        }
        return 0;
    }
  
    /* set the timer */
    Mftimer.func = (void (*) __ARGS((void *)))Mftick;/* what to call on timeout */
    Mftimer.arg = NULL;        /* dummy value */
    set_timer(&Mftimer,(uint32)atoi(argv[1])*1000); /* set timer duration */
    Mftick(NULL); /* Do one now and start it all!*/
    return 0;
}
#endif /* AX25 */
#endif /* MAILFOR */
  
/*************************************************************************/
  
#ifdef RLINE
  
/* Depending on the flag set, the mailbox will
 * read the message's original date,
 * the correct 'from' address (instead of the user%forwardbbs@myhost),
 * and for buls set the X-Forwarded options to prevent
 * unneccesary forward attemps
 * all from the R: lines supplied by the bbs system
 * 920311 - WG7J
 */
static int dorlinedate __ARGS((int argc,char *argv[],void *p));
static int doholdold __ARGS((int argc,char *argv[],void *p));
static int dorreturn __ARGS((int argc,char *argv[],void *p));
static int dofwdcheck __ARGS((int argc,char *argv[],void *p));
static int dombloophold __ARGS((int argc,char *argv[],void *p));
static void ReadFwdBbs(void);
  
char MyFwds[NUMFWDBBS][FWDBBSLEN+1];
int Numfwds = 0;
int Checklock = 0;   /* get increased to lock list of forward bbses */
int Rdate = 0;
int Rreturn = 0;
int Rfwdcheck = 0;
int Rholdold = 0;
  
static struct cmds DFAR Rlinetab[] = {
    { "check",    dofwdcheck,0, 0, NULLCHAR },
    { "date",     dorlinedate,   0, 0, NULLCHAR },
    { "holdold",  doholdold,   0,0, NULLCHAR },
    { "loophold", dombloophold,0,0, NULLCHAR },
    { "return",   dorreturn, 0, 0, NULLCHAR },
    { NULLCHAR, NULL, 0, 0, NULLCHAR }
};
  
static int
dorlinedate(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Rdate,"Use R: for orig. date",argc,argv);
}
  
static int
doholdold(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint(&Rholdold,"Hold when older than (days)",argc,argv);
}
  
int Mbloophold = 2;
  
/* set loop detection threshold - WG7J */
static int
dombloophold(int argc,char *argv[],void *p)
{
    return setint(&Mbloophold,"Loop hold after",argc,argv);
}
  
static int
dorreturn(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Rreturn,"Use R: for ret. addr.",argc,argv);
}
  
  
static int
dofwdcheck(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register int i;
  
    setbool(&Rfwdcheck,"Use R: to check buls",argc,argv);
    if((argc == 1) && Rfwdcheck && Numfwds) { /*list the bbses we check*/
        j2tputs("Checking for:");
        for(i=0;i<Numfwds;i++)
            tprintf(" %s",MyFwds[i]);
        tputc('\n');
    } else {
        if(Rfwdcheck)
            ReadFwdBbs();
    }
    return 0;
}
  
int
dombrline(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Rlinetab,argc,argv,p);
}
  
static void
ReadFwdBbs() {
    FILE *fp;
    int start = 1;
    char *cp;
    char line[80];
  
    if(Checklock) {
        j2tputs("Bbs-list locked, forward.bbs not re-read\n");
        return;
    }
    Numfwds = 0;    /* reset */
    if((fp=fopen(Forwardfile,READ_TEXT)) == NULLFILE) {
        j2tputs("forward.bbs not found\n");
        return;
    }
    /*Now scan the forward.bbs file to find bbs's*/
    while(fgets(line,sizeof(line),fp) != NULLCHAR && (Numfwds < NUMFWDBBS)) {
        if(*line == '\n' || *line == '#')
            continue;
    /* lines starting with '-' separate the forwarding records */
        if(*line == '-') {
            start = 1;
            continue;
        }
        if(start) {
            start = 0;
        /* get the name of this forwarding record */
            if((cp=strpbrk(line,"\n\t ")) != NULLCHAR)
                *cp = '\0';
            if(strlen(line) > FWDBBSLEN)
                continue;   /*What kind of bbs-call is this ?*/
            strcpy(MyFwds[Numfwds++],strupr(line));
        }
    }
    fclose(fp);
    return;
}
  
#endif /* RLINE */
