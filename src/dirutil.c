/* dirutil.c - MS-DOS directory reading routines
 *
 * Bdale Garbee, N3EUA, Dave Trulli, NN2Z, and Phil Karn, KA9Q
 * Directory sorting by Mike Chepponis, K3MC
 * New version using regs.h by Russell Nelson.
 * Rewritten for Turbo-C 2.0 routines by Phil Karn, KA9Q 25 March 89
 *
 * Added path filter functions and applied to dodir
 * also used by ftpcli.c, added current directory
 * storage capability (11/92 WA3DSP)
 * Bugfixes in the above by WG7J, WA7TAS
 * Added Unix-style dir display compile-time option, from WA3DSP's 1.08dff.
 */
  
#ifndef UNIX
#include <dir.h>
#include <dos.h>
#else
#include <sys/stat.h>
/* There are at least FOUR variants of statfs()... we handle SCO and Linux */
#ifdef M_UNIX
#include <sys/statfs.h>
#define f_bavail f_bfree
#elif defined(sun)
#include <sys/statvfs.h>
#define statfs statvfs
#define f_bsize f_frsize
#else
#include <sys/vfs.h>
#endif /* M_UNIX */
#endif /* UNIX */
#include <ctype.h>
#ifndef UNIX
#include <io.h>
#endif
#include "global.h"
#include "proc.h"
#include "session.h"
#include "dirutil.h"
#include "commands.h"
  
#ifdef CALLSERVER
/*#include <string.h>*/
/*#include <alloc.h>*/
extern char *CDROM; /* buckbook.c: defines CDROM drive letter e.g. "s:"  */
#endif
  
struct dirsort {
    struct dirsort *next;
    struct ffblk de;
};
#define NULLSORT (struct dirsort *)0
  
static int fncmp __ARGS((char *a, char *b));
static void format_fname_full __ARGS((FILE *file,struct ffblk *sbuf,int full,
int n));
static void free_clist __ARGS((struct dirsort *this));
  
#ifdef  notdef
static int getdir_nosort __ARGS((char *path,int full,FILE *file));
#endif
#ifndef UNIX_DIR_LIST
static void commas __ARGS((char *dest));
static void print_free_space __ARGS((char *path,FILE *file,int n));
#endif
  
extern void crunch __ARGS((char *buf,char *path));
  
#ifdef UNIX
#define REGFILE (FA_HIDDEN|FA_SYSTEM|FA_DIREC)
#else
#define REGFILE (FA_DIREC)
#endif
  
#define insert_ptr(list,new)    (new->next = list,list = new)
  
  
/* Create a directory listing in a temp file and return the resulting file
 * descriptor. If full == 1, give a full listing; else return just a list
 * of names.
 */
FILE *
dir(path,full)
char *path;
int full;
{
    FILE *fp;
  
    if((fp = tmpfile()) != NULLFILE){
        getdir(path,full,fp);
        rewind(fp);
    }
    return fp;
}
  
/* find the first or next file and lowercase it. */
int
nextname(command, name, sbuf)
int command;
char *name;
struct ffblk *sbuf;
{
    int found;
  
    switch(command){
        case 0:
            found = findfirst(name,sbuf,REGFILE);
            break;
        default:
            found = findnext(sbuf);
    }
    found = found == 0;
#ifndef UNIX
    if(found)
        strlwr(sbuf->ff_name);
#endif
  
    return found;
}
  
