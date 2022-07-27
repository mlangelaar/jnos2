/*
 * JNOS 2.0
 *
 * $Id: files.c,v 1.5 2012/04/26 13:43:28 ve4klm Exp $
 *
 * System-dependent definitions of various files, spool directories, etc
 * Mods by PA0GRI
 * Modified to allow for configurable file names/locations - WG7J
 * Mods by VE4KLM
 */
#include "global.h"
#include "netuser.h"
#include "files.h"
#include "mailbox.h"

#ifdef	UNIX
#include <unistd.h>
#endif

#ifdef MD5AUTHENTICATE
#include "md5.h"
#ifndef MD5MINMATCH
#define MD5MINMATCH 16  /* # hex digits that must match; an EVEN # <= 32 */
#endif
#define MD5MINMATCHB (MD5MINMATCH/2)  /* # bytes that must match */
#endif
  
#ifdef  MSDOS
char System[] = "MSDOS";
#ifdef	APRSD
char *APRSdir   = "/aprs";		/* APRS root directory 27May2005 */
#endif

char *Usagedir = "/usage";		/* 29May2020, Maiko (VE4KLM), usage files */

char *Startup   = "/autoexec.nos";      /* Initialization file */
char *Userfile  = "/ftpusers";          /* Authorized FTP users and passwords */
char *Hostfile  = "/net.rc";            /* hosts and passwords */
char *Spoolqdir = "/spool";             /* Spool directory */
char *Maillog   = "/spool/mail.log";    /* mail log */
char *Mailspool = "/spool/mail";        /* Incoming mail */
char *Mailqdir  = "/spool/mqueue";      /* Outgoing mail spool */
char *LogsDir   = "/logs";              /* Logs directory */
char *Mailqueue = "/spool/mqueue/*.wrk";/* Outgoing mail work files */
char *Routeqdir = "/spool/rqueue";      /* queue for router */
char *Alias = "/alias";             /* the alias file */
char *Dfile = "/domain.txt";        /* Domain cache */
char *Fdir  = "/finger";            /* Finger info directory */
char *Fdbase = "/finger/dbase.dat"; /* Finger database */
char *Pdbase = "/spool/names.dat"; /* Personal names databases (IW5DHE) */
char *Arealist  = "/spool/areas";       /* List of message areas */
char *Rewritefile = "/spool/rewrite";       /* TO-Address rewrite file */
#ifdef TRANSLATEFROM
char *Translatefile = "/spool/translat";    /* FROM-address rewrite file */
#endif
#ifdef SMTP_REFILE
char *Refilefile = "/spool/refile";         /* FROM|TO-address rewrite file */
#endif
#ifdef HOLD_LOCAL_MSGS
char *Holdlist = "/spool/holdlist";         /* List of areas in which locally-originated msgs are marked Held */
#endif
char *Ftpmotd = "/spool/ftpmotd.txt";       /* FTP message of the day */
char *CmdsHelpdir  = "/help";       /* Console/Sysop commands help file directory */
#if defined(MAILBOX) || defined(CONVERS)
char *Helpdir   = "/spool/help";        /* Mailbox help file directory */
#endif
#ifdef HTTP
char *Httpdir      = "/wwwroot";        /* ABSOLUTE path to Root dir for finding www documents */
char *HttpStatsDir = "/wwwstats";       /* Directory for page counters */
#ifdef HTTP_EXTLOG
char *HLogsDir  = "/wwwlogs";           /* Http log directory */
#endif
#endif /* HTTP */
#ifdef MAILBOX
char *Signature = "/spool/signatur";        /* Mail signature file directory */
char *Historyfile = "/spool/history";       /* Message ID history file */
char *Motdfile = "/spool/motd.txt";     /* Mailbox message of the day */
#ifdef USERLOG
char *UDefaults = "/spool/users.dat";       /* User preference file */
char *UDefbak = "/spool/users.bak";
char *Mregfile = "/spool/mreg.txt";     /* Mailbox registration message */
#endif
#ifdef WPAGES
char *WhitePages = "/spool/wp/wpages";      /* WP user log file */
char *WhitePagesBBS = "/spool/wp/wpagebbs"; /* WP bbs log file */
char *WPUpdateFile  = "/spool/wp/update.wp"; /* WP update temp file */
#endif
#endif /* MAILBOX */

#ifdef CONVERS
char *Cinfo = "/finger/dbase.dat";  /* Convers user info file */
#ifdef CNV_CHG_PERSONAL
char *Cinfobak = "/finger/dbase.bak"; /* Convers user info backup */
#endif
#ifdef CNV_CHAN_NAMES
char *Channelfile = "/spool/channel.dat";  /* Channel names */
#endif
char *ConvMotd = "/spool/convmotd.txt"; /* Convers Message of the Day */
#endif
#if (defined(POP2CLIENT) || defined(POP2SERVER) || defined(POP3CLIENT) || defined(POP3SERVER))
char *Popusers  = "/popusers";          /* POP user and password file */
#endif
#if (defined(NNTP) || defined(NNTPS))
char *Newsdir   = "/spool/news";        /* News messages and NNTP data */
char *History   = "/spool/news/history";    /* NNTP article msgids and timestamps */
#endif
#if (defined(RLINE) || defined(MBFWD))
char *Forwardfile = "/spool/forward.bbs";   /* Mail forwarding file */
#endif
#ifdef NETROM
char *Netromfile = "/netrom.sav";       /* Netrom node save file */
#endif
char *Onexit = "/onexit.nos";           /* Commands to be executed on exiting */
#ifdef EXPIRY
char *Expirefile = "/spool/expire.dat"; /* Message expiration control file */
#endif
#ifdef  NNTPS
char *Naccess   = "/spool/news/access";
char *Active    = "/spool/news/active";     /* */
char *Pointer   = "/spool/news/pointer";    /* */
char *NInfo = "/spool/news/info";       /* */
char *Nhelp = "/spool/news/help";       /* */
char *Forward   = "/spool/news/forward";    /* */
char *Poll  = "/spool/news/poll";       /* */
char *Newstomail = "/spool/news/gateway";
#endif
char Eol[]  = "\r\n";
#define SEPARATOR   "/"
#define ROOTDIR "/"
#endif /* MSDOS */
  
