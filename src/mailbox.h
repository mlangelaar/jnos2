#ifndef _MAILBOX_H
#define _MAILBOX_H
/* Defines for the ax.25 mailbox facility */
  
#ifndef _SMTP_H
#include "smtp.h"
#endif
  
#ifndef _TIMER_H
#include "timer.h"
#endif
  
#ifndef _AX25_H
#include "ax25.h"
#endif
  
#ifndef _CONFIG_H
#include "config.h"
#endif
  
#ifndef _TIPMAIL_H
#include "tipmail.h"
#endif
  
#ifndef _INDEX_H
#include "index.h"
#endif
  
#ifndef _LZHUF_H
#ifdef __GNUC__
struct fwd;  /* forward reference ... keeps GCC happy! */
#endif
#include "lzhuf.h"
#endif

/* some macros to access a telnet options bit in an array of 32-bit words */  
#define OPT_IS_SET(m,i)        (m->tel_opts[(i)/32] & (1L<< (i)%32))
#define OPT_IS_DEFINED(m,i)    (m->opt_defined[(i)/32] & (1L<< (i)%32))
#define SET_OPT(m,i)    m->tel_opts[(i)/32] |= (1L << (i)%32), \
                        m->opt_defined[(i)/32] |= (1L << (i)%32)
#define RESET_OPT(m,i)  m->tel_opts[(i)/32] &= ~(1L << (i)%32), \
                        m->opt_defined[(i)/32] |= (1L << (i)%32)

#define FWD_SCRIPT_CMDS "#.+@&*!"

/* a mailbox entry */
struct let {
    long    start;
    long    size;
    int     status;
#ifdef USERLOG
    long    msgid;
#endif
#ifdef FBBFWD
    long    indexoffset;    /* Location of Index record in the *.idn file */
#endif
};
  
#define MAXPWDLEN 64        /* sysop passwd string...longer the better */
#define MBXLINE 128         /* max length of line */
#define MBXNAME 20          /* max length of user name */
#define CNAMELEN 10         /* max length of convers names */
#define AREALEN 64
  
struct mbx {
    struct mbx *next;       /* next one on list */
    int state ;             /* mailbox state */
#define MBX_LOGIN       0               /* trying to log in */
#define MBX_CMD         1               /* in command mode */
#define MBX_SUBJ        2               /* waiting for a subject line */
#define MBX_DATA        3               /* collecting the message */
#define MBX_REVFWD      4               /* reverse forwarding in progress */
#define MBX_TRYING      5               /* pending forwarding connection */
#define MBX_FORWARD     6               /* established forwarding connection */
#define MBX_GATEWAY 7       /* gatewaying somewhere */
#define MBX_READ    8       /* reading a message */
#define MBX_UPLOAD  9       /* uploading a file */
#define MBX_DOWNLOAD 10     /* downloading a file */
#define MBX_CONVERS 11      /* Using convers mode */
#define MBX_CHAT    12      /* Chatting with sysop */
#define MBX_WHAT    13      /* Listing files */
#define MBX_SYSOPTRY 14     /* Trying sysop */
#define MBX_SYSOP   15      /* Is sysop */
#define MBX_XMODEM_RX 16    /* receiving xmodem */
#define MBX_XMODEM_TX 17    /* sending xmodem */
#ifdef	B2F
/*
 * 24Mar2008, Maiko Langelaar (VE4KLM), Need to know if B2F is active,
 * since there is an extra 2 byte CRC that needs to be *skipped* ...
 */
#define MBX_REVB2F    18     /* reverse forwarding (B2F) in progress */
#endif
    int family;             /* Type of incoming connection */
    char name[MBXNAME+1];   /* Name of remote station */
    char call[AXALEN];      /* User call in shifted form, if applicable*/
#ifdef USERLOG
    char *username;         /* User's name from registration */
    char *homebbs;          /* User's homebbs address */
    char *IPemail;          /* User's email address */
    long last;              /* Time of last login */
#endif
#ifdef MAILCMDS
    char *to ;          /* To-address in form user or user@host */
    char *origto ;                  /* Original To-address, if rewritten */
    char *tofrom ;                  /* Optional <from in to-address */
    char *origbbs ;         /* Original bbs, if tracing R: lines */
    char *tomsgid ;         /* Optional $msgid in to-address */
    char *subject ;         /* Message subject */
    char *date ;            /* Date of the message */
    FILE *mfile;            /* Used during reverse forwarding */
    FILE *tfile ;           /* Temporary file for message, or */
                            /* forwarding script file. */
    FILE *tfp;              /* Temporary file when reading R: headers */
#endif
    char line[MBXLINE+1] ;  /* Room for null at end */
    int sid ;               /* Characteristics indicated by the SID */
                            /* banner of the attaching station.  If */
                            /* no SID was sent, this is zero.  If an */
                            /* SID of any kind was received, it is */
                            /* assumed that the station supports */
                            /* abbreviated mail forwarding mode. */
#define MBX_SID         0x01    /* Got any SID */
#define MBX_RLI_SID     0x02    /* This is an RLI BBS, disconnect after F> */
#define MBX_HIER_SID    0x04    /* The BBS supports hierarchical routing */
#define MBX_EXPERT  0x08        /* expert user status */
#define MBX_AREA    0x10        /* show area in prompt */
#define MBX_NRID    0x20        /* use netrom ident */
#define MBX_FBBCMP  0x40        /* F6FBB bbs - Compression-capable */
#define MBX_LL      0x80        /* LAN-LINK system */
#define MBX_MID     0x0100      /* BBS supports MID's */
#define MBX_FBB     0x0200      /* F6FBB bbs */
#define MBX_REPLYADDR 0x0400    /* Include a 'Reply-to:' line in mail */
#define MBX_FBBFWD  0x0800      /* F6FBB bbs - batched-forwarding possible */
#define MBX_LINEMODE 0x1000     /* set (by XP) to disable attempts to read single char responses */
#define MBX_FOREGO_FBBCMP 0x2000/* set (by ! script cmd) to avoid using FBB compression */

/*
 * 27Aug2013, Maiko (VE4KLM), really need to complete this and make sure we
 * actually properly handle and support B1F (which up till now we didn't).
 */
#define MBX_FBBCMP1  0x4000	/* F6FBB bbs - B1F Compression-capable */

