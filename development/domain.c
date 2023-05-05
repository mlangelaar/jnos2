/*
 *      DOMAIN.C -- domain name system stub resolver
 *
 *      Original code by Phil Karn, KA9Q
 *
 *      04-90 -- Bill Simpson added address->name resolution, time-to-live,
 *      thru     memory caching, generalized multi-record multi-type searches,
 *      10-90    and many minor changes to conform more closely to the RFCs.
 *
 *  06-89 -- Gerard van der Grinten, PA0GRI
 *      thru     Lots of changes and inprovements including server code.
 *      02-91
 *
 *  Jan 92  Bill Simpson added CNAME support to domainsuffix routine.
 *
 *  Jun 92  Johan. K. Reinalda, WG7J
 *          Ported the Domain Name Server from PA0GRI's 910828 .
 *          Version info see down below
 *  Apr 96  put Bill Simpson's TYPE_ANY and "query" cmd back in! .. n5knx
 *  Oct 96  fix CNAME resolving. -- n5knx
 *  Jan 97  ifdef QUERYPROC, make dns_query an interruptible process. -- k2mf/n5knx
 *
 *  Mar 2021 Maiko (VE4KLM), security patch (qdcount), better error logging,
 *           and up'd the stack size from 1024 to 2048, there were signs it
 *           was possibly a problem on a PI device, more fixes coming ...
 *
 *  Apr 2023 Maiko (VE4KLM), time to put in IPV6 support
 *
 */
  
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "ip.h"
#include "socket.h"
#include "cmdparse.h"
#include "proc.h"
#include "session.h"
#include "domain.h"
#include "commands.h"
#include "files.h"

#include <string.h>	/* 24Apr2023, Maiko, strpbrk() prototype */
  
#ifdef UNIX
/* following 2 defs route output to trace screen */
#define FPRINTF tfprintf
#define PRINTF tprintf
#include "unix.h"
#else
#define FPRINTF fprintf
#define PRINTF printf
#endif /* UNIX */
  
#if (0)
#undef  DEBUGDNS               /* for certain trace messages */
#undef  DEBUGDNS_PAIN                      /* for painful debugging */
#endif
/* We have the option of putting negative DNS replies into the cache, to speed
 * up processing it again, or to save some code space (~ 160 bytes) and just
 * ignore them (so we must repeat the query if we are asked again). -- n5knx
 */  
#define RETAIN_NEGATIVE_REPLIES 1

extern int main_exit;                   /* from main program (flag) */
extern int Mprunning;                   /* from main program (flag) */
int DTranslate;                 /* do IP address to domain name translation */
int DVerbose;                   /* Use all of resolved name or first element */
 
static int dns_debug = 0;	/* 18Sep2008, Maiko, 'useless' log entries */ 
  
static struct rr *Dcache;       /* Cache of resource records */
static int Dcache_size = 20;            /* size limit */
static time_t Dcache_time = 0L;         /* timestamp */
  
static int Dfile_clean;             /* discard expired records (flag) */
static int Dfile_reading;           /* read interlock (count) */
static int Dfile_writing;           /* write interlock (count) */
  
struct proc *Dfile_updater;
static int32 Dfile_wait_absolute;       /* timeout Clock time */
static int Dfile_wait_relative = 300;   /* timeout file activity (seconds) */
  
static struct dserver *Dservers;        /* List of potential servers */
static int Dserver_retries = 2;         /* Attempts to reach servers */
static int32 Dserver_maxwait = 60;     /* maximum server timeout limit (seconds) */
  
/* KG7CP -  */
static int Dfile_upd = 1;        /* update the domain file (flag) */
#ifdef DOMAINSERVER
static int32 Dns_ttl = 0;      /* time-to-live of dns answers */
static int dns_maxcli = 6;      /* max number of simultaneous DNS processes */
static int dns_process_count;
#endif

static int32 Dns_TLDlen = 5;	/* 28Nov2015, Maiko (VE4KLM), new */

/* N7IPB */
static int Dsubnet_translate = 1;   /* Translate subnet and broadcast addresses */
  
static char *Dsuffix;           /* Default suffix for names without periods */
static int Dsuffixl;            /* size of Dsuffix (less computing to do */
static int Dtrace;

#ifdef	IPV6
/* 14Apr2023, Maiko, Leave AAAA out of Dtypes */
static char *DtypeAAAA = "AAAA";
#endif

static char *Dtypes[] = {
    "",
    "A",
    "NS",
    "MD",
    "MF",
    "CNAME",
    "SOA",
    "MB",
    "MG",
    "MR",
    "NULL",
    "WKS",
    "PTR",
    "HINFO",
    "MINFO",
    "MX",
    "TXT"
};
#define NDTYPES 17
static char delim[] = " \t\r\n";
  
static int docache __ARGS((int argc,char *argv[],void *p));
static int dosuffix __ARGS((int argc,char *argv[],void *p));
static int dotranslate __ARGS((int argc,char *argv[],void *p));
static int doverbose __ARGS((int argc,char *argv[],void *p));
  
static int docacheclean __ARGS((int argc,char *argv[],void *p));
static int docachelist __ARGS((int argc,char *argv[],void *p));
static int docachesize __ARGS((int argc,char *argv[],void *p));
static int docachewait __ARGS((int argc,char *argv[],void *p));
  
static int dofileupdate __ARGS((int argc,char *argv[],void *p));
static int dodns_ttl __ARGS((int argc,char *argv[],void *p));
static int docachedump __ARGS((int argc,char *argv[],void *p));
static int dosubnet_translate __ARGS((int argc,char *argv[],void *p));

/* 28Nov2015, Maiko (VE4KLM), new function to deal with TLD len issues */
static int dodns_TLDlen (int, char**, void*);
  
static void dlist_add __ARGS((struct dserver *dp));
static void dlist_drop __ARGS((struct dserver *dp));
static int dodnsadd __ARGS((int argc,char *argv[],void *p));
static int dodnsdrop __ARGS((int argc,char *argv[],void *p));
static int dodnslist __ARGS((int argc,char *argv[],void *p));
static int dodnsquery __ARGS((int argc,char *argv[],void *p));
static int dodnsmaxw __ARGS((int argc,char *argv[],void *p));
static int dodnsretry __ARGS((int argc,char *argv[],void *p));
static int dodnstrace __ARGS((int argc,char *argv[],void *p));
static int dodnsserver __ARGS((int argc,char *argv[],void *p));
static int dodnsmaxcli __ARGS((int argc,char *argv[],void *p));
static int dodomlook __ARGS((int argc,char *argv[],void *p));
  
static char *loc_getline __ARGS((FILE *));
static int iscomment __ARGS((char *line));
static char *dtype __ARGS((int value));
static int check_ttl __ARGS((struct rr *rrlp));
static int compare_rr __ARGS((struct rr *search_rrp,struct rr *target_rrp));
static int compare_rr_list __ARGS((struct rr *rrlp,struct rr *target_rrp));
static struct rr *copy_rr __ARGS((struct rr *rrp));
static struct rr *copy_rr_list __ARGS((struct rr *rrlp));
static struct rr *make_rr __ARGS((int source,
char *dname,int16 class,int16 type,int32 ttl,int16 rdl,void *data));
  
static void dcache_add __ARGS((struct rr *rrlp));
static void dcache_drop __ARGS((struct rr *rrp));
static struct rr *dcache_search __ARGS((struct rr *rrlp));
static void dcache_update __ARGS((struct rr *rrlp));
  
static struct rr *get_rr __ARGS((FILE *fp, struct rr *lastrrp));
static void put_rr __ARGS((FILE *fp,struct rr *rrp));
static struct rr *dfile_search __ARGS((struct rr *rrlp));
static void dfile_update __ARGS((int s,void *unused,void *p));
  
static void dumpdomain __ARGS((struct dhdr *dhp,int32 rtt));
static int dns_makequery __ARGS((int16 op,struct rr *rrp,
char *buffer,int16 buflen));
static void dns_query __ARGS((int,void *,void *));
  
static int isaddr __ARGS((char *s));
static struct rr *resolver __ARGS((struct rr *rrlp));
  
static void free_dhdr __ARGS((struct dhdr *));
static void proc_query __ARGS((int,void *,void *));
static void drx __ARGS((int,void *,void *));
  
/**
 **     Domain Resolver Commands
 **/
  
static struct cmds DFAR Dcmds[] = {
    { "addserver",dodnsadd,             0, 2, "add <hostid>" },
    { "cache",    docache,              0, 0, NULLCHAR },
#ifdef DOMAINSERVER
    { "dns",      dodnsserver,          0, 0, NULLCHAR },
#endif
    { "dropserver",   dodnsdrop,        0, 2, "drop <hostid>" },
    { "list",     dodnslist,            0, 0, NULLCHAR },
#ifdef MORESESSION
    { "look",     dodomlook,          512, 2, "look <search text>" },
#endif
#ifdef DOMAINSERVER
    { "maxclients",   dodnsmaxcli,      0, 0, NULLCHAR },
#endif
    { "maxwait",  dodnsmaxw,            0, 0, NULLCHAR },
#ifdef DQUERYSESSION
    { "query",    dodnsquery,         512, 2, "query <hostid>" },
#endif
    { "retries",  dodnsretry,           0, 0, NULLCHAR },
    { "subnet",   dosubnet_translate,   0, 0, NULLCHAR },
    { "suffix",   dosuffix,             0, 0, NULLCHAR },
/* 28Nov2015, Maiko (VE4KLM), new TLD len function */
    { "TLDlen",   dodns_TLDlen,         0, 0, NULLCHAR },
    { "trace",    dodnstrace,           0, 0, NULLCHAR },
    { "translate",dotranslate,          0, 0, NULLCHAR },
#ifdef DOMAINSERVER
/*KG7CP - */
    { "ttl",      dodns_ttl,            0, 0, NULLCHAR },
#endif
    { "update",   dofileupdate,         0, 0, NULLCHAR },
    { "verbose",  doverbose,            0, 0, NULLCHAR },
    { NULLCHAR,	NULL,				0,	0,	NULLCHAR }
};
  
static struct cmds DFAR Dcachecmds[] = {
    { "clean",     docacheclean,   0, 0, NULLCHAR },
    { "dump",      docachedump,    0, 0, NULLCHAR },  /* really is a flush! */
    { "list",      docachelist,    0, 0, NULLCHAR },
    { "size",      docachesize,    0, 0, NULLCHAR },
    { "wait",      docachewait,    0, 0, NULLCHAR },
    { NULLCHAR,		NULL,		0,	0, NULLCHAR }
};
  
int
dodomain(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Dcmds,argc,argv,p);
}
  
int
docache(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Dcachecmds,argc,argv,p);
}
  
static int
dosuffix(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2){
        if(Dsuffix != NULLCHAR)
            tprintf("%s\n",Dsuffix);
        else
            j2tputs("No domain suffix defined.\n");
        return 0;
    }
    if(strcmp(argv[1],"none") == 0){
        if(Dsuffix != NULLCHAR){
            free(Dsuffix);
            Dsuffix = NULLCHAR ;    /* clear out suffix */
            Dsuffixl = 0;
        }
    } else
        if(argv[1][strlen(argv[1])-1] != '.'){
            tprintf(" %s is not a valid suffix.\n",argv[1]);
            return 1;
        } else {
            free(Dsuffix);
            Dsuffix = j2strdup(argv[1]);
            Dsuffixl = strlen(Dsuffix);
        }
    return 0;
}
  
static int
dotranslate(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool( &DTranslate, "Translate IP address to host names", argc,argv );
}
  
static int
doverbose(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool( &DVerbose, "Verbose translation of host names", argc,argv );
}
  
static int dodnsmaxw (int argc, char **argv, void *p)
{
    return setint32 (&Dserver_maxwait, "Server response timeout limit (sec)",
	argc, argv);
}
  
static int
docacheclean(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool( &Dfile_clean, "discard expired records", argc,argv );
}

/* 28Nov2015, New Max TLD length after which we append default suffix */
static int dodns_TLDlen (int argc, char **argv, void *p)
{
    return setint32 (&Dns_TLDlen,
		"max length of TLD we accept after which append default suffix",
			argc, argv);
}
  
