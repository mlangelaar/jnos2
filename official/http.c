/* HTTP server
 * Almost completely rewritten. I borrowed extensively from the ideas
 * and code of Brian Lantz' (brian@lantz.com) adaptation of the http
 * server for Tnos which was based loosely on my previous version of 
 * the HTTP server which was derived from Ashok Aiyar's server
 * <ashok@biochemistry.cwru.edu>, which was based on
 * Chris McNeil's <cmcneil@mta.ca> Gopher server, which was based in
 * part on fingerd.c
 *
 *                          5/6/1996 Selcuk Ozturk
 *                                   seost2+@pitt.edu
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef MSDOS
#include <dir.h>
#include <dos.h>
#endif
#include <sys/stat.h>
#include <ctype.h>
#include "global.h"
#ifdef HTTP
#include "files.h"
#include "mbuf.h"
#include "socket.h"
#include "session.h"
#include "proc.h"
#include "dirutil.h"
#include "commands.h"
#include "mailbox.h"
#include "netuser.h"
#include "ftp.h"
#include "smtp.h"
#ifdef MSDOS
#include <alloc.h>
#endif
#include "cmdparse.h"
#include "domain.h"

/*
 * 15Oct2019, Maiko, Oops, need to include tcp.h, which is not included
 * anymore in some of the 'higher level' header files, due to conflicts
 * discovered in the tun code, and possible other finds down the road.
 */
#include "tcp.h"

#ifdef __GNUC__
struct reqInfo;   /* forward def for GCC */
#endif

/* function prototypes */
static void http __ARGS((int s,void *unused,void *p));
static int mkwelcome __ARGS((int s, struct reqInfo *rq));
static int dohttpstatus __ARGS((int argc,char *argv[],void *p));
static int dohttptdisc __ARGS((int argc,char *argv[],void *p));
static int dohttpsim __ARGS((int argc,char *argv[],void *p));
static int dohttpmax __ARGS((int argc,char *argv[],void *p));
static int dohttpabs __ARGS((int argc,char *argv[],void *p));
static int dohttpalways __ARGS((int argc,char *argv[],void *p));
static int dohttpmulti __ARGS((int argc,char *argv[],void *p));
#ifdef HTTP_EXTLOG
static int dohttpdontlog __ARGS((int argc,char *argv[],void *p));
#endif
static void httpHeaders __ARGS((int s, int resp, struct reqInfo *rq));
static void httpFileInfo __ARGS((int s,FILE *fp, int vers, int type));
static int getmonth __ARGS((char *cp));
static int isnewer __ARGS((time_t thetime,char *tmstr));
static long countusage __ARGS((int s, const char *file,  int display, int increase, int create));
static void sendhtml __ARGS((FILE *fp, int s, char *buf, int buflen, struct reqInfo *reqInfo));
static void file_or_virtual __ARGS((char *cp, struct reqInfo *rq));
char *gmt_ptime __ARGS((time_t *thetime));
static void httpError __ARGS((int s,int resp,int msg,char *str,struct reqInfo *rq));
static void process_cgi __ARGS((int s,struct reqInfo *reqInfo));
/* also called from gopher.c */
int authorization __ARGS((int s, char *buf,struct reqInfo *reqInfo));
char *decode __ARGS((char *string, int isPath));
#ifdef HTTP_EXTLOG
void wwwlog(int s, struct reqInfo *rq);
#endif

/* Allow for changing the standard index file name */
#ifndef HTTP_INDEX_NAME
#define HTTP_INDEX_NAME "welcome"
#define HTTP_ROOTINDEX_NAME "root"
#else
#define HTTP_ROOTINDEX_NAME HTTP_INDEX_NAME
#endif

/* n5knx: under Unix FA_DIREC alone will fail to findfirst() a dir */
#ifdef UNIX
#define REGFILE (FA_HIDDEN|FA_SYSTEM|FA_DIREC)
#else
#define REGFILE (FA_DIREC)
#endif

/* HTTP Method defines */
#define METHOD_HEAD     0   /* this one must be defined 0 */
#define METHOD_GET      1
#define METHOD_POST     2
#define METHOD_HTML     3   /* used when a cgi func is called from inside a html doc */
#define METHOD_UNKNOWN  -1


#define HLINELEN    256

struct reqInfo {
    int index;
    int method;
    int version;
    int qsize;
    int response;
    char *myname;
    char *url;
    char *arg;
    char *query;
    char *newcheck;
    char *from;
    char *referer;
    char *agent;
    char *passwd;
    char file[128];
};

struct cgi {
    char *name; /* name of the cgi program, ie. path portion of the URL */
    int  (*func) __ARGS((int s, struct reqInfo *rq));
};



#if 0
/***********************    CGI Defines   ****************************/ 
/* #define these features in config.h just after the #define HTTP */
#undef CGI_XBM_COUNTER  1
#undef CGI_POSTLOG      1
#endif

/**********************   CGI Prototypes   ***************************/
#ifdef CGI_XBM_COUNTER
static int xbm_counter(int s,struct reqInfo *rq);
#endif
#ifdef CGI_POSTLOG
static int postlog(int s, struct reqInfo *rq);
#endif

/**********************   CGI Registry    *****************************/
struct cgi Cgis[] = {
#ifdef CGI_XBM_COUNTER
    { "counter.xbm",        xbm_counter},
#endif
#ifdef CGI_POSTLOG
    { "postlog",            postlog},
#endif
    { NULLCHAR, NULL} /* This must be the last entry */
};
/***********************************************************************/


static char *Cgi_Error = NULLCHAR;
static char DFAR errorstr[] = "[error in directive]";

#ifdef UNIX
static char DFAR entry[] = "%-6s<a href=\"/%s%s%s\">%32s</a>%15lu bytes   %02d %s %04d\n";
#else
static char DFAR entry[] = "%-6s<a href=\"/%c:%s%s%s\">%13s</a>%15lu bytes   %02d %s %04d\n";
#endif

int HttpUsers = 0;
static int Httpmax = 10;
static int Httpsimc = 5;
static int32 Httptdisc = 180;
static int AbsInclude = 0;
static int htmlAlwaysModified = 1;
static int MultiHomed = 0;

extern char  *Days[];

/* HTTP Header lines sent or processed */
#define HDR_TYPE	0
#define HDR_LEN	        1
#define HDR_MIME	2
#define HDR_SERVER	3
#define HDR_LOCATION	4
#define HDR_DATE	5
#define HDR_MODIFIED	6
#define HDR_SINCE	7
#define HDR_REF 	8
#define HDR_AGENT	9
#define HDR_FROM        10
#define HDR_AUTHEN      11
#define HDR_AUTH        12
static const char * DFAR wHdrs[] = {
	"Content-Type:",
	"Content-Length:",
	"MIME-version:",
	"Server:",
	"Location:",
	"Date:",
	"Last-Modified:",
	"If-Modified-Since:",
	"Referer:",
	"User-Agent:",
        "From:",
        "WWW-Authenticate:",
        "Authorization:"
};

/* HTTP response codes */

static const char * DFAR HttpResp[] = {
#define RESP_200    0
    "200 Ok",
#define RESP_202    1
    "202 Accepted",
#define RESP_204    2
    "204 No Content",
#define RESP_301    3
    "301 Moved",
#define RESP_302    4
    "302 Found",
#define RESP_304    5
    "304 Not Modified",
#define RESP_400    6
    "400 Bad Req.",
#define RESP_401    7
    "401 Unauthorized",
#define RESP_403    8
    "403 Forbidden",
#define RESP_404    9
    "404 Not Found",
#define RESP_500    10
    "500 Internal Error",
#define RESP_501    11
    "501 Not Implemented",
#define RESP_503    12
    "503 Busy",
#define RESP_601    13
    "601 Timed Out"  // This is not really a HTTP/1.0 response; only used for logging
};

static char * DFAR HttpMsg[] = {
#define MSG_301     0
#define MSG_302     0
    "<TITLE>MOVED</TITLE><H1>Moved</H1>Requested document moved, <A HREF=\"%s\">click here</A>.",
#define MSG_400     1
    "<TITLE>BAD</TITLE><H1>Bad Request</H1>Reason: %s.",
#define MSG_401     2
    "<TITLE>AUTH</TITLE><H1>Authorization failed.</H1>",
#define MSG_403     3
    "<TITLE>AUTH</TITLE><H1>Forbidden</H1>",
#define MSG_404     4
    "<TITLE>ERROR</TITLE><H1>Not Found</H1>Requested document '/%s' not found.",
#define MSG_500     5
    "<TITLE>ERROR</TITLE><H1>Internal Error</H1>%s.",
#define MSG_501     6
    "<TITLE>ERROR</TITLE><H1>Not Implemented</H1>The method \"%s\" is not supported."
};

 
/* First component of MIME types */

