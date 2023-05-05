/*
 * JNOS 2.0
 *
 * $Id: curses.c,v 1.1 2015/04/22 01:51:45 root Exp root $
 *
 * Intelligent session manager using curses.  Options include ANSI X3.64
 * emulation with both VTx00 and PC graphics.
 *
 * Brandon S. Allbery KF8NH
 * No copyright, copyleft, copysideways, copytwisted, ....
 *
 * 29Jan2012, Maiko (VE4KLM), NOS PANIC (keyboard) *fix*
 */
#include <curses.h>
#include <term.h>
#undef FALSE
#undef TRUE

/* This nonsense WILL go away... */
#ifdef NCURSES_VERSION
#define _tmarg _regtop
#define _bmarg _regbottom
#ifdef very_old_linux
#ifndef wbkgdset
#define wbkgdset(w,attr) ((w)->_bkgd = (attr))
#endif
#ifndef getattrs
#define getattrs(w) ((w)->_attrs)
#endif
#endif /* very_old_linux */
#endif /* NCURSES_VERSION */

#include "unix.h"
#include "proc.h"
#include "socket.h"
#undef tputs
#include "tty.h"
#include "sessmgr.h"
#include "config.h"

#ifdef SM_CURSES

#ifdef NCURSES_VERSION
char curseslibver[] = NCURSES_VERSION;
#else
/* use value provided from the Makefile */
char curseslibver[] = LCURSES;
#endif

#ifdef linux
extern int isatty __ARGS((int));
#endif

#define UNUSED(x) ((void)(x)) /* 15Apr2016, VE4KLM, trick to suppress
					compiler warnings */

extern struct sessmgr_sw curses_sessmgr; /* forward declaration */

static int Suspense, initted;
static TERMINAL *my_term;
static void *curscreen;

#ifdef COLOR_SUPPORT
static char *color_pair_map;
extern char MainColors;
static int dos2unixcolor[8] = {COLOR_BLACK, COLOR_BLUE, COLOR_GREEN, COLOR_CYAN,
			COLOR_RED, COLOR_MAGENTA, COLOR_YELLOW, COLOR_WHITE};
#endif

struct keytrie
{
    enum {KT_DEF, KT_TRIE, KT_TVAL, KT_VAL} kt_type; /* type of entry */
    int kt_tval;		/* if a TVAL, the timed-out value */
    union
    {
	struct keytrie *ktu_trie; /* sub-trie */
#define kt_trie kt_u.ktu_trie
	int ktu_val;		/* value */
#define kt_val kt_u.ktu_val
    } kt_u;
};

struct curses_data
{
    WINDOW *win;
    struct
    {
	int x, y, tmarg, bmarg, valid;
	chtype attr, bkgd;
    } save;
    char parm[8];
    char nparm;
    char bg;			/* current background */
    char fg;			/* current foreground */
    char flags;
#define C_ESC		0x01
#define C_CSI		0x02
#define C_INS		0x04
#define C_TTY		0x08
#define C_BOLD		0x10
#define C_8BIT		0x20
};

static struct keytrie *keys;

/* mapping of IBM line-drawing characters to curses line drawing */
/* this is evil: it doesn't use e.g. ACS_HLINE because that's an array ref */
static chtype ibm_map[128] =
{
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
    'a', 'a', 'a', 'x', 'u', 'u', 'u', 'k',
    'k', 'u', 'x', 'k', 'j', 'j', 'j', 'k',
    'm', 'v', 'w', 'u', 'q', 'n', 'u', 'u',
    'j', 'l', 'v', 'w', 'u', 'q', 'n', 'v',
    'v', 'w', 'w', 'm', 'm', 'l', 'l', 'n',
    'n', 'j', 'l',  0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
     0,   0,   0,   0,   0,   0,   0,   0,
};

#ifdef M_UNIX
#define Getmaxy(w) getmaxy(w)
#define Getmaxx(w) getmaxx(w)
#else

static int
Getmaxy(WINDOW *w)
{
    int y, x;

	UNUSED (x);	/* 16Apr2016, Maiko (VE4KLM), compiler warnings */

    getmaxyx(w, y, x);
    return y;
}

