// June 15, 1994 - KF5MG - Jack Snodgrass
//
// This code was adapted/copied from the SAM.C code. The code to process
// the QRZ disk was taken from the QRZ CD-ROM. This code could work
// with the name and zipcode versions of the Callbook data, but as-is, it
// only processes callsigns.
// Modifications 1995 by N5KNX: use UMBs, display enhancements, bugfixes.

#include <ctype.h>
#ifdef MSDOS
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <io.h>
#include <fcntl.h>
#endif
#include <sys/stat.h>

#define RECLEN 200     /* arbitrary upper bound for a CALLBKC.DAT entry */
#include "global.h"
#ifdef QRZCALLB
#include "mbuf.h"
#include "socket.h"
#include "session.h"
#include "proc.h"
#include "netuser.h"
#include "commands.h"
#include "tty.h"
#include "config.h"
#ifdef XMS
#include "xms.h"
#endif
#ifdef UNIX
#include "unix.h"
#endif

extern char *Callserver;                        /* in buckbook.c */
int cb_lookup __ARGS((int s,char *,FILE *));	/* referenced from calldbd.c */

static int  qrzfind(char *,int s);       /* does the actual lookup */
static void parse_record(char *, int s); /* Parse QRZ data record */
static char *formatdate(char *);         /* Format QRZ yyddd-style date */

/* Global variables. */
char prettycall[7];
char *qrzdir;
char *qrzdrv;

/*     Taken from the QRZ CD-ROM disk.
 *     This block is located at the start of each index
 */
/*
 *     New Index Header Block Definition
 */
typedef struct {
  char  dataname[16];    /* Name of the data file            */
  char  bytesperkey[8];  /* Data Bytes per Index Item        */
  char  numkeys[8];      /* Number of items in this index    */
  char  keylen[8];       /* Length of each key item in bytes */
  char  version[8];      /* Database Version ID              */
} index_header;

/*
 *     Old Index Header Block Definition
 */
typedef struct {
  char  dataname[13];    /* Name of the data file            */
  long  bytesperkey;     /* Data Bytes per Index Item        */
  int   numkeys;         /* Number of items in this index    */
  int   keylen;          /* Length of each key item in bytes */
} old_index_header ;

/* return values:  1= Callbook Error, 2= not found, 0= okay */
#define QRZerr 1
#define QRZnotfound 2
#define QRZfound 0

int cb_lookup(s, str, fp)
int s;
char *str;
FILE *fp;
{
   return (qrzfind(str,s));
}

