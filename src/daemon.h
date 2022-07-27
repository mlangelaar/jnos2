#ifndef _DAEMON_H
#define _DAEMON_H
  
struct daemon {
    char *name;
    unsigned stksize;
    void (*fp) __ARGS((int,void *,void *));
};
#define NULLDAEMON ((struct daemon *)0)
extern struct daemon Daemons[];
  
#ifndef UNIX
/* In alloc.c: */
void gcollect __ARGS((int,void*,void*));
#endif
  
/* In main.c: */
void keyboard __ARGS((int,void*,void*));
#ifndef UNIX
void display __ARGS((int,void *,void *));
#endif
  
/* In kernel.c: */
void killer __ARGS((int,void*,void*));
  
/* In timer.c: */
void timerproc __ARGS((int,void*,void*));
  
#endif  /* _DAEMON_H */
  