static int
Getmaxx(WINDOW *w)
{
    int y, x;

	UNUSED (y);	/* 16Apr2016, Maiko (VE4KLM), compiler warnings */

    getmaxyx(w, y, x);
    return x;
}

#endif

static struct keytrie *
key_init(void)
{
    struct keytrie *k;
    int i;

    k = mallocw(256 * sizeof *keys);
    for (i = 256; i--; )
    {
	k[i].kt_type = KT_DEF;
	k[i].kt_val = i;
    }
    return k;
}

static void
key_add(const char *str, int val)
{
    struct keytrie *t;
    int c;

    /*
     * Follow the trie until we get to the right subtrie for the string, then
     * add the value.  If we hit a KT_DEF, expand the trie.  If we hit a KT_VAL
     * change it to a KT_TVAL, which times out as a KT_VAL but continues as a
     * KT_TRIE.  If given a value for a KT_TRIE, change it to a KT_TVAL.
     */
    if (!str || !*str)
	return;			/* no key to define */
    t = keys;
    while (str[1])
    {
	c = uchar(*str++);
	if (t[c].kt_type != KT_TRIE)
	{
	    if (t[c].kt_type == KT_DEF)
		t[c].kt_type = KT_TRIE;
	    else
	    {
		t[c].kt_type = KT_TVAL;
		t[c].kt_tval = t[c].kt_val;
	    }
	    t[c].kt_trie = key_init();
	}
	t = t[c].kt_trie;
    }
    c = uchar(*str);
    if (t[c].kt_type == KT_TRIE)
    {
	t[c].kt_type = KT_TVAL;
	t[c].kt_tval = val;
    }
    else if (t[c].kt_type == KT_DEF)
    {
	t[c].kt_type = KT_VAL;
	t[c].kt_val = val;
    }
}

#ifdef COLOR_SUPPORT

static int
map_colors(int bg, int fg)
{
    int cp, cmask;

    if (!color_pair_map)
	return 0;
#ifdef COLORDEBUG
    fprintf(stderr, "map_colors %d %d", bg, fg);
#endif
    cmask = (bg << 4) | fg;
    for (cp = 0; cp < COLOR_PAIRS; cp++)
    {
	if (color_pair_map[cp] == -1 || color_pair_map[cp] == cmask)
	    break;
    }
    if (cp == COLOR_PAIRS) {
#ifdef COLORDEBUG
        fprintf(stderr, " - out of pairs!\n");
#endif
	return 0;
}
    if (color_pair_map[cp] == -1)
    {
      if (init_pair(cp + 1, fg, bg) == ERR) {
#ifdef COLORDEBUG
            fprintf(stderr, "can't create pair %d\n", cp);
#endif
	    cp = 0;
      }
      else {
#ifdef COLORDEBUG
            fprintf(stderr, " - new pair %d\n", cp);
#endif
	    color_pair_map[cp] = cmask;
      }
    }
#ifdef COLORDEBUG
    else fprintf(stderr, " is pair %d\n", cp);
#endif
    return COLOR_PAIR((cp + 1));
}

#endif

static int
curses_init(const struct sessmgr_sw *sm)
{
    extern int Numrows, Numcols;

    if (!isatty(0))
	return 0;
    if (initted)
    {
#ifdef RAW_SESSMGR
	set_curterm(my_term);
#endif
	refresh();		/* bring curses back to life */
    }
    else
    {
	initscr();			/* be nice to trap errors... */
#ifdef COLOR_SUPPORT
	/*
	 * I assume the curses manpage tells the truth when it claims that
	 * colors are initialized to RGB defaults.  If not, I may need to set
	 * up the colors in question...
	 */
	if (has_colors() && start_color() != ERR && COLORS >= 8)
	{
	    color_pair_map = mallocw(COLOR_PAIRS * sizeof *color_pair_map);
	    memset(color_pair_map, -1, COLOR_PAIRS * sizeof *color_pair_map);
	    map_colors(COLOR_BLACK, COLOR_WHITE); /* default color pair */
            if(!MainColors) MainColors = LIGHTGRAY+(BLACK<<4); /* not high-intensity */
	}
        else MainColors = 0;  /* no colors available */
#endif
	my_term = cur_term;
	noecho();
	nonl();
	raw();
	keys = key_init();
	key_add(key_down, DNARROW);
	key_add(key_f1, -3);
	key_add(key_f2, -4);
	key_add(key_f3, -5);
	key_add(key_f4, -6);
	key_add(key_f5, -7);
	key_add(key_f6, -8);
	key_add(key_f7, -9);
	key_add(key_f8, -10);
	key_add(key_f9, -11);
#ifdef M_UNIX
	/* SCO botches it, as per usual */
	key_add(key_f0, -2);
#else
	key_add(key_f10, -2);
#endif
	key_add(key_left, LTARROW);
	key_add(key_right, RTARROW);
	key_add(key_up, UPARROW);
	key_add("\177", '\b');	/* so DEL behaves as BS */
	initted = 1;
    }
    Suspense = 0;
    Numrows = LINES;
    Numcols = COLS;
    return 1;
}

