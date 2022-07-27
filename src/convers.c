/* convers server - based on conversd written by DK5SG
 * ported to WNOS by DB3FL - 9109xx/9110xx
 * ported to NOS by PE1NMB - 920120
 * Mods by PA0GRI
 * Cleanup, and additional mods by WG7J
 * Additions by N2RJT & WA2ZKD
 * More mods by N5KNX 1/96.
 */

#undef riskcmdoutlossage 	/* defined => don't flush on NLs in /cmd output
                                 * (risking loss if noblocking is also defined)
                                 */
#define noblocking		/* defined => drop data from TX queue if > LOCSFLOW,
				 * so as to conserve mem devoted to o/p queues
                                 * (to avoid breaking XCONVERS, blocking is forced on for that code only)
                                 */
#undef oldlocks			/* defined => use global 'locked' flag, which is
				 * modified asynchronously by multiple convers
				 * processes, hence can't work reliably.
                                 */
#undef Oldcode			/* defined => use larger work buffers for msg display
                                 * consuming more stack space
                                 */
#undef no_dupes			/* defined => /name, USER cmds are checked to
                                 * eliminate duplicate user names.  No other
                                 * converse server does this, so why bother?
                                 */

#include <time.h>
#include <ctype.h>
#ifdef  UNIX
#include <sys/types.h>
#include <sys/stat.h>
#endif
#ifdef MSDOS
#include <alloc.h>
#include <io.h>
#endif
#include <errno.h>
#include "global.h"
#ifdef CONVERS
#include "mailbox.h"
#include "netuser.h"
#include "pktdrvr.h"
#include "timer.h"
#include "cmdparse.h"
#include "usock.h"
#include "socket.h"
#include "session.h"
#include "files.h"
#include "mailutil.h"
#include "commands.h"

#include "sysevent.h"	/* 02Feb2008, New notification scripts - VE4KLM */

/*
 * Explanations of compile-time options:
 * 
 * #define XCONVERS 1    "LZW Compressed convers server and links"
 *                         Socket 3601 implies use of LZW-compressed links.
 * 			LZW compression tends to use MORE link bandwidth
 *                         since most msgs are short, and are dominated by
 *                         the size of the data dictionary which must accompany
 *                         the msgs.  XCONVERS is thus deprecated.
 * 			Start by:  start convers 3601
 * #define CNV_VERBOSE 1   "Verbose msgs"
 *                         Converse msgs can be longer instead of terser, at
 *                         the expense of more RAM and bandwidth used.
 * #define CNV_CHAN_NAMES 1 "Convers named channels"
 * 			Channels can have names, if the sysop puts them in
 * 			channel.dat file.  The format is simple: each line
 * 			contains a number followed by a space and a channel
 * 			name.  All linked convers nodes should use the same
 *                      names!
 * #define CNV_CALLCHECK 1 "Convers only allows callsigns"
 * 			Some people like this, others don't.  It forces the
 * 			name used in convers to be a Ham callsign.
 * #define CNV_LOCAL_CHANS 1 "Convers local channels and msg-only channels"
 * 			Local channels only allow chatting on local node, ie,
 *                         the data are not sent to linked convers servers.
 * 			Msg-only channels only allow /msg traffic.
 * 			Nice for when you step out for a few minutes.
 * #define CNV_ENHANCED_VIA 1 "If convers user is local, 'via' gives more info"
 * 			For the /who output, if the user is local, use the
 * 			otherwise blank "via" field to show how the user is
 * 			connected.  If ax25, show the port name.  If netrom,
 * 			show the node name.  If telnet, just say telnet,
 * 			since his domain name won't fit.
 * #define CNV_CHG_PERSONAL 1 "Allow users to change personal data permanently"
 * 			Enables the CONVERS SETINFO command, which allows
 * 			changes made with the /personal command to be stored
 * 			permanently in dbase.dat.  Nice for lazy sysops!
 * #define CNV_LINKCHG_MSG 1 "Send link-change messages in convers"
 * 			Sends a message on all channels alerting local users
 * 			whenever a link to another convers node is added or
 * 			dropped.  When a link is dropped, all of the lost
 * 			users are listed on one line (or more..) instead
 * 			of showing "XXXXX signed off" for each individual. 
 * #define LINK 1          "permit this convers node to be linked with others"
 *                         This is normally defined, since it's much more fun!
 * #define CNV_TOPICS 1	"Include channel topic gathering"
 *                         Only if LINK and CNV_CHAN_NAMES are also #defined.
 * #define CNV_VERBOSE_CHGS "Default to /VERBOSE yes"
 *                         Show signon/signoff notifications; generates lots
 *                         of msgs and hence local traffic.
 * #define CNV_STRICT_ASCII 1 "Disallow 8-bit char codes in messages"
 * 			Define if you wish to limit msg text to the 7-bit ASCII
 * 			set.  Actually only the first char of the msg is tested!
 * #define CNV_TIMESTAMP_MSG 1 "Add hh:mm prefix to msgs sent to local users"
 */

#ifndef LINK
#undef CNV_LINKCHG_MSG
#undef CNV_TOPICS
#endif

#ifndef CNV_CHAN_NAMES
#undef CNV_TOPICS
#endif

char Chostname[CNAMELEN+1];
int ConvUsers;
int ConvHosts;

#ifdef CNV_VERBOSE
static char cnumber[] = "*** Channel numbers must be in the range 0..%d.\n";
#ifdef CNV_LOCAL_CHANS
static char clocal[]  = "*** This is a local channel - no chatting with other nodes\n";
static char cmessage[]= "*** This is a message channel - only /msg allowed or received\n";
#endif
#else
static char cnumber[] = "* range 0..%d.\n";
#ifdef CNV_LOCAL_CHANS
static char clocal[]  = "* LOCAL CHANNEL\n";
static char cmessage[]= "* MESSAGE CHANNEL\n";
#endif
#endif

#define MAXCHANNEL  32767
#define LINELEN     256
/* The convers daemon stack needs to be AT LEAST 4 times the LINELEN !
 * since quiet a few of the commands have local vars of size 2 or 3 * LINELEN.
 */
#ifdef Oldcode
#define CDAEMONSTACK 2048
#else
#define CDAEMONSTACK 1280
#endif
#define CLINKSTACK 1024
#define MAX_WAITTIME    (60*60*3)
#define NAMELEN 10	/* Adjust all sscanf %10s lengths to match! */
#define CONNECT_TIMEOUT (300*1000)
#define myfeatures "up"	/* see tpp-1.14 SPECS: ping-pong + USER/UDAT */

struct convection {
    int  type;          /* Connection type */
#define CT_UNKNOWN      0
#define CT_USER         1
#define CT_HOST         2
#define CT_CLOSED       3
    char  name[NAMELEN+1];         /* Name of user or host */
    char  host[NAMELEN+1];         /* Name of host where user is logged on */
    struct convection *via;     /* Pointer to neighbor host */
    char *data;                 /* room for some personal data */
    int channel;            /* Channel number */
    int32 time;         /* Connect time */
    int maxq;           /* Maximum outstanding data before link reset */
#ifdef oldlocks
    int locked;         /* Set if mesg already sent */
#endif
    int fd;             /* Socket descriptor */
    int flags;          /* User flags */
#define CLOSE_SOCK  1
#define USE_SOUND   2
#define USE_LZW     4
#define VERBOSE     8
    /* This buffer is only needed for local users; a lot of space is wasted
     * for users from other hosts (256 bytes per user!). Fixed 930728 - WG7J
     * char ibuf[LINELEN];
     */
    char *ibuf;                      /* Input buffer */
    unsigned long received;          /* Number of bytes received */
    unsigned long xmitted;           /* Number of bytes transmitted */
    struct convection *next;    /* Linked list pointer */
};

#define CM_UNKNOWN  (1 << CT_UNKNOWN)
#define CM_USER     (1 << CT_USER)
#define CM_HOST     (1 << CT_HOST)
#define CM_CLOSED   (1 << CT_CLOSED)

#define NULLCONNECTION  ((struct convection *) 0)

static struct convection *convections;
#ifdef __GNUC__
struct linklist;   /* forward definition to keep GCC happy */
#endif

/* N2RJT - added channel names */
#ifdef CNV_CHAN_NAMES

struct channelname {
    int channel;    /* channel number */
    char *name;     /* channel name   */
#ifdef CNV_TOPICS
    char *topic;
    unsigned long timestamp;
#endif
    struct channelname *next;   /* linked list pointer */
};

#define NULLCHANNELNAME ((struct channelname *) 0)

static struct channelname *channelnames = NULLCHANNELNAME;

#ifdef CNV_LOCAL_CHANS
/* following 2 defs could dereference a NULL ptr, fatal under Linux */
/* #define ISLOCAL(chan) (!strncmpi(channame_of(chan,NULL),"loc",3))*/
/* #define ISMESSAGE(chan) (!strncmpi(channame_of(chan,NULL),"msg",3))*/
#define ISLOCAL(chan,x) ((x=channame_of(chan,NULL))!=NULLCHAR?!strncmpi(x,"loc",3):0)
#define ISMESSAGE(chan,x) ((x=channame_of(chan,NULL))!=NULLCHAR?!strncmpi(x,"msg",3):0)
#endif

#define PERSONAL_LEN 24

#else /* !CNV_CHAN_NAMES */

#define PERSONAL_LEN 31

#ifdef CNV_LOCAL_CHANS
#define ISLOCAL(chan,x) (chan>=30000 && chan < 32000)
#define ISMESSAGE(chan,x) (chan >= 32000)
#endif
#endif /* CNV_CHAN_NAMES */

#ifndef CNV_LOCAL_CHANS
#define ISLOCAL(chan,x) (0)
#define ISMESSAGE(chan,x) (0)
#endif

struct permlink {
    char name[NAMELEN+1];   /* Name of link */
    char softrev[NAMELEN+1]; /* Software and version (optional) */
    struct socket dest;     /* address and tcp port to link with */
#ifdef XCONVERS
    int use_lzw;
#endif
    struct convection *convection;  /* Pointer to associated connection */
    int32 statetime;        /* Time of last (dis)connect */
    int  tries;         /* Number of connect tries */
    int32 waittime;         /* Time between connect tries */
    int32 retrytime;        /* Time of next connect try */
    int fd;             /* socket descriptor */
    struct permlink *next;      /* Linked list pointer */
};
#define NULLPERMLINK ((struct permlink *) 0)

struct filter_link {
    struct filter_link *next;
    int32 addr;
};
#define NULLFL ((struct filter_link *) 0)

#ifdef LINK
struct proc *Linker;
static void connect_permlinks __ARGS((int a,void *b,void *c));
static void update_permlinks __ARGS((char *name,struct convection *cp));
#endif

int conv0 __ARGS((int argc,char *argv[],void *p));
int conv1 __ARGS((int argc,char *argv[],void *p));
void conv_incom __ARGS((int s,void *t,void *p));
void xconv_incom __ARGS((int s,void *t,void *p));
static void conv_incom2 __ARGS((int s,void *t,void *p,struct convection *cp));
static void free_connection __ARGS((register struct convection *cp));
static char *formatline __ARGS((char  *prefix,char *text));
static char *timestring __ARGS((long  gmt));
#ifdef oldlocks
static void clear_locks __ARGS((void));
#endif
static void send_msg_to_user __ARGS((char *fromname,char *toname,char *text,struct convection *cp));
static void send_user_change_msg __ARGS((char *name,char *host,int oldchannel,int newchannel,struct convection *cp));
static void send_link_change_msg __ARGS((struct convection *cp,char *name,char *change));
static void send_invite_msg __ARGS((char *fromname,char *toname,int channel,struct convection *cp));
static void personal_command __ARGS((struct convection *cp));
static void bye_command __ARGS((struct convection *cp));
static void name_command __ARGS((struct convection *cp));
static void who_command __ARGS((struct convection *cp));
static void send_msg_to_channel __ARGS((char *fromname,int channel,char *text,struct convection *cp));
static void free_closed_connections __ARGS((void));
static struct convection *alloc_connection __ARGS((int fd));
static void process_commands __ARGS((struct convection *cp,struct mbx *m));
static void set_personal __ARGS((struct convection *cp));
#ifdef CNV_CHG_PERSONAL
static void personal_data __ARGS((struct convection *cp));
static void store_personal __ARGS((struct convection *cp));
#endif
static void version_command __ARGS((struct convection *cp));
static void help_command __ARGS((struct convection *cp));
static void flags_command __ARGS((struct convection *cp));
static void h_host_command __ARGS((struct convection *cp));
static void h_invi_command __ARGS((struct convection *cp));
static void invite_command __ARGS((struct convection *cp));
static void h_loop_command __ARGS((struct convection *cp));
static void h_udat_command __ARGS((struct convection *cp));
static void h_umsg_command __ARGS((struct convection *cp));
static void h_user_command __ARGS((struct convection *cp));
static void h_ping_command __ARGS((struct convection *cp));
static void h_unknown_command __ARGS((struct convection *cp));
#ifdef LINK
int ShowConfLinks __ARGS((int s, int full));
static void links_command __ARGS((struct convection *cp));
static void h_cmsg_command __ARGS((struct convection *cp));
#endif
static void msg_command __ARGS((struct convection *cp));
static int channum_of __ARGS((char *name));
static char *channame_of __ARGS((int num, char **topicptr));
static void read_channels __ARGS((void));
static void show_channels __ARGS((struct convection *cp));
static void free_channels __ARGS((void));
static void channel_command __ARGS((struct convection *cp));
static char *pvia __ARGS((void *p));
static char *skipfields __ARGS((int fnum, char *p));
static char *trimfield __ARGS((char *str, int maxlen));
static int link_done __ARGS((struct linklist *head, struct convection *p));
void check_buffer_overload __ARGS((void));
#ifdef CNV_TOPICS
static void h_topi_command __ARGS((struct convection *cp));
#endif
#ifdef AX25
static int doconfcall __ARGS((int argc,char *argv[],void *p));
static int doct4 __ARGS((int argc,char *argv[],void *p));
#endif

