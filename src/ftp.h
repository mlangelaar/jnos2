#ifndef _FTP_H
#define _FTP_H
  
#ifndef _MAILBOX_H
#include "mailbox.h"
#endif
  
/* Definitions common to both FTP servers and clients */
#define BLKSIZE 4096    /* Chunk size for file I/O */
            /* Was too small for 9600 baud transfers - iw0cnb */
  
#define ASCII_TYPE  0
#define IMAGE_TYPE  1
#define LOGICAL_TYPE    2
  
/* In ftpsubr.c: */
long sendfile __ARGS((FILE *fp,int s,int mode,int hash,struct mbx *m));
long recvfile __ARGS((FILE *fp,int s,int mode,int hash,int32 timeoutms));
int isbinary __ARGS((FILE *fp));
long getsize __ARGS((FILE *fp));
unsigned long checksum __ARGS((FILE *fp, long n));
#endif  /* _FTP_H */
