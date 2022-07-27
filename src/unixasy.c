/*
 * File stolen from WAMPES 921229, modified for compatibility with JNOS and my
 * tastes, and left to sink or swim.  Blub!  ++bsa
 *
 * The actual structure is much closer to that of JNOS than to WAMPES.  The
 * reason is that WAMPES uses these weirdball I/O hooks... We will use the
 * "classic" interface, modified by the use of register_fd().  (Actually, the
 * weirdball I/O hooks are just WAMPES's version of register_fd().  The API
 * for WAMPES is a heck of a lot hairier, though.)
 */

#include <sys/types.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
/* screwball ifdefs --- glibc changed library version ifdefs midstream */
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 0)
#include <sys/ioctl.h>
#include <termios.h>
#else
#if defined(linux) && (__GNU_LIBRARY__  >  1)
/* RH5.0 needs more to define TIOCMGET and TIOCM_CAR: */
#include <asm/termios.h>
#include <ioctls.h>
#else
#include <termios.h>
#endif
#endif
#include <sys/time.h>
#include <errno.h>
#include <signal.h>

#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#include "asy.h"
#include "timer.h"
#include "unixasy.h"
#include "unix.h"
#include "devparam.h"
#include "commands.h"
#include "cmdparse.h"
#ifdef POLLEDKISS
#include "kisspoll.h"
#endif

/* Some Unix systems have a different lockfile and device path format: */
#if defined(SVR4) || defined(__svr4__) || defined (__SVR4) || defined(solaris)
#undef SVR4
#define SVR4
#define LFDEVNO
#endif

#ifdef LFDEVNO
#include <sys/stat.h>			/* For major() & minor() macros. */
					/* Should be in <sys/types.h>. */
#ifndef major				/* If we didn't find it */
#ifdef SVR4				/* then for Sys V R4 */
#include <sys/mkdev.h>			/* look here */
#else					/* or for Sunos versions */
#ifdef SUNOS4				/* ... */
#include <sys/sysmacros.h>		/* look here */
#else					/* Otherwise take a chance: */
#define	major(dev) ( (int) ( ((unsigned)(dev) >> 8) & 0xff))
#define	minor(dev) ( (int) ( (dev) & 0xff))
#endif /* SUNOS4 */
#endif /* SVR4 */
#endif /* major */
#endif /* LFDEVNO */

#ifndef LOCKDIR
/* #define LOCKDIR "/usr/spool/uucp"  =or=  "/var/spool/locks" */
#define LOCKDIR "/var/lock"
#endif

#ifndef sun
extern int chmod __ARGS((const char *, unsigned));
#endif

static int find_speed __ARGS((long speed));
static void pasy __ARGS((struct asy *asyp));
static void asy_tx __ARGS((int, void *, void *));
static void asy_monitor __ARGS((int, void *, void *));
static void asy_input __ARGS((int, void *, void *));
static int asy_vmin __ARGS((int dev, int32 pktsize));
static int asy_rts __ARGS((int dev, int32 onoff));

struct asy Asy[ASY_MAX];

/*---------------------------------------------------------------------------*/

static int dorxqueue __ARGS((int argc, char **argv, void *p));
static int dotxqueue __ARGS((int argc, char **argv, void *p));
static int dobufsize __ARGS((int argc, char **argv, void *p));
static int doasy2stat __ARGS((int argc, char **argv, void *p));

static struct cmds AsyPcmds[] =
{
    { "rxqueue",	dorxqueue,	0, 0, NULLCHAR },
    { "txqueue",	dotxqueue,	0, 0, NULLCHAR },
    { "bufsize",	dobufsize,	0, 0, NULLCHAR },
    { "status",	doasy2stat,	0, 0, NULLCHAR },
    { 0,	NULL,	0,	0, NULLCHAR }
};

/*---------------------------------------------------------------------------*/