static struct filter_link *Filterlinks;
static int FilterMode;
static struct permlink *permlinks;
extern char Ccall[AXALEN];
extern char shortversion[];

#ifdef CNV_CHG_PERSONAL
static int allow_info_updates;
static int docinfo __ARGS((int argc,char *argv[], void *p));
#endif

static int docfilter __ARGS((int argc,char *argv[],void *p));
static int dochostname __ARGS((int argc,char *argv[],void *p));
static int dociface __ARGS((int argc,char *argv[],void *p));
static int doclink __ARGS((int argc,char *argv[],void *p));
#ifdef XCONVERS
static int docxlink __ARGS((int argc,char *argv[],void *p));
static int doclinks __ARGS((int argc,char *argv[],void *p,int lzw));
#endif
static int docunlink __ARGS((int argc,char *argv[],void *p));
static int docmaxwait __ARGS((int argc,char *argv[],void *p));
static int dohmaxq __ARGS((int argc,char *argv[],void *p));
static int doumaxq __ARGS((int argc,char *argv[],void *p));
static int doconfstat __ARGS((int argc,char *argv[],void *p));
static int dotdisc __ARGS((int argc,char *argv[],void *p));
static int docdefaultchannel __ARGS((int argc,char *argv[],void *p));

static char USERcmd[] = "/\377\200USER %s %s %lu %d %d\n";
static char UMSGcmd[] = "/\377\200UMSG %s %s %s\n";
static char CMSGcmd[] = "/\377\200CMSG %s %d %s\n";
static char INVIcmd[] = "/\377\200INVI %s %s %d\n";
static char UDATcmd[] = "/\377\200UDAT %s %s %s\n";
static char HOSTcmd[] = "/\377\200HOST %s %s %s\n";
static char LOOPcmd[] = "/\377\200LOOP %s %s %s\n";
#ifdef CNV_TOPICS
static char TOPIcmd[] = "/\377\200TOPI %s %s %lu %d %s\n";
#endif

#define UNUSED(x) ((void)(x))	/* 15Apr2016, VE4KLM, trick to suppress any
				 * unused variable warning, why are there so
				 * many MACROs in this code, should convert
				 * them to actual functions at some point. */

static struct cmds DFAR Ccmds[] = {
    { "channel",  docdefaultchannel, 0, 0, NULLCHAR },
#ifdef LINK
    { "drop",     docunlink,  0, 0, NULLCHAR },
    { "filter",   docfilter,  0, 0, NULLCHAR },
    { "hmaxq",    dohmaxq,    0, 0, NULLCHAR },
#endif
    { "hostname", dochostname,0, 0, NULLCHAR },
    { "interface",dociface,   0, 0, NULLCHAR },
#ifdef LINK
    { "link",     doclink,    0, 0, NULLCHAR },
    { "maxwait",  docmaxwait, 0, 0, NULLCHAR },
#endif
#ifdef AX25
    { "mycall",   doconfcall, 0, 0, NULLCHAR },
#endif
    { "online",   doconfstat, 0, 0, NULLCHAR },
#ifdef CNV_CHG_PERSONAL
    { "setinfo",  docinfo,    0, 0, NULLCHAR },
#endif
#ifdef AX25
    { "t4",       doct4,      0, 0, NULLCHAR },
#endif
    { "tdisc",    dotdisc,    0, 0, NULLCHAR },
    { "umaxq",    doumaxq,    0, 0, NULLCHAR },
#if defined(XCONVERS) && defined(LINK)
    { "xlink",    docxlink,   0, 0, NULLCHAR },
#endif
    { NULLCHAR,	NULL,		0, 0, NULLCHAR }
};

/* N2RJT - usputs doesn't return length. */
static int
usputscnt(int s, char *x)
{
    if (usputs(s,x)!=EOF)
        return strlen(x);
    else
        return -1;
}

#ifdef CNV_CHAN_NAMES
static int
channum_of(char *name)
{
    struct channelname *tick;
    tick = channelnames;
    while (tick != NULLCHANNELNAME) {
        if (tick->name != NULLCHAR)
            if (!stricmp(name,tick->name))
                return tick->channel;
        tick = tick->next;
    }
    return -1;
}

static char *
channame_of(int num, char **topicptr)
{
    struct channelname *tick;
    tick = channelnames;
    while (tick != NULLCHANNELNAME) {
        if (num==tick->channel) {
#ifdef CNV_TOPICS
            if (topicptr != NULL) *topicptr = tick->topic;
#endif
            return tick->name;
        }
        tick = tick->next;
    }
    return NULLCHAR;
}

static void
read_channels()
{
    FILE *f;
    char line[256], *s2;
    struct channelname *temp, *list;
    if (channelnames != NULLCHANNELNAME)
        return;

    list = NULLCHANNELNAME;
    if ((f=fopen(Channelfile,READ_TEXT))==NULLFILE)
        return;

    while (fgets(line,sizeof(line),f)!=NULL) {
        rip(line);
        temp = (struct channelname *) mallocw(sizeof(struct channelname));
        temp->channel = atoi(line);
#ifdef CNV_TOPICS
        temp->topic =
#endif
        temp->name = NULLCHAR;
        if ((s2 = strchr(line,' '))!=0) {
            s2++;
            temp->name = j2strdup(s2);
        }
        temp->next = NULLCHANNELNAME;
        if (list)
            list->next = temp;
        else
            channelnames = temp;
        list = temp;
    }
    fclose(f);
}

static void
show_channels(struct convection *cp)
{
    struct channelname *temp;
    char *chtype;

    if (channelnames != NULLCHANNELNAME) {
        cp->xmitted += usputscnt(cp->fd, "Channel names:\n");
    }
    temp = channelnames;
    while (temp != NULLCHANNELNAME) {
#ifdef CNV_LOCAL_CHANS
        if (ISLOCAL(temp->channel,chtype)) chtype="(local)";
        else if (ISMESSAGE(temp->channel,chtype)) chtype="(message)";
        else
#endif
#ifdef CNV_TOPICS
            if(temp->topic) chtype=temp->topic;
            else
#endif
	        chtype="";
        cp->xmitted += usprintf(cp->fd, "%5d %-8s %s\n",
            temp->channel, temp->name, chtype);
        temp = temp->next;
    }
    cp->xmitted += usputscnt(cp->fd, "***\n");
}


/* Called by conv0 to free allocations for channel names */
static void
free_channels(void)
{
    struct channelname *temp,*nxt;

    for(temp = channelnames; temp; temp = nxt) {
        free(temp->name);
#ifdef CNV_TOPICS
        free(temp->topic);
#endif
        nxt = temp->next;
        free(temp);
    }
    channelnames=NULLCHANNELNAME;
}
#endif /* CNV_CHAN_NAMES */

/* Multiplexer for top-level convers command */
int
doconvers(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Ccmds,argc,argv,p);
}

#ifdef AX25
/* Display or change our AX.25 conference call */
static int
doconfcall(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char tmp[AXBUF];

    if(argc < 2){
        tprintf("%s\n",pax25(tmp,Ccall));
        return 0;
    }
    if(setcall(Ccall,argv[1]) == -1)
        return -1;
    return 0;
}

int32 CT4init = 7200;   /* 2 hours default */

/* Set link redundancy timer */
static int doct4 (int argc, char **argv, void *p)
{
    return setint32 (&CT4init, "Conf. redundancy timer (sec)", argc, argv);
}

#endif /* AX25 */

#ifdef LINK
static int32 CMaxwait = MAX_WAITTIME;

/* Set maxwait time for timed out links */
static int docmaxwait (int argc, char **argv, void *p)
{
    return setint32 (&CMaxwait, "Re-link max wait (sec)", argc, argv);
}

static int HMaxQ = 5*1024;

/* Set max qlimit for host links */
static int
dohmaxq(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint(&HMaxQ,"Max. Host Queue (bytes)",argc,argv);
}
#endif /* LINK */

static int UMaxQ = 2048;  /* Big enough to accommodate convers.hlp */

/* Set max qlimit for user links */
static int
doumaxq(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint(&UMaxQ,"Max. User Queue (bytes)",argc,argv);
}

int CDefaultChannel;

/* Set the initial channel */
static int
docdefaultchannel(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint(&CDefaultChannel,"Default channel",argc,argv);
}

static int32 Ctdiscinit = 0;

/* Set convers redundancy timer */
static int dotdisc (int argc, char **argv, void *p)
{
    return setint32 (&Ctdiscinit, "redundancy timer (sec)", argc, argv);
}

static int
dochostname(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc == 1)
        tprintf("%s\n",Chostname);
    else {
        strncpy(Chostname,argv[1],CNAMELEN);
        Chostname[CNAMELEN] = '\0';
    }
    return 0;
}

#ifdef CNV_CHG_PERSONAL
static int
docinfo(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&allow_info_updates,"Allow users to change info file",argc,argv);
}
#endif

#ifdef LINK
static int
docfilter(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int32 addr;
    struct filter_link *fl;

    if(argc == 1) {
        if(Filterlinks) {
            tprintf("Mode is: %s\n", FilterMode ? "Accept" : "Refuse");
            for(fl=Filterlinks;fl;fl=fl->next)
                tprintf("%s\n",inet_ntoa(fl->addr));
        }
        return 0;
    }
    if(!stricmp(argv[1],"mode")) {
        if(argc == 2)
            tprintf("Mode is: %s\n", FilterMode ? "Accept" : "Refuse");
        else {
            if(*argv[2] == 'a' || *argv[2] == 'A')
                FilterMode = 1;
            else
                FilterMode = 0;
        }
        return 0;
    }
    if((addr = resolve(argv[1])) == 0) {
        tprintf(Badhost,argv[1]);
        return 1;
    }
    /* check to see if we already have this in the list */
    for(fl=Filterlinks;fl;fl=fl->next)
        if(fl->addr == addr)
            return 0;       /* already have this one ! */

    /* Seems like a new one */
    fl = (struct filter_link *)callocw(1,sizeof(struct filter_link));
    fl->addr = addr;
    fl->next = Filterlinks;
    Filterlinks = fl;
    return 0;
}

