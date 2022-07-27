/* TERM server: access a local asy device from a tcp connection.
 * Copyright 1991 Phil Karn, KA9Q
 * Adapted from KA9Q's NOS 930622 by N5KNX 11/95.
 * Many mods by VK2ANK/N5KNX  02/96.
 * Modify to handle break character/Add CRONLY option by VK2ANK 10/96
 */
#include "global.h"
#if defined(TERMSERVER)
#include "netuser.h"
#include "mbuf.h"
#include "socket.h"
#include "proc.h"
#include "remote.h"
#include "smtp.h"
#include "iface.h"
#include "asy.h"
#include "tcp.h"
#include "nr4.h"
#include "commands.h"
#include "mailbox.h"
#ifdef MSDOS
#include "i8250.h"
#else
#include "unix.h"
#include "unixasy.h"
#endif
#include "devparam.h"
#include "cmdparse.h"
#include "telnet.h"

static void termserv __ARGS((int s,void *unused,void *p));
static void termrx __ARGS((int s,void *p1,void *p2));
static int prompting_read __ARGS((char *prompt, int s, char *buf, int buflen));
static int mysetflag __ARGS((int argc,int *flags,int flag,char *argv[],char *flagdesc));
static int dotermwink __ARGS((int argc, char *argv[], void *p));
static int dotermnoecho __ARGS((int argc, char *argv[], void *p));
static int dotermnoopt __ARGS((int argc, char *argv[], void *p));
static int dotermnlcr __ARGS((int argc, char *argv[], void *p));
static int doterm7bit __ARGS((int argc, char *argv[], void *p));
static int dotermflush __ARGS((int argc, char *argv[], void *p));
static int dotermdrop __ARGS((int argc, char *argv[], void *p));
static int dobreakchar __ARGS((int argc, char *argv[], void *p));
static int docronly __ARGS((int argc, char *argv[], void *p));

static char *Termpass = NULLCHAR;
struct portlist {
    char *portname;
    int32 flushwait;
    int flags;
#define TERM_WINKDTR 1
#define TERM_NOECHO  2
#define TERM_NLCR    4
#define TERM_7BIT    8
#define TERM_INUSE   16
#define TERM_NOOPT   32
#define TERM_CRONLY  64
    int breakchar;
    int prev;
    struct portlist *next;
};
#define NULLPL (struct portlist *)NULL
static struct portlist *Termports = NULLPL;

/* Start up TCP term server */
int
term1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

    if(argc < 2)
        port = IPPORT_TERM;
    else
        port = atoi(argv[1]);
    return start_tcp(port,"Term Server",termserv,512);
}

/* Stop term server */
int
term0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

    if(argc < 2)
        port = IPPORT_TERM;
    else
        port = atoi(argv[1]);
    return stop_tcp(port);
}

/* 01Jan2005, Replacing GOTO 'quit:' tags with function call */

static int (*klmrawsave) __ARGS((struct iface *,struct mbuf *));

static void do_quit (int saved, int s, struct proc *rxproc,
						struct iface *ifp, struct asy *ap)
{
    if (saved)
	{
        killproc(rxproc);
        ifp->raw = klmrawsave;
        resume(ifp->rxproc);
#ifdef POLLEDKISS
        resume(ap->poller);
#endif
    }
    log(s,"close term");
    Curproc->input = -1;  /* avoid closing s twice */
    /* close_s(s);  done during process destruction */
}

