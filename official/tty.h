#ifndef _TTY_H
#define _TTY_H
  
#ifndef _MBUF_H
#include "mbuf.h"
#endif
  
#ifndef _SESSION_H
#include "session.h"
#endif
  
/* Command recall ('history') structure - WG7J */
struct hist {
    struct hist *next;
    struct hist *prev;
    char *cmd;
};
extern struct hist *Histry;
  
/* Arrow keys */
#define UPARROW 256
#define DNARROW 257
#define RTARROW 258
#define LTARROW 259
  
/* In ttydriv.c: */
struct mbuf *ttydriv __ARGS((struct session *sp,int c));
  
#endif /* _TTY_H */