static const char * DFAR CTypes1[] = {
#define TYPE1_VID   0
  "video/",
#define TYPE1_TEXT  1
  "text/",
#define TYPE1_IMG   2
  "image/",
#define TYPE1_AUD   3
  "audio/",
#define TYPE1_APP   4
  "application/"
};


/* Struct for looking up MIME types from file extensions */
struct FileTypes {
	const char *ext;
	short type1;
	const char *type2;
};

static struct FileTypes DFAR HTTPtypes[] = {
	/* Other 'special' entries can be added in front of 'html',
	 * but care must be taken to make sure that the defines for
	 * T_binary, T_plain, T_form, and T_html are correct
	 * and that no entries with a 'NULLCHAR' ext come after 'html'
	 * except for the one that ends the table.
	 */
#define T_binary 0
	{ NULLCHAR,	4,	"octet-stream" },
#define T_plain 1
	{ NULLCHAR,	1,	"plain" },
#define T_form 2
	{ NULLCHAR,	4,	"x-www-form-urlencoded" },
#define T_html 3
	{ "html",	1,	"html" },
#define T_htm  4
        { "htm",        1,      "html" },
	/* the following can be in any order */
#define T_avi    5
	{ "avi",	0,	"x-msvideo" },
#define T_qt     6
	{ "qt",		0,	"quicktime" },
#define T_mov    7
	{ "mov",	0,	"quicktime" },
#define T_mpeg   8
	{ "mpeg",	0,	"mpeg" },
#define T_mpg    9
	{ "mpg",	0,	"mpeg" },
#define T_rtx   10
	{ "rtx",	1,	"richtext" },
#define T_xpm   11
	{ "xpm",	2,	"x-xpixmap" },
#define T_xbm   12
	{ "xbm",	2,	"x-xbitmap" },
#define T_rgb   13
	{ "rgb",	2,	"x-rgb" },
#define T_ppm   14
	{ "ppm",	2,	"x-portable-pixmap" },
#define T_pgm   15
	{ "pgm",	2,	"x-portable-graymap" },
#define T_pbm   16
	{ "pbm",	2,	"x-portable-bitmap" },
#define T_pnm   17
	{ "pnm",	2,	"x-portable-anymap" },
#define T_ras   18
	{ "ras",	2,	"x-cmu-raster" },
#define T_tiff  19
	{ "tiff",	2,	"tiff" },
#define T_tif   20
        { "tif",        2,      "tiff" },
#define T_jpeg  21
	{ "jpeg",	2,	"jpeg" },
#define T_jpg   22
	{ "jpg",	2,	"jpeg" },
#define T_gif   23
	{ "gif",	2,	"gif" },
#define T_wav   24
	{ "wav",	3,	"x-wav" },
#define T_aiff  25
	{ "aiff",	3,	"x-aiff" },
#define T_aif   26
        { "aif",        3,      "x-aiff" },
#define T_au    27
	{ "au",		3,	"basic" },
#define T_snd   28
	{ "snd",	3,	"basic" },
#define T_tar   29
	{ "tar",	4,	"x-tar" },
#define T_man   30
	{ "man",	4,	"x-trof-man" },
#define T_rtf   31
	{ "rtf",	4,	"rtf" },
#define T_eps   32
	{ "eps",	4,	"postscript" },
#define T_ps    33
	{ "ps",		4,	"postscript" },
#define T_sit   34
	{ "sit",	4,	"x-stuffit" },
#define T_hqx   35
	{ "hqx",        4,	"mac-binhex40" },
#define T_fif   36
	{ "fif",	4,	"fractals" },
#define T_zip   37
	{ "zip",	4,	"x-zip" },
#define T_gz    38
	{ "gz",		4,	"x-gzip" },
#define T_z     39
	{ "z",		4,	"x-compress" },
	{ NULLCHAR,	-1,	NULLCHAR }
};


/* Struct to keep tract of ports defined and their root directory */

#define MAXPORTS 5
struct portsused {
	int	port;
	char	DR;
	char	*dirname;
};
static struct portsused ports[MAXPORTS];


static struct cmds DFAR Httpcmds[] = {
        { "absinclude", dohttpabs,      0, 0, NULLCHAR },
        { "always",     dohttpalways,   0, 0, NULLCHAR },
#ifdef HTTP_EXTLOG
        { "dontlog",    dohttpdontlog,  0, 0, NULLCHAR },
#endif
	{ "maxcli",	dohttpmax,	0, 0, NULLCHAR },
        { "multihomed", dohttpmulti,    0, 0, NULLCHAR },
	{ "simult",	dohttpsim,	0, 0, NULLCHAR },
	{ "status",	dohttpstatus,	0, 0, NULLCHAR }, 
	{ "tdisc",	dohttptdisc,	0, 0, NULLCHAR },
	{ NULLCHAR,	NULL,		0, 0, NULLCHAR }
};


int
dohttp (argc, argv, p)
int argc;
char *argv[];
void *p;
{
	return subcmd (Httpcmds, argc, argv, p);
}

#ifdef HTTP_EXTLOG
static char *Dont_log = NULLCHAR;

int
dohttpdontlog(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2) {
        tprintf("Don't log: %s\n",Dont_log ? Dont_log : "");
        return 0;
    }
    free(Dont_log);
    Dont_log = j2strdup(argv[1]);
    return 0;
}
#endif


int
dohttpstatus (argc, argv, p)
int argc;
char *argv[];
void *p;
{
int k;
struct cgi *cgip;

	for (k = 0; k < MAXPORTS; k++)	{
		if (ports[k].port)	{
#ifdef UNIX
			tprintf ("HTTP Server on port #%-4u - %s\n", ports[k].port, ports[k].dirname);
#else
			tprintf ("HTTP Server on port #%-4u - %c:%s\n", ports[k].port, ports[k].DR,ports[k].dirname);
#endif
		}
	}
        j2tputs("\nWith CGI functions:\n");
        for(cgip = Cgis;cgip->name != NULLCHAR;cgip++){
            tprintf("%31s\n",cgip->name);
        }
        tputc('\n');

        dohttpabs (0, argv, p);
	dohttpalways(0,argv,p);
        dohttpmax (0, argv, p);
	dohttpsim (0, argv, p);
	dohttptdisc (0, argv, p);
#ifdef HTTP_EXTLOG
        dohttpdontlog(0, argv,p);
#endif
	return 0;
}


static int
dohttpabs(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&AbsInclude, "Abs. include allowed", argc, argv);
}

static int
dohttpalways(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&htmlAlwaysModified, "Always send HTMLs", argc, argv);
}

static int
dohttpmax (argc, argv, p)
int argc;
char *argv[];
void *p;
{
	return setint (&Httpmax, "Max. HTTP connects", argc, argv);
}

static int
dohttpmulti(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&MultiHomed, "This server is multi-homed", argc, argv);
}

static int
dohttpsim (argc, argv, p)
int argc;
char *argv[];
void *p;
{
	return setint (&Httpsimc, "Simult. HTTP conn.'s serviced", argc, argv);
}

static int dohttptdisc (int argc, char **argv, void *p)
{
	return setint32 (&Httptdisc, "HTTP Server tdisc (sec)", argc, argv);
}

static int Firstserver = 1;

/* Start up http service */
/* Usage: "start http [port#] [drive_letter] [root_directory] */
int
httpstart (argc, argv, p)
int argc;
char *argv[];
void *p;
{
int port, k;

	if(argc < 2)
		port = IPPORT_HTTP;
	else
		port = atoi(argv[1]);

        if(Firstserver) {                   // initialize
            Firstserver = 0;
            for(k = 0; k < MAXPORTS; k++)
                ports[k].port = 0;
        }
        
	for (k = 0; k < MAXPORTS; k++) {
		if (ports[k].port == port)	{
			tprintf ("You already have an HTTP server on port #%d!\n", port);
			return 0;
		}
	}
	for (k = 0; k < MAXPORTS; k++) {
		if (!ports[k].port)
			break;
	}
	if (k == MAXPORTS)	{
		tprintf ("All %d HTTP ports are assigned!\n", MAXPORTS);
		return 0;
	}

        if(argc < 3)
            ports[k].DR = 'c';
        else
            ports[k].DR = argv[2][0];
            
	if(argc < 4)
		ports[k].dirname = j2strdup (Httpdir);
	else
		ports[k].dirname = j2strdup (argv[3]);
                

	ports[k].port = port;
	return (start_tcp(port,"HTTP Server", http, 1560));
}


