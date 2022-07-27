#ifndef _NNTP_H
#define _NNTP_H
  
#ifdef MSDOS
#include <dos.h>
#endif
  
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
#ifndef _SMTP_H
#include "smtp.h"
#endif
  
struct nntpsv {
    int s;
    int ret;
    int debug;
    int slave;
    unsigned int first;
    unsigned int last;
    unsigned int pointer;
    unsigned int hold_i;
    char buf[512];
    char history[512];
    char *newnews;
    char *path;
    char *fname;
    char *id;
    FILE *ihave;
    int32 dest;
    int32 unixtime;
    long ftime;
    struct date *datest;
    struct time *timest;
    struct article *ap;
};
#define NULLNNTPSV (struct nntpsv *)0
  
struct article {
    char *group;
    char *id;
    char *path;
    unsigned int number;
    unsigned int tmpu;
    struct article *next;
};
#define NULLARTICLE (struct article *)0
  
struct post {
    char *user;
    char *reply;
    char *sig;
    char *organ;
    char *fullname;
};
#define NULLPOST (struct post *)0
  
struct groups {
    struct article *a;
    struct article *next;
};
#define NULLGROUP (struct groups *)0
  
struct head {
    char *from;
    char *reply_to;
    char *subject;
    char *id;
};
#define NULLHEAD (struct head *)0
  
struct g_list {
    char *str;
    struct g_list *next;
};
#define NULLG (struct g_list *)0
  
struct search {
    struct g_list *not;
    struct g_list *all;
    struct g_list *group;
};
#define NULLSEARCH (struct search *)0
  
struct DFREE {
    unsigned char drive;
    unsigned long bytes;
    struct DFREE *next;
};
#define NULLDRV (struct DFREE *)0
  
struct Servers {
    struct timer nntpt;
    char *name;
    int32 dest;
    char *newsgroups;       /* list of newsgroups */
    int lowtime;            /* for connect window */
    int hightime;
    struct Servers *next;
};
#define NULLSERVER (struct Servers *)0
  
#define LineLen 512
  
int nntp1   __ARGS((int argc, char *argv[], void *p));
int nntp0   __ARGS((int argc, char *argv[], void *p));
int donntp  __ARGS((int argc, char *argv[], void *p));
int nnGpost __ARGS((FILE *data,char *from,struct list *le));
  
#endif /* _NNTP_H */
