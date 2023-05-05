/*
 * Session manager interface
 *
 * NOS uses several functions to interface with the outside world (not
 * including printf, which I'm trying to stomp out).  These were originally
 * called directly.
 *
 * The JNOS session manager translates calls to these functions into calls
 * through a "session manager switch".  This switch is an array of pointers to
 * session manager definitions, including a session manager name and a series
 * of function pointers.  Each session has a pointer to its session manager
 * definition taken from the session manager switch, and a pointer to data
 * which is specific to (and only known to) the session manager.
 *
 * There are several internal session managers:
 *
 * none
 *	All session manager operations are mapped to null functions except
 *	the keyboard, which maps to a function that never returns.  Used from
 *	/etc/rc.  Remappable.
 *	(Not yet present.  Without external sessions, this one is fatal...)
 *
 * dumb
 *	No screen or keyboard operations; suitable for use as a pipe (!) or
 *	file output.  Remappable.  (This one may not stay; it's intended more
 *	for debugging and experimentation with the session manager than for
 *	any practical use.)
 *
 * raw
 *	Uses raw terminfo for built-in operations, passes everything else to
 *	the actual terminal.  Another testing one.
 *	(Present but not tested:  ncurses has bugs in its terminfo-only
 *	interface.  May test this under SCO.)
 * 
 * curses
 *	The "original" session interface.
 *
 * ansi
 *	A modified "curses" with ANSI-X3.64 and PC line graphics support.
 *	(This will become a per-session option to "curses".)
 *
 * I intend to provide an "xterm" interface, which spawns slave xterms for its
 * sessions.
 *
 * The "wherex" and "wherey" entries are a problem for externally located
 * sessions, not to mention the "none" and "dumb" session managers.  They are
 * only used with split sessions.
 *
 * This is *not* the external session manager, which is a different entity
 * entirely.  The external session manager is a JNOS task which waits for
 * connections on a (Unix-domain or possibly IP) system socket and spawns
 * sessions whose I/O is mapped to the outside connection.  To map the I/O,
 * the external session manager will use a private session manager definition
 * (not present in the session manager switch), so this is a prerequisite for
 * external sessions... but this does not in and of itself enable external
 * sessions.  (External sessions also have headaches of their own:  the trace
 * and command sessions.  More later, when I tackle external sessions.)
 */

struct sessmgr_sw
{
    char *name;
    int flags;
#define SM_SPLIT	0x01	/* can handle split sessions */
#define SM_STDIO	0x02	/* multiplexed on stdin/stdout */
#define SM_SUSPEND	0x04	/* internal: suspended mpx'ed session */
#define SM_INIT		0x08	/* has been initialized */
#define SM_FIXED	0x10	/* can't change session managers */
#define SM_LOCK		0x20	/* I/O locked to avoid reentrancy problems */
    int (*init) __ARGS((const struct sessmgr_sw *));
    char *(*options) __ARGS((const struct sessmgr_sw *, char *));
    void *(*create) __ARGS((const struct sessmgr_sw *, struct session *));
    char *(*sessopt) __ARGS((const struct sessmgr_sw *, void *, char *));
    int (*swtch) __ARGS((const struct sessmgr_sw *, void *, void *));
    void (*putch) __ARGS((const struct sessmgr_sw *, void *, int));
    void (*clreol) __ARGS((const struct sessmgr_sw *, void *));
    void (*clrscr) __ARGS((const struct sessmgr_sw *, void *));
    int (*wherex) __ARGS((const struct sessmgr_sw *, void *));
    int (*wherey) __ARGS((const struct sessmgr_sw *, void *));
    void (*window) __ARGS((const struct sessmgr_sw *, void *, int, int, int,
			   int));
    void (*gotoxy) __ARGS((const struct sessmgr_sw *, void *, int, int));
    void (*high) __ARGS((const struct sessmgr_sw *, void *));
    void (*norm) __ARGS((const struct sessmgr_sw *, void *));
    void (*cursor) __ARGS((const struct sessmgr_sw *, void *, int));
    int (*kbread) __ARGS((const struct sessmgr_sw *, void *));
    void (*destroy) __ARGS((const struct sessmgr_sw *, void *));
    void (*status) __ARGS((const struct sessmgr_sw *, void *, int, char *));
    void (*rflush) __ARGS((const struct sessmgr_sw *, void *));
    void (*flush) __ARGS((const struct sessmgr_sw *, void *));
    void (*suspend) __ARGS((const struct sessmgr_sw *));
    void (*resume) __ARGS((const struct sessmgr_sw *));
    void (*end) __ARGS((const struct sessmgr_sw *));
    int refcnt;
};


/* in sessmgr.c */
void sm_status __ARGS((int pos, char *str));
void sm_splitscreen __ARGS((struct session *sp));
void UpdateStatus __ARGS((void));
void StatusRefresh __ARGS((int, void *, void *));