static void
curses_end(const struct sessmgr_sw *sm)
{
    if (!Suspense)
    {
	wrefresh(((struct curses_data *) Current->screen->sesmdat)->win);
	endwin();
    }
}

static void
curses_suspend(const struct sessmgr_sw *sm)
{
    if (!Suspense++)
    {
	/* note that we use stdscr for this, since it's otherwise unoccupied */
	clear();
	refresh();
	endwin();
    }
}

static void
curses_resume(const struct sessmgr_sw *sm)
{
    extern int Numrows, Numcols;

    if (!--Suspense)
    {
#ifdef RAW_SESSMGR
	set_curterm(my_term);
#endif
	clearok(((struct curses_data *) curscreen)->win, TRUE);
	wrefresh(((struct curses_data *) curscreen)->win);
	Numrows = LINES;
	Numcols = COLS;
    }
}

/*
 * Options supported:
 *
 * tty, ansi
 *	Select old glass-tty emulation or new ANSI X3.64 emulation
 *
 * bold
 *	Specify style of highlight used
 *
 * 8bit
 *      Pass ascii codes > 127 on output
 */

static char *
curses_opts(const struct sessmgr_sw *sm, void *sp, char *opts)
{
    struct curses_data *cp;
    static char buf[1024];
    char *xp, *ep;
    size_t l;

    cp = (struct curses_data *) sp;
    if (!opts)
    {
	/* return printable version of options */
	buf[0] = '\0';
	if (cp->flags & C_TTY)
	    strcpy(buf, "tty");
	if ((cp->flags & (C_TTY|C_BOLD)) == (C_TTY|C_BOLD))
	    strcat(buf, ",");
	if (cp->flags & C_BOLD)
	    strcat(buf, "bold");
	if (cp->flags & C_8BIT) {
	    if(strlen(buf)) strcat(buf, ",");
	    strcat(buf, "8bit");
	}

	return buf;
    }
    /* parse and set options */
    xp = opts;
    while (*xp)
    {
	while (isspace(*xp) || *xp == ',')
	    xp++;
	if (!*xp)
	    break;
	l = 0;
	for (ep = xp; *ep && *ep != ',' && !isspace(*ep); ep++)
	    l++;
	memcpy(buf, xp, l);
	buf[l] = '\0';
	if (strcasecmp(buf, "tty") == 0)
	    cp->flags |= C_TTY;
	else if (strcasecmp(buf, "ansi") == 0)
	    cp->flags &= ~C_TTY;
	else if (strcasecmp(buf, "notty") == 0)
	    cp->flags &= ~C_TTY;
	else if (strcasecmp(buf, "noansi") == 0)
	    cp->flags |= C_TTY;
	else if (strcasecmp(buf, "bold") == 0)
	    cp->flags |= C_BOLD;
	else if (strcasecmp(buf, "nobold") == 0)
	    cp->flags &= ~C_BOLD;
	else if (strcasecmp(buf, "8bit") == 0)
	    cp->flags |= C_8BIT;
	else if (strcasecmp(buf, "no8bit") == 0)
	    cp->flags &= ~C_8BIT;
	else
	    tprintf("curses: unknown option \"%s\"\n", buf);
	xp = ep;
    }
	
    return 0;
}

