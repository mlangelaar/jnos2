#include <time.h>
#include <sys/timeb.h>
#ifdef MSDOS
#include  <dos.h>
#include <alloc.h>
#endif
#include <ctype.h>
#include <fcntl.h>
#ifdef MSDOS
#include <io.h>
#endif
#include "global.h"
#if defined CALLSERVER || defined ICALL || defined CALLCLI || defined BUCKTSR
#include "files.h"
#include "config.h"
#include "socket.h"
#include "cmdparse.h"
#include "ftp.h"
  
/*
    Fred Peachman KB7YW
    7186 Northview Drive
    Brookfield, OH 44403
  
    Fri  05-08-1992
    OK here is my "final" product for the Buckmaster "HamCall" CD-ROM based
    callsign server.
  
   It is a substitute for N4PCR's callsign server. The intent is to replace
   N4PCR's callbook.c with this file - and have a callsign server that will
   run off Buckmaster's CD-ROM: the October 1991 "HAM CALL" CD.(or May  '92)
  
   The CD-ROM must be configured with a driver installed in config.sys,
   and the mscdex.exe TSR must be running.
  
   This module can be compiled one of three ways.
  
    In config.h:
  
   - If CALLSERVER is defined, but ICALL is not:
      you get the same sort of callsign server that I did for the
      October '91 Ham Call CD. It is compatible with either the October
      '91 CD or the May '92 CD. The October '91 CD can utilize the
      Canadian Callsign database as well as the U.S. The May '92 CD does
      not provide the separate Canadian callsign database so the
      "callserver database canadian" command should not be used in
      autoexec.nos. One new feature to this old server is provision for
      "cross-referencing", a feature that is new with May '92 HamCall CD
      but not on the October '91 CD.
  
   - If ICALL is defined, you get the international callsign database
      that is new for the May '92 HamCall CD. The international callsign
      database contains U.S., Canadian and a smattering of foreign callsigns.
      It is based on shelling out to Buckmaster's icall.exe utility which
      is located in the root directory of the May '92 CD. THIS option is
      NOT available on the October '91 HamCall CD!!
  
        To set it up, you use the "cdrom" command to tell NOS the drive
      letter of your CD-ROM drive. You set yourself up as the callserver
      with "callserver hostname <hostname>". You "start callbook". That's it!
  
      - de KB7YW
  
   - If BUCKTSR is defined:
      you get the international callsign database
      that is new for the April '95 HamCall CD. The international callsign
      database contains U.S., Canadian and many foreign callsigns.
      It is based on invoking Buckmaster's bucktsr.exe tsr which is obtained
      from Buckmaster or elsewhere.  The tsr MUST be loaded prior to running
      Jnos, and takes up about 25 KB.
  
        To set it up, you use the "cdrom" command to tell NOS the drive
      letter of your CD-ROM drive. You set yourself up as the callserver
      with "callserver hostname <hostname>". You establish the software IRQ
      used to communicate with bucktsr.exe by "callserver tsrirq 102" (or
      whatever you used after the /I option to bucktsr.exe.
      You then "start callbook". That's it!
  
      - de N5KNX
  
   ---------------------------------------------------------------------------
   - for CALLSERVER:
   Wed  04-29-1992 Mods for the May '92 callbook server. Buckmaster does
   not provide a separate Canadian callsign database on this CD. So this
   code will only be good for the USA database: "s:\\ham0\\hamcall.129".
   "Cross-referencing" works: For a callsign which has changed, the old
   call will be displayed, the date it was changed will be given, and the
   amateur's directory listing under his new callsign will be given. As
   for ICALL, since this is a simple shell to Buckmaster's icall.exe
   utility, I cannot add cross-referencing!
  
   The callserver database commands will configure the cd-rom.
  
   config.h must #define CALLCLI or CALLSERVER. If CALLSERVER is #define'd
   CALLCLI is forced on by config.h. Only CALLCLI can be #define'd separately
   of CALLSERVER so it is the default condition for compilation of this
   module - which won't used at all if neither CALLCLI nor CALLSERVER are
   #define'd in config.h.
 */
  
/* if ICALL is defined, it forces CALLSERVER to be defined. However, if
  CALLSERVER is defined, it does not force ICALL. Therefore definition of
  ICALL controls which server code gets compiled.
  
  If neither CALLSERVER nor ICALL is defined, this module might still be
  needed if CALLCLI is defined, for the callserver client code.
  
 */
struct months {
    char *m;  /* name of month  */
    int  d;   /* days in month  */
};
  
#ifdef ICALL
#ifndef CALLSERVER
#define CALLSERVER 1
#endif    /* #ifndef CALLSERVER */
#endif    /* #ifdef ICALL */
  
char  *Callserver = NULLCHAR;
static int
docallserver_hostname(int argc,char *argv[],void *p);
  
#ifdef BUCKTSR
static int
docallserver_tsrirq(int argc,char *argv[],void *p);
#endif /* BUCKTSR */
  
#if defined(CALLSERVER) || defined(BUCKTSR)
char *CDROM = NULLCHAR;
#endif

#ifdef CALLSERVER
static int
docallserver_databases(int argc,char *argv[],void *p);
  
/* CD-ROM code by Fred Peachman KB7YW */
char *cdrom_prompt =
"\tUse the \"cdrom\" command to specify CD-ROM's drive letter\n";
struct months month[12] = {
    {"January", 31},{"February",28},{"March",31},{"April",30},{"May",31},
    {"June",30},{"July",31},{"August",31},{"September",30},{"October",31},
    {"November",30},{"December",31}
};
  
#ifndef ICALL
/* What follows is the original stuff for the Oct '91 Ham Call CD:  */
  
/*struct database_activity
  One of these structs gets filled out by init -
  initializes data base for calls from hamcall.c -
  should be able to handle generic functions across different data base
  formats.
 */
struct database_activity {
    int type;                     /* for checking data/function formats*/
#define USA 0
#define CANADA 1
    int  handle;                  /* -1 if unitialized  */
    unsigned long record;         /* current record #                 */
    void *record_buf;           /* where to read the record to      */
};
  