#ifdef DOMAINSERVER
/*KG7CP -  set the standard time-to-live for nameserver answers */
static int dodns_ttl (int argc, char **argv, void *p)
{
    return setint32 (&Dns_ttl, "ttl of nameserver answers (seconds)",
	argc, argv );
}
#endif
  
/* KG7CP -   remove all entries from the cache */
static int
docachedump(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    struct rr *dump_rrlp;
    dump_rrlp = Dcache;
    Dcache = NULLRR;
    free_rr(dump_rrlp);
    return 0;
}
  
/* KG7CP -  flag whether the domain file is updated from name server
 * quey responses
 */
static int
dofileupdate(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    return setbool( &Dfile_upd, "update domain file", argc,argv );
}
  
/* N7IPB - Enable or Disable translation of subnet and broadcast addresses */
static int
dosubnet_translate(argc, argv, p)
int argc;
char *argv[];
void *p;
{
    return setbool( &Dsubnet_translate, "Translate subnet and broadcast addresses", argc,argv );
}
  
static int
docachelist(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct rr *rrp;
  
    (void)dcache_search(NULLRR); /* update ttl */
#ifndef UNIX
    rflush();
#endif
    for(rrp=Dcache;rrp!=NULLRR;rrp=rrp->next)
    {
        put_rr(stdout,rrp);
    }
    return 0;
}
  
static int
docachesize(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int newsize;
    int oldsize;
    int result;
  
    newsize = oldsize = Dcache_size;
    result = setint( &newsize, "memory cache size", argc,argv );
  
    if(newsize > 0){
        Dcache_size = newsize;
        if(newsize < oldsize){
            (void)dcache_search(NULLRR); /* update size */
        }
    }
    return result;
}
  
static int
docachewait(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint( &Dfile_wait_relative, "time before file update (seconds)", argc,argv );
}
  
static void
dlist_add(dp)
register struct dserver *dp;
{
    dp->prev = NULLDOM;
    dp->next = Dservers;
    if(Dservers != NULLDOM)
        Dservers->prev = dp;
    Dservers = dp;
}
  
static void
dlist_drop(dp)
register struct dserver *dp;
{
    if(dp->prev != NULLDOM)
        dp->prev->next = dp->next;
    else
        Dservers = dp->next;
    if(dp->next != NULLDOM)
        dp->next->prev = dp->prev;
}
  
static int
dodnsadd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int32 address;
    int timeout = 0;

#ifdef	IPV6
	if (strchr (argv[1], ':'))
	{
		struct j2sockaddr_in6 sock6;

		if (j2_ipv6_asc2ntwk (argv[1], &sock6) != 1)
		{
			tprintf("Resolver %s unknown\n", argv[1]);
			return 1;
		}
    	if(argc > 2)
		{
        	timeout = atoi(argv[2]);
		}

		return add_nameserver (sock6.sin6_addr.s6_addr, timeout, IPV6VERSION);
	}
#endif
 
    if((address = resolve(argv[1])) == 0){
        tprintf("Resolver %s unknown\n",argv[1]);
        return 1;
    }
    if(argc > 2)
        timeout = atoi(argv[2]);

#ifdef	IPV6
    return add_nameserver(&address, timeout, IPVERSION);
#else
    return add_nameserver(address,timeout);
#endif
}

#ifdef	IPV6
int add_nameserver (void *address, int timeout, int ipver)
{
    extern int32 Tcp_irtt;
    struct dserver *dp;
  
    dp = (struct dserver *)callocw(1,sizeof(struct dserver));

	dp->ipver = ipver;

	if (ipver == IPV6VERSION)
	{
		dp->address6 = mallocw (16);
		copyipv6addr ((unsigned char*)address, dp->address6);
	}
	else
    	dp->address = *((long*)address);
    if(timeout)
        dp->srtt = timeout * 1000;
    else
        dp->srtt = 3 * Tcp_irtt;  /* Round trip plus processing time */
    dp->mdev = 0;
    dp->timeout = dp->srtt;    /* 4 * dp->mdev + dp->srtt;*/
    dlist_add(dp);
    return 0;
}
#else
int
add_nameserver(address,timeout)
int32 address;
int timeout;
{
    extern int32 Tcp_irtt;
    struct dserver *dp;
  
    dp = (struct dserver *)callocw(1,sizeof(struct dserver));
    dp->address = address;
    if(timeout)
        dp->srtt = timeout * 1000;
    else
        dp->srtt = 3 * Tcp_irtt;  /* Round trip plus processing time */
    dp->mdev = 0;
    dp->timeout = dp->srtt;    /* 4 * dp->mdev + dp->srtt;*/
    dlist_add(dp);
    return 0;
}
#endif
  
static int
dodnsdrop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct dserver *dp;
    int32 addr;
  
    addr = resolve(argv[1]);
    for(dp = Dservers;dp != NULLDOM;dp = dp->next)
        if(addr == dp->address)
            break;
  
    if(dp == NULLDOM){
        j2tputs("Not found\n");
        return 1;
    }
  
    dlist_drop(dp);
    free((char *)dp);
    return 0;
}
  
static int
dodnslist(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct dserver *dp;
  
    j2tputs("Server address              srtt    mdev    timeout  queries responses timeouts\n");
    for(dp = Dservers;dp != NULLDOM;dp = dp->next)
	{
#ifdef	IPV6
		if (dp->ipver == IPV6VERSION)
 	       tprintf("%-25s", ipv6shortform (human_readable_ipv6 (dp->address6), 0));
		else
#endif
 	       tprintf("%-25s", inet_ntoa(dp->address));

	tprintf("%8d%8d%10d%9d%9d%9d\n", dp->srtt,dp->mdev,dp->timeout,
        	dp->queries,dp->responses,dp->timeouts);
    }
    return 0;
}
  
static int
dodnsretry(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint( &Dserver_retries, "server retries", argc,argv );
}
  
static int
dodnstrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setbool(&Dtrace,"server trace",argc,argv);
}
  
  
/**
 **     Domain Resource Record Utilities
 **/
  
/* loc_getline() - version 1.0 - PA0GRI
 *
 * 02Oct2009, Maiko, renamed since GLIB 4.6 now has a getline() call ...
 *
 * Read a line from a domain file and return a useful line.
 * Completely assembled (if multi line).
 * It skips any comment lines.
 * It follows RFC 1133, 1134, 1135 and 1136.
 *
 * Rewritten for simplicity and fixed to provide an upper limit
 * to the maximum permissible size of a single continuation line
 * 02/97 - K2MF, N5KNX & VE3DTE
 */

static char *loc_getline (FILE *fp)
{
#define DNSLINELEN 512
    char *contline, *cp, *line;

    if(fp == NULLFILE)
        return NULLCHAR;

    line = mallocw(DNSLINELEN);

    for(;;) {
        if(fgets(line,DNSLINELEN,fp) != NULLCHAR) {
            if(iscomment(line))
                continue;    /* skip commented lines */
        } else {
            free(line);
            return NULLCHAR;
        }
        rip(line);

        if((cp = strpbrk(line,"#;")) != NULLCHAR)
            /* Eliminate trailing comment */
            *cp = '\0';

        /* Continuation */
        if((cp = strchr(line,'(')) != NULLCHAR) {
            *cp = ' ';        /* replace with space */

            /* Continuation */
            if((cp = strchr(line,')')) != NULLCHAR) {
                *cp = ' ';    /* replace with space */
                return line;    /* complete line */
            }
            break;  /* need to read more, looking for ')' */
        } else
            return line;
    }
    contline = mallocw(DNSLINELEN);

    for(;;) {
        if(fgets(contline,DNSLINELEN,fp) != NULLCHAR) {
            if(iscomment(contline))
                continue;    /* skip commented lines */
        } else {
            free(contline);
            free(line);
            return NULLCHAR;
        }
        rip(contline);

        if((cp = strpbrk(contline,"#;")) != NULLCHAR)
            /* Eliminate trailing comment */
            *cp = '\0';

        cp = mallocw((strlen(line) + strlen(contline) + 2));
        sprintf(cp,"%s %s",line,contline);
        free(line);
        line = cp;

        /* Continuation */
        if((cp = strchr(line,')')) != NULLCHAR) {
            *cp = ' ';        /* replace with space */
            break;   /* done reading continuation lines */
        }
        if(strlen(line) > DNSLINELEN) {  /* too darned big? */
            free(contline);
            free(line);
            return NULLCHAR;
        }
    }
    free(contline);
    return line;
}

static int
iscomment (char *line)
{
    if(line != NULLCHAR) {
        /* Skip over leading whitespace */
        while(*line && isspace((int)*line))
            line++;

        if(*line && strchr("\n#;",*line) == NULLCHAR)
            /* Line is not empty and not a comment */
            return 0;
    }
    /* Line is either empty or a comment */
    return 1;
}

static char *
dtype(value)
int value;
{
    static char buf[10];

    if (value < NDTYPES)
        return Dtypes[value];
#ifdef	IPV6
	/*
	 * 14Apr2023, Maiko, I would rather leave the type 28 as what it really
	 * is in realtime, instead of mapping it to lower numbers in the list, so
	 * leave the TYPE_AAAA out of Dtypes[] and just catch it here instead !
	 */
	if (value == TYPE_AAAA)
		return DtypeAAAA;
#endif
    sprintf( buf, "{%d}", value);
    return buf;
}

/* check list of resource records for any expired ones.
 * returns number of expired records.
 */
static int
check_ttl(rrlp)
register struct rr *rrlp;
{
    int count = 0;
  
    while(rrlp != NULLRR){
        if(rrlp->ttl == 0)
            count++;
        rrlp = rrlp->next;
    }
    return count;
}
  
/* Compare two resource records.
 * returns 0 if match, nonzero otherwise.
 */
static int
compare_rr(search_rrp,target_rrp)
register struct rr *search_rrp,*target_rrp;
{
    int i, j, k;
  
    if(search_rrp == NULLRR || target_rrp == NULLRR)
        return -32765;
  
    if(search_rrp->class != target_rrp->class)
        return -32763;
  
    if(search_rrp->type != TYPE_ANY
        && search_rrp->type != target_rrp->type
        && (search_rrp->source != RR_QUERY
            || (target_rrp->type != TYPE_CNAME
                && target_rrp->type != TYPE_PTR)))
        return -32761;
  
    if(search_rrp->source != RR_INQUERY){
        i = strlen(search_rrp->name);
        j = strlen(target_rrp->name);
        if(i == j){
            if((k = strnicmp(search_rrp->name,target_rrp->name,(size_t)i)) != 0){
                return k;
            }
        } else {
  
            if(Dsuffix != NULLCHAR){
                if(i != j+Dsuffixl+1){
                    return -32759;
                } else {
                    if(search_rrp->name[j] != '.')
                        return -32755;
                    if(strnicmp(target_rrp->name
                        ,search_rrp->name,(size_t)j) != 0)
                        return -32757;
                }
            } else {
                return -32759;
            }
  
#ifdef RETAIN_NEGATIVE_REPLIES
        /* match negative records so that they are replaced */
            if(target_rrp->rdlength == 0)
                return 0;
#endif
        }
    }
  
    /* if a query has gotten this far, match it */
    if(search_rrp->source == RR_QUERY)
        return 0;
  
#ifdef RETAIN_NEGATIVE_REPLIES
    /* ensure negative records don't replace older records */
    if(search_rrp->rdlength == 0)
        return -32757;
#endif
  
    /* match expired records so that they are replaced */
    if(search_rrp->source != RR_INQUERY){
        if(target_rrp->ttl == 0)
            return 0;
    }
  
    /* Note: rdlengths are not compared because they vary depending
     * on the representation (ASCII or encoded) this record was
     * generated from.
     */
  
    switch(search_rrp->type){
        case TYPE_A:
            i = search_rrp->rdata.addr != target_rrp->rdata.addr;
            break;
#ifdef IPV6
	/*
	 * 10Apr2023, Maiko (VE4KLM), Adding IPV6 support
	 *  (rfc 3596 (Oct 2003), obsoletes 3152, 1886)
	 */
        case TYPE_AAAA:
            i = memcmp (search_rrp->rdata.addr6, target_rrp->rdata.addr6, 16);
            break;
#endif
        case TYPE_CNAME:
        case TYPE_MB:
        case TYPE_MG:
        case TYPE_MR:
        case TYPE_NS:
        case TYPE_PTR:
        case TYPE_TXT:
            i = stricmp(search_rrp->rdata.data,target_rrp->rdata.data);
            break;
        case TYPE_HINFO:
            i = strcmp(search_rrp->rdata.hinfo.cpu,target_rrp->rdata.hinfo.cpu) ||
            strcmp(search_rrp->rdata.hinfo.os,target_rrp->rdata.hinfo.os);
            break;
        case TYPE_MX:
            i = stricmp(search_rrp->rdata.mx.exch,target_rrp->rdata.mx.exch);
            break;
        case TYPE_SOA:
            i = search_rrp->rdata.soa.serial != target_rrp->rdata.soa.serial;
            break;
        default:
            i = -32755;     /* unsupported */
    }
    return i;
}
  