#ifdef  UNIX
/*
 * DOS compatible names now used.  If you used the old names, use compat.cfg
 * as a configuration file to get things working, then rename/symlink as needed
 * --- the new names are the same as the old DOS ones, so it should be easy.
 *
 * We now use this more as a resource file than as merely a list of filenames
 * (we can get away with it, where this costs precious memory in DOS).  Some
 * of the things found here are the default session managers for the Command
 * and Trace sessions and color information (when implemented).
 */
char System[]   = "UNIX";
#ifdef	APRSD
char *APRSdir   = "./aprs";		/* APRS root directory 27May2005 */
#endif

char *Usagedir = "./usage";		/* 29May2020, Maiko (VE4KLM), usage files */

char *Startup	= "./autoexec.nos";		/* Initialization file */
char *Userfile  = "./ftpusers";
char *Hostfile  = "./net.rc";           /* hosts and passwords */
char *Spoolqdir = "./spool";             /* Spool directory */
char *Maillog   = "./spool/mail.log";         /* mail log */
char *Mailspool = "./spool/mail";
char *Mailqdir  = "./spool/mqueue";
char *LogsDir   = "./logs";              /* Logs directory */
char *Mailqueue = "./spool/mqueue/*.wrk";
char *Routeqdir = "./spool/rqueue";           /* queue for router */
char *Alias     = "./alias";            /* the alias file */
char *Dfile     = "./domain.txt";       /* Domain cache */
char *Fdir      = "./finger";           /* Finger info directory */
char *Fdbase    = "./finger/dbase.dat"; /* Finger database */
char *Pdbase    = "./spool/names.dat"; /* Personal names databases (IW5DHE) */
char *Arealist  = "./spool/areas";            /* List of message areas */
char *Rewritefile = "./spool/rewrite";        /* TO-Address rewrite file */
#ifdef TRANSLATEFROM
char *Translatefile = "./spool/translat";    /* FROM-address rewrite file */
#endif
#ifdef SMTP_REFILE
char *Refilefile = "./spool/refile";         /* FROM|TO-address rewrite file */
#endif
#ifdef HOLD_LOCAL_MSGS
char *Holdlist = "./spool/holdlist";         /* List of areas in which locally-originated msgs are marked Held */
#endif
char *Ftpmotd   = "./spool/ftpmotd.txt";       /* FTP message of the day */
char *CmdsHelpdir  = "./help";           /* Console/Sysop commands help file directory */
#if defined(MAILBOX) || defined(CONVERS)
char *Helpdir   = "./spool/help";         /* Mailbox help file directory */
#endif
#ifdef HTTP
char *Httpdir      = "./wwwroot";       /* Root dir for finding www documents */
char *HttpStatsDir = "./wwwstats";      /* Directory for page counters */
#ifdef HTTP_EXTLOG
char *HLogsDir  = "./wwwlogs";          /* Http log directory */
#endif
#endif /* HTTP */
#ifdef MAILBOX
char *Signature = "./spool/signatur";         /* Mail signature file directory */
char *Historyfile = "./spool/history";        /* Message ID history file */
char *Motdfile  = "./spool/motd.txt";     /* Mailbox message of the day */
#ifdef USERLOG
char *UDefaults = "./spool/users.dat";       /* User preference file */
char *UDefbak = "./spool/users.bak";
char *Mregfile = "./spool/mreg.txt";     /* Mailbox registration message */
#endif
#endif /* MAILBOX */

#ifdef WPAGES
char *WhitePages = "./spool/wp/wpages";      /* WP user log file */
char *WhitePagesBBS = "./spool/wp/wpagebbs"; /* WP bbs log file */
char *WPUpdateFile  = "./spool/wp/update.wp"; /* WP update temp file */
#endif

#ifdef CONVERS
char *Cinfo = "./finger/dbase.dat";  /* Convers user info file */
#ifdef CNV_CHG_PERSONAL
char *Cinfobak = "./finger/dbase.bak"; /* Convers user info backup */
#endif
#ifdef CNV_CHAN_NAMES
char *Channelfile = "./spool/channel.dat";  /* Channel names */
#endif
char *ConvMotd = "./spool/convmotd.txt";
#endif
#if (defined(POP2CLIENT) || defined(POP2SERVER) || defined(POP3CLIENT) || defined(POP3SERVER))
char *Popusers  = "./popusers";         /* POP user and password file */
#endif
#if (defined(NNTP) || defined(NNTPS))
char *Newsdir   = "./spool/news";         /* News messages and NNTP data */
char *History   = "./spool/news/history";    /* NNTP article msgids and timestamps */
#endif
#if (defined(RLINE) || defined(MBFWD))
char *Forwardfile = "./spool/forward.bbs";        /* Mail forwarding file */
#endif
#ifdef NETROM
char *Netromfile = "./netrom.sav";      /* Netrom node save file */
#endif
char *Onexit = "./onexit.nos";           /* Commands to be executed on exiting */
#ifdef EXPIRY
char *Expirefile = "./spool/expire.dat"; /* Message expiration control file */
#endif
#ifdef  NNTPS
char *Naccess   = "./spool/news/access";
char *Active    = "./spool/news/active";     /* */
char *Pointer   = "./spool/news/pointer";    /* */
char *NInfo = "./spool/news/info";       /* */
char *Nhelp = "./spool/news/help";       /* */
char *Forward   = "./spool/news/forward";    /* */
char *Poll  = "./spool/news/poll";       /* */
char *Newstomail = "./spool/news/gateway";
#endif /* NNTPS */
char Eol[]  = "\n";
#define SEPARATOR   "/"
#define ROOTDIR "."
/* default session managers */
char *Command_sessmgr = "";
char *Trace_sessmgr = "";
#endif /* UNIX */
  