#ifdef XCONVERS
static int
doclinks(argc,argv,p, use_lzw)
#else
static int
doclink(argc,argv,p)
#endif /* XCONVERS */
int argc;
char *argv[];
void *p;
#ifdef XCONVERS
int use_lzw;
#endif
{
    int32 addr;
    int   i;
    struct permlink *pl;

    if(argc == 1) {
        for(pl=permlinks;pl;pl=pl->next)
#ifdef XCONVERS
            tprintf("%10s: %s %s\n",pl->name,pinet(&pl->dest),
                pl->use_lzw ? "XCONVERS" : "CONVERS");
#else
            tprintf("%10s: %s\n",pl->name,pinet(&pl->dest));
#endif /* XCONVERS */
        (void)ShowConfLinks(Curproc->output, 1);  /* even more info */
        return 0;
    }
    if((addr = resolve(argv[1])) == 0) {
        tprintf(Badhost,argv[1]);
        return 1;
    }
    /* check to see if we already have a link to such animal,
     * this happens when we stop and restart the server - WG7J
     */
    for(pl=permlinks;pl;pl=pl->next)
        if(pl->dest.address == addr)
            return 1;       /* already have this one ! */

    /* Seems like a new link ! Go add it */
    pl = (struct permlink *)callocw(1,sizeof(struct permlink ));
    pl->dest.address = addr;
#ifdef XCONVERS
    pl->use_lzw = use_lzw;
    pl->dest.port = (use_lzw ? IPPORT_XCONVERS : IPPORT_CONVERS);
#else
    pl->dest.port = IPPORT_CONVERS;
#endif
    pl->next = permlinks;
    permlinks = pl;
    if (argc > 2 && (i=atoi(argv[2]))!=0) {  /*  could be a port number */
        pl->dest.port = i;
        argc--; argv++;
    }
    if(argc > 2) {
        strncpy(pl->name,argv[2],NAMELEN); /* callocw() put terminal NUL for us */
        update_permlinks(pl->name,NULLCONNECTION);
        pl->retrytime -= 45;  /* initial connect should happen sooner */
    } else
        strcpy(pl->name,"Unknown");  /* can't call update_permlinks() without unique name! */
    if(!Linker)
        Linker = newproc("Clinker",CLINKSTACK,connect_permlinks,0,0,NULL,0);

    return 0;
}

#ifdef XCONVERS
static int
doclink(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doclinks(argc,argv,p,0);
}

static int
docxlink(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return doclinks(argc,argv,p,1);
}
#endif	/* XCONVERS */

static int
docunlink(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int32 addr;
    struct permlink *pl;

    if(argc > 1 && (addr = resolve(argv[1])) == 0) {
        tprintf(Badhost,argv[1]);
        return 1;
    }

    for(pl=permlinks;pl;pl=pl->next) {
        if(!pl->dest.address) continue;  /* skip previously-dropped links */
        if(argc == 1 || pl->dest.address == addr) {
            tprintf("Unlinking %s\n",inet_ntoa(pl->dest.address));
#ifdef CNV_LINKCHG_MSG
            send_link_change_msg(pl->convection,pl->name,"unlinked by sysop");
#endif
            j2shutdown(pl->fd,2);

            pl->dest.address = 0;  /* flag to connect_permlinks() to free entry */
            if (argc > 1) return 0;
        }
    }

    if(argc > 1)
        tprintf("Not linked to %s\n",argv[1]);
    return 0;
}
#endif	/* LINK */

static int
dociface(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setflag(argc,argv[1],IS_CONV_IFACE,argv[2]);
}

static int
doconfstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int quick;
    char *cp = NULLCHAR;

    if (argc==1) quick=1;  /* no opt means quick format */
    else {
        quick=0;
        cp=argv[1];
        if(!strnicmp(cp,"long",strlen(cp)))
            cp = NULLCHAR;
    }
    (void)ShowConfUsers(Curproc->output,quick,cp);
    return 0;
}

/* Remember how many we've started .. maybe second for LZW link */
static int convers_servers = 0;

/* Stop convers server */
int
conv0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

    if(argc < 2)
        port = IPPORT_CONVERS;
    else
        port = atoi(argv[1]);
    if (convers_servers > 0)
	convers_servers--;
#ifdef CNV_CHAN_NAMES
    if (!convers_servers)
        free_channels();
#endif
#ifdef LINK
    if(!convers_servers && Linker) {
        killproc(Linker);
        Linker = NULLPROC;
    }
#endif /* LINK */
    return stop_tcp(port);
}

/* Start up convers server */
int
conv1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;

#ifdef CNV_CHAN_NAMES
    if (!convers_servers)
        read_channels();
#endif /* CNV_CHAN_NAMES */
    convers_servers++;
    if(argc < 2)
        port = IPPORT_CONVERS;
    else
        port = atoi(argv[1]);

#ifdef XCONVERS
    if (port == IPPORT_XCONVERS)
        return start_tcp(port,"XCONVERS Server",xconv_incom,CDAEMONSTACK);
    else
#endif
        return start_tcp(port,"CONVERS Server",conv_incom,CDAEMONSTACK);
}

/* Free a connection structure ... won't remove from chain (be careful) */
static void
free_connection(cp)
register struct convection *cp;
{
    register struct permlink *p;

    for(p = permlinks; p; p = p->next)
        if(p->convection == cp)
            p->convection = NULLCONNECTION;
    /* if(cp->data)   free() checks for NULL; smaller code :-) */
    free(cp->data);
    free(cp->ibuf);
    if(cp->flags & CLOSE_SOCK)
        close_s(cp->fd);
    cp->next = NULLCONNECTION;  /* defensive */
    free((char *) cp);
}

static void
free_closed_connections()
{
    register struct convection *cp,*p;
    time_t currtime;

    currtime = time(&currtime);

    for(p = NULLCONNECTION,cp = convections; cp; )
        if(cp->type == CT_CLOSED ||
        (cp->type == CT_UNKNOWN && cp->time + 300 < currtime)) {
            if(p) {
                p->next = cp->next;
                free_connection(cp);
                cp = p->next;
            } else {
                convections = cp->next;
                free_connection(cp);
                cp = convections;
            }
        } else {
            p = cp;
            cp = cp->next;
        }
}


static struct convection *
alloc_connection(fd)
int  fd;
{
    register struct convection *cp;
    time_t currtime;

    currtime = time(NULL);

    cp = (struct convection *)callocw(1,sizeof(struct convection ));
    cp->ibuf = (char *)callocw(1,LINELEN);
    cp->fd = fd;
    cp->maxq = UMaxQ;       /* Maximum qlimit for user */
#ifdef CNV_VERBOSE_CHGS
    cp->flags = CLOSE_SOCK+USE_SOUND+VERBOSE;  /* establish default behaviour */
#else
    cp->flags = CLOSE_SOCK+USE_SOUND;  /* establish default behaviour */
#endif
    cp->time = currtime;
    cp->next = convections;
    convections = cp;
    return cp;
}

/* check the host links for backlogged data.
 * If larger than set threshold, kill the link.
 * WG7J, 930208
 */
void check_buffer_overload(void) {
    struct convection *p;

    /* check the size of the outstanding data buffers */
    for(p = convections; p; p = p->next)
        if((p->maxq != 0) && (socklen(p->fd,1) > p->maxq)) {
                /* notify everyone of this special :-) occasion */
            bye_command(p);
                /* Blow this one out of the water */
            j2shutdown(p->fd,2);
            close_s(p->fd);
        }
}

#ifdef LINK
static void
update_permlinks(name,cp)
char *name;
struct convection *cp;
{
    register struct permlink *p;
    time_t currtime;

    for(p = permlinks; p; p = p->next)
        if(!strcmp(p->name,name)) {
            currtime = time(&currtime);
            p->convection = cp;
            p->statetime = currtime;
            p->tries = 0;
            p->waittime = 60;
            p->retrytime = currtime + p->waittime;
        }
}


void
connect_permlinks(a,b,c)
int a;
void *b;
void *c;
{
    int s;
#ifdef XCONVERS
	int x;
#endif
    register struct permlink *p, *plp, *pnext;
    struct sockaddr_in cport;
    time_t currtime;

    for(;;) {
        j2pause(15000);
        for(p = permlinks, plp=NULLPERMLINK; p; p = pnext) {
            pnext = p->next;
            currtime = time(&currtime);
            if(!p->dest.address) {  /* one to be deallocated */
                if (plp) plp->next=pnext;
                else permlinks=pnext;
                p->next=NULLPERMLINK;  /* defensive */
                free(p);
                continue;
            }
            else plp=p;  /* keep track of previous link ptr */

            if(p->convection || p->retrytime > currtime)
                continue;

            p->tries++;
            p->waittime <<= 1;

            if(p->waittime > CMaxwait)
                p->waittime = CMaxwait;

            p->retrytime = p->waittime + currtime;
#ifdef XCONVERS
            x = p->use_lzw;
#endif
            cport.sin_family = AF_INET;
            cport.sin_port = p->dest.port;
            cport.sin_addr.s_addr = p->dest.address; /* we've resolved this earlier */
            if((s = j2socket(AF_INET,SOCK_STREAM,0)) == -1)
			{
				/* 31Aug2016, Maiko (VE4KLM), should at least log this */
				log (s, "j2socket failed, errno [%d]", errno);
                continue;
			}

            j2alarm(CONNECT_TIMEOUT);  /* don't backoff too long! */

            if(j2connect(s,(char *)&cport,SOCKSIZE) == -1)
			{
                j2alarm(0);
                j2shutdown(s,2);  /* to make sure it doesn't linger around */
                close_s(s);     /* WG7J - 9207228 */
                continue;
            }
            j2alarm(0);
            p->fd = s;

            if(newproc("permlink",CDAEMONSTACK,
#ifdef XCONVERS
               x ? xconv_incom : conv_incom,
#else
               conv_incom,
#endif
               s,(void *)TELNET,NULL,0) == NULLPROC){
                j2shutdown(s,2);  /* blow it out of the water :-) */
                close_s(s);
            }
        }
        /* This is now called from the garbage collect process, such that it
         * checks even when the linker process isn't running! - WG7J
        check_buffer_overload();
        */
    }
}
#endif /* LINK */


#ifdef oldlocks
static void
clear_locks()
{
    register struct convection *p;

    for(p = convections;p;p = p->next)
        p->locked = 0;
}
#endif

static void send_sounds(struct convection *p) {
    if(p->flags & USE_SOUND)
        p->xmitted += usputscnt(p->fd,"");
}

/* skip over <n> fields in <str>, returning ptr to field <n>+1.  n>=1.
   Fields are separated by whitespace (blank, tab). */
static char *
skipfields (int n, char *str)
{
    int count=0; /* current field # */

    while ( count++<n && str!=NULLCHAR ) {
        str = skipnonwhite(str);
        str = skipwhite(str);
    }
    return(str);
}

/* Given str->field, truncate it to maxlen, and return ptr to next field */
static char *
trimfield (char *str, int maxlen)
{
    char *cp, *p;

    cp = skipnonwhite(str);
    p = skipwhite(cp);
    *cp = '\0';
    if ((int)strlen(str) > maxlen) *(str + maxlen) = '\0';

    return(p);
}

#ifndef oldlocks
/* We need a way to keep track of which channels we've already shown */
struct chanlist {
	int chanseen;
	struct chanlist *next;
};

/* We need a way to keep track of which links we've already written to */
struct linklist {
    struct convection *cp;
    struct linklist *next;
};

static int
chan_done(struct chanlist *head, int channel)
{
    struct chanlist *clp;

    for (clp=head; clp; clp=clp->next)
        if(clp->chanseen == channel) return 1;
    return 0;  /* not in done list */
}

static int
link_done(struct linklist *head, struct convection *p)
{
    struct linklist *clp;

    for (clp=head; clp; clp=clp->next)
        if(clp->cp == p) return 1;
    return 0;  /* not in done list */
}
#endif


#ifdef LINK
#ifdef CNV_LINKCHG_MSG
static void
send_link_change_msg(cp,name,change)
struct convection *cp;
char *name,*change;
{
    register struct convection *p;
    time_t curtime;
    char   *now;
    char deadmsg[256];
    int users;

    curtime = time(NULL);
    now = timestring(curtime);

    if (!strcmp(change,"DEAD")) {
        users = 0;
        strcpy(deadmsg, "link lost, users:");
        for(p = convections; p; p = p->next)
            if(p->via == cp) {
                p->type = CT_CLOSED;
                if(strlen(deadmsg) < sizeof(deadmsg)-20) {
                    strcat(deadmsg, " ");
                    strcat(deadmsg, p->name);
                }
                users++;
            }
        if (users==0)
            strcpy(deadmsg, "link lost, no users.");

    } else
        strcpy(deadmsg,change);

#ifdef oldlocks
    clear_locks();
#endif
    for(p = convections; p; p = p->next) {
        pwait(NULL);
	if(p->type == CT_USER
#ifdef oldlocks
          && !p->locked
#endif
          && !p->via )
            p->xmitted += usprintf(p->fd,
              "***%s %s %s.\n", now,name,deadmsg);
    }
    return;
}
#endif
#endif /* LINK */

