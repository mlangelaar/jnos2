#ifndef NULLRLOGIN
  
#define LINESIZE    256 /* Length of local editing buffer */
  
/* Rlogin protocol control block */
struct rlog_cli {
    char    state;
#define RLOGIN_NONE_STATE      0
#define RLOGIN_PORT_STATE      RLOGIN_NONE_STATE+1
#define RLOGIN_OPEN_STATE      RLOGIN_PORT_STATE+1
    struct session *session;        /* Pointer to session structure */
    struct proc *input;     /* net -> console */
    struct proc *output;        /* console -> net */
};
#define        NULLRLOGIN        (struct rlogin_cli *)NIL
  
/* In rlogcli.c: */
void rlogin_input __ARGS((int unused,void *tn1,void *p));
void rlogin_upload __ARGS((int unused,void *sp1,void *p));
  
#endif  /* RLOGIN_PORT */