/* The Buckmaster HAM CALL CD format for U.S. Callsign database       */
struct usa  {
    char callsign[6];   /* None of these fields are ASCIIZ so be careful */
    char lastname[24];
    char firstname[11];
    char middle[1];
    char street[35];
    char city[20];
    char state[2];
    char zip[5];
    char fluff[8];      /* supreme nothingness                            */
    char exp[2];        /* 'X3' if license expired                        */
    char born[2];       /* Last two digits of year of birth  i.e. '54     */
    char expiryr[2];    /* Last two digits of year of license expiration  */
    char expirday[3];   /* The day of the year that license expires       */
    char validyr[2];    /* year that license was issued                   */
    char validday[3];   /* day of the year that license was issued        */
    char class[1];      /* 1 letter license classification                */
    char endline[2];    /* file marker                                    */
};
  
/* The Buckmaster HAM CALL CD format for Canadian Callsign database
   It is 0x9a bytes long (154) The first record has a blank callsign field
   that should be no problem!                                             */
  
struct canada  {
    char callsign[8];
    char name[0xe8 - 0xa2];
    char street[0x10a - 0xe8];
    char town[0x121-0x10a];
    char province[0x12c-0x121];
    char zip[6];
    char endline[2];
};
                    /* generic function calls: headers */
static int
init(struct database_activity *db,int type,int s);
static int
close_down(struct database_activity *db);
static void
disp(char *cfield, int siz, int s);
static int
readrec(struct database_activity *db);
static int
displayrec(struct database_activity *db, int s);
static int
find_call(struct database_activity *db, char *callsign);
static int
make_mask(char *mask, char *callsign, int s);
  
                        /* for u.s.a database:  */
static int
display_usa(void *,int s);
static int
comp_usa_calls(char *, char *);
  
                        /* for canada database  */
static int
display_canada(void *,int s);
static int
comp_canada_calls(char *, char *);
  
/* most of this module is server code, but some is client: CALLCLI is by
   default - forced on in config.h whenever CALLSERVER is #define'd */
  
/* How many different databases are we supporting right now?              */
#define DATABASE_TYPES 2
  
/* how many passes should we make before we give up (in find_call)?    */
#define MAXREADS 30
/*----------------------------------------------------------------------------
  
    Here are arrays of variables to use depending on which database is
    needed. In each array make sure the value order is same as defs for
    database_activity.types i.e. USA, CANADA, ....
  
    In order to tack on another database, add an element for each of these
    arrays, increment the definition for DATABASE_TYPES (above), and
    tell your program how to tell init to use the new one.  In this
    module the launcher (cb_lookup) will figure out which database to
    initialize ("init(type, callsign,s)").
  
    Basically for the new database you would only need to specify its file
    name in database[], write a display function and specify it in
    displayfunc[], specify the size of one record in record_size[],
    write a callsign comparison function and specify it in compare_calls[],
    and specify the byte offset of the callsign field within a record -
    specify this offset in call_offset[] - it will probably be 0 anyway.
  
    You need to write something in cb_lookup to tell init when to initialize
    your new database. Basically if "call" satisfies inspection, initialize
    the appropriate database.
 */
  
char *database[DATABASE_TYPES]  = {
    { NULLCHAR  }, /* "S:\\HAM0\\HAMCALL.129"},  USA  */
    { NULLCHAR  }  /* "S:\\HAM0\\CANADA2.DAT"}   CANADA */
};
  
char *database_label[DATABASE_TYPES]  = {
    { "usa"},{"canadian"}
};
  
int (*displayfunc[DATABASE_TYPES])(void *, int) = {
    { display_usa },
    { display_canada }
};
  
/* basic record size, in bytes, in each database  */
int record_size[DATABASE_TYPES] =   {   129, 154  };
  
            /* How fresh is this stuff in the database?
            Wed  04-29-1992  Now handled by getftime  - KB7YW
static char *Volume = "October 1991";
              */
  
            /* number of records in each type of database */
unsigned long records[DATABASE_TYPES];
  
       /* The database-specific callsign comparison procedure  */
int (*compare_calls[DATABASE_TYPES])(char *,char *) = {
    {comp_usa_calls},{comp_canada_calls}
};
/*      At what offset within a database struct is the callsign located?
               Look at struct usa and struct canada above
 */
int call_offset[DATABASE_TYPES] = { 0,0 };
  
/*
char *BusyServer = "\n\nThe callsign server is busy right now\n\
.... please call back later\n\n";
 */
  
char *Db_Unavail = "\n\nDatabase is not available.\n";
  
char *Db_Not_config = "\n\nDatabase is not configured.\n";
  
static char *TAB = "        ";
  
/*--------------------------------------------------------------------------*/
/* --------------- Generic (database-independent) functions ----------------*/
/*------------------------------------------------------------------------
  Initialize a database activity struct. this consists of making sure
  the database file is opened and that the number of records in that
  file is computed. Also a record_buf is malloced for each db.
 */
static int
init(db, type, s)
struct database_activity *db;
int type;
int s;
{
    long tmp;
    int i;
    extern char *Callserver;  /* declared in calldbd.c  */
    if ((type < 0) || (type >= DATABASE_TYPES))  return -1;
  
    db->type = type;
  
    if (database[type] == NULLCHAR)  {
        usputs(s,Db_Not_config);
        return -1;
    }
  
    i = db->handle = _open(database[type], O_RDONLY | O_BINARY);
    if (i == -1) {
        usputs(s,Db_Unavail);
        return -1;
    }
    for (i = 0; i < 50; i++)  pwait(NULL);
  /* The CD-ROM is a slow device!! - give other processes lots of slack */
  
    tmp = filelength(db->handle);
/*   if (tmp == -1L)  {
    usprintf(s,"Error reading length of file \"%s\"\n\
    please report to the sysop at %s\n",database[type], Callserver);
    return -1;
  }
*/
    records[type] = tmp/((long) record_size[type]);
  
    db->record_buf = (char *)malloc(record_size[type]);
    if (db->record_buf == NULL) {
        usputs(s,Db_Unavail);  /* No memory to allocate, actually  */
        return -1;
    }
    return 0;
}
  
