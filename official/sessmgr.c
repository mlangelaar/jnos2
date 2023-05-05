/*
 * Session manager and session manager switch
 *
 * The session manager switch intercepts all session and console I/O calls and
 * forwards them to session managers based on the switch entry managing the
 * session, or the system default session manager for new sessions.  New
 * sessions may also be started attached to specific session managers.
 * Sessions can also be "reparented" from one session manager to another.
 *
 * Per-session options are now supported.  Per-session-*manager* options have
 * assigned slots in the session manager definition, but are not yet supported.
 */

#include <time.h>
#include "global.h"
#include "unix.h"
#include <signal.h>
#include "proc.h"
#include "socket.h"
#undef tputs
#include "tty.h"
#include "sessmgr.h"
#include "cmdparse.h"
#include "commands.h"
#include "usock.h"
#ifdef SM_CURSES
#undef newscreen
#include <curses.h>
#include <signal.h>
#endif /* SM_CURSES */

extern int StatusLines, Numrows, Numcols;
#ifdef SPLITSCREEN
extern char MainColors;
extern char SplitColors;
#endif

static int dosmstatus __ARGS((int, char **, void *));
static int dosmdefault __ARGS((int, char **, void *));
static int dosmcreate __ARGS((int, char **, void *));
static int dosmreparent __ARGS((int, char **, void *));
static int dosmoptions __ARGS((int, char **, void *));
static int LOCKSCR (register struct screen *sc, int wait);
static void UNLOCKSCR (register struct screen *sc);


/*
 * The session manager switch:  an array of pointers to session managers.
 */ 

#ifdef SM_CURSES
extern struct sessmgr_sw curses_sessmgr;
extern void curses_textattr(const struct sessmgr_sw *sm, void *dp, int fg_bg);
#endif
#ifdef SM_RAW
extern struct sessmgr_sw raw_sessmgr;
#endif
#ifdef SM_DUMB
extern struct sessmgr_sw dumb_sessmgr;
#endif

static struct sessmgr_sw *sessmgr_sw[] =
{
#ifdef SM_CURSES
    &curses_sessmgr,
#endif
#ifdef SM_DUMB
    &dumb_sessmgr,
#endif
#ifdef SM_RAW
    &raw_sessmgr,
#endif
    0,
};

static int sesm_initted;

/*
 * Commands for session manager administration
 */

static struct cmds SMcmds[] =
{
    { "create",		dosmcreate,	0,	0,	NULLCHAR },
    { "default",	dosmdefault,	0,	0,	NULLCHAR },
    { "options",	dosmoptions,	0,	0,	NULLCHAR },
    { "reparent",	dosmreparent,	0,	0,	NULLCHAR },
    { "status",		dosmstatus,	0,	0,	NULLCHAR },
    { NULLCHAR,	NULLFP((int,char**,void*)),	0,	0,	NULLCHAR }
};

static struct cmds SMsessions[] =
{
#ifdef BBSSESSION
    { "bbs",	dotelnet,	1024,	0,	NULLCHAR },
#endif
#ifdef AX25SESSION
    { "connect",	doconnect,	1024,	3,
	"connect <interface> <callsign>" },
#endif
#ifdef DIRSESSION
    { "dir",	dodir,		0,	0,	NULLCHAR },
#endif
#ifdef DIALER
    { "dialer",	dodialer,	512,	2,
	"dialer <iface> [<file> [<seconds> [<pings> [<hostid>]]]]" },
#endif
#if defined(MD5AUTHENTICATE) && defined(TELNETSESSION)
    { "etelnet",      dotelnet,    1024, 4, "etelnet <address> [port] loginid passwd" },
#endif
#ifdef FINGERSESSION
    { "finger",	dofinger,	1024,	2,	"finger name[@host]" },
#endif
#ifdef FTPSESSION
    { "ftp",	doftp,		2048,	2,	"ftp <address>" },
#endif
#ifdef HOPCHECKSESSION
    /* hopcheck - need to restrict to hopcheck itself */
#endif
#ifdef MORESESSION
    { "more",	domore,		0,	2,	"more <file> [searchstring]" },
#endif
#ifdef NETROM
    /* netrom connect */
#endif
#ifdef PINGSESSION
    { "ping",	doping,		512,	4,
	"ping <hostid> <length> <interval> [incflag]" },
#endif
#ifdef RLOGINSESSION
    { "rlogin",	dorlogin,	2048,	2,	"rlogin <address>" },
#endif
#ifdef AX25SESSION
    { "split",	doconnect,	1024,	3,
	"split <interface> <callsign>" },
#endif
#ifdef TELNETSESSION
    { "telnet",	dotelnet,	1024,	2,	"telnet <address> [port]" },
#endif
#ifdef TTYLINKSESSION
    { "ttylink",	dotelnet,	1024,	2,	"ttylink <address> [port]" },
#endif
    { NULLCHAR,	NULLFP((int,char**,void*)),	0,	0,	NULLCHAR }
};

/*
 * Used by the newscreen() entry point, the creation of the trace session
 * in main(), and by reparenting.
 */

