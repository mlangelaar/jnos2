/*
 * Timers and process delays work differently under POSIX.  The entire system
 * is driven on a single select() call, which uses the timeout to detect alarms
 * and the file descriptors to detect input.  An itimer is used to allow
 * keyboard input to continue during lengthy activities --- which I tried to
 * avoid for portability reasons, but it behaves *real* ugly otherwise.
 * Especially when LakeSW.ampr.org lets 350K of SMTP mail pile up...
 */

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include "unix.h"
#ifdef M_UNIX
#include <time.h>
#include <sys/select.h>
#else
#include <sys/time.h>
#endif
#include "timer.h"
#include "proc.h"
#include "unixtm.h"
#include "socket.h"
#include "files.h"
#include "session.h"         /* to get Current known */

#ifdef NO_GETTOD
#include <sys/timeb.h>
#endif

extern void _exit (int);  /* 12Mar2009, Maiko, _exit() prototype, should use
                             the <unistd.h> header, but conflicts happen */
#ifdef M_UNIX
#ifndef bzero
/* stupid bug in SCO 3.2.2 (is there any other kind for them?) */
#define bzero(s,n) memset((s),0,(n))
#endif
#endif

/* and undo a collision avoidance, since we need the real one */
#undef malloc
#undef free
extern void *malloc __ARGS((size_t));
extern void free __ARGS((void *));

/* Don't use this yet... something's still calling malloc() directly */
#undef LMCHECK			/* TEST memory allocation checker */

struct io
{
    struct io *next;
    int fd;
    void *event;
};

char Hashtab[256];
volatile int Tick;
int Keyboard;

/*
 * 05Aug2010, Maiko (VE4KLM), the JNOS clock is now 64 bits. This fixes the
 * 25 day negative timer problem. I also renamed this from 'Clock' to make it
 * more specific to JNOS, as it could easily be interpreted as an O/S var !
 */
int64 JnosClock;

static int __istate = 1;
static struct timeval Starttime;
static struct io *Events;
#ifdef SHELL
static int Shell;
#endif

/*****************************************************************************\
*		  Miscellanous functions not found in Unix		      *
\*****************************************************************************/

unsigned long
filelength(fd)
    int fd;
{
    static struct stat sb;

    if (fstat(fd, &sb) == -1)
	return 0;
    return sb.st_size;
}

#ifdef NEED_STRCASECMP

int
strcasecmp(s1, s2)
    register char *s1, *s2;
{
    while (*s1 && *s2 && tolower(*s1) == tolower(*s2))
	s1++, s2++;
    return tolower(*s1) - tolower(*s2);
}

#endif

#ifdef NEED_STRNCASECMP

int
strncasecmp(s1, s2, n)
    register char *s1, *s2;
    int n;
{
    while (n && *s1 && *s2 && tolower(*s1) == tolower(*s2))
	s1++, s2++, n--;
    return n? tolower(*s1) - tolower(*s2): 0;
}

#endif

char *
strupr(s)
    char *s;
{
    register char *p = s;

    while (*p)
	*p = toupper(*p), p++;
    return s;
}

char *
strlwr(s)
    char *s;
{
    register char *p = s;

    while (*p)
	*p = tolower(*p), p++;
    return s;
}

char *
stpcpy(d, s)
    register char *d;
    register const char *s;
{
    while (*s)
	*d++ = *s++;
    *d = '\0';
    return d;
}

char *
itoa(n, buf, base)
    int n, base;
    char *buf;
{
    if (base != 10)
	tprintf("WARNING: itoa() passed radix %d\n", base);
    sprintf(buf, "%d", n);
    return buf;
}

/* This was in assembler, I assume for speed. */

int16
hash_ip(ipaddr)
    int32 ipaddr;
{
    int h;

    h = ((ipaddr >> 16) & 0xFFFF) ^ (ipaddr & 0xFFFF);
    return (int16)(Hashtab[((h >> 8) & 0xFF) ^ (h & 0xFF)]);
}

/*****************************************************************************\
*				Memory interface			      *
\*****************************************************************************/

#ifdef LMCHECK

/*
 * We track memory allocation via hash buckets.  This is a fixed-size hash
 * table with variable-sized buckets:  we hash an address by taking some bits
 * from it, and store the result in a slot in the bucket.
 */

#define NMWHASH		4096	/* number of hash buckets - s/b power of 2 */
#define LOGNMWH		12	/* log2(NMWHASH) */
#define NMWHSHIFT	8	/* ptr shift for hashing */
#define NMWHSLOT	32	/* initial slots in bucket */

struct hbucket
{
    void **ptr;
    int nptr;
};