static int
close_down(db)
struct database_activity* db;
{
    free(db->record_buf);
    _close(db->handle);
    return 0;
}
static int
invalid_callsign(callsign, s)
char *callsign;
int s;
{
    usprintf(s,"\n\n\"%s\" is an invalid callsign\n\n", callsign);
    return -1;
}
  
/* make_mask makes a Buckmaster-compatible mask out of the callsign.
  caller must allocate 6 bytes exactly for "mask" or 7 to make an ascii
  string out of it.
  
  callsign is the callsign we are looking to match; mask is the same
  callsign only padded to Left with a space if necessary to keep the
  callsign digit at position 2 (starting with 0), and padded with spaces
  to the right if necessary to make sure the mask is exactly 6 digits
  long.
  
  This routine will work right for US or Canadian callsigns, but NOT with
  foreign callsigns (that contain more than one numeric character) it
  will fail!
 */
  
static int
make_mask(mask, callsign,s)
char *mask;
char *callsign;
int s;
{
    int j, k, len, already=0;
    char *q, *p;
    char tmp[7];
  
    len = strlen(callsign);
  
  /* there must be only ONE digit-character at position 1 or 2
      (starting from 0)  */
    q = callsign;
    already = 0;
    for (j = 0; j < len; j++)   {
        if (isdigit(*q++))  {
            if (!(already++)) k = j;/* already counts the number of digit chars */
            else return invalid_callsign(callsign,s);
        }
    }
    if ((k < 1) || ( k > 2))  return invalid_callsign(callsign,s);
  
/* FINALLY, Buckmaster pads to the left with spaces to position the digit at
  mask[2] */
  
    q = mask;
    for (j = 0; j < 6; j++) *q++ = ' '; /* initialize!  */
  
    p = callsign;
    q = mask;
  
    switch (k)  {
        case 1:
            *q++ = ' '; /* left-pad with a space  */
            break;
  
        case 2:
            break;
  
        default:
            break;
/*  usprintf(s,"Error in make_mask %s line %d\n",__FILE__,__LINE__);
    exit(255);
*/
    }
  
    for (j = 0; j < len; j++) *q++ = *p++;
  
    return ((int) (mask[2]) &0x0f); /* returns digit of the valid U.S. callsign */
}
  
/* a method for display of non-ASCIIZ strings */
static void
disp(cfield,siz,s)
char *cfield;
int siz;
int s;
{
    char *q;
    int i, sw=0;
  
    q = cfield;
    for (i = 0; i < siz; i++) {
/* Was:
    if ((!sw) || (*q != ' ')) usputc(s, (int) *q, stdout);
    Now is:  (WG7J) */
        if ((!sw) || (*q != ' ')) usputc(s, (int) *q);
        if (*q == ' ') sw = 1;
        else sw = 0;  /* We will print out one space before we quit!  */
        q++;
    }
}
readrec(db)
struct database_activity *db;
{
    unsigned long filepointer;
    int n;
    int st;
  
    st = db->type;
    filepointer = db->record*((long) record_size[st]);
    lseek(db->handle, filepointer,SEEK_SET);
    n = _read(db->handle, db->record_buf,record_size[st]);
    if (n != -1) return 0;
    else return -1;
}
  
static int
displayrec(db,s)
struct database_activity *db;
int s;
{
    int (*func)(void *v, int s);
  
    func = displayfunc[db->type];
/*   usprintf(s,"\n%s*** Record # % 8lu:***\n",TAB, db->record);  */
    return func(db->record_buf,s);
}
/* find_call: Find the record number that matches the callsign. For any
   callbook data where the records are all of the same length. The
   returned record value is found as db->record.
  
   If the input callsign needs to be converted to a search mask in any
   way, do it then call findcall.
  
   The returned int value is the number of records read during the search
   process.
 */
  
static int
find_call(db, callsign)
struct database_activity *db;
char *callsign;
{
    int i, ret, met= 0, reads = 0;
    long ra,rz;  /* first, middle, last record numbers */
    int (*comp)(char *, char *);
    int st;
    char *rec_call;
  
    st = db->type;  /* determine database structure type  */
    comp = compare_calls[st];
/* set initial pointers */
    ra = 0L;
    rz = records[st] - 1L;
    db->record = (ra + rz)/2L;  /* go for the middle  */
  
    rec_call = (char *)db->record_buf + call_offset[st];
    ret = 1;
    while (ret != 0)  {
        readrec(db);  /* read in the data pointed to by db->record  */
        for (i=0; i < 50; i++)
            pwait(NULL);  /* sit back and let other processes run for a while.
      The CD-ROM is a slow-access device so lots of other processes may
      be waiting or trying to run - like the keyboard!!! */
  
        reads++;
        ret = comp(callsign, rec_call);
  
        if (ret < 0)  /* callsign < record's callsign field  */
            rz = db->record - 1L;
        else if (ret > 0)  /* callsign > record's callsign field  */
            ra = db->record + 1L;
        else return reads;
  
        if (reads == MAXREADS) return -1;  /* Abnormal return  */
        else if (ra >= rz)  {
            if (met)  return -1;
            else  met = 1;  /* catch this if it happens a second time!  */
        }
        db->record = (ra + rz)/2L;
    }
  /* not reached  */
}
/*---------------- ROUTINES FOR U.S. CALLSIGN DATABASE ----------------------*/
  
/* getdaynum: given a character array 'p' of 'siz' elements, convert the
   array to an ASCIIZ string, return the integer value that it
   represents.
 */
static int
getdaynum(p,siz)
char *p;
int siz;
{
    char *q, tmp[80];
    int ret, i;
  
    q = p;
    for (i = 0; i < siz; i++) {
        tmp[i] = *q++;
    }
    tmp[i] = '\0';
  
    ret = atoi(tmp);
  
    return ret;
}
/* yrval:
   Take the two-letter year code and determine if we have a year that is in
   the 1900's or the 21st century. Return a proper integer answer:
   i.e. "2002" or "1998".
 */
static int
yrval(yrin)
char *yrin;
{
    int i, num;
  
    num = getdaynum(yrin, 2);
  
    if (num > 90) num += 1900;
    else num += 2000;
    return num; /* an integer */
}
/* day_calc: given the 3-element character array 'day' and the 2-element
   character array 'yr', construct a string that returns the date for
   that year, in the form "February 28, 2002" */
  
