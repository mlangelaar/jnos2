/* This programs convert mail files in the "BM" format to the new mail
   file format.
   Link object with files.obj, misc.obj and dirutil.obj.
*/
#include "global.h"
#include "files.h"
static void convert __ARGS((char *dir));
static int isarea __ARGS((char *area));
static void areafile __ARGS((char *filename,char *area));
static void mkdirs __ARGS((char *path));
  
/* Takes optional subdirectory in /spool/mail to convert as argument.
   Defaults to /spool/mail
 */
main(argc,argv)
int argc;
char *argv[];
{
    char fname[256];
    sprintf(fname,"%s/mail",Newsdir);
    mkdirs(fname);
    mkdirs(Userdir);
    if(argc > 1)
        sprintf(fname,"%s/%s",Mailspool,argv[1]);
    else
        strcpy(fname,Mailspool);
    convert(fname);
}
static void
convert(dir)
char *dir;
{
    char fname[256],buf[1024],line[20],fpath[256],dirstr[256],*cp;
    int msg, skip = 0;
    FILE *mfile = NULLFILE, *fp;
    printf("Scanning directory %s\n",dir);
    sprintf(dirstr,"%s/*",dir);
    filedir(dirstr,0,line);
    while(line[0] != '\0') {
        if(skip) {
            if(stricmp(line,buf) == 0)
                skip = 0;
            filedir(dirstr,1,line);
            continue;
        }
        if(strchr(line,'.') == NULLCHAR) { /* possible directory */
           /* doesn't work if filedir() doesn't operate on directories */
            sprintf(fname,"%s/%s",dir,line);
            strcpy(buf,line);
            skip = 1;
            convert(fname);
            filedir(dirstr,0,line);
            continue;
        }
        if(strlen(line) > 3 && stricmp(&line[strlen(line)-4],".txt") == 0) {
            sprintf(buf,"%s/%s",dir,line);
            if((fp = fopen(buf,READ_TEXT)) == NULLFILE) {
                filedir(dirstr,1,line);
                continue;
            }
            printf("Processing %s\n",buf);
            line[strlen(line)-4] = '\0';
            if(isarea(line) || stricmp(dir,Mailspool) != 0) {
                if(strlen(dir) > strlen(Mailspool)) {
                    sprintf(buf,"%s.%s",&dir[strlen(Mailspool)+1],line);
                    while((cp = strchr(buf,'/')) != NULLCHAR)
                        *cp = '.';
                }
                else
                    sprintf(buf,"%s",line);
            }
            else
                sprintf(buf,"mail.%s",line);
            areafile(fpath,buf);
            mkdirs(fpath);
            msg = 0;
            mfile = NULLFILE;
            while(fgets(buf,sizeof(buf),fp) != NULLCHAR) {
                if(strnicmp("From ",buf,5) == 0) {
                    fclose(mfile);
                    sprintf(fname,"%s/%u",fpath,++msg);
                    if((mfile = fopen(fname,WRITE_TEXT)) == NULLFILE){
                        printf("Can't write %s\n",fname);
                        exit(1);
                    }
                    if((cp = strchr(buf+5,' ')) != NULLCHAR)
                        *cp = '\0';
                    fprintf(mfile,"Return-Path: %s\n",buf+5);
                }
                else
                    fputs(buf,mfile);
            }
            fclose(mfile);
            fclose(fp);
            sprintf(buf,"%s/active",Newsdir);
            if((fp = fopen(buf,APPEND_TEXT)) == NULLFILE) {
                printf("Can't write %s\n",buf);
                exit(1);
            }
            if(isarea(line) || stricmp(dir,Mailspool) != 0) {
                if(strlen(dir) > strlen(Mailspool)) {
                    sprintf(buf,"%s.%s",&dir[strlen(Mailspool)+1],line);
                    while((cp = strchr(buf,'/')) != NULLCHAR)
                        *cp = '.';
                }
                else
                    strcpy(buf,line);
                fprintf(fp,"%s ",buf);
            }
            else
                fprintf(fp,"mail.%s ",line);
            if(msg)
                fprintf(fp,"%u 1 y",msg);
            fprintf(fp,"\n");
            fclose(fp);
        }
        filedir(dirstr,1,line);
    }
}
  