static void
sm_newscreen(char *sc, struct session *sp)
{
    struct sessmgr_sw *sm;
    char *cp;

    if (!(sm = sm_lookup(sc, &cp)))
    {
	sm = sessmgr_sw[0];	/* system-wide default session manager */
	cp = 0;
    }
    if (!sp)
	return;
    sp->screen = mallocw(sizeof *sp->screen);
    if (!(sm->flags & SM_INIT))
    {
	if (Current && Current != sp && Current->screen &&
	    !(Current->screen->sessmgr->flags & SM_SUSPEND))
	{
	    (*Current->screen->sessmgr->suspend)(Current->screen->sessmgr);
	    Current->screen->sessmgr->flags |= SM_SUSPEND;
	}
	(*sm->init)(sm);
	sm->flags |= SM_INIT;
    }
    sm->refcnt++;
    sp->screen->sessmgr = sm;
    sp->screen->sesmdat =
	(*sm->create)(sm, sp);
    if (sm->sessopt && cp)
	(*sm->sessopt)(sm, sp->screen->sesmdat, cp);
    sp->screen->flags = 0;
    if (sp->split)
	sp->screen->flags |= SMS_SPLIT;
    if (!(sm->flags & SM_SPLIT))
	sp->split = 0;	/* if sm can't do split, split sessions are deranged */
    sp->screen->next_sm = j2strdup(sc);
    sp->screen->use_sm = sp->screen->next_sm;
    if (sm->flags & SM_SUSPEND)
    {
	(*Current->screen->sessmgr->resume)(Current->screen->sessmgr);
	Current->screen->sessmgr->flags &= ~SM_SUSPEND;
    }
#if 0
    /* this is ugly... */
    /* (it's also wrong; figure it out later...) */
    if (sp->type == TRACESESSION)
	sp->screen->flags |= SMS_DISCARD;
#endif
    sp->tsavex = sp->tsavey = 1;
#ifdef SPLITSCREEN
    sp->bsavex = 1;  sp->bsavey = Numrows-1;
#if defined(COLOR_SUPPORT) && defined(SM_CURSES)
    /* We'd like the bottom window to start out with its colors showing.
       This is accomplished by setting the colors, then clearing the window.
    */
    if(sp->split && sm->window) {
        LOCKSCR(sp->screen, 1);
	(*sm->window)(sm, sp->screen->sesmdat, 1, Numrows-1, Numcols, Numrows);
	curses_textattr(sm, sp->screen->sesmdat, SplitColors);
	(*sm->clrscr)(sm, sp->screen->sesmdat);
        UNLOCKSCR(sp->screen);
    }
#endif
#endif
}

int
dosessmgr(int argc, char **argv, void *p)
{
    if (argc < 2)
	return dosmstatus(argc, argv, p);
    return subcmd(SMcmds, argc, argv, p);
}

int
dosmstatus(int argc, char **argv, void *p)
{
    struct screen *sp;
    char *cp;
    int s;

    tprintf("Known session managers are:\n");
    for (s = 0; sessmgr_sw[s]; s++)
	tprintf("%s\n", sessmgr_sw[s]->name);
    tprintf("\n");
    dosmdefault(0, 0, 0);
    tprintf("\n#  Type         Name         Process      Manager  Options\n");
    tprintf("== ============ ============ ============ ======== ==========\n");
    for (s = 0; s < Nsessions; s++)
    {
	if (Sessions[s].type == FREE)
	    continue;
	tprintf("%-2d %-12s ", s, Sestypes[Sessions[s].type]);
	if (Sessions[s].name)
	    tprintf("%-12.12s ", Sessions[s].name);
	else
	    tprintf("             ");
	if (Sessions[s].proc->name && *Sessions[s].proc->name)
	    tprintf("%-12.12s", Sessions[s].proc->name);
	else
	    tprintf("%8.8lx    ", FP_SEG(Sessions[s].proc));
	tprintf(" %-8.8s", Sessions[s].screen->sessmgr->name);
	/* we don't support session MANAGER options just yet... */
	if ((sp = Sessions[s].screen)->sessmgr->sessopt &&
	    (cp = (*sp->sessmgr->sessopt)(sp->sessmgr, sp->sesmdat,(char *)0)))
	    tprintf(" %s", cp);
#if 1
        if(sm_blocked(&Sessions[s]))
            tprintf(" (blocked)");
#endif
	tprintf("\n");
    }
    return 0;
}

int
dosmdefault(int argc, char **argv, void *p)
{
    char *cp;

    if (argc < 2)
    {
	if (Current && Current->screen && Current->type == COMMAND)
	    cp = Current->screen->next_sm;
	else if (Command && Command->screen)
	    cp = Command->screen->next_sm;
	else
	    cp = "";
	if (!cp || !*cp)
	    cp = sessmgr_sw[0]->name;
	tprintf("Session manager for new sessions is \"%s\".\n", cp);
	return 0;
    }
    if (argc > 2)
    {
	tprintf("usage: sessmgr default [session-manager]\n");
	return 1;
    }
    if (!sm_lookup(argv[1], 0))
    {
	tprintf("No such session manager: \"%s\"\n", argv[1]);
	return 1;
    }
    if (Current->screen->next_sm)
	free(Current->screen->next_sm);
    Current->screen->next_sm = j2strdup(argv[1]);
    return 0;
}

int
dosmcreate(int argc, char **argv, void *p)
{
    int rc;

    if (argc < 3)
    {
	tprintf("usage: sessmgr create session-manager command [args...]\n");
	return 1;
    }
    if (!sm_lookup(argv[1], 0))
    {
	tprintf("Unknown session manager \"%s\"\n", argv[1]);
	return 1;
    }
    if (Current->type != COMMAND)
	write(2, "WHOA! I'm not being run from a COMMAND session!\n", 48);
    Current->screen->use_sm = argv[1];
    rc = subcmd(SMsessions, argc - 1, argv + 1, p);
    return rc;
}

int
dosmoptions(int argc, char **argv, void *p)
{
    struct screen *sp;
    char *cp;
    int i;

    if (argc < 2)
    {
	tprintf("usage: sessmgr options session [options]\n");
	return 1;
    }
    if ((i = atoi(argv[1])) < 0 || i > Nsessions || Sessions[i].type == FREE)
    {
	tprintf("Invalid session %s\n", argv[1]);
	return 1;
    }
    sp = Sessions[i].screen;
    if (argc < 3)
    {
	tprintf("Options for session: ");
	if (!sp->sessmgr->sessopt)
	    cp = 0;
	else
	    cp = (*sp->sessmgr->sessopt)(sp->sessmgr, sp->sesmdat, (char *) 0);
	if (!cp)
	    cp = "(none)";
	tprintf("%s\n", cp);
	return 0;
    }
    if (sp->sessmgr->sessopt)
	(*sp->sessmgr->sessopt)(sp->sessmgr, sp->sesmdat, argv[2]);
    return 0;
}

/*
 * Administration of the session manager interface
 */