static struct {
	long speed;
	speed_t flags;
} speed_table[] = {
#ifdef B0
	{ 0,  B0 },
#endif
#ifdef B50
	{ 50, B50 },
#endif
#ifdef B75
	{ 75, B75 },
#endif
#ifdef B110
	{ 110, B110 },
#endif
#ifdef B134
	{ 134, B134 },
#endif
#ifdef B150
	{ 150, B150 },
#endif
#ifdef B200
	{ 200, B200 },
#endif
#ifdef B300
	{ 300, B300 },
#endif
#ifdef B600
	{ 600, B600 },
#endif
#ifdef B900
	{ 900, B900 },
#endif
#ifdef B1200
	{ 1200, B1200 },
#endif
#ifdef B1800
	{ 1800, B1800 },
#endif
#ifdef B2400
	{ 2400, B2400 },
#endif
#ifdef B3600
	{ 3600, B3600 },
#endif
#ifdef B4800
	{ 4800, B4800 },
#endif
#ifdef B7200
	{ 7200, B7200 },
#endif
#ifdef B9600
	{ 9600, B9600 },
#endif
#ifdef B19200
	{ 19200, B19200 },
#endif
#ifdef B38400
	{ 38400, B38400 },
#endif
#ifdef B57600
	{ 57600, B57600 },
#endif
#ifdef B115200
	{ 115200, B115200 },
#endif
#ifdef B230400
	{ 230400, B230400 },
#endif
#ifdef B460800
	{ 460800, B460800 },
#endif
	{ -1, 0 }
};

/*---------------------------------------------------------------------------*/

static int find_speed (long speed)
{
	int i;

	i = 0;
	while (speed_table[i].speed < speed && speed_table[i+1].speed > 0)
		i++;
	return i;
}

/*---------------------------------------------------------------------------*/

/* 01Dec2004, Maiko, New function replaces 'Fail:' GOTO and labels */
static int do_Fail (int fd, struct asy *ap)
{
#ifndef HEADLESS
    rflush();		/* make sure the message gets out */
#endif
    if (fd != -1)
		close(fd);
    /* Unlock port */
    if (ap->uulock[0])
		unlink(ap->uulock);
    ap->uulock[0] = '\0';
    ap->iface = NULLIF;
    return -1;
}

/* 01Dec2004, Maiko, New function replaces 'lock_failed:' GOTO and labels */
static int do_lock_failed (int fd, struct asy *ap, char *arg1)
{
	tprintf("Can't lock /dev/%s (%s): %s\n",
		arg1,ap->uulock,strerror(errno));
	ap->uulock[0] = '\0';
	return (do_Fail (fd, ap));
}