static void
termserv(s,unused,p)
int s;
void *unused;
void *p;
{
    char buf[64];
    struct iface *ifp;
    struct route *rp;
    struct sockaddr_in fsocket;
    struct proc *rxproc = NULLPROC;
    struct asy *ap;
    struct mbuf *bp;
    struct portlist *pl;
    int i, saved=0, firstime=1;
    
    sockmode(s,SOCK_ASCII);
    sockowner(s,Curproc);
    close_s(Curproc->output);
    close_s(Curproc->input);
    Curproc->output = Curproc->input = s;
    j2setflush(s, '\n');
    log(s,"open term");
    tputc('\n');

    /* Prompt for and check remote password */
    if (Termpass) {
        if (prompting_read("Password: ", s, buf, sizeof(buf)) < 0)
			return (do_quit (saved, s, rxproc, ifp, ap));
        rip(buf);
        if(strcmp(buf,Termpass) != 0){
            j2tputs("Login incorrect\n");
			return (do_quit (saved, s, rxproc, ifp, ap));
        }
    }

    /* Prompt for desired interface. Verify that it exists, that
     * we're not using it for our TCP connection, that it's an
     * asynch port, and that there isn't already another tip, term
     * or dialer session active on it.
     */
    for(;;){
        if (firstime && (pl=Termports) != NULLPL && pl->next == NULLPL) /* just one defined? */
            strcpy(buf,pl->portname), firstime=0;  /* prime answer buffer with its name */
        else if (prompting_read("Interface: ",s,buf,sizeof(buf)) < 0)
			return (do_quit (saved, s, rxproc, ifp, ap));
        rip(buf);
        if((ifp = if_lookup(buf)) == NULLIF){
            tprintf(Badinterface,buf);
            continue;
        }
        for (pl=Termports; pl; pl=pl->next)
            if (!strcmp(pl->portname, buf)) break;
        if (!pl) {
            tprintf(Badinterface,buf);
            continue;
        }
	i = sizeof(fsocket);
        if(j2getpeername(s,(char *)&fsocket,&i) != -1
         && !ismyaddr(fsocket.sin_addr.s_addr)
         && (rp = rt_lookup(fsocket.sin_addr.s_addr)) != NULLROUTE
         && rp->iface == ifp){
            tprintf("You're using interface %s!\n",ifp->name);
            continue;
        }

        ap=&Asy[ifp->dev];
        if( ifp->dev >= ASY_MAX || ap->iface != ifp ){
            tprintf("Interface %s not asy port\n",buf);
            continue;
        }
        if(ifp->raw == bitbucket){
            tprintf("%s already in use\n",buf);
            continue;
        }

        /* Save output handler and temporarily redirect output to null */
        klmrawsave = ifp->raw;
        ifp->raw = bitbucket;
        saved = 1;

        /* Suspend the packet input driver. Note that the transmit driver
         * is left running since we use it to send buffers to the line.
         */
        suspend(ifp->rxproc);
#ifdef POLLEDKISS
        suspend(ap->poller);
#endif
        break;
    }

    /* bring the line up (just in case) */
    if ( ifp->ioctl != NULL )
        (*ifp->ioctl)( ifp, PARAM_UP, TRUE, 0L );
  
    if (pl->flags & TERM_WINKDTR) {
        asy_ioctl(ifp,PARAM_DTR,1,0);    /* drop DTR */
        j2pause(1000);
        asy_ioctl(ifp,PARAM_DTR,1,1);    /* raise DTR */
    }

    if (pl->flags & TERM_NOECHO)
        tprintf("%c%c%c%c%c%c",IAC,WILL,TN_ECHO,IAC,WILL,TN_SUPPRESS_GA);

    tprintf("Connection Established.\n");
    pl->flags |= TERM_INUSE;  /* Prevent a 'term drop' */
    sockmode(s,SOCK_BINARY);    /* Switch to raw mode */

    /* Now fork into receive and transmit processes */
    rxproc = newproc("term rx",384,termrx,s,ifp,pl,0);

    /* We continue to handle the TCP->asy direction */
    while((i = recvchar(s)) != EOF){
        while(i == IAC && !(pl->flags & TERM_NOOPT)) {
            i = recvchar(s);  /* DO/DONT/WILL/WONT */
            if (i == IAC) break;  /* escaped IAC */
            i = recvchar(s);  /* opt */
            i = recvchar(s);
        }
        if (i == pl->breakchar)
           asy_sendbreak(ifp->dev);
        else
        {
          if(i == '\n' && pl->flags&TERM_NLCR)
              i = '\r';               /* NL => CR */
          if (!(i == '\n' && pl->flags&TERM_CRONLY && pl->prev == '\r'))
          {
            if (pl->flags & TERM_7BIT) i &= 0x7f;
            bp = pushdown(NULLBUF,1);
            bp->data[0] = i;
            asy_send(ifp->dev,bp);
            ifp->lastsent = secclock();
            if (i == '\r') pwait(NULL);  /* give output buffers a chance to empty */
          }
          pl->prev = i;
        }
    }
    pl->flags &= ~TERM_INUSE;  /* Allow a 'term drop' */

	return (do_quit (saved, s, rxproc, ifp, ap));
}

static void
termrx(int s, void *p1, void *p2)
{
    int c;
    struct iface *ifp = (struct iface *)p1;
    struct portlist *pl = (struct portlist *)p2;
    
    for (;;) {
        if (pl->flushwait) j2alarm(pl->flushwait);
        if ((c = get_asy(ifp->dev)) != EOF) {
            if (pl->flags & TERM_7BIT) c &= 0x7f;
            usputc(s, c);
        }
        else usflush(s);
    }
}

static int
prompting_read(char *prompt, int s, char *buf, int buflen)
{
    int nread;

    if (prompt) {
        usputs(s,prompt);
        usflush(s);
    }
/*    j2alarm(10000);*/
    nread = recvline(s,buf,buflen);
/*    j2alarm(0);*/

    return nread;
}