static char *
day_calc(day, daystr, yr)
char day[3];
char *daystr;
char yr[2];
{
    int i, daynum, subt, year, t;
    char *q, tmp[4];
    int leap = 0;
  
    year = yrval(yr);
  
    if (!(year%4))  leap = 1; /* evenly divisible by four means leap year */
  
    daynum = getdaynum(day, 3);
    if (daynum == 0)  return NULL;
  
  
    subt = 0;
    for (i = 0; i < 12; i++)  {
        if (((t = subt + ((i == 1) ? leap : 0) + month[i].d)) > daynum) break;
        else (subt = t);
    }
    t = daynum - subt;
    if (i == 12)  daystr[0] = '\0';
    else  sprintf(daystr,"%s %d, %d", (t > 0 ? month[i].m : month[--i].m),
        (t > 0 ? t : month[i].d + ((i==1) ? leap : 0)),
        year);
    return daystr;
}
/* comp_usa_calls compares the sought-for callsign with a mask of the
   type encountered in the callsign field of a struct usa,
  
  returns 0 if they match, -1 if mask1 < mask2, 1 if mask1 > mask2.
  mask1 is the callsign requested, mask2 is the database callsign from
  the record under test for match.
 */
static int
comp_usa_calls(mask1, mask2)
char mask1[6];
char mask2[6];
{
    int i;
    char *p, *q;
  
  /* This is in accordance with the way that Buckmaster orders callsigns
     on its CD-ROM database, so follow along:
   */
  
    p = &mask1[2], q = &mask2[2];
    for (i =2; i < 6; i++)  {
        if (*p != *q)  return (int) *p - (int) *q;
        p++, q++;
    }
    p = mask1, q = mask2;
    for (i =0; i < 2; i++)  {
        if (*p != *q)  return (int) *p - (int) *q;
        p++, q++;
    }
    return 0;
}
  
/* for display of an 'X' record in a US callsign record Apr '92 HamCall CD.
  first three elements of the usa struct look like this:
  
       callsign: ùN8MVI
       lastname: ùCHANGEDù08/27/91ùùùùùùù
      firstname: ùSEEùKF8PHù
  
      where 'ù' is a placeholder to show size only.
      Let's do an appropriate display of this information.
  */
  
static int
display_x(us,s)
struct usa *us;
int s;
{
/*
  in us->lastname, ["ùCHANGEDù08/27/91ùùùùùùù"] us->lastname[17] is where
  to ASCIIZ-ize it. us->callsign is not NULL-terminated, and us->lastname
  follows it immediately. So we will just print out the first two elements
  of the structure usa:
 */
    us->lastname[17] = '\0';
/* A NULL at us->middle will ASCIIZ-ise us->firstname, of which we want
  all past the "SEE".
 */
    us->middle[0] = '\0';
    usprintf(s,"%s\nNew callsign is \"%s\"\n\n", us->callsign, &us->firstname[5]);
    return 0;
}
/*
    Old U.S. callsign display format:
    Buckmaster CD-ROM October 1991
      *** Record #   452100:***
Call:   KB7YW
        FREDERICK A PEACHMAN
        7186 NORTHVIEW DR
        BROOKFIELD , OH 44403
Born:   1954
Class:  EXTRA
Exp:    March 29, 1998
 */
static int
display_usa(us,s)
void *us;
int s;
{
    struct usa *u;
    char daystr[20];
    int yeardate;
    char *Unknown = "%sUnknown Expiration Date\n";
  
/* Sat  05-02-1992  Here I am inserting code to recognize license class 'X',
  which is Buckmaster's way of saying that the license has expired because
  the licensee has changed his callsign - for whatever reason. This is only
  a possibility with the USA database and then only for CD's dated april
  '92 or later. See 'display_x()', above.
    I have commented out my old display format. Gonna simplify it
  somewhat - KB7YW */
  
    u = (struct usa *)us;
  
    if (u->class[0] == 'X')  {
        display_x(us,s);
        return -2;  /* -2 is a flag for the caller to recognize an 'X' record */
    }
/*   usprintf(s, "CALL:   "); */
    disp(u->callsign,sizeof(u->callsign),s);
/*   usprintf(s,"\n%s", TAB); */
    usputc(s,'\n');
    disp(u->firstname,sizeof(u->firstname),s);
    disp(u->middle,sizeof(u->middle),s);
    usputc(s,' ');
    disp(u->lastname,sizeof(u->lastname),s);
/*   usprintf(s,"\n%s", TAB); */
    usputc(s,'\n');
    disp(u->street,sizeof(u->street),s);
/*   usprintf(s,"\n%s", TAB); */
    usputc(s,'\n');
    disp(u->city,sizeof(u->city),s);
    usputs(s,", ");
    disp(u->state,sizeof(u->state),s);
    usputc(s,' ');
    disp(u->zip,sizeof(u->zip),s);
    if (u->born[0] != '-')  {
        usputs(s,"\nBorn 19");
        disp(u->born,sizeof(u->born),s);
    }
    usputs(s,"\nClass:  ");
  
    switch(u->class[0])   {
        case 'N':
            usputs(s,"NOVICE");
            break;
  
        case 'T':
            usputs(s,"TECHNICIAN");
            break;
  
        case 'C':
            usputs(s, "CLUB STATION");
            break;
  
        case 'G':
            usputs(s,"GENERAL");
            break;
  
        case 'A':
            usputs(s,"ADVANCED");
            break;
  
        case 'E':
            usputs(s,"EXTRA");
            break;
  
        default:
            usputc(s,u->class[0]);
    }
    usputc(s,'\n');
  
    yeardate = yrval(u->expiryr);
    if (day_calc(u->expirday, daystr, u->expiryr) != NULL)  {
        if (daystr[0] != '\0')  {
            if ((u->exp[0] == ' ') || (yeardate > 2000))  {
                usputs(s,"Expires ");
                usprintf(s,"%s\n", daystr);
            }
            else usprintf(s,Unknown,TAB);
        }
        else usprintf(s,Unknown,TAB);
    }
    return 0;
}
  