/* Initialize asynch port "dev" */
int
asy_init(dev,ifp,arg1,arg2,bufsize,trigchar,monitor,speed,force,triglevel,polled)
    int dev;
    struct iface *ifp;
    char *arg1,*arg2;       /* Attach args for address and vector */
    int16 bufsize;
    int trigchar;
    char monitor;
    long speed;
    int force;
    int triglevel;
    int polled;
{
    register struct asy *ap;
    char filename[80];
    char *ifn;
    int len, sp, fd = -1; /* should initialize this, or close(fd) might crash */
    struct termios termios;
#ifdef LFDEVNO
    struct stat devbuf;		/* For device numbers (SVR4). */
#endif /* LFDEVNO */

    ap = &Asy[dev];

    /* UUCP locking with ASCII pid */
#ifdef LFDEVNO				/* Lockfilename has device numbers. */
    ap->uulock[0] = '\0';
    strcpy(filename, "/dev/");
    strcat(filename, arg1);

    if (stat(filename,&devbuf) < 0)
		return (do_lock_failed (fd, ap, arg1));

    sprintf(ap->uulock,"%s/LK.%03d.%03d.%03d", LOCKDIR,
        major(devbuf.st_dev),	/* inode */
        major(devbuf.st_rdev),	/* major device number */
        minor(devbuf.st_rdev));	/* minor device number */
#else					/* Others... */
    if ((ifn=strrchr(arg1,'/')) != NULLCHAR)  /* cua/a  ?? */
        sprintf(ap->uulock,"%s/LCK..%.3s%s", LOCKDIR, arg1, ++ifn);
    else
        sprintf(ap->uulock,"%s/LCK..%s", LOCKDIR, arg1);
#endif
    for (;;)
    {
		if ((fd = open(ap->uulock, O_WRONLY|O_CREAT|O_EXCL, 0644)) != -1)
			break;
		if (errno != EEXIST)
	    	break;

		/* read pid, unlink and retry if proc no longer exists */
		if ((fd = open(ap->uulock, O_RDONLY)) == -1)
	    	continue;	/* timing is everything */

		/* 08Oct2009, Maiko, What happens if read returns -1 ? Fixed !!! */
		len = (int)read(fd, filename, 10);
		if (len < 1) len = 0;
		filename[len] = '\0';

		close(fd);
		sscanf(filename, "%d", &fd);
		if (kill(fd, 0) == -1 && errno == ESRCH)
		{
	    	tprintf("Removing stale lockfile for %s\n", arg1);
	    	unlink(ap->uulock);
	    	continue;
		}
		tprintf("/dev/%s is locked\n", arg1);
		ap->uulock[0] = '\0'; /* so it won't clobber existing lock */
		return (do_Fail (fd, ap));
    }

    if (fd == -1)
		return (do_lock_failed (fd, ap, arg1));

    chmod(ap->uulock, 0644); /* beware of overly restrictive umask */
    sprintf(filename, "%10d\n", getpid());
    write(fd, filename, 11);
    close(fd);

    strcpy(filename, "/dev/");
    strcat(filename, arg1);
    if ((fd = open(filename, O_RDWR|O_NONBLOCK|O_NOCTTY, 0644)) == -1)
    {
		tprintf("Can't open port: %s\n", strerror(errno));
		return (do_Fail (fd, ap));
    }

#ifdef POLLEDKISS
    ap->poller = NULL;
#endif
    ap->iface = ifp;
    sp = find_speed(speed);
    ap->speed = speed_table[sp].speed;
    memset((char *) &termios, 0, sizeof(termios));
    termios.c_iflag = IGNBRK|IGNPAR;
    termios.c_cflag = CS8|CREAD|speed_table[sp].flags;
    if(!(ap->flags & ASY_CARR))  /* does device assert DCD? */
        termios.c_cflag |= CLOCAL;  /* no; pretend it is always there */
    termios.c_cc[VTIME] = 2;
    if ((ap->pktsize = triglevel & 255))
	termios.c_cc[VMIN] = triglevel & 255;
    if (cfsetispeed(&termios, speed_table[sp].flags) == -1)
    {
	tprintf("Can't set speed: %s\n", strerror(errno));
	return (do_Fail (fd, ap));
    }
    if (cfsetospeed(&termios, speed_table[sp].flags) == -1)
    {
	tprintf("Can't set speed: %s\n", strerror(errno));
	return (do_Fail (fd, ap));
    }
    if (tcsetattr(fd, TCSANOW, &termios) == -1)
    {
	tprintf("Can't configure port: %s\n", strerror(errno));
	return (do_Fail (fd, ap));
    }
    /* security: port won't work until re-opened */
    if ((ap->fd = open(filename, O_RDWR|O_NONBLOCK|O_NOCTTY, 0644)) == -1)
    {
	tprintf("Can't reopen port: %s\n", strerror(errno));
	return (do_Fail (fd, ap));
    }
    close(fd);

#ifdef NRS
    if(ap->nrs_cts) { /* WZ2B */
        int ctlbits = TIOCM_RTS;  /* assert RTS asap */
        ioctl(ap->fd,TIOCMBIS,&ctlbits);
    }
#endif
    /* new asy parameters, defaulted for now */
    ap->rxq = 1;		/* disable queueing */
    ap->txq = 1;
    ap->rxbuf = bufsize;

    /* clear statistics */
    ap->rxints = 0;
    ap->txints = 0;
    ap->rxchar = 0;
    ap->txchar = 0;
    ap->rxput = 0;
    ap->rxovq = 0;
    ap->rxblock = 0;
    ap->txget = 0;
    ap->txovq = 0;
    ap->txblock = 0;

    ifp->txproc = newproc(ifn = if_name(ifp," tx"),
			  256, asy_tx, dev, ifp, NULL, 0);
    free(ifn);
    ap->rxproc = newproc(ifn = if_name(ifp, " hw"),
			 256, asy_input, dev, ifp, NULL, 0);
    free(ifn);

    if(monitor && (ap->flags&ASY_CARR)) { /* does device assert DCD? */
        ap->monitor = newproc(ifn = if_name(ifp, " monitor"),
			 350, asy_monitor, dev, ifp, NULL, 0);
        free(ifn);
        pwait(NULL);  /* ?? needed to let asy_monitor() start up?? */
    }
    else
#ifdef NRS
        if (! ap->nrs_cts)
#endif
	  if (!ioctl(ap->fd, TIOCMGET, &sp) && (sp&(TIOCM_CAR|TIOCM_DTR))==(TIOCM_CAR|TIOCM_DTR))
              if (ifp->iostatus) (*ifp->iostatus)(ifp, PARAM_UP, 0);

#ifdef POLLEDKISS
    if (polled) {  /* added by n5knx from i8250.c ... */
        /* Calculate the poll interval: some processing time +
         * the packet duration for a mtu size packet.
         */
        long interval = (((long)ifp->mtu * 10000L) / speed);
        ap->poller = newproc(ifn=if_name(ifp," poller"),
            384,kiss_poller,ifp->xdev,(void *)interval,NULL,0);
        free(ifn);
    }
        
#endif

    register_io(ap->fd, &ap->fd);

    return 0;

  /* 25Oct2009, Maiko, What's all this stuff below ???? */

    rflush();		/* make sure the message gets out */
    if (fd != -1)
	close(fd);
    /* Unlock port */
    if (ap->uulock[0])
	unlink(ap->uulock);
    ap->uulock[0] = '\0';
    ap->iface = NULLIF;
    return -1;
}

