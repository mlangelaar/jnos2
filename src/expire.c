/* Expire old messages.
 * Inspired by 'expire.c' by Bernie Roehl.
 * Substantially rewritten for integration into KA9Q NOS,
 * WG7J v1.01 and later
 * by Johan. K. Reinalda, WG7J/PA3DIS, March/April 92
 *
 * Old bid expiry by WG7J, March 92
 * Index file support for jnos 1.10x3 and later added. WG7J Summer 1993
 * Error checking/logging added by N5KNX 14Jun94
 * NNTPS expiry written by VK5XXX, added 28Jun94 and extended for NNTP
 *  for jnos 1.10f -- n5knx
 */

/* 'expire #' sets the expire interval for n hours.
 * Each time the timer goes off, a new process is started,
 * that expires the old messages...
 *
 * The control file '~/spool/expire.dat' contains lists of
 * filename age
 *     -or-
 * !newsgroup age
 *
 * where 'filename' is the name of the .txt file under '~/spool/mail'
 * containing the messages (but specified WITHOUT the ending '.txt').
 * filename can be extended into subdirectories, and can have either
 * '/', '\' or '.' to indicate subdirectories.
 *
 * and where 'newsgroup' is the NNTP newsgroup name whose articles are
 * to be expired.  The active and history files are also updated when
 * necessary.
 *
 * 'age' is an integer giving the maximum age of a message or newsgroup
 * article, in days, after which expiry is performed.
 * If no age is given, the default age is 21 days.
 */
#ifdef MSDOS
#include <dir.h>
#include <dos.h>
#include <alloc.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <ctype.h>
#include <time.h>
#include "global.h"
#include "timer.h"
#include "proc.h"
#include "bm.h"
#include "files.h"
#include "smtp.h"
#include "socket.h"
#include "index.h"
#include "commands.h"
#ifdef NNTPS
#include "dirutil.h"
#include <limits.h>
#endif

#if defined(NNTPS) || defined(NNTP)
#define NN_EITHER
#endif

#ifdef UNIX
#include "unix.h"
#endif

#define DEFAULT_EXPIRY	/* 26Oct2014, Maiko (VE4KLM), For ALL areas */

#ifdef	DEFAULT_EXPIRY
#include "dirutil.h"
#endif
  
#ifndef __BORLANDC__
#ifndef UNIX
time_t mktime(struct tm *);
extern long UtcOffset;
  
/* If you're using BC++ 2.0 or higher, you don't need this,
 * but TCC 2.0 doesn't have it... (But, TC++ v1.01 does)
 */
/* Simple emulation of the mktime() call (sort-of works :-) )
 * doesn't do any error checking,  or value adjustments.
 * DOES do timezone adjustments, since input is local time but result is in GMT.
 * Simply 'sort-a' calculates the number of seconds since 1970 - WG7J
 */
time_t
mktime(t)
struct tm *t;
{
    static int total[12]={0,31,59,90,120,151,181,212,243,273,304,334};
    int years;
    int leapyears;
    int days;
  
    /* time count start at jan 1, 1970, 00:00 hrs */
    years = t->tm_year - 70;
    /* adjust for leap-years */
    leapyears = (years + 2) / 4;
    if (!((years+70) & 3) && (t->tm_mon < 2))
        --leapyears;    /* no extra day until 3/1 or after */
  
    days = years*365L + leapyears + total[t->tm_mon] + (t->tm_mday-1);
  
    return( days*86400L + t->tm_hour*3600L + t->tm_min*60L + t->tm_sec + UtcOffset);
}
#endif  /* UNIX */
#endif  /* __BORLANDC__ */
  
/* Include the rest in #ifdef's, so we don't pull in the whole module
 * when we only have AT defined and NOT EXPIRE !
 * (since these are the two modules that needs the surrogate mktime()
 *  if compiling with Turbo C 2.0 )
 */
#ifdef EXPIRY
  
/* Default expiry values: */
#define DEFAULT_AGE 21       /* 21 days from origination date */

/* 11Oct2009, Maiko, these are int32 now, not LONG constants */
#define MSPHOUR (1000*60*60)
#define DAYStoSECS (24*60*60)

static struct timer Expiretimer;
int Eproc=0;
  