#ifdef  AMIGA
char System[] = "AMIGA";
#ifdef	APRSD
char *APRSdir   = "TCPIP:aprs";		/* APRS root directory 27May2005 */
#endif

char *Usagedir = "TCPIP:usage";		/* 29May2020, Maiko (VE4KLM), usage files */

char *Startup   = "TCPIP:nos-startup";
char *Config    = "TCPIP:config.nos";       /* Device configuration list */
char *Hostfile  = "TCPIP:net.rc";       /* hosts and passwords */
char *Userfile  = "TCPIP:ftpusers";
char *Mailspool = "TCPIP:spool/mail";
char *Maillog   = "TCPIP:spool/mail.log";
char *Mailqdir  = "TCPIP:spool/mqueue";
char *Mailqueue = "TCPIP:spool/mqueue/#?.wrk";
char *Routeqdir = "TCPIP:spool/rqueue";     /* queue for router */
char *Alias = "TCPIP:alias";        /* the alias file */
char *Dfile = "TCPIP:domain.txt";       /* Domain cache */
char *Fdir  = "TCPIP:finger";       /* Finger info directory */
char *Fdbase = "TCPIP:finger/dbase.dat"; /* Finger database */
char *CmdsHelpdir  = "TCPIP:help";       /* Console/Sysop commands help file directory */
char *Arealist  = "TCPIP:spool/areas";      /* List of message areas */
char *Helpdir   = "TCPIP:spool/help";       /* Mailbox help file directory */
char *Rewritefile = "TCPIP:spool/rewrite";  /* TO-Address rewrite file */
#ifdef TRANSLATEFROM
char *Translatefile = "TCPIP:spool/translat"; /* FROM-address rewrite file */
#endif
#ifdef SMTP_REFILE
char *Refilefile = "TCPIP:spool/refile";      /* FROM|TO-address rewrite file */
#endif
#ifdef HOLD_LOCAL_MSGS
char *Holdlist = "TCPIP:/spool/holdlist";   /* List of areas in which locally-originated msgs are marked Held */
#endif
#ifdef HTTP
char *Httpdir      = "TCPIP:/wwwroot";        /* Root dir for finding www documents */
char *HttpStatsDir = "TCPIP:/wwwstats";       /* Directory for page counters */
#ifdef HTTP_EXTLOG
char *HLogsDir  = "TCPIP:/wwwlogs";           /* Http log directory */
#endif
#endif /* HTTP */
char *Signature = "TCPIP:spool/signatur";   /* Mail signature file directory */
char *Popusers  = "TCPIP:/popusers";        /* POP user and password file */
char *Newsdir   = "TCPIP:spool/news";       /* News messages and NNTP data */
char *Forwardfile = "TCPIP:spool/forward.bbs";  /* Mail forwarding file */
char *Historyfile = "TCPIP:spool/history";  /* Message ID history file */
char *UDefaults = "TCPIP:spool/users.dat";  /* User preference file */
char *Netromfile = "TCPIP:netrom.sav";      /* Netrom node save file */
char *Onexit = "TCPIP:onexit.nos";           /* Commands to be executed on exiting */
char *Expirefile = "TCPIP:spool/expire.dat"; /* Message expiration control file */
#if (defined(NNTP) || defined(NNTPS))
char *History   = "TCPIP:spool/news/history";    /* NNTP article msgids and timestamps */
#endif
#ifdef  NNTPS
char *Active    = "TCPIP:spool/news/active";     /* */
char *Pointer   = "TCPIP:spool/news/pointer";    /* */
char *NInfo = "TCPIP:spool/news/info";       /* */
char *Nhelp = "TCPIP:spool/news/help";       /* */
char *Forward   = "TCPIP:spool/news/forward";    /* */
char *Poll  = "TCPIP:spool/news/poll";       /* */
char *Newstomail = "TCPIP:spool/news/gateway";
#endif
char Eol[]  = "\r\n";
#define SEPARATOR   "/"
#endif
  
#ifdef  MAC
char System[] = "MACOS";
#ifdef	APRSD
char *APRSdir   = "Mikes Hard Disk:aprs"; /* APRS root directory 27May2005 */
#endif

char *Usagedir = "Mikes Hard Disk:usage";	/* 29May2020, Maiko (VE4KLM), usage files */

