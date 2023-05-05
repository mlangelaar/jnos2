#ifndef _INDEX_H
#define _INDEX_H
/* Public routines for Mail File Indexing.
 * (c) 1993 Johan. K. Reinalda, WG7J
 */
  
#ifdef MSDOS
#include <io.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
  
#define INDEXVERSION 1
  
#define AXBUF 10
  
/* Mail index structure - WG7J */
  
struct indexhdr {
    int version;    /* Version number */
    int msgs;       /* Number of messages in mailbox */
    int unread;     /* Number of unread msgs; only valid for private mailbox */
};
  
struct cclist {
    struct cclist *next;
    char *to;
};
  
struct fwdbbs {
    struct fwdbbs *next;
    char call[AXBUF];
};
  
struct mailindex {
    long msgid;                 /* Internal message number */
    char type;                  /* Message type */
    char status;                /* Message status */
    long size;                  /* Size of message */
    char *to;                   /* To: field */
    char *from;                 /* From: field */
    char *subject;              /* Subject: field */
    char *replyto;              /* Reply-to: field */
    char *messageid;            /* Message-Id: field */
    time_t mydate;              /* Received date field */
    time_t date;                /* Date: field */
    struct cclist *cclist;      /* List of Cc: addressees */
    struct fwdbbs *bbslist;     /* List of bbs calls forwarded to */
};
  
#define READBINARY O_RDONLY+O_BINARY
#define WRITEBINARY O_WRONLY+O_BINARY
#define READWRITEBINARY O_RDWR+O_BINARY
#define CREATEBINARY O_CREAT+READWRITEBINARY
#define CREATETRUNCATEBINARY O_CREAT+O_TRUNC+READWRITEBINARY
#define CREATEMODE S_IREAD+S_IWRITE
  
#define NOMBX -2
#define NOIND -3
#define ERROR -10
#define NOERROR 0
  
void UpdateIndex(char *path,int force);
int SyncIndex(char *name);
int IndexFile(char *name,int verbose);
int MsgsInMbx(char *name);
void default_index(char *name, struct mailindex *ind);
void default_header(struct indexhdr *hdr);
int get_index(int msg,char *name,struct mailindex *ind);
void set_index(char *buf,struct mailindex *index, int hdrtype);
int write_index(char *name, struct mailindex *ind);
int WriteIndex(int idx,struct mailindex *ind);
int write_header(int idx,struct indexhdr *hdr);
int read_header(int idx,struct indexhdr *hdr);
int read_index(int idx,struct mailindex *ind);
void print_index(struct mailindex *ind);
void delete_index(char *filename);
long mydate(char *s);
void dotformat(char *area);
void dirformat(char *area);
void firsttoken(char *line);
char *bgets(char * buf,int size,FILE *fp);
  
#endif /* _INDEX_H */