static struct cmds DFAR TermIfcmds[] = {
    "7bit",       doterm7bit,  0, 0, NULLCHAR,
    "drop",       dotermdrop,  0, 0, NULLCHAR,
    "flushwait",  dotermflush, 0, 0, NULLCHAR,
    "nlcr",       dotermnlcr,  0, 0, NULLCHAR,
    "noecho",     dotermnoecho,0, 0, NULLCHAR,
    "noopt",      dotermnoopt, 0, 0, NULLCHAR,
    "winkdtr",    dotermwink,  0, 0, NULLCHAR,
    "breakchar",  dobreakchar, 0, 0, NULLCHAR,
    "cronly",     docronly,    0, 0, NULLCHAR,
    NULLCHAR,	NULL,			0, 0, NULLCHAR
};

/* 01Jan2005, Replaced GOTO 'usage:' with function call instead */
static int do_usage ()
{
	j2tputs ("Usage: term iface [<iface> options] | term password pwstring\n");

	return 0;
}

int
doterm(int argc,char *argv[],void *p)
{
    struct portlist *pl;
    struct iface *ifp;

    if (argc == 1)
		return (do_usage ());
    else if (!strcmp(argv[1],"iface"))
	{
        if (argc == 2) {
            j2tputs("term interfaces: ");
            for (pl=Termports; pl; pl=pl->next)
                tprintf("%s ", pl->portname);
            tputc('\n');
            return 0;
        }
        if((ifp = if_lookup(argv[2])) == NULLIF){
            tprintf(Badinterface,argv[2]);
            return 1;
        }
        for (pl=Termports; pl; pl=pl->next) {
            if(!strcmp(pl->portname, argv[2]))
                break;
        }
        if (pl == NULLPL) {
            pl = callocw(1,sizeof (struct portlist));
            pl->portname = j2strdup(argv[2]);
            pl->breakchar = -1;   /* disabled */
            pl->next = Termports;
            Termports = pl;
        }
        
        if (argc > 3) return subcmd(TermIfcmds,argc-2,&argv[2],pl); 
    }
    else if (!strncmp(argv[1], "pass", 4))
	{
        if (argc != 3)
			return (do_usage ());
        free(Termpass);
        Termpass = j2strdup(argv[2]);
    }
    else
		return (do_usage ());

    return 0;
}

static int
mysetflag(int argc,int *flags,int flag,char *argv[],char *flagdesc) {
  
    if(argc == 1) {
        /* Show the value of the flag */
        if(*flags & flag)
            tprintf("%s: On\n",flagdesc);
        else
            tprintf("%s: Off\n",flagdesc);
    } else {
            if(strcmpi(argv[1],"on") == 0 ||
               strncmpi(argv[1],"yes",strlen(argv[1])) == 0)
                *flags |= flag;
            else
                if(strcmpi(argv[1],"off") == 0 ||
                   strncmpi(argv[1],"no",strlen(argv[1])) == 0)
                    *flags &= ~flag;
            /* Invalid option ! */
                else j2tputs("Invalid option\n");
    }
    return 0;
}

static int
dotermwink(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    struct portlist *pl = p;

    return mysetflag(argc, &(pl->flags), TERM_WINKDTR, argv, "Wink DTR");
}

static int
dotermnoecho(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    struct portlist *pl = p;
    return mysetflag(argc, &(pl->flags), TERM_NOECHO, argv, "Disable local echo");
}

static int
dotermnoopt(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    struct portlist *pl = p;
    return mysetflag(argc, &(pl->flags), TERM_NOOPT, argv, "Disable telnet options processing");
}

static int
dotermnlcr(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    struct portlist *pl = p;
    return mysetflag(argc, &(pl->flags), TERM_NLCR, argv, "Translate newline to CR");
}

static int
doterm7bit(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    struct portlist *pl = p;
    return mysetflag(argc, &(pl->flags), TERM_7BIT, argv, "Mask to 7 bits");
}

static int
dotermflush(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    struct portlist *pl = p;
    return setint32 (&(pl->flushwait), "flush wait (ms)", argc, argv);
}

static int
dobreakchar(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    struct portlist *pl = p;
    return setint(&(pl->breakchar), "Break Charcode", argc, argv);
}

static int
docronly(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    struct portlist *pl = p;
    return mysetflag(argc, &(pl->flags), TERM_CRONLY, argv, "Only pass CR of CRLF");
}

static int
dotermdrop(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    struct portlist *pl = p, *pl1, *plp;

    if (pl->flags & TERM_INUSE)
        j2tputs("Interface in use.\n");
    else {
        for (pl1 = Termports, plp=NULLPL; pl1 && pl1 != pl; plp=pl1, pl1=pl1->next) ;
        if(pl1) {
            if(plp) plp->next = pl1->next;
            else Termports = pl1->next;
            free (pl1->portname);
            free (pl1);
        }
    }
    return 0;
}

#endif /* TERMSERVER */