static struct hbucket MWhashtab[NMWHASH];

static void
mwhash(void *p)
{
    register struct hbucket *h;
    int i;

    h = MWhashtab + (((unsigned long) p >> NMWHSHIFT) & (NMWHASH - 1));
    if (!h->nptr)
    {
	h->ptr = malloc(NMWHSLOT * sizeof *h->ptr);
	memset(h->ptr, 0, NMWHSLOT * sizeof *h->ptr);
	i = 0;
    }
    else
    {
	for (i = 0; i < h->nptr; i++)
	{
	    if (!h->ptr[i])
		break;
	}
	if (i == h->nptr)
	{
	    h->ptr = realloc(h->ptr, (h->nptr + NMWHSLOT) * sizeof *h->ptr);
	    memset(h->ptr + h->nptr, 0, NMWHSLOT * sizeof *h->ptr);
	    h->nptr += NMWHSLOT;
	}
    }
    h->ptr[i] = p;
}

static int
mwunhash(void *p)
{
    register struct hbucket *h;
    int i;

    h = MWhashtab + (((unsigned long) p >> NMWHSHIFT) & (NMWHASH - 1));
    for (i = h->nptr; i--; )
    {
	if (h->ptr[i] == p)
	    break;
    }
    if (i == -1)
	return 0;
    h->ptr[i] = 0;
    return 1;
}

#endif

void *
mallocw(unsigned size)
{
    void *p;

    if (!(p = malloc(size)))
	where_outta_here(1,250);
#ifdef LMCHECK
    mwhash(p);
#endif
    return p;
}

void *
callocw(cnt, size)
    unsigned cnt, size;
{
    void *p;

    p = mallocw(size * cnt);
    memset(p, 0, size * cnt);
    return p;
}

void
j_free(void *p)
{
  if (p) {
#ifdef LMCHECK
    if (!mwunhash(p))
    {
	printf("\r\7WARNING: free()ing unknown pointer %lx\r\n",
	       (unsigned long) p);
	return;
    }
#endif
    free(p);
  }
}

#ifndef LINUX

/*
 * these should by rights try to determine the available VM... oh, well
 * 05Sep2010, Maiko, This is DOS only, leave it out for LINUX systems.
 */

unsigned long availmem ()
{
    return MAINSTKBASE - (unsigned long) sbrk(0);
}

#endif

#ifdef	DONT_COMPILE
/*
 * 05Sep2010, Maiko (VE4KLM), Useless functions in my opinion, and I want
 * to break loose from the 'sbrk()' call, which is far from portable.
 */
unsigned long farcoreleft ()
{
#ifdef linux
#warning "farcoreleft - using 'linux' code"
  /* Suppose we look up RSS in the /proc fs and use that value (since getrusage() fails */
  /* If /proc/self/status doesn't exist, use the old scheme.  -- n5knx */
    FILE *proc;
    char line[80], *cp;

    if ((proc=fopen("/proc/self/status", "r")) != NULLFILE) {
        while(fgets(line, sizeof(line), proc) != NULLCHAR) {
            cp = skipnonwhite(line);
            *cp++ = '\0';
            if (!strcmp(line,"VmRSS:")) {
                fclose(proc);
                return (unsigned long)atol(cp);  /* in kB */
            }
        }
        fclose(proc);
    }
    return 0xC0000000 - (unsigned long) sbrk(0);
#elif defined(sun)
#warning "farcoreleft - using sun code"
#include <procfs.h>
    psinfo_t psi;
    FILE *proc;

    if ((proc=fopen("/proc/self/psinfo", "r")) != NULLFILE) {
        if (fread(&psi, 1, sizeof(psi), proc) == sizeof(psi)) {
            fclose(proc);
            return (unsigned long)psi.pr_size;  /* in kB */
        }
        fclose(proc);
    }
    return 0x80000000 - (unsigned long) sbrk(0);
#else
#warning "farcoreleft - using 'default' code"
    return 0x80000000 - (unsigned long) sbrk(0);
#endif
}

#endif

/*****************************************************************************\
*			Interrupt management - null			      *
\*****************************************************************************/

int
istate()
{
    return __istate;
}

int
dirps()
{
    sigset_t s;
    int ops;

    if (__istate)
    {
	sigemptyset(&s);
	sigaddset(&s, SIGALRM);
	sigprocmask(SIG_BLOCK, &s, (sigset_t *) 0);
    }
    ops = __istate;
    __istate = 0;
    return ops;
}

void
restore(ps)
    int ps;
{
    sigset_t s;

    if (__istate != ps)
    {
	sigemptyset(&s);
	sigaddset(&s, SIGALRM);
	sigprocmask((ps? SIG_UNBLOCK: SIG_BLOCK), &s, (sigset_t *) 0);
    }
    __istate = ps;
}