/*---------- Procedures specific to Canadian Callsign DataBase----------------*/
static int
display_canada(us,s)
void *us;
int s;
{
    struct canada *u;
#define DISP(x) {usputs(s,"\n        ");\
disp(x, sizeof(x),s);}
  
u = (struct canada *) us;
  
DISP(u->callsign);
DISP(u->name);
DISP(u->street);
DISP(u->town);
disp(u->zip, sizeof(u->zip),s);
usputc(s,'\n');
usprintf(s,"%sCANADA\n",TAB);
  
return 0;
}
/* comp_canada_calls compares the sought-for callsign with a mask of the
   type encountered in the callsign field of a struct canada,
  
  returns 0 if they match, -1 if mask1 < mask2, 1 if mask1 > mask2.
  mask1 is the callsign requested, mask2 is the database callsign from
  the record under test for match.
 */
  
static int
comp_canada_calls(mask1, mask2)
char mask1[6];
char mask2[6];
{
    int i;
    char *p, *q;
  
    p = mask1, q = mask2;
    for (i =0; i < 6; i++)  {
        if (*p != *q)  return (int) *p - (int) *q;
        p++, q++;
    }
    return 0;
}
/* One of three public  functions!  */
int
cb_lookup(s,call)
int s;
char  *call;
{
/* This is the launcher.  Get rid of any callsign validity-checking code
  that happens before calling cb_lookup. Eventually we will have to
  launch the US, Canadian, or foreign Buckmaster CD databases. For right
  now, tho, we only have USA and CANADA.
  
  For April '92 CD we only have USA! - KB7YW
  
    Sat  05-02-1992  Here I am inserting code to recognize license class 'X',
  which is Buckmaster's way of saying that the license has expired because
  the licensee has changed his callsign - for whatever reason. This is only
  a possibility with the USA database and then only for CD's dated april
  '92 or later - KB7YW
 */
    char *q;
    int len, j;
    char mask[7];
    struct database_activity DA;
    char *cant = "Can't handle callsign \"%s\"\n";
    struct ftime ft;
    struct usa *u;
  
                  /* FIRST TEST CALLSIGN FOR VALIDITY */
  
  /* Buckmaster only matches callsigns of 4 to 6 letters in length  */
    len = strlen(call);
    if ((len < 4) || (len > 6)) return invalid_callsign(call,s);
  
    for (j = 0; j < 6; j++) call[j] = toupper(call[j]);
  
                  /* every character must be alphanumeric */
    q = call;
    for (j = 0; j < len; j++) if (!(isalnum(*q++)))
            return invalid_callsign(call,s);
  
    q = call;
        switch (*q) {
            case 'A':
            case 'K':
            case 'N':
            case 'W':
                if ((init(&DA, USA, s)) == -1)  return 0;
                if ((make_mask(mask, call, s)) == -1) {
                    close_down(&DA);
                    return 0;
                }
                break;
  
            case 'V':
                if ((strncmp(call,"VO",2) == 0) || (strncmp(call,"VE",2) == 0)) {
                    if ((init(&DA, CANADA, s)) == -1)  return 0;
                    if ((make_mask(mask, call, s)) == -1)   {
                        close_down(&DA);
                        return 0;
                    }
                }
                else  {
                    usprintf(s,cant,call);
                    return 0;
                }
                break;
  
            default:
                usprintf(s,cant,call);
                return 0;
        }
  
    if ((find_call(&DA,mask)) == -1)
        usprintf(s, "\n\nCallsign \"%s\" not found\n\n",call);
    else {
        while (displayrec(&DA,s) == -2)  {
/* If we got to here, we are dealing with an 'X' record, which is only a
  possibility in the USA database. (Of course now (Sat  05-02-1992), we
  only have the U.S. database to play with as of April '92 HamCall CD).
  Soon we can get rid of the Canadian stuff, since the CD's that support
  it will be too old to use.
 */
            u = (struct usa *) DA.record_buf;
  
/* Buckmaster will have filled out u->firstname as: "ùSEEùKF8PHù". So
   our new callsign to look up is at position 5 in u->firstname!
 */
            strncpy(call, &u->firstname[5], 6);/* put in the new callsign !!! */
            if ((make_mask(mask, call, s)) == -1)   {
                close_down(&DA);
                return 0;
            }
            if ((find_call(&DA,mask)) == -1)  {
                usprintf(s, "\n\nCallsign \"%s\" not found\n\n",call);
                break;  /* Get out of what might otherwise be an infinite loop  */
            }
        }
        getftime(DA.handle, &ft);
        usprintf(s,"\nDatabase date: %s %d, %02d\n\n\n",
        month[ft.ft_month-1].m, ft.ft_day, ft.ft_year+1980);
    }
    /* find_call returns with appropriate record already read into
       DA.record_buf and the return value is -1 for error or number of
       seeks required on success.
     */
  
    close_down(&DA);
    return 0;
}
  
#else   /* #ifdef ICALL  i.e. ICALL is now defined, & here's what you get: */
  
/* Buckmaster's international callsign database - new April '92             */
long Mincoreleft = 77000L;      /* minimum coreleft needed for icall.exe    */
int init_Database(char *);      /* public - used by cdbstart in callcdb.c   */
static int get_Database_date(char *);
char *Database_date = NULLCHAR; /* set once in get_Database_date()          */
char *Database = NULLCHAR;      /* set by init_Database()                   */
char *db = "/ham0/hamcall.all"; /* default initialized address for Database */
  
