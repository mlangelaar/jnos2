/* MAIL2IND performs various utility functions external to Jnos itself.
 * It can reindex all *.TXT mail files in and below the mailspool directory,
 * although this is obsolete since Jnos includes the "index *" cmd to do this.
 * It can determine the largest msgid in use, to provide a value to place
 * into sequence.seq when it's been lost.  It can find which message has a
 * specified bid.  Finally, it can dump an index file with annotation.
 * (C) 1993 Johan. K. Reinalda, WG7J
 * Freely usable for non-commercial use only !
 *
 * Syntax:
 * ' MAIL2IND [-d<rootdirname>] [-m] [-v] [-h | -? | -sAREANAME | -bBIDtext | AREANAME]
 * Eg: mail2ind -d/nos -samsat
 *
 * if called with an argument not starting with - ,
 * this is taken to be a file to be indexed. If this file has
 * no extension, the '.txt' is assumed.
 * Eg: mail2ind wg7j
 *     mail2ind wg7j.txt
 *
 * v1.08 - add -b bid-search feature.
 * v1.07 - add -m option to display highest msgid found (n5knx)
 * v1.06 - fixed -d again :-(
 * v1.05 - redone for new format of index file. Now called MAIL2IND.EXE
 *         released with jnos 1.10x10 . Added -v option to show index file.
 *         and -v for verbose mode.
 * v1.04 - /spool/areas file is read for message type. (-d option also
 *         works with this)
 * v1.03 - added -? option for help, smtp 'Apparently-to:' handled,
 * v1.02 - bug fix in -d option. (930706)
 * v1.01 - bug fix, no more corrupt index file if message file is empty
 *         or contains no valid messages.  (930629)
 * v1.0  - Initial release, with JNOS 1.10X3 (930622)
 *
 */
  
#include <fcntl.h>
#include <ctype.h>
#ifdef MSDOS
#include <dir.h>
#include <io.h>
#include <dos.h>
#endif
#include "global.h"
#include "mailutil.h"
#include "smtp.h"
#include "files.h"
#include "index.h"
#ifdef UNIX
#include "unix.h"
#include "dirutil.h"
#endif
  
#define VERSION "JNOS index file utility v1.09\n" \
"(C) 1993 Johan. K. Reinalda, WG7J\n"
  
#undef fopen
  
char *Mailspool = "/spool/mail";
char *Arealist = "/spool/areas";
char *Historyfile = "/spool/history";
  
int found,verbose,domsgid;
long highest_msgid;
char *Bid2Find = NULLCHAR;

int LookForBid(char *file);
void show_index(char *name, int msgidflag);
void show_bid(char *srchbid);
void Index(char *file);
int checkdir(char *path, void (*func) __ARGS((char *pt)));
void main(int argc,char *argv[]);
void *mallocw(unsigned nb);
int pwait(void *event);
  
void Index(char *file) {
    int val;
    char *cp;
  
    found = 1;
    printf("File: %s/%s\n",Mailspool,file);
  
    if((cp = strrchr(file,'.')) != NULL)
        *cp = '\0';
  
    if (Bid2Find) {
        LookForBid(file);
    } else {
        if((val=IndexFile(file,verbose)) != 0)
            printf("Error %d occured!\n",val);
        else
            if (domsgid) show_index(file, domsgid);
    }
  
    return;
}
  
void show_bid(char *srchbid) {
    FILE *fp;
    char buf[FILE_PATH_SIZE], *cp;

    if ((fp = fopen(Historyfile, "r")) == NULLFILE) {
        perror(Historyfile);
        return;
    }

    while(fgets(buf,sizeof(buf),fp) != NULLCHAR) {
        /* The first word on each line is all that matters */
        cp=skipnonwhite(buf);
        *cp = '\0';
        if(stricmp(srchbid,buf) == 0) {    /* bid exists in some area */
            break;
        }
    }
    if (feof(fp)) {
        printf ("bid %s not found in history file.\n", srchbid);
        fclose(fp);
        return;
    }
    fclose(fp);

    Bid2Find = srchbid;   /* set flag for Index() */
    if(checkdir(NULL, Index))
        puts("ERROR detected, not all index files could be examined.\n");

}


int LookForBid(char *file) {
/* scan index associated with <file> for a msg containing bid Bid2Find */

    struct indexhdr hdr;
    struct mailindex ind;
    char buf[FILE_PATH_SIZE], *cp;
    int idx,i,bidlen, serr = 0;

    dirformat(file);
    sprintf(buf,"%s/%s.ind",Mailspool,file);
    if((idx = open(buf,READBINARY)) == -1) {
        printf("Can not read index file %s\n",buf);
        return -1;
    }
  
    memset(&ind,0,sizeof(ind));
    default_index("",&ind);
    bidlen=strlen(Bid2Find);
  
    /* Read the index file header */
    if (read_header(idx,&hdr) == 0)
	{
		for(i=1;i<=hdr.msgs;i++)
		{
        	if (read_index(idx,&ind) == -1)
			{
        		serr = 1;
				break;
			}
        	if(strnicmp(ind.messageid,Bid2Find,bidlen) == 0) {
            	if (*(ind.messageid+bidlen) == '@')
                	printf("Bid %s found in msg #%d of area %s\n", Bid2Find, i, file);
        	}
        	/* Done with this index, clear it */
        	default_index("",&ind);
    	}
	}
    else serr = 1;

	if (serr)
		printf("Error reading index of %s\n", file);

    close(idx);
    return 0;
}