static void
send_user_change_msg(name,host,oldchannel,newchannel,skipcp)
char  *name,*host;
int  oldchannel,newchannel;
struct convection *skipcp;
{
    register struct convection *p;
    time_t curtime;
    char   *now;
    char   tempname[NAMELEN+CNAMELEN+3];
#ifdef CNV_CHAN_NAMES
    char *channame;
#ifdef CNV_LOCAL_CHANS
    char *xp;
#endif
    channame = channame_of(newchannel,NULL);
#endif /* CNV_CHAN_NAMES */

    curtime = time(NULL);
    now = timestring(curtime);
    strcpy(tempname,name);
    if (strcmp(host,Chostname)) {
        strcat(tempname,"@");
        strcat(tempname,host);
    }

    for(p = convections; p; p = p->next) {
        pwait(NULL);
        if(p->type == CT_USER && !p->via && (p->flags&VERBOSE)
#ifdef oldlocks
           && !p->locked
#else
           && p!=skipcp
#endif
#ifdef CNV_LINKCHG_MSG
          && newchannel != -2
#endif

#ifdef CNV_LOCAL_CHANS
	&& !ISMESSAGE(p->channel,xp)
#endif

        ) {
            if(p->channel == oldchannel)
			{
                if(newchannel >= 0)
				{
#ifdef CNV_CHAN_NAMES
                    if (channame != NULLCHAR)
                        p->xmitted += usprintf(p->fd,
                        "***%s %s switched to channel %d (%s).\n",
                        now,tempname,newchannel,channame);
                    else
#endif  /* CNV_CHAN_NAMES */
                        p->xmitted += usprintf(p->fd,
                        "***%s %s switched to channel %d.\n",
                        now, tempname,newchannel);
                }
				else
				{
#ifdef SYSEVENT
					/* 02Feb2008, New notification - VE4KLM */
					j2sysevent (convdisc, tempname);
#endif
                    p->xmitted += usprintf(p->fd,
                    "***%s %s signed off.\n",now, tempname);
				}
#ifdef oldlocks
                p->locked = 1;
#endif
            }
            if(p->channel == newchannel)
			{
                send_sounds(p);

#ifdef SYSEVENT
				/* 02Feb2008, New notification - VE4KLM */
				j2sysevent (convconn, tempname);
#endif
                p->xmitted += usprintf(p->fd,
                "***%s %s signed on.\n",now, tempname);
#ifdef oldlocks
                p->locked = 1;
#endif
            }
        }
        if(p->type == CT_HOST
#ifdef oldlocks
           && !p->locked
#else
           && p!=skipcp
#endif
          ) {
            p->xmitted += usprintf(p->fd, USERcmd,
                name,host,0L,oldchannel,newchannel == -2 ? -1 : newchannel);
#ifdef oldlocks
            p->locked = 1;
#endif
        }
    }
    return;
}

#ifdef Oldcode

static char *
formatline(prefix,text)
char  *prefix,*text;
{

#define PREFIXLEN 10
#define CONVLINELEN   79

    register char  *f,*t,*x;
    register int  l,lw;

    static char buf[2*LINELEN];

    for(f = prefix,t = buf; *f; *t++ = *f++) ;
    l = (int)(t - buf);
    f = text;

    for(;;) {
        while(isspace(uchar(*f)))
            f++;
        if(!*f) {
            *t++ = '\n';
            *t = '\0';
            return buf;
        }
        for(x = f; *x && !isspace(uchar(*x)); x++) ;
        lw = (int)(x - f);
        if(l > PREFIXLEN && l + 1 + lw > CONVLINELEN) {
            *t++ = '\n';
            l = 0;
        }
        do {
            *t++ = ' ';
            l++;
        } while(l < PREFIXLEN);
        while(lw--) {
            *t++ = *f++;
            l++;
        }
    }
}