struct sessmgr_sw *
sm_lookup(char *smname, char **optp)
{
    static char buf[1024];
    char *cp;
    int i;

    if (!smname)
    {
	/* inherit a reasonable session manager */
	if (Current && Current->screen && Current->type == COMMAND)
	{
	    smname = Current->screen->use_sm;
	    Current->screen->use_sm = Current->screen->next_sm;
	}
	else if (Command && Command->screen)
	    smname = Command->screen->next_sm;
	else
	    smname = "";
    }
    i = 0;
    for (cp = smname; *cp && *cp != ':'; cp++)
	buf[i++] = *cp;
    buf[i] = '\0';
    if (*cp)
	cp++;
    else
	cp = 0;
    if (!i)
    {
	if (optp)		/* is this wise?  sessmgr ":opt", that is */
	    *optp = cp;
	return sessmgr_sw[0];
    }
    for (i = 0; sessmgr_sw[i]; i++)
    {
	if (strcasecmp(sessmgr_sw[i]->name, buf) == 0)
	{
	    if (optp)
		*optp = cp;
	    return sessmgr_sw[i];
	}
    }
    if (optp)
	*optp = 0;
    return 0;
}

int
sm_blocked(struct session *sp)
{
    /* Return TRUE if the session is blocked. */
    return !(sp->screen->flags & SMS_ACTIVE) ||
	(sp->screen->sessmgr->flags & SM_SUSPEND);
}

/*
 * Session reparenting.  This is mildly interesting.
 *
 * The idea is that a session can be destroyed and recreated.  (Actually, the
 * session is retained; the screen is destroyed.)  The only fly in the oint-
 * ment is that you can't retain the contents of the screen:  some session
 * managers have no backing store ("dumb", and especially "none").
 *
 * Sessions can be reparented individually or by session manager.  Individual
 * sessions can be named if they have names.
 */

static void
sm_reparent(struct session *sp, char *new)
{
    struct sessmgr_sw *old, *np;
    int susp;

    if (!(np = sm_lookup(new, 0)))
	return;
    if ((old = sp->screen->sessmgr) == np)
	return;
    if (sp->screen->flags & SMS_SPLIT) /* sp->split is 0 if s.m. won't split */
	sp->split = 1;		/* newscreen will deactivate if needed */
    susp = (old->flags & SM_STDIO) && (np->flags & SM_STDIO);
    freescreen(sp);
    if (susp && !(old->flags & SM_SUSPEND))
    {
	(*old->suspend)(old);
	old->flags |= SM_SUSPEND;
    }
    sm_newscreen(new, sp);
}

int
dosmreparent(int argc, char **argv, void *p)
{
    struct sessmgr_sw *old;
    struct session *s;
    int i, susp;

    if (argc != 3)
    {
	tprintf("usage: sessmgr reparent <session> <session-manager>\n");
	return 1;
    }
    if (!sm_lookup(argv[2], 0))
    {
	tprintf("Unknown session manager \"%s\"\n", argv[2]);
	return -1;
    }
    if (isdigit(argv[1][0]))
    {
	if ((i = atoi(argv[1])) >= Nsessions || Sessions[i].type == FREE)
	{
	    tprintf("Invalid session number %d.\n", i);
	    return 1;
	}
	sm_reparent(Sessions + i, argv[2]);
	if (Sessions + i == Current)
	    Sessions[i].screen->flags |= SMS_ACTIVE;
	else
	{
	    /* we must do this because newscreen() partially switches us */
	    swapscreen(Current, Sessions + i);
	    swapscreen(Sessions + i, Current);
	}
	return 0;
    }
    susp = -1;
    for (i = 0; i < Nsessions; i++)
    {
	if (Sessions[i].type == FREE)
	    continue;
	if (Sessions[i].name && *Sessions[i].name &&
	    strcasecmp(Sessions[i].name, argv[1]) == 0)
	{
	    if (susp != -1)
		break;
	    susp = i;
	}
	if (Sessions[i].proc->name && *Sessions[i].proc->name &&
	    strcasecmp(Sessions[i].proc->name, argv[1]) == 0)
	{
	    if (susp != -1)
		break;
	    susp = i;
	}
	if (strcasecmp(Sestypes[Sessions[i].type], argv[1]) == 0)
	{
	    if (susp != -1)
		break;
	    susp = i;
	}
    }
    if (i != Nsessions)
    {
	tprintf("Session ID \"%s\" is not unique\n", argv[1]);
	return 1;
    }
    if (susp != -1)
    {
	sm_reparent(Sessions + susp, argv[2]);
	if (Sessions + susp == Current)
	    Sessions[susp].screen->flags |= SMS_ACTIVE;
	else
	{
	    /* we must do this because newscreen() partially switches us */
	    swapscreen(Current, Sessions + susp);
	    swapscreen(Sessions + susp, Current);
	}
	return 0;
    }
    for (i = 0; sessmgr_sw[i]; i++)
    {
	if (sessmgr_sw[i]->refcnt &&
	    strcasecmp(sessmgr_sw[i]->name, argv[1]) == 0)
	    break;
    }
    if (!sessmgr_sw[i])
    {
	tprintf("Can't decipher session ID \"%s\"\n", argv[1]);
	return 1;
    }
    /* reparent all of them! */
    old = sessmgr_sw[i];
    s = Current;
    for (i = 0; i < Nsessions; i++)
    {
	if (Sessions[i].screen->sessmgr != old)
	    continue;
	sm_reparent(Sessions + i, argv[2]);
	swapscreen(s, Sessions + i);
	s = Sessions + i;
    }
    if (s == Current)
	Sessions[i].screen->flags |= SMS_ACTIVE;
    else
	swapscreen(s, Current);
    return 0;
}

/*
 * Ugly hack for doshell():  tell if the current session manager is SM_STDIO.
 */

int
sm_usestdio(void)
{
    return Current->screen->sessmgr->flags & SM_STDIO;
}

/*
 * Start a session with a specified session manager.  This could be static
 * since it's normally only used by the "sessmgr <sm> create" command, but
 * we also need it to start the trace session in a different session manager
 * from the default.
 *
 * This incorporates newsession().  Ultimately, newsession() should be changed
 * to call this function.
 */