/*****************************************************************************\
*			      Date and time functions			      *
\*****************************************************************************/

int32 secclock()
{
#ifdef NO_GETTOD
    static struct timeb t;

    ftime(&t);
    return t.time - Starttime.tv_sec - (Starttime.tv_usec > t.millitm * 1000);
#else
    static struct timezone tz;
    static struct timeval tv;

    gettimeofday(&tv, &tz);
    return tv.tv_sec - Starttime.tv_sec - (Starttime.tv_usec > tv.tv_usec);
#endif
}

/*
 * 05Aug2010, Maiko (VE4KLM), System Clock is now 64 bits, changed this
 * function to return 64 bits, to fix 25 day negative timer problem.
 *
 * 08Aug2010, Maiko (VE4KLM),  A bit of a screwup on my part, the original
 * fix from 3 days ago will NOT work on 32 bit systems. You can't just cast
 * the result of an overflow'd difference between 2 time_t (long) values,
 * you have to do the actual math using 64 bit integers. Actually, this is
 * relevant only for 32 bit systems. Long is alot bigger on 64 bit systems,
 * but the code needs to work on BOTH, therefore this *refix* - works now !
 */
int64 msclock()
{
#ifdef NO_GETTOD
    static struct timeb t;

    ftime(&t);
    t.millitm *= 1000;
    if (t.millitm < Starttime.tv_usec)
    {
	t.millitm += 1000000;
	t.time--;
    }
    return (int64)((t.time - Starttime.tv_sec) * 1000 +
		(t.millitm - Starttime.tv_usec) / 1000);
#else
    static struct timezone tz;
    static struct timeval tv;

    /* 08Aug2010, Maiko (VE4KLM), You have to do the math in 64 bit ! */
    int64 st_usec, st_sec, tv_usec, tv_sec;

    gettimeofday (&tv, &tz);

    st_usec = Starttime.tv_usec;
    st_sec = Starttime.tv_sec;

    tv_usec = tv.tv_usec;
    tv_sec = tv.tv_sec;

    if (tv_usec < st_usec)     {
        tv_usec += 1000000;
        tv_sec--;
    }

    return (tv_sec - st_sec) * 1000 + (tv_usec - st_usec) / 1000;
#endif
}

static void
init_time(void)
{
#ifdef NO_GETTOD
    struct timeb t;

    ftime(&t);
    Starttime.tv_sec = t.time;
    Starttime.tv_usec = t.millitm * 1000;
#else
    struct timezone tz;

    gettimeofday(&Starttime, &tz);
#endif
}

void j2gettime (struct time *tp)
{
    struct tm *tm;
#ifdef NO_GETTOD
    static struct timeb tb;

    ftime(&tb);
    tm = localtime(&tb.time);
    tp->ti_hund = tb.millitm / 10;
#else
    static struct timeval tv;
    static struct timezone tz;

    gettimeofday(&tv, &tz);
    tm = localtime(&tv.tv_sec);
    tp->ti_hund = tv.tv_usec / 10000;
#endif
    tp->ti_hour = tm->tm_hour;
    tp->ti_min = tm->tm_min;
    tp->ti_sec = tm->tm_sec;
}

void j2getdate (struct date *dp)
{
    struct tm *tm;
#ifdef NO_GETTOD
    static struct timeb tb;

    ftime(&tb);
    tm = localtime(&tb.time);
#else
    static struct timeval tv;
    static struct timezone tz;

    gettimeofday(&tv, &tz);
    tm = localtime(&tv.tv_sec);
#endif
    dp->da_year = tm->tm_year + 1900;
    if (dp->da_year < 1970)  /* would Unix really use 00 for 2000?? */
        dp->da_year += 100;

    dp->da_mon = tm->tm_mon + 1;
    dp->da_day = tm->tm_mday;
}

long
dostounix(dp, tp)
    struct date *dp;
    struct time *tp;
{
    static struct tm tm;
    struct tm *tx;
    long now;

    tm.tm_year = dp->da_year - 1900;
    tm.tm_mon = dp->da_mon - 1;
    tm.tm_mday = dp->da_day;
    tm.tm_hour = tp->ti_hour;
    tm.tm_min = tp->ti_min;
    tm.tm_sec = tp->ti_sec;
    /* This desperately needs to be fixed.  How? */
    time(&now);
    tx = localtime(&now);
    tm.tm_isdst = tx->tm_isdst;
    return mktime(&tm);
}

/*****************************************************************************\
*			    Timers, I/O and scheduling			      *
\*****************************************************************************/