static int
curses_swap(const struct sessmgr_sw *sm, void *old, void *new)
{
    if (new)
    {
	clearok(((struct curses_data *) new)->win, TRUE);
	touchwin(((struct curses_data *) new)->win);
	curscreen = new;
    }
    return 1;			/* can always run concurrent output */
}

static void
wiach(WINDOW *win, chtype ch, int insert)
{
    if (insert)
	winsch(win, (chtype) ch);
    else
	waddch(win, (chtype) ch);
}

static void
curses_putch(const struct sessmgr_sw *sm, void *dp, int c)
{
    register struct curses_data *sp;
    int x, y, ox, oy;
    chtype attrs;

    sp = dp;
    if (c == 7)
    {
	write(1, "\7", 1);
	return;
    }
    if (c == '\r' || c == '\b')
    {
	waddch(sp->win, (chtype) c);
	return;
    }
    if (c == '\t')
    {
	wiach(sp->win, (chtype) c, sp->flags & C_INS);
	return;
    }
    if (c == '\n')
    {
	/* waddch() does clrtoeol() implicitly.  This breaks us. */
	getyx(sp->win, y, x);
	if (++y > sp->win->_bmarg)
	{
	    y--;
	    scroll(sp->win);
	}
	wmove(sp->win, y, 0);
	return;
    }
    if (sp->flags & C_TTY)
    {
	if (c >= 32 && (c < 127 || sp->flags&C_8BIT))
	    wiach(sp->win, (chtype) c, sp->flags & C_INS);
	return;
    }
    if (c == 24 || c == 26)
    {
	sp->flags &= ~(C_ESC|C_CSI);
	return;
    }
    if (c == 27)
    {
	sp->flags |= C_ESC;
	return;
    }
    if (c == '\016')
    {
	wattron(sp->win, A_ALTCHARSET);
	return;
    }
    if (c == '\017')
    {
	wattroff(sp->win, A_ALTCHARSET);
	return;
    }
    if (c < 32)
	return;
    if (!(sp->flags & (C_ESC|C_CSI)))
    {
	if (c > 127 && sp->flags & C_8BIT)
	    wiach(sp->win, (chtype) c, sp->flags & C_INS);
	else if (c > 127 && ibm_map[c - 128])
	    wiach(sp->win, ibm_map[c - 128] | A_ALTCHARSET, sp->flags & C_INS);
	else if (c < 127)
	    wiach(sp->win, (chtype) c, sp->flags & C_INS);
	return;
    }
    if (sp->flags & C_ESC)
    {
	/* we only handle '[' */
	switch (c)
	{
	case '[':
	    sp->flags &= ~C_ESC;
	    sp->flags |= C_CSI;
	    sp->nparm = 0;
	    memset(sp->parm, 0, sizeof sp->parm);
	    break;
	case '7':
	    getyx(sp->win, sp->save.y, sp->save.x);
	    sp->save.attr = getattrs(sp->win);
	    sp->save.tmarg = sp->win->_tmarg;
	    sp->save.bmarg = sp->win->_bmarg;
	    sp->save.bkgd = sp->win->_bkgd; /* should be a macro for this */
	    sp->save.valid = 1;
	    sp->flags &= ~(C_ESC|C_CSI);
	    break;
	case '8':
	    if (sp->save.valid)
	    {
		wmove(sp->win, sp->save.y, sp->save.x);
		wattrset(sp->win, sp->save.attr);
		wbkgdset(sp->win, sp->save.bkgd);
#ifdef linux
		werase(sp->win); /* wbkgdset() doesn't do anything */
#endif
		wsetscrreg(sp->win, sp->save.tmarg, sp->save.bmarg);
		sp->save.valid = 0;
	    }
	    sp->flags &= ~(C_ESC|C_CSI);
	    break;
	default:
	    sp->flags &= ~(C_ESC|C_CSI);
	    break;
	}
	return;
    }
    /* handle CSI-sequences */
    if(c < '0' || c > '9')  /* end of, or missing, numeric parameter? */
        sp->nparm++;
    switch (c)
    {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
	sp->parm[(int)sp->nparm] = sp->parm[(int)sp->nparm] * 10 + c - '0';
	break;
    case ';':
	sp->nparm++;
	break;
    case '?':
	/* just ignore it for now */
	break;
    /* action character */
    case 'H':
    case 'f':
        if(sp->parm[0] == 0) sp->parm[0]=1;
        if(sp->parm[1] == 0) sp->parm[1]=1;
	wmove(sp->win, sp->parm[1] - 1, sp->parm[0] - 1);
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case 'J':
	switch (sp->parm[0])
	{
	case 0:
	    attrs = getattrs(sp->win);
	    wattrset(sp->win, A_NORMAL);
	    wclrtobot(sp->win);
	    wattrset(sp->win, attrs);
	    break;
	case 1:
	    getyx(sp->win, oy, ox);
	    attrs = getattrs(sp->win);
	    wattrset(sp->win, A_NORMAL);
	    for (y = 0; y < oy; y++)
	    {
		wmove(sp->win, y, 0);
		for (x = Getmaxx(sp->win); x--; )
		    waddch(sp->win, ' ');
	    }
	    wmove(sp->win, oy, 0);
	    for (x = 0; x < ox; x++)
		waddch(sp->win, ' ');
	    wattrset(sp->win, attrs);
	    break;
	case 2:
	    wclear(sp->win);
	    break;
	}
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case 'K':
	switch (sp->parm[0])
	{
	case 0:
	    clrtoeol();
	    break;
	case 1:
	    getyx(sp->win, oy, ox);
	    wmove(sp->win, oy, 0);
	    attrs = getattrs(sp->win);
	    wattrset(sp->win, A_NORMAL);
	    for (x = 0; x < ox; x++)
		waddch(sp->win, ' ');
	    wattrset(sp->win, attrs);
	    break;
	case 2:
	    getyx(sp->win, oy, ox);
	    wmove(sp->win, oy, 0);
	    attrs = getattrs(sp->win);
	    wattrset(sp->win, A_NORMAL);
	    for (x = Getmaxx(sp->win); x--; )
		waddch(sp->win, ' ');
	    wmove(sp->win, oy, ox);
	    wattrset(sp->win, attrs);
	    break;
	}
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case 'm':
	sp->flags &= ~(C_ESC|C_CSI);
	for (x = 0; x < sp->nparm; x++)
	{
	    switch (sp->parm[x])
	    {
	    int temp;

	    case 0:
		wattroff(sp->win, A_BOLD|A_DIM|A_UNDERLINE|A_BLINK|A_REVERSE);
		break;
	    case 1:
		wattron(sp->win, A_BOLD);
		break;
	    case 2:
		wattron(sp->win, A_DIM);
		break;
	    case 4:
		wattron(sp->win, A_UNDERLINE);
		break;
	    case 5:
		wattron(sp->win, A_BLINK);
		break;
	    case 7:
		wattron(sp->win, A_REVERSE);
		break;
	    case 21:
		wattroff(sp->win, A_BOLD);
		break;
	    case 22:
		wattroff(sp->win, A_DIM);
		break;
	    case 24:
		wattroff(sp->win, A_UNDERLINE);
		break;
	    case 25:
		wattroff(sp->win, A_BLINK);
		break;
	    case 27:
		wattroff(sp->win, A_REVERSE);
		break;
	    /* ANSI color sequences */
#ifdef COLOR_SUPPORT
		/* Let's assume sp->{fg,bg} reflect the initial window colors.
		   This is true for the main window...which is where we expect to get CSI color sequences.
		   If we don't change MainColors, our changes here will not
		   persist after the next call to textattr()...a problem??
		   */
	    case 30:
	    case 31:
	    case 32:
	    case 33:
	    case 34:
	    case 35:
	    case 36:
	    case 37:
	      sp->fg = sp->parm[x] - 30;

	/* note : flow through */
	    case 40:
	    case 41:
	    case 42:
	    case 43:
	    case 44:
	    case 45:
	    case 46:
	    case 47:
	    	if ((sp->parm[x]) > 39)	/* replaces GOTO csi_new_color */
	      		sp->bg = sp->parm[x] - 40;

	      temp = map_colors(sp->bg, sp->fg);
	      wattron(((struct curses_data *) dp)->win, temp);
	      wbkgdset(((struct curses_data *) dp)->win, temp);

#ifdef SGR_DEBUG
log(-1,"bg,fg,attr=%d,%d,%x",sp->bg, sp->fg, temp);
#endif
              break;
#endif /* COLOR_SUPPORT */
	    }
#ifdef SGR_DEBUG
log(-1,"SGR code %d processed", sp->parm[x]);
#endif
	}
	break;
    case 'A':
	getyx(sp->win, y, x);
	if ((y -= (sp->parm[0]? sp->parm[0]: 1)) < 0)
	    y = 0;
	wmove(sp->win, y, x);
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case 'B':
	getyx(sp->win, y, x);
	if ((y += (sp->parm[0]? sp->parm[0]: 1)) >= Getmaxy(sp->win))
	    y = Getmaxy(sp->win) - 1;
	wmove(sp->win, y, x);
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case 'C':
	getyx(sp->win, y, x);
	if ((x += (sp->parm[0]? sp->parm[0]: 1)) >= Getmaxx(sp->win))
	    x = Getmaxx(sp->win) - 1;
	wmove(sp->win, y, x);
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case 'D':
	getyx(sp->win, y, x);
	if ((x -= (sp->parm[0]? sp->parm[0]: 1)) < 0)
	    x = 0;
	wmove(sp->win, y, x);
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case 'r':
	wsetscrreg(sp->win, (sp->parm[0]? sp->parm[0] - 1: 0),
		   (sp->nparm > 1? sp->parm[1] - 1: Getmaxy(sp->win) - 1));
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case 's':
	getyx(sp->win, sp->save.y, sp->save.x);
	sp->save.attr = getattrs(sp->win);
	sp->save.tmarg = sp->win->_tmarg;
	sp->save.bmarg = sp->win->_bmarg;
	sp->save.bkgd = sp->win->_bkgd; /* should be a macro for this */
	sp->save.valid = 1;
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case 'u':
	if (sp->save.valid)
	{
	    wmove(sp->win, sp->save.y, sp->save.x);
	    wattrset(sp->win, sp->save.attr);
	    wbkgdset(sp->win, sp->save.bkgd);
	    wsetscrreg(sp->win, sp->save.tmarg, sp->save.bmarg);
	    sp->save.valid = 0;
	}
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case 'L':
	if (!sp->parm[0])
	    sp->parm[0] = 1;
	while (sp->parm[0]--)
	    winsertln(sp->win);
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case 'M':
	if (!sp->parm[0])
	    sp->parm[0] = 1;
	while (sp->parm[0]--)
	    wdeleteln(sp->win);
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case '@':
	if (!sp->parm[0])
	    sp->parm[0] = 1;
	while (sp->parm[0]--)
	    winsch(sp->win, ' ');
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case 'P':
	if (!sp->parm[0])
	    sp->parm[0] = 1;
	while (sp->parm[0]--)
	    wdelch(sp->win);
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    case 'h':
    case 'l':
	for (x = 0; x < sp->nparm; x++)
	{
	    switch (sp->parm[x])
	    {
	    case 4:
		if (c == 'h')
		    sp->flags |= C_INS;
		else
		    sp->flags &= ~C_INS;
		break;
	    /* insert mode is all we handle right now */
	    }
	}
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    default:
	sp->flags &= ~(C_ESC|C_CSI);
	break;
    }
}