char *Startup   = "Mikes Hard Disk:nos.start";
char *Config    = "Mikes Hard Disk:config.nos"; /* Device configuration list */
char *Hostfile  = "Mikes Hard Disk:net.rc"; /* hosts and passwords */
char *Userfile  = "Mikes Hard Disk:ftpusers";
char *Mailspool = "Mikes Hard Disk:spool:mail:";
char *Maillog   = "Mikes Hard Disk:spool:mail.log:";
char *Mailqdir  = "Mikes Hard Disk:spool:mqueue:";
char *Mailqueue = "Mikes Hard Disk:spool:mqueue:*.wrk";
char *Routeqdir = "Mikes Hard Disk:spool/rqueue:";  /* queue for router */
char *Alias = "Mikes Hard Disk:alias";      /* the alias file */
char *Dfile = "Mikes Hard Disk:domain:txt";     /* Domain cache */
char *Fdir  = "Mikes Hard Disk:finger";     /* Finger info directory */
char *Fdbase = "Mikes Hard Disk:finger/dbase.dat"; /* Finger database */
char *CmdsHelpdir  = "Mikes Hard Disk:help";       /* Console/Sysop commands help file directory */
char *Arealist  = "Mikes Hard Disk:spool/areas";    /* List of message areas */
char *Helpdir   = "Mikes Hard Disk:spool/help";     /* Mailbox help file directory */
char *Rewritefile = "Mikes Hard Disk:spool/rewrite";    /* TO-Address rewrite file */
#ifdef TRANSLATEFROM
char *Translatefile = "Mikes Hard Disk:spool/translat"; /* FROM-address rewrite file */
#endif
#ifdef SMTP_REFILE
char *Refilefile = "Mikes Hard Disk:spool/refile";     /* FROM|TO-address rewrite file */
#endif
#ifdef HOLD_LOCAL_MSGS
char *Holdlist = "Mikes Hard Disk:spool/holdlist";   /* List of areas in which locally-originated msgs are marked Held */
#endif
#ifdef HTTP
char *Httpdir      = "Mikes Hard Disk:/wwwroot";  /* Root dir for finding www documents */
char *HttpStatsDir = "Mikes Hard Disk:/wwwstats"; /* Directory for page counters */
#ifdef HTTP_EXTLOG
char *HLogsDir  = "Mikes Hard Disk:/wwwlogs";     /* Http log directory */
#endif
#endif /* HTTP */
char *Signature = "Mikes Hard Disk:spool/signatur"; /* Mail signature file directory */
char *Popusers  = "Mikes Hard Disk:/popusers";      /* POP user and password file */
char *Newsdir   = "Mikes Hard Disk:spool/news"; /* News messages and NNTP data */
char *Forwardfile = "Mikes Hard Disk:spool/forward.bbs"; /* Mail forwarding file */
char *Historyfile = "Mikes Hard Disk:spool/history";    /* Message ID history file */
char *UDefaults = "Mikes Hard Disk:spool/users.dat";  /* User preference file */
char *Netromfile = "Mikes Hard Disk:netrom.sav";    /* Netrom node save file */
char *Onexit = "Mikes Hard Disk:onexit.nos";           /* Commands to be executed on exiting */
char *Expirefile = "Mikes Hard Disk:spool/expire.dat"; /* Message expiration control file */
#if (defined(NNTP) || defined(NNTPS))
char *History   = "Mikes Hard Disk:spool/news/history";/* NNTP article msgids and timestamps */
#endif
#ifdef  NNTPS
char *Naccess   = "Mikes Hard Disk:spool/news/access";
char *Active    = "Mikes Hard Disk:spool/news/active";      /* */
char *Pointer   = "Mikes Hard Disk:spool/news/pointer"; /* */
char *NInfo = "Mikes Hard Disk:spool/news/info";        /* */
char *Nhelp = "Mikes Hard Disk:spool/news/help";        /* */
char *Forward   = "Mikes Hard Disk:spool/news/forward"; /* */
char *Poll  = "Mikes Hard Disk:spool/news/poll";        /* */
char *Newstomail = "Mikes Hard Disk:spool/news/gateway";
#endif
char Eol[]  = "\r";
#define SEPARATOR   ":"
#endif
  
static char *rootdir = ROOTDIR;
static int Assigned;
static int Initroot;
  
static void setname __ARGS((char *name,char *file));
static void tabs_to_spaces __ARGS((char *p));
void assign_filenames __ARGS((char *config));
  
extern void undosify __ARGS((char *s));
  
/* Establish a root directory other than the default. Can only be called
 * once, at startup time
 */
void
initroot(root)
char *root;
{
    if(Assigned) {
        puts("-f option used, -d ignored !\n");
        return;
    }
#ifdef  MSDOS
    undosify(root);
#endif
#ifdef UNIX
    /* ending '/' => chroot desired [but can find terminfo DB, etc?] */
    if(root[strlen(root)-1] == '/') {
        chroot(root);
        root="/";
    }
#endif

    rootdir = j2strdup( root );
  
#ifdef	APRSD
    APRSdir = rootdircat(APRSdir);	/* new for 27May2005 */
#endif

    Usagedir = rootdircat(Usagedir);	/* 29May2020, Maiko (VE4KLM), usage files */

    Startup = rootdircat(Startup);
    Userfile = rootdircat(Userfile);
    Hostfile = rootdircat(Hostfile);
    Maillog = rootdircat(Maillog);
    Spoolqdir = rootdircat(Spoolqdir);
    Mailspool = rootdircat(Mailspool);
    Mailqdir = rootdircat(Mailqdir);
    LogsDir = rootdircat(LogsDir);
    Mailqueue = rootdircat(Mailqueue);
    Routeqdir = rootdircat(Routeqdir);
    Alias = rootdircat(Alias);
    Dfile = rootdircat(Dfile);
    Fdir = rootdircat(Fdir);
    Fdbase = rootdircat(Fdbase);
    Pdbase = rootdircat(Pdbase);
#ifdef MAILCMDS
    Arealist = rootdircat(Arealist);
#endif
    Rewritefile = rootdircat(Rewritefile);
#ifdef TRANSLATEFROM
    Translatefile = rootdircat(Translatefile);
#endif
#ifdef SMTP_REFILE
    Refilefile = rootdircat(Refilefile);
#endif
#ifdef HOLD_LOCAL_MSGS
    Holdlist = rootdircat(Holdlist);
#endif
    Ftpmotd = rootdircat(Ftpmotd);
    CmdsHelpdir = rootdircat(CmdsHelpdir);
#if defined(MAILBOX) || defined(CONVERS)
    Helpdir = rootdircat(Helpdir);
#endif
#ifdef HTTP
/*    HttpDir = rootdircat(HttpDir);   would break lots of paths embedded in html docs?? */
    HttpStatsDir = rootdircat(HttpStatsDir);
#ifdef HTTP_EXTLOG
    HLogsDir = rootdircat(HLogsDir);
#endif
#endif /* HTTP */
#ifdef MAILBOX
    Signature = rootdircat(Signature);
    Historyfile = rootdircat(Historyfile);
    Motdfile = rootdircat(Motdfile);
#ifdef USERLOG
    UDefaults = rootdircat(UDefaults);
    UDefbak = rootdircat(UDefbak);
    Mregfile = rootdircat(Mregfile);
#endif
#ifdef WPAGES
	/* 02Feb2012, Maiko, Oops, forgot to rootdircat the 3 of them */
	WhitePages = rootdircat(WhitePages);
	WhitePagesBBS = rootdircat(WhitePagesBBS);
	WPUpdateFile  = rootdircat(WPUpdateFile);
#endif
#endif
#ifdef CONVERS
    Cinfo = rootdircat(Cinfo);
#ifdef CNV_CHG_PERSONAL
    Cinfobak = rootdircat(Cinfobak);
#endif
#ifdef CNV_CHAN_NAMES
    Channelfile = rootdircat(Channelfile);
#endif
    ConvMotd = rootdircat(ConvMotd);
#endif
#if (defined(POP) || defined(POP2CLIENT) || defined(POP2SERVER) || defined(POP3CLIENT) || defined(POP3SERVER))
    Popusers = rootdircat(Popusers);
#endif
#if (defined(NNTP) || defined(NNTPS))
    Newsdir = rootdircat(Newsdir);
    History = rootdircat(History);
#endif
#if (defined(MBFWD) || defined(RLINE))
    Forwardfile = rootdircat(Forwardfile);
#endif
#ifdef NETROM
    Netromfile = rootdircat(Netromfile);
#endif
    Onexit = rootdircat(Onexit);
#ifdef EXPIRY
    Expirefile = rootdircat(Expirefile);
#endif
#ifdef  NNTPS
    Naccess = rootdircat(Naccess);
    Active = rootdircat(Active);
    Pointer = rootdircat(Pointer);
    NInfo = rootdircat(NInfo);
    Nhelp = rootdircat(Nhelp);
    Forward = rootdircat(Forward);
    Poll = rootdircat(Poll);
    Newstomail = rootdircat(Newstomail);
#endif
    Initroot = 1;
}
  