/* wildcard filename lookup */
int
filedir(name,times,ret_str)
char *name;
int times;
char *ret_str;
{
    static struct ffblk sbuf;
    int rval;
  
    switch(times){
        case 0:
            rval = findfirst(name,&sbuf,REGFILE);
            break;
        default:
            rval = findnext(&sbuf);
            break;
    }
    if(rval == -1){
        ret_str[0] = '\0';
    } else {
        /* Copy result to output */
        strcpy(ret_str, sbuf.ff_name);
    }
    return rval;
}
/* do a directory list to the stream
 * full = 0 -> short form, 1 is long
*/
int
getdir(path,full,file)
char *path;
int full;
FILE *file;
{
    struct ffblk *sbuf;
    int command = 0;
    int n = 0;
#ifdef UNIX_DIR_LIST
    unsigned long kb=0L;
#endif
    struct dirsort *head, *here, *new;
  
    sbuf = mallocw(sizeof *sbuf);
    path = wildcardize(path);
  
    head = NULLSORT;        /* No head of chain yet... */
    for(;;){
        if (!nextname(command, path, sbuf))
            break;
        command = 1;    /* Got first one already... */
        if (sbuf->ff_name[0] == '.')     /* drop "." and ".." */
#ifdef UNIX
            if (sbuf->ff_name[1] == '\0' || (sbuf->ff_name[1] == '.' &&
                sbuf->ff_name[2] == '\0'))
#endif
                continue;
  
        new = (struct dirsort *) mallocw(sizeof(struct dirsort));
        memcpy(&new->de, sbuf, sizeof *sbuf); /* Copy contents of directory entry struct */
  
        /* insert it into the list */
        if (!head || fncmp(new->de.ff_name, head->de.ff_name) < 0) {
            insert_ptr(head, new);
        } else {
            register struct dirsort *this;
            for (this = head;
                this->next != NULLSORT;
                this = this->next)
                if (fncmp(new->de.ff_name, this->next->de.ff_name) < 0)
                    break;
            insert_ptr(this->next, new);
        }
    } /* infinite FOR loop */
  
#ifdef UNIX_DIR_LIST
    if (full) {
        for (here = head; here; here = here->next)
            kb += (here->de.ff_fsize + 1023)/1024;

        fprintf(file, "total %lu\n", kb);
    }
#endif

    for (here = head; here; here = here->next)
        format_fname_full(file,&here->de,full,++n);
  
    /* Give back all the memory we temporarily needed... */
    free_clist(head);
    free(sbuf);
  
#ifndef UNIX_DIR_LIST
    if(full)
        print_free_space(path, file, n);
#endif
  
    return 0;
}
  
static int
fncmp(a,b)
register char *a, *b;
{
    int i;
  
    for(;;){
        if (*a == '.')
            return -1;
        if (*b == '.')
            return 1;
        if ((i = *a - *b++) != 0)
            return i;
        if (!*a++)
            return -1;
    }
}
  
  
#if defined DIRSESSION || defined FTPSESSION
  
/* List directory to console */
int
dircmd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char *path;
    FILE *fp;
    char **margv;
    char tmpname[80];
  
    path=j2strdup(make_dir_path(argc,argv[1],Command->curdirs->dir));
    margv = (char **)callocw(2,sizeof(char *));
    j2tmpnam(tmpname);
    fp = fopen(tmpname,WRITE_TEXT);
    getdir(path,1,fp);
    free(path);
    fclose(fp);
    margv[1] = j2strdup(tmpname);
    morecmd(2,margv,p);
    free(margv[1]);
    free(margv);
    unlink(tmpname);
    return 0;
}
#endif  /* DIRSESSION || FTPSESSION */
  
#ifdef DIRSESSION
  
int
dodir(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char **pargv;
    int i;
  
    if(Curproc->input == Command->input) {
        /* Make private copy of argv and args,
         * spawn off subprocess and return.
         */
        pargv = (char **)callocw((unsigned)argc,sizeof(char *));
        for(i=0;i<argc;i++)
            pargv[i] = j2strdup(argv[i]);
#ifdef UNIX
#define DIRSTK 1024
#else
#define DIRSTK 512
#endif
        newproc("dir",DIRSTK,(void (*)__ARGS((int,void*,void*)))dircmd,argc,(void *)pargv,p,1);
    } else
        dircmd(argc,argv,p);
    return 0;
}
  
