#ifndef _MAILCLI_H
#define _MAILCLI_H
/* Mail Command Line Interface -- Clients
 * Copyright 1992 William Allen Simpson
 */
  
#ifndef _DAEMON_H
#include "daemon.h"
#endif
  
#ifndef _TIMER_H
#include "timer.h"
#endif
  
struct mailservers {
    struct mailservers *next;
    struct daemon *reader;
    struct timer timer;
    int lowtime, hightime;  /* for connect window */
    char *hostname;
    char *username;
    char *password;
    char *mailbox;
};
#define NULLMAIL    (struct mailservers *)NULL
  
extern struct daemon Mailreaders[];
extern struct mailservers *Mailservers;
extern unsigned short Mailtrace;
extern int Mailquiet;
  
int domsread __ARGS((int argc,char *argv[],void *p));
  
int mailresponse __ARGS((int s, char *buf, char *comment));
int mailbusy __ARGS((struct mailservers *np));
  
/* in pop2cli.c */
void pop2_job __ARGS((int unused,void *v1,void *p2));
  
/* in pop3cli.c */
void pop3_job __ARGS((int unused,void *v1,void *p2));
  
#endif /* _MAILCLI_H */