static char OpenErr[] = "expire: err %d opening %s";
static char RenErr[] = "expire: err %d renaming %s to %s";
static char WriteErr[] = "expire: err %d writing %s";
static char IRMsg[] = "expire: err %d reading index for msg %d of %s";
static char IWMsg[] = "expire: err %d writing index for msg %d of %s";
static char TRMsg[] = "expire: err %d reading msg %d of %s";
static char TWMsg[] = "expire: err %d writing msg %d to %s";

static void Expireprocess __ARGS((int a,void *v1,void *v2));
static void Expiretick __ARGS((void *));
static void expire __ARGS((char *,int));
static void Oldbidtick __ARGS((void *p));
/* NNTP Expire stuff */
#ifdef NN_EITHER
static void update_nntp_history __ARGS((int));
static int convert_num __ARGS((char **));
void RemoveNntpLocks __ARGS((void)); /* called by main() during startup phase */
#endif
#ifdef NNTPS
static void expire_nntpsrv __ARGS((char *, int));
static void update_nntp_active __ARGS((void));
static void update_newsrc __ARGS((int, int, char *));
static char *newsgroup_to_path __ARGS((char *));
#endif

/* 04Apr2016, VE4KLM (Maiko), make sure future expiry works */
#ifdef	DEBUG_FUTURE
extern long UtcOffset;
#endif

/* 11May2013, Maiko, Add ability to expire messages dated into the future */
static int expfuturemsgs = 0;

int doexpire (int argc, char **argv, void *p)
{
    if (argc < 2)
	{
		tprintf ("usage: expire [ <interval in hours> | now | future [ yes | no ] ]\n");
		tprintf ("current timer: %d/%d hrs, expire future: %s\n",
			read_timer(&Expiretimer)/MSPHOUR,
				dur_timer(&Expiretimer)/MSPHOUR,
					expfuturemsgs ? "yes":"no");

        return 0;
    }

	/* 11May2013, Maiko, Let sysop toggle desire to expire future messages */
	if (!stricmp (argv[1], "future"))
	{
		if (argc == 2)
			tprintf ("%s\n", expfuturemsgs ? "yes":"no");

		else
		{
			if (!stricmp (argv[2], "yes"))
				expfuturemsgs = 1;
			else
				expfuturemsgs = 0;
		}

		return 0;
	}

    if (*argv[1] == 'n')
	{
        Expiretick(NULL);
        return 0;
    }

    /* set the timer */
    stop_timer(&Expiretimer); /* Just in case */
    Expiretimer.func = (void (*)__FARGS((void *)))Expiretick;/* what to call on timeout */
    Expiretimer.arg = NULL;        /* dummy value */
    set_timer(&Expiretimer,(uint32)atoi(argv[1])*MSPHOUR); /* set timer duration */
    start_timer(&Expiretimer);
    return 0;
}
  
void
Expiretick(p)
void *p;
{
    start_timer(&Expiretimer);
    /* Spawn off the process */
    if(!Eproc)
        newproc("Expiry", 1536, Expireprocess, 0, NULL, NULL, 0);
}

#ifdef	DEFAULT_EXPIRY

/*
 * 26Oct2014, Maiko (VE4KLM), New functions to do default expiry on ALL areas
 * if a '*' entry is added to the expiry.dat configuration file, requested by
 * N6MEF (Fox), pretty easy to implement, just requires a few extra functions,
 * decided to use the 'struct list' used in SMTP, it's convenient to use.
 */

static struct list *already_expired = NULLLIST; /* list will work for this */

/* Add to the list of processed filenames, needed later for default expiry */
static void add_processed_expiry (char *fname)
{
	addlist (&already_expired, fname, 0, NULLCHAR);
}

/* Check list of processed filenames, indicate if in the list or not */
static int processed_expiry (char *fname)
{
	struct list *ap;

	for (ap = already_expired; ap != NULLLIST; ap = ap->next)
		if (!strcmp (ap->val, fname))
			return 1;
	return 0;
}

/* Delete the list when we're done */
static void delete_processed_list ()
{
	register struct list *tp, *tp1;

	for (tp = already_expired; tp != NULLLIST; tp = tp1)
	{
		tp1 = tp->next;
		free (tp->val);
	/* I don't use 'tp->aux' here, it's just set to NULLCHAR, so don't free */
		free ((char*)tp);
	}

	already_expired = NULLLIST;		/* You MUST do this or crash next time */
}