/* Concatenate root, separator and arg strings into a malloc'ed output
 * buffer, then remove repeated occurrences of the separator char
 */
char *
rootdircat(filename)
char *filename;
{
    char *out = filename;
  
    if(strlen(rootdir) > 0){
        char *separator = SEPARATOR;
        out = mallocw(strlen(rootdir)
        + strlen(separator)
        + strlen(filename) +1);
        strcpy(out,rootdir);
        strcat(out,separator);
        strcat(out,filename);
        if(*separator != '\0'){
            char *p1, *p2;
        /* Remove any repeated occurrences */
            p1 = p2 = out;
            while(*p2 != '\0'){
                *p1++ = *p2++;
#ifdef UNIX
                while(p2[-1] == *separator && p2[0] == '.' && p2[1] == *separator)
                    p2 += 2;  /* replace "/./" by "/" */
#endif
                while(p2[0] == p2[-1] && p2[0] == *separator)
                    p2++;
            }
            *p1 = '\0';
        }
    }
    return out;
}
  
#ifdef PPP
  
#ifdef notdef
/* Read through FTPUSERS looking for user record
 * Returns line which matches username, or NULLCHAR when no match.
 * Each of the other variables must be copied before freeing the line.
 */
char *
userlookup(username,password,directory,permission,ip_address)
char *username;
char **password;
char **directory;
long *permission;
int32 *ip_address;
{
    FILE *fp;
    char *buf;
    char *cp;
  
    if((fp = fopen(Userfile,READ_TEXT)) == NULLFILE)
        /* Userfile doesn't exist */
        return NULLCHAR;
  
    buf = mallocw(128);
    while ( fgets(buf,128,fp) != NULLCHAR ){
        if(*buf == '#')
            continue;   /* Comment */
  
        if((cp = strchr(buf,' ')) == NULLCHAR)
            /* Bogus entry */
            continue;
        *cp++ = '\0';       /* Now points to password */
  
        if( stricmp(username,buf) == 0 )
            break;      /* Found user */
    }
    if(feof(fp)){
        /* username not found in file */
        fclose(fp);
        free(buf);
        return NULLCHAR;
    }
    fclose(fp);
  
    if ( password != NULL )
        *password = cp;
  
    /* Look for space after password field in file */
    if((cp = strchr(cp,' ')) == NULLCHAR) {
        /* Invalid file entry */
        free(buf);
        return NULLCHAR;
    }
    *cp++ = '\0';   /* Now points to directory field */
  
    if ( directory != NULL )
        *directory = cp;
  
    if((cp = strchr(cp,' ')) == NULLCHAR) {
        /* Permission field missing */
        free(buf);
        return NULLCHAR;
    }
    *cp++ = '\0';   /* now points to permission field */
  
    if (permission != NULL )
        *permission = strtol( cp, NULLCHARP, 0 );
  
    if((cp = strchr(cp,' ')) == NULLCHAR) {
        /* IP address missing */
        if ( ip_address != NULL )
            *ip_address = 0L;
    } else {
        *cp++ = '\0';   /* now points at IP address field */
        if ( ip_address != NULL )
            *ip_address = resolve( cp );
    }
    return buf;
}
#endif
  
/* Read through FTPUSERS looking for user record
 * Returns line which matches username, or NULLCHAR when no match.
 * Each of the other variables must be copied before freeing the line.
 */