int
http0 (argc, argv, p)
int argc;
char *argv[];
void *p;
{
int port, k;

	if(argc < 2)
		port = IPPORT_HTTP;
	else
		port = atoi(argv[1]);
	for (k = 0; k < MAXPORTS; k++) {
		if (ports[k].port == port)	{
			ports[k].port = 0;
			free (ports[k].dirname);
			ports[k].dirname = NULLCHAR;
			return (stop_tcp(port));
		}
	}
	tprintf ("No HTTP server was found on port #%d!\n", port);
	return 0;
}



static int get_p_index(int s)
{
    int i;
    struct sockaddr_in lsocket;

    i = SOCKSIZE;
    j2getsockname(s,(char *)&lsocket,&i);
    for(i=0;i<MAXPORTS;i++)
        if(ports[i].port == lsocket.sin_port)
            break;
    return i;
}

/* 28Dec2004, Maiko, Replaces GOTO 'quit' and 'quit0' LABELS */
static void doquit (FILE *fp, int s, struct reqInfo *rq)
{
	if(fp)
		fclose(fp);

#ifdef HTTP_EXTLOG
	wwwlog(s,rq);
#endif

	free(rq->myname);
	free(rq->url);
	free(rq->query);
	free(rq->newcheck);
	free(rq->from);
	free(rq->referer);
	free(rq->agent);
	free(rq->passwd);   /* never free rq.arg & rq.file */
        
	countusage (s, "tcount.dat", 0, 1, 1);
 
	close_s (s);
	HttpUsers--;
	j2psignal (&HttpUsers,1);
}

static void
http (s, v1, p)
int s;
void *v1;
void *p;
{

    int  length, err = 1, qsize = 0;
    char Inline[HLINELEN], *cp;
#ifndef UNIX
	char DR;
#endif
    char *dirname, *tquery, *queryp = NULLCHAR;
    struct stat sb;
    struct reqInfo rq;
    struct tcb *tcbp;
    FILE *fp = NULLFILE;
	
    sockowner (s, Curproc);
    sockmode (s, SOCK_ASCII);
    HttpUsers++;

    memset(&rq,0,sizeof(rq));

    if (Httpmax < HttpUsers) {
        rq.response = RESP_503;
		return (doquit (fp, s, &rq));
    }

    if(HttpUsers > Httpsimc)
	pwait(&HttpUsers);

    j2alarm (Httptdisc*1000);
    if (recvline (s, Inline, HLINELEN) == -1) {
        rq.response = RESP_601;
		return (doquit (fp, s, &rq));
    }
    j2alarm (0);


    rip(Inline);
    rq.index = get_p_index(s);
    log(s,"HTTP #%u: %s",ports[rq.index].port,Inline);
        
    if((cp = strstr(Inline," HTTP/1.")) != NULLCHAR) { /* allow 1.x not just 1.0 - k2mf */
        rq.version = 1;
        *cp = 0;
    }
        
    /* Inline truncated to "METHOD /URI" */
    cp = skipnonwhite(Inline);
    if((queryp = strchr(cp,'?')) != NULLCHAR)
        *queryp++ = 0;
        
    /* Let's check if we have a query with the GET method */
    if(queryp && *queryp) {
            rq.query = decode(queryp,0);           
            rq.qsize = strlen(rq.query);
    }

    /* only path portion is left at cp */
    if(strlen(cp) > 2) { 
        *cp++ = 0;
        rq.url = decode(++cp,1); // drop the initial '/'
    } else {
        *cp = 0;
        rq.url = callocw(1,2); // assume root.html, we are allocating memory
                                    // since that is assumed later
    }
        
    length = strlen (rq.url);
        
    if(!strcmp(Inline,"GET"))
        rq.method = METHOD_GET;
    else if(!strcmp(Inline,"HEAD"))
        rq.method = METHOD_HEAD;
    else if(!strcmp(Inline,"POST"))
        rq.method = METHOD_POST;
    else {
        rq.method = METHOD_UNKNOWN;
        httpError(s, RESP_501, MSG_501, Inline, &rq);
		return (doquit (fp, s, &rq));
    }


    /* if HTTP 0.9 (no version string), only 'GET' is allowed */
    /* also, don't allow query */
    if (!rq.version && (rq.method != METHOD_GET || queryp)) {
		httpError (s, RESP_400, MSG_400,"Invalid HTTP/0.9 request", &rq);
		return (doquit (fp, s, &rq));
    }

    /* We don't allow urls with'?'in HEAD & POST methods. */
    /* Reason: Spec unclear and I'm lazy */
    if((rq.method != METHOD_GET) && queryp) {
        httpError(s,RESP_400,MSG_400,"Invalid URI for 'HEAD' or 'POST'",&rq);
		return (doquit (fp, s, &rq));
    }
    
	/* This is Version 1.0, so get all the header info */
    if (rq.version) {
        while (j2alarm (Httptdisc*1000),recvline (s,Inline,HLINELEN) != -1) {
	   j2alarm (0);
           rip (Inline);
	   if(!*Inline) {
                if(qsize && rq.method == METHOD_POST) {
                    tquery = (char *)mallocw(qsize+3);
                    *tquery = 0;
                    j2alarm(Httptdisc*1000);
                    if(recvline(s,tquery,qsize+3) == -1) {
                        free(tquery);
                        rq.response = RESP_601;
						return (doquit (fp, s, &rq));
                    }
                    j2alarm(0);
                    if(*tquery)
                        rq.query = decode(tquery,0);
                    free(tquery);
                    rq.qsize = strlen(rq.query); // correct the query size
                }
                break;
           }
           if(!strnicmp(Inline,wHdrs[HDR_LEN],strlen(wHdrs[HDR_LEN]))) {
                qsize = atoi(&Inline[strlen(wHdrs[HDR_LEN])]);
           } else if(!strnicmp(Inline,wHdrs[HDR_SINCE],strlen(wHdrs[HDR_SINCE]))) {
                rq.newcheck = j2strdup(&Inline[strlen(wHdrs[HDR_SINCE])]);
           } else if(!strnicmp(Inline,wHdrs[HDR_FROM],strlen(wHdrs[HDR_FROM]))) {
                rq.from = j2strdup(&Inline[strlen(wHdrs[HDR_FROM])+1]);
           } else if(!strnicmp(Inline,wHdrs[HDR_REF],strlen(wHdrs[HDR_REF]))) {
                rq.referer = j2strdup(&Inline[strlen(wHdrs[HDR_REF])+1]);
           } else if(!strnicmp(Inline,wHdrs[HDR_AGENT],strlen(wHdrs[HDR_AGENT]))) {
                rq.agent = j2strdup(&Inline[strlen(wHdrs[HDR_AGENT])+1]);
           } else if(!strnicmp(Inline,wHdrs[HDR_AUTH],strlen(wHdrs[HDR_AUTH]))) {
                rq.passwd = j2strdup(&Inline[strlen(wHdrs[HDR_AUTH])+7]); 
           } /* We don't care about the rest */
        }
    }

    if(MultiHomed) {
        for(tcbp = Tcbs; tcbp != NULLTCB; tcbp = tcbp->next) {
            if(s == tcbp->user) {
                rq.myname = resolve_a(tcbp->conn.local.address,0);
                break;
            }

        }
        if(!rq.myname)
            rq.myname = j2strdup(Hostname);
    } else
        rq.myname = j2strdup(Hostname);

    if(!authorization(s,Inline,&rq)) {
		return (doquit (fp, s, &rq));
    }

    pwait(NULL); /* Let's be nice to others */
    
    if(rq.query) {
        process_cgi(s,&rq);
		return (doquit (fp, s, &rq));
    }
    
    /* The only way we can get to this point with a POST command is through
       a badly formed POST command */
       
    if(rq.method == METHOD_POST) {
        httpError(s,RESP_400,MSG_400,"POST request w/o Content-Length",&rq);
		return (doquit (fp, s, &rq));
    }
    
    dirname = ports[rq.index].dirname;
    
#ifdef UNIX
    if (length == 0 || !strcmp(rq.url,"/"))	{
	sprintf(rq.file,"%s/" HTTP_ROOTINDEX_NAME ".html", dirname);
        if((fp = fopen(rq.file,READ_BINARY)) == NULLFILE) {
            sprintf(rq.file,"%s/", dirname);  /* VK2ANK: dir listing unless root index is readable */
        } else fclose(fp);
    } else {
	sprintf (rq.file, "%s/%s", dirname, rq.url);
    }
#else
    DR = ports[rq.index].DR;	/* 15Apr2016, Maiko, moved here, warnings */
    if (length == 0 || !strcmp(&rq.url[1],":/"))	{
	sprintf(rq.file,"%c:%s/" HTTP_ROOTINDEX_NAME ".html", (rq.url[1] == ':') ? rq.url[0] : DR, dirname);
        if((fp = fopen(rq.file,READ_BINARY)) == NULLFILE) {
            cp = strrchr(rq.file,'/');  /* VK2ANK: dir listing unless root.html readable */
            *++cp = '\0';  /* drop root.html suffix */
        } else fclose(fp);
    } else if (rq.url[1] == ':') {
        sprintf (rq.file, "%c:%s%s", rq.url[0], dirname, &rq.url[2]);
        /* VK2ANK: drive letter must match if we have nul dirname, ie, DR:/ */
        if (rq.file[0] != DR && strlen(dirname) == 0)  {
            httpError (s, RESP_403, MSG_403, rq.url, &rq);
			return (doquit (fp, s, &rq));
        }
    } else {
	sprintf (rq.file, "%c:%s/%s", DR, dirname, rq.url);
    }
/* n5knx: note user supplies drive name and a path, and we prefix the path
 * with 'dirname', thus limiting access to a subtree.  But, that subtree on
 * ANY drive is accessible via http protocol!  When 'dirname' is "", we are
 * making the whole drive accessible, so Neil added a test to only do this
 * for the drive we gave the 'start http' cmd.  With large disk drives now
 * common, we ought to rethink allowing all drives to be accessed...
 */
#endif

#ifdef notdef
    /* decode handles this now */
    /* delete any .. to avoid */
    /* anyone telnetting to http and cd'ing to a directory */
    /* other than within Httpdir */

    for ( i=0; i <= (int) strlen(rq.file); i++ ) {
        if (rq.file[i] == '.' && rq.file[i+1] == '.') {
            rq.file[i] = ' ';
            rq.file[i+1] = ' ';
	}
    }
#endif

    cp = &rq.file[strlen(rq.file)-1];
    if (*cp == '/') {
        strcat(rq.file,HTTP_INDEX_NAME ".nhd");
        if((fp = fopen(rq.file,READ_BINARY)) != NULLFILE) {
            rq.version = 0;                    // send this w/o headers
        } else {
            *++cp = 0;
            strcat (rq.file, HTTP_INDEX_NAME ".html");
	    if ((fp = fopen (rq.file, READ_BINARY)) == NULLFILE)	{
                err = mkwelcome (s,&rq);
	    }
        }
    }
	else if ((fp = fopen(rq.file,READ_BINARY)) == NULLFILE
				&& !access(rq.file,0))
	{
        sprintf(Inline,"http://%s:%u/%s/",
			rq.myname,ports[rq.index].port,rq.url);

  		httpError (s, RESP_302, MSG_302,Inline,&rq);

		return (doquit (fp, s, &rq));
    }

    if (fp != NULLFILE)	{
        int thetype = -1;

	if ((cp = strrchr (rq.file, '.')) != NULLCHAR)	{
            cp++;
            for (thetype = T_html; HTTPtypes[thetype].ext != NULLCHAR; thetype++) {
                if (!strnicmp (HTTPtypes[thetype].ext, cp, strlen(HTTPtypes[thetype].ext)))
	           break;
            }
	}

            /* either had no extension, or extension not found in table */
        if (thetype == -1 || HTTPtypes[thetype].type1 == -1)
            thetype = (isbinary(fp)) ? T_binary : T_plain;

        fstat (fileno(fp), &sb);
        if(!htmlAlwaysModified || (thetype != T_html && thetype != T_htm))
		{
            if (rq.method && rq.newcheck
				&& !isnewer (sb.st_mtime, rq.newcheck))
			{
                httpHeaders(s,RESP_304,&rq);
                usputc(s,'\n');
				return (doquit (fp, s, &rq));
            }
        }
        httpHeaders (s,RESP_200,&rq);
        httpFileInfo (s, fp, rq.version, thetype);

        if (rq.method)	{
            if (thetype != T_html && thetype != T_htm)	/* if not html, just send */
               sendfile (fp, s, IMAGE_TYPE, 0, NULL);
            else	/* otherwise, scan for server-side includes */
               sendhtml (fp,s,Inline,HLINELEN,&rq);
        }
    } else if (err) {
        httpError(s,RESP_404,MSG_404,rq.url,&rq);
    }

	return (doquit (fp, s, &rq));
}