static void
curses_clreol(const struct sessmgr_sw *sm, void *dp)
{
    wclrtoeol(((struct curses_data *) dp)->win);
}

static void
curses_rflush(const struct sessmgr_sw *sm, void *dp)
{
    /* stub - later we'll update status here */
}

static void
curses_flush(const struct sessmgr_sw *sm, void *dp)
{
    if (!Suspense && curscreen == dp)
	wrefresh(((struct curses_data *) dp)->win);
}

static void *
curses_create(const struct sessmgr_sw *sm, struct session *sp)
{
    struct curses_data *ssp;
    int temp;

    ssp = mallocw(sizeof *ssp);
    ssp->win = newwin(LINES, COLS, 0, 0);
    scrollok(ssp->win, TRUE);
#ifdef COLOR_SUPPORT
    ssp->bg = dos2unixcolor[(MainColors>>4) & 0x07];
    ssp->fg = dos2unixcolor[MainColors & 0x07];
    temp = map_colors(ssp->bg, ssp->fg);
    wbkgdset(ssp->win, temp);
    wattrset(ssp->win, temp);  /* means no special char attributes in effect */
    werase(ssp->win);  /* so background color fills screen */
#endif
    ssp->flags = 0;
    ssp->save.valid = 0;
    curscreen = ssp;
    return ssp;
}