#endif /* DIRSESSION */
  
/* fix up the filename so that it contains the proper wildcard set */
char *
wildcardize(path)
char *path;
{
#ifdef UNIX
    static
#endif
    struct ffblk sbuf;
#ifdef UNIX
    static char ourpath[1024];
#else
    static char ourpath[64];
#endif
  
    /* Root directory is a special case */
    if(path == NULLCHAR ||
        *path == '\0' ||
        strcmp(path,"\\") == 0 ||
    strcmp(path,"/") == 0) {
#ifdef UNIX
        strcpy(ourpath,"/*");
#else
        strcpy(ourpath,"/*.*");
#endif
        return ourpath;
    }
  
#ifdef MSDOS
    /* MSDOS Root directory can also have a drive letter prefix */
    if(isalpha(*path) &&
        (strcmp(path+1,":\\") == 0 || strcmp(path+1,":/") == 0) ) {
            sprintf(ourpath,"%c:/*.*", *path);
            return ourpath;
    }
#endif
  
#ifdef CALLSERVER
    if  (CDROM != NULLCHAR && strcmp(path, CDROM) == 0)  {
#ifdef UNIX
        sprintf(ourpath, "%s/*", CDROM);
#else
        sprintf(ourpath, "%s/*.*", CDROM);
#endif
        return ourpath;
    }
#endif
  
    /* if they gave the name of a subdirectory, append \*.* to it */
    if (nextname(0, path, &sbuf) &&
        (sbuf.ff_attrib & FA_DIREC) &&
    !nextname(1, path, &sbuf)) {
  
        /* if there isn't enough room, give up -- it's invalid anyway */
#ifdef UNIX
        if (strlen(path) + 2 > sizeof(ourpath) - 1) return path;
#else
        if (strlen(path) + 4 > sizeof(ourpath) - 1) return path;
#endif
        strcpy(ourpath, path);
#ifdef UNIX
        strcat(ourpath, "/*");
#else
        strcat(ourpath, "/*.*");
#endif
        return ourpath;
    }
#ifdef UNIX
    findlast(&sbuf);
#endif
  
    return path;
}
  
#ifdef UNIX_DIR_LIST
static void
format_fname_full(file, sbuf, full, n)
FILE *file;
struct ffblk *sbuf;
int full, n;
{
	struct date curdate;
	char line_buf[30];              /* for long dirlist */
	char cbuf[20];                  /* for making line_buf */

    if (full) {
	getdate(&curdate);

	if(sbuf->ff_attrib & FA_DIREC) 
		 strcpy(line_buf, "d");
	else
		 strcpy(line_buf, "-");
	if(sbuf->ff_attrib & FA_RDONLY)
		strcat(line_buf,"r-xr-xr-x");
	else
		strcat(line_buf,"rwxrwxrwx");
	strcat(line_buf,"  1  owner group");
	sprintf(cbuf,"%ld",sbuf->ff_fsize);
	/* commas(cbuf); */
	fprintf(file,"%s%10s",line_buf,cbuf);
#ifdef UNIX
	if (curdate.da_year==((sbuf->ff_ftime.tm_year+1900))) {
		fprintf(file," %s %2d %2d:%02d ",
		  Months[sbuf->ff_ftime.tm_mon],     /* month */
		  sbuf->ff_ftime.tm_mday,            /* day */
		  sbuf->ff_ftime.tm_hour,            /* hour */
		  sbuf->ff_ftime.tm_min);            /* minute */
	 } else {
		fprintf(file," %s %2d  %4d ",
		  Months[sbuf->ff_ftime.tm_mon],  /* month */
		  sbuf->ff_ftime.tm_mday,         /* day */
		  sbuf->ff_ftime.tm_year+1900);   /* year */
	}
#else
	if (curdate.da_year==((sbuf->ff_fdate>>9)+1980)) {
		fprintf(file," %s %2d %2d:%02d ",
		  Months[((sbuf->ff_fdate >> 5) & 0xf)-1],  /* month */
		  (sbuf->ff_fdate ) & 0x1f,                 /* day */
		  (sbuf->ff_ftime >> 11) & 0x1f,            /* hour */
		  (sbuf->ff_ftime >> 5) & 0x3f);            /* minute */
	 } else {
		fprintf(file," %s %2d  %4d ",
		  Months[((sbuf->ff_fdate >> 5) & 0xf)-1],  /* month */
		  (sbuf->ff_fdate ) & 0x1f,                 /* day */
		  (sbuf->ff_fdate >> 9) + 1980);            /* year */
	}
#endif
    }
    fprintf(file,"%s\n",sbuf->ff_name);
}
#else
/*
 * Return a string with commas every 3 positions.
 * the original string is replace with the string with commas.
 *
 * The caller must be sure that there is enough room for the resultant
 * string.
 *
 *
 * k3mc 4 Dec 87
 */