#ifdef HTTP_EXTLOG
void wwwlog(int s, struct reqInfo *rq)
{
    //va_list ap;
    int i;
    char *cp,nolog[HLINELEN],ML[FILE_PATH_SIZE];
    time_t t;
    struct sockaddr fsocket;
    FILE *fp;
    
    if(Dont_log) {
        cp = Dont_log;
        i = 0;
        
        do {
            if(*cp == '|' || !*cp) {
                nolog[i] = 0;
                if(strstr(rq->url,nolog))
                    return;
                i = 0;
            } else {
                nolog[i] = *cp;
                i++;
            }
            cp++;
        } while(*(cp-1)); 
    }
    
    time(&t);
    cp = ctime(&t);
#ifdef UNIX
    if (*(cp+8) == ' ') *(cp+8) = '0';  /* 04 Feb, not b4 Feb */
#endif
    sprintf(ML,"%s/%2.2s%3.3s%2.2s",HLogsDir,cp+8,cp+4,cp+22);
    if((fp = fopen(ML,APPEND_TEXT)) == NULLFILE)
        return;
    
    i = SOCKSIZE;
    fprintf(fp,"%9.9s",cp+11);
    if(j2getpeername(s,(char *)&fsocket,&i) != -1)
        fprintf(fp," %s",psocket(&fsocket));
    fputs(" - ",fp);
    fprintf(fp,"Port: %u\nURL: /%s\nHTTP: %s\nFILE: %s\nResponse: %s\n%s %s\n%s %s\n%s %s\n\n",
            ports[rq->index].port,
            rq->url,
            (rq->version) ? "1.0" : "0.9",
            rq->file,
            HttpResp[rq->response],
            wHdrs[HDR_FROM],rq->from,
            wHdrs[HDR_REF],rq->referer,
            wHdrs[HDR_AGENT],rq->agent);
    fclose(fp);
            
}
#endif
            
    
/* Return Date/Time in Arpanet format in passed string */
char * gmt_ptime(long *t)
{
    /* Print out the time and date field as
     *      "DAY day MONTH year hh:mm:ss ZONE"
     */
    register struct tm *ltm;
    static char str[40];

    
    /* Read GMT time */
    ltm = gmtime(t);
  
    /* rfc 822 format */
    sprintf(str,"%s, %.2d %s %04d %02d:%02d:%02d GMT",
	Days[ltm->tm_wday],
	ltm->tm_mday,
	Months[ltm->tm_mon],
	ltm->tm_year+1900,
	ltm->tm_hour,
	ltm->tm_min,
	ltm->tm_sec);
	return(str);
}

static void
httpFileInfo (s, fp, vers, type)
int s;
FILE *fp;
int vers;
int type;
{
struct stat sb;

    if (!vers)
        return;		/* it is 0.9, no headers sent */

    fstat (fileno(fp), &sb);
    usprintf (s, "%s %s\n", wHdrs[HDR_MODIFIED], gmt_ptime(&sb.st_mtime));
    usprintf (s, "%s %s%s\n", wHdrs[HDR_TYPE], CTypes1[HTTPtypes[type].type1], HTTPtypes[type].type2);

    /* because of the includes, size might be incorrect for html files, and
        that causes truncation by Netscape */
    if(type != T_html && type != T_htm)
        usprintf (s, "%s %lu\n", wHdrs[HDR_LEN], (unsigned long)sb.st_size);
    usputc(s,'\n');
}

static void
httpHeaders (s, resp, rq)
int s;
int resp;
struct reqInfo *rq;
{
    long thetime;
    extern char shortversion[];
    
    rq->response = resp;
    if (!rq->version)
	return;		/* it is 0.9, no headers sent */
    time (&thetime);
	