/* Go through all mailbox files using dirutils */
static void process_default_expiries (int default_expiry_age)
{
	struct ffblk ff;

	char buf[20], fname[32], *extptr;

	// log (-1, "processing default expiry on ALL areas");

    strcpy (buf, "spool/mail/*.txt");

	if (findfirst (buf, &ff, 0) == 0)
	{
        do  {

			strcpy (fname, ff.ff_name);

		/* The expire function expects NO extension, so strip it off */
			extptr = strstr (fname, ".txt");
			if (extptr)
			{
				*extptr = 0;	/* strip off extension */

				// log (-1, "checking [%s]", fname);

				/* make sure this mailbox was not expiry processed already */
				if (!processed_expiry (fname))
				{
					/* log (-1, "expiring [%s] default age %d",
						fname, default_expiry_age);
					*/

					expire (fname, default_expiry_age);
				}
			}

		} while (findnext (&ff) == 0);
	}
}
#endif
  
static void
Expireprocess(a,v1,v2)
int a;
void *v1, *v2;
{
    char line[80];
    int age;
#ifdef NN_EITHER
    int expire_nntp_history = 0;
#endif
    char *cp;
    FILE *ctl;
#if defined(FBBFWD) && defined(MBFWD)
    extern int FBBSendingCnt;
#endif
#ifdef	DEFAULT_EXPIRY
	int default_expiry_age = 0;
#endif
  
    Eproc = 1;
#if defined(FBBFWD) && defined(MBFWD)
/* n5knx: Eproc == 1 will prevent new FBB-style forwarding (just FF's are sent)
 * BUT we must also wait for existing FBB-style forwarding sessions to finish.
 * Why all this bother?  expire() will rebuild area.txt and area.ind, confusing
 * dofbbsend() and sendmsg(), resulting in empty msgs.
 * Fortunately, MBL-style forwarding resyncs and needs no special handling.
 */
    while(FBBSendingCnt) {  /* must we wait? */
        pwait(&FBBSendingCnt);
    }
#endif /* FBBFWD && MBFWD */
    if ((ctl = fopen(Expirefile, "r")) == NULLFILE) {
        Eproc = 0;
        return;
    }
    /* read lines from the control file */
    while(fgets(line, sizeof(line), ctl) != NULLCHAR) {
        pwait(NULL); /* be nice */
        if((*line == '#') || (*line == '\n')
#ifndef NN_EITHER
           || (*line == '!')
#endif
          ) /* comment or blank line */
            continue;
        rip(line);
        /* terminate area name */
        age = DEFAULT_AGE;
        if((cp=strpbrk(line, " \t")) != NULLCHAR)
		{
            /* there is age info */
            *cp++ = '\0';
            age = atoi(cp);
/*
 * 01Nov2014, Maiko (VE4KLM), By introducing a default expiry, we inadvertently
 * force expiry on ALL remaining areas, which might not be desireable. There may
 * be areas we don't want to run expiry processing on. For those areas, you can
 * add an entry with the age set to -1, and the area will be 'bypassed'.
 */
#ifdef	DEFAULT_EXPIRY
			if (age == -1)
			{
				log (-1, "[%s] no expiry processing", line);
				add_processed_expiry (line);
				continue;
			}
			else
#endif
            if (age <= 0) age = DEFAULT_AGE;
        }

/* 26Oct2014, Maiko (VE4KLM), N6MEF (Fox) requested 'default expiry' */
#ifdef	DEFAULT_EXPIRY
	if (*line == '*')
	{
		default_expiry_age = age;
		continue;
	}
#endif

#ifdef NN_EITHER
        if (*line == '!') {  /* Expire NNTP entry if line begins with ! */
#ifdef NNTPS
            expire_nntpsrv(&line[1], age);
#endif
#ifdef NNTP
            expire(&line[1], age);
#endif
            if (expire_nntp_history < age)
                expire_nntp_history = age;
        }
        else
#endif
		{
#ifdef	DEFAULT_EXPIRY
			add_processed_expiry (line);
#endif
        	expire(line,age);
		}
    }
    fclose(ctl);

/* 26Oct2014, Maiko (VE4KLM), N6MEF (Fox) requested 'default expiry' */
#ifdef	DEFAULT_EXPIRY
	if (default_expiry_age)
		process_default_expiries (default_expiry_age);
	delete_processed_list ();
#endif

#ifdef NN_EITHER
    if (expire_nntp_history) {
        for (age=30; age>0; age--) {  /* try to lock news History file */
            if(!mlock(History,NULLCHAR)) break;
            j2pause(2000L);
        }
        if (age) { /* did lock, we'll now cleanup History and other files */
            update_nntp_history(expire_nntp_history);
#ifdef NNTPS
            update_nntp_active();  /* and news.rc */
#endif
            (void)rmlock(History,NULLCHAR);
        }
    }
#endif
    Eproc = 0;
}
  
