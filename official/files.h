#ifndef _FILES_H
#define _FILES_H
  
/* see mailbox.h for most other privs definitions */
#define PPP_ACCESS_PRIV 0x0100L  /* Priv bit for PPP connection */
#define PPP_PWD_LOOKUP  0x0200L  /* Priv bit for peerID/pass lookup */

#define FILE_PATH_SIZE 128
  
/* External definitions for configuration-dependent file names set in
 * files.c
 */

extern char *APRSdir;	/* new for 27May2005, APRS root directory */

extern char *Startup;   /* Initialization file */
extern char *Userfile;  /* Authorized FTP users and passwords */
extern char *Hostfile;  /* Remote FTP user and password */
extern char *Spoolqdir; /* Spool directory */
extern char *Maillog;   /* mail log */
extern char *Mailspool; /* Incoming mail */
extern char *Mailqdir;  /* Outgoing mail spool */
extern char *LogsDir;   /* Logs directory */
extern char *Mailqueue; /* Outgoing mail work files */
extern char *Routeqdir; /* queue for router */
extern char *Alias; /* the alias file */
extern char *Dfile; /* Domain cache */
extern char *Fdir;  /* Finger info directory */
extern char *Fdbase;        /* Finger database file */
extern char *Pdbase;        /* Personal names database file (IW5DHE) */
extern char *Arealist;      /* List of message areas */
extern char *Helpdir;       /* Mailbox help file directory */
extern char *CmdsHelpdir;   /* Console/Sysop commands help file directory */
extern char *Motdfile;      /* Mailbox message of the day */
extern char *Rewritefile;   /* TO-Address rewrite file */
extern char *Translatefile; /* FROM-Address rewrite file */
extern char *Refilefile;    /* FROM|TO-Address rewrite file */
extern char *Holdlist;      /* Areas in which local msgs are marked Held */
extern char *Httpdir;       /* Http server root directory */
extern char *HLogsDir;      /* Http log directory */
extern char *HttpStatsDir;  /* Directory for page counters */
extern char *Signature;     /* Mail signature file directory */
extern char *Popusers;      /* POP user and password file */
extern char *Newsdir;       /* News messages and NNTP data */
extern char *Forwardfile;   /* Mail forwarding file */
extern char *Historyfile;   /* Message ID history file */
extern char *UDefaults;     /* User preference file */
extern char *UDefbak;       /* Backup of preference file */
extern char *Mregfile;      /* User registration message file */
extern char *Cinfo;         /* Convers information file */
extern char *Cinfobak;      /* Convers information backup */
extern char *Channelfile;   /* Convers channel names */
extern char *ConvMotd;      /* Convers motd file */
extern char *Netromfile;    /* Netrom node save file */
extern char *Onexit;        /* Cmds executed on exit */
extern char *Expirefile;    /* Message expiration control file */
extern char *Ftpmotd;       /* FTP message of the day */
extern char *Naccess;       /* NNTPS access file (permissions) */
extern char *Active;        /* NNTPS active file (name, max&min art#, flag) */
extern char *Pointer;       /* NNTPS file of ng name and its dir */
extern char *NInfo;
extern char *Nhelp;
extern char *History;       /* NNTP file of article msgids and timestamps */
extern char *Forward;       /* NNTPS dir for unrecognized ng names */
extern char *Poll;          /* NNTPS file of servername and time-last-contacted */
extern char *Newstomail;    /* NNTPS file of newsgroup and SMTP To: addr maps */
  
#ifdef UNIX
/* Session manager defaults */
extern char *Trace_sessmgr;	/* Session manager for Trace session */
extern char *Command_sessmgr;	/* Session manager for Command session */
#endif /* UNIX */

/* In files.c: */
void initroot __ARGS((char *root));
long userlogin __ARGS((char *name,char *pass,char **path,int len,int *pwdignore,char *defname));
char *rootdircat __ARGS((char *filename));
char *userlookup __ARGS((char *username,char **password,char **directory,
                         long *permission,int32 *ip_address));

/* 16Nov2009, Maiko, challenge stays as 32 bit, don't use LONG, use int32 */
char *md5sum(int32 challenge, char *s);

/* In ftpserv.c: */
int permcheck __ARGS((char *path,int op,char *file));
  
#endif  /* _FILES_H */
  