static int
compare_rr_list(rrlp,target_rrp)
register struct rr *rrlp,*target_rrp;
{
    while(rrlp != NULLRR){
        if(compare_rr(rrlp,target_rrp) == 0)
            return 0;
#ifdef DEBUGDNS_PAIN
        if(Dtrace)
            PRINTF("%15d %s\n", compare_rr(rrlp,target_rrp), target_rrp->name);
#endif
        rrlp = rrlp->next;
    }
    return -32767;
}
  
/* Make a new copy of a resource record */
static struct rr *
copy_rr(rrp)
register struct rr *rrp;
{
    register struct rr *newrr;
  
    if(rrp == NULLRR)
        return NULLRR;
  
    newrr = (struct rr *)callocw(1,sizeof(struct rr));
    newrr->source = rrp->source;
    newrr->name =   j2strdup(rrp->name);
    newrr->type =   rrp->type;
    newrr->class =  rrp->class;
    newrr->ttl =    rrp->ttl;
    if(rrp->suffix != NULLCHAR)
        newrr->suffix = j2strdup(rrp->suffix);
    if((newrr->rdlength = rrp->rdlength) == 0)
        return newrr;
  
    switch(rrp->type){
        case TYPE_A:
            newrr->rdata.addr = rrp->rdata.addr;
            break;
#ifdef	IPV6
	/*
	 * 10Apr2023, Maiko (VE4KLM), Adding IPV6 support
	 *  (rfc 3596 (Oct 2003), obsoletes 3152, 1886)
	 */
        case TYPE_AAAA:
#ifdef DONT_COMPILE
	/* 15Apr2023, Maiko, I'm being silly, just define it
	 * as an actually buffer in the structure, no malloc !
	 */
		newrr->rdata.addr6 = mallocw (16);	/* 13Apr2023, Maiko */
#endif
            copyipv6addr (rrp->rdata.addr6, newrr->rdata.addr6);
            break;
#endif
        case TYPE_CNAME:
        case TYPE_MB:
        case TYPE_MG:
        case TYPE_MR:
        case TYPE_NS:
        case TYPE_PTR:
        case TYPE_TXT:
            newrr->rdata.name = j2strdup(rrp->rdata.name);
            break;
        case TYPE_HINFO:
            newrr->rdata.hinfo.cpu = j2strdup(rrp->rdata.hinfo.cpu);
            newrr->rdata.hinfo.os = j2strdup(rrp->rdata.hinfo.os);
            break;
        case TYPE_MX:
            newrr->rdata.mx.pref = rrp->rdata.mx.pref;
            newrr->rdata.mx.exch = j2strdup(rrp->rdata.mx.exch);
            break;
        case TYPE_SOA:
            newrr->rdata.soa.mname =        j2strdup(rrp->rdata.soa.mname);
            newrr->rdata.soa.rname =        j2strdup(rrp->rdata.soa.rname);
            newrr->rdata.soa.serial =       rrp->rdata.soa.serial;
            newrr->rdata.soa.refresh =      rrp->rdata.soa.refresh;
            newrr->rdata.soa.retry =        rrp->rdata.soa.retry;
            newrr->rdata.soa.expire =       rrp->rdata.soa.expire;
            newrr->rdata.soa.minimum =      rrp->rdata.soa.minimum;
            break;
    }
    return newrr;
}
  
static struct rr *
copy_rr_list(rrlp)
register struct rr *rrlp;
{
    register struct rr **rrpp;
    struct rr *result_rrlp;
  
    rrpp = &result_rrlp;
    while(rrlp != NULLRR){
        *rrpp = copy_rr(rrlp);
        rrpp = &(*rrpp)->next;
        rrlp = rrlp->next;
    }
    *rrpp = NULLRR;
    return result_rrlp;
}
  
/* Free (list of) resource records */
void
free_rr(rrlp)
register struct rr *rrlp;
{
    register struct rr *rrp;
  
    while((rrp = rrlp) != NULLRR){
        rrlp = rrlp->next;
  
        free(rrp->comment);
        free(rrp->suffix);
        free(rrp->name);
        if(rrp->rdlength > 0){
            switch(rrp->type){
                case TYPE_A:
                    break;  /* Nothing allocated in rdata section */
#ifdef	IPV6
		/*
		 * 10Apr2023, Maiko (VE4KLM), Adding IPV6 support
		 *  (rfc 3596 (Oct 2003), obsoletes 3152, 1886)
		 */
                case TYPE_AAAA:
                    break;  /* Nothing allocated in rdata section */
#endif
                case TYPE_CNAME:
                case TYPE_MB:
                case TYPE_MG:
                case TYPE_MR:
                case TYPE_NS:
                case TYPE_PTR:
                case TYPE_TXT:
                    free(rrp->rdata.name);
                    break;
                case TYPE_HINFO:
                    free(rrp->rdata.hinfo.cpu);
                    free(rrp->rdata.hinfo.os);
                    break;
                case TYPE_MX:
                    free(rrp->rdata.mx.exch);
                    break;
                case TYPE_SOA:
                    free(rrp->rdata.soa.mname);
                    free(rrp->rdata.soa.rname);
                    break;
            }
        }
        free((char *)rrp);
    }
}
  
static struct rr *
make_rr(source,dname,dclass,dtype,ttl,rdl,data)
int source;
char *dname;
int16 dclass;
int16 dtype;
int32 ttl;
int16 rdl;
void *data;
{
    register struct rr *newrr;
  
    newrr = (struct rr *)callocw(1,sizeof(struct rr));
    newrr->source = source;
    newrr->name = j2strdup(dname);
    newrr->class = dclass;
    newrr->type = dtype;
    newrr->ttl = ttl;
    if((newrr->rdlength = rdl) == 0)
        return newrr;
  
    switch(dtype){
        case TYPE_A:
        {
            register int32 *ap = (int32 *)data;
            newrr->rdata.addr = *ap;
            break;
        }
#ifdef IPV6
	/*
	 * 10Apr2023, Maiko (VE4KLM), Adding IPV6 support
	 *  (rfc 3596 (Oct 2003), obsoletes 3152, 1886)
	 */
        case TYPE_AAAA:
        {
            copyipv6addr ((unsigned char*)data, newrr->rdata.addr6);
            break;
        }
#endif
        case TYPE_CNAME:
        case TYPE_MB:
        case TYPE_MG:
        case TYPE_MR:
        case TYPE_NS:
        case TYPE_PTR:
        case TYPE_TXT:
        {
            newrr->rdata.name = j2strdup((char *)data);
            break;
        }
        case TYPE_HINFO:
        {
            register struct hinfo *hinfop = (struct hinfo *)data;
            newrr->rdata.hinfo.cpu = j2strdup(hinfop->cpu);
            newrr->rdata.hinfo.os = j2strdup(hinfop->os);
            break;
        }
        case TYPE_MX:
        {
            register struct mx *mxp = (struct mx *)data;
            newrr->rdata.mx.pref = mxp->pref;
            newrr->rdata.mx.exch = j2strdup(mxp->exch);
            break;
        }
        case TYPE_SOA:
        {
            register struct soa *soap = (struct soa *)data;
            newrr->rdata.soa.mname =        j2strdup(soap->mname);
            newrr->rdata.soa.rname =        j2strdup(soap->rname);
            newrr->rdata.soa.serial =       soap->serial;
            newrr->rdata.soa.refresh =      soap->refresh;
            newrr->rdata.soa.retry =        soap->retry;
            newrr->rdata.soa.expire =       soap->expire;
            newrr->rdata.soa.minimum =      soap->minimum;
            break;
        }
    }
    return newrr;
}
  
  
/**
 **     Domain Cache Utilities
 **/
  
static void
dcache_add(rrlp)
register struct rr *rrlp;
{
    register struct rr *last_rrp;
    struct rr *save_rrp;
  
    if(rrlp == NULLRR)
        return;
  
    save_rrp = rrlp;
    last_rrp = NULLRR;
    while(rrlp != NULLRR){
        rrlp->last = last_rrp;
        last_rrp = rrlp;
        rrlp = rrlp->next;
    }
    last_rrp->next = Dcache;
    if(Dcache != NULLRR)
        Dcache->last = last_rrp;
    Dcache = save_rrp;
}
  
static void
dcache_drop(rrp)
register struct rr *rrp;
{
    if(rrp->last != NULLRR)
        rrp->last->next = rrp->next;
    else
        Dcache = rrp->next;
    if(rrp->next != NULLRR)
        rrp->next->last = rrp->last;
    rrp->last =
    rrp->next = NULLRR;
}
  
/* Search cache for resource records, removing them from the cache.
 * Also, timeout cache entries, and trim cache to size.
 * (Calling with NULLRR is legal -- will timeout & trim only.)
 * Note that an answer from the cache cannot be authoritative, because
 * we cannot guarantee that all the entries remain from a previous request.
 * Returns RR list, or NULLRR if no record found.
 */
static struct rr *
dcache_search(rrlp)
struct rr *rrlp;
{
    register struct rr *rrp, *test_rrp;
    struct rr **rrpp, *result_rrlp;
    int32 elapsed;
    time_t now;
    int count = 0;
  
#ifdef DEBUGDNS
    if(Dtrace && rrlp != NULLRR){
        PRINTF("dcache_search: searching for %s\n",rrlp->name);
    }
#endif
  
    elapsed = (int32)(time(&now) - Dcache_time);
    Dcache_time = now;
  
    rrpp = &result_rrlp;
    for(rrp = Dcache; (test_rrp = rrp) != NULLRR;){
        rrp = rrp->next;
                    /* timeout entries */
        if(test_rrp->ttl > 0 && (test_rrp->ttl -= elapsed) <= 0)
            test_rrp->ttl = 0;
  
        if(compare_rr_list(rrlp,test_rrp) == 0){
            dcache_drop( *rrpp = test_rrp );
            rrpp = &(*rrpp)->next;
/*        } else if(test_rrp->source == RR_FILE && ++count > Dcache_size){ */
        } else if(++count > Dcache_size){
            dcache_drop(test_rrp);
            free_rr(test_rrp);
        }
    }
    *rrpp = NULLRR;
    return result_rrlp;
}
  
/* Move a list of resource records to the cache, removing duplicates. */
static void
dcache_update(rrlp)
register struct rr *rrlp;
{
    if(rrlp == NULLRR)
        return;
  
    free_rr(dcache_search(rrlp));   /* remove duplicates, first */
    dcache_add(rrlp);
}
  
  
/**
 **     File Utilities
 **/
  
static struct rr *
get_rr(fp,lastrrp)
FILE *fp;
struct rr *lastrrp;
{
    char *line,*lp;
    struct rr *rrp;
    char *name,*ttl,*class,*type,*data;
    int i;
  
    line = loc_getline(fp);
    if(line == NULLCHAR)            /* eof or error */
        return NULLRR;
  
    rrp = (struct rr *)callocw(1,sizeof(struct rr));
    rrp->source = RR_FILE;
  