/*---------------------------------------------------------------------------*/

int
asy_stop(ifp)
struct iface *ifp;
{
	register struct asy *ap;

	ap = &Asy[ifp->dev];

	if(ap->iface == NULLIF)
		return -1;      /* Not allocated */

	unregister_io(ap->fd);

#ifdef POLLEDKISS
        if(ap->poller)
            killproc(ap->poller);
        ap->poller = 0;
#endif

        if(ap->monitor) {
            killproc(ap->monitor);
            ap->monitor = 0;
        }

	if (ifp->txproc)
	    killproc(ifp->txproc);
	ifp->txproc = 0;

	if (ap->rxproc)
	    killproc(ap->rxproc);
	ap->rxproc = 0;

	ap->iface = NULLIF;

	free_q(&ap->sndq);
	close(ap->fd);

	free_q(&ap->rcvq);

	if (ap->uulock[0])
	    unlink(ap->uulock);
	ap->uulock[0] = '\0';

	return 0;
}

void
detach_all_asy()
{
    register struct asy *ap;

    for (ap = Asy; ap != Asy + ASY_MAX; ap++)
    {
	if(ap->iface == NULLIF)
	    break;
	unregister_io(ap->fd);
#ifdef POLLEDKISS
        if(ap->poller)
            killproc(ap->poller);
        ap->poller = 0;
#endif
        if (ap->monitor)
            killproc(ap->monitor);
        ap->monitor = 0;
	if (ap->iface->txproc)
	    killproc(ap->iface->txproc);
	ap->iface->txproc = 0;
	if (ap->rxproc)
	    killproc(ap->rxproc);
	ap->rxproc = 0;
	ap->iface = NULLIF;
	free_q(&ap->sndq);
	free_q(&ap->rcvq);
	close(ap->fd);
	if (ap->uulock[0])
	    unlink(ap->uulock);
	ap->uulock[0] = '\0';
    }
}


/*---------------------------------------------------------------------------*/

/* Monitor RLSD == DCD input */
static void
asy_monitor(dev, p1, p2)
    int dev;
    void *p1, *p2;
{
    struct asy *asyp = &Asy[dev];
    struct iface *ifp = (struct iface *)p1;
    int c;
    int ctlbits, octlbits=0;
#ifdef PDEBUG
    int k=0;
#endif

    ioctl(asyp->fd, TIOCMGET, &octlbits);
#ifdef PDEBUG
    log(-1,"monitor: begin@%x for fd=%d",octlbits,asyp->fd);
#endif
    while(1) {
        j2pause (100);
        if (!ioctl(asyp->fd, TIOCMGET, &ctlbits) && (c=octlbits^ctlbits)) {
#ifdef PDEBUG
            log(-1,"monitor: change of %x to %x",c,ctlbits);
#endif
            octlbits = ctlbits;
            if(c & TIOCM_CAR) { /* DCD changed */
                if(ctlbits&TIOCM_CAR)
                    c=PARAM_UP;
                else
		    c=PARAM_DOWN;
                if(ifp->ioctl)
                    (*ifp->ioctl)( ifp, c, TRUE, 0 );
                if(ifp->iostatus)
                    (*ifp->iostatus)( ifp, c, 0 );
            }
        }
#ifdef PDEBUG
        if(++k%50==0) {
		int x;
		ioctl(asyp->fd, TIOCMGET, &x);
		log(-1,"monitor: 50 loops; now %x (%x,%x)",x,octlbits,ctlbits);
	}
#endif
    }
}

