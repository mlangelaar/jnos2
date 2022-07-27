#ifndef _FTPSERV_H
#define _FTPSERV_H
  
#include <stdio.h>
  
#ifndef _SOCKADDR_H
#include "sockaddr.h"
#endif
  
#ifndef _TIMER_H
#include "timer.h"
#endif
  
#define CTLZ    26              /* EOF for CP/M systems */
  
extern char *Userfile;  /* List of user names and permissions */
  
struct ftpserv {
    int control;            /* Control socket */
    int data;               /* Data socket */
    char type;              /* Transfer type */
    int logbsize;           /* Logical byte size for logical type */
  
    FILE *fp;               /* File descriptor being transferred */
    struct sockaddr_in port;/* Remote port for data connection */
    char *username;         /* Arg to USER command */
    char *path;             /* Allowable path prefix */
    char *cd;               /* Current directory name */
    long ttim;              /* Challenge for encrypted password */
    long startpoint;        /* Offset for restarting a transfer */
    int  uselzw;            /* Socket wants LZW compression.   */
    int  lzwbits;
    int  lzwmode;
};
  
/* FTP commands (value based on position in commands[] in ftpserv.c */
#define USER_CMD        0
#define ACCT_CMD        1
#define PASS_CMD        2
#define TYPE_CMD        3
#define LIST_CMD        4
#define CWD_CMD         5
#define DELE_CMD        6
#define HELP_CMD        7
#define QUIT_CMD        8
#define RETR_CMD        9
#define STOR_CMD        10
#define PORT_CMD        11
#define NLST_CMD        12
#define PWD_CMD         13
#define XPWD_CMD        14
#define MKD_CMD         15
#define XMKD_CMD        16
#define XRMD_CMD        17
#define RMD_CMD         18
#define STRU_CMD        19
#define MODE_CMD        20
#define RSME_CMD        21
#define RPUT_CMD        22
#define NOOP_CMD        23
#define SYST_CMD        24
#define PASV_CMD        25
#define SIZE_CMD        26
#define APPE_CMD        27
#define LZW_CMD         28
#define CDUP_CMD	29
#define REST_CMD	30
#define RNFR_CMD        31
#define RNTO_CMD        32
#define MDTM_CMD        33
  
#endif  /* _FTPSERV_H */