struct session *
sm_newsession(char *sc, char *name, int type, int split)
{
#define MASK_SPLIT 1
#define MASK_NOSWAP 2
    /*extern struct session *Trace;*/
    struct session *sp;
    int i;

    if (!sc || !*sc)
    {
	/*
	 * HACK - new sessions should inherit the current session if it is a
	 * COMMAND session.  Else use Command's next session, unless Command
	 * doesn't exist yet (creation of Command or Trace) in which case the
	 * default session manager is used.
	 *
	 * Maybe this isn't really a hack... but it feels like one.
	 */
	if (Current && Current->screen && Current->type == COMMAND)
	    sc = Current->screen->next_sm;
	else if (Command && Command->screen)
	    sc = Command->screen->next_sm;
	else
	    sc = "";
    }
    if (type == TRACESESSION)
	i = Nsessions - 1;
    else
    {
	for (i = 0; i < Nsessions; i++)
	{
	    if (Sessions[i].type == FREE)
		break;
	}
    }
    if (i == Nsessions)
	return NULLSESSION;
    sp = Sessions + i;

    sp->curdirs = NULL;
    sp->num = i;
    sp->type = type;
    sp->s = -1;
    if (name != NULLCHAR)
	sp->name = j2strdup(name);
    sp->proc = Curproc;
    Curproc->session = sp;  /* update Curproc's session pointer as well! */
    /* Create standard input and output sockets. Output is
     * translated to local end-of-line by default
     */

    /* close_s(Curproc->input); [not in sm_newsession!] */
    Curproc->input =  sp->input = j2socket(AF_LOCAL,SOCK_STREAM,0);
    seteol(Curproc->input,Eol);
    sockmode(Curproc->input,SOCK_BINARY);
#ifdef REDIRECT_2
    if ((up = itop(Curproc->output)) == NULLUSOCK || up->type != TYPE_WRITE_ONLY_FILE) {
        /* replace output socket unless it's used for output redirection (can't be tracing) */
#endif
        /* close_s(Curproc->output); [not in sm_newsession!] */
        Curproc->output = sp->output = j2socket(AF_LOCAL,SOCK_STREAM,0);
        seteol(Curproc->output,Eol);
        sockmode(Curproc->output,SOCK_ASCII);
#ifdef REDIRECT_2
    }
#endif

    /* on by default */
    sp->ttystate.crnl = sp->ttystate.edit = sp->ttystate.echo = 1;
    sp->flowmode = 0;	/* Off by default */
    sp->morewait = 0;
    sp->row = Numrows - 1 - StatusLines;
#ifdef SPLITSCREEN
    if ((sp->split = split&MASK_SPLIT) != 0)
        sp->row -= 2;   /* but suppose the sm doesn't support split? */
#else
    sp->split = split&MASK_SPLIT;
#endif
    sm_newscreen(sc, sp);
    if (!(split&MASK_NOSWAP)) {   /* screen swap enabled? */
        swapscreen(Current,sp);
        Current = sp;
    }
#ifdef STATUSWIN
    UpdateStatus();
#endif
    return sp;
}

/*
 * Session manager initiation and cleanup.
 */

void
ioinit(int no_itimer)
{
    init_sys(no_itimer);
    sesm_initted = 1;
}

void
iostop(void)
{
    int c;

    sesm_initted = 0;
    for (c = 0; sessmgr_sw[c]; c++)
    {
	if (sessmgr_sw[c]->flags & SM_INIT)
	    (*sessmgr_sw[c]->end)(sessmgr_sw[c]);
    }
    deinit_sys();
}

/*
 * Suspend and resume now only suspend/resume the current session manager.
 * It is assumed that the session manger is SM_STDIO and that an outside
 * process needs to intercept the SM_STDIO interface temporarily.
 */

void
iosuspend(void)
{
    register struct screen *sp = Current->screen;

    if ((sp->sessmgr->flags & (SM_INIT|SM_SUSPEND)) == SM_INIT)
    {
	(*sp->sessmgr->suspend)(sp->sessmgr);
	sp->sessmgr->flags |= SM_SUSPEND;
    }
}

void
ioresume(void)
{
    register struct screen *sp = Current->screen;

    if ((sp->sessmgr->flags & (SM_INIT|SM_SUSPEND)) == (SM_INIT|SM_SUSPEND))
    {
	(*sp->sessmgr->resume)(sp->sessmgr);
	sp->sessmgr->flags &= ~SM_SUSPEND;
    }
}

/*
 * Session creation, destruction, and swapping.
 */

void
j_newscreen(struct session *sp)
{
    char *sc;

    if (Current && Current->screen && Current->type == COMMAND)
    {
	sc = Current->screen->use_sm;
	Current->screen->use_sm = Current->screen->next_sm;
    }
    else if (Command && Command->screen)
	sc = Command->screen->next_sm;
    else
	sc = "";
    sm_newscreen(sc, sp);
}

void
freescreen(struct session *sp)
{
    if (!sp || !sp->screen)
	return;
    (*sp->screen->sessmgr->destroy)(sp->screen->sessmgr, sp->screen->sesmdat);
    free(sp->screen->next_sm);
    if (!--sp->screen->sessmgr->refcnt)
    {
	(*sp->screen->sessmgr->end)(sp->screen->sessmgr);
	sp->screen->sessmgr->flags &= ~SM_INIT;
    }
    free(sp->screen);
    sp->screen = 0;
}

int UpdateStatusBusy = 0;

void
swapscreen(struct session *old, struct session *new)
{
    if (old == new)
	return;
#ifdef STATUSWIN
    UpdateStatusBusy++;   /* prevent status window updates */
#endif
    /*
     * If they're in different session managers, swap each to/from NULL.
     * Otherwise, swap them in the common session manager.
     */
    if (old && new && old->screen->sessmgr == new->screen->sessmgr)
    {
	if (!(*old->screen->sessmgr->swtch)(old->screen->sessmgr,
					    old->screen->sesmdat,
					    new->screen->sesmdat))
	    old->screen->flags &= ~SMS_ACTIVE;
    }
    else
    {
	if (old)
	{
	    (*old->screen->sessmgr->swtch)(old->screen->sessmgr,
					   old->screen->sesmdat, 0);
	}
	if (old && new && (old->screen->sessmgr->flags & SM_STDIO) &&
	    (new->screen->sessmgr->flags & SM_STDIO) &&
	    !(old->screen->sessmgr->flags & SM_SUSPEND))
	{
	    (*old->screen->sessmgr->suspend)(old->screen->sessmgr);
	    old->screen->sessmgr->flags |= SM_SUSPEND;
	}
	if (new &&
	    ((new->screen->sessmgr->flags & (SM_SUSPEND|SM_STDIO|SM_INIT)) ==
	     (SM_SUSPEND|SM_STDIO|SM_INIT)))
	{
	    (*new->screen->sessmgr->resume)(new->screen->sessmgr);
	    new->screen->sessmgr->flags &= ~SM_SUSPEND;
	}
	if (new)
	{
	    (*new->screen->sessmgr->swtch)(new->screen->sessmgr, 0,
					   new->screen->sesmdat);
	}
    }
    if (new)
    {
	new->screen->flags |= SMS_ACTIVE;
	j2psignal(new->screen, 0);
    }
#ifdef STATUSWIN
    UpdateStatusBusy--;   /* allow status window updates */
#endif
}