int
cb_lookup(s,call)
int s;
char  *call;
{
    FILE *fp, *fd;
    char command[50];
    char file[20], dosfile[20];
    int fhandle, retval;
    long filesize;
    char *q, *p;
  
    if (coreleft() < Mincoreleft) {
        usprintf(s,
        "\n\n\tLow System Memory !!!.\n\
        \tAlert sysop @ %s:\n\
        \tCannot service callsign -\n\
        \tdatabase requests until\n\
        \tprogram reboots.\n\n", Callserver);
        log(s,"callserver failure [insufficient memory]");
        return 0;
    }
  
    if (Database == NULLCHAR)
        init_Database(NULLCHAR);
  
    tmpnam(file);
    strcpy(dosfile, file);
    q = dosfile;
  
    while (*q != '\0')  {
        if (*q == '/') *q = '\\'; /* make slashes into backslashes for MS-DOS */
        q++;
    }
  
    sprintf(command,"%s\\icall.exe %s %s *> %s",CDROM, CDROM, call, dosfile);
  
    retval = system(command);
  
    if (!retval)  {
        if((fp = fopen(file,READ_TEXT)) != NULLFILE) {
            sendfile(fp,s,ASCII_TYPE,0,NULL);
            if (Database_date != NULLCHAR)
                usprintf(s, "\n\nDatabase date: %s\n\n\n", Database_date);
            fclose(fp);
            remove(file);
            return 0;
        }
    }
    usputs(s,"Could not open database - please try later\n\n");
    return 0;
}
#endif  /* #ifndef ICALL */
#endif /* #ifdef CALLSERVER */
  
struct cmds Callserver_cmds[] = {
    { "hostname", docallserver_hostname, 0,  0,  NULLCHAR },
#ifdef CALLSERVER     /* not used if only CALLCLI is defined  */
    { "database", docallserver_databases,  0,  0,  NULLCHAR },
#endif
#ifdef BUCKTSR
    { "tsrirq", docallserver_tsrirq, 0, 0,  NULLCHAR },
#endif
    { NULLCHAR, NULL, 0, 0, NULLCHAR }
};
  
/* Another public, headered in commands.h */
int
docallserver(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return subcmd(Callserver_cmds, argc, argv, p);
}
  
static int
docallserver_hostname(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc < 2)
    {
        if(Callserver != NULLCHAR)
            tprintf("The callserver's hostname is: %s\n",Callserver);
        else
        {
            j2tputs("Callserver not configured!\n");
            j2tputs("Usage: callserver <hostname>|<ip_address>\n");
        }
    }
    else {
        if(Callserver != NULLCHAR)
            free(Callserver);
        Callserver = j2strdup(argv[1]);
    }
    return 0;
}

#ifdef BUCKTSR
/* This part of the code was written 5/95 by N5KNX for use with the bucktsr.exe
   TSR driver from Buckmaster, supplied for the April '95 Hamcall CDROM.
 */

static int tsrirq = 0x66;   /* bucktsr.exe defaults to /I102 */

static int
docallserver_tsrirq(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint(&tsrirq,"BUCKMASTER bucktsr's software IRQ",argc,argv);
}

static char *formatdate(char *date);