static void
curses_destroy(const struct sessmgr_sw *sm, void *dp)
{
    delwin(((struct curses_data *) dp)->win);
    j_free(dp);
}

static void
curses_clrscr(const struct sessmgr_sw *sm, void *dp)
{
    WINDOW *w;
    int l;

    w = ((struct curses_data *) dp)->win;
    for (l = w->_tmarg; l <= w->_bmarg; l++)  /* was l < w->_bmarg */
    {
	wmove(w, l, 0);
	wclrtoeol(w);
    }
    wmove(w, w->_tmarg, 0);
}

static int
curses_wherex(const struct sessmgr_sw *sm, void *dp)
{
    int x, y;

	UNUSED (y);	/* 16Apr2016, Maiko (VE4KLM), compiler warnings */

    getyx(((struct curses_data *) dp)->win, y, x);
    return x + 1;
}

static int
curses_wherey(const struct sessmgr_sw *sm, void *dp)
{
    WINDOW *w;
    int x, y;

	UNUSED (x);	/* 16Apr2016, Maiko (VE4KLM), compiler warnings */

    w = ((struct curses_data *) dp)->win;
    getyx(w, y, x);
    return y - w->_tmarg + 1;
}

static void
curses_window(const struct sessmgr_sw *sm, void *dp, int x1, int y1, int x2,
	      int y2)
{
    WINDOW *w;

    w = ((struct curses_data *) dp)->win;
    wsetscrreg(w, y1 - 1, y2 - 1);
    /* ttydriv() assumes the cursor is placed in this window somewhere */
    wmove(w, y1 - 1, 0);
}