/*
 * Some session managers (e.g. curses) aren't re-entrant.  We deal with this
 * by locking individual screens.  (we assume the session manager is reentrant
 * between separate screens; this may be unsafe...)
 */

static int
#ifdef __STDC__
LOCKSCR(register struct screen *sc, int wait)
#else
LOCKSCR(sc, wait)
    register struct screen *sc;
    int wait;
#endif
{
    sigset_t s, t;

    sigfillset(&s);
    sigprocmask(SIG_BLOCK, &s, &t);
    while (sc->flags & SM_LOCK)
    {
	if (!wait)
	    return 0;
	pwait(&sc->flags);
    }
    sc->flags |= SM_LOCK;
    sigprocmask(SIG_SETMASK, &t, 0);
    return 1;
}

static void
#ifdef __STDC__
UNLOCKSCR(register struct screen *sc)
#else
UNLOCKSCR(sc)
    register struct screen *sc;
#endif
{
    sigset_t s, t;

    sigfillset(&s);
    sigprocmask(SIG_BLOCK, &s, &t);
    sc->flags &= ~SM_LOCK;
    j2psignal(&sc->flags, 0);
    sigprocmask(SIG_SETMASK, &t, 0);
}

/*
 * I/O routines come in two flavors.  The internal ones, which start with
 * "s", are used by rflush() and by the external ones.  The external ones are
 * used by the rest of NOS.
 */

static void
sputch(struct session *s, int c)
{
    (*s->screen->sessmgr->putch)(s->screen->sessmgr, s->screen->sesmdat, c);
}

static void
scputs(struct session *s, char *str)
{
    register struct screen *sp;

    sp = s->screen;
    while (*str)
	(*sp->sessmgr->putch)(sp->sessmgr, sp->sesmdat, *str++);
}

static void
sclreol(struct session *s)
{
    (*s->screen->sessmgr->clreol)(s->screen->sessmgr, s->screen->sesmdat);
}

/*
 * rflush() used to flush data to the display process.  Now it does I/O itself;
 * the display process is defunct.
 */

void
rflush(void)
{
    struct session *sp;
    int i, c;

    if (!Sessions || !sesm_initted)
	return;	/* timer could tick before we exist, or while suspended */
    for (sp = Sessions, i = 0; i < Nsessions; sp++, i++)
    {
	if (sp->type == FREE)
	    continue;
	if (!sp->screen || !sp->screen->sessmgr || !sp->screen->sesmdat)
	    continue;
	if (!(sp->screen->flags & SMS_ACTIVE))
	    continue;	    
	/* err, do I want to do this? */
	if (sp->screen->sessmgr->flags & SM_SUSPEND)
	    continue;
	if (!LOCKSCR(sp->screen, 0))
	{
	    write(2, "screen not responding - still trying\r\n", 38);
	    continue;
	}
	(*sp->screen->sessmgr->rflush)(sp->screen->sessmgr,
				       sp->screen->sesmdat);

	if (sp->morewait == 2)
	{
	    sp->morewait = 0;
	    sputch(sp, '\r');
	    sclreol(sp);
	}

	while (sp->morewait != 1 && socklen(sp->output, 0) > 0)
	{
	    if ((c = rrecvchar(sp->output)) == -1)
		continue;
	    if (!sp->split || c != 0x0a)
		sputch(sp, c);
	    else
	    {
		scputs(sp, Eol);
		sclreol(sp);
	    }
	    if (sp->record != NULLFILE)
	    {
		if (c == '\r' || c == '\n')
		    fflush(sp->record);
		if (c != '\r' || sockmode(sp->output, -1) != SOCK_ASCII)
		    putc(c, sp->record);
	    }
	    if (sp->flowmode && c == '\n' && --sp->row <= 0) /* n5knx: like pc.c */
	    {
		scputs(sp, "--More--");
		sp->morewait = 1;
		break;
	    }
	}
	(*sp->screen->sessmgr->flush)(sp->screen->sessmgr,
				      sp->screen->sesmdat);
	UNLOCKSCR(sp->screen);
    }
}

/*
 * Public output routines.  getcursess() attempts to intuit the correct session
 * for output.
 */

static struct session *
getcursess(void)
{
    static struct proc *kbproc;

    /* this is a horrible hack that will (MUST!) go away in the future */
    if (!kbproc && Curproc && strcmp(Curproc->name, "keyboard") == 0)
	kbproc = Curproc;

    if (kbproc != Curproc) {
        /*
         * if Curproc's session isn't active we must block...
         * N.B. should handle trace with a special flag indicating that the
         * session should discard output instead of blocking
         */
        if (Curproc->session->screen && !(Curproc->session->screen->flags & SMS_ACTIVE))
        {
#if 0
	    if (Curproc->session->screen->flags & SMS_DISCARD)
	        return 0;
#endif
	    pwait(Current->screen);
        }
    }
    return Current;
}

void
putch(int c)
{
    struct session *sp;

    sp = getcursess();
    LOCKSCR(sp->screen, 1);
    sputch(sp, c);
    UNLOCKSCR(sp->screen);
}

void
cputs(char *s)
{
    struct session *sp;

    sp = getcursess();
    LOCKSCR(sp->screen, 1);
    scputs(sp, s);
    UNLOCKSCR(sp->screen);
}

static int
cprintf(char *fmt,...)
{
	va_list args;
	int len;
        char *buf;

        buf = mallocw(SOBUF);
	va_start(args,fmt);
        if((len=vsprintf(buf,fmt,args)) >= SOBUF) {
            /* It's too late to be sorry. He's dead, Jim. */
            where_outta_here(1,252);
        }
	va_end(args);

        cputs(buf);
        free(buf);
	return len;
}