static void
send_msg_to_user(fromname,toname,text,skipcp)
char  *fromname,*toname,*text;
{
    register struct convection *p;
    char buffer[2*LINELEN];

    for(p = convections; p; p = p->next) {
        pwait(NULL);
        if(p->type == CT_USER && !strcmp(p->name,toname))
            if(p->via) {
#ifdef oldlocks
                if(!p->via->locked) {
#else
                if(p->via!=skipcp) {
#endif
                    p->via->xmitted += usprintf(p->via->fd,
                    UMSGcmd,fromname,toname,text);
#ifdef oldlocks
                    p->via->locked = 1;
#endif
                }
            } else {
#ifdef oldlocks
                if(!p->locked) {
#else
                if(p!=skipcp) {
#endif
                    if(strcmp(fromname,"conversd")) {
#ifdef CNV_TIMESTAMP_MSG
                        time_t currtime;

                        (void) time(&currtime);
                        sprintf(buffer,"<*%s%s*>:",fromname,timestring(currtime));
#else
                        sprintf(buffer,"<*%s*>:",fromname);
#endif
                        p->xmitted += usputscnt(p->fd,formatline(buffer,text));
                    } else {
                        p->xmitted += usputscnt(p->fd,text);
                        p->xmitted += usputscnt("\n");
                    }
#ifdef oldlocks
                    p->locked = 1;
#endif
                }
            }
    }
    return;
}

static void
send_msg_to_channel(fromname,channel,text,skipcp)
char  *fromname;
int  channel;
char  *text;
struct convection *skipcp;
{
    char  buffer[3*LINELEN];
    register struct convection *p;
#ifndef oldlocks
    struct linklist *ll_head=NULL, *clp;
#endif
#ifdef CNV_CHAN_NAMES
    char *xp;
#endif

#ifdef CNV_LOCAL_CHANS
    if (ISMESSAGE(p->channel,xp))
        return;
#endif

    for(p = convections; p; p = p->next) {
        pwait(NULL);
        if(p->type == CT_USER && p->channel == channel)
            if(p->via && !ISLOCAL(p->channel,xp)) {
#ifdef oldlocks
                if(!p->via->locked) {
#else
                if(p->via != skipcp && !link_done(ll_head,p->via)) {
#endif
                    p->via->xmitted += usprintf(p->via->fd,
                    CMSGcmd,fromname,channel,text);
#ifdef oldlocks
                    p->via->locked = 1;
#else
                    /* mark link as one we're done with */
                    clp=mallocw(sizeof(struct linklist));
                    clp->cp = p->via;
                    clp->next = ll_head;
                    ll_head = clp;
#endif
                }
            } else {
#ifdef oldlocks
                if(!p->locked) {
#else
                if(p!=skipcp) {
#endif

#ifdef CNV_TIMESTAMP_MSG
                    time_t currtime;

                    (void) time(&currtime);
                    sprintf(buffer,"<%s%s>:",fromname,timestring(currtime));
#else
                    sprintf(buffer,"<%s>:",fromname);
#endif
                    p->xmitted += usputscnt(p->fd,formatline(&buffer[0],text));
#ifdef oldlocks
                    p->locked = 1;
#endif
                }
            }
    }
#ifndef oldlocks
    while(ll_head) {  /* free allocations */
        clp=ll_head->next;
        free(ll_head);
        ll_head=clp;
    }
#endif
}

#else /* Oldcode */

/* Returns a formatted version of the given text.
 * The prefix appears at the beginning of the text, and each
 * line after the first will be indented to column PREFIXLEN.
 * All whitespace (SPACE and TAB) will be replaced by a single
 * SPACE character,
 * Lines will be filled to be CONVLINELEN characters long, wrapping
 * words as necessary.
 *
 * This uses an static internal  buffer rather than one passed by
 * the caller.  This increases the static memory used by the program
 * even if converse isn't active.  If we passed a buffer  allocated
 * on the stack, the process's stack would have to be large enough.
 */
static char *
formatline(prefix, text)
char  *prefix, *text;
{

#   define PREFIXLEN    10
#   define CONVLINELEN  79
#   define FMTBUFLEN    LINELEN+PREFIXLEN

    static char          buf[FMTBUFLEN];
    register char       *f, *t;
    register int         l, lw;
    register int         left = FMTBUFLEN-2;
    /* Runs of characters in delims[] will be collapsed into a single
       space by formatline().
     */
    char                *delims = " \t\n\r";
/*
#   define BPUTC(c)     if (left > 0) { left--; *t++ = (c); } else
*/
#   define BPUTC(c)     do{ if (left > 0) { left--; *t++ = (c); }} while(0)

    /* Copy prefix into buf; set l to length of prefix.
     */
    l = 0;
    for (f = prefix, t = buf; *f; ) {
        BPUTC (*f++);
        l++;
    }

    f = text;

    for (;;) {
        /* Skip leading spaces */
        while (isspace(uchar(*f)))
            f++;

        /* Return if nothing more or no room left */
        if (!*f || (left <= 0)) {
            *t++ = '\n';        /* don't use BPUTC; do even if !left */
            *t   = '\0';
            return buf;
        }

        /* Find length of next word (seq. of non-blanks) */
        lw = strcspn (f, delims);

        /* If the word would extend past end of line, do newline */
        if (l > PREFIXLEN && (l + 1 + lw) > CONVLINELEN) {
            BPUTC ('\n');
            l = 0;
        }

        /* Put out a single space */
        do {
            BPUTC (' ');
            l++;
        } while(l < PREFIXLEN);

        /* Put out the word */
        while(lw--) {
            BPUTC (*f++);
            l++;
        }
    }
#   undef BPUTC
}


static void
send_msg_to_user(fromname,toname,text,skipcp)
char  *fromname,*toname,*text;
struct convection *skipcp;
{
    register struct convection *p;

    for (p = convections; p; p = p->next)
	{
        pwait(NULL);
        if(p->type == CT_USER && !strcmp(p->name,toname))
		{
            if(p->via)
			{
#ifdef oldlocks
                if(!p->via->locked)
				{
#else
                if(p->via!=skipcp)
				{
#endif
                    p->via->xmitted += usprintf(p->via->fd,
                    UMSGcmd, fromname, toname, text);
#ifdef oldlocks
                    p->via->locked = 1;
#endif
                }
            }
			else
			{
#ifdef oldlocks
                if(!p->locked)
				{
#else
                if(p!=skipcp)
				{
#endif
                    if(strcmp(fromname,"conversd"))
					{
                        char prefix[NAMELEN+12];
                        char *buf;

#ifdef CNV_TIMESTAMP_MSG
                        time_t currtime;

                        (void) time(&currtime);
                        sprintf(prefix,"<*%.10s%s*>:",fromname,timestring(currtime));
#else
                        sprintf(prefix,"<*%.10s*>:",fromname);
#endif

                        buf = formatline (prefix,text);
                        p->xmitted += strlen (buf);
                        (void) usputs (p->fd, buf);
                    }
					else
					{    /* not from conversd */
                        p->xmitted += strlen (text);
                        (void) usputs (p->fd, text);
                    }   /* not from conversd */
#ifdef oldlocks
                    p->locked = 1;
#endif
                } /* if not locked */
            }   /* not via */
		}	/* type and strcmp */
    } /* for */
}

static void
send_msg_to_channel(fromname,channel,text,skipcp)
char  *fromname;
int  channel;
char  *text;
struct convection *skipcp;
{
    register struct convection *p;
#ifndef oldlocks
    struct linklist *ll_head=NULL, *clp;
#endif
#ifdef CNV_CHAN_NAMES
    char *xp;
	UNUSED (xp);	/* suppress warning, VE4KLM, 14Apr2016 */
#endif

#ifdef CNV_LOCAL_CHANS
    if (ISMESSAGE(channel,xp))
        return;
#endif

    for (p = convections; p; p = p->next)
	{
        pwait(NULL);
        if(p->type == CT_USER && p->channel == channel)
		{
            if(p->via && !ISLOCAL(p->channel,xp))
			{
#ifdef oldlocks
                if(!p->via->locked)
				{
#else
                if (p->via!=skipcp && !link_done(ll_head,p->via))
				{
#endif
                    p->via->xmitted += usprintf(p->via->fd, CMSGcmd,
                    fromname, channel, text);
#ifdef oldlocks
                    p->via->locked = 1;
#else
                    /* mark link as one we're done with */
                    clp=mallocw(sizeof(struct linklist));
                    clp->cp = p->via;
                    clp->next = ll_head;
                    ll_head = clp;
#endif
                }
            }
			else
			{
#ifdef oldlocks
                if(!p->locked)
				{
#else
                if(p!=skipcp)
				{
#endif
                    char         prefix[NAMELEN+12];
                    char        *buf;

#ifdef CNV_TIMESTAMP_MSG
                    time_t currtime;

                    (void) time(&currtime);
                    sprintf(prefix,"<%.10s%s>:",fromname,timestring(currtime));
#else
                    sprintf(prefix,"<%.10s>:",fromname);
#endif
#ifdef SYSEVENT
					/* 02Feb2008, New notification - VE4KLM */
					j2sysevent (convmsg, fromname);
#endif
                    buf = formatline (prefix, text);
                    p->xmitted += strlen (buf);
                    (void) usputs (p->fd, buf);

#ifdef oldlocks
                    p->locked = 1;
#endif
                } /* not locked */
            } /* not via */
		} /* type and channel */
    } /* for */
#ifndef oldlocks
    while(ll_head) {  /* free allocations */
        clp=ll_head->next;
        free(ll_head);
        ll_head=clp;
    }
#endif
}


#endif /* Oldcode */


extern char *Months[];      /* in smtpserv.c */

static char  *
timestring(gmt)
long  gmt;
{
    static char  buffer[10];  /* " hh:mm" or "MMM dd" */
    struct tm *tm;
    time_t currtime;

    time(&currtime);
    tm = localtime(&gmt);
    if(gmt + 24 * 60 * 60 > currtime)
        sprintf(buffer," %2d:%02d",tm->tm_hour,tm->tm_min);
    else
        sprintf(buffer,"%-3.3s %2d",Months[tm->tm_mon],tm->tm_mday);
    return buffer;
}

#ifdef CNV_VERBOSE
char invitetext[] = "\n*** Message from %s at%s ...\nPlease join convers channel %d.\n\n";
char mbinvitetext[] = "\n*** Message from %s at%s ...\nPlease join convers by typing 'CONV %d' !\n\n";
char responsetext[] = "*** Invitation sent to %s @ %s";
#else
char invitetext[] = "\n*** Msg frm %s at%s ...\nPse join ch. %d.\n\n";
char mbinvitetext[] = "\n*** Msg frm %s at%s ...\nPse hit 'CONV %d' to join convers.\n\n";
char responsetext[] = "*** sent to %s @ %s";
#endif
char cnvd[] = "conversd";

static void
send_invite_msg(fromname,toname,channel,skipcp)
char  *fromname,*toname;
int  channel;
struct convection *skipcp;
{
    char buffer[LINELEN];
    struct convection *p;
#ifdef MAILBOX
    struct mbx *m;
#endif
    time_t currtime;

    currtime = time(&currtime);

#ifdef MAILBOX
    /* Check users in the mailbox that aren't active */
    for(m=Mbox;m;m=m->next){
        if(m->state == MBX_CMD && !stricmp(m->name,toname)) {
            usprintf(m->user,mbinvitetext,fromname,timestring(currtime),channel);
            usflush(m->user);
#ifdef oldlocks
            clear_locks();
#endif
            sprintf(buffer,responsetext,toname,"BBS@");
            strcat(buffer,Hostname);
	    strcat(buffer,"\n");
            send_msg_to_user(cnvd,fromname,buffer,NULLCONNECTION);
            return;
        }
    }
#endif

    /* check the current convers users */
    for(p = convections; p; p = p->next) {
        if(p->type == CT_USER && !stricmp(p->name,toname)) {
            if(p->channel == channel) {
#ifdef oldlocks
                clear_locks();
#endif
                sprintf(buffer,"*** User %s is already on this channel.\n",toname);
                send_msg_to_user(cnvd,fromname,buffer,NULLCONNECTION);
                return;
            }
            if(!p->via
#ifdef oldlocks
               && !p->locked
#else
               && p!=skipcp
#endif
              ) {
                p->xmitted += usprintf(p->fd,invitetext,fromname, \
                timestring(currtime),channel);
#ifdef oldlocks
                clear_locks();
#endif
                sprintf(buffer,responsetext,toname,Chostname);
                strcat(buffer,"\n");
                send_msg_to_user(cnvd,fromname,buffer,NULLCONNECTION);
                return;
            }
            if(p->via
#ifdef oldlocks
               && !p->via->locked
#else
               && p->via!=skipcp
#endif
              ) {
                p->via->xmitted += usprintf(p->via->fd,
                INVIcmd,fromname,toname,channel);
                return;
            }
        }
    }
    /* Nothing found locally, invite user on all links */
    for(p = convections; p; p = p->next) {
        pwait(NULL);
        if(p->type == CT_HOST
#ifdef oldlocks
           && !p->locked
#else
           && p!=skipcp
#endif
          ) {
            p->xmitted += usprintf(p->fd,
            INVIcmd,fromname,toname,channel);
        }
    }
    return;
}

static void
bye_command(cp)
struct convection *cp;
{
    struct convection *p;

    switch(cp->type) {
        case CT_UNKNOWN:
            cp->type = CT_CLOSED;
            break;
        case CT_USER:
            cp->type = CT_CLOSED;
#ifdef oldlocks
            clear_locks();
#endif
            send_user_change_msg(cp->name,cp->host,cp->channel,-1,cp);
            ConvUsers--;
            break;
        case CT_HOST:
            cp->type = CT_CLOSED;
#ifdef CNV_LINKCHG_MSG
            send_link_change_msg(cp,cp->name,"DEAD");
#endif
#ifdef LINK
            update_permlinks(cp->name,NULLCONNECTION);
#endif
            for(p = convections; p; p = p->next)
                if(p->via == cp) {
                    p->type = CT_CLOSED;
#ifdef oldlocks
                    clear_locks();
#endif
#ifdef CNV_LINKCHG_MSG
                    send_user_change_msg(p->name,p->host,p->channel,-2,p);
#else
                    send_user_change_msg(p->name,p->host,p->channel,-1,p);
#endif
                }
            ConvHosts--;
            break;
        case CT_CLOSED:
            break;
    }
}

static void
channel_command(cp)
struct convection *cp;
{
#ifdef CNV_CHAN_NAMES
    char *channame, *tptr, *xp;
	UNUSED (xp);	/* suppress warning, VE4KLM, 14Apr2016 */
#endif
    char  *s;
    int  newchannel;

    s = skipfields(1,cp->ibuf);
    rip(s);
    if(*s == '\0') {
#ifdef CNV_VERBOSE
        cp->xmitted += usprintf(cp->fd,"*** You are on channel %d",cp->channel);
#else
        cp->xmitted += usprintf(cp->fd,"* On channel %d",cp->channel);
#endif
#ifdef CNV_CHAN_NAMES
        if ((channame=channame_of(cp->channel,&tptr))!=NULLCHAR) {
            cp->xmitted += usprintf(cp->fd," (%s)",channame);
#ifdef CNV_TOPICS
            if (tptr)
                cp->xmitted += usprintf(cp->fd," [%s]",tptr);
#endif
        }
#endif
        cp->xmitted += usputscnt(cp->fd,".\n");
#ifdef CNV_LOCAL_CHANS
        if (ISLOCAL(cp->channel,xp))
            cp->xmitted += usputscnt(cp->fd,clocal);
        if (ISMESSAGE(cp->channel,xp))
            cp->xmitted += usputscnt(cp->fd,cmessage);
#endif
#ifdef CNV_CHAN_NAMES
        show_channels(cp);
#endif
        return;
    }
#ifdef CNV_CHAN_NAMES
    newchannel = channum_of(s);
    if (newchannel == -1){
#endif
        newchannel = atoi(s);
#ifdef CNV_CHAN_NAMES
        if (newchannel == 0 && strspn(s,"0")!=strlen(s)) /* invalid numerics */
            newchannel = -1;
    }
#endif
    pwait(NULL);
    if(newchannel < 0) {
        /*  || newchannel > MAXCHANNEL) { */
        cp->xmitted += usprintf(cp->fd,cnumber,MAXCHANNEL);
        return;
    }
    if(newchannel == cp->channel) {
        cp->xmitted += usprintf(cp->fd,
        "*** Already on channel %d.\n",cp->channel);
        return;
    }
    send_user_change_msg(cp->name,cp->host,cp->channel,newchannel,cp);
    cp->channel = newchannel;
    cp->xmitted += usprintf(cp->fd,"*** Now on channel %d",cp->channel);
#ifdef CNV_CHAN_NAMES
    if ((channame=channame_of(cp->channel,&tptr))!=NULLCHAR) {
        cp->xmitted += usprintf(cp->fd," (%s)",channame);
#ifdef CNV_TOPICS
        if (tptr)
            cp->xmitted += usprintf(cp->fd," [%s]",tptr);
#endif
    }
#endif /* CNV_CHAN_NAMES */
    cp->xmitted += usputscnt(cp->fd,".\n");
#ifdef CNV_LOCAL_CHANS
    if (ISLOCAL(cp->channel,xp))
        cp->xmitted += usputscnt(cp->fd,clocal);
    if (ISMESSAGE(cp->channel,xp))
        cp->xmitted += usputscnt(cp->fd,cmessage);
#endif
    return;
}

static void
help_command(cp)
struct convection *cp;
{
    FILE *fp;
    char fname[FILE_PATH_SIZE];	
    sprintf(fname,"%s/convers.hlp",Helpdir);
    if((fp = fopen(fname,READ_TEXT)) != NULLFILE)
	{
#ifdef	DONT_COMPILE /* not productive */
		usflush(cp->fd); /* try empty output queue so we can fill it again */
#endif
        sendfile(fp, cp->fd, ASCII_TYPE, 0, NULL);
        fclose(fp);
    } else {
        cp->xmitted += usprintf(cp->fd,"No help available. /q to quit\n");
    }
    return;
}

static void
version_command(cp)
struct convection *cp;
{
    cp->xmitted +=
#ifdef WHOFOR
    usprintf(cp->fd,"%s, Compile: %s, Uptime: %s\n",
    shortversion,WHOFOR,tformat(secclock()));
#else
    usprintf(cp->fd,"%s, Uptime: %s\n",
    shortversion,tformat(secclock()));
#endif
}

static void
invite_command(cp)
struct convection *cp;
{
    char toname[NAMELEN+1];
    char *cp1;

    toname[0] = '\0';
    sscanf(cp->ibuf,"%*s %10s",toname);
    if((cp1=strchr(toname,'@')) != NULLCHAR)
        *cp1 = '\0';
    strlwr(toname);
    if(toname[0] != '\0')
        send_invite_msg(cp->name,toname,cp->channel,cp);
    return;
}

#ifdef LINK
int ShowConfLinks(int s, int full) {
    int num;
    struct convection *pc;
    struct permlink *pp;
    char  tmp[20];

    num = usprintf(s,"Host       State        Software  Since%s\n",
    (full) ? " NextTry Tries Receivd Xmitted TxQueue" : "");
    for(pc = convections; pc; pc = pc->next)
        if(pc->type == CT_HOST) {
            for (pp = permlinks; pp; pp = pp->next)
                if (pp->convection == pc) break;
            num += usprintf(s,
            (full) ?
            "%-10s Connected    %-8.8s %s               %7lu %7lu %7u\n" :
            "%-10s Connected    %-8.8s %s\n",
            pc->name,
            (pp)?pp->softrev:"",
            timestring(pc->time),
            pc->received,
            pc->xmitted,
            socklen(pc->fd,1));
            pwait(NULL);
        }

    for(pp = permlinks; pp; pp = pp->next)
        if(!pp->convection || pp->convection->type != CT_HOST) {
            strcpy(tmp,timestring(pp->retrytime));
            num += usprintf(s,
            (full) ?
            "%-10s %-12s %-8.8s %s %s %5d\n" :
            "%-10s %-12s %-8.8s %s\n",
            pp->name,
            pp->convection ? "Connecting" : "Disconnected",
            pp->softrev,
            timestring(pp->statetime),
            tmp,
            pp->tries);
            pwait(NULL);
        }
    num += usputscnt(s,"***\n");
    return num;
}

static void
links_command(cp)
struct convection *cp;
{
    char full[3];
    int f = 0;
    time_t curtime;

    curtime = time(NULL);
    cp->xmitted += usprintf(cp->fd,"*** %s",ctime(&curtime));
    full[0] = '\0';
    sscanf(cp->ibuf,"%*s %2s",full);
    if(*full == 'l' || *full == 'L')
        f = 1;
    cp->xmitted += ShowConfLinks(cp->fd,f);
    return;
}
#endif /* LINK */

static void
msg_command(cp)
struct convection *cp;
{

    char toname[NAMELEN+1],*text;
    register struct convection *p;

    toname[0] = '\0';
    sscanf(cp->ibuf,"%*s %10s",toname);
    strlwr(toname);
    text = skipfields(2,cp->ibuf);

    if(!*text)
        return;
    for(p = convections; p; p = p->next)
        if(p->type == CT_USER && !strcmp(p->name,toname))
            break;
    if(!p)
        cp->xmitted += usprintf(cp->fd,"*** No such user: %s.\n",toname);
    else
        send_msg_to_user(cp->name,toname,text,cp);
    return;
}

/* Set some personal data, like name and qth - WG7J */

static void
#ifdef CNV_CHG_PERSONAL
personal_data(cp)
#else
personal_command(cp)
#endif
struct convection *cp;
{
    struct convection *p;
    char *cp2;

    if((cp2 = strchr(cp->ibuf,' ')) != NULLCHAR) {
        cp2++;
        if(*cp2) {  /* there actually is an argument */
            free(cp->data);
            rip(cp->ibuf);      /* get rid of ending '\n' */
            if(strlen(cp2) > PERSONAL_LEN)
                *(cp2+PERSONAL_LEN) = '\0';
            cp->data = j2strdup(cp2);
            /* update all links too ! - WG7J */
            for(p=convections;p;p=p->next)
                if(p->type == CT_HOST) {
                    p->xmitted += usprintf(p->fd,UDATcmd,
                        cp->name,cp->host,cp->data);
                    pwait(NULL);
                }
            return;
        }
    }
    cp->xmitted += usprintf(cp->fd,"*** data set to: %s\n", \
    cp->data ? cp->data : "" );
    return;
}

/* find the personal information for this user */
void set_personal(struct convection *cp) {
    FILE *fp;
    char *cp1;

    if((fp = fopen(Cinfo,"r")) == NULL)
        return;
    while(fgets(cp->ibuf,LINELEN,fp) != NULL) {
        cp1 = cp->ibuf;
        /* find end of name */
        while(*cp1 != ' ' && *cp1 != '\t' && *cp1 != '\0')
            cp1++;
        if(!*cp1)
            continue;
        *cp1 = '\0';
        if(stricmp(cp->name,cp->ibuf))
            continue;
        /* Found personal data ! */
        *cp1 = ' ';
        fclose(fp);
        personal_command(cp);
        return;
    }
    fclose(fp);
    cp->xmitted += usputscnt(cp->fd,"*** Type /personal <your name> <your QTH>.\n");
}

#ifdef CNV_CHG_PERSONAL
/* Store personal data - N2RJT */
static void
store_personal(struct convection *cp)
{
    FILE *f1, *f2;
    char line[LINELEN];
    int namelen;

    namelen = strlen(cp->name);
    if (cp->data) {

        /* Save old personal file to backup */
        unlink(Cinfobak);
        if(rename(Cinfo,Cinfobak) && errno!=ENOENT)  /* OK if Cinfo doesn't exist yet */
            return;

        /*Write all users back, but update this one!*/
        if((f2 = fopen(Cinfo,WRITE_TEXT)) == NULLFILE) {
            /* Can't create defaults file ???*/
            rename(Cinfobak,Cinfo);   /* try to put things back */
            return;
        }

        if((f1 = fopen(Cinfobak,READ_TEXT)) != NULLFILE) {
            while (fgets(line, LINELEN, f1)!=NULLCHAR) {
                pwait(NULL);
                if (strncmp(line,cp->name,namelen))
                    fputs(line, f2);
            }
            fclose(f1);
        }
        fprintf(f2, "%s %s\n", cp->name, cp->data);
        fclose(f2);
    }
}

static void
personal_command(cp)
struct convection *cp;
{
    personal_data(cp);
    if (allow_info_updates)
        store_personal(cp);
}
#endif  /* CNV_CHG_PERSONAL */

static void
SendMotd(int s)
{
    FILE *fp;

    if((fp=fopen(ConvMotd,"r")) != NULL) {
        sendfile(fp, s, ASCII_TYPE, 0, NULL);
        fclose(fp);
    }
}


/* protected by ftpusers file - WG7J */
static void
name_command(cp)
struct convection *cp;
{
    int newchannel = CDefaultChannel;
    char dummy[7];
    int pwdignore;
    long privs;
#ifdef CNV_CALLCHECK
    int i,digits;
#endif
#ifdef CNV_CHAN_NAMES
    char *xp;
	UNUSED (xp);	/* suppress warning, VE4KLM, 14Apr2016 */
#endif
#ifdef no_dupes
    struct convection *p;
#endif
    cp->name[0] = dummy[0] = '\0';
    sscanf(cp->ibuf,"%*s %10s %6s",cp->name,dummy);
    if(dummy[0] != '\0')
        newchannel = atoi(dummy);
    if(cp->name[0] == '\0')
        return;
#ifdef no_dupes
    for(p = convections; p; p = p->next) { /* prevent dupe names */
        if(p->type == CT_USER && p!=cp && !strcmp(p->name,cp->name)) {
            cp->xmitted += usprintf(cp->fd,
               "Sorry, %s already used (on host %s)...try another\n", p->name, p->host);
            cp->name[0] = '\0';
            return;
        }
    }
#endif
    /* now check with ftpusers file - WG7J */
    pwdignore = 1;
    privs = userlogin(cp->name,NULLCHAR,&(cp->ibuf),LINELEN,&pwdignore,"confperm");
    if(privs & NO_CONVERS) {
        usputs(cp->fd, Noperm);
        cp->type = CT_CLOSED;
        return;
    }
/* N2RJT - validate the callsign, similar to the check done in mailbox.c */
/* In this context, a valid callsign is any with 3 or more characters,   */
/* and at least one digit.  Length and digits don't include anything     */
/* after a dash.                                                         */
#ifdef CNV_CALLCHECK
    for (digits=i=0; i<strlen(cp->name); i++) {
        if ((cp->name)[i] == '-')
            break;
        if (isdigit((cp->name)[i]))
            digits++;
    }
    if (i<3 || digits==0)
	{
        usputs(cp->fd, Noperm);
        cp->type = CT_CLOSED;
        return;
	}
#endif /* CNV_CALLCHECK */
    strlwr(cp->name);
    strcpy(cp->host,Chostname);
    cp->type = CT_USER;
    cp->xmitted += usprintf(cp->fd,
    "Conference @ %s  Type /HELP for help.\n",Chostname);
    /* Send the motd */
    SendMotd(cp->fd);
    if(dummy[0] != '\0' && newchannel < 0) {
        /* || newchannel > MAXCHANNEL) { */
        cp->xmitted += usprintf(cp->fd,cnumber,MAXCHANNEL);
    } else
        cp->channel = newchannel;
    send_user_change_msg(cp->name,cp->host,-1,cp->channel,cp);
#ifdef CNV_LOCAL_CHANS
    if (ISLOCAL(cp->channel,xp))
        cp->xmitted += usputscnt(cp->fd,clocal);
    if (ISMESSAGE(cp->channel,xp))
        cp->xmitted += usputscnt(cp->fd,cmessage);
#endif
    set_personal(cp);
    ConvUsers++;
    return;
}

/* Set or show the status of the 'sound' or 'verbose' flags - WG7J/N5KNX */
static void
flags_command(cp)
struct convection *cp;
{
    char *cp2,firstch;
    int mask=0;

    rip(cp->ibuf);
    firstch = tolower(cp->ibuf[1]);
    if(firstch == 's') mask = USE_SOUND;
    else if(firstch == 'v') mask = VERBOSE;

    if(*(cp2 = skipfields(1,cp->ibuf))) {
        if(*cp2 == 'n' || *cp2 == 'N') {   /* Turn it off */
            cp->flags &= ~mask;
        } else
            cp->flags |= mask;
    }
    else {
        cp->xmitted += usprintf(cp->fd,"*** %s %s\n", &cp->ibuf[1], (cp->flags&mask)?"yes":"no");
    }
    return;
}

#ifdef CNV_ENHANCED_VIA
/* Convert a socket (address + port) to an ascii string identifying
 * the family and possibly neighbor.
 */
char *
pvia(p)
void *p;	/* Pointer to structure to decode */
{
	static char buf[30];
	union sp sp;
	struct nrroute_tab *np;

	sp.p = p;
	switch(sp.sa->sa_family){
	case AF_LOCAL:
            strcpy(buf,"local");
            break;
	case AF_INET:
            strcpy(buf,"telnet");
	    break;
#ifdef	AX25
	case AF_AX25:
#ifdef JNOS20_SOCKADDR_AX
	    if (sp.ax->iface_index != -1)
		{
			struct iface *ifp = if_lookup2 (sp.ax->iface_index);

            sprintf (buf, "ax25:%s", ifp->name);
		}
#else
	    if(strlen(sp.ax->iface) != 0)
                sprintf(buf,"ax25:%s",sp.ax->iface);
#endif
	    else strcpy(buf,"ax25");
	    break;
#endif
#ifdef	NETROM
	case AF_NETROM:
            if ((np = find_nrroute(sp.nr->nr_addr.node)) != NULLNRRTAB)
                strcpy(buf,np->alias);
            else
                pax25(buf,sp.nr->nr_addr.node);
	    break;
#endif
        default:
            strcpy(buf,"?");
            break;
        }
	return buf;
}
#endif



/* Print a user display, return the number of characters sent */
int ShowConfUsers(int s,int quick,char *name) {
    int num,channel,l;
    struct convection *p;
#ifdef MAILBOX
    struct mbx *m;
#endif
    char buffer[LINELEN];
#ifdef CNV_CHAN_NAMES
    char *channame;
#endif
#ifndef oldlocks
struct chanlist *cl_head=NULL, *clp;
#endif

    if(quick) {
        num = usputscnt(s,"Channel ");
#ifdef CNV_CHAN_NAMES
        num += usputscnt(s,"          ");
#endif /* CNV_CHAN_NAMES */
        num += usputscnt(s,"Users\n");
#ifdef oldlocks
        clear_locks();    /* lock used to display like-channels together */
        do {
            channel = -1;
            for(p = convections; p; p = p->next) {
                if(p->type == CT_USER && !p->locked &&
                   (channel < 0 || channel == p->channel) ) {
                    pwait(NULL);
                    if(channel < 0) {
                        channel = p->channel;
#ifdef CNV_CHAN_NAMES
                        if ((channame=channame_of(channel,NULL))!=NULLCHAR) {
                            sprintf(buffer,"%7d(%s)         ",channel,channame);
                            buffer[17] = '\0';
                        } else
                            sprintf(buffer,"%7d          ", channel);
#else
                        sprintf(buffer,"%7d",channel);
#endif
                    }
                    /* n5knx: we could just usprints buffer[] then each user name, but
                     * we might overflow the printf() buf[SOLEN].  Better to just
                     * print sizeof[buffer] chars per line.
                     */
                    if (strlen(buffer) <= sizeof(buffer) - NAMELEN - 2) {
                        strcat(buffer," ");
                        strcat(buffer,p->name);
                    }
                    else {  /* flush what we have, then refill buffer */
                        num += usputscnt(s,buffer);
                        num += usputscnt(s,"\n");
                        sprintf(buffer,"%*s%s",
#ifdef CNV_CHAN_NAMES
                            17," ", p->name);
#else
                            7," ", p->name);
#endif
                            
                    }
                    p->locked = 1;
                }
            }
            if(channel >= 0) {
                num += usputscnt(s,buffer);
                num += usputscnt(s,"\n");
            }
        } while(channel >= 0);
#else	/* !oldlocks */
        do {
            channel = -1;
            for(p = convections; p; p = p->next) {
                if(p->type == CT_USER && !chan_done(cl_head,p->channel) &&
                   (channel < 0 || channel == p->channel) ) {
                    pwait(NULL);
                    if(channel < 0) {
                        channel = p->channel;
#ifdef CNV_CHAN_NAMES
                        if ((channame=channame_of(channel,NULL))!=NULLCHAR) {
                            sprintf(buffer,"%7d(%s)         ",channel,channame);
                            buffer[17] = '\0';
                        } else
                            sprintf(buffer,"%7d          ", channel);
#else
                        sprintf(buffer,"%7d",channel);
#endif
                    }
                    /* n5knx: we could just usprints buffer[] then each user name, but
                     * we might overflow the printf() buf[SOLEN].  Better to just
                     * print sizeof[buffer] chars per line.
                     */
                    if (strlen(buffer) <= sizeof(buffer) - NAMELEN - 2) {
                        strcat(buffer," ");
                        strcat(buffer,p->name);
                    }
                    else {  /* flush what we have, then refill buffer */
                        num += usputscnt(s,buffer);
                        num += usputscnt(s,"\n");
                        sprintf(buffer,"%*s%s",
#ifdef CNV_CHAN_NAMES
                            17," ", p->name);
#else
                            7," ", p->name);
#endif
                            
                    }
                }
            }
            if(channel >= 0) {
                num += usputscnt(s,buffer);
                num += usputscnt(s,"\n");
                /* mark channel as one we're done with */
                clp=mallocw(sizeof(struct chanlist));
                clp->chanseen = channel;
                clp->next = cl_head;
                cl_head = clp;
            }
        } while(channel >= 0);
        while(cl_head) {  /* free allocations */
            clp=cl_head->next;
            free(cl_head);
            cl_head=clp;
        }
#endif	/* oldlocks */
    } else {
        num = usputscnt(s,
#ifdef CNV_CHAN_NAMES
        "User       Host       Via      Channel Name       Time Personal\n");
#else
        "User       Host       Via        Channel Time Personal\n");
#endif
        for(p = convections; p; p = p->next) {
            if(p->type == CT_USER) {
                pwait(NULL);
                if(name == NULL || *name == '\0' || !stricmp(p->name,name)
                   || (*name == '@' && !stricmp(p->host,name+1))
                   || (*name == '#' && p->channel==atoi(name+1)) ) {
                    num += usprintf(s,"%-10s %-10s ",p->name,p->host);

                    if (p->via)
                        num += usprintf(s, "%-10s ", p->via->name);
                    else {
#ifdef CNV_ENHANCED_VIA
                        l = LINELEN;
                        if (j2getpeername(p->fd,buffer,&l)){
                            l = LINELEN;
                            j2getsockname(p->fd,buffer,&l);
                        }
                        num += usprintf(s, "%-10s ", pvia(buffer));
#else
                        num += usprintf(s, "%-10s ", "");
#endif
                    }
                    num += usprintf(s,
#ifdef CNV_CHAN_NAMES
                    "%5d %-8.8s %s %s\n",
#else	
                    "%5d %s %s\n",
#endif /* CNV_CHAN_NAMES */
                    p->channel,
#ifdef CNV_CHAN_NAMES
                    ((channame = channame_of(p->channel,NULL))!=0) ? channame : "",
#endif /* CNV_CHAN_NAMES */
                    timestring(p->time),
                    p->data ? p->data : "" );
                }
            }
        }
    }
#ifdef MAILBOX
    if (name == NULL || *name == '\0') {
        for(m=Mbox;m;m=m->next) {
            if(m->state == MBX_CMD) {
                if(quick)
                    num += usprintf(s," BBS %s\n",m->name);
                else
                    num += usprintf(s,"%-10s BBS@%s\n",m->name,Hostname);
                pwait(NULL);
            }
        }
    }
#endif
    num += usputscnt(s,"***\n");
    return num;
}

static void
who_command(cp)
struct convection *cp;
{
    char buffer[LINELEN];
    int quick = 1;
    time_t curtime;

    curtime = time(NULL);
    cp->xmitted += usprintf(cp->fd,"*** %s", ctime(&curtime));

    buffer[0] = '\0';
    sscanf(cp->ibuf,"%*s %s",buffer);
    if (buffer[0]) quick=0;  /* anything but "/who" means long format */
    if(!strnicmp(buffer,"long",strlen(buffer)))
        buffer[0] = '\0';

    cp->xmitted += ShowConfUsers(cp->fd,quick,buffer);
    return;
}

#ifdef LINK
/* /..CMSG <user> <channel> <text> */
static void
h_cmsg_command(cp)
struct convection *cp;
{
    char *text;
    int  channel;
    char name[NAMELEN+1];

    sscanf(cp->ibuf,"%*s %10s %d",name,&channel);
    text = skipfields(3,cp->ibuf);
#ifdef CNV_STRICT_ASCII
    if(isprint(*text))
#endif
        send_msg_to_channel(name,channel,text,cp);
    return;
}

/* Return 1 if the host is to be allowed, or 0 if refused - WG7J */
static int
Allow_host(int s) {
    struct filter_link *fl;
    struct sockaddr_in fsocket;
    int i = sizeof(struct sockaddr_in);

    if(Filterlinks) {    /* Check for this ip address */
        j2getpeername(s,(char *)&fsocket,&i);
        for(fl=Filterlinks;fl;fl=fl->next)
            if(fl->addr == (int32)fsocket.sin_addr.s_addr)
                return FilterMode;
        /* Not found ! */
        return !FilterMode;
    }
    return 1;
}

/* /..HOST <hostname> [software [facilities]] */
static void
h_host_command(cp)
struct convection *cp;
{

    char *name, *rev;
	/* char *features; */
    struct convection *p;
    struct permlink *pp;

    if(!Allow_host(cp->fd)) {
        bye_command(cp);
        return;
    }
    rip(cp->ibuf);
    name = skipfields(1,cp->ibuf);
    rev = trimfield(name,NAMELEN);
    /* features = trimfield(rev,NAMELEN); */
/* don't care (yet?)    (void)trimfield(features,NAMELEN);*/
    if(*name == '\0') {  /* only name is required */
        bye_command(cp);
        return;
    }

    for(p = convections; p; p = p->next)
        if(!strcmp(p->name,name)) {
            bye_command(p);
            return;
        }
    for(pp = permlinks; pp; pp = pp->next) {
        if(!stricmp(pp->name,name) && pp->convection && pp->convection != cp) {
            bye_command((strcmp(Chostname,name) < 0) ? pp->convection : cp);
            return;
        }

        if(pp->fd == cp->fd) /* can't rely on name since we allow Unknown */
		{

/*
 * 31Aug2016, Maiko (VE4KLM), Welll, if softrev is 20 bytes long, it will most
 * certainly overwrite the socket struct at the top of the permlink struct, and
 * corrupt the link address value, ie : makes it look like it's from china now,
 * no seriously, 44.12.3.129 gets changed to 117.112.109.100 (cn), so we need
 * to make sure we only copy NAMELEN bytes, not the whole thing !!! Might as
 * well do this for the name as well, just to be on the safe side ...
 *
 * Thanks to VE3CGR (Ron) for bugging me about this :)
 *
            strcpy(pp->softrev, rev);
            strcpy(pp->name, name);
 */
            strncpy(pp->softrev, rev, NAMELEN);
            strncpy(pp->name, name, NAMELEN);	/* in case was Unknown */
        }
    }
/*
    if(cp->type != CT_UNKNOWN)
        return;
 */
    cp->type = CT_HOST;
    cp->maxq = HMaxQ;
    strcpy(cp->name,name);      /* already allocated */
    update_permlinks(name,cp);
#ifdef CNV_LINKCHG_MSG
    send_link_change_msg(cp,cp->name,"linked");
#endif
    cp->xmitted += usprintf(cp->fd,HOSTcmd,Chostname, shortversion, myfeatures);
    for(p = convections; p; p = p->next)
        if(p->type == CT_USER
#ifndef oldlocks
           && p->via != cp
#endif
          ) {
            cp->xmitted += usprintf(cp->fd, USERcmd,
                                    p->name,p->host,0L,-1,p->channel);
            if(p->data)
                cp->xmitted += usprintf(cp->fd,UDATcmd,p->name,p->host,p->data);
            pwait(NULL);
        }

#ifdef CNV_TOPICS
    {
        struct channelname *np;

        for(np=channelnames; np; np=np->next)
            if(np->topic) {
                cp->xmitted += usprintf(cp->fd, TOPIcmd, "conversd", Chostname,
                                        np->timestamp, np->channel, np->topic);
                pwait(NULL);
            }
    }
#endif

    ConvHosts++;
    return;
}

/* /..INVI <from> <user> <channel> */
static void
h_invi_command(cp)
struct convection *cp;
{
    char fromname[NAMELEN+1],toname[NAMELEN+1];
    int  channel;

    sscanf(cp->ibuf,"%*s %10s %10s %d",fromname,toname,&channel);
    send_invite_msg(fromname,toname,channel,cp);
    return;
}

/* /..LOOP <host> */
static void
h_loop_command(cp)
struct convection *cp;
{
    char host[NAMELEN+1];

    sscanf(cp->ibuf,"%*s %10s",host);
    log(cp->fd, "conversd rx: LOOP %s",host);
    bye_command(cp);
}

/* Command to take user's personal data across a link - WG7J */
/* /..UDAT <user> <host> [text] */
static void
h_udat_command(cp)
struct convection *cp;
{
    char *name,*host,*data;
    struct convection *p;

    rip(cp->ibuf);  /* drop \n */
    name = skipfields(1,cp->ibuf);
    host = trimfield(name, NAMELEN);
    data = trimfield(host, NAMELEN);
    if (!*data) return;  /* missing fields */
    if(strlen(data)>PERSONAL_LEN) *(data+PERSONAL_LEN)='\0';
    if(!strcmp(data,"@")) *data = '\0';  /* @ taken as null-data (tnos) */

    /* everything seems fine, now find user ! */
    for(p=convections;p;p=p->next) {
        if(!strcmp(p->name,name) && !strcmp(p->host,host)) {
            free(p->data);
            p->data = j2strdup(data);
        }
        /* update over other links  Apr 12/93 VE3DTE */
        if(p->type == CT_HOST
#ifdef oldlocks
           && !p->locked
#else
           && p!=cp
#endif
          ) {
            p->xmitted += usprintf(p->fd,UDATcmd,name,host,data);
            pwait(NULL);
        }
    }
}

/* /..UMSG <from> <to> <text> */
static void
h_umsg_command(cp)
struct convection *cp;
{
    char fromname[NAMELEN+1],toname[NAMELEN+1],*text;

    sscanf(cp->ibuf,"%*s %10s %10s",fromname,toname);
    text = skipfields(3,cp->ibuf);
    if(*text)
        send_msg_to_user(fromname,toname,text,cp);
    return;
}

/* /..USER <user> <host> <timestamp> <fromchan> <tochan> [@|text] */
static void
h_user_command(cp)
struct convection *cp;
{
    char host[3*NAMELEN+1],name[3*NAMELEN+1],*data;
    int  newchannel,oldchannel,dlen;
    struct convection *p;
    time_t currtime;

    currtime = time(&currtime);

    sscanf(cp->ibuf,"%*s %s %s %*s %d %d",name,host,&oldchannel,&newchannel);
    data = skipfields(6,cp->ibuf);  /* Tnos extension */
    /* Make sure the fields are not longer */
    host[NAMELEN] = name[NAMELEN] = '\0';
    rip(data);      /* rip the '\n' */
    
    if((dlen=strlen(data)) > PERSONAL_LEN)
        *(data+PERSONAL_LEN) = '\0';
    else
        if(dlen==1 && *data=='@') *data = '\0';  /* @ taken as null-data (tnos) */

    for(p = convections; p; p = p->next)
        if(p->type == CT_USER) {
            /* new 920705 dl9sau */
            /* If Neighbour2 registers a user on HostX, while someone has already
             * been registered for HostX via Neighbour1, then we definitely have
             * a loop !  We send a loop detect message and then close the link:
             * /..LOOP <Chostname> <myneighbour> <host>
             *
             * The LOOP PREVENTION CODE detects ONLY a loop if it starts at this
             * host. That's, why I suggest this code to be implemented in every
             * conversd implementation.
             */
            if (oldchannel < 0 && p->via != cp && !stricmp(p->host, host)) {
                usprintf(cp->fd,LOOPcmd,
                Chostname, host,p->via ? p->via->name : Chostname);
                log(cp->fd, "conversd sent: LOOP %s",host);
                bye_command(cp);
                return;
            }
            if(p->channel == oldchannel && p->via == cp && \
                !strcmp(p->name,name) && !strcmp(p->host,host))
                break;
#ifdef no_dupes
            if(!strcmp(p->name,name))
                return;  /* ignore dupe user name */
#endif
        }
    if(!p) {
        p = (struct convection *)callocw(1,sizeof(struct convection ));
        p->type = CT_USER;
        strcpy(p->name,name);
        strcpy(p->host,host);
        p->via = cp;
        p->channel = oldchannel;
        p->time = currtime;
        p->next = convections;
        convections = p;
    }
    if((p->channel = newchannel) < 0) {
        p->type = CT_CLOSED;
        free_closed_connections();  /*  VE3DTE Apr 5/93 */
    }
    else if (data && *data) {  /* Tnos extension avoids UDAT use */
        free(p->data);
        p->data = j2strdup(data);
    }
    send_user_change_msg(name,host,oldchannel,newchannel,cp);
    return;
}


static void
h_ping_command(cp)
struct convection *cp;
{
    cp->xmitted += usprintf(cp->fd, "/\377\200PONG -1\n");
}


#ifdef CNV_TOPICS
/* remember channel net-topics for non-local, non-msg channels */
/*  /..TOPI <user> <host> <time> <channel> [text] */
static void
h_topi_command(cp)
struct convection *cp;
{
    char *topic;
#ifdef CNV_CHAN_NAMES
    char *xp;
	UNUSED (xp);	/* suppress warning, VE4KLM, 14Apr2016 */
#endif
    unsigned long topic_timestamp;
    int  channel;
    struct channelname *temp,*cnp=NULLCHANNELNAME;

    sscanf(cp->ibuf,"%*s %*s %*s %lu %d",&topic_timestamp,&channel);

    topic = skipfields(5,cp->ibuf);
    for (temp = channelnames; temp; temp = temp->next) {
        if (temp->channel==channel) {
#ifdef CNV_LOCAL_CHANS
            if(!ISMESSAGE(temp->channel,xp) && !ISLOCAL(temp->channel,xp)
               && topic_timestamp > temp->timestamp) {
#else
            if(topic_timestamp > temp->timestamp) {
#endif
                free(temp->topic);
                temp->topic = j2strdup(topic);  /* could be NUL */
                temp->timestamp = topic_timestamp;
                rip(temp->topic);  /* can't rip(topic) if we pass on ibuf */
            }
            break;
        }
        else if (temp->channel<channel)
            cnp=temp;   /* used to maintain monotonic ordering */
    }

    if(!temp) {
        temp = (struct channelname *) mallocw(sizeof(struct channelname));
        temp->channel = channel;
        temp->name = j2strdup("---");
        temp->topic = j2strdup(topic);
        temp->timestamp = topic_timestamp;
        rip(temp->topic);

        if(cnp) {
            temp->next = cnp->next;
            cnp->next = temp;
        } else {
            temp->next = channelnames;
            channelnames = temp;
        }
    }

    h_unknown_command(cp);  /* pass on the TOPI command */
}
#endif /* CNV_TOPICS */


/* this command simply passes on any host requests that we don't understand,
   since our neighbors might understand them */
static void
h_unknown_command(cp)
struct convection *cp;
{
    struct convection *p;

    for (p = convections; p; p = p->next) {
        if (p->type == CT_HOST && p != cp)
            cp->xmitted += usprintf(p->fd, cp->ibuf);
    }
}
#endif /* LINK */

struct cmdtable {
    char  *name;
    void (*fnc)(struct convection *);
    int  states;
};
struct cmdtable DFAR cmdtable[] = {
    { "?",        help_command,       CM_USER },
    { "bye",      bye_command,        CM_USER },
    { "channel",  channel_command,    CM_USER },
    { "exit",     bye_command,        CM_USER },
    { "help",     help_command,       CM_USER },
    { "invite",   invite_command,     CM_USER },
#ifdef LINK
    { "links",    links_command,      CM_USER },
#endif
    { "msg",      msg_command,        CM_USER },
    { "name",     name_command,       CM_UNKNOWN },
    { "personal", personal_command,   CM_USER },
    { "quit",     bye_command,        CM_USER },
    { "sounds",   flags_command,      CM_USER },
    { "verbose",  flags_command,      CM_USER },
    { "version",  version_command,    CM_USER },
    { "who",      who_command,        CM_USER },
    { "write",    msg_command,        CM_USER },

#ifdef LINK
    { "\377\200cmsg", h_cmsg_command,     CM_HOST },
    { "\377\200host", h_host_command,     CM_UNKNOWN },
    { "\377\200invi", h_invi_command,     CM_HOST },
    { "\377\200loop", h_loop_command,     CM_HOST },
#ifdef CNV_TOPICS
    { "\377\200topi", h_topi_command,     CM_HOST },
#endif
    { "\377\200udat", h_udat_command,     CM_HOST },
    { "\377\200umsg", h_umsg_command,     CM_HOST },
    { "\377\200user", h_user_command,     CM_HOST },
    { "\377\200ping", h_ping_command,     CM_HOST },
#endif /* LINK */
    { 0,      0,          0 }
};

static void
process_commands(cp,m)
struct convection *cp;
struct mbx *m;
{
    char arg[LINELEN];
    int arglen,size;
    char *ccp;
    struct cmdtable *cmdp;
	int do_goto_loop;

    for (;;)
	{
        /* loop: replaced this LABEL with continue statements, etc */

        if(cp->type == CT_CLOSED)
            break;
        j2setflush(cp->fd,'\n');
        usflush(cp->fd);
        memset(cp->ibuf,0,LINELEN);
        if(cp->type != CT_HOST)
            j2alarm(Ctdiscinit * 1000L);
        if((size = recvline(cp->fd,cp->ibuf,LINELEN-1)) <= 0)
            break;
        j2alarm(0);
        cp->received += size;
#ifdef oldlocks
        clear_locks();
        cp->locked = 1;  /* so user doesn't get own msg */
#endif
        if(*cp->ibuf == '/')
		{
            ccp = &cp->ibuf[1];
            arg[0] = '\0';
            sscanf(ccp,"%s",arg);
            arglen = strlen(arg);
#ifdef riskcmdoutlossage
            /* We are about to parse a command; most likely there
             * is alot of output; try to avoid fragmenting that
             * data by doing our own flushing ! - WG7J
             */
            j2setflush(cp->fd,-1);
/* n5knx: VE3DTE, N2RJT et alia, find data losses occur if we stop flushing
          after NL.  This must surely be a bug, but where?
          Ans: we probably exceeded tx blocking threshold, so dropped data
               due to NOTXBLOCK flag we set. */
#endif
			do_goto_loop = 0;
            for(cmdp = cmdtable; cmdp->name; cmdp++)
			{
                if(!strncmpi(cmdp->name,arg,arglen))
				{
                    if(cmdp->states & (1 << cp->type))
                        (*cmdp->fnc)(cp);
                    do_goto_loop = 1;
					break;
                }
            }
			/* this replaces the use of GOTO 'loop:' label */
			if (do_goto_loop)
				continue;

            if(cp->type == CT_USER)
                cp->xmitted += usprintf(cp->fd,
                "*** Unknown command '/%s'. Type /HELP for help.\n",arg);
#ifdef LINK
            else  /* didn't find match in table - may be a host command */
                if (cp->ibuf[1] == '\377' && cp->ibuf[2] == '\200')
                    h_unknown_command(cp);  /* yep, pass it on */
#endif
            /* goto loop; a continue should suffice at this location */
			continue;
        }
        rip(cp->ibuf);
        if(
#ifdef CNV_STRICT_ASCII
           /* disallow non-printing ASCII ... but also catches chars > 0x7e, hence disallows Int'l chars */
           isprint(cp->ibuf[0]) &&
#endif
           cp->type == CT_USER)
            send_msg_to_channel(cp->name,cp->channel,cp->ibuf,cp);
    }
    bye_command(cp);
#ifdef noblocking
    sockblock(cp->fd,SOCK_BLOCK);
#endif
    free_closed_connections();
}

/* Incoming convers session */
void
conv_incom(s,t,p)
int s;
void *t;
void *p;
{
    struct convection *cp;

    sockowner(s,Curproc);   /* We own it now */
    sockmode(s,SOCK_ASCII);  /* n5knx: was SOCK_BINARY */
#ifdef noblocking
    sockblock(s,SOCK_NOTXBLOCK);   /* prevent backlogs ! */
#endif
    cp = alloc_connection(s);
    conv_incom2(s,t,p,cp);
}

#ifdef XCONVERS
/* Incoming LZW convers session */
void
xconv_incom(s,t,p)
int s;
void *t;
void *p;
{
    struct convection *cp;

    sockowner(s,Curproc);	/* We own it now */
    sockmode(s,SOCK_BINARY);

#ifdef DONT_COMPILE
#ifdef noblocking
/* N5KNX: VERY BAD TO RISK LOSING LZW DATA...can't decode! */
    sockblock(s,SOCK_NOTXBLOCK);   /* prevent backlogs ! */
#endif
#endif

    cp = alloc_connection(s);
    lzwinit(s,Lzwbits,Lzwmode);
    cp->flags |= USE_LZW;
    conv_incom2(s,t,p,cp);
}
#endif

static void
conv_incom2(s,t,p,cp)
int s;
void *t;
void *p;
struct convection *cp;
{
    struct permlink *pl;
    int pwdignore;
    long privs;

	long t_l = (long)t; /* 01Oct2009, Maiko, bridging var for 64 bit warning */

    cp->channel = CDefaultChannel;

#ifdef LINK
    for(pl = permlinks; pl; pl = pl->next)
        if(pl->fd == s) {
            pl->convection = cp;
            cp->xmitted += usprintf(s,HOSTcmd,Chostname, shortversion, myfeatures);
            break;
        }

	/* 31Aug2016, Maiko (VE4KLM), give an indication in the log, never was */
	log (s, "convers [%s] session started", pl->name);

    if(pl == NULLPERMLINK) {
#endif /* LINK */
#ifdef AX25
        if ((int)t_l == AX25TNC) { /* figure out call from socket */
            struct usock *up;
            char *chrp;

            if((up = itop(s)) == NULLUSOCK)
			{
                cp->type = CT_CLOSED;
                free_closed_connections();
                return;
            }
            /* sockmode(s,SOCK_ASCII); should already be ASCII */
            pax25(cp->name,up->cb.ax25->remote);
            if((chrp=strchr(cp->name,'-')) != NULL)
                *chrp = '\0';
            strlwr(cp->name);
            strcpy(cp->host,Chostname);

            /* now check with ftpusers file - WG7J/N5KNX */
            pwdignore = 1;
            privs = userlogin(cp->name,NULLCHAR,&(cp->ibuf),LINELEN,&pwdignore,"confperm");
            if(privs & NO_CONVERS) {  /* true if privs==-1 too */
                usputs(s,Noperm);
                cp->type = CT_CLOSED;
                free_closed_connections();
                return;
            }

            cp->type = CT_USER;
            cp->xmitted += usprintf(s,
            "Conference @ %s  Type /HELP for help.\n",Chostname);
            SendMotd(s);
#ifdef oldlocks
            clear_locks();
            cp->locked = 1; /* send to everyone but ourself */
#endif
            send_user_change_msg(cp->name,cp->host,-1,CDefaultChannel,cp);
            set_personal(cp);
            ConvUsers++;
        } else
#endif /* AX25 */
            usputscnt(cp->fd,"\nPlease login with '/n <call> [channel #]'\n\n");
#ifdef LINK
    }
#endif
    process_commands(cp,NULLMBX);

	/* 31Aug2016, Maiko (VE4KLM), give an indication in the log, never was */
	log (s, "convers [%s] session terminated", pl->name);
}

#ifdef MAILBOX

/* this is for Mailbox users */
void
mbox_converse(struct mbx *m,int channel)
{
    struct convection *cp;
    int oldflush;
#ifdef CNV_CHAN_NAMES
    char *xp;
	UNUSED (xp);	/* suppress warning, VE4KLM, 14Apr2016 */
#endif

#ifdef noblocking
    sockblock(m->user,SOCK_NOTXBLOCK);  /* prevent backlogs ! */
#endif
    oldflush = j2setflush(m->user,'\n');  /* automatic line flushing */
    cp = alloc_connection(m->user);
    cp->channel = channel;
    strcpy(cp->name,m->name);
    strcpy(cp->host,Chostname);
    cp->type = CT_USER;
    cp->flags &= ~CLOSE_SOCK;     /* do not close socket on exit */
    cp->xmitted += usprintf(m->user,
    "Conference @ %s  Type /HELP for help.\n",Chostname);
    SendMotd(m->user);
#ifdef oldlocks
    clear_locks();
    cp->locked = 1; /* send to everyone but ourself */
#endif
    send_user_change_msg(cp->name,cp->host,-1,cp->channel,cp);
    set_personal(cp);
    ConvUsers++;

#ifdef CNV_LOCAL_CHANS
    if (ISLOCAL(cp->channel,xp))
        cp->xmitted += usputscnt(cp->fd,clocal);
    if (ISMESSAGE(cp->channel,xp))
        cp->xmitted += usputscnt(cp->fd,cmessage);
#endif
    process_commands(cp,m);

    j2setflush(m->user,oldflush);

}
#endif /* MAILBOX */

#endif /* CONVERS */