#ifdef NN_EITHER
/*
        expire NNTP articles
        coded by brett england (future amateur) and rob vk5xxx.
*/
static
int
convert_num( p )
char **p;
{
    int i;
    char *pp = *p;

    i = (*pp - '0') * 10 + *(pp+1) - '0';
    (*p)+=2;
    return (i);
}

static
void
update_nntp_history(age)
int age;
{
  FILE *old, *new;
  char newfile[FILE_PATH_SIZE];
  char line[LINELEN];
  char *p;
  struct tm tm_time;
  time_t expire_time, file_time, now;
  int i,history_records_expired = 0;

#ifdef notdef
  for (i=10; i>0; i--) {
      if(!mlock(History,NULLCHAR)) break;
      j2pause(2000L);
  }
  if (!i)  return;   /* can't lock, we'll have to wait until next time */
#endif

  expire_time = (long)age * DAYStoSECS;  /* days to seconds */
  time(&now);

  sprintf(newfile,"%s.new",History);
  unlink(newfile);
  if((old = fopen(History,READ_TEXT)) == NULLFILE) {
      log(-1,OpenErr, errno, History);
#ifdef notdef
      rmlock(History,NULLCHAR);
#endif
      return;
  }

  if((new = fopen(newfile,WRITE_TEXT)) == NULLFILE) {
      fclose(old);
      log(-1,OpenErr, errno, newfile);
#ifdef notdef
      rmlock(History,NULLCHAR);
#endif
      return;
  }

  /* Scan the history file, deleting old article references */
  /* format: <message-id> yymmdd hhmmss ngname/Art# */
  for(;;) {
    pwait(NULL);
    if(fgets(line, LINELEN, old) == NULL)
        break;

    p = line;
    while (*p != '\0' && *p != '>')
       p++;
    if (*p != '>')  /* Some sort of corrupt line */
        continue;
    p+=2;
    tm_time.tm_year = convert_num(&p);
    if (tm_time.tm_year < 70) tm_time.tm_year+=100;  /* 21-st century */
    tm_time.tm_mon = convert_num(&p)-1;
    tm_time.tm_mday = convert_num(&p);
    p++;   /* Point to time component */
    tm_time.tm_hour = convert_num(&p);
    tm_time.tm_min = convert_num(&p);
    tm_time.tm_sec = 0;
    tm_time.tm_isdst = -1;   /* we don't know */

    file_time = mktime(&tm_time);
#ifdef UNIX
    /* Silly problem arises when we convert a 02:nn value on the day DST changes! */
    if (file_time == -1L) {
        tm_time.tm_isdst = 0;  /* fudge it: claim not in effect */
        file_time = mktime(&tm_time);
    }
#endif

    if (file_time > ( now - expire_time ))
      fputs(line, new);
    else
      history_records_expired++;
  }
  i=ferror(new);
  fclose(new);
  fclose(old);

  if(!i) {
      unlink(History);
      if(rename(newfile, History) == -1)
          log(-1, RenErr, errno, newfile, History);
  }
  else log(-1, WriteErr, errno, newfile);

#ifdef notdef
  rmlock(History,NULLCHAR);
#endif

  if(history_records_expired > 0)
    log(-1,"Expired: %d in %s", history_records_expired, History);
}
#endif

#ifdef NNTPS
/* Check the news.rc file if the number contained is less than min
   replace the news.rc file contents with min.
*/
static
void
update_newsrc( min, max, path )
  int min, max;
  char *path;
{
  FILE *newsrcfd;
  char news_file[FILE_PATH_SIZE], line[LINELEN] = "";
  int no;

  sprintf(news_file,"%s/news.rc",path);
  if((newsrcfd = fopen(news_file,"r+")) != NULLFILE) {
      fgets(line, LINELEN, newsrcfd);
      rewind(newsrcfd);

      no = atoi(line);
      if((no < min) || (no > max))
          fprintf(newsrcfd,"%d\n",min);
      fclose(newsrcfd);
  }

}