    char stype ;            /* BBS send command type (B,P,T, etc.) */
    int type ;              /* Type of session when invoking "chat" */
    int user;               /* User linkage area */
    struct proc *proc;      /* My process */
    char escape;            /* Escape character */
    long privs;             /* Privileges (taken from Ftpusers file) */
#define FTP_READ        1L       /* 0x001 Read files */
#define FTP_CREATE      2L       /* 0x002 Create new files */
#define FTP_WRITE       4L       /* 0x004 Overwrite or delete existing files */
#define AX25_CMD        8L       /* 0x008 AX.25 gateway operation allowed */
#define TELNET_CMD      16L      /* 0x010 Telnet gateway operation allowed */
#define NETROM_CMD      32L      /* 0x020 NET/ROM gateway operation allowed */
#define SYSOP_CMD       64L      /* 0x040 Remote sysop access allowed */
#define EXCLUDED_CMD    128L     /* 0x080 This user is banned from the BBS */
/* 256 and 512 are used in PPP (see files.h) */
#define NO_SENDCMD      1024L    /* 0x400 Disallow send command */
#define NO_READCMD      2048L    /* 0x800 Disallow read command */
#define NO_3PARTY       4096L    /* 0x1000 Disallow third-party mail */
#define IS_BBS          8192L    /* 0x2000 This user is a bbs */
#define IS_EXPERT       16384L   /* 0x4000 This user is an expert */
#define NO_CONVERS      32768L   /* 0x8000 Disallow convers command */
#define NO_ESCAPE       65536L   /* 0x10000 Don't check for esc char in mbox.  See E cmd */
#define NO_LISTS        131072L  /* 0x20000 Disallow mbox IH, IP, P, N, NR cmds */
#define NO_LINKEDTO     262144L  /* 0x40000 No '*** LINKED TO' allowed */
#define NO_LASTREAD     524288L  /* 0x80000 Ignore lastread in <area>.usr (for shared accts) */
#define NO_FBBCMP      1048576L  /* 0x100000 Avoid FBB compression */
#define XG_ALLOWED     2097152L  /* 0x200000 Allow XG (dynip route) cmd */
#define T_NO_AMPRNET   4194304L  /* 0x400000 Disallow Telnet to 44/8 */
#define T_AMPRNET_ONLY 8388608L  /* 0x800000 Allow Telnet to only 44/8 */