void
clreol(void)
{
    struct session *sp;

    sp = getcursess();
    LOCKSCR(sp->screen, 1);
    sclreol(sp);
    UNLOCKSCR(sp->screen);
}

void
clrscr(void)
{
    register struct screen *sp;

    sp = getcursess()->screen;
    LOCKSCR(sp, 1);
    (*sp->sessmgr->clrscr)(sp->sessmgr, sp->sesmdat);
    UNLOCKSCR(sp);
}

int
wherex(void)
{
    register struct screen *sp;
    int i;

    sp = getcursess()->screen;
    if (sp->sessmgr->wherex)
    {
	LOCKSCR(sp, 1);
	i = (*sp->sessmgr->wherex)(sp->sessmgr, sp->sesmdat);
	UNLOCKSCR(sp);
	return i;
    }
    return -1;
}

int
wherey(void)
{
    register struct screen *sp;
    int i;

    sp = getcursess()->screen;
    if (sp->sessmgr->wherey)
    {
	LOCKSCR(sp, 1);
	i = (*sp->sessmgr->wherey)(sp->sessmgr, sp->sesmdat);
	UNLOCKSCR(sp);
	return i;
    }
    return -1;
}

void
window(int x1, int y1, int x2, int y2)
{
    register struct screen *sp;

    sp = getcursess()->screen;
    if (sp && sp->sessmgr->window)
    {
	LOCKSCR(sp, 1);
	(*sp->sessmgr->window)(sp->sessmgr, sp->sesmdat, x1, y1, x2, y2);
	UNLOCKSCR(sp);
    }
}

void
gotoxy(int x, int y)
{
    register struct screen *sp;

    sp = getcursess()->screen;
    LOCKSCR(sp, 1);
    (*sp->sessmgr->gotoxy)(sp->sessmgr, sp->sesmdat, x, y);
    UNLOCKSCR(sp);
}

void
highvideo(void)
{
    register struct screen *sp;

    sp = getcursess()->screen;
    LOCKSCR(sp, 1);
    (*sp->sessmgr->high)(sp->sessmgr, sp->sesmdat);
    UNLOCKSCR(sp);
}

void
normvideo(void)
{
    register struct screen *sp;

    sp = getcursess()->screen;
    LOCKSCR(sp, 1);
    (*sp->sessmgr->norm)(sp->sessmgr, sp->sesmdat);
    UNLOCKSCR(sp);
}

void
_setcursortype(int t)
{
    register struct screen *sp;

    sp = getcursess()->screen;
    if (sp->sessmgr->cursor)
    {
	LOCKSCR(sp, 1);
	(*sp->sessmgr->cursor)(sp->sessmgr, sp->sesmdat, t);
	UNLOCKSCR(sp);
    }
}

int
kbread(void)
{
    register struct screen *sp;

    sp = getcursess()->screen;
    return (*sp->sessmgr->kbread)(sp->sessmgr, sp->sesmdat);
}

/*
 * The status entry point is different:  any routine may call the status output
 * routine, which calls the status switch entry for the Command screen.  If
 * that is NULL, we perform direct output without passing information to
 * rflush(); this avoids the flow control problems which have plagued previous
 * versions, although it requires that we duplicate parts of rflush() here
 * (and it's ugly).
 *
 * The numeric argument is presently ignored.  Eventually I may use it for
 * a positioning hint.
 */

void
sm_status(int pos, char *str)
{
    struct screen *cs;

    cs = Command->screen;
    if (cs->sessmgr->status)
	(*cs->sessmgr->status)(cs->sessmgr, cs->sesmdat, pos, str);
    else
    {
	if (cs->sessmgr->flags & SM_SUSPEND)
	    return;
	if (!(cs->flags & SMS_ACTIVE))
	    return;
	LOCKSCR(cs, 1);
	(*cs->sessmgr->rflush)(cs->sessmgr, cs->sesmdat);
	/* should translate \n to Eol */
	scputs(Command, str);
#if 0
	scputs(Command, Eol);
#endif
	(*cs->sessmgr->flush)(cs->sessmgr, cs->sesmdat);
	UNLOCKSCR(cs);
    }
}

/* We now have the support routines for the Status Window (based on pc.c) */

/*
 * Used by dosession() to facilitate changing the split character of a session
 * screen. -- What a mess! n5knx
 */

void
sm_splitscreen(struct session *sp)
{
#ifdef SPLITSCREEN
    register struct screen *scrp = sp->screen;
    register struct sessmgr_sw *sm = sp->screen->sessmgr;

    if (sp->split)
      scrp->flags |= SMS_SPLIT;
    if (!(sm->flags & SM_SPLIT))
      sp->split = 0;  /* if sessmgr can't do split, revert */
    if (!sp->split) {
      scrp->flags &= ~SMS_SPLIT;
      if (sm->window) {
	LOCKSCR(scrp, 1);
	(*sm->window)(sm, scrp->sesmdat, 1,1+StatusLines,Numcols,Numrows);
	UNLOCKSCR(scrp);
	/*textattr(MainColors);*/
      }
    }
    /* now do a clrscr() in that screen ... to resync */
    LOCKSCR(scrp, 1);
    (*sm->clrscr)(sm, scrp->sesmdat);
#if defined(COLOR_SUPPORT) && defined(SM_CURSES)
    if (sp->split) {
        /* We'd like the bottom window to start out with its colors showing.
         * This is accomplished by setting the colors, then clearing the window.
         */
        if(sm->window) {
            (*sm->window)(sm, scrp->sesmdat, 1, Numrows-1, Numcols, Numrows);
            curses_textattr(sm, scrp->sesmdat, SplitColors);
            (*sm->clrscr)(sm, scrp->sesmdat);
        }
    }
#endif /* COLOR_SUPPORT && SM_CURSES */
    UNLOCKSCR(scrp);
#endif
}

#ifdef STATUSWIN
char MainStColors = WHITE+(MAGENTA<<4);
char SesStColors = WHITE+(BLUE<<4);
  
void StatusLine1(void);
  