/* Set asynch line speed */
int asy_speed (int dev, long bps)
{
    struct asy *asyp;
    int sp;
    struct termios termios;

    if(bps < 0 || dev >= ASY_MAX)
	return -1;
    asyp = &Asy[dev];
    if(asyp->iface == NULLIF)
	return -1;
#ifdef NRS
    /* but disallow baud==0 if NRS in effect */
    if(bps==0 && asyp->nrs_cts) {
        return -1;
    }
#endif
    sp = find_speed(bps);
    if (tcgetattr(asyp->fd, &termios))
	return -1;
    if (cfsetispeed(&termios, speed_table[sp].flags))
	return -1;
    if (cfsetospeed(&termios, speed_table[sp].flags))
	return -1;
    termios.c_cflag &= ~CBAUD;
    termios.c_cflag |= speed_table[sp].flags;
    if (tcsetattr(asyp->fd, TCSANOW, &termios))
	return -1;
    asyp->speed = speed_table[sp].speed;
    return 0;
}

/* Set termios VMIN (packet size) */
static int
asy_vmin(dev, pktsize)
    int dev;
    int32 pktsize;
{
    struct termios termios;
    struct asy *asyp;

    if (pktsize < 0 || pktsize > 255 || dev >= ASY_MAX)
	return -1;
    if ((asyp = &Asy[dev])->iface == NULLIF)
	return -1;
    if (tcgetattr(asyp->fd, &termios))
	return -1;
    termios.c_cc[VMIN] = asyp->pktsize = pktsize & 255;
    if (tcsetattr(asyp->fd, TCSANOW, &termios))
	return -1;
    return 0;
}

/* Set or clear RTS/CTS flow control */
static int
asy_rts(dev, onoff)
    int dev;
    int32 onoff;
{
    struct termios termios;
    struct asy *asyp;

    if ((asyp = &Asy[dev])->iface == NULLIF)
	return -1;
    if (tcgetattr(asyp->fd, &termios))
	return -1;
    /*
     * n5knx: break with the Linux past: do like in MSDOS: 0 => deassert.
     */
    if (onoff)
    {
	asyp->flags |= ASY_RTSCTS;
#ifdef NRS
        if(asyp->nrs_cts == 0)   /* if NRS disallow kernel RTS/CTS flowctl */
#endif
	termios.c_cflag |= CRTSCTS;
    }
    else
#ifdef NRS
         if(asyp->nrs_cts == 0)   /* can't allow deassert if nrs mode 'n' */
#endif
    {
	asyp->flags &= ~ASY_RTSCTS;
	termios.c_cflag &= ~CRTSCTS;
    }
    if (tcsetattr(asyp->fd, TCSANOW, &termios))
	return -1;
    return 0;
}

/*---------------------------------------------------------------------------*/

/* 01Dec2004, Maiko, New function replaces 'drop_dtr:' GOTO and labels */
static int32 do_drop_dtr (struct asy *ap, struct iface *ifp)
{
	int32 bps;

	bps = ap->speed;  /* save current baud */
	asy_speed(ifp->dev, 0L);   /* set baud to zero. This drops DTR. */
	ap->speed = bps;  /* retain previous speed value (for param up) */
#ifdef PDEBUG
	log(-1,"param down");
#endif
	return FALSE;
}

