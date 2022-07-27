#include "global.h"
#undef fopen
  
static char include[] = "#include";
static char nameline[1024];
static int Recurse = FALSE;
static int Tree = FALSE;
  
void
scanfile( char *filename, int indent )
{
    FILE *fp;
    char buf[512], *name_p, *end_p, *str_p;
  
    fp = fopen(filename,"r");
    if(fp == NULL){
        fprintf(stderr,"Cannot open %s\n",filename);
        return;
    }
  
    while(fgets(buf,512,fp) != NULL){
        if(strncmp(buf,include,sizeof(include)-1) != 0)
            continue;
        if((name_p = strchr(buf,'\"')) == NULL)
            continue;
        name_p++;   /* skip quote */
        if((end_p = strchr(name_p,'\"')) == NULL)
            continue;
        *end_p = '\0';
        if( Tree ){
            int i = 0;
  
            while( i++ < indent )
                printf("\t");
        }
  
        if( ( str_p = strstr( nameline, name_p ) ) != NULL
        && str_p[-1] == ' ' ){
            if( Tree ){
                printf( "***\t%s\n", name_p );
            }
        } else {
            if( Tree ){
                printf( "\t%s\n", name_p );
            }
            strcat( nameline, " " );
            strcat( nameline, name_p );
  
            if( Recurse )
                scanfile( name_p, indent + 1 );
        }
    }
    fclose(fp);
}
  
main(argc,argv)
int argc;
char *argv[];
{
    char buf[10],*cp;
    int c;
  
    while((c = j2getopt(argc,argv,"rt")) != EOF){
        switch(c){
            case 't':
                Tree = TRUE;
            case 'r':
                Recurse = TRUE;
                break;
        }
    }
  
    for(;j2optind<argc;j2optind++){
        nameline[0] = '\0';
        if( Tree ) {
            printf("%s\n",argv[j2optind]);
        } else {
            strcpy(buf,argv[j2optind]);
            if((cp = strchr(buf,'.')) == NULL)
                continue;
            *cp = '\0';
            printf("%s.obj: %s",buf,argv[j2optind]);
        }
        scanfile( argv[j2optind], 0 );
        if( !Tree )
            printf( "%s\n", nameline );
    }
    return 0;
}
