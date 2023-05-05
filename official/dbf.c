/*
   Get info about dBase files.  Safely ignores memo fields.

   Peter Crawshaw, June 28, 1993
   Using Borland C++ 3.1
*/

#define WORDSFILE "DBFWORDS.TXT"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "global.h"
#ifdef GOPHER
#include "dbf.h"
#include "socket.h"

dbf_fieldrec fields[MAX_FIELDS];   /* max no. of fields */
/* return results in regular ascii text or in CSO format */
static int output_format = ASCIITEXT;

extern int gnamefix(int s,char *string);
extern int findport(int s,char *dir,char c);

int dbfmatch(char *target);
int DBFLog(char *fmt,...);
void printrec_across(int s, char *recbuf, int nflds);

int dbf_fields(int s, char *file, int OF)
{
   FILE *f;
   dbf_header dbfinfo;
   int i,port;
   char *p,Gdir[64];

   output_format = OF;        /* ASCIITEXT or CSOTEXT */

   p = j2strdup(file);
   if (p == NULL)
   {
     tprintf("j2strdup() failed: dbf_fields()\n");
     return 0;
   }

   /* open the file */
   if ((f = fopen(file, "rb")) == NULL)
   {
     tprintf("Cannot open file.\n");
     return(1);
   }

   /* get info from the file header */
   dbf_getheader(f, &dbfinfo);

   /* fill field info structure */
   dbf_getfields(f, &dbfinfo);

   gnamefix(s,p);
   for (i=1; i<=dbfinfo.nflds; i++)
   {
      if (output_format == CSOTEXT)
      {
	 usprintf(s, "-200:%d:%s:max %d Indexed Lookup Public Default",
			  i, fields[i].name, fields[i].length);

	  usputc(s, 10);
	 usprintf(s, "-200:%d:%s:%s.",
			  i, fields[i].name, fields[i].name);
	 usputc(s, 10);
      }
      else
      {
	port = findport(s,Gdir,'g');
	usprintf(s, "7%s\tq%s~%s\t%s\t%u\n",
		 fields[i].name, p, fields[i].name, Hostname,port);
      }
   }
   free(p);
   fclose(f);

   return 0;
}

void dbf_do_search(int s, char *file, int type ,char *database)
{
   FILE *f;
   char *fn, *fld = "NAME", *targ, *p, *cp;

   /* return results in regular ascii text or in CSO format */
   output_format = type;

   p = j2strdup(file);
   if (p == NULL)
   {
     tprintf("j2strdup() failed: dbf_do_search()\n");
   return;
   }

   /* if it's a CSO search then 'file' is in the form:
	"name=value" */
   strupr(p);
   if (output_format == CSOTEXT)
   {

      fn = j2strdup (database);
	if (strpbrk (p, "=") == NULL )
	{
	    targ = p;
	 }
	else
	{

	     fld = strtok(p, "=");
	     targ = strtok(NULL, "=");
      }
	cp = strrchr(targ, ' ');  /* get rid of trailing spaces */
	while (*cp == ' ')
	{
	    *cp = '\0';
	    cp--;
	}

	cp = targ;         /* get rid of leading space */
	while (*cp == ' ') cp++;
	targ = cp;
   }
   else
   {
      fn = strtok(p, "~");
      fld = strtok(NULL, "$");
      targ = strtok(NULL, "$");
   }

   gnamefix(s,fn);
   dbf_search(s, fn, fld, targ);
   if (output_format == CSOTEXT)
      free(fn);
   free(p);
}

int dbf_search(int s, char *file, char *field, char *pattern)
{
   FILE *f;
   dbf_header dbfinfo;
   int i;

   if (strlen(pattern) <3)
   {
	 if (output_format == CSOTEXT)
	  usprintf(s, "-515:search string must be at least 3 characters.\n500:did not understand PH query.\n200:Ok.\n");
       else
	  usprintf(s, "\nSearch string must be at least 3 characters\nTry a longer search string\n");
	return 0;
   }
   else
   if ((dbfmatch(pattern)) == 1)
   {
	if (output_format == CSOTEXT)
	usprintf(s, "-515:search string was matched too often.\n500:choose an alternative search string.\n200:Ok.\n");
      else
	usprintf(s, "\nSearch string was matched too often\nTry an alternative search string\n");
      return 0;
   }
   else
   /* open the file */
   if ((f = fopen(file, "rb")) == NULL)
   {
     tprintf("Cannot open file: %s\n", file);
     return(1);
   }

   /* get info from the file header */
   dbf_getheader(f, &dbfinfo);

   /* fill field info structure */
   dbf_getfields(f, &dbfinfo);

   /* read records */
   dbf_getrecords(s, f, &dbfinfo, pattern, get_fieldno(field));

   fclose(f);
   return 0;
}


int get_fieldno(char *field)
{
   int i;

   for(i=1; i<=MAX_FIELDS; i++)
     if (stricmp(fields[i].name, field) == 0)
	return i;

   return 0;
}

