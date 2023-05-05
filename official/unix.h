#ifndef	_UNIX_H
#define	_UNIX_H

/*
 * Making a very PC-hardware-oriented program work under Unix is, to say the
 * least, interesting.
 */

#ifndef	_GLOBAL_H
#include "global.h"
#endif

#include <ctype.h>

struct session;

struct screen {
    struct sessmgr_sw *sessmgr;	/* session manager */
    int flags;			/* per-session flags */
#define SMS_ACTIVE	0x01	/*   session can do output */
#define SMS_DISCARD	0x02	/*   if not active, don't block; discard */
#define SMS_NOFLOW	0x04	/*   don't do flow control */
#define SMS_SPLIT	0x08	/*   wants to be split, if possible */
    void *sesmdat;		/* per-session data for session manager */
    char *next_sm;		/* session manager for new sessions */
    char *use_sm;		/* session manager override for "create" */
};
#define	NULLSCREEN	(struct screen *)0

/* from unix.c */
extern void register_io __ARGS((int fd, void *event));
extern void unregister_io __ARGS((int fd));
extern unsigned long filelength __ARGS((int fd));
#ifdef NEED_STRCASECMP
extern int strcasecmp __ARGS((char *s1, char *s2));
#endif
#ifdef NEED_STRNCASECMP
extern int strcasecmp __ARGS((char *s1, char *s2));
#endif
extern void init_sys __ARGS((int));
extern void deinit_sys __ARGS((void));
extern void giveup __ARGS((void));
extern void j_free __ARGS((void *));
extern int doshell __ARGS((int, char **, void *));
extern unsigned long availmem(void);
extern unsigned long farcoreleft(void);

/* from unixasy.c */
extern int doasystat __ARGS((int, char **, void *));
extern int doasyconfig __ARGS((int, char **, void *));

/* from sessmgr.c */
extern void ioinit __ARGS((int));
extern void iostop __ARGS((void));
extern void iosuspend __ARGS((void));
extern void ioresume __ARGS((void));
extern void swapscreen __ARGS((struct session *old, struct session *new));
extern void putch __ARGS((int c));
extern void cputs __ARGS((char *));
extern void clreol __ARGS((void));
extern void rflush __ARGS((void));
extern void j_newscreen __ARGS((struct session *));
extern void freescreen __ARGS((struct session *));
extern void clrscr __ARGS((void));
extern int wherex __ARGS((void));
extern int wherey __ARGS((void));
extern void window __ARGS((int, int, int, int));
extern void gotoxy __ARGS((int, int));
extern void highvideo __ARGS((void));
extern void normvideo __ARGS((void));
extern void _setcursortype __ARGS((int));
extern int kbread __ARGS((void));
extern struct sessmgr_sw *sm_lookup __ARGS((char *, char **));
extern struct session *sm_newsession __ARGS((char *, char *, int, int));
extern int sm_blocked __ARGS((struct session *));
extern int sm_usestdio __ARGS((void));

/* from unixasy.c */
extern void detach_all_asy __ARGS((void));

/*
 * 06Oct2009, Maiko, Enough already !!! Don't declare functions that are
 * already defined in the system headers of the O/S - ie, lseek, etc !
 */

#include <sys/types.h>
#include <unistd.h>

#define newscreen j_newscreen

#define _NOCURSOR 0
#define _NORMALCURSOR 1
#define _SOLIDCURSOR 2

#ifndef tell
#define tell(fh)	lseek(fh,0L,1);
#endif

#ifdef SM_CURSES
#define COLOR_SUPPORT	/* n5knx: try to use colors */
#endif

#define lowvideo normvideo
#if defined(COLOR_SUPPORT) && defined(SM_CURSES)
extern void textattr(int fg_bg);   /* in sessmgr.c */
#else
#define textattr(fg_bg)
#endif

#if !defined(__COLORS)
#define __COLORS
/* We define DOS colors since we want to be compatible with the -w/x/y/z opts */
enum COLORS {
    BLACK,          /* dark colors */
    BLUE,
    GREEN,
    CYAN,
    RED,
    MAGENTA,
    BROWN,
    LIGHTGRAY,
    DARKGRAY,       /* light colors */
    LIGHTBLUE,
    LIGHTGREEN,
    LIGHTCYAN,
    LIGHTRED,
    LIGHTMAGENTA,
    YELLOW,
    WHITE
};
#endif

#define BLINK       128 /* blink bit */


#endif	/* _UNIX_H */
