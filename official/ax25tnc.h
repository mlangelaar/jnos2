#ifndef NULLTNC
  
struct ax25tnc {
    struct proc *output;
    struct proc *upload;
    struct session *session;
};
#define NULLTNC ((struct ax25tnc *)0)
  
/* In ax25cmd.c: */
void ax_upload __ARGS((int unused,void *sp1,void *p));
  
#endif /* NULLTNC */
  
  