    if(line[0] == '$') {
        data = strtok(line,delim);
        if(strnicmp(data,"$origin",7) == 0) {
            data = strtok(NULLCHAR,delim);
            if (data) {
                rrp->suffix = j2strdup(data);
                line[strlen(line)] = '\t';  /* merge $origin + suffix in comment */
            } /* else rrp->suffix is (still) null */
            rrp->comment = j2strdup(line);
            rrp->type = TYPE_MISSING;
            free(line);
            return rrp;
        }
    } else {
        if(lastrrp != NULLRR)
            if(lastrrp->suffix != NULLCHAR)
                rrp->suffix = j2strdup(lastrrp->suffix);
    }
  
    if(!isspace(line[0]) || lastrrp == NULLRR){
        name = strtok(line,delim);
        lp = NULLCHAR;
    } else {        /* Name field is missing */
        name = lastrrp->name;
        lp = line;
    }
  
    /* KO4KS: set rrp->suffix to Dsuffix, if defined, when rrp->suffix is null */
    if (Dsuffix != NULLCHAR && rrp->suffix == NULLCHAR)
        rrp->suffix = j2strdup(Dsuffix);

    if(name[0] == '@') {
        if(rrp->suffix != NULLCHAR)
            name = rrp->suffix;
        else
            name = "ampr.org.";
    }
  
    i=strlen(name);
    if(name[i-1] != '.'){
        /* Tack on the current domain suffix if defined */
        if(rrp->suffix != NULLCHAR) {
            rrp->name = mallocw(i+strlen(rrp->suffix)+2);
            sprintf(rrp->name,"%s.%s",name,rrp->suffix);
        } else {
        /* Tack on a trailing period if it's not there */
            rrp->name = mallocw((unsigned)i+2);
            strcpy(rrp->name,name);
            strcat(rrp->name,".");
        }
    } else
        /* fully qualified domain name */
        rrp->name = j2strdup(name);
  
    ttl = strtok(lp,delim);
  
    if(ttl == NULLCHAR || (!isdigit(ttl[0]) && ttl[0] != '-')){
        /* Optional ttl field is missing */
        rrp->ttl = TTL_MISSING;
        class = ttl;
    } else {
	/* 11Oct2009, Maiko, Don't use atol on int32 vars */
        rrp->ttl = atoi(ttl);
        class = strtok(NULLCHAR,delim);
    }
  
    if(class == NULLCHAR){
        /* we're in trouble, but keep processing */
        rrp->class = CLASS_MISSING;
        type = class;
    } else if(class[0] == '<'){
        rrp->class = atoi(&class[1]);
        type = strtok(NULLCHAR,delim);
    } else if(stricmp(class,"IN") == 0){
        rrp->class = CLASS_IN;
        type = strtok(NULLCHAR,delim);
    } else {
        /* Optional class field is missing; assume IN */
        rrp->class = CLASS_IN;
        type = class;
    }
  
    if(type == NULLCHAR){
        /* we're in trouble, but keep processing */
        rrp->type = TYPE_MISSING;
        data = type;
    } else if(type[0] == '{'){
        rrp->type = atoi(&class[1]);
        data = strtok(NULLCHAR,delim);
    } else {
        rrp->type = TYPE_MISSING;
        for(i=1;i<NDTYPES;i++){
            if(stricmp(type,Dtypes[i]) == 0){
                rrp->type = i;
                data = strtok(NULLCHAR,delim);
                break;
            }
        }
    }
  
    if(rrp->type == TYPE_MISSING){
        data = NULLCHAR;
    }
  
    if(data == NULLCHAR){
        /* Empty record, just return */
        free(line);
        return rrp;
    }
    switch(rrp->type){
        case TYPE_A:
            rrp->rdlength = 4;
            rrp->rdata.addr = aton(data);
            break;
#ifdef	IPV6
	/*
	 * 10Apr2023, Maiko (VE4KLM), Adding IPV6 support
	 *  (rfc 3596 (Oct 2003), obsoletes 3152, 1886)
	 */
        case TYPE_AAAA:
            rrp->rdlength = 16;
            copyipv6addr ((unsigned char*)data, rrp->rdata.addr6);
            break;
#endif
        case TYPE_CNAME:
            i = strlen(data);
            if (data[i-1] != '.') { /* not fully qualified */
                if(rrp->suffix != NULLCHAR) {
                    rrp->rdata.name = mallocw(i+strlen(rrp->suffix)+2);
                    sprintf(rrp->rdata.name, "%s.%s", data, rrp->suffix);
                } else {
                    rrp->rdata.name = mallocw(i + 2);
                    strcpy (rrp->rdata.name, data);
                    strcat (rrp->rdata.name, ".");
                }
            } else
                rrp->rdata.name = j2strdup(data);
            rrp->rdlength = strlen(rrp->rdata.name);
            break;
        case TYPE_MB:
        case TYPE_MG:
        case TYPE_MR:
        case TYPE_NS:
        case TYPE_PTR:
        case TYPE_TXT:
            rrp->rdlength = strlen(data);
            rrp->rdata.name = j2strdup(data);
            break;
        case TYPE_HINFO:
            rrp->rdlength = strlen(data);
            rrp->rdata.hinfo.cpu = j2strdup(data);
            if((data = strtok(NULLCHAR,delim)) != NULLCHAR){
                rrp->rdlength += strlen(data);
                rrp->rdata.hinfo.os = j2strdup(data);
            }
            break;
        case TYPE_MX:
            rrp->rdata.mx.pref = atoi(data);
            rrp->rdlength = 2;
  
        /* Get domain name of exchanger */
            if((data = strtok(NULLCHAR,delim)) != NULLCHAR){
                rrp->rdlength += strlen(data);
                rrp->rdata.mx.exch = j2strdup(data);
            }
            break;
        case TYPE_SOA:
        /* Get domain name of master name server */
            rrp->rdlength = strlen(data);
            rrp->rdata.soa.mname = j2strdup(data);
  
        /* Get domain name of irresponsible person */
            if((data = strtok(NULLCHAR,delim)) != NULLCHAR){
                rrp->rdata.soa.rname = j2strdup(data);
                rrp->rdlength += strlen(data);
            }

	/* 11Oct2009, Maiko, Don't use atol on int32 vars */
            data = strtok(NULLCHAR,delim);
            rrp->rdata.soa.serial = atoi(data);
            data = strtok(NULLCHAR,delim);
            rrp->rdata.soa.refresh = atoi(data);
            data = strtok(NULLCHAR,delim);
            rrp->rdata.soa.retry = atoi(data);
            data = strtok(NULLCHAR,delim);
            rrp->rdata.soa.expire = atoi(data);
            data = strtok(NULLCHAR,delim);
            rrp->rdata.soa.minimum = atoi(data);
            rrp->rdlength += 20;
            break;
    }
  
    /* !!! need to handle trailing comments */
    free(line);
    return rrp;
}
  
/* Print a resource record */
static void
put_rr(fp,rrp)
FILE *fp;
struct rr *rrp;
{
    int trans;
  
    if(fp == NULLFILE || rrp == NULLRR)
        return;
  
    if(rrp->name == NULLCHAR && rrp->comment != NULLCHAR){
        FPRINTF(fp,"%s\n",rrp->comment);  /* probably $origin */
        return;
    }
  
    FPRINTF(fp,"%s",rrp->name);

	/* 11Oct2009, Maiko, Use "%d" for int32 vars */
    if(rrp->ttl != TTL_MISSING)
        FPRINTF(fp,"\t%d",rrp->ttl);

    if(rrp->class == CLASS_IN)
        FPRINTF(fp,"\tIN");
    else
        FPRINTF(fp,"\t<%u>",rrp->class);
    FPRINTF(fp,"\t%s",dtype(rrp->type));
    if(rrp->rdlength == 0){
        /* Null data portion, indicates nonexistent record */
        /* or unsupported type.  Hopefully, these will filter */
        /* as time goes by. */
        FPRINTF(fp,"\n");
        return;
    }
    switch(rrp->type){
        case TYPE_A:
            trans = DTranslate;             /* Save IP address translation state */
            DTranslate = 0;                 /* Force output to be numeric IP addr */
            FPRINTF(fp,"\t%s\n",inet_ntoa(rrp->rdata.addr));
            DTranslate = trans;             /* Restore original state */
            break;
#ifdef	IPV6
	/* 13Apr2023, Maiko */
	case TYPE_AAAA:
            trans = DTranslate;             /* Save IP address translation state */
            DTranslate = 0;                 /* Force output to be numeric IP addr */
            FPRINTF(fp,"\t%s\n", ipv6shortform (human_readable_ipv6 (rrp->rdata.addr6), 0));
            DTranslate = trans;             /* Restore original state */
		break;
#endif
        case TYPE_CNAME:
        case TYPE_MB:
        case TYPE_MG:
        case TYPE_MR:
        case TYPE_NS:
        case TYPE_PTR:
        case TYPE_TXT:
        /* These are all printable text strings */
            FPRINTF(fp,"\t%s\n",rrp->rdata.data);
            break;
        case TYPE_HINFO:
            FPRINTF(fp,"\t%s\t%s\n",rrp->rdata.hinfo.cpu,rrp->rdata.hinfo.os);
            break;
        case TYPE_MX:
            FPRINTF(fp,"\t%u\t%s\n", rrp->rdata.mx.pref, rrp->rdata.mx.exch);
            break;
        case TYPE_SOA:
	/* 12Oct2009, Maiko, int32 vars should be "%d" format */
            FPRINTF(fp,"\t%s\t%s\t%d\t%d\t%d\t%d\t%d\n",
            rrp->rdata.soa.mname,rrp->rdata.soa.rname,
            rrp->rdata.soa.serial,rrp->rdata.soa.refresh,
            rrp->rdata.soa.retry,rrp->rdata.soa.expire,
            rrp->rdata.soa.minimum);
            break;
        default:
            FPRINTF(fp,"\n");
            break;
    }
}
  
/* Search local database for resource records.
 * Returns RR list, or NULLRR if no record found.
 */
static struct rr *
dfile_search(rrlp)
struct rr *rrlp;
{
    register struct rr *frrp;
    struct rr **rrpp, *result_rrlp, *oldrrp;
    int32 elapsed;
    FILE *dbase;
    struct stat dstat;
  
#ifdef DEBUGDNS
    if(Dtrace){
        PRINTF("dfile_search: searching for %s\n",rrlp->name);
    }
#endif
  
    while(Dfile_writing > 0)
        pwait(&Dfile_reading);
    Dfile_reading++;
  
    if((dbase = fopen(Dfile,READ_TEXT)) == NULLFILE){
        Dfile_reading--;
        return NULLRR;
    }
    if(fstat(fileno(dbase),&dstat) != 0){
        j2tputs("dfile_search: can't get file status\n");
        fclose(dbase);
        Dfile_reading--;
        return NULLRR;
    }
    if((elapsed = (int32)(Dcache_time - (time_t)dstat.st_ctime)) < 0)
        elapsed = -elapsed;     /* arbitrary time mismatch */
  
    result_rrlp = NULLRR;           /* for contiguous test below */
    oldrrp = NULLRR;
    rrpp = &result_rrlp;
    while((frrp = get_rr(dbase,oldrrp)) != NULLRR){
        free_rr(oldrrp);
        if(frrp->type != TYPE_MISSING
            && frrp->rdlength > 0
        && compare_rr_list(rrlp,frrp) == 0){
            if(frrp->ttl > 0 && (frrp->ttl -= elapsed) <= 0)
                frrp->ttl = 0;
            *rrpp = frrp;
            rrpp = &(*rrpp)->next;
            oldrrp = copy_rr(frrp);
        } else {
            oldrrp = frrp;
            /*
                All records of the same name and the same type
                are contiguous.  Therefore, for a single query,
                we can stop searching.  Multiple queries must
                read the whole file.
            */
            if(rrlp->type != TYPE_ANY && rrlp->next == NULLRR
                && result_rrlp != NULLRR){
                break;
            }
        }
        if(!main_exit)
            pwait(NULL);    /* run multiple sessions */
    }
    free_rr(oldrrp);
    *rrpp = NULLRR;
  
    fclose(dbase);
  
    if(--Dfile_reading <= 0){
        Dfile_reading = 0;
        j2psignal(&Dfile_writing,0);
    }
  
