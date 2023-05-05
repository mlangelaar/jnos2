/*
This program allows you to push mail destined for one host,
to another host for it to perform delivery.  For example, suppose
you are going on vacation and shutting down your host but you have mail
in MQUEUE waiting to be delivered to other hosts which are not online.
This program will permit you to pick another host which IS online, to
receive your queued mail, thereby permitting that host to do the delivery
while you are away.  Remember to use fully-qualified-domain-names!

Usage:   cd /spool/mqueue;
	 for %i in (offline_host_1 offline_host_2 ...) do pushmail %i online_host
	 jnos
	 smtp kick


From: hays@apollo.hp.com (John D. Hays KD7UW)
Newsgroups: usl.maillist.tcp-group
Date: 20 Oct 89 16:11:44 GMT

Modified by jpd@usl.edu, N5KNX:  bugs fixed; added delete feature.
Compile by:  tcc -O -G -Z -a -ms pushmail.c
*/

#include <stdio.h>
#include <stdlib.h>
#ifdef MSDOS
#include <dir.h>
#include <dos.h>
#endif
#ifdef UNIX
#include "unix.h"
#include "dirutil.h"
#endif
#include <fcntl.h>
#include <string.h>
#define MAXLINE 256

void
main(int argc,char *argv[])
{
	struct ffblk block;
	int done,i,firsttime=1;
	FILE *fd,*fo;
	char *j,str[MAXLINE+2],*strp;
	char path[256], npath[256], *mqdir;

#ifdef VERBOSE_COPYRIGHT
	fprintf(stderr,"\n%s is COPYRIGHT 1989 by John D. Hays (KD7UW)\n",argv[0]);
	fprintf(stderr,"Authorized for unlimited distribution for ");
	fprintf(stderr,"Amateur Radio Use\n");
	fprintf(stderr,"This notice must be retained.  All Rights Reserved.\n\n");
#else
static char cmsg1[] = "COPYRIGHT 1989 by John D. Hays (KD7UW)\n";
static char cmsg2[] = "Authorized for unlimited distribution for ";
static char cmsg3[] = "Amateur Radio Use\n";
static char cmsg4[] = "This notice must be retained.  All Rights Reserved.\n\n";
#endif
	if (argc < 3)
	{
		fprintf(stderr,"Usage: %s oldhost forwardhost [mqueue_dir]\n",argv[0]);
		fprintf(stderr,"\tNo implicit domains are assumed; forwardhost may be 'delete' to kill msg(s) to oldhost\n");
		exit(1);
	}

	if (argc == 4)
		mqdir = argv[3];
	else mqdir = "/spool/mqueue";


	while (1) {
		if (firsttime) {
			strcpy(path,mqdir);
			strcat(path,"/");
			strcat(path,"*.wrk");
			done = findfirst(path,&block,0x3F);
			firsttime=0;
		}
		else
			done = findnext(&block);
		if (done) break;
		
		strcpy(path,mqdir);
		strcat(path, "/");
		strcat(path, block.ff_name);
		if ((fd = fopen(path,"r")) != NULL)
		{
			strp = str; /* Turbo-C is not handling fgets right ??? */
			strp = fgets(strp,MAXLINE,fd);
			j = strrchr(strp,'\n');
			if (j != NULL) *j = '\0';
			if (stricmp(argv[1],strp) == 0)
			{
				fprintf(stderr,"Changing: %s\n",block.ff_name);
				unlink(path);  /* Not really since is opened already */
				if (strcmp(argv[2],"delete")) { /* not delete */
					strcpy(npath,path);
					strcpy(&npath[strlen(npath)-3],"krw");
					fo = fopen(npath,"w");
					fprintf(fo,"%s\n",argv[2]);
					while ((strp = fgets(strp,MAXLINE,fd)) != NULL)
					{
						fprintf(fo,"%s",strp);
					}
					fclose(fo);
					fclose(fd);
					rename(npath,path);
				} else {  /* delete msg */
					strcpy(&path[strlen(path)-3],"txt");
					unlink(path);
					strcpy(&path[strlen(path)-3],"lck");
					unlink(path);  /* just in case */
					fclose(fd);
				}
			}
			else fclose(fd);
		}
		else
		    fprintf(stderr,"%s: Unable to open %s\n",argv[0],block.ff_name);
	}
	exit(0);
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

void *
mallocw(nb)
unsigned nb;
{
#undef malloc
    return malloc((size_t)nb);
}
#endif