    usprintf (s, "HTTP/1.0 %s\n%s %s\n", HttpResp[resp], wHdrs[HDR_DATE], gmt_ptime(&thetime));
    usprintf (s, "%s 1.0\n%s JNOS/%s\n",wHdrs[HDR_MIME], wHdrs[HDR_SERVER],&shortversion[4]);

}

static void httpError(int s, int resp, int msg, char *str, struct reqInfo *rq)
{

    httpHeaders(s,resp,rq);
    if(rq->version) {
        if(msg == MSG_302) /* Redirection */
            usprintf(s,"%s %s\n",wHdrs[HDR_LOCATION],str);
        if(rq->method)
            usprintf (s, "%s %s%s\n", wHdrs[HDR_TYPE], CTypes1[TYPE1_TEXT],
                         HTTPtypes[T_html].type2);
    }
    usputc(s,'\n');
    if(rq->method)
        usprintf(s,HttpMsg[msg],str);
    return;
}
    
static 
int mkwelcome(int s, struct reqInfo *rq)
{
  char *dirstring,*p,*cp;
  struct ffblk ffblk;
  int done,oldmode;
  
  dirstring = (char *)mallocw(strlen(rq->file) + 32);

  strcpy(dirstring,rq->file);
  cp = strrchr(dirstring,'/'); 
  *cp++ = '\0';  /* drop last slash and all after; cp points after the '/' */

  /* dirstring must now exist AND be a dir.  findfirst() has trouble with the
     root dir so we manually test for X: (or NUL, if Unix) */
#ifdef UNIX
  if (strlen(dirstring) != 0)
#else
  if (strlen(dirstring) > 2 || *(cp-2) != ':')
#endif
  {
    if (findfirst(dirstring,&ffblk,REGFILE) || !(ffblk.ff_attrib & FA_DIREC))
	{ /* error */
      free(dirstring);
#ifdef UNIX
      findlast(&ffblk);  /* free allocs */
#endif
      return -1;
    }
#ifdef UNIX
    else findlast(&ffblk);  /* free allocs */
#endif
  }
  
  if(rq->method != METHOD_HTML) {  
    httpHeaders(s,RESP_200,rq);
    if(rq->version)
        usprintf(s,"%s %s%s\n\n",wHdrs[HDR_TYPE],CTypes1[TYPE1_TEXT],HTTPtypes[T_html].type2);

    if(!rq->method) {    // It's a HEAD request
        free(dirstring);
        return 0;
    }
  }

  
#ifdef notdef
  strcat(dirstring,"/wwwpre.cat");
  if((fp = fopen(dirstring,READ_BINARY)) != NULLFILE) {
    tmp = (char *)mallocw(HLINELEN);
    sendhtml(fp,s,tmp,HLINELEN,rq);
    free(tmp);
/*    sendfile(fp,s,IMAGE_TYPE,0,NULL); */
    fclose(fp);
    pre = 1;
  }
#endif


#ifdef UNIX
  strcat(dirstring,"/*");
#else
  strcat(dirstring,"/*.*");
#endif
  done = findfirst(dirstring,&ffblk,REGFILE);
  *cp = '\0';  /* drop *.* from dirstring */

  oldmode = sockmode(s,SOCK_ASCII);
  j2setflush(s,-1); /* we will do our own flushing */

  p = dirstring + strlen(ports[rq->index].dirname) + 2;
  /* undosify(p); not necessary, done in decode */

  if(rq->method != METHOD_HTML)
#ifdef UNIX
    usprintf(s,"<TITLE>Dir</TITLE><H2>Directory of \"%s\"</H2>\n",p-1);
#else
    usprintf(s,"<TITLE>Dir</TITLE><H2>Directory of \"%c:%s\"</H2>\n",rq->file[0],p);
#endif

  usputs(s,"<PRE>\n");

  if(rq->method != METHOD_HTML)
      usputs(s,"      <a href=\"..\">Parent Directory</a>\n");
      
  while(!done) {
    if(ffblk.ff_name[0] != '.') {
#ifndef UNIX
        strlwr(ffblk.ff_name);
#endif
        if(ffblk.ff_attrib & FA_DIREC)
            cp = "[DIR]";
        else
            cp = "[FILE]";
#ifdef UNIX
        usprintf(s,entry,cp,p-1,ffblk.ff_name,
#else
        usprintf(s,entry,cp,rq->file[0],p,ffblk.ff_name,
#endif
                 (ffblk.ff_attrib & FA_DIREC) ? "/" : "",
                 ffblk.ff_name,
                 ffblk.ff_fsize,
#ifdef UNIX
                 ffblk.ff_ftime.tm_mday,                    /* day */
                 Months[ffblk.ff_ftime.tm_mon],             /* month */
                 (ffblk.ff_ftime.tm_year + 1900));           /* year */
#else
                 (ffblk.ff_fdate) & 0x1F,                   /* day */
                 Months[((ffblk.ff_fdate >> 5) & 0xf)-1],  /* month */
                 (ffblk.ff_fdate >> 9) + 1980);              /* year */
#endif
    } 
    pwait(NULL);      /* this might not be a good idea */
    done = findnext(&ffblk);
  }
  usputs(s,"</PRE>\n");
  usflush(s);

  sockmode(s,oldmode);

#ifdef notdef
  strcat(dirstring,"wwwpost.cat");
  fp = fopen(dirstring,READ_BINARY);
  if(fp != NULLFILE) {
    tmp = (char *)mallocw(HLINELEN);
    sendhtml(fp,s,tmp,HLINELEN,rq);
    free(tmp);
/*    sendfile(fp,s,IMAGE_TYPE,0,NULL); */
    fclose(fp);
  }
#endif

  free(dirstring);
  return 0;
}


static int
getmonth (cp)
char *cp;
{
int k;

	for (k = 0; k < 12; k++)	{
		if (!strnicmp (cp, Months[k], 3))	{
			return k;
		}
	}
	return 0;
}


static long
countusage (s, file, display, increase, create)
int s;
const char *file;
int display, increase, create;
{
char name[80], buf[32];
FILE *fp;
long count = 0;


	sprintf (name,"%s/%s",HttpStatsDir,file);

	if ((fp = fopen (name, READ_TEXT)) != NULLFILE) {
		fgets (buf, sizeof(buf), fp);
		fclose (fp);
		count = atol (buf);
	} else if(!create)
            return 0L;
            
	if (increase)	{
		count++;
		if ((fp = fopen (name, WRITE_TEXT)) != NULLFILE) {
			fprintf (fp, "%ld", count);
			fclose (fp);
		}
	}
	if (display)
		usprintf (s, "%ld", count);
	return (count);
}


static int
isnewer (thetime, tmstr)
time_t thetime;
char *tmstr;
{
time_t new,old;
struct tm t, *gtm;
char *cp, *cp2;
int format = 0;
/*time_t offset;*/

	/* the string should be in one of these three formats:
	 * (a) RFC 822 (preferred) - Sun, 06 Nov 1994 08:49:37 GMT
	 * (b) RFC 850		   - Sunday, 06-Nov-94 08:49:37 GMT
	 * (c) ANSI C's asctime    - Sun Nov  6 08:49:37 1994
	 */
	/* skip past the name of the weekday */
	cp = skipwhite (tmstr);
	cp = skipnonwhite (cp);
	cp = skipwhite (cp);
	if (isdigit (*cp))	{	/* format a or b */
		cp[2] = 0;
		t.tm_mday = atoi (cp);
		cp += 3;
		t.tm_mon = getmonth (cp);
		cp += 4;
		cp2 = skipnonwhite (cp);
		*cp2 = 0;
		t.tm_year = atoi (cp);
		cp = ++cp2;
	} else {			/* format c */
		t.tm_mon = getmonth (cp);
		cp += 4;
		t.tm_mday = atoi (cp);
		cp += 3;
		format = 1;
	}

	/* time parsing is common between the three formats */
	sscanf (cp, "%02d:%02d:%02d", &t.tm_hour, &t.tm_min, &t.tm_sec);
	/* then parse the year, if format c */
	if (format) {
		cp += 9;
		t.tm_year = atoi (cp);
	}
        if(t.tm_year>=1900)       /* yyyy assumed OK, we just remove 1900 bias */
            t.tm_year-=1900;
        else if (t.tm_year < 70)  /* yy < 70 => 20yy */
            t.tm_year+=100;
  /*    else if (t.tm_year > 99)  yyy (and yyyy < 1900) is invalid */

/*
#if !defined(sun) && !defined(__bsdi__)
	t.tm_isdst = -1;
#endif  */
	new = mktime (&t);
#ifdef UNIX
        /* Silly problem arises when we convert a 02:nn value on the day DST changes! */
        if (new == -1L) {
            t.tm_isdst = 0;  /* fudge it */
            new = mktime (&t);  /* try to do better */
        }
#endif

#ifdef notdef
	ltm = localtime(&new);		/* to get timezone info */
#if defined(sun) || defined(__bsdi__)
	offset = (ltm->tm_gmtoff * -1);
#else
	offset = ((timezone - (ltm->tm_isdst * 3600L)) * -1);
#endif
	new += offset;
#endif
        /* Can't we do it this way? */
        gtm = gmtime(&thetime);
        old = mktime(gtm);
	return (new < old);
}