/* First line. Global information */
void StatusLine1() {
  
    struct session *sp;
    int len=0,r;
    time_t now;
    char *cp;
    char hdrPrinted=0;

#ifdef	CONVERS	/* 05Jul2016, Maiko, compiler */
    extern int ConvUsers;
#ifdef LINK /* 05Jul2016, Maiko, compiler */
    extern int ConvHosts;
#endif
#endif

    extern int BbsUsers,FwdUsers,FtpUsers,SmtpUsers;
#ifdef HTTP
extern int HttpUsers;
#endif

#if defined(POP2SERVER) || defined(POP3SERVER) || defined(POP2CLIENT) || defined(POP3CLIENT)
#define HASPOP
    extern int PopUsers;
#else
#undef HASPOP
#endif

#ifdef TCPGATE
    extern int GateUsers;
#endif

#if defined(NNTP) || defined(NNTPS)
    extern int NntpUsers;
#endif

#if defined(SHOWIDLE) && !defined(UNIX)
    extern int idleticks;
#endif

#ifdef MAILFOR    
    extern int Mail_Received;   /* see mailfor.c */


/* PE1DGZ: Show blinking 'MAIL' if unread mail is present */
    if(Mail_Received) {
        textattr(MainStColors | 0x80);
        len += cprintf("MAIL ");
    }
#endif    
  
    /* Set the colors */
    textattr(MainStColors);
  

    now=time(&now);
    cp=ctime(&now);
    len+=cprintf("%5.5s",cp+11);

#ifdef DONT_COMPILE
  /* 05Sep2010, Maiko, Useless information and wrong ! */
    len+=cprintf("%-6.6lu", farcoreleft());
#endif

#ifdef CONVERS
#ifdef LINK
    if (ConvUsers || ConvHosts)
        len+=cprintf(" CONV=%d LNKS=%d", ConvUsers, ConvHosts);
#else
    if (ConvUsers)
        len+=cprintf(" CONV=%d", ConvUsers);
#endif /* LINK */
#endif /* CONVERS */

#ifdef MAILBOX
    if (BbsUsers)
        len+=cprintf(" BBS=%d", BbsUsers);

#ifdef MBFWD
    if (FwdUsers)
        len+=cprintf(" FWD=%d", FwdUsers);
#endif
#endif

#ifdef FTPSERVER
    if (FtpUsers)
        len+=cprintf(" FTP=%d", FtpUsers);
#endif

#ifdef TCPGATE
    if (GateUsers)
        len+=cprintf(" GATE=%d", GateUsers);
#endif 

#ifdef HTTP
    if (HttpUsers)
        len+=cprintf(" HTTP=%d", HttpUsers);
#endif

    if (SmtpUsers)
        len+=cprintf(" SMTP=%d", SmtpUsers);

#ifdef HASPOP
    if (PopUsers)
    len+=cprintf(" POP=%d", PopUsers);
#endif

#if defined(NNTP) || defined(NNTPS)
    if (NntpUsers)
    len+=cprintf(" NNTP=%d", NntpUsers);
#endif

#if defined(SHOWIDLE) && !defined(UNIX)
    if (Watchdog) {
        len+=cprintf(" IDLE=%d", idleticks);
        idleticks = 0;
    }
#endif

    /* Print all active sessions . Modified from TNOS
     * Calculate how much room there is left on the line
     */
  
    for(sp=Sessions; sp < &Sessions[Nsessions];sp++) {
/*        if(Numcols - len < (hdrPrinted ? 3 : 8))*/
/*            break;*/
        if(sp->type == FREE || sp->type == COMMAND || sp->type == TRACESESSION)
            continue;

        if (!hdrPrinted) {
            len+=cprintf(" Ses:");
            hdrPrinted=1;
        }
  
        /* if there is data waiting, blink the session number */
        r = socklen(sp->output,1);
        textattr ( (r) ? MainStColors | 0x80 : MainStColors);
        len+=cprintf (" %d", sp->num);
    }
    textattr(MainStColors); /* In case blinking was on! */
    clreol();
}
  
#ifdef MAILBOX
char *StBuf2;    /* allocated in main.c */
int StLen2;  

void StatusLine2(void);
  
void StatusLine2() {
    struct mbx *m;
    char *cp;
    int len;
  
    cp = StBuf2+StLen2;
    *cp = '\0';
    for(m=Mbox;m;m=m->next) {
        if((len = strlen(m->name)) != 0 && (len < Numcols-(cp-StBuf2)-4)) {
            *cp++ = ' ';
            if(m->sid & MBX_SID)
                *cp++ = '*';    /* Indicate a bbs */
            else switch(m->state) {
                case MBX_GATEWAY:
                    *cp++ = '!';
                    break;
                case MBX_READ:
                case MBX_SUBJ:
                case MBX_DATA:
                    *cp++ = '#';
                    break;
                case MBX_UPLOAD:
                case MBX_DOWNLOAD:
                case MBX_XMODEM_RX:
                case MBX_XMODEM_TX:
                    *cp++ = '=';
                    break;
                case MBX_SYSOPTRY:
                case MBX_SYSOP:
                    *cp++ = '@';
                    break;
                case MBX_CONVERS:
                case MBX_CHAT:
                    *cp++ = '^';
                    break;
                case MBX_CMD:
                    *cp++ = ' ';    /* To keep things aligned nicely */
                    break;
                default:
                    *cp++ = '?';
                    break;
            }
            strcpy(cp,m->name);
            cp += len;
        }
    }
    cputs(StBuf2);
    clreol();
}
#endif /* MAILBOX */
  
char *StBuf3;    /* allocated in main.c */
int StLen3;
void StatusLine3(void);
  