static void
curses_gotoxy(const struct sessmgr_sw *sm, void *dp, int x, int y)
{
    WINDOW *w;

    w = ((struct curses_data *) dp)->win;
    wmove(w, y + w->_tmarg - 1, x - 1);
}

static void
curses_high(const struct sessmgr_sw *sm, void *dp)
{
    wattron(((struct curses_data *) dp)->win,
	    (((struct curses_data *) dp)->flags & C_BOLD)? A_BOLD: A_REVERSE);
}

static void
curses_norm(const struct sessmgr_sw *sm, void *dp)
{
    wattroff(((struct curses_data *) dp)->win,
	     (((struct curses_data *) dp)->flags & C_BOLD)? A_BOLD: A_REVERSE);
}

#ifdef COLOR_SUPPORT
void
curses_textattr(const struct sessmgr_sw *sm, void *dp, int fg_bg)
{
    int fg, bg, temp;

    if (fg_bg & BLINK) {
        wattron(((struct curses_data *) dp)->win,A_BLINK);
    } else {
        wattroff(((struct curses_data *) dp)->win,A_BLINK);
    }
    bg = (fg_bg >> 4) & 0x07;
    fg = fg_bg & 0x07;

    if (fg_bg & 0x08)  /* high-intensity fg */
        wattron(((struct curses_data *) dp)->win, A_BOLD);
    else
        wattroff(((struct curses_data *) dp)->win, A_BOLD);

    temp = map_colors(dos2unixcolor[bg], dos2unixcolor[fg]);
    wattron(((struct curses_data *) dp)->win, temp);
    wbkgdset(((struct curses_data *) dp)->win, temp);
}
#endif
#if 0