    return result_rrlp;
}
  
/* Process which will add new resource records from the cache
 * to the local file, eliminating duplicates while it goes.
 */
static void
dfile_update(s,unused,p)
int s;
void *unused;
void *p;
{
    struct rr **rrpp, *rrlp, *oldrrp;
    char *newname;
    FILE *old_fp, *new_fp;
    struct stat old_stat, new_stat;
 
	if (dns_debug) 
    	log(-1,"update Domain.txt initiated");
  
    close_s(Curproc->input);
    Curproc->input = -1;
    close_s(Curproc->output);
    Curproc->output = -1;
  
    newname = j2strdup(Dfile);
    strcpy(&newname[strlen(newname)-3],"tmp");
  
    while(Dfile_wait_absolute != 0 && !main_exit){
        register struct rr *frrp;
        int32 elapsed;
  
        while(Dfile_wait_absolute != 0){
            elapsed = Dfile_wait_absolute - secclock();
            Dfile_wait_absolute = 0;
            if(elapsed > 0 && !main_exit){
                j2alarm(elapsed*1000);
                pwait(&Dfile_wait_absolute);
                j2alarm(0);
            }
        }
  
		if (dns_debug) 
        	log(-1,"update Domain.txt");
  
        /* create new file for copy */
        if((new_fp = fopen(newname,WRITE_TEXT)) == NULLFILE){
            PRINTF("dfile_update: can't create %s!\n",newname);
            break;
        }
        if(fstat(fileno(new_fp),&new_stat) != 0){
            PRINTF("dfile_update: can't get new_file status!\n");
            fclose(new_fp);
            break;
        }
  
        pwait(NULL);    /* file operations can be slow */
  
        /* timeout the cache one last time before writing */
        (void)dcache_search(NULLRR);
  
        /* copy new RRs out to the new file */
        /* (can't wait here, the cache might change) */
        rrpp = &rrlp;
        for(frrp = Dcache; frrp != NULLRR; frrp = frrp->next ){
            switch(frrp->source){
                case RR_QUESTION:
                case RR_ANSWER:
                case RR_AUTHORITY:
                case RR_ADDITIONAL:
                    *rrpp = copy_rr(frrp);
                    if(frrp->type != TYPE_MISSING
                        && frrp->rdlength > 0)
                        put_rr(new_fp,frrp);
                    rrpp = &(*rrpp)->next;
                    frrp->source = RR_FILE;
                    break;
            }
        }
        *rrpp = NULLRR;
  
        /* open up the old file, concurrently with everyone else */
        if((old_fp = fopen(Dfile,READ_TEXT)) == NULLFILE){
            /* great! no old file, so we're ready to go. */
            fclose(new_fp);
            rename(newname,Dfile);
            free_rr(rrlp);
            break;
        }
        if(fstat(fileno(old_fp),&old_stat) != 0){
            PRINTF("dfile_update: can't get old_file status!\n");
            fclose(new_fp);
            fclose(old_fp);
            free_rr(rrlp);
            break;
        }
        if((elapsed = (int32)(new_stat.st_ctime - old_stat.st_ctime)) < 0)
            elapsed = -elapsed;     /* file times are inconsistant */
  
        /* Now append any non-duplicate records */
        oldrrp = NULLRR;
        while((frrp = get_rr(old_fp,oldrrp)) != NULLRR){
            free_rr(oldrrp);
            if(frrp->name == NULLCHAR
                && frrp->comment != NULLCHAR)
                put_rr(new_fp,frrp);
            if(frrp->type != TYPE_MISSING
                && frrp->rdlength > 0
            && compare_rr_list(rrlp,frrp) != 0){
                if(frrp->ttl > 0 && (frrp->ttl -= elapsed) <= 0)
                    frrp->ttl = 0;
                if(frrp->ttl != 0 || !Dfile_clean)
                    put_rr(new_fp,frrp);
            }
            oldrrp = frrp;
            if(!main_exit)
                pwait(NULL);    /* run in background */
        }
        free_rr(oldrrp);
        fclose(new_fp);
        fclose(old_fp);
        free_rr(rrlp);
  
        /* wait for everyone else to finish reading */
        Dfile_writing++;
        while(Dfile_reading > 0)
            pwait(&Dfile_writing);
  
        unlink(Dfile);
        rename(newname,Dfile);
  
        Dfile_writing = 0;
        j2psignal(&Dfile_reading,0);
    }
    free(newname);
  
	if (dns_debug) 
    	log(-1,"update Domain.txt finished");

    Dfile_updater = NULLPROC;
}
  
  
/**
 **     Domain Server Utilities
 **/
  
static void
dumpdomain(dhp,rtt)
struct dhdr *dhp;
int32 rtt;
{
    struct rr *rrp;
  
	/* 12Oct2009, Maiko, int32 var should use "%d" format */
    PRINTF("response id %u (rtt %d ms) qr %u opcode %u aa %u tc %u rd %u ra %u rcode %u\n",
    dhp->id,rtt,
    dhp->qr,dhp->opcode,dhp->aa,dhp->tc,dhp->rd,
    dhp->ra,dhp->rcode);
    PRINTF("%u questions:\n",dhp->qdcount);
    for(rrp = dhp->questions; rrp != NULLRR; rrp = rrp->next){
        PRINTF("%s type %s class %u\n",rrp->name,
        dtype(rrp->type),rrp->class);
    }
    PRINTF("%u answers:\n",dhp->ancount);
    for(rrp = dhp->answers; rrp != NULLRR; rrp = rrp->next){
        put_rr(stdout,rrp);
    }
    PRINTF("%u authority:\n",dhp->nscount);
    for(rrp = dhp->authority; rrp != NULLRR; rrp = rrp->next){
        put_rr(stdout,rrp);
    }
    PRINTF("%u additional:\n",dhp->arcount);
    for(rrp = dhp->additional; rrp != NULLRR; rrp = rrp->next){
        put_rr(stdout,rrp);
    }
#ifndef UNIX
    fflush(stdout);
#endif
}
  
static int
dns_makequery(op,srrp,buffer,buflen)
int16 op;       /* operation */
struct rr *srrp;/* Search RR */
char *buffer;   /* Area for query */
int16 buflen;   /* Length of same */
{
    char *cp,*cp1;
    char *dname, *sname;
    int16 parameter;
    int16 dlen,len;
 
    int64 dns_idll;
    int16 dns_id;

#if 1
    /* We don't handle INQUERY RRs at present (eg, srrp->name could be null) */
    while(op==QUERY && srrp->source==RR_INQUERY)
        if((srrp = srrp->next) == NULLRR)
            return 0;  /* aberrant case, no RR_QUERY recs */
#endif
    cp = buffer;

    /* 07Aug2010, Maiko (VE4KLM), account for fact that msclock() is 64 bit */
    dns_idll = msclock () % MAXSHORT;
    dns_id = (int16)dns_idll;

    /* Use millisecond clock for timestamping */
    cp = put16(cp, dns_id);

    parameter = (op << 11)
    | 0x0100;       /* Recursion desired */
    cp = put16(cp,parameter);
    cp = put16(cp,1);
    cp = put16(cp,0);
    cp = put16(cp,0);
    cp = put16(cp,0);
  
    sname = j2strdup(srrp->name);
    dname = sname;
    dlen = strlen(dname);
    for(;;){
        /* Look for next dot */
        cp1 = strchr(dname,'.');
        if(cp1 != NULLCHAR)
            len = (int16)(cp1-dname);        /* More to come */
        else
            len = dlen;     /* Last component */
        *cp++ = len;            /* Write length of component */
        if(len == 0)
            break;
        /* Copy component up to (but not including) dot */
        strncpy(cp,dname,(size_t)len);
        cp += len;
        if(cp1 == NULLCHAR){
            *cp++ = 0;      /* Last one; write null and finish */
            break;
        }
        dname += len+1;
        dlen -= len+1;
    }
    free(sname);
    cp = put16(cp,srrp->type);
    cp = put16(cp,srrp->class);
    return (int)(cp - buffer);
}

/*
 * 28Nov2004, Maiko, This function written since GOTO labels seem to
 * have become DEPRECATED in my compiler version, and apparently are no
 * longer supported on the compiler that ships with Fedora Core 3. Goto
 * lables promote bad code structure anyways, so better fix it now !
 */
static void do_exit_query (struct rr *rrlp)
{
#ifdef QUERYPROC
    j2psignal(rrlp,1);   /* wakeup parent, indicating *result_rrlp is ready */
    Curproc->session->proc1 = NULLPROC;  /* undo */
#endif
	return;
}
  
/* domain server resolution loop
 * returns: any answers in cache, plus in the provided result RR.
 *      (future features:   multiple queries,   inverse queries.)
 * dns_query is now (1.11x6) a separate process (see resolver()). -- k2mf/n5knx
 * Invoke by: 	if (newproc("dns_query",512,dns_query,0,(void *)query_rrp,
 *                  (void *)&result_rrlp,0) != NULLPROC)
 *              pwait(query_rrp);  else ...;
 */