int
authorization(int s, char *buf, struct reqInfo *rq)
{
    int auth = 1, msg = MSG_401, resp = RESP_401;
    char *urlroot, *realm, *passkey, *cp;
    FILE *fp;
	int forbid_flag = 0;	/* replaces GOTO 'forbid' label */
    
    realm = "\0";

    sprintf(buf,"%s/%s",Spoolqdir,"access.www");
    if((fp = fopen(buf,READ_TEXT)) == NULLFILE)
	{
        auth = 0;
        forbid_flag = 1; // something's wrong, not enough file handles?
    }
    
	if (!forbid_flag)    
	{
    while (fgets(buf,HLINELEN,fp))
	{
        rip(buf);
        if(*buf == '#' || *buf == 0)
            continue;
        urlroot = skipwhite(buf);
        if(!*urlroot)
            continue;
        realm = skipnonwhite(urlroot);
        if(*realm)
            *realm++ = 0;
        realm = skipwhite(realm);
        passkey = skipnonwhite(realm);
        if(*passkey)
            *passkey++ = 0;
        passkey = skipwhite(passkey);
        cp = skipnonwhite(passkey);
        *cp = 0;
        
        if(!strnicmp((rq->url[1] == ':') ? &rq->url[3] : rq->url, &urlroot[1], strlen(urlroot) - 1)) {
            auth = 0;
            break;
        }
    }
    
    fclose(fp);
    
    if(auth)
        return auth;

	}	/* end of IF forbid flag */

    if(!*realm) {     // forbidden
        resp = RESP_403;
        msg = MSG_403;
    }
    if(rq->passwd && *realm && !strcmp(rq->passwd,passkey)) {
        auth = 1;
    } else {
        if(rq->version) {
            httpHeaders(s,resp,rq);
            if(*realm)                // no way in
                usprintf(s,"%s Basic realm=\"%s\"\n",wHdrs[HDR_AUTHEN],realm);
            usprintf(s,"%s %s%s\n\n",wHdrs[HDR_TYPE],CTypes1[TYPE1_TEXT],
                        HTTPtypes[T_html].type2);
        }
        usputs(s,HttpMsg[msg]);
    }
    return auth;
}


/* decode: Creates a copy of string which has '%'-escaped charecters decoded
   back to ASCII.  '+' signs to ' ' & '&' to 0xff if isPath == 0, else
   (decoding a path not a query string) string is canonized so that
   authorization can identify the path later.
   
   Allocates memory. Caller must free it. 
*/

char *decode (char *string, int isPath)
{
int i=0,k=0,slen;
char code[3],*tmp;

	slen = min(strlen (string),99);
        tmp = (char *)mallocw(slen+1);
        
	code[2] = '\0';

        /* possible security breach */
            
	while (i <= slen) {
            if (string[i] == '%') {
/*                string[i] = '#';    '%' sign causes problems in log()  */
		code[0] = string[i+1];
		code[1] = string[i+2];
		tmp[k] = (char)htoi(code);
		k++;  i += 3;
            } else {
                tmp[k] = string[i];
                if(!isPath) {
                    if(tmp[k] == '+') {
                        tmp[k] = ' ';
                    } else if(tmp[k] == '&') {
                        tmp[k] = 0xff;
                    }
                }
                k++; i++;
            }
	}
        
        /* now, canonize the path; DOS accepts different strings for the
           same path and that causes hell in authorization */
        if(isPath) {  // we might break something for the gopher here, but I don't think so
            undosify(tmp);
            if(*tmp == '.' || *tmp == '/')
                *tmp = 0;           // they'll get the root.htm
            
            i = k = 0; slen = strlen(tmp);
            while(i <= slen) {
                if((tmp[i] == '.' || tmp[i] == '/') &&
                      (tmp[i+1] == '.' || tmp[i+1] == '/')) {
                    i++;
                } else {
                    tmp[k++] = tmp[i++];
                }
            }
        }
	return tmp;
}
        

static void
sendhtml (fp, s, buf, buflen, rq)
FILE *fp;
int s;
char *buf;
int buflen;
struct reqInfo *rq;
{
time_t t;
struct stat ft;
char *cp1, *cp2, c, *newline, *tmp;
char *remainder, *cmdname;
FILE *tfp;
char *bufptr;

    // Headers already created, we will use this in exec and mkwelcome now
    rq->method = METHOD_HTML;
    sockmode (s, SOCK_BINARY);
    while ((fgets (buf, buflen, fp)) != NULLCHAR) {
	pwait(NULL);
	bufptr = buf;
	while (bufptr  && (cp1 = strstr (bufptr, "<!--#")) != NULLCHAR) {
	   if (cp1 != bufptr)	{
	       *cp1 = 0;
		usputs (s, bufptr);
	   }
	   cp1 += 5;
	   remainder = strstr (cp1, "-->");
	   if (remainder != NULLCHAR) {
	       *remainder = 0;
	       remainder = &remainder[3];
	   }
	   cmdname = skipwhite (cp1);
	   cp1 = skipnonwhite (cmdname);
	   cp1 = skipwhite (cp1);

	   if (!strnicmp (cmdname, "echo", 4)) {
	       if (!strnicmp (cp1, "var=\"", 5)) {
	           time (&t);
	           cp1 += 5;
		   if ((cp2 = strrchr (cp1, '\"')) != NULLCHAR)
		      *cp2 = 0;
		   if (!stricmp (cp1, "DATE_LOCAL"))
			usputs (s, ptime (&t));
		   else if (!stricmp (cp1, "DATE_GMT"))
			usputs (s,gmt_ptime (&t));
		   else if (!stricmp (cp1, "HOSTNAME"))
			usputs (s, rq->myname);
		   else if (!stricmp (cp1, "DOCUMENT_URI")) {
			usprintf (s, "/%s", rq->url);
		   } else if (!stricmp (cp1, "DOCUMENT_NAME"))	{
			cp2 = strrchr (rq->file, '/');
			if (cp2)
			     cp2++;
			else
			     cp2 = rq->file;
			usputs (s, cp2);
			
		   } else if (!stricmp (cp1, "LAST_MODIFIED")) {
			fstat (fileno(fp), &ft);
			usputs (s, ptime (&ft.st_mtime));
		   } else if (!stricmp (cp1, "TOTAL_HITS")) {
			countusage (s, "tcount.dat", 1, 0, 0);
                   } else if (!stricmp(cp1,"REQ_FROM")) {
                        if(rq->from)
                            usputs(s,rq->from);
                   } else if(!stricmp(cp1,"REQ_REFERER")) {
                        if(rq->referer)
                            usputs(s,rq->referer);
                   } else if(!stricmp(cp1,"REQ_AGENT")) {
                        if(rq->agent)
                            usputs(s,rq->agent);
		   } else
			usputs (s, errorstr);
	       } else if (!strnicmp (&cp1[1], "count=\"", 7))	{
		   c = tolower(*cp1);
		   switch (c) {
		      case 'd':	/* dcount */
		      case 'i':	/* icount */
		      case 's':	/* scount */
			cp1 = strchr (cp1, '\"');
			cp2 = strrchr (++cp1, '\"');
			if (cp2)
			     *cp2 = 0;
			countusage (s, cp1, (c != 's'), (c != 'd'), 1);
			break;
		      default:
                      	usputs (s, errorstr);
		   }
		} else
		   usputs (s, errorstr);
	   } else if (!strnicmp (cmdname, "config", 6)) {
                /* nothing to configure yet, maybe later */
		usputs (s, errorstr);
	   } else if (!strnicmp (cmdname, "include", 7)) {
                tmp = j2strdup(rq->file);                 // Save it.
                if((cp2 = strrchr(cp1,'\"')) != NULLCHAR)
                    *cp2 = 0;
                file_or_virtual (cp1, rq);
                if(*rq->file) {
                    if((tfp = fopen(rq->file,READ_BINARY)) != NULLFILE) {
                        newline = (char *)mallocw(HLINELEN);
                        sendhtml(tfp,s,newline,HLINELEN,rq);
                        free(newline);
                        fclose(tfp);
                    } else if(rq->file[strlen(rq->file)-1] != '/' || mkwelcome(s,rq))
                        usputs(s,errorstr);
                } else
                    usputs(s,errorstr);
                strcpy(rq->file,tmp);                   // Restore and free.
                free(tmp);
	   } else if (!strnicmp (cmdname, "exec", 4))	{
                if(!strnicmp(cp1,"cgi=\"",5)) {
                  cp1 += 5;
                  cp1 = skipwhite(cp1);
                  if((cp2 = strchr(cp1,'\"')) != NULLCHAR) {
                    *cp2 = 0;
                    if((cp2 = strchr(cp1,'?')) != NULLCHAR) {
                        *cp2++ = 0;
                        rq->arg = cp2;
                        rq->qsize = strlen(rq->arg);
                    }
                  }
                  tmp = rq->url;    // Save
                  rq->url = cp1;
                  process_cgi(s,rq);
                  rq->url = tmp;     // Restore
                  rq->arg = NULLCHAR;       // Clear it; in case ...
                } else 	if (!strnicmp (cp1, "cmd=\"", 5)) {
                /* exec cmd not supported, at least not yet */
        	  usputs (s, errorstr);
                } else {
                  usputs(s,errorstr);
                }
	   } else {
		usprintf (s, "<!--#%s %s-->", cmdname,cp1); /* unknown, pass on */
	   }
	   bufptr = remainder;
        }
        if (bufptr != NULLCHAR)
        usputs (s, bufptr);
    }
}