char *
userlookup(username,password,directory,permission,ip_address)
char *username;
char **password;
char **directory;
long   *permission;
int32 *ip_address;
{
    FILE *fp;
    char *buf;
    char *cp;
    char *universal = NULLCHAR;
    char *defalt = NULLCHAR;
    static char fdelims[] = " \t\n";
  
    if((fp = fopen(Userfile,READ_TEXT)) == NULLFILE)
        /* Userfile doesn't exist */
        return NULLCHAR;
  
    buf = mallocw(128);
    while ( fgets(buf,128,fp) != NULLCHAR ){
        if(*buf == '#')
            continue;   /* Comment */
  
        if((cp = strpbrk(buf," \t")) == NULLCHAR)
            /* Bogus entry */
            continue;
        *cp++ = '\0';       /* Now points to password */
  
        if( stricmp(username,buf) == 0 )
            break;      /* Found user */
        if(stricmp("univperm",buf) == 0)
            universal = j2strdup(cp); /* remember their anon entry */
        if(stricmp("pppperm",buf) == 0)
            defalt = j2strdup(cp); /* remember their ppp default entry */
    }
    if(feof(fp)) { /* no explicit match */
        if((universal == NULLCHAR) && (defalt == NULLCHAR)){
            /* User name not found in file, nor was pppperm nor univperm */
            fclose(fp);
            free(buf);
            return NULLCHAR;
        }

        *buf='\0';  /* null username => pppperm or univperm entry used */
        if(defalt != NULLCHAR) /* restore pppperm entry to the buffer */
            strcpy(cp = buf+1, defalt);
        else
            strcpy(cp = buf+1, universal);
    }
    free(universal);  /* OK if NULL */
    free(defalt);
    fclose(fp);
  
    cp = strtok(cp,fdelims);
    if ( password != NULL )
        *password = cp;
  
    /* Look for space or tab after password field in file */
    if((cp = strtok(NULL,fdelims)) == NULLCHAR)    {
        /* Invalid file entry */
        free(buf);
        return NULLCHAR;
    }
  
    if ( directory != NULL )
        *directory = cp;
  
    if((cp = strtok(NULL,fdelims)) == NULLCHAR)    {
        /* Permission field missing */
        free(buf);
        return NULLCHAR;
    }
  
    if ( permission != NULL )   {
        if(!strnicmp(cp,"0x",2))
            *permission = htol(cp);
        else
            *permission = atol(cp);
    }
  
    if((cp = strtok(NULL,fdelims)) == NULLCHAR)    {
        /* IP address missing */
        if ( ip_address != NULL )
            *ip_address = 0L;
    } else {
        if ( ip_address != NULL )
            *ip_address = resolve( cp );
    }
    return buf;
}
  