static void
dns_query(int unused, void *v1, void *v2)
{
    struct rr *rrlp = (struct rr *)v1;
    struct rr **result_rrlp = (struct rr **)v2;
    struct rr *lrrp, *nrrp, *xrrp;
    struct mbuf *bp;
    struct dhdr *dhp;
    struct dserver *dp;     /* server list */
    int32 rtt,abserr;
    int64 dns_idll;
    int16 dns_id;
    int tried = 0;          /* server list has been retried (count) */

    int s, rval;	/* 20Mar2021, Maiko, Moved to bigger scope */

#ifdef	IPV6
	struct j2sockaddr_in6 server_in6;	/* 10Apr2023, Maiko */
#endif
    struct sockaddr_in server_in;	/* 23Mar2021, Need it out here now */
  
#ifdef QUERYPROC
    /* First task: put our process-id into the parent session's proc1 variable,
        so that the reset command can alert us (thereby aborting any pwait())
        and so force an early exit.   -- k2mf/n5knx jnos 1.11x6 */
    Curproc->session->proc1 = Curproc;
#endif
        
    if((*result_rrlp = dcache_search(rrlp)) == NULLRR){
        *result_rrlp = dfile_search(rrlp);
    }
    if(*result_rrlp == NULLRR || check_ttl(*result_rrlp) != 0){
        dcache_add(*result_rrlp);        /* save any expired RRs. NULLRR OK. */
        *result_rrlp = NULLRR;           /* result_rrlp not valid now */

        if((dp = Dservers) != NULLDOM && Mprunning){
            for(;;){
                char *buf;
                int len;
                // struct sockaddr_in server_in;
                // int s;
                // int rval;
  
                dp->queries++;
 
#ifdef IPV6 
			if (dp->ipver == IPV6VERSION)
			{
               	if ((s = j2socket(AF_INET6,SOCK_DGRAM,0)) == -1)
    			{
        			log (-1, "ipv6 dns no socket");
                   	return (do_exit_query (rrlp));
    			}

	    // log (-1, "dns_query src %s", human_readable_ipv6 (dp->address6));

                server_in6.sin6_family = AF_INET6;
                server_in6.sin6_port = IPPORT_DOMAIN;
                copyipv6addr (dp->address6, server_in6.sin6_addr.s6_addr);

                if(Dtrace)
				{
                    PRINTF("dns_query: querying server %s for %s\n",
 	       			ipv6shortform (human_readable_ipv6 (dp->address6), 0),
                    rrlp->name);
                }
			}
			else
			{
#endif
                s = j2socket(AF_INET,SOCK_DGRAM,0);
    		/* 23Mar2021, Maiko, we should be checking return value */
    			if (s == -1)
    			{
        			log (-1, "dns no socket");
                   	return (do_exit_query (rrlp));
    			}

                server_in.sin_family = AF_INET;
                server_in.sin_port = IPPORT_DOMAIN;
                server_in.sin_addr.s_addr = dp->address;

                if(Dtrace){
                    PRINTF("dns_query: querying server %s for %s\n",
                    inet_ntoa(dp->address),rrlp->name);
                }
#ifdef IPV6 
			}
#endif
                buf = mallocw(512);
                len = dns_makequery(0,rrlp,buf,512);
#ifdef IPV6 
			if (dp->ipver == IPV6VERSION)
                rval=j2sendto(s,buf,len,0,(char *)&server_in6,sizeof(server_in6));
			else
#endif
                rval=j2sendto(s,buf,len,0,(char *)&server_in,sizeof(server_in));
                free(buf);
                if(rval != -1) {
                    j2alarm(max(dp->timeout, 100));
                    /* Wait for something to happen */
                    rval = recv_mbuf(s,&bp,0,NULLCHAR,0);
                    j2alarm(0);
                }
		/*
		 * 20Mar2021, Maiko (VE4KLM), I need the socket information for the
		 * recently modified ntohdomain () function call further below, so
		 * need to move this further down and look for it multiple times.
		 *
                close_s(s);
		 *
	 	 * 23Mar2021, Maiko, Actually not anymore, but leave it for now,
		 * and let things run for a while, now passing socket structure
		 * instead of the socket number - only way to log IP address !
		 */
                if(Dtrace){
                    PRINTF("dns_query: received message length %d, errno %d\n", rval, errno );
                }

                if(rval > 0)	/* note 's' is still open now at this point 20Mar2021 */
                    break;

                close_s(s);	/* 20Mar2021, Maiko (VE4KLM), make sure to close it now */
  
				/* Killed by "reset" command */
                if (errno == EABORT)
                    return (do_exit_query (rrlp));
  
                /* Timeout; back off this one and try another server */
                dp->timeouts++;
                dp->timeout <<= 1;
  
                /* But we must have some sort of sensible limit - surely? */
                if(dp->timeout > (Dserver_maxwait * 1000)){
                    dp->timeout = Dserver_maxwait * 1000;
                }

                if((dp = dp->next) == NULLDOM){
                    dp = Dservers;
                    if (Dserver_retries > 0 && ++tried > Dserver_retries)
                    	return (do_exit_query (rrlp));
                }
            } /* ends for() loop */
 
            /* got a response */
            dp->responses++;
            dhp = (struct dhdr *) mallocw(sizeof(struct dhdr));

	/*
         * 19Mar2021, Maiko (VE4KLM), new socket parameter, and seriously ! Why
         * are people not checking the return value of this function, especially
	 * if it is already doing error checks, plus my new security fixes ...
	 *  (20Mar2021, had to shuffle close_s (s) call to get this working)
	 *
	 * 23Mar2021, Maiko, Added ptype parameter, 0 means dns reply, and we
	 * need to use nos_log_peerless NOT nos_log for UDP clients, I forgot
	 * about that, that's why IP ADDRESS is not being logged, similar to
	 * how I did it in the SNMPD code, which is why I created peerless.
	 *
	 * So instead of passing 's', we pass the socket structure, so now
	 * wondering if it is really necessary to move the close_s (s) call,
	 * probably not, but leave it be for now, play with that after.
	 *
         */
#ifdef IPV6 
	/*
	 * 13Apr2023, Maiko, The fsock is just for peerless logging,
	 * that's all, BUT I'll have to modify the NLP for IPV6 too,
	 * so for now, just fake out the fsock with my local IPaddr.
	 *
	if (dp->ipver == IPV6VERSION)
	{
                server_in.sin_addr.s_addr = (uint32)Ip_addr;
                server_in.sin_port = IPPORT_DOMAIN;
		log (-1, "fake out IP for peerless logging, IPV6 it later");
	}
	 * 16Apr2023, Maiko, Don't fake it out anymore, now able to
	 * pass either type of socket to ntohdomain (using *void).
	 */
	if (dp->ipver == IPV6VERSION)
            rval = ntohdomain (&server_in6, 0, dhp,&bp);	/* Convert to local format */
	else
#endif
            rval = ntohdomain (&server_in, 0, dhp,&bp);	/* Convert to local format */

	    close_s (s); /* 20Mar2021, must close this now */

	    if (rval == -1)
               return (do_exit_query (rrlp));

  
    /* 07Aug2010, Maiko (VE4KLM), account for fact that msclock() is 64 bit */
            dns_idll = msclock () % MAXSHORT;
            dns_id = (int16)dns_idll;

            /* Compute and update the round trip time */
            rtt = (int32) (dns_id - dhp->id);

            abserr = rtt > dp->srtt ? rtt - dp->srtt : dp->srtt - rtt;
            dp->srtt = ((AGAIN-1) * dp->srtt + rtt + (AGAIN/2)) >> LAGAIN;
            dp->mdev = ((DGAIN-1) * dp->mdev + abserr + (DGAIN/2)) >> LDGAIN;
            dp->timeout = 4 * dp->mdev + dp->srtt;
  
            /* move to top of list for next time */
            if(dp->prev != NULLDOM){
                dlist_drop(dp);
                dlist_add(dp);
            }
  
            if(Dtrace)
                dumpdomain(dhp,rtt);
  
#ifdef RETAIN_NEGATIVE_REPLIES
            /* Add negative reply to answers.  This assumes that there was
             * only one question, which is true for all questions we send.
             */
            if(dhp->aa && (dhp->rcode == NAME_ERROR || dhp->ancount == 0))
			{
                register struct rr *rrp;
                int32 ttl = 600; /* Default TTL for negative records */
      
                /* look for SOA ttl */
                for(rrp = dhp->authority; rrp != NULLRR; rrp = rrp->next)
				{
                    if(rrp->type == TYPE_SOA)
                        ttl = rrp->ttl;
                }
  
                /* make the questions the negative answers */
                for(rrp = dhp->questions; rrp != NULLRR; rrp = rrp->next)
				{
                    rrp->ttl = ttl;
                    if (rrp->type == TYPE_ANY)
					{
                		free_rr(dhp->questions);
                		dhp->questions = NULLRR;
					 	break;
					}
                }
            }
			else
#endif
            {
                free_rr(dhp->questions);
                dhp->questions = NULLRR;
            }
  
            /* post in reverse order to maintain original order */
            if (dhp->arcount) dcache_update(dhp->additional);
            if (dhp->nscount) dcache_update(dhp->authority);
            if (dhp->ancount) dcache_update(dhp->answers);
            if (dhp->qdcount) dcache_update(dhp->questions);

            free((char *)dhp);
  
            Dfile_wait_absolute = secclock() + Dfile_wait_relative;
            if(Dfile_upd && Dfile_updater == NULLPROC){
                Dfile_updater = newproc("domain update",512,dfile_update,0,NULL,NULL,0);
            }
  
#ifdef DEBUGDNS
            if(Dtrace)
                keywait(NULLCHAR,1);    /* so we can look around */
#endif
        }
        *result_rrlp = dcache_search(rrlp);
    }

    dcache_add(copy_rr_list(*result_rrlp));   /* OK if NULLRR */

#ifdef RETAIN_NEGATIVE_REPLIES
/* N5KNX: dns_query() will store negative replies into the cache, so we must
   take care never to return them in the list of (valid) records.
*/
    xrrp = *result_rrlp;
    lrrp = NULLRR;
    while(xrrp!=NULLRR) {
        nrrp = xrrp->next;
        if (!(xrrp->rdlength)) {  /* remove it from list */
            if (lrrp != NULLRR)
                lrrp->next = nrrp;
            else
                *result_rrlp = nrrp;
            xrrp->next = NULLRR;
            free_rr(xrrp);
        }
        else {
            lrrp = xrrp;
        }
        xrrp = nrrp;
    }
#endif

	return (do_exit_query (rrlp));
}
  
  
/**
 **     Resolver Utilities
 **/
  
/* Return TRUE if string appears to be an IP address in dotted decimal;
 * return FALSE otherwise (i.e., if string is a domain name)
 */
static int
isaddr(s)
register char *s;
{
    char c;
  
    if(s == NULLCHAR)
        return TRUE;       /* Can't happen */
  
    while((c = *s++) != '\0'){
        if(c != '[' && c != ']' && !isdigit(c) && c != '.')
            return FALSE;
    }
    return TRUE;
}
  
/* Search for resource records.
 * Returns RR list, or NULLRR if no record found.
 */
static struct rr *
resolver(rrlp)
struct rr *rrlp;
{
    struct rr *result_rrlp = NULLRR;
  
    if (rrlp != NULLRR) {
#ifdef QUERYPROC
	/* DNS query process starting - K2MF */
	if (newproc("dns_query",512,dns_query,0,(void *)rrlp,
                    (void *)&result_rrlp,0) != NULLPROC)
            pwait(rrlp);
        else
            log(-1,"Can't fork dns_query proc");
#else
        dns_query(0,(void *)rrlp,(void *)&result_rrlp);
#endif
    }
    return result_rrlp;
}
  
/* general entry point for address -> domain name resolution.
 * Returns RR list, or NULLRR if no record found.
 */
struct rr *
inverse_a(ip_address)
int32 ip_address;
{
    struct rr *prrp;
    struct rr *result_rrlp;
    char pname[30];
  
    if(ip_address == 0)
        return NULLRR;
  
    sprintf( pname, "%u.%u.%u.%u.IN-ADDR.ARPA.",
    lobyte(loword(ip_address)),
    hibyte(loword(ip_address)),
    lobyte(hiword(ip_address)),
    hibyte(hiword(ip_address)) );
  
    prrp = make_rr(RR_QUERY,pname,CLASS_IN,TYPE_PTR,0,0,NULL);
  
    prrp->next =            /* make list to speed search */
    make_rr(RR_INQUERY,NULLCHAR,CLASS_IN,TYPE_A,0,4,&ip_address);
  
    result_rrlp = resolver(prrp);
  
    free_rr(prrp);
    return result_rrlp;
}
  
/* general entry point for domain name -> resource resolution.
 * Returns RR list, or NULLRR if no record found.
 */
/* Optional recursive search to resolve CNAME records that
   prevents proper handling of CNAME queries.  Forces recursion to take
   place only at the resolver of the node originating the query.
   Modification introducted by Don Sandstrom, KG7CP, October 15, 1992.
   Additional mod by WG7J .
   Mod by N5KNX to return CNAME + associated A record when possible, if recurse==0.
   This makes Jnos a better DNS server.
   */
  
struct rr *
resolve_rr(dname,dtype,recurse)
char *dname;
int16 dtype;
int recurse;
{
    struct rr *prrp,*qrrp,*lrrp;
    struct rr *result_rrlp;
    char *sname,*lastref,*prevref;
    int looping;
  
    if(dname == NULLCHAR)
        return NULLRR;
  
    sname = domainsuffix(dname);
    qrrp = make_rr(RR_QUERY,sname,CLASS_IN,dtype,0,0,NULL);
    free(sname);
  
    looping = MAXCNAME;
    if(!recurse) {
        result_rrlp = resolver(qrrp);
        prevref=NULLCHAR;
        while(looping > 0) {
            looping--;
            prrp=result_rrlp;
            lastref=NULLCHAR;
            while(prrp) {
                if (prrp->type != dtype && dtype != TYPE_ANY) /* CNAME || PTR */
                    lastref = prrp->rdata.name;
                lrrp = prrp;
                prrp = prrp->next;
            }
            if (lastref && prevref != lastref) {
                prevref=lastref;
                free(qrrp->name);
                qrrp->name = j2strdup(lastref);
                lrrp->next = resolver(qrrp);   /* add to end of result chain */
            }
            else break;
        }
#ifdef DEBUGDNS
        if(Dtrace)
            put_rr(stdout,result_rrlp);
#endif
    } else {
        while(looping > 0){
            if((result_rrlp=resolver(qrrp)) == NULLRR
                || (result_rrlp->type == dtype || dtype == TYPE_ANY))
                break;
#ifdef DEBUGDNS
            if(Dtrace)
                put_rr(stdout,result_rrlp);
#endif
        /* Should be CNAME or PTR record */
        /* Replace name and try again */
            free(qrrp->name);
            qrrp->name = j2strdup(result_rrlp->rdata.name);
            free_rr(result_rrlp);
            result_rrlp = NULLRR;
            looping--;
        }
    }
    free_rr(qrrp);
    return result_rrlp;
}
  