/* 28Dec2004, Replaces the GOTO 'xit_actv' label */
static void do_xit_actv ()
{
  rmlock(Active, NULLCHAR);
}

/* Update the active file and each news.rc file
*/
static
void
update_nntp_active() {
  FILE *old, *new;
  char newfile[FILE_PATH_SIZE];
  char line[LINELEN];
  char *p, *path, *rpath;
  int min, max;
  int command=0, file_num;
  struct ffblk *file;


  while(mlock(Active, NULLCHAR)) { /* since Active is updated in nntpserver */
      j2pause(1000L);
      if (++command == 60) {
          log(-1, "expire: can't update %s due to lock", Active);
          return;   /* can't lock, so skip it this time */
      }
  }

  sprintf(newfile,"%s.new",Active);
  unlink(newfile);
  if((old = fopen(Active,READ_TEXT)) == NULLFILE) {
      log(-1,OpenErr,errno,Active);
		return (do_xit_actv ());
  }

  if((new = fopen(newfile,WRITE_TEXT)) == NULLFILE) {
      fclose(old);
      log(-1,OpenErr,newfile);
		return (do_xit_actv ());
  }

  file = (struct ffblk *)mallocw(sizeof(struct ffblk));
  /* rewrite the active file, with current max and min article numbers */
  /* format: ngname MaxArt# MinArt# Postflag */
  for(;;) {
    pwait(NULL);
    if(fgets(line, LINELEN, old) == NULL)
       break;

    p = line;
    while (*p  &&  ! isspace(*p))
      p++;
    if (*p == '\0')
      continue;
    *p = '\0';

    if ((rpath = newsgroup_to_path( line )) == NULLCHAR) {
        *p = ' ';   /* weird error ... copy line unchanged to be safe */
        fprintf(new,"%s", line);
        continue;
    }

    p++;
    while(*p && (isspace(*p) || isdigit(*p)))
        p++;

    path = wildcardize(rpath);

    command = 0;
    min = INT_MAX;
    max = INT_MIN;
    for(;;) {
        pwait(NULL);
        if (!nextname(command, path, file))
            break;
        command = 1;   /* Find next */

        if (file->ff_name[0] == '.')
            continue;  /* ignore . and .. */

        file_num = atoi(file->ff_name);
        if (file_num > 0) {     /* should always be greater than 0 */
            if (file_num < min)
                min = file_num;
            if (file_num > max)
                max = file_num;
        } 
    }
    if (min == INT_MAX && max == INT_MIN) { /* No files */
        min = 1;
        max = 0;
    }
    update_newsrc(min, max, rpath);

    fprintf(new,"%s %05d %05d %s", line, max, min, p);
  }

  command=ferror(new);
  fclose(new);
  fclose(old);
  free(file);

  if(!command)
  {
      unlink(Active);
      if(rename(newfile,Active) == -1)
          log(-1,RenErr,newfile,Active);
  }
  else log(-1, WriteErr, errno, newfile);

  return (do_xit_actv ());
}

static
char
*newsgroup_to_path( group )
char *group;
{
    FILE *f;
    static char line[LINELEN];
    char *cp,*cp1;

    if((f = fopen(Pointer,"r")) == NULLFILE)
        return(NULLCHAR);

    for (;;) {
        pwait(NULL);
        if (fgets(line,LINELEN,f) == NULLCHAR)
            break;

        if (strcspn(line," ") != strlen(group))
            continue;

        if (strnicmp(group,line,strlen(group)) == 0) {
            cp = (strchr(line,' ')) + 1;
            if ((cp1 = strchr(cp, ' ')) != NULLCHAR)
                *cp1 = '\0';  /* drop date-created */
            else
                rip(cp);
            fclose(f);
            return (cp);
        }
    }
    fclose(f);
    /* Check if it's the JUNK group */
    if (!stricmp(group,"junk"))
        return (Forward);
    return (NULLCHAR);
}