#endif /* PPP */
  
  
/* Subroutine for logging in the user whose name is <name> and password is <pass>.
   The buffer <path> should be long enough to keep a line from the userfile.
   If <pwdignore> is true, the password check will be overridden.
   <defname> is the string to be used prior to "univperm" for assigning
   default permissions.
   The return value is the permissions field or -1 if the login failed.
   <path> is set to point at the path field, and <pwdignore> will be true if no
   particular password was needed for this user.

   The format of a line in the ftpusers file is:
     # this is a comment line
     name password rootdirlist1 perm1 rootdirlist2 perm2 ...
   where rootdirlist is a semicolon-separated list of directories.  We don't
   verify this here, though, and we return the FIRST permissions value found.

   MD5 extensions: the challenge that was issued, an int32, is passed in the
   first 4 bytes of <path>.  If <pass> doesn't match as a plain-text passwd,
   we compute a MD5 sum of (challenge+opt+true_password) and compare to <pass>,
   interpreted as hex digits.  Only the first MD5MINMATCH hex digits must
   match.  "opt" is only used when we were asked to validate an ftp access
   (<defname>=="ftpperm"), and is replaced by a "-".  If the ftp user is found
   to have used a leading "-" in the passwd, we set bit 2^7 in the returned
   integer <pwdignore>.  -- n5knx
*/
long
userlogin(name,pass,path,len,pwdignore,defname)
char *name;
char *pass;
char **path;
int len;            /* Length of buffer pointed at by *path */
int *pwdignore;
char *defname;
{
    char *cp;
    FILE *fp;
    char *universal = NULLCHAR;
    char *defalt = NULLCHAR;
#if defined (MD5AUTHENTICATE) && defined (FTPSERVER)
    static char FTPPERM[] = "ftpperm";
#endif
    long perm;
    char *line,*user,*password,*privs,*directory;
#ifdef MD5AUTHENTICATE
    int32 challenge;
    MD5_CTX md;
    int i,j;

    memcpy((char *)&challenge, *path, sizeof(challenge));
#endif  /* MD5AUTHENTICATE */
    /* Check environment variables first - WG7J (incompatible with MD5) */
    if((user=getenv("NOSSYSOP")) != NULL) {
        if(strcmp(user,name) == 0) {
            /* the right user name, now check password */
            if((password=getenv("NOSPASSWD")) != NULL) {
                if(*pwdignore || strcmp(password,pass) == 0) {
                    *pwdignore = 0;
                    if((directory=getenv("NOSPATH")) != NULL)
                        strcpy(*path,directory);
                    if((privs=getenv("NOSPRIVS")) != NULL) {
                        if(strnicmp(privs,"0x",2) == 0)
                            return htol(privs);
                        else
                            return atol(privs);
                    } else
                        return (FTP_READ + \
                        FTP_CREATE + \
                        FTP_WRITE + \
                        AX25_CMD + \
                        TELNET_CMD + \
                        NETROM_CMD + \
                        SYSOP_CMD + \
                        IS_EXPERT);
  
                }
            }
        }
    }
  
    if((fp = fopen(Userfile,READ_TEXT)) == NULLFILE) {
        /* Userfile doesn't exist */
        *pwdignore = 0;
        return -1;
    }
    while(fgets(*path,len,fp),!feof(fp)){
        line = skipwhite(*path);
        if(*line == '#')
            continue;   /* Comment */
        if((password = strpbrk(line," \t")) == NULLCHAR)
            /* Bogus entry */
            continue;
        rip(line);
        *password++ = '\0';
         /* Now points to spaces or tabs before password */
        password = skipwhite(password);
         /* Now points to password */
        if(stricmp(name,line) == 0)
            break;      /* Found user name */
        if(stricmp("univperm",line) == 0)
            universal = j2strdup(password); /* remember their anon entry */
	if(stricmp(defname,line) == 0)
	    defalt = j2strdup(password);  /* remember their default entry */
    }
    if(feof(fp)) {  /* no explicit match */
        if((universal == NULLCHAR) && (defalt == NULLCHAR)){
            /* User name not found in file, nor was default nor univperm */
            fclose(fp);
            /* No need to free universal/default ( == NULLCHAR ) remember !!! */
            return -1;
        }
        else if(defalt != NULLCHAR){
            /* restore defname to the buffer */
            strcpy(password = *path, defalt);
        }
        else {
            /* restore anonymous to the buffer */
            strcpy(password = *path, universal);
        }
    }
    free(universal);  /* OK if NULLCHAR ... free() just returns */
    free(defalt);
#ifdef MSDOS
    if(feof(fp)) {
    /* n5knx: When default or universal entry is used, we must disallow
       login if userid was > 8 chars long, since otherwise MSDOS will truncate
       it to 8 when used as a mailbox name, and we may then access another's
       mailbox!  A long userid is OK for ftp purposes, though.
    */
        if (strlen(name) > 8 && strcmp(defname,FTPPERM)) {
            fclose(fp);
            return -1;
        }
    }
#endif /* MSDOS */

    fclose(fp);
  
    /* Look for space or tab after password field in file */
    if((directory = strpbrk(password," \t")) == NULLCHAR)
        /* Invalid file entry */
        return -1;
    *directory++ = '\0';  /* Terminate password */
    /* Find start of path */
    directory = skipwhite(directory);
    if((int)strlen(directory) + 1 > len )
        /* not enough room for path */
        return -1;
  
    if(strcmp(password,"*") == 0)
        *pwdignore = 1;  /* User ID is password-free */
    if(!(*pwdignore) && strcmp(password,pass) != 0) {
        /* Password required, but wrong one given */
#ifdef MD5AUTHENTICATE
        /* See if it is the response to a challenge */
        if(strlen(pass) >= MD5MINMATCH) {
#ifdef FTPSERVER
            int k,m;
            if (strcmp(defname,FTPPERM))
                m=1;  /* not ftpperm, don't check for '-' prefix */
            else
                m=2;
            for (k=0; k<m; k++) {
#endif /* FTPSERVER */
            MD5Init(&md);
            MD5Update(&md,(unsigned char *)&challenge,sizeof(challenge));
#ifdef FTPSERVER
            if (k)  /* try '-' prefix to password */
                MD5Update(&md,(unsigned char *)"-",1);
#endif
            MD5Update(&md,(unsigned char *)password,strlen(password));
            MD5Final(&md);
#ifdef MD5DEBUG
            printf("auth: challenge %x, passwd %s\nMD5 sum is ", challenge, password);
            for(i=0; i<16; i++)
                printf("%02x",md.digest[i]);
            printf("\n");
#endif
            for(cp=pass,i=0; i<MD5MINMATCHB; cp+=2,i++) {
                sscanf(cp,"%2x",&j);
                if (md.digest[i] != (unsigned char)j)
#ifdef FTPSERVER
                    break;
#else
                    return -1;   /* give up at first miscompare */
#endif /* FTPSERVER */
            }
#ifdef FTPSERVER
            if (i>=MD5MINMATCHB) {  /* comparison succeeded */
                /* Was the '-' prefix necessary? */
                if (k)
                    *pwdignore |= 0x0080;  /* flag that '-' prefix was used */
                break;   /* exit for(k...) loop */
            }
            }  /* end of for(k..) loop */
            if (k>=m)  /* failed to match? */
                return -1;
#endif /* FTPSERVER */
            /* MD5(challenge+passwd_from_ftpusers) matches! */
        }
        else return -1;
#else
        return -1;
#endif  /* MD5AUTHENTICATE */
    }
  
    /* Find permission field */
    if((privs = strpbrk(directory," \t")) == NULLCHAR)
        /* Permission field missing */
        return -1;
    /* Find start of permissions */
    privs = skipwhite(privs);
    if(!strnicmp(privs,"0x",2))
        perm = htol(privs);
    else
        perm = atol(privs);
  
    /* Now copy the compound path, with possibly multiple paths and permissions */
    for (cp = directory; *cp; cp++)      /* allow '=' prefix to flag ftp homedir ... Selcuk */
        if (*cp == '\t') *cp = ' ';      /* by changing existing tabs to spaces */
        else if (*cp == '=') *cp = '\t'; /* and change '=' to the only tab */

    strcpy(*path,directory);
    dirformat(*path);
  
    /* Finally return the permission bits */
    return perm;
}
  
#ifdef MD5AUTHENTICATE
/* 16Nov2009, Maiko, Changed to int32 (always 32 bit) from long */
char *md5sum (int32 challenge, char *s)
{
    int i;
    MD5_CTX md;
    static char hexsig[33];
    char *cp;

    MD5Init(&md);
    MD5Update(&md,(unsigned char *)&challenge,sizeof(challenge));
    MD5Update(&md,(unsigned char *)s,strlen(s));
    MD5Final(&md);
    for(i=0, cp=hexsig; i<MD5MINMATCHB; i++, cp+=2)
        sprintf(cp,"%02x",md.digest[i]);

#ifdef MD5DEBUG
    printf("md5sum: %x + %s :> %s\n", challenge, s, hexsig);
#endif
    return hexsig;
}
#endif

