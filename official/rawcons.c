/*
 * A "raw console" session manager.  This is sort of a high-speed version of
 * "curses" --- but uses the current terminal type, instead of either a dumb
 * terminal or ANSI X3.64 emulation.  It's a little smarter than "dumb", but
 * not much.
 */

#include <curses.h>
#include <term.h>
#undef FALSE
#undef TRUE
#include <sys/types.h>
#include <termios.h>
#include "unix.h"
#include "proc.h"
#include "socket.h"
#include "tty.h"
#include "sessmgr.h"
#undef tputs
#include "config.h"

#ifdef SM_RAW

/*
 * ought to share these... they're going to be identical between raw and curses
 */

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

static struct termios old_tty, new_tty;
static int Suspense, infoed;
static struct keytrie *keys;
static TERMINAL *my_term;

static void
key_init(void)
{
    int i;

    keys = mallocw(256 * sizeof *keys);
    for (i = 256; i--; )
    {
	keys[i].kt_type = KT_DEF;
	keys[i].kt_val = i;
    }
}

static void
key_add(const char *str, int val)
{
    const char *old;
    struct keytrie *t;
    int c, i;

    /*
     * Follow the trie until we get to the right subtrie for the string, then
     * add the value.  If we hit a KT_DEF, expand the trie.  If we hit a KT_VAL
     * change it to a KT_TVAL, which times out as a KT_VAL but continues as a
     * KT_TRIE.  If given a value for a KT_TRIE, change it to a KT_TVAL.
     */
    if (!str || !*str)
	return;			/* no key to define */
    old = str;
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
	    t[c].kt_trie = mallocw(256 * sizeof (struct keytrie));
	    for (i = 256; i--; )
	    {
		t[c].kt_trie[i].kt_type = KT_DEF;
		t[c].kt_trie[i].kt_val = i;
	    }
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

static int
raw_init(const struct sessmgr_sw *sm)
{
    extern int Numrows, Numcols;

    if (infoed)
	set_curterm(my_term);
    else
    {
	/*
	 * By rights we should use curses' setup, but then it becomes harder
	 * to configure either out.  Waste a little memory for now; it's not
	 * going to be much more than the size of the terminfo file, which
	 * is itself rarely more than 1.5K.
	 */
	setupterm(0, 1, 0);
	my_term = cur_term;
	/* load keytrie */
	key_init();
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
	infoed = 1;
    }
    if (tcgetattr(0, &old_tty) == -1)
	return 0;
    new_tty = old_tty;
    new_tty.c_lflag &= ~(ISIG|ICANON|ECHO|ECHOE|ECHOK|ECHONL);
    new_tty.c_cc[VMIN] = 1;
    new_tty.c_cc[VTIME] = 0;
    tcsetattr(0, TCSADRAIN, &new_tty);
    Numrows = lines;
    Numcols = columns;
    putp(enter_ca_mode);
    return 1;
}

static void
raw_end(const struct sessmgr_sw *sm)
{
    if (!Suspense)
    {
	putp(exit_ca_mode);
	tcsetattr(0, TCSADRAIN, &old_tty);
    }
}

static void
raw_suspend(const struct sessmgr_sw *sm)
{
    if (!Suspense++)
    {
	putp(exit_ca_mode);
	fflush(stdout);
	tcsetattr(0, TCSADRAIN, &old_tty);
    }
}

static void
raw_resume(const struct sessmgr_sw *sm)
{
    extern int Numrows, Numcols;

    if (!--Suspense)
    {
	tcsetattr(0, TCSADRAIN, &new_tty);
	set_curterm(my_term);
	Numrows = lines;
	Numcols = columns;
	putp(enter_ca_mode);
    }
}

static int
raw_swap(const struct sessmgr_sw *sm, void *old, void *new)
{
    int c;

    if (old && new)
    {
	c = (int) new - 1;
	putp(clear_screen);
	printf(">>> switching to session %d", c);
	if (Sessions[c].name && *Sessions[c].name)
	    printf(": %s", Sessions[c].name);
	else if (Sessions[c].proc->name && *Sessions[c].proc->name)
	    printf(" [%s]", Sessions[c].proc->name);
	printf(" (%s)\n", Sestypes[Sessions[c].type]);
    }
    return 0;			/* only the current session can do output */
}

static void
raw_putch(const struct sessmgr_sw *sm, void *dp, int c)
{
    putchar(c);
}

static void
raw_clreol(const struct sessmgr_sw *sm, void *dp)
{
    putp(clr_eol);
}

static void
raw_rflush(const struct sessmgr_sw *sm, void *dp)
{
}

static void
raw_flush(const struct sessmgr_sw *sm, void *dp)
{
    if (!Suspense)
	fflush(stdout);
}

static void *
raw_create(const struct sessmgr_sw *sm, struct session *sp)
{
    return (void *) (sp - Sessions + 1);
}

static void
raw_destroy(const struct sessmgr_sw *sm, void *dp)
{
}

static void
raw_clrscr(const struct sessmgr_sw *sm, void *dp)
{
    putp(clear_screen);
}

static int
raw_wherex(const struct sessmgr_sw *sm, void *dp)
{
    return -1;
}

static int
raw_wherey(const struct sessmgr_sw *sm, void *dp)
{
    return -1;
}

static void
raw_window(const struct sessmgr_sw *sm, void *dp, int x1, int y1, int x2,
	      int y2)
{
}

static void
raw_gotoxy(const struct sessmgr_sw *sm, void *dp, int x, int y)
{
    putp(tparm(cursor_address, y - 1, x - 1));
}

static void
raw_high(const struct sessmgr_sw *sm, void *dp)
{
    vidattr(A_REVERSE);
}

static void
raw_norm(const struct sessmgr_sw *sm, void *dp)
{
    vidattr(A_NORMAL);
}

static void
raw_cursor(const struct sessmgr_sw *sm, void *dp, int c)
{
    switch (c)
    {
    case _NOCURSOR:
	putp(cursor_invisible);
	break;
    case _NORMALCURSOR:
	putp(cursor_normal);
	break;
    case _SOLIDCURSOR:
	putp(cursor_visible);
	break;
    }
}

/*
 * Same as for curses... probably ought to share them
 */

static int
kbchar(int ir)
{
    extern int Keyboard;
    unsigned char ch;
    int i;

    if (ir)
	j2alarm(500);
    do
    {
	if (pwait(&Keyboard) != 0 && ir)
	    return -1;
    }
    while ((i = read(0, &ch, 1)) == 0 || (i == -1 && errno == EWOULDBLOCK));
    j2alarm(0);
    if (i < 0)
    {
	tprintf("NOS PANIC: Lost keyboard\n");
	where_outta_here(1,248);
    }
    return ch;
}

static int
raw_kbread(const struct sessmgr_sw *sm, void *dp)
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

struct sessmgr_sw raw_sessmgr =
{
    "raw",
    SM_STDIO,
    raw_init,
    (char *(*)__FARGS((const struct sessmgr_sw *, char *))) 0,
    raw_create,
    (char *(*)__FARGS((const struct sessmgr_sw *, void *, char *))) 0,
    raw_swap,
    raw_putch,
    raw_clreol,
    raw_clrscr,
    raw_wherex,
    raw_wherey,
    raw_window,
    raw_gotoxy,
    raw_high,
    raw_norm,
    raw_cursor,
    raw_kbread,
    raw_destroy,
    0,
    raw_rflush,
    raw_flush,
    raw_suspend,
    raw_resume,
    raw_end,
};

#endif
