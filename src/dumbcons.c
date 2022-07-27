/*
 * A "dumb console" session manager.  This is intended for use when stdin/out
 * aren't ttys.  But stdin must still be usable for input.
 */

#include <stdio.h>
#include <sys/types.h>
#include <termios.h>
#include "config.h"
#include "unix.h"
#include "proc.h"
#include "socket.h"
#include "tty.h"
#include "sessmgr.h"

#ifdef SM_DUMB

static struct termios old_tty, new_tty;
static int Suspense, ttyed;

static int
dumb_init(const struct sessmgr_sw *sm)
{
    extern int Numrows, Numcols;

    if ((ttyed = (tcgetattr(0, &old_tty) != -1)))
    {
	new_tty = old_tty;
	new_tty.c_lflag &= ~(ISIG|ICANON|ECHO|ECHOE|ECHOK|ECHONL);
	new_tty.c_cc[VMIN] = 1;
	new_tty.c_cc[VTIME] = 0;
	tcsetattr(0, TCSADRAIN, &new_tty);
    }
    /* suppress flow control */
    Numrows = 0;
    Numcols = 0;
    return 1;
}

static void
dumb_end(const struct sessmgr_sw *sm)
{
    if (ttyed && !Suspense)
	tcsetattr(0, TCSADRAIN, &old_tty);
}

static void
dumb_suspend(const struct sessmgr_sw *sm)
{
    if (!Suspense++)
    {
	fflush(stdout);
	if (ttyed)
	    tcsetattr(0, TCSADRAIN, &old_tty);
    }
}

static void
dumb_resume(const struct sessmgr_sw *sm)
{
    extern int Numrows, Numcols;

    if (!--Suspense)
    {
	if (ttyed)
	    tcsetattr(0, TCSADRAIN, &new_tty);
	Numrows = 0;
	Numcols = 0;
    }
}

static int
dumb_swap(const struct sessmgr_sw *sm, void *old, void *new)
{
    int c;

    if (old && new)
    {
	c = (int) new - 1;
	printf("\n>>> switching to session %d", c);
	if (Sessions[c].name && *Sessions[c].name)
	    printf(": %s", Sessions[c].name);
	else if (Sessions[c].proc->name && *Sessions[c].proc->name)
	    printf(" [%s]", Sessions[c].proc->name);
	printf(" (%s)\n", Sestypes[Sessions[c].type]);
    }
    return 0;			/* only the current session can do output */
}

static void
dumb_putch(const struct sessmgr_sw *sm, void *dp, int c)
{
    putchar(c);
}

static void
dumb_clreol(const struct sessmgr_sw *sm, void *dp)
{
}

static void
dumb_rflush(const struct sessmgr_sw *sm, void *dp)
{
}

static void
dumb_flush(const struct sessmgr_sw *sm, void *dp)
{
    if (!Suspense)
	fflush(stdout);
}

static void *
dumb_create(const struct sessmgr_sw *sm, struct session *sp)
{
    return (void *) (sp - Sessions + 1);
}

static void
dumb_destroy(const struct sessmgr_sw *sm, void *dp)
{
}

static void
dumb_clrscr(const struct sessmgr_sw *sm, void *dp)
{
}

static int
dumb_wherex(const struct sessmgr_sw *sm, void *dp)
{
    return -1;
}

static int
dumb_wherey(const struct sessmgr_sw *sm, void *dp)
{
    return -1;
}

static void
dumb_window(const struct sessmgr_sw *sm, void *dp, int x1, int y1, int x2,
	      int y2)
{
}

static void
dumb_gotoxy(const struct sessmgr_sw *sm, void *dp, int x, int y)
{
}

static void
dumb_high(const struct sessmgr_sw *sm, void *dp)
{
}

static void
dumb_norm(const struct sessmgr_sw *sm, void *dp)
{
}

static void
dumb_cursor(const struct sessmgr_sw *sm, void *dp, int c)
{
}

/*
 * We define two special characters:  ^C is F10, ^T is F9.  Everything else is
 * passed literally.
 */

static int
dumb_kbread(const struct sessmgr_sw *sm, void *dp)
{
    extern int Keyboard;
    unsigned char ch;
    int i;

    do
    {
	pwait(&Keyboard);
    }
    while ((i = read(0, &ch, 1)) == 0 || (i == -1 && errno == EWOULDBLOCK));
    if (i < 0)
    {
	tprintf("NOS PANIC: Lost keyboard\n");
	where_outta_here(1,248);
    }
    if ((i = ch) == 3)
	i = -2;
    else if (i == 20)
	i = -11;
    else if (i == 127)		/* for when the backspace key sends DEL */
	i = 8;
    return i;
}

struct sessmgr_sw dumb_sessmgr =
{
    "dumb",
    SM_STDIO,
    dumb_init,
    (char *(*)__FARGS((const struct sessmgr_sw *, char *))) 0,
    dumb_create,
    (char *(*)__FARGS((const struct sessmgr_sw *, void *, char *))) 0,
    dumb_swap,
    dumb_putch,
    dumb_clreol,
    dumb_clrscr,
    dumb_wherex,
    dumb_wherey,
    dumb_window,
    dumb_gotoxy,
    dumb_high,
    dumb_norm,
    dumb_cursor,
    dumb_kbread,
    dumb_destroy,
    0,
    dumb_rflush,
    dumb_flush,
    dumb_suspend,
    dumb_resume,
    dumb_end,
};

#endif