/* main entry point for address -> domain name resolution.
 * Returns string, or NULLCHAR if no name found.
 */
char *
resolve_a(ip_address,shorten)
int32 ip_address;               /* search address */
int shorten;                    /* return only first part of name (flag)*/
{
    struct rr *save_rrlp, *rrlp;
    char *result = NULLCHAR, *p;
  
/* N7IPB - When we have a lot of unresolvable addresses such as subnet routes in
 *         the routing table, domain translation can take a long time.  The
 *         following allows us to skip the translation process for any address
 *         ending in .000 or .255.  These are assumed to be either subnets or
 *         broadcast addresses.  Can be turned on and off with the 'domain subnet'
 *         command.
 */
  
    if(((ip_address & 0x0ff) && ((ip_address & 0x0ff) ^ 0x0ff)) || Dsubnet_translate ) {
        for(rrlp = save_rrlp = inverse_a(ip_address);
            rrlp != NULLRR && result == NULLCHAR;
        rrlp = rrlp->next ){
            if(rrlp->rdlength > 0){
                switch(rrlp->type){
                    case TYPE_PTR:
                        result = j2strdup(rrlp->rdata.name);
                        break;
                    case TYPE_A:
                        result = j2strdup(rrlp->name);
                        break;
                }
            }
        }
        free_rr(save_rrlp);
  
        /* From Dennis Goodwin, kb7dz.
         * when domain verbose is off,
         * this make a domain name line bbs.wg7j.ampr.org show as bbs.wg7j
         * as opposed to bbs, as the above code does.
         */
        if(result != NULLCHAR && shorten) {
            if (Dsuffix != NULLCHAR)
            /* domain name minus domain suffix */
                p = strstr(result, Dsuffix);
            else
            /* domain name up to, and including the first period */
                p = strchr(result, '.') + 1;
            if(p != NULLCHAR)
                *p = '\0';
        }
        if(result != NULLCHAR && *result) {
        /* remove trailing . */
            for (p=result; *p; ++p)
                ;
            if (*(--p) == '.')
                *p = (char) 0;
        }
        /* end of mod */
    }
    return result;
}

#ifdef	IPV6
unsigned char *resolve6 (char *name)
{
	register struct rr *rrlp;

	static unsigned char ip_address[16];
 
	zeroipv6addr (ip_address);

	/* 16Apr2023, Maiko, Don't bother if IPV6 is not configured */ 
    if ((ipv6iface == (char*)0) || (name == NULLCHAR))
      return ip_address;

#ifdef	THIS_WAS_A_HACK
	/* 24Apr2023, Maiko, Damn it, forgot about IPV4 numeric notation */
	if (strstr (name, "44."))
      		return ip_address;
#endif
	/*
	 * 26Apr2023, Maiko, Use the isaddr() function, it's been around
	 * for eons, that's the proper way to do it. If it returns true,
	 * then this is a IPV4 numeric, leave now, and let the calling
	 * code decide to use the ipv4 resolve() instead.
	 */
	if (isaddr (name))
      		return ip_address;

 	/* If this appears to be a numerical address, return binary value */
	if (strchr (name, ':'))
	{
		struct j2sockaddr_in6 sock6;

		if (j2_ipv6_asc2ntwk (name, &sock6) != 1)
			return ip_address;

		copyipv6addr (sock6.sin6_addr.s6_addr, ip_address);

		return ip_address;
 	}
 
    if((rrlp = resolve_rr(name,TYPE_AAAA,1)) != NULLRR
        && rrlp->rdlength > 0)
        copyipv6addr (rrlp->rdata.addr6, ip_address);
  
    /* multi-homed hosts are handled here */
    if(rrlp != NULLRR && rrlp->next != NULLRR) {
        register struct rr *rrp;
        register struct route *rp;
        int16 cost = MAXINT16;
        rrp = rrlp;
        while(rrp != NULLRR) { /* choose the best of a set of routes */
            if(rrp->rdlength > 0 &&
                (rp = rt_lookup(rrp->rdata.addr)) != NULLROUTE &&
            rp->metric <= cost) {
                copyipv6addr (rrp->rdata.addr6, ip_address);
                cost = (int16)rp->metric;
            }
            rrp = rrp->next;
        }
    }
  
    free_rr(rrlp);
    return ip_address;
}
#endif
  
/* Main entry point for domain name -> address resolution.
 * Returns 0 if name is currently unresolvable.
 */
int32
resolve(name)
char *name;
{
    register struct rr *rrlp;
    int32 ip_address = 0;
  
    if(name == NULLCHAR)
        return 0;
  
    if(isaddr(name))
        return aton(name);
  
    if((rrlp = resolve_rr(name,TYPE_A,1)) != NULLRR
        && rrlp->rdlength > 0)
        ip_address = rrlp->rdata.addr;
  
    /* multi-homed hosts are handled here */
    if(rrlp != NULLRR && rrlp->next != NULLRR) {
        register struct rr *rrp;
        register struct route *rp;
        int16 cost = MAXINT16;
        rrp = rrlp;
        while(rrp != NULLRR) { /* choose the best of a set of routes */
            if(rrp->rdlength > 0 &&
                (rp = rt_lookup(rrp->rdata.addr)) != NULLROUTE &&
            rp->metric <= cost) {
                ip_address = rrp->rdata.addr;
                cost = (int16)rp->metric;
            }
            rrp = rrp->next;
        }
    }
  
    free_rr(rrlp);
    return ip_address;
}
  
  
/* Lookup alternative MX records. Upto 5 of them. -- Selcuk */
int
resolve_amx(char *name,int32 not_this_one,int32 Altmx[])
{
    register struct rr *rrp, *arrp;
    char *sname;
    int32 addr;
    int16 exists = 0, i, n, tmp[5];

    if(name == NULLCHAR)
        return exists;

    if(isaddr(name)){
        if((sname = resolve_a(aton(name),FALSE)) == NULLCHAR)
            return exists;
    }
    else
        sname = j2strdup(name);

    for(i=0;i<5;i++) { /* let's initialize */
        tmp[i] = MAXINT16;
        Altmx[i] = 0;
    }

    i = 0;
    rrp = arrp = resolve_rr(sname,TYPE_MX,1);
    /* Search this list of rr's for an MX record */
    while(rrp != NULLRR) {
        if(rrp->rdlength > 0 && (addr = resolve(rrp->rdata.mx.exch)) != 0
	&& addr != not_this_one){
            for(n = i; ;n--) {
	        if(n > 0 && rrp->rdata.mx.pref < tmp[n-1]) {
                    tmp[n] = tmp[n-1];
                    Altmx[n] = Altmx[n-1];
                } else {
                    if(rrp->rdata.mx.pref < tmp[n]) {
                        tmp[n] = rrp->rdata.mx.pref;
                        Altmx[n] = addr;
                        exists++;
                    }
                    break;
                }
            }
            if(i < 4) i++;
        }
        rrp = rrp->next;
    }
    free_rr(arrp);

    free(sname);
    return exists;
}
  
  
/* Main entry point for MX record lookup.
 * Returns 0 if name is currently unresolvable.
 */
int32
#ifdef SMTP_A_BEFORE_WILDMX
resolve_mx(name, A_rec_addr)
int32 A_rec_addr;
#else
resolve_mx(name)
#endif
char *name;
{
    register struct rr *rrp, *arrp;
    char *sname, *tmp, *cp;
    int32 addr, ip_address = 0;
    int16 pref = MAXINT16;
  
    if(name == NULLCHAR)
        return 0;
  
    if(isaddr(name)){
        if((sname = resolve_a(aton(name),FALSE)) == NULLCHAR)
            return 0;
    }
    else
        sname = j2strdup(name);
  
    cp = sname;
    while(1){
        rrp = arrp = resolve_rr(sname,TYPE_MX,1);
        /* Search this list of rr's for an MX record */
        while(rrp != NULLRR){
            if(rrp->rdlength > 0 && rrp->rdata.mx.pref <= pref &&
            (addr = resolve(rrp->rdata.mx.exch)) != 0){
                pref = rrp->rdata.mx.pref;
                ip_address = addr;
            }
            rrp = rrp->next;
        }
        free_rr(arrp);
        if(ip_address != 0)
            break;
#ifdef SMTP_A_BEFORE_WILDMX
        /* Compose wild card one level up UNLESS we know an A record exists */
        if(A_rec_addr || (cp = strchr(cp,'.')) == NULLCHAR)
#else
        /* Compose wild card one level up */
        if((cp = strchr(cp,'.')) == NULLCHAR)
#endif
            break;
        tmp = mallocw(strlen(cp)+2);
        sprintf(tmp,"*%s",cp);          /* wildcard expansion */
        free(sname);
        sname = tmp;
        cp = sname + 2;
    }
    free(sname);
    return ip_address;
}
  
/* Search for local records of the MB, MG and MR type. Returns list of
 * matching records.
 */
struct rr *
resolve_mailb(name)
char *name;             /* local username, without trailing dot */
{
    register struct rr *result_rrlp;
    struct rr *rrlp;
    char *sname;
  
    sname = j2strdup(name);

    rrlp = make_rr(RR_QUERY,sname,CLASS_IN,TYPE_MB,0,0,NULL);
    rrlp->next = make_rr(RR_QUERY,sname,CLASS_IN,TYPE_MG,0,0,NULL);
    rrlp->next->next = make_rr(RR_QUERY,sname,CLASS_IN,TYPE_MR,0,0,NULL);
    free(sname);
    if((result_rrlp = dcache_search(rrlp)) == NULLRR){
        result_rrlp = dfile_search(rrlp);
    }
    free_rr(rrlp);
    if(Dsuffix != NULLCHAR){
        rrlp = result_rrlp;
        while(rrlp != NULLRR){  /* add domain suffix to data */
            if(rrlp->rdlength > 0 &&
            rrlp->rdata.name[rrlp->rdlength-1] != '.'){
                sname = mallocw(rrlp->rdlength +
                Dsuffixl+2);
                sprintf(sname,"%s.%s",rrlp->rdata.name,Dsuffix);
                free(rrlp->rdata.name);
                rrlp->rdata.name = sname;
                rrlp->rdlength = strlen(sname);
            }
            rrlp = rrlp->next;
        }
    }
    dcache_add(copy_rr_list(result_rrlp));
    return result_rrlp;
}
  
/* Return "normalized" domain name, with default suffix and trailing '.'
 * Searches local cache for CNAME expansions.
 */
char *
domainsuffix(dname)
char *dname;
{
    char *sname, *tname, *pp;
    int l;
  
    if ( dname == NULLCHAR )
        return NULLCHAR;
  
    if(isaddr(dname)) {
        /* convert to our canonic form */
        return j2strdup( inet_ntoa( aton(dname) ) );
    }
  
    sname = j2strdup(dname);
    l = strlen(sname);
    if((pp = strrchr(sname,'.')) == NULLCHAR){
        /* No dot in name. Try to add default suffix */
        if(Dsuffix != NULLCHAR){
            /* Append default suffix */
            tname = mallocw(l+Dsuffixl+2);
            sprintf(tname,"%s.%s",sname,Dsuffix);
            free(sname);
            sname = tname;
        }
    } else {
        /* There is a dot in the name. Check last part of
         * name. If longer than 4 char it must be a name
         * 4 or less is probably a domain (org, army, uk)
         */
        if(Dsuffix != NULLCHAR)
		{
#ifdef	TRADITIONAL_TLD_LEN
            if(strlen(pp) <= 5)
#else
		/*
		 * 28Nov2015, Maiko (VE4KLM), not sure how to deal with this issue
		 * where TLD lengths are no longer in the era of just .gov, or .info,
		 * or whatever. The forums suggest this is complicated to deal with,
		 * even some modern 'apps' are not 'doing it right', so this is my
		 * solution in the interim. Set TLDlen to -1 to completely disable,
		 * the default value is 5 (meaning 4 I think), or set to your needs.
		 */
            if ((Dns_TLDlen == -1) || (strlen(pp) <= Dns_TLDlen))
#endif
			{
                for(++pp;*pp;pp++){
                    if(isdigit(*pp))
                        break;
                }
                if(*pp){
            /* Append default suffix */
                    tname = mallocw(l+Dsuffixl+2);
                    sprintf(tname,"%s.%s",sname,Dsuffix);
                    free(sname);
                    sname = tname;
                }
            } else {
            /* name with dot (must be call + local domain) */
                tname = mallocw(l+Dsuffixl+2);
                sprintf(tname,"%s.%s",sname,Dsuffix);
                free(sname);
                sname = tname;
            }
        }
    }
  
    if(sname[strlen(sname)-1] != '.'){
        /* Append trailing dot */
        tname = mallocw(strlen(sname)+2);
        sprintf(tname,"%s.",sname);
        free(sname);
        sname = tname;
    }
  
    return sname;
}
  