void
expire_nntpsrv(nntp_name, age)
char *nntp_name;
int age;
{
    char *path;
    int command = 0;
    struct ffblk file;
#ifndef UNIX
    struct tm tm_time;
#endif
    time_t file_time, expire_time, now;
    char nntp_file[FILE_PATH_SIZE];
    char save_path[FILE_PATH_SIZE];
    int file_num, expired = 0;

    /* Resolve nntp name into directory path */
    if ((path = newsgroup_to_path( nntp_name )) == NULLCHAR)
        return;

    expire_time = (long)age * DAYStoSECS;
    time(&now);
    strcpy(save_path, path);
    path = wildcardize(path);

    for(;;) {
        pwait(NULL);
        if (!nextname(command, path, &file))
            break;
        command = 1;   /* Find next */

        if (file.ff_name[0] == '.')
            continue;  /* ignore . and .. */

        file_num = atoi(file.ff_name);
        if (file_num > 0) {
            /* only expire a file that is numeric :-) */
#ifdef UNIX
            file_time = mktime(&file.ff_ftime);
#else
            tm_time.tm_sec = 0;  /* DOS doesn't store this */
            tm_time.tm_min = (file.ff_ftime >> 5) & 0x3f;
            tm_time.tm_hour = (file.ff_ftime >> 11) & 0x1f;
            tm_time.tm_mday = file.ff_fdate & 0x1f;
            tm_time.tm_mon = ((file.ff_fdate >> 5) & 0xf)-1;
            tm_time.tm_year = (file.ff_fdate >> 9) + 80;
            tm_time.tm_isdst = -1;

            file_time = mktime(&tm_time);
#endif
            if (( now - expire_time ) > file_time) {
                 sprintf(nntp_file, "%s/%s", save_path, file.ff_name);
                 unlink(nntp_file);
                 expired++;
            }
        }
    }
    if(expired)
        log(-1,"Expired: %d in %s",expired, nntp_name);
}
#endif /* end of NNTPS */

