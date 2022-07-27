/* Convert relative to absolute pathnames
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "dirutil.h"
#include "config.h"
#include "index.h"
#ifdef CALLSERVER
#include <string.h>
extern char *CDROM; /* buckbook.c: defines CDROM drive letter e.g. "s:"  */
#endif
  
void crunch __ARGS((char *buf,char *path));
#ifdef MSDOS
static char* getroot __ARGS((char *buf,char *path));
#endif
  
/* Given a working directory and an arbitrary pathname, resolve them into
 * an absolute pathname. Memory is allocated for the result, which
 * the caller must free
 */
char *
pathname(cd,path)
char *cd;       /* Current working directory */
char *path;     /* Pathname argument */
{
    char *buf;
#ifdef  MSDOS
    char *cp,c;
    int tflag = 0;
#endif
#ifdef CALLSERVER
    int drive = 0;  /* is there a drive spec to preserve? */
#endif
  
    if(cd == NULLCHAR || path == NULLCHAR)
        return NULLCHAR;
  
    /* Strip any leading white space on args */
    cd = skipwhite(cd);
    path = skipwhite(path);
  
    if(*path == '\0')
        return j2strdup(cd);
  
#ifdef  MSDOS
    /* If path has any backslashes, make a local copy with them
     * translated into forward slashes
     */
    if(strchr(path,'\\') != NULLCHAR){
        tflag = 1;
        cp = path = j2strdup(path);
        while(*cp) {
            if(*cp == '\\')
                *cp = '/';
            cp++;
        }
        cp = path;
    }
#endif
  
#ifdef CALLSERVER
    if (CDROM != NULLCHAR) {
        if(!strncmp(path, CDROM, 2))  {
            buf = j2strdup(path);
#ifdef MSDOS
            if (tflag) free(path);
#endif
            return buf;
        }
        if(!strncmp(cd, CDROM, 2))
            drive = 1;  /* make a note  */
    }
#endif
  
    /* Allocate and initialize output buffer; user must free */
    buf = mallocw((unsigned)strlen(cd) + strlen(path) + 10);        /* fudge factor */
    buf[0] = '\0';
  
    /* Interpret path relative to cd only if it doesn't begin with "/"
     * or doesn't have a drive letter indicator */
    if((path[0] != '/') || (strlen(path) > 1 && path[1] != ':'))
        crunch(buf,cd);
  
#ifdef CALLSERVER
    else
        drive = 0;
#endif
  
    crunch(buf,path);
    /* Special case: null final path means the root directory */
    if(buf[0] == '\0'){
        buf[0] = '/';
        buf[1] = '\0';
    }
#ifdef  MSDOS
    else if(buf[1] == ':' && buf[2] == '\0'){
        buf[2] = '/';
        buf[3] = '\0';
    }
    if(tflag)
        free(path);
#endif
#ifdef CALLSERVER
    if (drive)  {
        cp = buf;
        while(*cp != '\0')
            *cp = *(cp++ + 1);
    }
#endif
    return buf;
}
  
/* Process a path name string, starting with and adding to
 * the existing buffer
 */
void
crunch(buf,path)
char *buf;
char *path;
{
    char *cp;
#ifdef MSDOS
    path=getroot(buf,path); /* for a case drive is specified */
    if(buf[0] != '\0' && buf[1] == ':')
        buf+=2; /* don't allow drive to be removed by '..' */
#endif
  
    cp = buf + strlen(buf); /* Start write at end of current buffer */
  
    /* Now start crunching the pathname argument */
    for(;;){
        /* Strip leading /'s; one will be written later */
        while(*path == '/')
            path++;
        if(*path == '\0')
            break;          /* no more, all done */
        /* Look for parent directory references, either at the end
         * of the path or imbedded in it
         */
        if(strcmp(path,"..") == 0 || strncmp(path,"../",3) == 0){
            /* Hop up a level */
            if((cp = strrchr(buf,'/')) == NULLCHAR)
                cp = buf;       /* Don't back up beyond root */
            *cp = '\0';             /* In case there's another .. */
            path += 2;              /* Skip ".." */
            while(*path == '/')     /* Skip one or more slashes */
                path++;
        /* Look for current directory references, either at the end
         * of the path or imbedded in it
         */
        } else if(strcmp(path,".") == 0 || strncmp(path,"./",2) == 0){
            /* "no op" */
            path++;                 /* Skip "." */
            while(*path == '/')     /* Skip one or more slashes */
                path++;
        } else {
            /* Ordinary name, copy up to next '/' or end of path */
            *cp++ = '/';
            while(*path != '/' && *path != '\0')
                *cp++ = *path++;
        }
    }
    *cp++ = '\0';
}
  
/* Get the first path out of the path from ftpusers, and
 * allocate a new string for it. Caller should free this!
 * Selcuk+N5KNX: A tab precedes the desired "first" or "home" dir,
 * if an '=' char was used in the ftpusers file (see userlogin()).
 */
char *firstpath(char *path) {
    char *cp,*p;
    int len;
  
    if((p = strchr(path,'\t')) == NULLCHAR) p = path;
    else p = skipwhite(p);

    len = strcspn(p,"; \t");        /* Find length */
    cp = mallocw(len+1);            /* and copy it into new buffer */
    memcpy(cp,p,len);
    *(cp+len) = '\0';
    return cp;
}
  
#ifdef  MSDOS
/* If path specifies drive (e.g. A:), set buf to root on the drive
 * and return path without drive specification, otherwise return path
 * in any case remove leading / from path and set root if any found -jt
 */
static char*
getroot(buf,path)
char *buf,*path;
{
    int root=0;
  
    if(path[0] != '\0' && path[1] == ':') {
        *buf++=*path++;
        *buf++=*path++;
        root=1;
        /* Question here: what to do if drive specified
         * is same as default ? Go to root directory or remain in
         * current ? Currently goes to the root.
         */
    }
    /* Skip all leading '/' */
    while(*path == '/') {
        path++;
        root=1;
    }
    if(root) {
        if(buf[0] != '\0' && buf[1] == ':')
            buf+=2; /* leave drive if set earlier */
        buf[0]='\0';
    }
    return path;
}
#endif /* MSDOS */
  
