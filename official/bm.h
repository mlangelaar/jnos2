 /* Mods by G1EMM */
#ifndef _BM_H
#define _BM_H
  
#include <stdio.h>
#ifndef _MAILBOX_H
#include "mailbox.h"
#endif
#ifndef _SMTP_H
#include "smtp.h"
#endif
#ifndef _MAILUTIL_H
#include "mailutil.h"
#endif
  
/* bm.h -- definitions for bmutil.c that aren't included elsewhere */
  
/* number of columns and lines on a standard display, e.g. vt100 */
#define MAXCOL      80
#define MAXLIN      24
  
/* message status */
#define BM_DELETE   1
#define BM_READ     2
#define BM_FORWARDED    4
#define BM_HOLD     8
  
#define SLINELEN    64
#define LINELEN     256
  
extern char *Hdrs[];

/* In bmutil.c: */  
void getlastread __ARGS((struct mbx *m));
void setlastread __ARGS((struct mbx *m));
void scanmail __ARGS((struct mbx *m));
int msgtofile __ARGS((struct mbx *m));
int dolistnotes __ARGS((int argc,char *argv[],void *p));
int dodelmsg __ARGS((int argc,char *argv[],void *p));
int doreadmsg __ARGS((int argc,char *argv[],void *p));
int doreadnext __ARGS((int argc,char *argv[],void *p));
int mbx_reply __ARGS((int argc,char *argv[],struct mbx *m,struct list **cclist,char **rhdr));
int closenotes __ARGS((struct mbx *m));
long isnewprivmail __ARGS((struct mbx *m));
extern int tkeywait __ARGS((char *prompt,int flush,int linemode));
extern int mykeywait __ARGS((char *prompt,struct mbx *m));
extern void RemoveMailLocks __ARGS((void));

/* in index.c */
char *getaddress __ARGS((char *string,int cont));
int htype __ARGS((char *s, int *prevtype));
int isarea __ARGS((char *name));

/* in smtpserv.c */
int isheldarea __ARGS((char *name));

#endif  /* _BM_H */