static void
file_or_virtual (cp,rq)
char *cp;
struct reqInfo *rq;
{

	rq->file[0] = 0;
	if (!strnicmp (cp, "virtual=\"", 9))	{
		/* virtual filename, based on root http directory */
#ifdef UNIX
		sprintf (rq->file, "%s%s",ports[rq->index].dirname,&cp[9]);
#else
		sprintf (rq->file, "%c:%s%s",ports[rq->index].DR,ports[rq->index].dirname,&cp[9]);
#endif
	} else if (AbsInclude && !strnicmp (cp, "file=\"", 6)) {
		/* absolute filename */
		strcpy (rq->file, &cp[6]);
	}
}

static void process_cgi(int s, struct reqInfo *rq)
{
    struct cgi *cgip;
    char str[HLINELEN+16];

    for(cgip = Cgis;cgip->name != NULLCHAR;cgip++){
        if(!strcmp(rq->url,cgip->name))
            break;
    }
    
    if(cgip->name == NULLCHAR) {
        sprintf(str,"Cgi function '%s' is not available",rq->url);
        if(rq->method != METHOD_HTML) {
            httpError(s,RESP_400,MSG_400,str,rq);
        } else {
            usprintf(s,"[%s]",str);
        }
    } else {
        if((*cgip->func)(s,rq)) {
            if(rq->method != METHOD_HTML) {
                httpError(s,RESP_500,MSG_500,Cgi_Error,rq);
            } else {
                usprintf(s,"[%s]",Cgi_Error);
            }
        }
    }
    Cgi_Error = NULLCHAR;
    return;
}

/*****************************************************************************/
/*                                                                           */
/*                              CGI FUNCTIONS                                */
/*                                                                           */
/*****************************************************************************/
/*
 * All CGI functions should have a prototype of the form:
 *
 *      static int func_name(int s, struct reqInfo *rq)
 *
 * arg 's' is the socket we are on; arg 'rq points to the structure which
 * has all the info about the request we are receiving. It's defined as:
 *
 *   struct reqInfo {
 *       int index;         / which server is this?
 *       int method;        / method = METHOD_GET | METHOD_POST | METHOD_HTML
 *       int version;       / HTTP/1.0 == 1; HTTP/0.9 == 0; 
 *       int qsize;         / string length of the query string.
 *       char *url;         / the URI portion of the GET or POST request line
 *       char *arg;         / this is the argument string for us if METHOD_HTML
 *       char *query;       / this is the argument string for us if METHOD_GET or METHOD_POST
 *       char *newcheck;    / content of the 'If-Modified-Since:' header if we received one
 *       char *from;        / content of the 'From:' header if received one
 *       char *referer;     / content of the 'Referer:' header if received one
 *       char *agent;       / content of the 'User-Agent' header if received one
 *       char *passwd;      / BASE64 encoded 'user:password' string if received one
 *       char file[128];    / the complete path to the HTML file that called us; meaningless if not METHOD_HTML
 *   };
 *
 * Don't forget to put the prototype of your function at the beginning of this
 * file. And also, register your function in the Cgis[] array there. Cgis[] 
 * has to fields. 'name' == the URI string that will be associated with your
 * funtion; 'func' == func_name. It is a good idea to #ifdef your code. In that
 * case, don't forget to put the appropriate #define directive at the beginning
 * of this file. The 'name' parameter in the Cgis array must not have the
 * initial '/' char of the URI.
 *
 * Your function should return ZERO if it is successful. In case of errors, it can
 * either handle the situation itself; ie. generate and send the appropriate
 * error messages, and return ZERO (success), or alternatively point the Cgi_Error
 * to an error string and return ONE (error). In the latter case, the server
 * will create an error response for you, and send the string pointed to by
 * Cgi_Error as the document body. The error string must not can contain HTML
 * body tags. But, must not contain ant head tags. Don't allocate memory for
 * error the error string, it won't be freed. Use a static space instead.
 *
 * If your function is supposed to handle METHOD_HTML, then it will be called
 * from an HTML document with a command like the following:
 *
 *      <!--# exec cgi="name?arguments needed by your function" -->
 *
 * where 'name' == the URI you put into the 'name' field of Cgis[] array above.
 * the "?arguments ..." portion is only used if your function needs any arguments
 * of its own. In that case, rq->url == 'name' & rq->arg == 'arguments ...' up to
 * but not including the last '"' character. rq->qsize == strlen(rq->arg).
 *
 * If rq->method == METHOD_GET, then your function is called by a browser
 * with an URL like this:
 *
 *      GET /name?arguments HTTP/1.0
 *
 * where 'name' == the 'name field of Cgis[] entry. rq->url == 'name',
 * rq->query == 'arguments'. rq->qsize == strlen(rq->query). The browser
 * will send the 'arguments' escape encoded for special characters and
 * spaces turned into '+' characters. All of this has been already decoded
 * for you. The whole line buffer for GET request is 256 characters long.
 * So, you should use this method only if the the 'arguments' string you
 * expect is not too long; ie. 256 - 15 - strlen(name). IMPORTANT: For
 * your function to work with GET & POST methods, it has to have an
 * argument. Because the server uses the existence of arguments to distinguish
 * between a regular file and a CGI request; ie. an URL like
 * http://server/name will only be searched as a file or directory. Use,
 * http://server/name?dummy instead. This trick isn't necessary when your
 * function is called from inside an HTML file with the 'exec cgi' command.
 * 
 *
 *  If rq->method == METHOD_POST, then you were called with an URL like:
 *
 *      POST /name HTTP/1.0
 *      [Some HTTP/1.0 headers]
 *
 *      query-string
 *
 * and the query string itself is sent in the request body. Again it is
 * encoded by the browser and decoded by the server for you. In this case,
 * rq->url == 'name' as usual, rq->query == 'query_string', 
 * rq->qsize == strlen(rq->query). The different fields in the form are
 * separated by '&' characters. *** There is a possibility that
 * the user input also contains the '&' character though, so you should
 * check if the string following the '&' char is a valid entry name.
 * Later I might devise an escape mechanism for '&' character inputs if it
 * seems to be necessary.
 *
 * The POST method doen't have the length
 * restriction of the GET method. There is still a limit. I think it is
 * ~32760 since 32767 is the limit for the malloc() call if I am reading
 * alloc.c correctly. A few bytes needed for the overhead. Of course,
 * available memory under JNOS might be a real constraint here. Decoder
 * allocates another chunk of memory roughly equal to rq->qsize. Although
 * the first copy is freed immadiately after decoding, you still need
 * for a short while 2 * rq->qsize free memory.
 * 
 * I think this is about it. You should read the whole source and especially
 * the two example functions below. If you follow all the gudelines exposed
 * here, we should be able to exchange cgi functions between ourselves and
 * enrich this server over time.
 * 
 * I also think that it shouldn't be very difficult to translate the CGI
 * programs written for our big brothers, like NCSA httpd server, now.
 *
 * This is the first attempt in defining this interface. Therefore, it might
 * change later if new things appear to be needed. But, I think that any
 * such changes would be trivial.
 *
 * If you write any CGI functions for this server, please send a copy with
 * brief explanations to me at seost2+@pitt.edu for possible inclusion in
 * future versions.
 * 
 * Also, address any bug reports, fixes, and suggestions to:
 *
 *                                          Selcuk Ozturk
 *                                          4600 Bayard St. #307
 *                                          Pittsburgh, PA 15213
 *          e-mail: seost2+@pitt.edu
 *
 ********************************************************************************/