/* One of three public  functions!  */
int
cb_lookup(s,findcall)
int s;
char  *findcall;
{

#define BUCK_DRIVE 	1
#define BUCK_BUF	2
#define BUCK_LOOKUP	3

#define BUF_SIZE	300

#define BUCK_Callsign	181
#define BUCK_Class	182	/*  (E,A,G,P,T,N for U.S.) */
#define BUCK_BmCode	183	/*  (I,A,X,C,M,R,D,L) */
/* I - U.S. Individual    A - Internatational    X - Cross Reference
   C - Club               M - Military           R - RACES
   D - FCC Delete         L - Requested deletion from HamCall
   Q - Silent Key,per QST V - Vanity Callsign (FCC)
   Z - Internal use only  - No longer in FCC database
*/
#define BUCK_FirstName	184
#define BUCK_Middle	185
#define BUCK_Last	186
#define BUCK_Suffix	187
#define BUCK_Street	188
#define BUCK_ReciprocalAddress	189	/*  (FCC international records only) */
#define BUCK_AlienAddress	190	/*  (FCC international records only) */
#define BUCK_City	191
#define BUCK_State	192
#define BUCK_PostalCode	193
#define BUCK_BirthDate	194	/*  yyyymmdd */
#define BUCK_DateFirstIssue	195	/*  yyyymmdd */
#define BUCK_ExpirationDate	196	/*  yyyymmdd */
#define BUCK_ProcessDate	197	/*  (date of last change) yyyymmdd */
/* Note: Currently many of the international records have a process date in
   the form yyyyddd.  This will change on the October 95 CD, but we see no
   reason to pass this field to users even then, it really only applies to our
   internal processing. */
#define BUCK_County	198
#define BUCK_GMToffset	199
#define BUCK_Latitude	200	/*  (degrees.decimal degrees) */
#define BUCK_Longitude	201	/*  (degrees.decimal degrees) */
#define BUCK_GridSquare	202
#define BUCK_AreaCode	203
#define BUCK_PreviousCall	204
#define BUCK_PreviousClass	205
#define BUCK_TransactionType	206	/*  (FCC designation) */
#define BUCK_Email	207
#define BUCK_QSLmgr	208
#define BUCK_Country	209 /* Mailing Address Country */
#define BUCK_URL	210
#define BUCK_Vanity	211
/* Vanity eligibility flag
   A - Former primary station holder
   B - Close relative of primary station holder
   C - Former club station holder
   D - Club station with consent of close relative of former holder
   E - Primary station preference list
   F - Club station preference list
*/
#define BUCK_faxno	212


    char far *tsrvec;
    unsigned char *buf, *p;
    char *call, *class, *fname, *mi, *lname, *namesuff, *street, *licensed;
    char *city, *state, *zip, *birth, *expires, *pcall, *pclass;
    char *county, *lat, *lon, *grid;
    char *country, *email, *qslmgr, *url;
    char workbuf[80];

    union REGS regs;

#ifdef notdef
    for (i=0; i<strlen(argv[3]); i++) {  /* a majuscule => fuller display */
        if (*(argv[3]+i) >= 'A' && *(argv[3]+i) <= 'Z') {
            fulldisp=1; break;
        }
    }
#endif

    /* Verify that there's really a packet driver there, so we don't
     * go off into the ozone (if there's any left)
     */
    tsrvec = (char far *)getvect(tsrirq);
    tsrvec -= 0x20a;   /* offset obtained by looking at BUCKTSR.EXE, V1.0 */
    if (strncmp(tsrvec, "BUCKTSR", 7)) {
	sprintf(workbuf,"No BUCKTSR driver loaded at int %d (decimal) (sig @ %Fp)\n",tsrirq, tsrvec);
	usprintf(s,workbuf);
        log(-1, workbuf);
	return -1;
    }

    if (CDROM == NULLCHAR) {
        sprintf(workbuf,"The sysop has not issued the CDROM command; aborting\n");
        usprintf(s, workbuf);
        log(-1, workbuf);
        return -1;
    }

    buf = (unsigned char *)mallocw(BUF_SIZE);
/*    memset(buf, 0, BUF_SIZE); NOT NEEDED */

    regs.x.ax = BUCK_DRIVE;
    regs.x.bx = *CDROM - 'A';
    int86(tsrirq,&regs,&regs);

    regs.x.ax = BUCK_BUF;
    regs.x.bx = FP_SEG(buf);
    regs.x.cx = FP_OFF(buf);
    regs.x.dx = BUF_SIZE;
    int86(tsrirq,&regs,&regs);

    *buf = 0;  /* start empty */
    regs.x.ax = BUCK_LOOKUP;
    regs.x.bx = FP_SEG(findcall);
    regs.x.cx = FP_OFF(findcall);
    regs.x.dx = strlen(findcall)+1;  /* include nul */
    int86(tsrirq,&regs,&regs);

    for (p=buf+BUF_SIZE-1; p>=buf; p--) {  /* trim trailing whitespace */
        if (*p == 0 || *p == ' ' || *p == '\n' || *p == '\r') *p = '\0';
        else  break;
    }

#ifdef DEBUGBUCK
    printf("trim: finished with p = %Lp, *p = %c.\n", p, *p);
    for (p = buf; *p && p<buf+BUF_SIZE; p++) {
	if (*p & 0x80) printf("\nCode %d: ", *p);
        else fputc(*p, stdout);
    }
    fputc('\n', stdout);
#endif

    if (*buf != BUCK_Callsign) {  /* error of some kind */
        usprintf(s,"Error: %s\n", buf);
        return 1;
    }

    call=class=fname=mi=lname=namesuff=street=city=state=zip=birth\
        =expires=pcall=pclass=licensed=grid=lat=lon=county\
        =country=email=qslmgr=url="";

    for (p = buf; *p && p<buf+BUF_SIZE; p++) {
        if (*p & 0x80) {
            switch (*p) {
            case BUCK_Callsign:         if (! *call) call=(char *)p+1;  break;
                                        /* last field is followed by THIS one instead of a NUL */
            case BUCK_Class:	        class=(char *)p+1;  break;
            case BUCK_FirstName:        fname=(char *)p+1;  break;
            case BUCK_Middle:           mi=(char *)p+1;  break;
            case BUCK_Last:             lname=(char *)p+1;  break;
            case BUCK_Suffix:           namesuff=(char *)p+1;  break;
            case BUCK_Street:           street=(char *)p+1;  break;
            case BUCK_City:             city=(char *)p+1;  break;
	    case BUCK_State:            state=(char *)p+1;  break;
            case BUCK_PostalCode:       zip=(char *)p+1;  break;
            case BUCK_BirthDate:        birth=(char *)p+1;  break;
            case BUCK_DateFirstIssue:   licensed=(char *)p+1;  break;
            case BUCK_ExpirationDate:   expires=(char *)p+1;  break;
            case BUCK_PreviousCall:     pcall=(char *)p+1;  break;
            case BUCK_PreviousClass:    pclass=(char *)p+1;  break;
            case BUCK_County:           county=(char *)p+1;  break;
            case BUCK_Latitude:         lat=(char *)p+1;  break;
            case BUCK_Longitude:        lon=(char *)p+1;  break;
            case BUCK_GridSquare:       grid=(char *)p+1;  break;
            case BUCK_Email:            email=(char *)p+1;  break;
            case BUCK_QSLmgr:           qslmgr=(char *)p+1;  break;
            case BUCK_Country:          country=(char *)p+1;  break;
            case BUCK_URL:              url=(char *)p+1;  break;
            }
            *p = '\0';  /* terminate string */
        }
    }

/* Our display will look like this:
N5KNX   James P. Dugal                               Born:    08/19/1950
        POB 44844                                    Class: A 04/19/1988
        Lafayette, LA, 70504                         Prev:  G
        Grid EM30XF, Lat. 30.2239, Long. 092.0197    Expir:   04/19/1998
        County: Lafayette
        Email: jpd@usl.edu
        URL:
        QSL Manager:
*/

    sprintf(workbuf,"%s%s%s%s%s%s%s",fname, *mi == 0 ? "":" ",mi,
      *mi == 0 ? " ":". ", lname, *namesuff == 0 ? " ":", ", namesuff);

    usprintf(s,"\n%-8s%-45s", call, workbuf);
    if (*birth)
        usprintf(s,"Born:    %s\n",formatdate(birth));
    else
        usputc(s,'\n');

    if (*street) {
        usprintf(s,"%-8s%-45s","", street);
        if (*class || *licensed)
            usprintf(s,"Class: %s %s\n", *class == 0 ? " ":class, formatdate(licensed));
        else
            usputc(s,'\n');
    }

    if (*city) {
        if (*country && !*state) {
            state=country; country = "";
        }
        sprintf(workbuf, "%s, %s, %s %s", city, state, zip, country);
        usprintf(s,"%-8s%-45s", "", workbuf);
        if (*pcall || *pclass)
            usprintf(s,"Prev:  %s %s\n", pclass, pcall);
        else
            usputc(s,'\n');
    }

    if (*grid) {
        sprintf(workbuf, "Grid %s, Lat. %s, Long. %s", grid, lat, lon);
        usprintf(s,"%-8s%-45sExpir:   %s\n", "", workbuf, formatdate(expires));
    }

    if (*county) usprintf(s,"%-8sCounty: %s\n", "", county);
    if (*email) usprintf(s,"%-8sEmail: %s\n", "", email);
    if (*url) usprintf(s,"%-8sURL: %s\n", "", url);
    if (*qslmgr) usprintf(s,"%-8sQSL Manager: %s\n", "", qslmgr);

    free(buf);
    return 0;

}

