/* Mods by G1EMM and PA0GRI */
#ifndef _FTPCLI_H
#define _FTPCLI_H
  
#include <stdio.h>
  
#ifndef _FTP_H
#include "ftp.h"
#endif
  
#ifndef _SESSION_H
#include "session.h"
#endif
  
#ifndef _DIRUTIL_H
#include "dirutil.h"
#endif
  
#define LINELEN 256             /* Length of user command buffer */
  
#define CTLZ    26              /* EOF for CP/M systems */
  
/* Per-session FTP client control block */
struct ftpcli {
    int control;            /* Control socket */
    int data;               /* Data socket */
  
    char state;
#define COMMAND_STATE   0       /* Awaiting user command */
#define SENDING_STATE   1       /* Sending data to user */
#define RECEIVING_STATE 2       /* Storing data from user */
  
    int16 verbose;          /* Transfer verbosity level */
#define V_QUIET         0       /* Error messages only */
#define V_SHORT         1       /* Final message only */
#define V_NORMAL        2       /* display control messages */
#define V_HASH          3       /* control messages, hash marks */
#define V_BYTE          4       /* control messages, byte count */
  
    int batch;              /* Command batching flag */
    int abort;              /* Aborted transfer flag */
    int uselzw;             /* Attempt LZW compression flag */
    char type;              /* Transfer type */
    char typesent;          /* Last type command sent to server */
    int logbsize;           /* Logical byte size for logical type */
#ifdef FTP_REGET
    long startpoint;        /* Offset for restarting a transfer */
#endif
    FILE *fp;               /* File descriptor being transferred */
    char *line;             /* Response from the server */
  
    struct session *session;
    char *password;
    struct cur_dirs *curdirs;
};
#define NULLFTP (struct ftpcli *)0
  
#endif  /* _FTPCLI_H */
