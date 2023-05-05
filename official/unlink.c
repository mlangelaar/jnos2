#include <stdio.h>
#include <string.h>
  
#define BUFLEN 128
  
void main(int argc,char *argv[]) {
    char *cp,*cp2;
    FILE *fp;
    char buf[BUFLEN+1];
  
    if(argc == 1) {
        puts("Syntax: Unlink <list-file>");
        return;
    }
    if((fp = fopen(argv[1],"r")) == NULL) {
        printf("Could not open %s\n",argv[1]);
        return;
    }
    while(fgets(buf,BUFLEN,fp) != NULL) {
        cp = strchr(buf,' ');
        cp++;
        cp2 = strchr(cp,'.');
        strcpy(++cp2,"obj");
        if(unlink(cp))
            printf("Could not delete %s\n",cp);
    }
    fclose(fp);
}
  
