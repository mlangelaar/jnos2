#include <stdio.h>
#include <string.h>
  
/* A very simple dos-to-unix text file translator.
 * Johan. K. Reinalda, WG7J 930625
 */
  
void main(int argc,char *argv[]) {
    int i;
  
    if(argc == 1)
        return;
    for(i=1;i<argc;i++) {
        FILE *fpo,*fpn;
        char *cp;
        char back[128];
        char buf[128];
  
        strcpy(back,argv[i]);
        if((cp=strchr(back,'.')) != NULL)
            *cp = '\0';
        strcat(back,".&&&");
        unlink(back);
        if(rename(argv[i],back) != 0) {
            printf("Cannot rename %s to %s\n",argv[i],back);
            continue;
        }
        if((fpo=fopen(back,"rb")) == NULL) {
            printf("Cannot open %s\n",back);
            continue;
        }
        if((fpn=fopen(argv[i],"wb")) == NULL) {
            printf("Cannot write %s\n",argv[i]);
            continue;
        }
        while(fgets(buf,sizeof(buf),fpo) != NULL) {
            cp = buf;
            while(*cp) {
                if(*cp == 0xd || *cp == 0xa) {
                    *cp = '\0';
                    break;
                }
                cp++;
            }
            fprintf(fpn,"%s\n",buf);
        }
        fclose(fpo);
        fclose(fpn);
        unlink(back);
    }
    return;
}