static int qrzfind(char *call_in, int s)
{
index_header     idxhdr;
old_index_header oldidxhdr;

FILE         *fp;
char         *buf;
char         *bufptr;
char         IndexFile[] = "callbkc.idx";
int          size;
int          bytesperkey;     /* Data Bytes per Index Item        */
int          numkeys;         /* Number of items in this index    */
int          keylen;          /* Length of each key item in bytes */
int          slots;
int          i,j,k,found,slotcnt;
int          workbufsize;     /* bytesperkey + fudge */
long         fpos;
long         frc;
char         temp[255];
char         *cp;
char         *cp2;
char         call[8];
struct stat  statbuf;


   if (strlen(call_in) > 6) {
      usprintf(s,"Callsign too long.\n");
      return QRZnotfound;
   }

   /* call needs to be blank filled. */
   strcpy(call,"       ");

   /* Pretty call holds the original, un-qrz-formatted callsign. */
   strcpy(prettycall, call_in);
   strupr(prettycall);

   /* Callsigns in the QRZ Index are stored in a weird format. They are */
   /* 6 characters in length ( padded with spaces ), in the format of   */
   /* ccdccc where the digit is always in the 3rd posistion. If the     */
   /* callsign is a 1-by-something callsign, the 2nd posistion will be  */
   /* blank. KF5MG will be stored as KF5MGb. N5VGC will be stored as    */
   /* Nb5VGC. ( the b are spaces )                                      */
   /*                                                                   */
   call[0] = call_in[0];
   i = j = 1;
   if(!isdigit(call_in[j]))
      call[i++] = call_in[j++];
   else
      call[i++] = ' ';
   if(isdigit(call_in[j])) {
      call[i++] = call_in[j++];
   } else {
      /* No digit found in position 2 or 3. */
      usprintf(s,"Error parsing callsign... %s\n", call_in);
      return QRZnotfound;
   }
   for ( ; j < strlen(call_in); j++)
      call[i++] = call_in[j];
   call[6]=0;
   strupr(call);


   if ((qrzdir = getenv("QRZPATH")) == NULLCHAR)
      qrzdir = "/callbk";
   if ((qrzdrv = getenv("QRZDRV")) == NULLCHAR)
#ifdef UNIX
      qrzdrv = "/cdrom";
#else
      qrzdrv = "D:";
#endif

   /* Open the index file.  We'll use it to tell us the name of the     */
   /* database file. We'll also find the database version and some      */
   /* other useful info.                                                */
   /*                                                                   */
   sprintf(temp,"%s%s/%s",qrzdrv,qrzdir,IndexFile);
   if((fp = fopen(temp,"rt"))==NULLFILE) {
      usprintf(s,"Error opening Index: %s\n",temp);
      return QRZerr;
   }

   size = fread(&idxhdr,sizeof(idxhdr),1,fp);
   if(size != 1) {
      usprintf(s,"Error reading Index Header.\n");
      fclose(fp);
      return QRZerr;
   }

   // Old Style Index has a '0' at pos 16 and 17.
   if((int)idxhdr.dataname[16] != 0) {
      // This is a 'new' style index
      bytesperkey = atoi(idxhdr.bytesperkey);   /* size of data area.   */
      numkeys     = atoi(idxhdr.numkeys);       /* # of keys in file.   */
      keylen      = atoi(idxhdr.keylen);        /* length of each key.  */
   } else {
      // This is an 'old' style index.
      // rewind the file and read the header using the old_index_header struct.
      rewind(fp);

      size = fread(&oldidxhdr,sizeof(oldidxhdr),1,fp);
      if(size != 1) {
         usprintf(s,"Error reading Index Header.\n");
         fclose(fp);
         return QRZerr;
      }
      bytesperkey = (int)oldidxhdr.bytesperkey; /* size of data area.   */
      numkeys     = oldidxhdr.numkeys;          /* # of keys in file.   */
      keylen      = oldidxhdr.keylen;           /* length of each key.  */
   }

   if(fstat(fileno(fp), &statbuf) != -1)
      usprintf(s, "QRZ! database of %s", ctime(&statbuf.st_mtime));

   /* This is a 10K ish buffer. Each 'key' in the index covers almost   */
   /* 10K of data. Once you find the correct key, you have to read the  */
   /* 10K chunk of data from the data file. You then start searching    */
   /* for the correct callsign. This code uses the same 10k buffer to   */
   /* scan the index file ( typically 40K so you might do 4 reads ) to  */
   /* save memory. Once you've found the correct key, you calculate the */
   /* offset into the database. You then read the data base into your   */
   /* 10K buffer.  (We fudge bytesperkey by RECLEN to avoid a truncated */
   /* last record in the buffer).                                       */
   /*                                                                   */
   workbufsize = bytesperkey+RECLEN;
#ifdef XMS
   bufptr      = mallocUMB(workbufsize);
   if (bufptr == NULL)  /* err, or not enough paras avail, so use malloc */
#endif
   bufptr      = malloc(workbufsize);     /* Get space for buffer */
   if (bufptr == NULL) {     /* no space */
      usprintf(s,"No space for %d byte buffer.\n",workbufsize);
      fclose(fp);
      return QRZerr;
   }

   slots       = bytesperkey / keylen;          /* Calculate # of slots */
   found       = 0;
   slotcnt     = 0;

   do {
      /* Point floating buf pointer to start of big buffer.   */
      buf      = bufptr;
      /* Read slots number of entries that are keylen in size */
      size = fread(buf,keylen,slots,fp);
      if(size == 0) {
         /* (probably EOF) usprintf(s,"Error reading Index file.\n"); */
         found = 2;
      }

      /* Start scanning Index buffer. If the data is less than your search */
      /* Value... keep going. If the Data is greater, then your done. You  */
      /* then subtract one from your slotcnt (unless it's an exact match)  */
      /* and that's the closet record to your data.                        */
      /*                                                                   */
      for(i=0;i<size;i++) {
        slotcnt++;
        strncpy(temp,(char *)buf,keylen);
        temp[keylen] = 0;
        if(strncmp(&temp[3],&call[3],3) >= 0) {
           if(strncmp(&temp[3],&call[3],3) > 0) {
              found = 1;
              slotcnt--;
           }
           else {  /* last 3 equal. How about digit? */
              if(strncmp(&temp[2],&call[2],1) >= 0) {
                 if(strncmp(&temp[2],&call[2],1) > 0) {
                    slotcnt--;
                    found = 1;
                 }
                 else /* digits are equal. Check first 2 chars */
                    if(strncmp(&temp[0],&call[0],2) > 0) {
                       slotcnt--;
                       found = 1;
                    }
              }
           }
        }
        if(found)
           break;
        buf += keylen;
      }
   } while(!found); /* enddo */

   slotcnt--;

   /* We're done with the Index so we can close it. */
   fclose(fp);

   /* If we found a match, calculate the offset and read the data. */
   if(found == 1) {
      found    = 2;  /* start again with not-found flag */
      buf      = bufptr;
      sprintf(temp,"%s%s/%s",qrzdrv,qrzdir,idxhdr.dataname);
      if((fp = fopen(temp,"rt"))==NULLFILE) {
         usprintf(s,"Error opening Database: %s\n",temp);
         free(bufptr);
         return QRZerr;
      }

      /* seek to correct posistion in file .                           */
      fpos = (long) bytesperkey*slotcnt;

      frc = lseek(fileno(fp),(long) fpos,SEEK_SET);
      if(frc < 0L) {
         usprintf(s,"Error seeking Database file.\n");
         free(bufptr);
         fclose(fp);
         return QRZerr;
      }

      /* Read slot from callbk file. */
      size = fread(buf,workbufsize,1,fp);
      if(size < 1) {
         usprintf(s,"Error reading Database file.\n");
         free(bufptr);
         fclose(fp);
         return QRZerr;
      }

      /* Done with data file. Now we can close it too. Our target is either */
      /* in our buffer or doesn't exist.                                   */
      fclose(fp);

      i = 0;
      for(;;) {
         if(i>workbufsize)
            break;
         k = min(strcspn(buf,"\n"), sizeof(temp)-1);  /* n5knx: defensive */
         strncpy(temp,buf,k);
         temp[k] = 0;
         buf += k+1;  /* advance past \n */

         /* Find our user. */
         if(strncmp(temp,call,6) == 0) {
            /* Found it. Now read and format the data. */
            found = 1;
            parse_record(temp,s);
            break;
         }
         i+=k+1;
      }
   }
   /* Done with the buffer. */
   free(bufptr);
   return (found==1)?QRZfound:QRZnotfound;
}