/* Returns 1 if name is a public message Area, 0 otherwise */
static int
isarea(name)
char *name;
{
    char buf[1024], *cp;
    FILE *fp;
    if((fp = fopen(Arealist,READ_TEXT)) == NULLFILE)
        return 0;
    while(fgets(buf,sizeof(buf),fp) != NULLCHAR) {
        /* The first word on each line is all that matters */
        if((cp = strchr(buf,' ')) == NULLCHAR)
            if((cp = strchr(buf,'\t')) == NULLCHAR)
                continue;
        *cp = '\0';
        if((cp = strchr(buf,'\t')) != NULLCHAR)
            *cp = '\0';
        if(stricmp(name,buf) == 0) {    /* found it */
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}
  
static void
areafile(filename,area)
char *filename;
char *area;
{
    FILE *fp;
    char buf[1024], *cp;
    static char *delim = " \t\n";
    if((fp = fopen(Pointerfile,READ_TEXT)) != NULLFILE){
        while(fgets(buf,sizeof(buf),fp) != NULLCHAR){
            if((cp = strtok(buf,delim)) == NULLCHAR)
                continue;
            if(stricmp(cp,area) == 0 &&
            (cp = strtok(NULLCHAR,delim)) != NULLCHAR){
                strcpy(filename,cp);
                fclose(fp);
                return;
            }
        }
        fclose(fp);
    }
    strcpy(buf,area);
    while((cp = strchr(buf,'.')) != NULLCHAR)
        *cp = '/';
    sprintf(filename,"%s/%s",Newsdir,buf);
}
static void
mkdirs(path)
char *path;
{
    char *cp, buf[128];
    strcpy(buf,path);
    if(path[strlen(path)-1] != '/')
        strcat(buf,"/");
    cp = buf;
    while((cp = strchr(cp,'/')) != NULLCHAR) {
        *cp = '\0';
#ifdef  UNIX
        mkdir(buf,0755);
#else
        mkdir(buf);
#endif
        *cp++ = '/';
    }
}
  
#ifdef UNIX
#include <sys/types.h>
#include <dirent.h>
/* wildcard filename lookup */
filedir(name, times, ret_str)
char    *name;
int times;
char    *ret_str;
{
    static char dname[128], fname[128];
    static DIR *dirp = NULL;
    struct dirent *dp;
    char    *cp, temp[128];
  
    /*
     * Make sure that the NULL is there in case we don't find anything
     */
    ret_str[0] = '\0';
  
    if (times == 0) {
        /* default a null name to *.* */
        if (name == NULL || *name == '\0')
            name = "*.*";
        /* split path into directory and filename */
        if ((cp = strrchr(name, '/')) == NULL) {
            strcpy(dname, ".");
            strcpy(fname, name);
        } else {
            strcpy(dname, name);
            dname[cp - name] = '\0';
            strcpy(fname, cp + 1);
            /* root directory */
            if (dname[0] == '\0')
                strcpy(dname, "/");
            /* trailing '/' */
            if (fname[0] == '\0')
                strcpy(fname, "*.*");
        }
        /* close directory left over from another call */
        if (dirp != NULL)
            closedir(dirp);
        /* open directory */
        if ((dirp = opendir(dname)) == NULL) {
            printf("Could not open DIR (%s)\n", dname);
            return;
        }
    } else {
        /* for people who don't check return values */
        if (dirp == NULL)
            return;
    }
  
    /* scan directory */
    while ((dp = readdir(dirp)) != NULL) {
        /* test for name match */
/*      if (wildmat(dp->d_name, fname)) {*/
        if (!strcmp(dp->d_name, fname) || (fname[0] == '*' &&
            !strcmp(&dp->d_name[strlen(dp->d_name) - strlen(fname)+1],
        &fname[1]))) { /* ...when we do not use wildmat */
            /* test for regular file */
            sprintf(temp, "%s/%s", dname, dp->d_name);
            strcpy(ret_str, dp->d_name);
            break;
        }
    }
  
    /* close directory if we hit the end */
    if (dp == NULL) {
        closedir(dirp);
        dirp = NULL;
    }
}
#endif