void show_index(char *name, int msgidflag) {
    int i,idx;
    char *cp;
    struct indexhdr hdr;
    struct mailindex ind;
    char buf[129];
  
    if((cp=strchr(name,'.')) != NULL)
        *cp = '\0';
    sprintf(buf,"%s/%s.ind",Mailspool,name);
    if((idx = open(buf,READBINARY)) == -1) {
        printf("Index file '%s' not found!\n",buf);
        return;
    }
  
    if(read_header(idx,&hdr) == -1){
        printf("Can not read header from index file '%s'\n",buf);
        close(idx);
        return;
    }
  
    if (!msgidflag)
        printf("%s has %d message%s:\n\n",buf,hdr.msgs,(hdr.msgs == 1 ? "" : "s"));
  
    memset(&ind,0,sizeof(ind));
  
    for(i=1;i<=hdr.msgs;i++) {
        if (!msgidflag) printf("Message %d\n",i);
        default_index("",&ind);
        if(read_index(idx,&ind) == -1) {
            puts("Can not read index!");
            break;
        }
        if (msgidflag) {
            if (ind.msgid > highest_msgid) highest_msgid = ind.msgid;
        }
        else print_index(&ind);
    }
    default_index("",&ind);
    close(idx);
  
}


static long allsz;
static long allmsgcnt;
  
void show_area_tots(char *areap) {
    int i,idx;
    unsigned int  bullcnt, holdcnt, trafcnt;
    long bullsz, holdsz, trafsz, totsz;
    struct indexhdr hdr;
    struct mailindex ind;
    char buf[FILE_PATH_SIZE], *cp;
#define BM_HOLD 8
  
    sprintf(buf,"%s/%s", Mailspool, areap);
    if ((cp=strrchr(areap,'.')) != NULL) *cp='\0';
    if((idx = open(buf,READBINARY)) != -1) {
        if(read_header(idx,&hdr) == -1)
            hdr.msgs = 0;

        memset(&ind,0,sizeof(ind));

        bullcnt = holdcnt = trafcnt = 0U;
        bullsz = holdsz = trafsz = totsz = 0L;
	for(i=1;i<=hdr.msgs;i++) {
            default_index("",&ind);
            if(read_index(idx,&ind) == -1) {
                puts("* index read err *");
                break;
            }
            totsz += ind.size;
            if (ind.type == 'B') {bullsz += ind.size; bullcnt++;}
            if (ind.type == 'T') {trafsz += ind.size; trafcnt++;}
            if (ind.status & BM_HOLD) {holdsz += ind.size; holdcnt++;}
	}
        printf("%-27s%6u %5ldK ",areap,hdr.msgs, (totsz+1023)/1024);
        allsz += totsz;
        
        if (hdr.msgs) {
            allmsgcnt += hdr.msgs;
            if (bullcnt) printf("%6u %5ldK", bullcnt, (bullsz+1023)/1024);
            else printf("%12s", " ");
            if (trafcnt) printf("%6u %5ldK", trafcnt, (trafsz+1023)/1024);
            else printf("%12s", " ");
            if (holdcnt) printf("%6u %5ldK", holdcnt, (holdsz+1023)/1024);
            /*else printf("%12s", " ");*/
        }
        printf("\n");
        default_index("",&ind);
        close(idx);
    }
}