void expire (char *filename, int age)
{
    int idx,idxnew,expired,i,err;
    FILE *txt,*new;
    long start,pos,msgsize;
    time_t now;
    struct indexhdr hdr;
    struct mailindex index;
    char file[FILE_PATH_SIZE];
    char newfile[FILE_PATH_SIZE];
    char buf[LINELEN];
	int futured;

#define IRERR	-1	/* Index read error */
#define IWERR	-2	/* Index write error */
#define TRERR	-3	/* area text file read error */
#define TWERR	-4	/* area text file write error */
#define TPERR	-5	/* area text file position error */

    dirformat(filename);
  
    if(mlock(Mailspool,filename))
        /* can't get a lock */
        return;
  
    sprintf(file,"%s/%s.txt",Mailspool,filename);
    if((txt=fopen(file,READ_BINARY)) == NULLFILE) {
        rmlock(Mailspool, filename);
        return;
    }

    sprintf(file,"%s/%s.ind",Mailspool,filename);
    if((idx=open(file,READBINARY)) == -1) {    /* Open the index file */
        fclose(txt);
        log(-1, OpenErr, errno, file);
        rmlock(Mailspool,filename);
        return;
    }

    /* Create new text file */
    sprintf(file,"%s/%s.new",Mailspool,filename);
    if((new=fopen(file,WRITE_TEXT)) == NULLFILE) {
        fclose(txt);
        close(idx);
        log(-1, OpenErr, errno, file);
        rmlock(Mailspool, filename);
        return;
    }
  
    /* Create the new index file */
    sprintf(file,"%s/%s.idn",Mailspool,filename);
    if((idxnew=open(file,CREATETRUNCATEBINARY,CREATEMODE)) == -1) {
        fclose(txt);
        close(idx);
        fclose(new);
        rmlock(Mailspool,filename);
        log(-1, OpenErr, errno, file);
        return;
    }
  
    memset(&index,0,sizeof(index));
    time(&now);
    start = pos = 0L;
    expired = err = 0;
	futured = 0;
  
    /* Write a default header to the new index file */
    default_header(&hdr);
    if (write_header(idxnew,&hdr) == -1) {
        log(-1, IWMsg, errno, -1, filename);
        err=IWERR;
    }
  
    /* Read the header from the index file */
    if (read_header(idx,&hdr) == -1) {
        log(-1, IRMsg, errno, -1, filename);
        err=IRERR;
    }
  
    /* Now read all messages, and expire old ones */
    for(i = 1; i <= hdr.msgs && !err; i++) {
        default_index(filename,&index);
        if (read_index(idx,&index) == -1) {
            log(-1,IRMsg,errno,i,filename);
            err=IRERR;
            break;
        }
        msgsize = index.size;

/* 04Apr2016, VE4KLM (Maiko), make sure future expiry works */
#ifdef	DEBUG_FUTURE
		log (-1, "index date %ld gmt %ld now %ld",
			index.date, index.date + UtcOffset, now);
		if ((index.date + UtcOffset) > now)
			log (-1, "would have expired future");
#endif
		/* 11May2013, Option to expire messages that are in the future */
		if (expfuturemsgs && (index.date > now))
			futured++;

		else if (now - index.date < ((long)age*DAYStoSECS))
		{
            /* This one we should keep ! Copy it from txt to new */
            fseek(txt,start,SEEK_SET);
            pos = ftell(new);
            /* Read the first line, should be 'From ' line */
            if (bgets(buf,sizeof(buf),txt) == NULL) {
                log(-1, TRMsg, errno,i,filename);
                err=TRERR;
                break;
            }
	    if (strncmp(buf,"From ",5)) {
                log(-1, "expire: format error in msg %d of %s.txt",i,filename);
                err=TPERR;
                break;
            }

            /* Now copy to output until we find another 'From ' line or
             * reach end of file.
             */
            do {
                if (fprintf(new,"%s\n",buf) == -1) {
                    log(-1, TWMsg,errno,i,filename);
                    err=TWERR;
                    break;  /* copy of msg will have shrunk ...  */
                }
                if (bgets(buf,sizeof(buf),txt) == NULL) break;
                pwait(NULL);
            } while (strncmp(buf,"From ",5));
            /* write the index for the new copy of the message */
            index.size = ftell(new) - pos;
            if (index.size < 0 || pos < 0) {
                log(-1, "expire: ftell err %d for %s", errno, filename);
                err=TPERR;
                break;
            }
            if (WriteIndex(idxnew,&index) == -1) {
                log(-1,IWMsg,errno,i,filename);
                err=IWERR;
                break;
            }
        } else
            expired++;
        start += msgsize;    /* starting offset of the next message */
    }
    default_index("",&index);
    close(idx);
    close(idxnew);
    pos=ftell(new);        /* how long is new area */
    fclose(new);
    fclose(txt);

    if (!err) {

        sprintf(file,"%s/%s.txt",Mailspool,filename);
        sprintf(newfile,"%s/%s.new",Mailspool,filename);
        unlink(file);
        if(pos)
            rename(newfile,file);
        else
            /* remove a zero length file */
            unlink(newfile);
        sprintf(file,"%s/%s.ind",Mailspool,filename);
        sprintf(newfile,"%s/%s.idn",Mailspool,filename);
        unlink(file);
        if(pos)
            rename(newfile,file);
        else {
            /* remove a zero length file */
            unlink(newfile);
#ifdef USERLOG
            sprintf(file,"%s/%s.inf", Mailspool, filename);
            unlink(file);
#endif
        }
    }
    rmlock(Mailspool,filename);
    if(expired)
        log(-1,"Expired: %d in %s",expired,filename);
	if(futured)
		log (-1, "Futured: %d in %s", futured, filename);
}
  
  
/******************************************************************/
/* This program will deleted old BID's from the history file,
 * after making a backup copy.
 *
 *  Eg. 'oldbids 24 30' will try to delete all bids older then 30 days
 *      every 24 hours.
 *      'oldbids now' will do it now, with set value of age.
 *
 * Copyright 1992, Johan. K. Reinalda, WG7J/PA3DIS
 *      email : johan@ece.orst.edu
 *      packet: wg7j@wg7j.or.usa.na
 *
 * Any part of this source may be freely distributed for none-commercial,
 * amateur radio use only, as long as credit is given to the author.
 *
 * v1.0 920325
 */
static struct timer Oldbidtimer;
static int Oldbid_age = 30;
  