void
register_io(fd, event)
    int fd;
    void *event;
{
    struct io *evp;

    evp = mallocw(sizeof *evp);
    evp->fd = fd;
    evp->event = event;
    evp->next = Events;
    Events = evp;
}

void
unregister_io(fd)
    int fd;
{
    struct io *evp, *evc;

    for (evp = 0, evc = Events; evc && evc->fd!=fd; evp = evc, evc = evc->next)
	;
    if (!evc)
    {
	tprintf("unregister_io: unknown fd %d.\n",fd);
	return;
    }
    if (evp)
	evp->next = evc->next;
    else
	Events = evc->next;
    j_free(evc);
}

static void
ouch(int sig)
{
    struct sigaction sa;

    sa.sa_handler = SIG_DFL;
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, (struct sigaction *) 0);
#ifdef SIGBUS
    sigaction(SIGBUS, &sa, (struct sigaction *) 0);
#endif
    if (fork() == 0)
    {
	sigaction(sig, &sa, (struct sigaction *) 0);
#ifdef sun
	kill(getpid(), sig);
#else
	raise(sig);
#endif
    }
    detach_all_asy();
    exit(1);
}

static void
ding(int i)
/* i is the interrupt number to invoke this handler, or 0 (see giveup()) */
{
    extern struct timer *Timers;
    static struct timeval tv;
    struct timeval *tvp;
    static fd_set fds;
    struct io *evp;

    int64 oclock;

    /* do pending output */
    if (!i)   /*  we flush only when giveup calls us??? */
    {
        usflush(Current->output);  /* n5knx: helps jnos? */
	tflush();
#ifndef HEADLESS
	rflush();
#endif
    }

    /* collect input events to wait on */
    FD_ZERO(&fds);
    for (evp = Events; evp; evp = evp->next)
	FD_SET(evp->fd, &fds);
    /* get time until next timer; if zero, fake a very large one */
    /* if we have a nonzero argument, we're a timer tick; poll, don't block */
    if (i)
    {
	tv.tv_sec = tv.tv_usec = 0;
	tvp = &tv;
    }
    else if (!Timers)
	tvp = 0;
    else
    {
	tv.tv_sec = (time_t)((Timers->expiration - JnosClock) * MSPTICK);
	if (tv.tv_sec <= 0)
	    tv.tv_sec = 0;
	tv.tv_usec = (tv.tv_sec % 1000) * 1000;
	tv.tv_sec /= 1000;
	tvp = &tv;
    }
    /* check for I/O */
    select(FD_SETSIZE, &fds, 0, 0, tvp);
    /* signal events for pending I/O */
    for (evp = Events; evp; evp = evp->next)
    {
	if (FD_ISSET(evp->fd, &fds))
	    j2psignal(evp->event, 1);
    }
    /* run any due timers */
    j2psignal(&Tick, 1);
    /* and update the system time */
    oclock = JnosClock;
    JnosClock = msclock() / MSPTICK;
    Tick = (int)(JnosClock - oclock);
}


static void
init_tick(void)
{
    struct sigaction sa;
    struct itimerval it;

    sa.sa_flags = 0;
    sa.sa_handler = ding;
    sigaction(SIGALRM, &sa, (struct sigaction *) 0);
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = MSPTICK * 1000;
    it.it_value = it.it_interval;
    setitimer(ITIMER_REAL, &it, (struct itimerval *) 0);
}

static void
deinit_tick(void)
{
    struct itimerval it;

    it.it_interval.tv_sec = it.it_interval.tv_usec = 0;
    it.it_value = it.it_interval;
    setitimer(ITIMER_REAL, &it, (struct itimerval *) 0);
}

static void
cleanup (int sig)  /* from Tnos */
{
#if 0
        if (sig == SIGHUP)
                log (-1, "Received SIGHUP");
        where_outta_here ((sig == SIGHUP) ? 2 : 0, 255);
#else
        where_outta_here (0, 255);
#endif
}