static void
commas(dest)
char *dest;
{
    char *src, *core;       /* Place holder for malloc */
    unsigned cc;            /* The comma counter */
    unsigned len;
  
    len = strlen(dest);
    /* Make a copy, so we can muck around */
    core = src = j2strdup(dest);
  
    cc = (len-1)%3 + 1;     /* Tells us when to insert a comma */
  
    while(*src != '\0'){
        *dest++ = *src++;
        if( ((--cc) == 0) && *src ){
            *dest++ = ','; cc = 3;
        }
    }
    free(core);
    *dest = '\0';
}
  
static void
format_fname_full(file, sbuf, full, n)
FILE *file;
struct ffblk *sbuf;
int full, n;
{
    char cbuf[30];                  /* for size field, with commas */
    int len;
  
    fputs(sbuf->ff_name,file);
    len=strlen(sbuf->ff_name);
    if(sbuf->ff_attrib & FA_DIREC) fputc('/',file),len++;
    if (full) {        /* Long form, give other info too */
        while(len<13) {
            fputc(' ',file);
            len++;
        }

        if(sbuf->ff_attrib & FA_DIREC)
            fputs("           ",file); /* 11 spaces */
        else {
            sprintf(cbuf,"%ld",sbuf->ff_fsize);
            commas(cbuf);
            fprintf(file,"%10s ",cbuf);
        }
        fprintf(file,"%2d:%02d %2d/%02d/%02d%s",
#ifdef UNIX
        sbuf->ff_ftime.tm_hour, sbuf->ff_ftime.tm_min,
        sbuf->ff_ftime.tm_mon + 1, sbuf->ff_ftime.tm_mday,
        sbuf->ff_ftime.tm_year%100,
#else
        (sbuf->ff_ftime >> 11) & 0x1f,        /* hour */
        (sbuf->ff_ftime >> 5) & 0x3f, /* minute */
        (sbuf->ff_fdate >> 5) & 0xf,  /* month */
        (sbuf->ff_fdate ) & 0x1f,             /* day */
        ((sbuf->ff_fdate >> 9) + 80)%100,   /* year */
#endif
        (n & 1) ? "   " : "\n");
    } else {
        fputc('\n',file);
    }
}
#endif