static
void
Oldbidtick(p)
void *p;
{
    int i,locked,expired = 0;
    char *cp;
    FILE *old, *new;
    time_t now;
    time_t age;
    time_t bidtime;
    #define LEN 80
    char newfile[LEN];
    char buf[LEN];
  
    stop_timer(&Oldbidtimer);
  
    for (locked=30; locked>0; locked--) {  /* n5knx: try to lock bid History file */
        if(!mlock(Historyfile,NULLCHAR)) break;
        j2pause(2000L);
    }
    while (locked) { /* did lock, we'll now cleanup bid history file */
        sprintf(newfile,"%s.new",Historyfile);
        unlink(newfile);
        if((old = fopen(Historyfile,READ_TEXT)) == NULLFILE) {
            log(-1, OpenErr, errno, Historyfile);
            break;
        }
        if((new = fopen(newfile,WRITE_TEXT)) == NULLFILE) {
            fclose(old);
            log(-1, OpenErr, errno, newfile);
            break;
        }

        now = time(&now);
        age = (time_t)(Oldbid_age*DAYStoSECS);
  
        while(fgets(buf,LEN,old) != NULL) {
            rip(buf);
            if((cp=strchr(buf,' ')) != NULLCHAR) {
                /*found one with timestamp*/
                *cp = '\0';
                cp++;   /* now points to timestamp */
                if((bidtime = atol(cp)) == 0L)
                    /*something wrong, re-stamp */
                    fprintf(new,"%s %ld\n",buf, now);
                else {
                    /* Has this one expired yet ? */
                    if(now - bidtime < age)
                        fprintf(new,"%s %ld\n",buf, bidtime);
                    else
                        expired++;
                }
            } else {
                /* This is an old one without time stamp,
                 * add to the new file with current time as timestamp
                 */
                fprintf(new,"%s %ld\n",buf,now);
            }
        }

        i=ferror(new);
        fclose(old);
        fclose(new);
  
        if(!i) {
            unlink(Historyfile);
            if(rename(newfile,Historyfile) == -1)
                log(-1, RenErr, errno, newfile, Historyfile);
        }
        else log(-1, WriteErr, errno, newfile);
        break;  /* only do this once (saves duplicated call-rmlock() code) */
    }
    if (locked)
        (void)rmlock(Historyfile,NULLCHAR);

    if(expired)
        log(-1,"Oldbids: %d expired",expired);

    start_timer(&Oldbidtimer);
    return;
}
  
int
dooldbids(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2){
        tprintf("timer: %d/%d hrs; age: %d days\n",
        read_timer(&Oldbidtimer)/MSPHOUR,
        dur_timer(&Oldbidtimer)/MSPHOUR,
        Oldbid_age);
        return 0;
    }
    if(*argv[1] == 'n') {
        Oldbidtick(NULL);
        return 0;
    }
    /* set the timer */
    stop_timer(&Oldbidtimer); /* Just in case */
    Oldbidtimer.func = (void (*)(void*))Oldbidtick;/* what to call on timeout */
    Oldbidtimer.arg = NULL;        /* dummy value */
    set_timer(&Oldbidtimer,(uint32)atoi(argv[1])*MSPHOUR); /* set timer duration */
    if(argc > 2)
        Oldbid_age = atoi(argv[2]);
    Oldbidtick(NULL); /* Do one now and start it all!*/
    return 0;
}
  
#endif /* EXPIRE */


#ifdef NN_EITHER
void
RemoveNntpLocks(void)          /* called by main() during startup phase */
{
    (void)rmlock(History, NULLCHAR);
#ifdef NNTP
    (void)rmlock(Newsdir, "nntp");
    /* any newsgroups are handled by RemoveMailLocks() */
#endif /* NNTP */
#ifdef NNTPS
    /* remove locks for any newsgroup named in the active file */
    {
        FILE *actf;
        char line[LINELEN];
        char *p, *rpath;

        if((actf = fopen(Active,READ_TEXT)) == NULLFILE)
            return;

        for(;;) {
            if(fgets(line, LINELEN, actf) == NULLCHAR)
                break;

            p = line;
            while (*p && ! isspace(*p))  p++;
            if (*p == '\0')  continue;  /* malformed */
            *p = '\0';

#ifdef EXPIRY	/* newsgroup_to_path() exists? */
            if ((rpath = newsgroup_to_path( line )) != NULLCHAR)
                (void)rmlock(rpath,NULLCHAR);
#else
            (void)rmlock(Mailspool,line);   /* we think we know where to look */
#endif /* EXPIRY */
        }
        fclose(actf);
    }
#endif /* NNTPS */
}
#endif /* NN_EITHER */
