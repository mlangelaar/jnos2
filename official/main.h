#ifndef _MAIN_H
#define _MAIN_H
  
#ifndef _PROC_H
#include "proc.h"
#endif
  
extern char Badhost[];
extern char *Hostname;
extern char Nospace[];          /* Generic malloc fail message */
extern char Version2[];
  
extern struct proc *Cmdpp;
#ifdef MSDOS
extern struct proc *Display;
#endif
extern int main_exit;           /* from main program (flag) */
  
#endif