#ifndef UNIX_DIR_LIST
/* Provide additional information only on DIR */
static void
print_free_space(path, file, n)
char *path;
FILE *file;
int n;
{
    unsigned long free_bytes, total_bytes;
    char s_free[25], s_total[25]; /* Changed to accomodate big disks - WA7TAS */
    char cbuf[20];
#ifdef UNIX
    char *pbuf;
    struct statfs vfsb;
    char *cp;
  
    pbuf = mallocw(1024);
    strcpy(pbuf, path);
    if ((cp = strrchr(pbuf, '/')) == 0)
        strcat(pbuf, "/.");
    else
    {
        *++cp = '.';
        *++cp = '\0';
    }
#ifdef M_UNIX
    statfs(pbuf, &vfsb, sizeof vfsb, 0);
#else
    statfs(pbuf, &vfsb);
#endif
    free_bytes = vfsb.f_bsize * vfsb.f_bavail;
    total_bytes = vfsb.f_bsize * vfsb.f_blocks;
#else /* UNIX */
    struct dfree dtable;
    unsigned long bpcl;
    char resolved[80];      /* may need as little as 67 */
    union REGS regs;
    struct SREGS sregs;
    int drive;
    char drivex[3];
  
    if(_osmajor>=3) {
        /* do an undocumented call to find which drive this name resolves to */
        /*  this call does *NOT* work in dos <3.0 */
        /* Note: this can yield a path like: \\machine\path  , EVEN FOR A CDROM,
           in which case we use the dos 2.0 method ... N5KNX */
        regs.x.si = FP_OFF(path);
        sregs.ds = FP_SEG(path);
        regs.x.di = FP_OFF(resolved);
        sregs.es = FP_SEG(resolved);
        regs.h.ah = 0x60;
        intdosx(&regs,&regs,&sregs);
        if (regs.x.cflag || (resolved[1] != ':'))
            drive = -1;
        else drive = resolved[0] - '@';
    }
    if (_osmajor < 3 || drive == -1) {
        /* use a method that works for dos < 3.0 */
        drivex[0]=0;
        for(drive=0;drive<strlen(path);drive++)
            path[drive]=toupper(path[drive]);
        fnsplit(path,drivex,NULL,NULL,NULL);
        if(drivex[0])
            drive = drivex[0] - '@';
        else
            drive = 0;
    }
  
    /* Find disk free space */
    getdfree(drive,&dtable);
  
 /* Changed to accomodate big disks - WA7TAS */
    bpcl = (unsigned long)dtable.df_bsec * (unsigned long)dtable.df_sclus;
    free_bytes  = (unsigned long)dtable.df_avail * bpcl;
    total_bytes = (unsigned long)dtable.df_total * bpcl;
#endif /* UNIX */
  
    if(n & 1)
        fputs("\n",file);
  
    sprintf(s_free,"%lu",free_bytes);
    commas(s_free);
    sprintf(s_total,"%lu",total_bytes);
    commas(s_total);
  
    if(n)
        sprintf(cbuf,"%d",n);
    else
        strcpy(cbuf,"No");
  
    fprintf(file,"%s file%s. %s bytes free. Disk size %s bytes.\n",
    cbuf,(n==1? "":"s"),s_free,s_total);
}
#endif /* !defined(UNIX_DIR_LIST) */

static void
free_clist(this)
struct dirsort *this;
{
    struct dirsort *next;
  
    while (this != NULLSORT) {
        next = this->next;
        free(this);
        this = next;
    }
}
#ifdef  notdef
static int
getdir_nosort(path,full,file)
char *path;
int full;
FILE *file;
{
    struct ffblk sbuf;
    int command;
    int n = 0;      /* Number of directory entries */
  
/*      path = wildcardize(path); */
    command = 0;
    while(nextname(command, path, &sbuf)){
        command = 1;    /* Got first one already... */
        if (sbuf.ff_name[0] == '.')     /* drop "." and ".." */
            continue;
        format_fname_full(file, &sbuf, full, ++n);
    }
    if(full)
        print_free_space(path, file, n);
    return 0;
}
#endif
  
/* Translate those %$#@!! backslashes to proper form */
void
undosify(s)
char *s;
{
    while(*s != '\0'){
        if(*s == '\\')
            *s = '/';
        s++;
    }
}
  