int dbfmatch(char *pattern)
{
  FILE *WorkFile;
  char Line[80];
  int result;

  if((WorkFile = fopen(WORDSFILE, "rt")) != NULLFILE){
    while (!feof(WorkFile))
    {
    fgets(Line, 80, WorkFile);
      strupr(Line);
      strupr(pattern);
    if (strstr(Line, pattern) != NULL)
      {
	result = 1;
	break;
      }
    }
  fclose(WorkFile);
  }
  else result = 0;
  return(result);
}


int dbf_getheader(FILE *f, dbf_header *H)
/*
   Reads the first 32 bytes of the file and fills in the header
   structure H, except for nflds, since we don't know it yet.
*/
{
   unsigned char buf[HEADER_SIZE];         /* buffer for header */

   /* read the first 32 bytes containing some structure info */
   if ((fread(buf, HEADER_SIZE, 1, f)) != 1)
   {
      tprintf("read error\n");
      return(1);
   }

   /* check to see if it's a dBase file */
   /* 0x03: normal dBase file */
   /* 0x83: dBase file with memo field(s) */
   H->type = buf[0];
   if ((H->type != 0x03) && (H->type != 0x83))
   {
     tprintf("not a dBase file.\n");
     return(1);
   }

   /* date the file was last updated (3 bytes) */
   H->year  = buf[1];
   H->month = buf[2];
   H->day   = buf[3];

   /* # of records - 4 bytes (long int) */
   H->nrecs = buf[4];
   H->nrecs |= buf[5] << 8;
   H->nrecs |= buf[6] << 16;
   H->nrecs |= buf[7] << 24;

   /* header length - 2 bytes (int) */
   H->hlen = buf[8] | (buf[9] << 8);

   /* record length - 2 bytes (int) */
   H->rlen = buf[10] | (buf[11] << 8);

   return 0;
}


int dbf_getfields(FILE *f, dbf_header *H)
/*
   Reads field records (32 bytes each) until a '\r' is reached after
   one of them.  Puts this information in the global array fields.
   Then fills in nflds in the header structure H.
*/
{
   int c, i, done;
   unsigned char buf[FIELD_REC_SIZE];

   /* get field info into array of fieldrec */
   /* note: fields[0] not used, info for field 1 is in fields[1] */
   done = 0;
   H->nflds = 0;
   while (!done)
   {
	/* read a field record  (32 bytes) */
	if ((c = fread(buf, FIELD_REC_SIZE, 1, f)) != 1)
	{
	   tprintf("read error\n");
	   return(1);
	}
	c = getc(f);     /* look ahead for 'end of field info' byte */
	if (c == '\r')
	  done = 1;
	else             /* if not done, put that byte back */
	  ungetc(c, f);
	H->nflds++;

	/* field name */
	memcpy( fields[H->nflds].name, buf, 11);
	fields[H->nflds].name[11] = '\0';
	/* field type */
	fields[H->nflds].type = buf[11];
	/* field length */
	fields[H->nflds].length = buf[16];
   }
   return 0;
}


int dbf_getrecords(int s, FILE *f, dbf_header *H, const char *target, int n)
/*
  Read the data records and displays records on the screen that match
  target on field n.
  Uses structure information in fields and the header structure H.
*/
{
   char *recbuf, *fldbuf, targbuf[79];
   int c, i, found;
   int index = 0;

   /* copy target string into a buffer and uppercase it */
   strcpy(targbuf, target);
   strupr(targbuf);

   /* allocate a buffer to hold one record */
   recbuf = (char *) mallocw(H->rlen);

   /* allocate a buffer for the field */
   /* A pointer to this is passed to 'match' so we don't have to allocate
   /* a buffer each time the loop executes */
   fldbuf = (char *) mallocw(fields[n].length+1);


   /* read the records */
   for (i=1; i <= H->nrecs; i++)
   {
	/* read a data record */
	if ((fread(recbuf, H->rlen, 1, f)) != 1)
	{
	   tprintf("read error\n");
	   return(1);
	}

	/* check for eof and errors */
	if (feof(f) || ferror(f))
	{
	   tprintf("EOF or error while reading data\n");
	   return(1);
	}

	/* search for a record */
	if (match(s, recbuf, fldbuf, targbuf, n, H->nflds, &index))
	   found = 1;
	pwait(NULL);            /* give up the CPU for a while */
    }

    free(recbuf);                 /* release memory for record buffer */
    free(fldbuf);
    if (found == 0)
    {
       if (output_format == CSOTEXT)
	  usprintf(s, "501:There are no matches to your query.\n200:Ok.\n");
       else
	  usprintf(s, "\nNo Records match with field %s = %s\n",
			  fields[n].name, target);
    }
    else
       if (output_format == CSOTEXT)
       {
	   usprintf(s, "200:Ok.\n");
	   usputc(s, 26);
       }

     return 0;
}


