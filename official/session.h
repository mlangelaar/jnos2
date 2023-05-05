/* Mods by PA0GRI */
#ifndef _SESSION_H
#define _SESSION_H
  
#include <stdio.h>
  
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
#ifndef _PROC_H
#include "proc.h"
#endif
  
#ifndef _UNIX_H
#include "unix.h"
#endif
  
  
#ifndef _FTPCLI_H
#include "ftpcli.h"
#endif
  
#ifndef _TELNET_H
#include "telnet.h"
#endif
  
#define MOREROWS        24      /* rows on screen before --more-- */
  
struct ttystate {
    struct mbuf *line;      /* Line buffer */
    int echo;               /* Keyboard local echoing? */
    int edit;               /* Local editing? */
    int crnl;               /* Translate cr to lf? */
};
  
/* Session control structure; only one entry is used at a time */
struct session {
    int type;
#define FREE    0
#define TELNET  1
#define FTP     2
#define AX25TNC 3
#define FINGER  4
#define PING    5
#define NRSESSION 6
#define COMMAND 7
#define MORE    8
#define HOP     9
#define TIP     10
#define PPPPASS 11
#define DIAL    12
#define DQUERY  13
#define DCLIST  14
#define RLOGIN  15
#define REPEAT  16
#define LOOK    17
#define TRACESESSION 18
#define AXUITNC 19
#define EDIT    20
#define NN      21
  
    int num;        /* Number of the session, for status line - WG7J */
    char *name;     /* Name of remote host */
    union {
        struct ftpcli *ftp;
        struct telnet *telnet;
    } cb;
    struct proc *proc;      /* Primary session process (e.g., tn recv) */
    struct proc *proc1;     /* Secondary session process (e.g., tn xmit) */
    struct proc *proc2;     /* Tertiary session process (e.g., upload) */
    int s;                  /* Primary network socket (control for FTP) */
    FILE *record;           /* Receive record file */
    char *rfile;            /* Record file name */
    FILE *upload;           /* Send file */
    char *ufile;            /* Upload file name */
    struct ttystate ttystate;
    struct screen *screen;
    int input;              /* Input socket */
    int output;             /* Output socket */
    int flowmode;           /* control "more" mode */
    int row;                /* Rows remaining until "more" */
    int morewait;           /* Output driver is waiting on us */
    int tsavex;             /* Save for x top screen */
    int tsavey;             /* Save for y top screen */
    int bsavex;             /* Save for x bottom screen */
    int bsavey;             /* Save for y bottom screen */
    int split;              /* Signal for split screen */
    struct cur_dirs *curdirs;
};
#define NULLSESSION     (struct session *)0
  
extern char *Sestypes[];
extern int Nsessions;                   /* Maximum number of sessions */
extern struct session *Sessions;        /* Session descriptors themselves */
extern struct session *Current;         /* Always points to current session */
extern struct session *Lastcurr;        /* Last non-command session */
extern struct session *Command;         /* Pointer to command session */
  
/* In session.c: */
void freesession __ARGS((struct session *sp));
struct session *sessptr __ARGS((char *cp));
struct session *newsession __ARGS((char *name,int type,int split));
void upload __ARGS((int unused,void *sp1,void *p));
extern char TooManySessions[];
  
/* In pc.c: */
void swapscreen __ARGS((struct session *old,struct session *new));
  
extern int16 Lport;
#define ALERT_EOF       1
  
  
#endif  /* _SESSION_H */
