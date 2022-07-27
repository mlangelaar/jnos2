#ifndef _SMTP_H
#define _SMTP_H
  
#define SMTPTRACE           /* enable tracing for smtp */
#define MAXSESSIONS 10      /* most connections allowed */
#define JOBNAME     13      /* max size of a job name with null */
#define LINELEN     256
#define PLINELEN    256
#define RLINELEN    512
#define TLINELEN    1024
#define SLINELEN    64
#define MBOXLEN     8       /* max size of a mail box name */
  
/* types of address used by smtp in an address list */
#define BADADDR     0
#define LOCAL       1
#define DOMAIN      2
#define NNTP_GATE   3
  
/* a list entry */
struct list {
    struct list *next;
    char *val;
    char *aux;
    char type;
};
  
/* Per-session control block  used by smtp server */
struct smtpsv {
    int s;          /* the socket for this connection */
    char *system;       /* Name of remote system */
    char *from;     /* sender address */
    struct list *to;    /* Linked list of recipients */
    char *bid;      /* BID if any */
    int dupbid;     /* indicates a duplicate bid */
    FILE *data;     /* Temporary input file pointer */
};
  
/* used by smtpcli as a queue entry for a single message */
struct smtp_job {
    struct  smtp_job *next; /* pointer to next mail job for this system */
    char    jobname[9]; /* the prefix of the job file name */
    long    len;        /* length of file */
    char    *from;      /* address of sender */
    struct list *to;    /* Linked list of recipients */
};
  
/* control structure used by an smtp client session */
struct smtpcli {
    int     s;      /* connection socket */
    int32   ipdest;     /* address of forwarding system */
    char    *destname;  /* domain address of forwarding system */
    char    *wname;     /* name of workfile */
    char    *tname;     /* name of data file */
    char    buf[LINELEN];   /* Output buffer */
    char    cnt;        /* Length of input buffer */
    FILE    *tfile;
    struct  smtp_job *jobq;
    struct  list    *errlog;
    int lock;       /* In use */
#ifdef J2_MULTI_SMTP_SESSION
    char    *prefix;
#endif
};
  
/* smtp server routing mode */
#define QUEUE   1
  
#define NULLLIST    (struct list *)0
#define NULLSMTPSV  (struct smtpsv *)0
#define NULLSMTPCLI (struct smtpcli *)0
#define NULLJOB     (struct smtp_job *)0
  
extern int Smtpmode;
extern char *Mailspool;
extern char *Maillog;
extern char *Mailqdir;      /* Outgoing spool directory */
extern char *Routeqdir; /* spool directory for a router program */
extern char *Mailqueue; /* Prototype of work file */
extern char *Maillock;      /* Mail system lock */
extern char *Alias;     /* File of local aliases */
  
/* In smtpserv.c: */
char *ptime __ARGS((long *t));
long get_msgid __ARGS((void));
char *getname __ARGS((char *cp));
int validate_address __ARGS((char *s));
int queuejob __ARGS((FILE *dfile,char *host,struct list *to,char *from));
struct list *addlist __ARGS((struct list **head,char *val,int type,char *aux));
int mdaemon __ARGS((FILE *data,char *to,struct list *lp,int bounce));
  
/* In smtpcli.c: */
void smtptick __ARGS((void *t));
int mlock __ARGS((char *dir,char *id));
int rmlock __ARGS((char *dir,char *id));
void del_list __ARGS((struct list *lp));
int32 mailroute __ARGS((char *dest));
extern char *Months[];
  
#endif  /* _SMTP_H */
  