struct Nosfiles {
    char *name;
    char **path;
};
  
static struct Nosfiles DFAR Nfiles[] = {
    { "Startup",&Startup },

    { "Usagedir",&Usagedir },	/* 29May2020, Maiko (VE4KLM), usage files */

#ifdef	APRSD
	{ "APRSdir",&APRSdir },		/* new for 27May2005 */
#endif
    { "Userfile",&Userfile },
    { "Hostfile",&Hostfile },
    { "Spoolqdir",&Spoolqdir },
    { "Maillog",&Maillog },
    { "Mailspool",&Mailspool },
    { "Mailqdir",&Mailqdir },
    { "LogsDir",&LogsDir },
    { "Mailqueue",&Mailqueue },
    { "Routeqdir",&Routeqdir },
    { "Alias",&Alias },
    { "Dfile",&Dfile },
    { "Fdir",&Fdir },
    { "Fdbase",&Fdbase },
    { "Pdbase",&Pdbase },
    { "Rewritefile",&Rewritefile },
#ifdef TRANSLATEFROM
    { "Translatefile",&Translatefile },
#endif
#ifdef SMTP_REFILE
    { "Refilefile",&Refilefile },
#endif
#ifdef HOLD_LOCAL_MSGS
    { "Holdlist",&Holdlist },
#endif
    { "Onexit",&Onexit },
    { "Ftpmotd",&Ftpmotd },
    { "CmdsHelpdir",&CmdsHelpdir },
#if defined(MAILBOX) || defined(CONVERS)
    { "Helpdir",&Helpdir },
#endif
#ifdef HTTP
    { "HttpDir",&Httpdir },
    { "HttpStatsDir",&HttpStatsDir },
#ifdef HTTP_EXTLOG
    { "HLogsDir",&HLogsDir },
#endif
#endif /* HTTP */
#ifdef MAILBOX
    { "Signature",&Signature },
    { "Historyfile",&Historyfile },
    { "Motdfile",&Motdfile },
#ifdef MAILCMDS
    { "Arealist",&Arealist },
#endif
#ifdef USERLOG
    { "UDefaults",&UDefaults },
    { "UDefbak",&UDefbak },
    { "Mregfile",&Mregfile },
#endif
#ifdef WPAGES
	/* 02Feb2012, Maiko, Oops, forgot to include whitepage mappings */
	{ "WhitePages", &WhitePages },
	{ "WhitePagesBBS", &WhitePagesBBS },
	{ "WPUpdateFile", &WPUpdateFile },
#endif
#endif
#ifdef CONVERS
    { "Cinfo",&Cinfo },
#ifdef CNV_CHG_PERSONAL
    { "Cinfobak",&Cinfobak },
#endif
#ifdef CNV_CHAN_NAMES
    { "Channelfile",&Channelfile },
#endif
    { "ConvMotd",&ConvMotd },
#endif
#if (defined(POP) || defined(POP2CLIENT) || defined(POP2SERVER) || defined(POP3CLIENT) || defined(POP3SERVER))
    { "Popusers",&Popusers },
#endif
#if (defined(NNTP) || defined(NNTPS))
    { "Newsdir",&Newsdir },
    { "History",&History },
#endif
#if (defined(MBFWD) || defined(RLINE))
    { "Forwardfile",&Forwardfile },
#endif
#ifdef NETROM
    { "Netromfile",&Netromfile },
#endif
#ifdef EXPIRY
    { "Expirefile",&Expirefile },
#endif
#ifdef NNTPS
    { "Naccess",&Naccess },
    { "Active",&Active },
    { "Pointer",&Pointer },
    { "NInfo",&NInfo },
    { "Nhelp",&Nhelp },
    { "Forward",&Forward },
    { "Poll",&Poll },
    { "Newstomail",&Newstomail },
#endif
    { NULLCHAR, NULL }
};

#if defined(MSDOS) || defined(UNIX)
void setname(char *name,char *file) {
    int i = 0;
  
    while(Nfiles[i].name != NULLCHAR) {
        if(strcmp(name,Nfiles[i].name) == 0) {
            if(Initroot)
                free(*Nfiles[i].path);
            *Nfiles[i].path = j2strdup(file);
            return;
        }
        i++;
    }
}
  
void tabs_to_spaces(char *p) {
    while(*p != '\0') {
        if(*p == '\t')
            *p = ' ';
        p++;
    }
}
  
void
assign_filenames(char *config) {
    FILE *fp;
    char *name,*file,*cp;
    int line;
#define BUFLEN 128
    char buf[BUFLEN+1];
  
    /* n5knx: we can't use tprintf/tputs since Curproc not estblished yet */
    undosify(config);
    if((fp = fopen(config,"r")) == NULL) {
        printf("Cannot open %s\n",config);
        return;
    }
  
    line = 0;
    while(fgets(buf,BUFLEN,fp) != NULL) {
        line++;
        if(*buf == '#')         /* comment */
            continue;
        rip(buf);   /* delete ending \n */
        tabs_to_spaces(buf);
        name = buf;
        while(*name == ' ')    /* no leading spaces */
            name++;
        if(*name == '\0')      /* blank line */
            continue;
        /* Search for filename */
        if((file = strchr(name,'=')) == NULL) {
            printf("Need '=', line %d of %s.\n",line,config);
            continue;
        }
        *file++ = '\0';
        /* cut trailing spaces */
        if((cp = strchr(name,' ')) != NULL)
            *cp = '\0';
  
        /* find start of filename */
        while(*file == ' ')
            file++;
  
        if(*file == '\0')
            continue;
  
        /* cut trailing spaces */
        if((cp = strchr(file,' ')) != NULL)
            *cp = '\0';
  
        /* Now parse the name, and assign if valid */
        setname(name,file);
    }
    fclose(fp);
    Assigned = 1;
}
#endif
  