/* convert yyyymmdd to mm/dd/yyyy (if we were really eager we could consult DOS date-format spec) */
static char *formatdate(char *date)
{
    static char pretty_date[12];

    if (strlen(date) != 8) {  /* unexpected format */
        strncpy(pretty_date, date, sizeof(pretty_date));
        pretty_date[sizeof(pretty_date)-1] = '\0';
    }
    else
        sprintf(pretty_date, "%2.2s/%2.2s/%4.4s", date+4, date+6, date);

    return (pretty_date);
}
#endif  /* BUCKTSR */

#ifdef CALLSERVER
#ifdef ICALL
  
/* For the new (May '92) HamCall CD the  callserver database is
"/ham0/hamcall.all" unless the user specifies otherwise
 */
  
static int
get_Database_date(db)
char *db; /* temporary string contains putative database filename  */
{
    FILE *fd;
    int fhandle;
    struct ftime ft;
    char tmpstr[50];
    char database[50];
  
/*  CDROM contains drive letter + ':', e.g. "s:"
    db contains the rest of the path + database file name, e.g.
    "/ham0/hamcall.exe".
 */
    sprintf(database, "%s%s", CDROM, db);
  
    if ((fd = fopen(database, "rb")) != NULLFILE) {
        fhandle = fileno(fd);
        getftime(fhandle, &ft);
        sprintf(tmpstr,"%s %d, %02d",
        month[ft.ft_month-1].m, ft.ft_day, ft.ft_year+1980);
        fclose(fd);
        if (Database_date != NULLCHAR) free(Database_date);
        Database_date = j2strdup(tmpstr); /* Now it's initialized!! */
        return 0;
    }
    else return -1;   /* Database could not be opened for binary read */
}
  
/* public: called when the ICALL callserver is started. db must be allocated
    with j2strdup unless it is to be assigned to the global char *db,
    declared this module. In which case db should be passed as NULLCHAR.
 */
  
int
init_Database(tmp_db)
char *tmp_db;
{
    extern char *db;  /* global: "/ham0/hamcall.all"  */
  
    if (tmp_db == NULLCHAR) tmp_db = db;
  
    if (Database == NULLCHAR)
        if (CDROM == NULLCHAR)  {
            j2tputs(cdrom_prompt);
            return -1;
        }
    if (get_Database_date(tmp_db) == 0) {
        Database = tmp_db;
        return 0;
    } else  {
        tprintf(
        "\t%c%s could not be found.\n\
        \tPlace \"Ham Call\" CD in CD-ROM drive \"%s\" -OR\n",'\007',tmp_db, CDROM);
        j2tputs(
        "\tspecify alternate callserver database filename.\n");
        return -1;
    }
}
  
static int
docallserver_databases(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    char *tmpstr;
    char *cs = "callserver database";
  
    if (argc == 1 ) {
        tprintf("%s ", cs);
        if (Database == NULLCHAR) {
            j2tputs("not configured.\n"); /* Will not be configured unless
                                         user has already used the "cdrom"
                                         command, and optionally
                                         "callserver database </path/filename>"
                                         as well as "start callbook".
                                       */
            if (CDROM == NULLCHAR)
                j2tputs(cdrom_prompt);
        }
        else  tprintf("is \"%s\" (dated %s)\n", Database, Database_date);
        return 0;
    } else if (argc >= 2)   {
        tmpstr = j2strdup(argv[1]);
        if (init_Database(tmpstr) == 0) {
            return 0;
        } else  {
            free(tmpstr);
      /* no error reporting. init_Database() does plenty of that  */
            return -1;
        }
    }
    return 0;
}
  
  
#else       /* #ifndef ICALL i.e. ICALL is NOT defined here. You use this
                with the old-style database  */
static int
docallserver_databases(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int t;
  
    if (argc < 2) {
        for (t = 0; t < DATABASE_TYPES; t++)  {
            tprintf("callserver database %s ", database_label[t]);
            if (database[t] == NULLCHAR)  {
                j2tputs("not configured.\n");
            } else  tprintf("%s\n", database[t]);
        }
        return 0;
    } else  { /* argc >= 2 */
        for (t = 0; t < DATABASE_TYPES; t++)  {
            if (strncmp(database_label[t],argv[1],strlen(argv[1])) == 0)  {
                if (argc < 3) {
                    tprintf("callserver database %s ", database_label[t]);
                    if (database[t] == NULLCHAR)  j2tputs("not configured\n");
                    else  tprintf("%s\n", database[t]);
                } else  { /* argc >= 3  */
                    if (database[t] == NULLCHAR)  free(database[t]);
                    database[t] = j2strdup(argv[2]);
                }
                return 0; /* we have found our match with database_label[t] */
            }
        }
      /* no match with database_label: which database?  */
        j2tputs("Usage: callserver database ");
        for (t = 0; t < DATABASE_TYPES; t++)  {
            tprintf("%s", database_label[t]);
            if ( t < DATABASE_TYPES - 1)  tputc('|');
        }
        j2tputs(" <database drive:/path/filename>\n");
        return -1;
    }
  /* not reached  */
}
#endif  /* #ifdef ICALL  */
#endif  /* CALLSERVER */

#if defined(CALLSERVER) || defined(BUCKTSR)
/* the last public of this module, also headered in commands.h  */
int
docdrom(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    extern char *CDROM; /* declared above, this module  */
  
    if (argc < 2) {
        if (CDROM == NULLCHAR)  {
            j2tputs("CDROM drive letter not specified\n");
        }
        else  tprintf("CDROM drive is \"%s\"\n", CDROM);
        return 0;
    }
    else  {
        if ((argv[1][2] == '\0')    &&
            (isalpha(argv[1][0]))   &&
        (argv[1][1] == ':'))  {
            if (CDROM != NULLCHAR)  free(CDROM);
            CDROM = j2strdup(argv[1]);
            return 0;
        }
        else  {
            tprintf("Usage: \"cdrom driveletter:\", e.g. \"cdrom s:\"\n");
            return 1;
        }
    }
  /* not reached  */
}
#endif  /* if defined(CALLSERVER) || defined(BUCKTSR) */
#endif /* BUCKBOOK */
  