/* Asynchronous line I/O control */
int32
asy_ioctl(ifp, cmd, set, val)
struct iface *ifp;
int cmd;
int set;
int32 val;
{
    struct asy *ap = &Asy[ifp->dev];

    switch(cmd)
	{
/*
 * 15Feb2005, Maiko, New Pactor (17) parameter to
 * set the flags for Pactor or No Pactor mode !
 * 03Mar2006, Maiko, Use proper define now.
 */
	case PARAM_PACTOR:
		if (set)
			ap->flags |= ASY_PACTOR;
	
		return (ap->flags & ASY_PACTOR) != 0;

    case PARAM_SPEED:
	if (set)
	    asy_speed(ifp->dev, val);
	return ap->speed;
    case PARAM_MIN:
	if (set)
	    asy_vmin(ifp->dev, val);
	return ap->pktsize;
    case PARAM_RTS:
	if (set)
	    asy_rts(ifp->dev, val);
	return (ap->flags & ASY_RTSCTS) != 0;
    case PARAM_DOWN:
		return (do_drop_dtr (ap, ifp));
    case PARAM_DTR:
        if (set) {
            if(val) ; /* fall through to param up */
            else return (do_drop_dtr (ap, ifp));
        }
        else return -1;  /* disallow query? */
    case PARAM_UP:
        asy_speed(ifp->dev, ap->speed);  /* Undoes 'param down' ie DTR now asserted */
#ifdef PDEBUG
        log(-1,"param up");
#endif
        return TRUE;
    }
    return -1;
}

/*---------------------------------------------------------------------------*/

static void
asy_input(dev, arg1, arg2)
    int dev;
    void *arg1, *arg2;
{
    struct timeval tv;
    extern int errno;
    struct asy *ap;
    struct iface *ifp = (struct iface *) arg1;
    fd_set fds;
    char *buf;
    int i, c;

    ap = &Asy[dev];
    for (;;)
    {
        if (pwait(&ap->fd) != 0) {
            ifp->rxproc = NULLPROC;
	    return;
        }
	ap->rxints++;
	buf = mallocw(ap->rxbuf);

	if (ap->pktsize)
	    fcntl(ap->fd, F_SETFL, fcntl(ap->fd, F_GETFL, 0) & ~O_NONBLOCK);
	if ((i = (int)read (ap->fd, buf, (size_t)(ap->rxbuf))) == 0 ||
	    (!ap->pktsize && i == -1 && errno == EWOULDBLOCK))
	{
	    if (ap->pktsize)
		fcntl(ap->fd, F_SETFL, fcntl(ap->fd, F_GETFL, 0) | O_NONBLOCK);
	    ap->rxblock++;
	    free(buf);
	    continue;
	}
	if (i == -1)
	{
            if (errno == EINTR) continue;  /* timeout, retry */
	    tprintf("asy_input(%d): %s read error %d, shutting down\n",
		     dev, ifp->name, errno);
	    if (ap->pktsize)
		fcntl(ap->fd, F_SETFL, fcntl(ap->fd, F_GETFL, 0) | O_NONBLOCK);
	    free(buf);
            ifp->rxproc = NULLPROC;
	    return;  /* this terminates the process */
	}
	if ((c = ap->rxq) <= 0)
	    c = 1;
	while (i > 0 && c > 0)
	{
	    ap->rxchar += i;
	    ap->rxput++;
	    enqueue(&ap->rcvq, qdata(buf, i));
	    c--;
	    if (ap->pktsize)
	    {
		/* can't just read to check for data, since it might block */
		FD_ZERO(&fds);
		FD_SET(ap->fd, &fds);
		tv.tv_sec = tv.tv_usec = 0;
		if (select(FD_SETSIZE, &fds, 0, 0, &tv) == 0)
		{
		    i = -1;
		    errno = EWOULDBLOCK;
		    break;
		}
	    }
	    i = (int)read (ap->fd, buf, (size_t)(ap->rxbuf));
	}
	free(buf);
	if (i == -1) {
            if (errno == EINTR) continue;  /* timeout, so retry */
            else if (errno != EWOULDBLOCK) {
                tprintf("asy_input(%d): %s read error %d, shutting down\n",
                         dev, ifp->name, errno);
                ifp->rxproc = NULLPROC;
                return;  /* this terminates the process */
	    }
	}
	if (ap->pktsize)
	    fcntl(ap->fd, F_SETFL, fcntl(ap->fd, F_GETFL, 0) | O_NONBLOCK);
	if (c < 1 && ap->rxq > 1)
	    ap->rxovq++;
    }
}

int
get_asy(dev)
int dev;
{
    struct asy *ap;

    ap = &Asy[dev];
    if (ap->iface == NULLIF)
	return -1;
    while (!ap->rcvq)
    {
	if (pwait(&ap->rcvq) != 0)
	    return -1;		/* may not be dead, e.g. alarm in dialer */
    }
    return PULLCHAR(&ap->rcvq);
}

