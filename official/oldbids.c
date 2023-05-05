/* This program will deleted old BID's from the history file,
 * after making a backup copy.
 * If no arguments are given, anything older then 30 days will
 * be deleted.
 * By default, the history file is '/spool/history'
 * on the current drive.
 * Optional parameters:
 *      -dpath, where 'path' is the path to the history file
 *          (path can have an optional ending '/')
 *      #, where # is the age of bids to expire
 *
 *  Eg. 'oldbids 30 -d/nos/spool/' will delete all bids older then
 *      30 days from the file '/nos/spool/history'
 *
 * Copyright 1992, Johan. K. Reinalda, WG7J/PA3DIS
 *      email : johan@ece.orst.edu
 *      packet: wg7j@wg7j.or.usa.na
 *
 * Any part of this source may be freely distributed for none-commercial,
 * amateur radio use only, as long as credit is given to the author.
 *
 * v1.0 920325
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
  
static char Copyright[] = "(C) '92 Johan. K. Reinalda, WG7J";
  
#define LEN 80
#define HISTFILE "history"
#define BACKUP ".bak"
#define NULLCHAR (char *) 0
  
char hfile[LEN] = "/spool/";
int days = 30;
int expired = 0;
  
void
main(argc,argv)
int argc;
char *argv[];
{
    register int i;
    char *cp;
    FILE *old, *new;
    time_t now;
    time_t age;
    time_t bidtime;
    char oldfile[LEN];
    char buf[LEN];
  
    /* first get options, if any */
    if(argc > 1) {
        for(i=1;i<argc;i++) {
            if(!strncmp(argv[i],"-d",2) && strlen(argv[i]) < LEN+1) {
                strcpy(hfile,&argv[i][2]);
                cp = &hfile[strlen(hfile)-1];
                if(*cp != '/')
                    strcat(hfile,"/");
            } else {
                if((days = atoi(argv[i])) == 0){
                    printf("Invalid option: %s\n",argv[i]);
                    return;
                }
            }
        }
    }
    strcat(hfile,HISTFILE);
    strcpy(oldfile,hfile);
    strcat(oldfile,BACKUP);
  
    /* delete a previous backup and rename current history file */
    unlink(oldfile);
    if(rename(hfile,oldfile) == -1) {
        puts("Can't rename history file");
        return;
    }
  
    /* open backup for reading, create new history file */
    if((old = fopen(oldfile,"rt")) == NULL) {
        puts("Error opening history.bak");
        return;
    }
    if((new = fopen(hfile,"wt")) == NULL) {
        puts("Error creating history file");
        return;
    }
  
    now = time(&now);
    age = (time_t)(days*86400L);
  
    while(fgets(buf,LEN,old) != NULL) {
        if((cp=strchr(buf,'\n')) != NULLCHAR)
            *cp = '\0';
        if((cp=strchr(buf,' ')) != NULLCHAR) {
            /*found one with timestamp*/
            *cp = '\0';
            cp++;   /* now points to timestamp */
            if((bidtime = atol(cp)) == 0L)
                /*something wrong, re-stamp */
                fprintf(new,"%s %ld\n",buf,now);
            else {
                /* Has this one expired yet ? */
                if(now - bidtime < age)
                    fprintf(new,"%s %ld\n",buf,bidtime);
                else
                    expired++;
            }
        } else {
            /* This is an old one without time stamp,
             * add to the new file with current time as timestamp
             */
            fprintf(new,"%s %ld\n",buf,now);
        }
    }
  
    fclose(old);
    fclose(new);
  
    printf("%d bid's expired\n",expired);
    return;
}
  