static int
color_name(char *color)
{
    if (strcasecmp(color, "black") == 0)
	return COLOR_BLACK;
    if (strcasecmp(color, "red") == 0)
	return COLOR_RED;
    if (strcasecmp(color, "yellow") == 0)
	return COLOR_YELLOW;
    if (strcasecmp(color, "green") == 0)
	return COLOR_GREEN;
    if (strcasecmp(color, "blue") == 0)
	return COLOR_BLUE;
    if (strcasecmp(color, "magenta") == 0)
	return COLOR_MAGENTA;
    if (strcasecmp(color, "cyan") == 0)
	return COLOR_CYAN;
    return COLOR_WHITE;
}

#endif

static void
curses_cursor(const struct sessmgr_sw *sm, void *dp, int c)
{
    curs_set(c);
}

#define	RECOVER_FROM_SCREEN_RESIZE	/* 29Jan2012, Maiko, NOS PANIC 'fix' */

static int kbchar (int ir)
{
	int save_errno, i;
	unsigned char ch;
	extern int Keyboard, Numrows, Numcols;

	if (ir) j2alarm (500);

	do
	{
		if (pwait (&Keyboard) != 0 && ir)
		return -1;
	}

	while ((i = (int)read (0, &ch, 1)) == 0 ||
		(i == -1 && errno == EWOULDBLOCK));

	save_errno = errno;
	j2alarm (0);

	if (i < 0)
	{

#ifdef	RECOVER_FROM_SCREEN_RESIZE

	/*
	 * 29Jan2012, Maiko, This might actually work, seems to do the trick so
	 * far. It would appear you have to call 'refresh ()' to get the updated
	 * values for LINES and COLS. If you try to get LINES and COLS before you
	 * call refresh (), you will get the old values from before the resize.
	 *
	 * JNOS should not have to die just because of a screen resize.
	 */
		log (-1, "keyboard interrupted, errno %d", save_errno);

		/* initially thought I would need this, but does not appear so
		 initscr ();
		*/

		refresh ();

		log (-1, "refresh () new LINES (%d) and COLS (%d)", LINES, COLS);

		Suspense = 0;
		Numrows = LINES;
		Numcols = COLS; 
		initted = 1;
#else
		tprintf("NOS PANIC: Lost keyboard (%d/%d)\n", i, errno);
		where_outta_here(1,248);
#endif

	}

	return ch;
}

/*
 * This remains incorrect.  The keyboard process must be recreated as part of
 * the session manager instead of being independent; this process must be
 * capable of distributing input from several sources, including the real
 * keyboard and external sessions.
 */

static int
curses_kbread(const struct sessmgr_sw *sm, void *dp)
{
    static int ungets[10];
    struct keytrie *t;
    static int unget;
    int ungetc[10];
    int c, i, u;

    i = 0;
    u = 0;
    if (unget > i)
	c = ungets[i++];
    else
	c = kbchar(0);
    ungetc[u++] = c;
    t = keys;
    while (t[c].kt_type == KT_TRIE || t[c].kt_type == KT_TVAL)
    {
	t = t[c].kt_trie;
	if (unget > i)
	    c = ungets[i++];
	else
	    c = kbchar(1);
	if (c == -1)
	    break;
	ungetc[u++] = c;
    }
    if (t[c].kt_type == KT_VAL)
    {
	u = 0;
	c = t[c].kt_val;
    }
    else if (t[c].kt_type == KT_TVAL)
    {
	u = 0;
	c = t[c].kt_tval;
    }
    while (i < unget)
	ungetc[u++] = ungets[i++];
    if (u)
    {
	c = ungetc[0];
	for (i = u; i; i--)
	    ungets[i - 1] = ungetc[i];
	unget = u - 1;
    }
    return c;
}

struct sessmgr_sw curses_sessmgr =
{
    "curses",
    SM_SPLIT|SM_STDIO,
    curses_init,
    (char *(*)__FARGS((const struct sessmgr_sw *, char *))) 0,
    curses_create,
    curses_opts,
    curses_swap,
    curses_putch,
    curses_clreol,
    curses_clrscr,
    curses_wherex,
    curses_wherey,
    curses_window,
    curses_gotoxy,
    curses_high,
    curses_norm,
    curses_cursor,
    curses_kbread,
    curses_destroy,
    0,
    curses_rflush,
    curses_flush,
    curses_suspend,
    curses_resume,
    curses_end,
	0
};

#endif