int
doasystat(int argc,char **argv,void *p)
{
	register struct asy *asyp;
	struct iface *ifp;
	int i;

	if(argc < 2){
		for(asyp = Asy;asyp < &Asy[ASY_MAX];asyp++){
			if(asyp->iface != NULLIF)
				pasy(asyp);
		}
		return 0;
	}
	for(i=1;i<argc;i++){
		if((ifp = if_lookup(argv[i])) == NULLIF){
			tprintf("Interface %s unknown\n",argv[i]);
			continue;
		}
		for(asyp = Asy;asyp < &Asy[ASY_MAX];asyp++){
			if(asyp->iface == ifp){
				pasy(asyp);
				break;
			}
		}
		if(asyp == &Asy[ASY_MAX])
			tprintf("Interface %s not asy\n",argv[i]);
	}

	return 0;
}

/*---------------------------------------------------------------------------*/

static void
pasy(asyp)
struct asy *asyp;
{
    tprintf("%s: %s, %ld bps, ", asyp->iface->name, asyp->uulock+strlen(LOCKDIR)+6,asyp->speed);
    if (asyp->pktsize)
	tprintf("packet size %d", asyp->pktsize);
    else
	tprintf("non-blocking");
#ifdef NRS
    if (asyp->nrs_cts)
        j2tputs(", NRS flow-control");
    else
#endif
        tprintf(", RTS/CTS %sabled", (asyp->flags & ASY_RTSCTS? "en": "dis"));
    tprintf(", carrier %sabled\n", (asyp->flags & ASY_CARR? "en": "dis"));
    tprintf("  RX: ints %lu chars %lu puts %ld rxqueue %d qlen %d ovq "
	    "%ld block %ld buf %d\n",
	    asyp->rxints, asyp->rxchar, asyp->rxput, asyp->rxq,
	    len_q(asyp->rcvq), asyp->rxovq, asyp->rxblock, asyp->rxbuf);
    tprintf("  TX: ints %lu chars %lu gets %ld txqueue %d qlen %d ovq %ld "
	    "block %ld\n",
	    asyp->txints, asyp->txchar, asyp->txget, asyp->txq,
	    len_q(asyp->sndq), asyp->txovq, asyp->txblock);
}

/*---------------------------------------------------------------------------*/

/* Serial transmit process, common to all protocols */

/*
 * Yes, this badly needs to be rewritten.
 */

static void
asy_tx(dev, p1, p2)
    int dev;
    void *p1, *p2;
{
    register struct mbuf *bp;
    struct asy *asyp;
    struct iface *ifp = (struct iface *)p1;
    int c, l, off;
#ifdef NRS
    int ctlbits;
#endif

    asyp = &Asy[dev];
    if ((c = asyp->txq) <= 0)
	c = 1;
    for (;;)
    {
	while (asyp->sndq == NULLBUF)
	{
	    if (!(c = asyp->txq))
		c = 1;
	    if (pwait(&asyp->sndq) != 0) {
                ifp->txproc = NULLPROC;
		return;
            }
	    asyp->txints++;
	}
	bp = dequeue(&asyp->sndq);
	asyp->txget++;
	off = 0;
	while (bp != NULLBUF)
	{
#ifdef NRS
            if(asyp->nrs_cts) { /* WZ2B */
                while(!ioctl(asyp->fd,TIOCMGET,&ctlbits) && !(ctlbits&TIOCM_CTS))
                    pwait(NULL);  /* await CTS asserted */
                ctlbits = TIOCM_RTS;  /* deassert RTS */
                ioctl(asyp->fd,TIOCMBIC,&ctlbits);
            }
#endif
	    l = (int)write (asyp->fd, bp->data + off, (size_t)(bp->cnt - off));
	    if (l == -1) {  /* some write error */
                if (errno == EINTR) {
#ifdef NRS
                    if(asyp->nrs_cts) {  /* try to reassert RTS */
                        ctlbits = TIOCM_RTS;
                        ioctl(asyp->fd,TIOCMBIS,&ctlbits);
                    }
#endif
                    continue;  /* timeout, retry */
                }
                else if (errno == EWOULDBLOCK) /* == EAGAIN perhaps? */
                    l = 0;  /* treat as 0 bytes written */
	        else {
                    tprintf("asy_tx(%d): %s write error %d, shutting down\n",
			    dev, ifp->name, errno);
                    ifp->txproc = NULLPROC;
#ifdef NRS
                    if(asyp->nrs_cts) {  /* try to assert RTS before return */
                        ctlbits = TIOCM_RTS;
                        ioctl(asyp->fd,TIOCMBIS,&ctlbits);
                    }
#endif
                    return;  /* this terminates the asy_tx process! */
		}
	    }

#ifdef NRS
            if(asyp->nrs_cts) { /* WZ2B */
                /* wait for output to finish: */
#ifdef linux
                while (!ioctl(asyp->fd,TIOCSERGETLSR,&ctlbits)
                 && !(ctlbits&TIOCSER_TEMT)) {
                    pwait(NULL);  /* await tx-empty (TIOCSER_TEMT) */
                }
#elif defined(sun)
		/* We don't seem to have access to the UART level, so let's
		   try draining the fd, but note we don't multitask */
		tcdrain(asyp->fd);
#endif
                ctlbits = TIOCM_RTS;  /* reassert RTS */
                ioctl(asyp->fd,TIOCMBIS,&ctlbits);
            }
#endif
	    asyp->txchar += l;
	    if (l == bp->cnt - off)
	    {
		bp = free_mbuf(bp);
		off = 0;
	    }
	    else
	    {
		asyp->txblock++;
		pwait(NULL);
		off += l;
	    }
	}
	if (--c < 1 && asyp->txq > 1)
	{
	    asyp->txovq++;
	    pwait(NULL);
	}
    }
}

