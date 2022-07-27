/* Simple-minded program that generates dependencies for a makefile.
 * Does not process #ifdefs, so some spurious dependencies may be
 * generated.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <string.h>
#include <dir.h>
  
char include[] = "#include";
#define HEADER 0
#define SOURCE 1
  
void mkdep(char *name, int type) {
    int found = 0;
    FILE *fp;
    char *cp,*cp1,*cp2;
    char buf[512];
  
    fp = fopen(name,"r");
    strlwr(name);
    while(fgets(buf,sizeof(buf),fp) != NULL){
        if(strncmp(buf,include,sizeof(include)-1) != 0)
            continue;
        if((cp = strchr(buf,'\"')) == NULL)
            continue;
        cp++;
        if((cp1 = strchr(cp,'\"')) == NULL)
            continue;
        if(!found) {
            if(type == HEADER) {
                printf("%s:",name);
            } else {
                cp2 = strchr(name,'.');
                *cp2 = '\0';
                printf("%s.obj: %s.c",name,name);
            }
            found = 1;
        }
        putchar(' ');
        while(cp != cp1)
            putchar(*cp++);
    }
    if(found)
        putchar('\n');
    fclose(fp);
}
  
void main(void) {
    struct ffblk ff;
    int done;
  
    /* First check all the header files */
    done = findfirst("*.h",&ff,0);
    while(!done){
        mkdep(ff.ff_name,HEADER);
        done = findnext(&ff);
    }
  
    /* Next check all the C files */
    done = findfirst("*.c",&ff,0);
    while(!done){
        mkdep(ff.ff_name,SOURCE);
        done = findnext(&ff);
    }
    exit(0);  
}
  