/* The session dependent data */
void StatusLine3() {
    char *cp;
    int  s,t,SesType;
    struct usock *up;
  
    static struct session *MyCurrent;
    static int SesNameLen;
    static int SockStatus,SockName,SesData;
  
    /* Set the colors */
    textattr(SesStColors);
  
    /* Next line. Session specific information */
    if(MyCurrent != Current) {
        /* Keep track of the current session */
        MyCurrent = Current;
        SesType = MyCurrent->type;
        /* Remember to offset for "\r\n" at start of buffer ! */
        SesNameLen = 2;
        SesNameLen+=sprintf(StBuf3+SesNameLen,"%d %s:",
        MyCurrent->num,Sestypes[SesType]);
  
        /* We can't show network socket data until socket is valid ! */
        SesData = SockName = 0;
  
        SockStatus = 1; /* Show socket status by default */
        if(SesType == COMMAND || SesType == TRACESESSION) {
// The problem with showing cwd is knowing when to rebuild StBuf3 after a change,
// and it must be done efficiently (in the above if(MyCurrent!=Current) cmd).
// docd() should probably increment a counter which we compare here .. worth it?
//          if((Command) && (Command->curdirs))
//              sprintf(StBuf3+SesNameLen, " cwd=%s",Command->curdirs->dir);
            SockStatus = 0;     /* Don't show socket name and ses data */
  
        } else if(SesType == MORE ||
            SesType == REPEAT ||
            SesType == LOOK) {
                sprintf(StBuf3+SesNameLen," %-.60s",MyCurrent->name);
                SockStatus = 0;     /* Don't show socket name and ses data */
          }
    }
    /* Only if this is a session with a network socket do we show the status */
    if(SockStatus)
	{
        if(!SockName && (s=MyCurrent->s) != -1)
		{
            struct sockaddr fsocket;

            int i = sizeof(fsocket); /* 11Nov2004, Maiko, VERY Important ! */
  
            /* The session now has a valid network socket.
             * Go get the name, and pointer.
             */
            if(j2getpeername(s,(char *)&fsocket,&i) == -1)
                cp = "";
            else cp = psocket(&fsocket);
            SesNameLen+=sprintf(StBuf3+SesNameLen," %-18.18s TxQ ",cp);
            SockName = SesData = 1;
        }
        /* We have the socket name, now go print the socket session data */
        if(SockName) {
            /* Some sessions keep hanging on to their network socket
             * until the use hits return to close the session !
             * Others will delete it sooner...
             */
            if((s=MyCurrent->s) != -1 && ((cp=sockstate(s))!= NULL) ) {
                /* Network socket for session still valid */
                t = socklen(s,1);
                StLen3 = SesNameLen +
                sprintf(StBuf3+SesNameLen,"%4.4d St: %-12.12s",t,cp);
            } else {
                SesData = 0;    /* Don't print rest of line 3 ! */
                sprintf(StBuf3+SesNameLen-4,"  LIMBO !");
            }
            /* If the socket is still valid, print some data */
            if(SesData) {
                up = itop(s);
                switch(up->type) {
                    case(TYPE_TCP):
                    {
                        struct tcb *tcb = up->cb.tcb;
                        sprintf(StBuf3+StLen3, " T: %5.5d/%-5.5d ms",
                          read_timer(&tcb->timer), dur_timer(&tcb->timer));
                    }
                        break;
#ifdef AX25
                    case(TYPE_AX25I):
                    {
                        struct ax25_cb *axp = up->cb.ax25;
                        sprintf(StBuf3+StLen3, " T1: %5.5d/%5.5d ms",
                         read_timer(&axp->t1), dur_timer(&axp->t1));
                    }
                        break;
#endif
#ifdef NETROM
                    case(TYPE_NETROML4):
                    {
                        struct nr4cb *cb = up->cb.nr4;
                        sprintf(StBuf3+StLen3, " T: %5.5d/%5.5d ms",
                         read_timer(&cb->tcd), dur_timer(&cb->tcd));
                    }
                        break;
#endif
                }
            }
        }
    }
    cputs(StBuf3);
    clreol();
}
  
/* Build the status window on the screen - WG7J */
void UpdateStatus() {
  
    int currx, curry;
    struct session *sesp;
    struct screen *sp;
    struct sessmgr_sw *sm;


    if(!StatusLines || UpdateStatusBusy)
        return;

    /* check if the sessmgr has SM_SPLIT capability */  
    sesp = getcursess();
    if ((sp = sesp->screen) == NULL)
        return;  /* no screen => destroying or creating session */
    if(sesp->type == TRACESESSION) return;  /* don't bother trace w/ status win */
    sm = sp->sessmgr;
    if(!(sm->flags & SM_SPLIT))
        return;  /* s.m. can't split, so can't do status win either */
    UpdateStatusBusy++;

    /* get the current output context */
    currx = wherex();
    curry = wherey();
  
    /* create the status window */
    window(1,1,Numcols,StatusLines);

#if defined(COLOR_SUPPORT) && defined(SM_CURSES)
    if (!MainColors)
#endif
        highvideo();   /* NO COLORS, so use highlighting */

    StatusLine1();  /* Global system status */
    if(StatusLines > 1)
#ifdef MAILBOX
        StatusLine2();  /* Mailbox user status */
    if(StatusLines > 2)
#endif
#ifdef SWDEBUG
	;
#else
        StatusLine3();    /* Session dependent status */
#endif
  
    /* restore the previous output context */
    /* Note we assume we never encounter the cursor in the split input window */
    window(1,1+StatusLines, Numcols, (sesp->split ? Numrows-2 : Numrows));
    textattr(MainColors);
    gotoxy(currx,curry);
#if (defined(COLOR_SUPPORT) && defined(SM_CURSES))
    if (!MainColors)
#endif
        normvideo();         /* UNDO highvideo() mode established above */

    UpdateStatusBusy--;
    /* log(-1,"UpdateStatus: end @(%d,%d) [%d]",currx,curry,sesp->num);*/
}
  
/* Now the Status window refresh process */
void
StatusRefresh(int i, void *v1, void *v2)
{
    for (;;) {
        j2pause(1000L);
        if(StatusLines) {
            UpdateStatus();
        }
    }
}
#endif /* STATUSWIN */

#if defined(COLOR_SUPPORT) && defined(SM_CURSES)
/* Emulate Borland C's textattr(int fg_bg). The high nibble contains the
   background color (0x80 set for blinking), and the low nibble contains the
   foreground color.  0x08 is set to "lighten" the color.  Colors 0..7 are:
   black, blue, green, cyan, red, magenta, brown/yellow, grey/white.
   We ought to add an entry to the sessmgr_sw entrypoint array, but presently
   textattr() only works in SM_CURSES. -- n5knx
*/
void
textattr(int fg_bg)
{
    register struct screen *sp;
    struct session *sesp;

    sesp = getcursess();
    if ( sesp && (sp = sesp->screen) != NULL) {
        LOCKSCR(sp, 1);

        curses_textattr(sp->sessmgr, sp->sesmdat, fg_bg);
    
        UNLOCKSCR(sp);
    }
}
#endif