/*---------------------------------------------------------------------------*/

/* Send a message on the specified serial line */
int
asy_send(dev,bp)
int dev;
struct mbuf *bp;
{
	struct asy *asyp;

	if(dev < 0 || dev >= ASY_MAX || (asyp = &Asy[dev])->iface == NULLIF){
		free_p(bp);
		return -1;
	}
	enqueue(&asyp->sndq, bp);
	return 0;
}

#ifdef TIPSERVER
int
carrier_detect(dev)
    int dev;
{
    struct asy *ap = &Asy[dev];

    if(ap->flags & ASY_CARR) {  /* can device assert DCD? */
        int ctlbits;
        if (!ioctl(ap->fd,TIOCMGET,&ctlbits))
            return (ctlbits&TIOCM_CAR) ? 1 : 0;
        /* else  fall into  return 1 */
    }
    return 1;	/* with CLOCAL mode set, it is always on */
}
#endif

#ifdef TERMSERVER
void
asy_sendbreak(int dev)
{
    struct asy *ap = &Asy[dev];
#ifndef BREAK_DURATION
#define BREAK_DURATION 0	/* 0=>.25 to .5 secs */
#endif

    tcsendbreak(ap->fd, BREAK_DURATION);
}
#endif /* TERMSERVER */

/*---------------------------------------------------------------------------*/

int
doasyconfig(ac, av, p)
    int ac;
    char **av;
    void *p;
{
    struct iface *ip;
    struct asy *ap;

    if (ac < 3)
	return 1;
    if (!(ip = if_lookup(av[1])))
    {
	tprintf("Interface %s unknown\n", av[1]);
	return 1;
    }
    for (ap = Asy; ap < &Asy[ASY_MAX]; ap++)
    {
	if (ap->iface == ip)
	    break;
    }
    if (!ap)
    {
	tprintf("Interface %s not asy\n", av[1]);
	return 1;
    }
    return subcmd(AsyPcmds, ac - 1, av + 1, ap);
}

static int
doasy2stat(ac, av, d)
    int ac;
    char **av;
    void *d;
{
    pasy((struct asy *) d);
    return 0;
}

static int
dorxqueue(ac, av, d)
    int ac;
    char **av;
    void *d;
{
    return setint(&((struct asy *) d)->rxq, "Receive queue size", ac, av);
}

static int
dotxqueue(ac, av, d)
    int ac;
    char **av;
    void *d;
{
    return setint(&((struct asy *) d)->txq, "Transmit queue size", ac, av);
}

static int
dobufsize(ac, av, d)
    int ac;
    char **av;
    void *d;
{
    return setuns(&((struct asy *) d)->rxbuf, "Receive buffer size", ac, av);
}
