/* memlog.c ... standalone program
   Analyze memlog.dat file produced by Jnos 1.10H+ compiled with
   MEMLOG defined.   Mucho code taken from stktrace.c!   -- N5KNX

   Essentially, with MEMLOG defined, main.c will create a memlog.dat
   file in the root dir, and alloc() and free() will record which routine
   did the alloc/free in this file.  When memlog.dat is scanned by this
   program, allocs without a later free are displayed.  To find which
   line in the source corresponds to the indicated address, something
   like this might prove useful:  bcc +bcc.cfg -B -Tla -y -c PROG.c
   Then look in PROG.lst for the indicated offset and src line number.

   Remember that all this logging takes a lot of resources (disk, cpu time)
   and may not be suitable for slow machines.  It's also possible that
   the added stack usage by the MEMLOG code might overflow a marginally-sized
   stack, but I've not seen this (yet).

   Usage:  memlog  <path_to_MEMLOG.DAT>  <path_to_NOS.MAP>
*/

#include "global.h"
#include <conio.h>
#include <dos.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <alloc.h>
#include <errno.h>

#undef fopen

struct memrecstruct {
    char alloc_or_free;
    char pad;
#ifdef  LARGECODE
    int16 caller_segment;
#endif
    int16 caller_offset;
#ifdef  LARGEDATA
    int16 ptr_segment;
#endif
    int16 ptr_offset;
/*    time_t   time;
/*    unsigned long filled_size;
/*    unsigned int request_size;
*/
};
static struct memrecstruct MemRec;

struct alloc_chain {
   struct alloc_chain *next;
   struct alloc_chain *prev;
   struct memrecstruct MemRec;
};

#define NULLCHAIN (struct alloc_chain *)NULL
struct alloc_chain *Head = NULLCHAIN, *p;

struct symtab {
    struct symtab *next;
    unsigned short seg;
    unsigned short offs;
    char *name;
};
static struct symtab *Symtab;
static void rdsymtab (int unused,void *name,void *p);
static void clrsymtab (void);
static struct symtab *findsym (void (*)());
static int scompare();
static void enchain(void);
static void dechain(void);
static void rip(char *s);

void enchain(void)
{
	if ((p = malloc(sizeof(struct alloc_chain))) == NULL) {
		fprintf (stderr, "Allocation failed ... need more memory!\n");
		return;
	}
        memcpy((void *)&(p->MemRec), (void *)&MemRec, sizeof(struct memrecstruct));
	p->next = Head;
	p->prev = NULL;
	if (Head != NULLCHAIN) Head->prev = p;
	Head = p;
}

void dechain(void)
{
	for (p = Head; p; p=p->next) {
		if (p->MemRec.ptr_segment == MemRec.ptr_segment &&
		    p->MemRec.ptr_offset == MemRec.ptr_offset ) {
			if (p->next) p->next->prev = p->prev;
			if (p->prev) p->prev->next = p->next;
			if (p == Head) Head = p->next;
			free(p);
			break;
		}
	}
	if (p == NULLCHAIN)
		 printf("Free without previous allocation: %04X:%04X\n",
			MemRec.ptr_segment, MemRec.ptr_offset);
}


void main(argc, argv)
int argc;
char *argv[];
{
   FILE *fp;
   int  cnt;
   struct symtab *sp;
   void (*pc)();

   if (argc != 3) {
	fprintf(stderr, "Usage: %s MEMLOG.DAT  NOS.MAP\n", argv[0]);
	return;
   }

   fp = fopen(argv[1], "rb");

   for(;;) {
      cnt = fread(&MemRec, 1, sizeof(struct memrecstruct), fp);
      if (cnt != sizeof(struct memrecstruct))
         break;
/*
      timeptr = localtime(&MemRec.time);
      strftime(str, sizeof(str), "%H:%M:%S %y\\%m\\%d", timeptr);
*/
#ifdef notdef		
	if (MemRec.alloc_or_free == 'A')
	      printf("A %04X:%04X %04X:%04X\n", MemRec.caller_segment, MemRec.caller_offset,
              	MemRec.ptr_segment, MemRec.ptr_offset);
	else printf("F           %04X:%04X\n", MemRec.ptr_segment, MemRec.ptr_offset);
#endif

	if (MemRec.alloc_or_free == 'A') enchain ();
	else if (MemRec.alloc_or_free == 'F') dechain();
        else {
		fprintf(stderr, "File format error ... aborting\n");
		break;
	}
   }
   if (errno) perror("reading input file");

   fclose(fp);

   if (Head != NULLCHAIN) {
     rdsymtab(0,(void *)argv[2],(void *)NULL);
     fprintf(stderr,"After symbol table loaded, coreleft = %lu\n", coreleft());

     printf ("Remaining allocations:\nBY\t\t\tAT\n");

     for (p = Head; p; p=p->next) {
	 printf("%04X:%04X", p->MemRec.caller_segment, p->MemRec.caller_offset);
	 pc = MK_FP(p->MemRec.caller_segment, p->MemRec.caller_offset);
         sp = findsym(pc);
         if(sp != NULL)
            printf(" %s+%x",sp->name,p->MemRec.caller_offset - sp->offs);
	 else printf("\t");
	 printf("\t%04X:%04X\n", p->MemRec.ptr_segment, p->MemRec.ptr_offset);
     }
  }
}

static struct symtab *
findsym(pc)
void (*pc)();
{
    struct symtab *sp,*spprev;
    unsigned short seg,offs;
  
#ifdef  LARGECODE
    seg = FP_SEG(pc);
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
  
    if((fp = fopen(name,"rt")) == NULL){
        fprintf(stderr,"Memlog: can't read %s\n",name);
        return;
    }
    if ((buf = (char *)malloc(128)) == NULL) {
        fprintf(stderr,"Memlog: can't malloc(128).\n");
        return;
    }
    while(fgets(buf,128,fp),!feof(fp)){
        rip(buf);
        if(strcmp(buf,"  Address         Publics by Value") == 0)
            break;
    }
    if(feof(fp)){
        fprintf(stderr,"Memlog: Can't find header line in %s\n",name);
        free(buf);
        return;
    }
    Symtab = NULL;
    while(fgets(buf,128,fp),!feof(fp)){
        rip(buf);
        if(sscanf(buf,"%x:%x",&seg,&offs) != 2)
            continue;
        sp = (struct symtab *)malloc(sizeof(struct symtab));
	if (sp == NULL) {
	    fprintf(stderr,"Memlog: can't malloc a symtab entry.\n");
            return;
        }
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
    spp = malloc(size*sizeof(struct symtab *));
    if (spp == NULL) {
        fprintf(stderr,"Memlog: can't malloc %d symtab entries.\n", size);
	return;
    }
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

/* replace terminating end of line marker(s) with null */
static void
rip(s)
register char *s;
{
    register char *cp;
  
    if((cp = strchr(s,'\n')) != NULL)
        *cp = '\0';
}