#ifdef DOMAINSERVER
  
/* Domain Name Server - based on the server in GRI-Nos 910828
 * ported to current NOS code by Johan. K. Reinalda, WG7J/PA3DIS
 *
 * - Does not answer more then one query per frame
 * - Gives non-authoritative answers to all queries.
 * - Does not reply with authority or additional RR's
 * - If no answers are found in local cache or domain.txt file,
 *   remote servers, if configured, are queried.
 *
 *
 * v0.93  10/19/92   queries do not recurse anymore, but let the requester
 *                   do the recursion (Solves problem with CNAME queries)
 * v0.92  06/24/92   RR length bug fixed.
 *                   A,CNAME,MX,HINFO,PTR,NS,SOA queries now work
 * v0.91  06/22/92   MX has small bug with RR length indication
 * v0.90  06/20/92   Only supports a single type 'A' request per frame
 */
  
int Dsocket = -1;
  
/* Process a query received by the DNS server */
static void
proc_query(unused,d,b)
int unused;
void *d;
void *b;
{
    struct dserver *dp;
    struct dhdr *dhdr;
    int i,len;
    char *buf;
    struct sockaddr_in server;
    struct rr *rrp, *rrans, *rrns, *rradd, *rrtmp;
    struct rr *qp;
  
    dp = (struct dserver *) d;      /* The query address */
    dhdr = (struct dhdr *) b;       /* The query in host format */
  
    rrans = rrns = rradd = NULLRR;
    qp = dhdr->questions;
  
        dhdr->aa = 0;
        switch(qp->type) {
            case TYPE_ANY:
	        /* We aren't authoritative, so we flush the cache to force external query */
                rrp=Dcache;  /* we could just call dodomaindump(0,NULLARGP,NULL) */
                Dcache=NULLRR;   /* But this is lees code (maybe) */
                free_rr(rrp);
                /* fall through into common handler code */
            case TYPE_A:
#ifdef IPV6
			/* 13Apr2023, Maiko */
			case TYPE_AAAA:
#endif
            case TYPE_MX:
            case TYPE_CNAME:
            case TYPE_HINFO:
            case TYPE_PTR:
            case TYPE_NS:
            case TYPE_SOA:
        /* Let the other side resolve CNAME references, do not recurse ! */
                if((rrp = resolve_rr(qp->name,qp->type,0)) != NULLRR && rrp->rdlength>0) {
        /* we found an entry, go tell him */
                    dhdr->rcode = NO_ERROR;
                    dhdr->qr = RESPONSE;
                } else {
        /* we did not find an entry, go tell him */
                    free_rr(rrp);  /* in case rdlength==0 */
                    rrp=NULLRR;
                    dhdr->rcode = NAME_ERROR;
                    dhdr->qr = RESPONSE;
                }
                rrans = rrp;
                break;
    /* Search only the local cache and domain file for these next few */
    /* Is this a good idea ??? */
            case TYPE_MB:
            case TYPE_MG:
            case TYPE_MR:
                rrp = make_rr(RR_QUERY,qp->name,CLASS_IN,qp->type,0,0,NULL);
                if((rrans = dcache_search(rrp)) == NULLRR){
                    rrans = dfile_search(rrp);
                }
                free_rr(rrp);
                break;
            default:
                dhdr->rcode = NOT_IMPL;
                dhdr->qr = RESPONSE;
        }
  
    /* Find the number of answer records */
    i = 0;
    rrtmp = rrans;
    while(rrtmp != NULLRR) {
        i++;
        /* KG7CP -  - if no ttl in database, set the time-to-live value for
         * dns responses, unless no ttl value has been defined.
         */
        if(rrtmp->ttl == TTL_MISSING && Dns_ttl > 0)
            rrtmp->ttl = Dns_ttl;
        rrtmp = rrtmp->next;
    }
    dhdr->ancount = i;
    dhdr->answers = rrans;
  
    /* Authority and Additional RR's not implemented yet. */
    dhdr->nscount = 0;
    dhdr->authority = NULLRR;
    dhdr->arcount = 0;
    dhdr->additional = NULLRR;
    dhdr->ra = 0;  /* recursion NOT available */
  
    if(Dtrace) {
        puts("DNS: replying");
        dumpdomain(dhdr,0);
    }
  
    /* Maximum reply-pkt size is 512 (see rfc1034/1035), enforced in htondomain */
    buf = mallocw(DNSBUFLEN+256);  /* add some extra room */
    len = htondomain(dhdr,buf,DNSBUFLEN+256);
    free_dhdr(dhdr);
  
    server.sin_family = AF_INET;
    server.sin_port = dp->port;
    server.sin_addr.s_addr = dp->address;
    j2sendto(Dsocket,buf,len,0,(char *)&server,sizeof(server));

    free(buf);
    free((char *)dp);
    dns_process_count--;
}
  
  
/* Process to receive all domain server related messages */
static void
drx(unused,u,p)
int unused;
void *u;
void *p;
{
    struct sockaddr_in sock,from;
    int fromlen;
    struct mbuf *bp;
    struct dhdr *dhdr;
    struct dserver *dp;
    int foo;
  
    Dsocket = j2socket(AF_INET,SOCK_DGRAM,0);
  	/* 23Mar2021, Maiko, we should be checking return value */
    if (Dsocket == -1)
    {
        log (-1, "dns no socket");
        return;
    }
    sock.sin_family = AF_INET;
    sock.sin_addr.s_addr = INADDR_ANY;
    sock.sin_port = IPPORT_DOMAIN;
    if(j2bind(Dsocket,(char *)&sock,sizeof(sock)) == -1) {
	/* 23Mar2021, Maiko, Put this in the logfile instead and close the socket */
        log (-1, "dns can't bind");
	close_s (Dsocket);
        Dsocket = -1;
        return;
    }

    /* Now loop forever, processing queries */
    for(;;){
        fromlen = sizeof(from);
        if((foo = recv_mbuf(Dsocket,&bp,0,(char *)&from,&fromlen)) == -1)
            break;  /* Server closing */
        if(foo == 0)
            continue;
        dhdr = mallocw(sizeof(struct dhdr));

	/*
         * 19Mar2021, Maiko (VE4KLM), new socket parameter, and seriously ! Why
         * are people not checking the return value of this function, especially
	 * if it is already doing error checks, plus my new security fixes ...
	 *
	 * 23Mar2021, Maiko, Added ptype parameter, 1 means dns request
	 *  (see comments in other ntohdomain() call, we have to use the
	 *    nos_log_peerless () call, so now passing socket structure,
	 *     and not the socket number)
         */
        if (ntohdomain(&from, 1, dhdr,&bp) == -1)
	{
                free_dhdr(dhdr);
		continue;
	}

        if(Dtrace) {
            PRINTF("DNS: %u bytes from %s\n",foo,
            psocket((struct sockaddr *)&from));
            dumpdomain(dhdr,0);
        }
        if (dns_process_count >= dns_maxcli) {  /* G8FSL */
            if (Dtrace)
                j2tputs("DNS: ignored - too many processes\n");
            free_dhdr(dhdr);
            continue;
        }
        dns_process_count++;
    /* Process queries only */
        if(dhdr->qr == QUERY) {
        /* Queries from ourself will cause a loop ! */
            if(ismyaddr(from.sin_addr.s_addr) != NULLIF) {
                if(Dtrace)
                    j2tputs("DNS: question from myself ignored\n");
                free_dhdr(dhdr);
                dns_process_count--;
                continue;
            } else {
                dp=(struct dserver *)callocw(1,sizeof(struct dserver));
                dp->address = from.sin_addr.s_addr;
                dp->srtt = (Dserver_maxwait * 1000) / MSPTICK;
                dp->timeout = dp->srtt * 2;
                dp->port = from.sin_port;
                if(dhdr->opcode == ZONEINIT) {
            /* ZONEINIT not implemented */
                    free_dhdr(dhdr);
                    free(dp);
                } else
                    newproc("Domain server",2048,proc_query
                    ,0,(void *)dp,(void *)dhdr,0);
            }
        }
    }

    log (-1, "DNS : server closed");	/* 23Mar2021, Maiko */
}
  
static int
dodnsserver(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc == 1) {
        j2tputs((Dsocket != -1) ? "on\n" : "off\n");
    } else {
        if(!stricmp(argv[1],"on")) {
            if(Dsocket == -1)
        /* Start domain server task */
                newproc("Domain listener",2048,drx,0,NULL,NULL,0);
        } else {
            close_s(Dsocket);
            Dsocket = -1;
        }
    }
    return 0;
}
  
/* Free a domain message */
static void
free_dhdr(dp)
struct dhdr *dp;
{
    if(dp->qdcount != 0)
        free_rr(dp->questions);
    if(dp->ancount != 0)
        free_rr(dp->answers);
    if(dp->nscount != 0)
        free_rr(dp->authority);
    if(dp->arcount != 0)
        free_rr(dp->additional);
    free((char *)dp);
}
  
static int
dodnsmaxcli(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint( &dns_maxcli, "max. simultaneous DNS processes", argc,argv );
}
    
#endif /* DOMAINSERVER */
  
#ifdef MORESESSION
int
dodomlook(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char **margv;
    margv = (char **)callocw(3,sizeof(char *));
    margv[1] = j2strdup(Dfile);
    margv[2] = j2strdup(argv[1]);
    domore(3,margv,p);
    free(margv[1]);
    free(margv[2]);
    free(margv);
    return 0;
}
#endif  /* MORESESSION */

#ifdef DQUERYSESSION
#include "session.h"
extern int StatusLines, Numrows;

static int
dodnsquery(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct rr *rrp;
    struct rr *result_rrlp;
    char *sname;
    struct session *sp;
    int row = Numrows - 1 - StatusLines;
    int usesession=0;

    if(Curproc->input == Command->input) {
        usesession=1;
        if((sp = newsession(argv[1],DQUERY,0)) == NULLSESSION)
            return -1;
        /* Put tty into raw mode so single-char responses will work */
        sp->ttystate.echo = sp->ttystate.edit = 0;
    }

    if ( isaddr( argv[1] ) ) {
        result_rrlp = inverse_a( aton( argv[1] ) );
    } else {
        sname = domainsuffix( argv[1] );
        rrp = make_rr(RR_QUERY,sname,CLASS_IN,TYPE_ANY,0,0,NULL);
        free(sname);

#ifdef QUERYPROC
	if (newproc("dns_query",512,dns_query,0,(void *)rrp,
                    (void *)&result_rrlp,0) != NULLPROC)
            pwait(rrp);
        else {
            j2tputs("Can't fork dns_query proc\n");
            result_rrlp = NULLRR;
        }
#else
        dns_query(0,(void *)rrp,(void *)&result_rrlp);
#endif

        free_rr(rrp);
    }

    for( rrp=result_rrlp; rrp!=NULLRR; rrp=rrp->next)
    {
        put_rr(stdout,rrp);
        if(usesession && --row == 0){
            row = keywait(TelnetMorePrompt,0);
            switch(row){
            case -1:
            case 'q':
            case 'Q':
                rrp = NULLRR;
                break;
            case '\n':
            case '\r':
                row = 1;
                break;
            case ' ':
            default:
                row = Numrows - 1 - StatusLines;
            };
        }
    }
#ifndef UNIX
    fflush(stdout);
#endif
    free_rr(result_rrlp);
    if(usesession) {
        keywait(NULLCHAR,1);
        freesession(sp);
    }
    return 0;
}
#endif /* DQUERYSESSION */