void show_area_stats(void) {
    printf ("AREA                         #Msgs  (KB)  #Bulls  (KB)  #Traf  (KB)  #Held  (KB)\n");

    allsz = 0L; allmsgcnt=0;
    checkdir(NULL, show_area_tots);
    printf ("%-27s%6lu %5ldK ","==TOTALS==",allmsgcnt, (allsz+1023)/1024);
    printf ("\n");
}

  
int checkdir(char *path, void (*func) __ARGS((char *pt)) ) {
    char *wildcard,*newpath,*fullname;
    char *suffix;
    struct ffblk ff;
    int done;
  
    if (func == Index ) suffix = "txt";
    else suffix = "ind";

    if((wildcard = malloc(3*FILE_PATH_SIZE)) == NULL) {
        puts("can't allocate path variables");
        return -1;
    }
    newpath = wildcard+FILE_PATH_SIZE;
    fullname = newpath+FILE_PATH_SIZE;
    /* First check all the files */
    if(!path)
        sprintf(wildcard,"%s/*.%s",Mailspool,suffix);
    else
        sprintf(wildcard,"%s/%s/*.%s",Mailspool,path,suffix);
    done = findfirst(wildcard,&ff,0);
    while(!done){
        if(!path)
            strcpy(fullname,ff.ff_name);
        else
            sprintf(fullname,"%s/%s",path,ff.ff_name);
        func(fullname);
        done = findnext(&ff);
    }
    /* Now check for sub-directories */
    if(!path)
        sprintf(wildcard,"%s/*.*",Mailspool);
    else
        sprintf(wildcard,"%s/%s/*.*",Mailspool,path);
    done = findfirst(wildcard,&ff,FA_DIREC);
    while(!done){
        if(strcmp(ff.ff_name,".") && strcmp(ff.ff_name,"..")) {
            /* Not the present or 'mother' directory, so create new path,
             * and recurs into it.
             */
            if(!path)
                strcpy(newpath,ff.ff_name);
            else
                sprintf(newpath,"%s/%s",path,ff.ff_name);
            checkdir(newpath, func);
        }
        done = findnext(&ff);
    }
    free(wildcard);
    return 0;
}
  
char HelpTxt[] = "Options:\n"
"<filename>   mailfile to index\n"
"-d<rootdir>  set new root, same as 'NOS -d<dir>\n"
"-s<filename> view index file for <filename>\n"
"-f           summarize area status\n"
"-v           verbose (show index before writing it)\n"
"-m           display highest msgid found during processing\n"
"-bBIDtext    search history file and areas for msg(s) having this bid\n"
"-?,-h,-H     produce this help.";
  
char NewMailspool[FILE_PATH_SIZE];
char NewArealist[FILE_PATH_SIZE];
char NewHistoryfile[FILE_PATH_SIZE];
  
void main(int argc,char *argv[]) {
    char *cp;
    int i,len,onefile = 0;
    char fn[FILE_PATH_SIZE];
  
    puts(VERSION);
  
    if(argc > 1) {
        for(i=1;i<argc;i++) {
/*            dirformat(argv[1]);		/* ?????? why? */
            if(argv[i][0] == '-') {
                /* Command line options */
                switch(tolower(argv[i][1])) {
                    case '?':
                    case 'h':
                        puts(HelpTxt);
                        return;
                    case 'd':
                    /* Set the directory to search and index */
                        cp = &argv[i][2];
                        len=strlen(cp);
                        if(len) {
                            if(cp[len-1] == '/' || cp[len-1] == '\\')
                                cp[len-1] = '\0';
                        /* Add path in front */
                            strcpy(NewMailspool,cp);
                            strcat(NewMailspool,Mailspool);
                            Mailspool = NewMailspool;
                            strcpy(NewArealist,cp);
                            strcat(NewArealist,Arealist);
                            Arealist = NewArealist;
                            strcpy(NewHistoryfile,cp);
                            strcat(NewHistoryfile,Historyfile);
                            Historyfile = NewHistoryfile;
                        }
                        break;
                    case 'v':
                        verbose = 1;
                        break;
                    case 'm':
                        domsgid = 1;
                        break;
                    case 's':
                        show_index(&argv[i][2], domsgid);
                        if (domsgid)
                            printf("Largest msgid found was %ld\n", highest_msgid);
                        return;
                        /*break;*/
                    case 'f':
                        show_area_stats();
                        return;
                        /*break;*/
                    case 'b':
                        show_bid(&argv[i][2]);
                        return;
                        /* break */
                    default:
                        printf("Invalid option %s\n\n",argv[1]);
                        puts(HelpTxt);
                        return;
                }
            } else {
                /* Just one file to do */
                if((cp=strchr(argv[i],'.')) != NULL)
                    *cp = '\0'; /* Cut off extension */
                strcpy(fn,argv[i]);
                strcat(fn,".txt");
                onefile = 1;
            }
        }
    }
  
    if(onefile) {
        Index(fn);
        if(!found)
            printf("Not found: %s\n", fn);
        else if (domsgid)
            printf("Largest msgid found was %ld\n", highest_msgid);
        return;
    }
  
    if(checkdir(NULL, Index))
        puts("ERROR detected, not all index files created!\n");
    if(!found)
        puts("NO files found to index !\n");
    else if (domsgid)
        printf("Largest msgid found was %ld\n", highest_msgid);
}
  
int pwait(void *event) {
    return 0;
}
  
void *
mallocw(nb)
unsigned nb;
{
#undef malloc
    return malloc((size_t)nb);
}

#ifdef UNIX
/* We've defined free to be j_free, so we need to supply a j_free! */
/* We can't undef free at the start, since glob.o calls j_free ... */
void
j_free(void *p)
{
#undef free
    free(p);
}
#endif
