#include <time.h>
#include <sys/timeb.h>
#include "global.h"
#ifdef CALLBOOK
#include "files.h"
  
#undef DEBUG 1
#define TRUE 1
#define FALSE 0
  
static int days_tbl[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  
  
struct callrec  {
    char    *call;
    char    *last_name;
    char    *title;
    char    *first_name;
    char    *middle_name;
    char    *birth_date;
    char    *license_date;
    char    *expire_date;
    char    *mail_addr1;
    char    *mail_city;
    char    *mail_state;
    char    *zip;
    char    *location1;
    char    *location2;
    char    *location3;
    char    *license_class;
    char    *prev_call;
    char    *prev_class;
    char    *extra;
};
  
typedef struct callrec CALLREC;
  
#define HIGHER 1
#define LOWER 0
  
  
extern char *get_field();
extern fill_record();
extern long calc_nxt_record();
extern strnicmp();
  
  
long    firstrec;       /* address of the end of the first record */
char    cbuffer[256];
CALLREC current_rec;
  
  
struct tm st_time;
  
struct tm *ct_time;
long    rtime;          /* time in secs since 70 */
  
  
long    lasthi,lastlo,current;
  
cb_lookup(s,call)
int s;
char    *call;
{
    FILE    *fp;
    char    *file;      /* derived file name */
    long    pos = 0;
    int sts;
    int dir,srch;
    char    search_call[10];
    int srch_cnt = 0;
    int srch_len= 0;
    char    tmpstr[80];
    unsigned    bdy,byr;
    int yrs_old;
    char    class;
  
    srch_len = strlen(call);
    strcpy(search_call,"      ");
    strncpy(search_call,call,srch_len);
#ifdef notdef
    file = (char *)pathname(CDBdir,"calldb");
    tprintf("Opening database: %s\n",file);
  
    if((fp = fopen(file,READ_TEXT)) == NULLFILE)
#endif
        if((fp = fopen("/callbook/calldb",READ_TEXT)) == NULLFILE)
        {
            usputs(s,"Callbook Services unavailable.\n");
#ifdef notdef
            free(file);
#endif
            return(0);
        }
#ifdef notdef
    free(file);     /* free up the filename string */
#endif
    /* position to end of file */
    fseek(fp,0L,2L);
    lasthi = ftell(fp);
  
#ifdef DEBUG
    usprintf(s,"lasthi %ld\n",lasthi);
#endif
  
    lastlo = 0;
    current = lasthi/2;
  
    fgets(cbuffer,256,fp);
    cbuffer[0] = '\0';
  
    firstrec = ftell(fp);
  
    while(current >= 0)
    {
#ifdef DEBUG
        usprintf(s,"Position: %d\n",current);
#endif
        pwait(NULL);        /* give up the processor for others to run */
  
        sts = find_next_rec(fp,current,cbuffer);
        if(sts)
        {
            usputs(s,"Error reading database\n");
            fclose(fp);
            return(0);
        }
  
        srch_cnt++;
  
        srch = strnicmp(search_call,cbuffer,6);
  
#ifdef DEBUG
        usprintf(s,"srch: %s    currec: %s\n",search_call,current_rec.call);
#endif
        if(srch== 0)
        {
            memset(&current_rec,'\0',sizeof(struct callrec));
  
            sts = fill_record(cbuffer,&current_rec);
  
            if(sts == 0)
            {
                usprintf(s,"\n\nCallsign:  %s\n",current_rec.call);
  
                /* License Class */
                usputs(s,"Class:     ");
                class = current_rec.license_class[0];
                switch(class)
                {
                    case    'A':    usputs(s,"Advanced");
                        break;
                    case    'E':    usputs(s,"Extra");
                        break;
                    case    'G':    usputs(s,"General");
                        break;
                    case    'N':    usputs(s,"Novice");
                        break;
                    case    'T':    usputs(s,"Technician");
                        break;
                    default:    usputs(s,"Unknown");
                }
                usputc(s,'\n');
  
                /* Expire Date */
                sprintf(tmpstr,"%.2s",current_rec.expire_date);
  
                sscanf(tmpstr,"%d",&byr);
                sprintf(tmpstr,"%s",&current_rec.expire_date[2]);
                sscanf(tmpstr,"%d",&bdy);
  
                month_day_year(byr,bdy,&st_time);
  
                format_date(&st_time,tmpstr);
                usprintf(s,"Expires:   %s\n",tmpstr);
                usputc(s,'\n');  /* blank line */
  
                usprintf(s,"Owner:     %s ",current_rec.first_name);
                if(current_rec.middle_name != NULL)
                    usprintf(s,"%s ",current_rec.middle_name);
                usprintf(s,"%s ",current_rec.last_name);
                if(current_rec.title != NULL)
                    usputs(s,current_rec.title);
  
                usputc(s,'\n');
  
  
                /* Address formatting */
  
                usprintf(s,"           %s\n",current_rec.mail_addr1);
  
                usputs(s,"           ");  /* line up nxt line */
  
                if(current_rec.mail_city != NULL)
                    usprintf(s,"%s ",current_rec.mail_city);
                if(current_rec.mail_state != NULL)
                    usprintf(s,"%s ",current_rec.mail_state);
                if(current_rec.zip != NULL)
                    usprintf(s,"%s ",current_rec.zip);
                usputc(s,'\n');
  
  
                /* Birth date */
                sprintf(tmpstr,"%.2s",current_rec.birth_date);
  
                sscanf(tmpstr,"%d",&byr);
                sprintf(tmpstr,"%s",&current_rec.birth_date[2]);
                sscanf(tmpstr,"%d",&bdy);
  
                month_day_year(byr,bdy,&st_time);
  
  
                /* get current date and time */
                rtime = time((time_t *)0);
                ct_time = localtime(&rtime);
  
                /* calculate age */
                yrs_old = ct_time->tm_year -  byr;
  
                if((st_time.tm_mday > ct_time->tm_mday) &&
                    (st_time.tm_mon >=  ct_time->tm_mon)) yrs_old--;
  
  
/*
                format_date(&st_time,tmpstr);
                usprintf(s,"Birth date:      %s\n",tmpstr);
*/
  
  
                usputc(s,'\n'); /* blank line */
  
                if(current_rec.prev_call != NULL)
                    usprintf(s,"Prev Call: %s\n",current_rec.prev_call);
                usprintf(s,"Age:       %d\n",yrs_old);
            }
  
            else
            {
                usputs(s,"Internal Database Error!\n");
            }
  
            usprintf(s,"\nSearch Count: %d\n\n",srch_cnt);
  
            free_record(&current_rec);
  
            fclose(fp);
            return(0);
        }
  
        else
        {
            if(srch < 0)
            {
                dir = LOWER;
#ifdef DEBUG
                usprintf(s,"Nomatch: %s    TRY LOWER\n",current_rec.call);
#endif
            }
            if(srch > 0)
            {
                dir = HIGHER;
#ifdef DEBUG
                usprintf(s,"Nomatch: %s    TRY HIGHER\n",current_rec.call);
#endif
            }
            current = calc_nxt_record(dir);
        }
    }
  
    usputs(s,"Record not found!\n");
    usprintf(s,"Search Count: %d\n",srch_cnt);
    fclose(fp);
  
}
  
/* seek to file position "pos" then find start of next record */
  
find_next_rec(fp,pos,buffer)
FILE    *fp;
long pos;
char    *buffer;
{
  
    /* go to the right place in the file */
    /* if error then exit */
  
    if(fseek(fp,pos,0L) != 0) return -1;
  
    /* get and toss any partial record */
    if(pos != 0)fgets(buffer,256,fp);
  
    /* now get next complete record */
    fgets(buffer,256,fp);
  
    return 0;
  
}
  
  
  
/* here disect an input string into it's component parts */
fill_record(buffer,record)
char    *buffer;
CALLREC *record;
{
    char    fldbuf[256];
    char    *tmpbuf;
    int sts;
  
    sts = -1;       /* default to fail */
    tmpbuf = buffer;
  
    while(1)
    {
  
    /* get the data fields... and expand as neccessary */
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
  
    /* get the callsign */
        if(strlen(fldbuf))record->call = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->last_name = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->title = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->first_name = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->middle_name = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->birth_date = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->license_date = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->expire_date = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->mail_addr1 = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->mail_city = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->mail_state = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->zip = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->location1 = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->location2 = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->location3 = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->license_class = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->prev_call = j2strdup(fldbuf);
  
        if((tmpbuf = get_field(tmpbuf,fldbuf)) == NULL) break;
        if(strlen(fldbuf))record->prev_class = j2strdup(fldbuf);
  
        get_field(tmpbuf,fldbuf);
        if(strlen(fldbuf)) record->extra = j2strdup(fldbuf);
        sts = 0;
    }
  
  
    return sts;
  
}
  
  
/* given input buffer, pull out the next field, copying data into specified */
/* field, and return pointer to beginning of next field... skipping over */
/* the "BAR".  Return NULL for no more fields */
  
char *
get_field(buffer,field)
char    *buffer;
char    *field;
{
  
    char    *bar;
    int field_len;
    int blnklen = 0;
  
    field[0] = '\0';    /* initially null terminate the string */
  
    /* find the "BAR" separator field */
    bar = strchr(buffer,'|');
  
    if(bar != NULL)
    {
        field_len = (int)(bar - buffer);
  
        if(field_len)
        {
            blnklen = strspn(buffer," ");
            strncpy(field,buffer+blnklen,field_len-blnklen);
  
            field[field_len-blnklen] = '\0';    /* terminate the string */
        }
  
        /* bump pointer past the "BAR" */
        bar +=1;
    }
    else
    {
        field_len = strlen(buffer);
        blnklen = strspn(buffer," ");
        strncpy(field,buffer+blnklen,field_len-blnklen);
        field[field_len-blnklen] = '\0';   /* terminate the string */
    }
    return bar;
}
  
long
calc_nxt_record(dir)
int dir;
{
    long    new;
  
    if(dir == LOWER)
    {
        lasthi = current;
        if(current == 1 )new = 0;
        else new = current - ((current - lastlo)/2);
    }
  
    if(dir == HIGHER)
    {
        lastlo = current;
        new = current + ((lasthi-current)/2);
    }
  
    if(new == current)new = -1;
  
    return new;
}
  
  
#ifdef BSD
  
/* Case-insensitive string comparison */
strnicmp(a,b,n)
register char *a,*b;
register int n;
{
    char a1,b1;
  
    while(n-- != 0 && (a1 = *a++) != '\0' && (b1 = *b++) != '\0'){
        if(a1 == b1)
            continue;   /* No need to convert */
        a1 = tolower(a1);
        b1 = tolower(b1);
        if(a1 == b1)
            continue;   /* NOW they match! */
        if(a1 > b1)
            return 1;
        if(a1 < b1)
            return -1;
    }
    return 0;
}
  
#endif
  
  
  
  
format_date(st_time,day_str)
struct  tm  *st_time;
char    *day_str;
{
    sprintf(day_str,"%1d/%1d/%2d",
    st_time->tm_mon,
    st_time->tm_mday,
    st_time->tm_year);
}
  
  
  
/*
 * Fills the  time/date structure from supplied DAY of YEAR
 */
month_day_year(year,dayno,st_time)
unsigned year,dayno;
struct tm *st_time;
{
    int i;
    unsigned leap,days;
    unsigned month;
    unsigned days_in_year, cur_days_in_cur_month;
  
  
    days = dayno;
  
    if ((year % 4) == 0)
        leap = TRUE;
    else
        leap = FALSE;
  
    month = 1;
  
    cur_days_in_cur_month = days_tbl[1];
  
    /* now add in the days for previous months in this year */
    while ( days > cur_days_in_cur_month )
    {
        /* next month */
        month++;
  
        /* remaining days in year */
        days -= cur_days_in_cur_month;
  
        /* get current month's days */
        cur_days_in_cur_month = days_tbl[month];
  
        if ( (month == 2) && (leap) )
            cur_days_in_cur_month++;
    }
  
    if ( days == 0 )
        st_time->tm_mday = 1;
    else
        st_time->tm_mday = days;
  
    st_time->tm_mon = month;
    st_time->tm_year = year;
    st_time->tm_sec = 0;
    st_time->tm_min = 0;
    st_time->tm_hour = 0;
  
    return;
}
  
free_record(record)
CALLREC *record;
{
  
    if(record->call != NULL) free(record->call);
  
    if(record->last_name != NULL) free(record->last_name);
  
    if(record->title != NULL) free(record->title);
  
    if(record->first_name != NULL) free(record->first_name);
  
    if(record->middle_name != NULL) free(record->middle_name);
  
    if(record->birth_date != NULL) free(record->birth_date);
  
    if(record->license_date != NULL) free(record->license_date);
  
    if(record->expire_date != NULL) free(record->expire_date);
  
    if(record->mail_addr1 != NULL) free(record->mail_addr1);
  
    if(record->mail_city != NULL) free(record->mail_city);
  
    if(record->mail_state != NULL) free(record->mail_state);
  
    if(record->zip != NULL) free(record->zip);
  
    if(record->location1 != NULL) free(record->location1);
  
    if(record->location2 != NULL) free(record->location2);
  
    if(record->location3 != NULL) free(record->location3);
  
    if(record->license_class != NULL) free(record->license_class);
  
    if(record->prev_call != NULL) free(record->prev_call);
  
    if(record->prev_class != NULL) free(record->prev_class);
  
    if( record->extra != NULL) free(record->extra);
  
  
}
#endif /* CALLBOOK */
  