    int32 tel_opts[2];      /* storage for telnet options */
    int32 opt_defined[2];   /* option set/reset flag */
#define MAXTELNETOPT 63
    char *path;             /* Directory path */
    char *startmsg;         /* Message to be sent at connect */
#ifdef MAILCMDS
    long mboxsize;          /* size of mailbox when opened */
    int mycnt;              /* number of msgs in my private mailbox */
    struct let *mbox;
    int current;            /* the current message number */
    int nmsgs;              /* number of messages in this mail box */
    int newmsgs;            /* number of new messages in mail box */
    int change;             /* mail file changed */
    int anyread;            /* true if any message has been read */
    char area[AREALEN+1];          /* name of current mail area */
    int areatype;           /* is the current mail area private or public ? */
#define USER    0           /* Sysop checking other personal areas */
#define PRIVATE 1
#define AREA    2
    int lockcnt;	    /* of associated mbox */
#endif
    int morerows;           /* Number of lines before -more- prompt */
/* Next two are used by userlog code - WG7J */
#ifdef USERLOG
    long lastread;          /* number of last read message in area */
    long newlastread;       /* id of new last listed message in area */
#endif
#ifdef MAILCMDS
    char *stdinbuf;         /* the stdio buffer for the mail file */
    char *stdoutbuf;        /* the stdio file io buffer for the temp file */
#endif
#ifdef TIPSERVER
    struct tipcb *tip;       /* tip structure if tip mail incoming */
#endif

/* 25Mar2008, Maiko (VE4KLM), The 'sid' var is *full*, we need another flag */
#ifdef	B2F
    int sid2;			/* newer SID options and FWD flags */
#define MBX_B2F  0x01		/* WinLink B2F */
#endif

};

#define         NULLMBX         (struct mbx *)0
  
/* Structure used for automatic flushing of gateway sockets */
struct gwalarm {
    int s1;
    int s2;
    struct timer t;
};
  
#ifdef MBFWD
/* A forward entry */
struct fwd {
    struct mbx *m;
    char bid[15];
    struct mailindex ind;
#ifdef FBBFWD
    struct fbbpacket *msglst;
    struct fwdarealist *FwdAreas;
#endif
#ifdef FBBCMP
    struct lzhufstruct  *lzhuf;
    char                iFile[80];
    char                oFile[80];
    char                *tmpBuffer;
#endif
};
#ifdef FBBFWD
// FBB Packet
struct fbbpacket {
     int    number;          // Message number in mail area.
     int    accept;          // NotActive = 0 / Reject(-) = 1
                             // Accept(+) = 2 / Defer(=)  = 3
     char   fbbcmd[3];       // FB ( FA when compression is added )
     char   type;            // (B)ulletin, (P)ersonal, (T)raffic/NTS
     char   bid[15];         // Hold makecl() modified bid.
     char   *sline;          // bufptr that holds the bbs send command
                             // used to enter a message onto the system.
     char   *to;
     char   *from;
     char   *messageid;
     char   *rewrite_to;
     int    size;
};

struct fwdarealist {
    char *name;
    char *opt_dest;
    long mindeferred;
    long maxdeferred;
    struct fwdarealist *next;
};
#endif
#endif
  
#define MSG_MODIFY  0
#define MSG_READ    1
  
struct alias {
    struct alias *next;
    char *name;
    char *cmd;
};
extern struct alias *AliasList;
  
/* In converse.c */
extern void mbox_converse(struct mbx *m,int channel);
extern int ShowConfLinks __ARGS((int s,int full));
extern int ShowConfUsers __ARGS((int s,int quick,char *name));
  
/* In mailbox.c */
extern char Noperm[];
extern char Nosock[];
extern char Mbpasswd[];
extern char MboxId[];
#ifdef B2F
/* 25Mar2008, Maiko (VE4KLM), New B2F (WinLink) SID support */
extern char MboxIdB2F[];
#endif
#ifdef FBBFWD
extern char MboxIdF[];
#endif
#ifdef FBBCMP
extern char MboxIdFC[];
extern char MboxIdB1F[];  /* 28Aug2013, Maiko (VE4KLM), Support B1F !!! */
#endif
extern char Badmsg[];
extern char Nomail[];
extern char Mbnrid[];
extern char Mbwarning[];
extern char Howtoend[];
extern char MsgAborted[];
extern char *Mtmsg;
  
extern struct mbx *Mbox;
extern void (*Listusers) __ARGS((int s));
  
extern int ThirdParty;
extern int Mtrace;

#ifdef FBBFWD

/*
 * 26Mar2008, Maiko (VE4KLM), Added new B2F support for WinLink / Airmail
 *
 * 0 => MBL
 * 1 => FBB batched
 * 2 => FBB batched (compressed)
 * 3 => FBB batched (B2F compressed)
 */

extern int16 Mfbb;

#endif

extern int MbRegister;
extern int MAttended;
extern int Mbloophold;
extern int NoBid;
extern int Mbnewmail;
extern int Usenrid;
extern int MBSecure;
extern int Mbsendquery;
  
extern int32 Mbtdiscinit;
extern unsigned Tiptimeout;

/* 08Mar2009, Maiko, Add prototype to resolve some compiler warnings */
extern void logmbox (int, char*, char*, ...);

