#ifndef _DIRUTIL_H
#define _DIRUTIL_H
  
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
#ifdef UNIX

#include <time.h>
  
struct ffblk
{
    char ff_name[256];        /* current filename result */
    struct tm ff_ftime;       /* modify date/time */
    long ff_fsize;        /* file size */
    int ff_attrib;        /* DOS-style attributes (ugh) */
#ifdef GLOB_INTERNAL      /* ...only visible to internals... */
    char *ff_pfx;     /* prefix for found patterns */
    char *ff_pat;     /* pattern for the current level */
    DIR *ff_dir;      /* directory scan for the current level */
    struct dirent *ff_cur;    /* current path component */
    long ff_sattr;        /* selected attributes */
#else
    long youdontseethis[5];
#endif
};
  
#define FA_NORMAL 0   /* anything except the following: */
#define FA_RDONLY 1   /* read-only file */
#define FA_HIDDEN 2   /* dot-files */
#define FA_SYSTEM 4   /* device node or unreadable file */
#define FA_DIREC  16  /* directory */
  
extern int findfirst(char *pat, struct ffblk *ff, int attr);
extern int findnext(struct ffblk *ff);
extern void findlast __ARGS((struct ffblk *ff));
  
#else
#include <dir.h>
#endif
  
struct cur_dirs {
#ifndef UNIX
    int drv;
    char * curdir[27];
#endif
    char * dir;
} ;
  
/* In dirutil.c */
FILE *dir __ARGS((char *path,int full));
int filedir __ARGS((char *name,int times,char *ret_str));
int getdir __ARGS((char *path,int full,FILE *file));
void undosify __ARGS((char *s));
char * make_dir_path(int count,char *arg,char* curdir);
char * make_fname(char * curdir, char * fname);
char * init_dirs(struct cur_dirs * dirs);
void free_dirs(struct cur_dirs * dirs);
int dir_ok(char * path,struct cur_dirs * dirs);
int dircmd __ARGS((int argc,char *argv[],void *p));

/* Made public for nntp expiry */
int nextname __ARGS((int command, char *name, struct ffblk *sbuf));
char *wildcardize __ARGS((char *path));

#ifdef MSDOS
int dosfnchr __ARGS((int ch));
#endif
  
  
/* In pathname.c: */
char *pathname __ARGS((char *cd,char *path));
char *firstpath __ARGS((char *path));
  
/* In pc.c: */
char *Tmpnam __ARGS((char *name));
  
/* In bmutil.c */
long fsize __ARGS((char *name));
  
#endif /* _DIRUTIL_H */
  
