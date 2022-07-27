#ifndef _TELNET_H
#define _TELNET_H
  
/* This seems to cause problems... WG7J */
#undef TN_KK6JQ	/* enable extended negotiation */
  
#ifndef _SESSION_H
#include "session.h"
#endif
  
#define LINESIZE    256 /* Length of local editing buffer */
  
/* Telnet command characters */
#define IAC     255 /* Interpret as command */
#define WILL        251
#define WONT        252
#define DO      253
#define DONT        254
#define SB      250
#define SE      240
#define TS_IS       0
#define TS_SEND     1
  
#define TN_LINEMODE 34  /* VE4WTS */
  
/* Telnet options */
#define TN_TRANSMIT_BINARY  0
#define TN_ECHO         1
#define TN_SUPPRESS_GA      3
#define TN_STATUS       5
#define TN_TIMING_MARK      6
#ifdef TN_KK6JQ
#define TN_REM_CON_TRANS_ECHO   7
#define TN_NEG_LINE_WIDTH   8
#define TN_NEG_OUT_PAGE_SIZE    9
#define TN_NEG_CR_DISPOSITION   10
#define TN_NEG_HORZ_TAB_STOPS   11
#define TN_NEG_HORZ_TAB_DISP    12
#define TN_NEG_FORMFEED_DISP    13
#define TN_NEG_VERT_TAB_STOPS   14
#define TN_NEG_VERT_TAB_DISP    15
#define TN_NEG_LINEFEED_DISP    16
#define TN_EXT_ASCII        17
#define TN_FORCE_LOGOUT     18
#define TN_BYTE_MACRO       19
#define TN_DATA_ENTRY_TERM  20
#define TN_PROT_SUPDUP      21
#define TN_OUT_SUPDUP       22
#define TN_SEND_LOCATION    23
#define TN_TERM_TYPE        24
#define TN_END_OF_RECORD    25
#define NOPTIONS        25  /* value of highest option supported */
#else
#define NOPTIONS        6   /* value of highest option supported */
#endif
  
/* Telnet protocol control block */
struct telnet {
    char local[NOPTIONS+1];   /* Local option settings */
    char remote[NOPTIONS+1];  /* Remote option settings */
    struct session *session;    /* Pointer to session structure */
    char eolmode;       /* Control translation of enter key */
};
#define NULLTN  (struct telnet *)0
  
extern int Refuse_echo;
extern int Tn_cr_mode;
  
/* In telnet.c: */
int tel_connect __ARGS((struct session *sp,char *fsocket,int len));
void tel_output __ARGS((int unused,void *p1,void *p2));
void tnrecv __ARGS((struct telnet *tn));
void doopt __ARGS((struct telnet *tn,int opt));
#ifdef TN_KK6JQ
void dosb __ARGS((struct telnet *tn,int opt));
#endif /* TN_KK6JQ */
void dontopt __ARGS((struct telnet *tn,int opt));
void willopt __ARGS((struct telnet *tn,int opt,int ignore_refuse_echo));
void wontopt __ARGS((struct telnet *tn,int opt));
void answer __ARGS((struct telnet *tn,int r1,int r2));
  
/* In ttylink.c: */
void ttylhandle __ARGS((int s,void *unused,void *p));
void ttylink_tcp __ARGS((int s,void *unused,void *p));

/* In RMSlink.c (renamed to tnlink.c) */
void tnlhandle (int, void *, void *);
void tnlink_tcp (int, void *, void *);

#endif  /* _TELNET_H */