/*  In mboxgate.c */  
int dombescape __ARGS((int argc,char *argv[],void *p));
int dombtelnet __ARGS((int argc,char *argv[],void *p));
void gw_alarm __ARGS((void *p));
int gw_connect __ARGS((struct mbx *m,int s,struct sockaddr *fsocket,int len));
void gw_input __ARGS((int s,void *notused,void *p));
void gw_superv __ARGS((int null,void *proc,void *p));
int dombnrnodes __ARGS((int argc,char *argv[],void *p));
int dombnrneighbour __ARGS((int argc,char *argv[],void *p));
int dombconnect __ARGS((int argc,char *argv[],void *p));
int dombports __ARGS((int argc,char *argv[],void *p));

/* In mailbox.c */  
void mbx_incom __ARGS((int s,void *t,void *p));
void mbx_incom_cr __ARGS((int s,void *t,void *p));
int mbxrecvline __ARGS((struct mbx *m));
int domboxdisplay __ARGS((int argc,char *argv[],void *p));
struct mbx *newmbx __ARGS((void));
void exitbbs __ARGS((struct mbx *m));
int domboxbye __ARGS((int argc,char *argv[],void *p));
int mbx_parse __ARGS((struct mbx *m));
/*char *rewrite_address __ARGS((char *addr));*/
void listusers __ARGS((int s));
void putprompt __ARGS((struct mbx *m));
int charmode_ok __ARGS((struct mbx *m));
int dombconvers __ARGS((int argc,char *argv[],void *p));
int dombfinger __ARGS((int argc,char *argv[],void *p));
int dombiproute __ARGS((int argc,char *argv[],void *p));
int dombsemicolon __ARGS((int argc,char *argv[],void *p));
int dosysop __ARGS((int argc,char *argv[],void *p));
int dostars __ARGS((int argc,char *argv[],void *p));
  
/* In mboxfile.c */
int dodownload __ARGS((int argc,char *argv[],void *p));
int dombupload __ARGS((int argc,char *argv[],void *p));
int dowhat __ARGS((int argc,char *argv[],void *p));
int doxmodem __ARGS((char mode,char *filename,void *p));
int dozap __ARGS((int argc,char *argv[],void *p));
int uuencode __ARGS((FILE *infile,int s,char *infilename));

/* In mboxcmd.c */
struct alias *findalias(char *cmd);
struct alias *findalias(char *cmd);
int dombalias(int argc,char *argv[],void *p);
int doaliases(int argc,char *argv[],void *p);
int dombuserinfo __ARGS((int argc,char *argv[],void *p));
int dombholdlocal __ARGS((int argc,char *argv[],void *p));
void loguser __ARGS((struct mbx *m));
void setmbnrid __ARGS((void));
void updatedefaults __ARGS((struct mbx *));
char *cmd_line __ARGS((int argc,char *argv[],char stype));
void listnewmail(struct mbx *m,int silent);
  
/* In forward.c: */
#ifdef MBFWD
int dorevfwd __ARGS((int argc,char *argv[],void *p));
int dofbbfwd __ARGS((int argc,char *argv[],void *p));
void exitfwd __ARGS((struct mbx *));
int sendmsg __ARGS((struct fwd *f,int msgn));
char *mbxtime __ARGS((time_t date));
int fwdinit __ARGS((struct mbx *m));
int makecl __ARGS((struct fwd *f,int msgn,char *dest,char *line,char **subj,int *bul));
int dombtimer __ARGS((int argc,char *argv[],void *p));
int dombkick __ARGS((int argc,char *argv[],void *p));
#endif

/* In mboxmail.c */  
void changearea __ARGS((struct mbx *m,char *area));
int doarea __ARGS((int argc,char *argv[],void *p));
int dosend __ARGS((int argc,char *argv[],void *p));
int dosid __ARGS((int argc,char *argv[],void *p));
int mbx_to __ARGS((int argc,char *argv[],void *p));
int mbx_data __ARGS((struct mbx *m,struct list *cclist,char *extra));

/* 31Aug2020, Maiko (VE4KLM), added socket paratmer for better logging */
int msgidcheck __ARGS((int s, char *string));

int storebid __ARGS((char *bid));
int dombmovemail __ARGS((int argc,char *argv[],void *p));
int thirdparty __ARGS((struct mbx *m));


int dombping __ARGS((int argc,char *argv[],void *p));
int donrneighbour __ARGS((int argc,char *argv[],void *p));
int doregister  __ARGS((int argc,char *argv[],void *p));
int dombusers __ARGS((int argc,char *argv[],void *p));
int dombpast __ARGS((int argc,char *argv[],void *p));
int dombstatus __ARGS((int argc,char *argv[],void *p));
int dombmailstats __ARGS((int argc,char *argv[],void *p));
int doipheard __ARGS((int argc,char *argv[],void *p));
  
void eout(char *s);
  
#endif  /* _MAILBOX_H */
  