int match(int s, char *recbuf, char *fldbuf, const char *target,
				     int fld, int nflds, int *index)
/*
   Assume target has been converted to uppercase
*/
{
   int i;
   int found = 0;
   char *p;

   p = recbuf;
   p++;                                    /* skip record deletion marker */
   for (i=1; i < fld; i++)          /* move to the target field in buffer */
      p += fields[i].length;

   strncpy(fldbuf, p, fields[i].length);     /* get the field */
   fldbuf[fields[i].length] = '\0';          /* ensure null-terminated */
   strupr(fldbuf);      		     /* uppercase it */

   if (strstr(fldbuf, target) != NULL)       /* substring search for target */
   {
     found = 1;
     if (output_format == CSOTEXT)
     {
	 CSO_printrec(s, recbuf, nflds, ++(*index));
	 usputc(s, 10);
     }
     else
	 printrec(s, recbuf, nflds);
     }

     return found;
}


void printrec(int s, char *recbuf, int nflds)
/*
   Display a record one field per line.
   Uses information in the 'fields' global array.
*/
{
   char *p, *q;
   int i;

   p = recbuf;         /* point to the buffer containing the record */
   p++;               /* skip deletion marker */

   for (i=1; i<=nflds; i++)
   {
      if ( (fields[i].type != 'M') )      /* ignore memo fields */
      {
	  usprintf(s, "%s: ", fields[i].name);    /* field name */
	  if (fields[i].type == 'D')              /* date fields */
	  {
	     q = p;                               /* point to the field */
	     q+=2;                                /* skip 'century' */
	     j2send(s, q, 2, 0);   /* year */
	     usputc(s, '/');
	     q+=2;
	     j2send(s, q, 2, 0);   /* month */
	     q+=2;
	     usputc(s, '/');
	     j2send(s, q, 2, 0);   /* day */
	  }
	  else                                   /* others */
	  j2send(s, p, fields[i].length, 0);    /* field data */
	  usputc(s, '\n');
      }
      p = p + fields[i].length;          /* move to next field in buffer */
   }
   usputc(s, '\n');
}


void CSO_printrec(int s, char *recbuf, int nflds, int index)
/*
   Display a record in CSO nameserver format.
   Uses information in the 'fields' global array.
*/
{
   char *p, *q;
   int i;
   
   p = recbuf;         /* point to the buffer containing the record */
   p++;                /* skip deletion marker */

   for (i=1; i<=nflds; i++)
   {
      if ( (fields[i].type != 'M') )      /* ignore memo fields */
      {
	  usprintf(s, "-200:%d:%20s: ", index, fields[i].name);    /* field name */
	  if (fields[i].type == 'D')              /* date fields */
	  {
	     q = p;                               /* point to the field */
	     q+=2;                                /* skip 'century' */
	     j2send(s, q, 2, 0);   /* year */
	     usputc(s, '/');
	     q+=2;
	     j2send(s, q, 2, 0);   /* month */
	     q+=2;
	     usputc(s, '/');
	     j2send(s, q, 2, 0);   /* day */
	  }
	  else                                   /* others */
	     j2send(s, p, fields[i].length, 0);    /* field data */
	  usputc(s, 10);
      }
      p = p + fields[i].length;          /* move to next field in buffer */
   }
 usprintf(s, "-200:%d:%20s ", index," ");
}


void printrec_across(int s, char *recbuf, int nflds)
/*
   Display a record one field per line.
   Uses information in the 'fields' global array.
*/
{
   char *p, *q;
   int i;

   p = recbuf;         /* point to the buffer containing the record */
   p++;               /* skip deletion marker */

   for (i=1; i<=nflds; i++)
   {
      if ( (fields[i].type != 'M') )      /* ignore memo fields */
      {
	  if (fields[i].type == 'D')              /* date fields */
	  {
	     q = p;                               /* point to the field */
	     q+=2;                                /* skip 'century' */
	     j2send(s, q, 2, 0);   /* year */
	     usputc(s, '/');
	     q+=2;
	     j2send(s, q, 2, 0);   /* month */
	     q+=2;
	     usputc(s, '/');
	     j2send(s, q, 2, 0);   /* day */
	  }
	  else                                   /* others */
	     j2send(s, p, fields[i].length, 0);    /* field data */
	  usputc(s, ' ');
      }
      p = p + fields[i].length;          /* move to next field in buffer */
   }
   usputc(s, '\n');
}


int DBFLog(char *fmt,...)
/*
   Write stuff to the DBF log file.
   File is opened only when necessary.
*/
{
   va_list args;
   int len;
   FILE *f;
    
   f = fopen("dbf.log", APPEND_TEXT);
   va_start(args,fmt);
   len = vfprintf(f, fmt, args);
   va_end(args);
   fclose(f);

   return len;
}
#endif /* GOPHER */