char *
make_dir_path(int count,char * arg,char * curdir)
{
    char path[128], *q;
  
    undosify(curdir);
    if (count>=2) {  /* arg valid? */
        undosify(arg);
        q=arg;
        q+=strlen(arg)-1;
        if (*q=='/' || *q==':') {
            strcpy(path,arg);
#ifdef UNIX
            strcat(path,"*");
#else
            strcat(path,"*.*");
#endif
        } else
            strcpy(path,arg);
    } else {
#ifdef UNIX
        strcpy(path,"*");
#else
        strcpy(path,"*.*");
#endif
    }
    return (make_fname(curdir,path));
}
  
char*
make_fname(char * curdir, char * fname)
{
    char *p;
    static char new_name[128];
  
    strcpy(new_name,curdir);
    undosify(fname);
#ifdef UNIX
    if (fname[0]=='/') {
#else
        if (fname[0]=='/' || (strchr(fname,':') != NULLCHAR) ) {
#endif
            return fname;
        } else {
            p=new_name;
            p+=strlen(p)-1;
            if (*p=='/')
                *p='\0';
            crunch(new_name,fname);
            return new_name;
        }
    }
  
/* Check Drive/Directory for validity - 1=OK, 0=NOGOOD */
  
    int
    dir_ok(char * newpath,struct cur_dirs * dirs)
    {
#ifdef UNIX
        char *a, curpath[1024];
#else
        char *a, curpath[128];
#endif
        char buf[128],fullpath[1026]; /* 01Oct2019, Maiko, compiler format overflow warning, fullpath should be at least curpath+2, not 128 */
        int result;
#ifndef UNIX
        int drive=dirs->drv;
#endif
  
        undosify(newpath);
#ifdef UNIX
        strcpy(buf, newpath);
        strcpy(curpath, dirs->dir);
#else
        a=newpath;
        if ((*(a+1)==':') && (isalpha(*a))){
            drive=toupper(*a)-'@';
            strcpy(buf,a+2);
            if (dirs->curdir[drive]==NULLCHAR) {
                if(!getcurdir(drive,curpath)) {
                    undosify(curpath);
                    sprintf(fullpath,"%c:/%s",drive+'@',curpath);
                    dirs->curdir[drive]=j2strdup(fullpath);
                    dirs->drv=drive;
                    dirs->dir=dirs->curdir[drive];
                }
            }
        } else {
            strcpy(buf,newpath);
        }
  
        if((a=dirs->curdir[drive])!=NULLCHAR) {
            if ((*(a+1)==':') && (isalpha(*a))){
                if (*(a+2)=='/')
                    strcpy(curpath,a+3);
                else
                    strcpy(curpath,a+2);
            } else {
                strcpy(curpath,a);
            }
        } else {
            strcpy(curpath,"");
        }
#endif
  
        if (*buf!='/') {
            crunch(curpath,buf);
        } else {
            strcpy(curpath,buf+1);
        }
        a=curpath;
#ifdef UNIX
        sprintf(fullpath,"%s%s",(*a!='/' ? "/" : ""),curpath);
#else
        sprintf(fullpath,"%c:%s%s",drive+'@',(*a!='/' ? "/" : ""),curpath);
#endif
  
        if((result=access(fullpath,0)+1)==1) {
#ifdef UNIX
            free(dirs->dir);
            dirs->dir = j2strdup(fullpath);
#else
            if(dirs->curdir[drive])
                free(dirs->curdir[drive]);
            dirs->curdir[drive]=j2strdup(fullpath);
            dirs->drv=drive;
            dirs->dir=dirs->curdir[drive];
#endif
        }
        return result;
    }
  
    char *
    init_dirs(struct cur_dirs * dirs)
    {
#ifdef UNIX
        dirs->dir = getcwd(NULL, 1024);
        return dirs->dir;
#else
        char buf[128],fullpath[128];
        int x,drive;
  
        for(x=0;x<=26;x++)
            dirs->curdir[x]='\0';
  
        drive=getdisk()+1;
        getcurdir(drive,buf);
        undosify(buf);
        sprintf(fullpath,"%c:/%s",drive+'@',buf);
        dirs->curdir[drive]=j2strdup(fullpath);
        dirs->drv=drive;
        dirs->dir=dirs->curdir[drive];
        return dirs->curdir[drive];
#endif
    }
  
    void
    free_dirs(struct cur_dirs * dirs)
    {
#ifdef UNIX
        free(dirs->dir);
#else
        int x;
  
        for(x=0;x<=26;x++) {
            if (dirs->curdir[x])
                free(dirs->curdir[x]);
        }
#endif
    }
  
#ifdef  MSDOS
/* Valid characters in a DOS filename matrix */
    static unsigned char doschars[] = {
        0x00, 0x00, 0x00, 0x00, 0xfa, 0x23, 0xff, 0x03,
        0xff, 0xff, 0xff, 0xc7, 0xff, 0xff, 0xff, 0x6f,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
  
    int
    dosfnchr(ch)
    int ch;
    {
        int i, j;
  
        i = (ch & 0xf8) >> 3;
        j = doschars[i] & (1 << (ch & 0x07));
        return j;
    }
#endif
  
#ifdef DOSCMD
/* Standard commands called from main */
    int
    dodelete(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        int i;
        char fname[128];
  
        for(i=1;i < argc; i++){
            strcpy(fname,make_fname(Command->curdirs->dir,argv[i]));
            if(unlink(fname) == -1){
                tprintf("Can't delete %s: %s\n",fname,strerror(errno));
            }
        }
        return 0;
    }
  
    int
    dorename(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        char fname1[128];
        char fname2[128];
  
        strcpy(fname1,make_fname(Command->curdirs->dir,argv[1]));
        strcpy(fname2,make_fname(Command->curdirs->dir,argv[2]));
        if(rename(fname1,fname2) == -1)
            tprintf("Can't rename: %s\n",strerror(errno));
        return 0;
    }
  
    int
    docopy(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        FILE *old, *new;
        int ch;
        unsigned char count;
        char fname[128];
  
        strcpy(fname,make_fname(Command->curdirs->dir,argv[1]));
        if((old = fopen(fname,"rb")) == NULL) {
            tprintf("Can't open %s: %s\n",fname,strerror(errno));
            return 1;
        }
        strcpy(fname,make_fname(Command->curdirs->dir,argv[2]));
        if((new = fopen(fname,"wb")) == NULL) {
            tprintf("Can't open %s: %s\n",fname,strerror(errno));
            fclose(old);
            return 1;
        }
    /* Now go copy */
        count = 0;
        while((ch = fgetc(old)) != EOF) {
            if (fputc(ch,new) == EOF) {
                    j2tputs("Copy failed!\n");
                    break;
            }
            if(!(++count))      /* be polite to other users */
                pwait(NULL);
        }
        fclose(old);
        fclose(new);
        return 0;
    }
  
/* Change working directory */
    int
    docd(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        if(argc > 1){
            if (!dir_ok(argv[1],Command->curdirs)) {
                tprintf("Invalid Drive/Directory - %s\n",argv[1]);
                return 1;
            }
        }
        tprintf("Local Directory - %s\n",Command->curdirs->dir);
        return 0;
    }
  
/* Create directory */
    int
    domkd(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        char path[128];
  
        strcpy(path,make_fname(Command->curdirs->dir,argv[1]));
  
#ifdef UNIX
        if(mkdir(path, 0755) == -1)
#else
            if(mkdir(path) == -1)
#endif
                tprintf("Can't make %s: %s\n",path,strerror(errno));
        return 0;
    }
  
/* Remove directory */
    int
    dormd(argc,argv,p)
    int argc;
    char *argv[];
    void *p;
    {
        char path[128];
  
        strcpy(path,make_fname(Command->curdirs->dir,argv[1]));
        if(rmdir(path) == -1)
            tprintf("Can't remove %s: %s\n",path,strerror(errno));
        return 0;
    }
  
#endif /* DOSCMD */
  