#ifdef CGI_XBM_COUNTER


static
char xbm_digits[] = {0xff,0xff,0xff,0xc3,0x99,0x99,0x99,0x99,
                     0x99,0x99,0x99,0x99,0xc3,0xff,0xff,
                     0xff,0xff,0xff,0xcf,0xc7,0xcf,0xcf,0xcf,
                     0xcf,0xcf,0xcf,0xcf,0xcf,0xff,0xff,
                     0xff,0xff,0xff,0xc3,0x99,0x9f,0x9f,0xcf,
                     0xe7,0xf3,0xf9,0xf9,0x81,0xff,0xff,
                     0xff,0xff,0xff,0xc3,0x99,0x9f,0x9f,0xc7,
                     0x9f,0x9f,0x9f,0x99,0xc3,0xff,0xff,
                     0xff,0xff,0xff,0xcf,0xcf,0xc7,0xc7,0xcb,
                     0xcb,0xcd,0x81,0xcf,0x87,0xff,0xff,
                     0xff,0xff,0xff,0x81,0xf9,0xf9,0xf9,0xc1,
                     0x9f,0x9f,0x9f,0x99,0xc3,0xff,0xff,
                     0xff,0xff,0xff,0xc7,0xf3,0xf9,0xf9,0xc1,
                     0x99,0x99,0x99,0x99,0xc3,0xff,0xff,
                     0xff,0xff,0xff,0x81,0x99,0x9f,0x9f,0xcf,
                     0xcf,0xe7,0xe7,0xf3,0xf3,0xff,0xff,
                     0xff,0xff,0xff,0xc3,0x99,0x99,0x99,0xc3,
                     0x99,0x99,0x99,0x99,0xc3,0xff,0xff,
                     0xff,0xff,0xff,0xc3,0x99,0x99,0x99,0x99,
                     0x83,0x9f,0x9f,0xcf,0xe3,0xff,0xff
                    };
                    
                    
/*
 * A simple graphic hit counter function. Used with an URL like:
 *
 *      http://server.address/counter.xbm?filename.ext
 *
 * where 'filename.ext' is the name of a counter file you have already
 * created in the '/wwwstats' directory. If the file is not there returns
 * 0000.
 * Adapted from Michael Nelson's perl scrip counter.xbm program, which
 * was based upon Dan Rich's code and which was based upon Frans van
 * Hoesel's C code.
 */
static int xbm_counter(int s,struct reqInfo *rq)
{
    char count[32], *cp;
    int i, j, len, inc = 1, inv = 0,  hexno;
    int begin = 0, end = 15;
    unsigned long counter;
    
    /* rq->query points to a str of the form:
       "filename.ext [inv] [noinc]"; [] == optional  */
    if(rq->method != METHOD_HTML)
        cp = skipnonwhite(rq->query);
    else {
        Cgi_Error = "This function can't be used here";
        return 1;
    }
    *cp++ = 0;
    
    if(strstr(cp,"inv")) { // reverse the background and foreground colors
        inv = 1;
        begin = 2;
        end = 13;
    }
    if(strstr(cp,"noinc"))
        inc = 0;
    
    counter = countusage(s,rq->query,0,inc,0);
    sprintf(count,"%04lu",counter);
    len = strlen(count);
    for(i=0;i < len;i++)
        count[i] -= '0';
    
    /* generate HTTP headers */
    httpHeaders(s,RESP_200,rq);
    usprintf(s,"%s %s%s\n\n",wHdrs[HDR_TYPE],CTypes1[TYPE1_IMG],HTTPtypes[T_xbm].type2);
    
    /* xbm format requires UNIX type EOL; so, don't translate */
    sockmode(s,SOCK_BINARY);
    usprintf(s,"#define c_width %d\n#define c_height %d\n",len*8,inv ? 11 : 15);
    usputs(s,"static char c_bits[] = {\n");
    for(i = begin; i < end;i++) {
        for(j = 0;j < len;j++) {
            hexno = xbm_digits[count[j] * 15 + i];
            if(inv)
                hexno = ~hexno;
            else
                hexno &= 0xff;
            usprintf(s,"0x%02x",hexno);
            if(j < (len - 1))
                usputc(s,',');
        }
        if (i == (end - 1))
            usputs(s,"};\n");
        else
            usputs(s,",\n");
        pwait(NULL);
   }
   return 0;
}

#endif /* CGI_XBM_COUNTER */

#ifdef CGI_POSTLOG
/* 
 * A very simple form logging function, for demostration of the CGI interface
 * with the POST method. It is requested by the browser as:
 *
 *      POST /postlog HTTP/1.0
 *      HTTP Headers
 *
 *      form_input
 *
 * entry #, From: header and the date of the request are logged in the
 * file [rootdir]/postlog.dat along with the form_input (one entry field
 * per line).
 */
 

static int postlog(int s, struct reqInfo *rq)
{
    time_t  t;
    long    count;
    int     i;
    char    *cp;
    FILE    *fp;
    
    if(rq->method != METHOD_POST) {    // only POST methods processed
        Cgi_Error = "Invalid Method"; // set Cgi_Error; don't allocate mem, it won't be freed
        return 1;               // Error return
    }
    
    /* You can use 'rq->file' as a buffer; it's 128 chars long */
    /* But, if rq->method == METHOD_HTML, you should preserve its content*/
#ifdef UNIX
    sprintf(rq->file,"%s/postlog.dat",ports[rq->index].dirname);
#else
    sprintf(rq->file,"%c:%s/postlog.dat",ports[rq->index].DR,ports[rq->index].dirname);
#endif
    if((fp = fopen(rq->file,APPEND_TEXT)) == NULLFILE) {
        Cgi_Error = "Can't open logfile!";
        return 1;
    }
    
    count = countusage(s,"postlog.cnt",0,1,1); // Get the entry #, but don't display it.
                                               // begins from 0;
    time(&t);
    fprintf(fp,"*** Entry #%ld:\n",count);
    fprintf(fp,"%s %s\n",wHdrs[HDR_FROM],(rq->from) ? rq->from : "");
    fprintf(fp,"%s %s\n",wHdrs[HDR_DATE], ptime(&t)); // ptime() supplies a '\n';
                                                        //gmt_ptime() does not.
    
    for(cp=rq->query,i=0; i < rq->qsize; cp++,i++) {
        // Query entries are separated by 0xff chars; turn them into '\n's
        // If we think that somebody might want to screw us by sending char 
        // 0xff in the body of a text entry, we should check if a valid
        // entry name follows the 0xff, but that's unlikely.
        if(*cp == 0xff) 
            *cp = '\n';
    }
    fputs(rq->query,fp);
    fputs("\n\n\n",fp);
    fclose(fp);
    
    /* Now, we have to report success to the client */
    /* you can assume HTTP 1.0; 0.9 can't come here */
    /* This generates the HTTP response & the hdrs DATE,SERVER,and MIME */
    httpHeaders(s,RESP_202,rq); // response 202 is the post accepted response
                                         // the last arg (1) indicates HTTP/1.0
    
    /* This tells that we are responding with an HTML document &
        ends the headers w/ an extra '\n' */
    usprintf(s,"%s %s%s\n\n",wHdrs[HDR_TYPE],CTypes1[TYPE1_TEXT],HTTPtypes[T_html].type2);
    
    /* Now, the body of the message in HTML format */
    usputs(s,"<TITLE>Ok</TITLE><H1>POST OK</H1>Your post has been accepted and logged.");
    
    /* Now return without error */
    return 0;
}
#endif /* CGI_POSTLOG */

#endif /* HTTP */