void
init_sys(int no_itimer)
{
    struct sigaction sa;
    extern int coredumpok;

    init_time();
    register_io(0, &Keyboard);

    /* Check for essential directories. */
    mkdir(LogsDir, (mode_t)0755);
    mkdir(Fdir, (mode_t)0755);
    mkdir(Spoolqdir, (mode_t)0755);
    mkdir(Mailspool, (mode_t)0755);
    mkdir(Mailqdir, (mode_t)0755);
    mkdir(Routeqdir, (mode_t)0755);
#ifdef HTTP
    mkdir(HttpStatsDir, (mode_t)0755);
#ifdef HTTP_EXTLOG
    mkdir(HLogsDir, (mode_t)0755);
#endif
#endif /* HTTP */
#ifdef MAILBOX
    mkdir(Helpdir, (mode_t)0755);
    mkdir(Signature, (mode_t)0755);
#endif
#if defined NNTPS || defined NNTP
    mkdir(Newsdir, (mode_t)0755);
#endif

    if (!coredumpok) {
        sa.sa_handler = ouch;
        sa.sa_flags = 0;
        sigaction(SIGSEGV, &sa, (struct sigaction *) 0);
#ifdef SIGBUS
        sigaction(SIGBUS, &sa, (struct sigaction *) 0);
#endif
    }

    sa.sa_handler = cleanup;
    sa.sa_flags = 0;
    sigaction (SIGTERM, &sa, (struct sigaction *) 0);
#if 0
    sigaction (SIGHUP, &sa, (struct sigaction *) 0);
#endif

    sa.sa_handler = SIG_IGN;  /* from Tnos */
    sa.sa_flags = 0;
    (void) sigaction (SIGWINCH, &sa, (struct sigaction *) 0);

    if (!no_itimer)
	init_tick();
}

void
deinit_sys()
{
    deinit_tick();
    unregister_io(0);
}

void
giveup()
{
    /* suspend heartbeat */
    deinit_tick();
    /* block for I/O */
    ding(0);
    /* and reactivate the tick */
    init_tick();
}

#ifdef SHELL

int
doshell(int argc, char **argv, void *p)
{
    struct sigaction sa, old_int, old_quit;
    int ac, pid;
    int pi[2];
    char *av[4], c, *cmd;

    /*
     * Under *ix, one would expect ! or shell to work like in other *ix
     * programs.  Since we don't really want to emulate DOS doshell()'s
     * special handling for argv[1] == "/c" anyway :-) we will handle
     * this properly.
     *
     * argc < 2: invoke ${SHELL:-/bin/sh}
     *     >= 2: concatenate and /bin/sh -c it (NOT $SHELL)!
     *
     * N5KNX: essentially we emulate popen(), but we avoid blocking reads.
     */
#ifndef HEADLESS
    if (!sm_usestdio() || (argc==1 && strcmp(Curproc->name,"cmdintrp")))
    {  /* shell without args only legal from console. (could be AT, from remote sysop, etc) */
	tprintf("Shell command (without args?} rejected\n");
	return 1;
    }
#endif
    if (pipe(pi) == -1)
    {
	tprintf("Can't create pipe for subprocess\n");
	return 1;
    }

    /* Concatenate all arguments to form one command string */
    if (argc > 1) {
        for (ac=1, pid=0; argv[ac]; ac++)
            pid += strlen(argv[ac])+1;
        cmd = mallocw(pid);
        for (ac=1, *cmd=0; argv[ac]; ac++) {
            if(ac!=1) strcat(cmd, " ");
            strcat(cmd, argv[ac]);
        }
    }

    switch (pid = fork())
    {
    case -1:
	tprintf("Fork failed\n");
	return 1;
    case 0:
	close(pi[0]);
	ac = 1;
	if (argc > 1 || !(av[0] = getenv("SHELL")))
	    av[0] = "/bin/sh";
	if (argc > 1)
	{
	    av[ac++] = "-c";
	    av[ac++] = cmd;  /* argv[1] " " argv[2] " " argv[3] ... */
	}
	av[ac] = 0;

        if(argc > 1) dup2(pi[1],1); /* direct stdout to our pipe output */

	execv(av[0], av);
	_exit(1);
    default:
	close(pi[1]);
#ifndef HEADLESS
        if (argc == 1)
	    iosuspend();
#endif
	unregister_io(0);
	register_io(pi[0], &Shell);
	/* signal handling... */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGINT, &sa, &old_int);
	sigaction(SIGQUIT, &sa, &old_quit);

        do {
            pwait(&Shell);
            if ((ac = read(pi[0], &c, 1)) == 1)  /* read from pipe */
                tputc(c);
        } while (ac == 1);

	sigaction(SIGQUIT, &old_quit, 0);
	sigaction(SIGINT, &old_int, 0);
	unregister_io(pi[0]);
	close(pi[0]);
	register_io(0, &Keyboard);

#ifdef HEADLESS
	if (argc != 1)
	  free(cmd);
#else
        if (argc == 1) {
            ioresume();
            swapscreen(NULLSESSION, Command);
        }
        else free(cmd);
#endif
        /* this SHOULD take care of clearing zombie processes (from Tnos) */
        ac = 0;
        (void) waitpid (pid, &ac, WNOHANG);
    }
    return 0;
}

#endif
