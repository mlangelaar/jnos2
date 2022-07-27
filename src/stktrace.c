/* This file contains code to print function/arg stack tracebacks
 * at run time, which is extremely useful for finding heap free() errors.
 *
 * This code is highly specific to Borland C and the 80x6 machines.
 *
 * April 10, 1992 P. Karn
 * (adopted for 911229 based code by Johan, WG7J)
 */
  
#include <dos.h>
#include <time.h>
#include "global.h"
#ifdef STKTRACE
#include "proc.h"
  
  
struct symtab {
    struct symtab *next;
    unsigned short seg;
    unsigned short offs;
    char *name;
};
static struct symtab *Symtab;
static void rdsymtab __ARGS((int unused,void *name,void *p));
static void clrsymtab __ARGS((void));
static struct symtab *findsym __ARGS((void (*)()));
static int scompare();
static void paddr __ARGS((void (*pc)(),FILE *fp));
  
static unsigned short Codeseg;
extern unsigned char _osmajor;
  
void
stktrace()
{
    int i,j;
    unsigned short far *context;
    unsigned short far *cnext;
    unsigned short far *ctmp;
    void (*pc)();
    int nargs;
    struct proc *rdproc;
    struct symtab *sp;
    extern char **_argv;
    char *mapname;
    char *cp;
    FILE *fp;
    time_t t;
  
    /* output to file */
    if((fp = fopen("stktrace.out","at")) == NULLFILE)
        return; /* Give up */
  
    time(&t);
    fprintf(fp,"stktrace from proc %s at %s",Curproc->name,ctime(&t));
    Codeseg = _psp + 0x10;
#ifdef  notdef
    fprintf(fp,"Code base segment: %x\n",Codeseg);
#endif
    /* Construct name of map file */
    mapname = malloc(strlen(_argv[0]) + 5);
/*
 * Dos's less than 3.0 do not pass the progname in via argv[0] we fake it, and
 * the mapfile must be 'nos.map'
 */
    if(_osmajor<3)
        strcpy(mapname,"nos");
    else
        strcpy(mapname,_argv[0]);
    if((cp = strrchr(mapname,'.')) != NULLCHAR)
        *cp = '\0';
    strcat(mapname,".map");
  
    /* Read the symbol table in another process to avoid overstressing
     * the stack in this one
     */
    rdproc = newproc("rdsymtab",512,rdsymtab,1,mapname,NULL,0);
    pwait(rdproc);
    free(mapname);
  
    context = MK_FP(_SS,_BP);
    pc = stktrace;
  
    for(i=0;i<20;i++){
        paddr(pc,fp);
        sp = findsym(pc);
        if(sp != NULL)
            fprintf(fp," %s+%x",sp->name,FP_OFF(pc) - sp->offs);
  
        if(FP_OFF(context) == 0){
            /* No context left, we're done */
            fputc('\n',fp);
            break;
        }
        cnext = MK_FP(FP_SEG(context),*context);
        /* Compute number of args to display */
        if(FP_OFF(cnext) != 0){
            nargs = (int)(cnext - context - (1 + (sizeof(pc) >> 1)));
            if(nargs > 20)
                nargs = 20; /* limit to reasonable number */
        } else {
            /* No higher level context, so just print an
             * arbitrary fixed number of args
             */
            nargs = 6;
        }
        /* Args start after saved BP and return address */
        ctmp = context + 1 + (sizeof(pc) >> 1);
        fputc('(',fp);
        for(j=0;j<nargs;j++){
            fprintf(fp,"%x",*ctmp);
            if(j < nargs-1)
                fputc(' ',fp);
            else
                break;
            ctmp++;
        }
        fprintf(fp,")\n");
#ifdef  notdef
        if(strcmp(cp,"_main") == 0)
            break;
#endif
  
#ifdef  LARGECODE
        pc = MK_FP(context[2],context[1]);
#else
        pc = (void (*)())MK_FP(FP_SEG(pc),context[1]);
#endif
        context = cnext;
    }
    clrsymtab();
    fclose(fp);
}
static struct symtab *
findsym(pc)
void (*pc)();
{
    struct symtab *sp,*spprev;
    unsigned short seg,offs;
  
#ifdef  LARGECODE
    seg = FP_SEG(pc) - Codeseg;
#else
    seg = 0;    /* Small code, no segment */
#endif
    offs = FP_OFF(pc);
    spprev = NULL;
    for(sp = Symtab;sp != NULL;spprev = sp,sp = sp->next){
        if(sp->seg > seg || (sp->seg == seg && sp->offs > offs)){
            break;
        }
    }
    return spprev;
}
static void
clrsymtab()
{
    struct symtab *sp,*spnext;
  
    for(sp = Symtab;sp != NULL;sp = spnext){
        spnext = sp->next;
        free(sp->name);
        free(sp);
    }
    Symtab = NULL;
}
static void
rdsymtab(unused,name,p)
int unused;
void *name;
void *p;
{
    char *buf;
    FILE *fp;
    unsigned short seg;
    unsigned short offs;
    struct symtab *sp;
    struct symtab **spp;
    int size = 0;
    int i;
  
    if((fp = fopen(name,"rt")) == NULL ||
       (buf = (char *)malloc(128)) == NULLCHAR ){
        printf("Stktrace: can't read %s\n",name);
        return;
    }
    while(fgets(buf,128,fp),!feof(fp)){
        rip(buf);
        if(strcmp(buf,"  Address         Publics by Value") == 0)
            break;
    }
    if(feof(fp)){
        printf("Stktrace: Can't find header line in %s\n",name);
        free(buf);
        fclose(fp);
        return;
    }
    Symtab = NULL;
    while(fgets(buf,128,fp),!feof(fp)){
        rip(buf);
        if(sscanf(buf,"%x:%x",&seg,&offs) != 2)
            continue;
        if ((sp = (struct symtab *)malloc(sizeof(struct symtab))) == (struct symtab *) 0)
            break;  /* out of memory */
        sp->offs = offs;
        sp->seg = seg;
        sp->name = j2strdup(buf+17);
        sp->next = Symtab;
        Symtab = sp;
        size++;
    }
    fclose(fp);
    free(buf);
#ifdef  notdef
    printf("Stktrace: Symbols read: %d\n",size);
#endif
    /* Sort the symbols using the quicksort library function */
    if ((spp = malloc(size*sizeof(struct symtab *))) == (struct symtab **)0)
        return;  /* symbols remain unsorted... */
    for(i=0,sp = Symtab;sp != NULL;i++,sp = sp->next)
        spp[i] = sp;
    qsort(spp,size,sizeof(struct symtab *),scompare);
    /* Now put them back in the linked list */
    Symtab = NULL;
    for(i=size-1;i >= 0;i--){
        sp = spp[i];
        sp->next = Symtab;
        Symtab = sp;
    }
    free(spp);
#ifdef  notdef
    for(sp = Symtab;sp != NULL;sp = sp->next)
        printf("Stktrace: %x:%x   %s\n",sp->seg,sp->offs,sp->name);
#endif
}
static int
scompare(a,b)
struct symtab **a,**b;
{
    if((*a)->seg > (*b)->seg)
        return 1;
    if((*a)->seg < (*b)->seg)
        return -1;
    if((*a)->offs > (*b)->offs)
        return 1;
    if((*a)->offs < (*b)->offs)
        return -1;
    return 0;
}
/* Print a code address according to the memory model */
static void
paddr(pc,fp)
void (*pc)();
FILE *fp;
{
#ifdef  LARGECODE
    fprintf(fp,"%04x:%04x",FP_SEG(pc) - Codeseg,FP_OFF(pc));
#else
    fprintf(fp,"%04x",FP_OFF(pc));
#endif
}
  
#endif /* STKTRACE */