static void parse_record(char *record, int s) {
/*
 *    Standard Record Format: comma-separated fields:
 */
char *callsign;         /* Call Sign (sort-ordered) */
char *lastname;         /* Last Name */
char *namesuffix;       /* Name Suffix */
char *frstname;         /* First Name */
char *middleinit;       /* Middle Initial */
char *datelicensed;     /* Date Licensed yyddd */
char *dateborn;         /* Date Born */
char *dateexpires;      /* Date License expires */
char *streetaddr;       /* Street Address */
char *city;             /* City */
char *state;            /* State Code */
char *zipcode;          /* Zip Code */
char *license_class;    /* License Class */
char *prevcall;         /* Previous Call */
char *prevclass;        /* Previous Class */

char *cp, *cp1;
char fullname[80];
char address[80];

#ifdef QRZDEBUG
  usprintf(s, "debug: %s\n", record);
#endif
  cp = record;

#define FIELD(P)	if ((cp1=strchr(((P)=cp),','))!=NULLCHAR) {*cp1++ = 0; cp = cp1;}

  FIELD(callsign);

  if(strlen(cp) < 8) {
     usprintf(s,"%s is an old call for %s\n", prettycall, cp);
     return;
  }

  FIELD(lastname);

  FIELD(namesuffix);
  if (!strcmp(namesuffix,".")) {  /* Newer QRZ cdroms flag email addr existence this way */
    namesuffix="";   /* ignore .; someday display email addr */
  }

  FIELD(frstname);

  FIELD(middleinit);

  FIELD(dateborn);

  FIELD(datelicensed);

  FIELD(dateexpires);

  FIELD(streetaddr);

  FIELD(city);

  FIELD(state);

  FIELD(zipcode);

  FIELD(license_class);

  FIELD(prevcall);

  FIELD(prevclass);

  sprintf(fullname,"%s%s%s%s%s%s%s",frstname, *middleinit ? " ":"",middleinit,
      *middleinit ? ". ":" ", lastname, *namesuffix ? ", ":" ", namesuffix);
  sprintf(address, "%s, %s, %s", city, state, zipcode);

  usprintf(s,"\n%-8s", prettycall);
  usprintf(s,"%-45s", fullname);
  if (*dateborn)
      usprintf(s,"Born:    %s\n", formatdate(dateborn));
  else usputc(s,'\n');

  usprintf(s,"%-8s%-45s","", streetaddr);
  if (*datelicensed || *license_class)
      usprintf(s,"Class: %s %s\n", (*license_class?license_class:" "), formatdate(datelicensed));
  else usputc(s,'\n');
 
  usprintf(s,"%-8s%-45s", "", address);
  if (*prevclass || *prevcall)
      usprintf(s,"Prev:  %s %s\n", prevclass, prevcall);
  else usputc(s,'\n');

  if (*dateexpires)
      usprintf(s,"%52s Expir:   %s\n", "", formatdate(dateexpires));

  return;
}

/* Convert numeric yyddd date format to mm/dd/yy format */
static char *
formatdate(char *date){
char year[5];
char rest[4];
int  mon;
int  day;
int  days;
int  years;
int  i;

int  dayarray[13] = { 0, 31,  60,  91, 121, 152, 182,  /* 0 permits handling 00000 better */
                     213, 244, 274, 305, 335, 366 };
static char pretty_date[12];

   strncpy(year,date,2);
   year[2]  = 0;
   years    = atoi(year);
#ifdef notdef
   if(years < 20)   /* fails for DOB, where 15 => 1915. -- n5knx */
      years += 2000;
   else
      years += 1900;
#endif

   strncpy(rest,&date[2],3);
   rest[3]  = 0;
   days     = atoi(rest);

   if((years % 4) != 0)   /* fails in 2100 */
      if(days > 59)
         days++;

   for(i=1;i<13;i++){
      if(days <= dayarray[i]) {
         mon = i;
         day = days - dayarray[i-1];
         break;
      }
   }
   sprintf(pretty_date, "%02d/%02d/%02d", mon,day,years);
   return(pretty_date);
}
#endif
