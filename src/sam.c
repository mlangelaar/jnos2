#include <ctype.h>
#include "global.h"
#ifdef SAMCALLB
#include "mbuf.h"
#include "socket.h"
#include "session.h"
#include "proc.h"
#include "netuser.h"
#include "commands.h"
#include "tty.h"
#include "config.h"
#include "samapi.h"         /* samapi interface spec */

#define SAM_COUNTY 1
  
/* Bug fix for CLUB calls and calls of ' ' class - WG7J 940309 */
/* SAM_COUNTY code added - N5KNX - 960815 */
/* Tech-Plus display added - WA4GIU - 971125 */
  
/*
 * functions in samlib.c
 */
  
int LocateSam(void);
int CallSam(int cmd, void far *cmdbuf, void far *rspbuf);
extern char *Callserver;  /* buckbook.c */
extern int usesplit;
int cb_lookup __ARGS((int s,char *str,FILE *fp));
static int cb_look(char *str, cmdfindcall_t *incall,rspdatarec_t *outcall,int *err);
static char Nofind[] = "*** Call not found in SAM database of %s as of %s\n\n";
static char callhdr[] = "Amateur Radio Callsign: %s  (%s Class)  born in 19%s\n";
int SAMoutbytes = 0;
extern void *malloc __ARGS((unsigned nb));
void leadingCaps __ARGS((char *str, int mode));
  
  
int
docallbook(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct sockaddr_in sock;
    char *cp;
    int s,i;
    struct mbuf *bp;
    struct session *sp;
    int thesocket, err;
  
    /*Make sure this comes from console - WG7J*/
    if(Curproc->input != Command->input)
        return 0;
  
    /* Allocate a session descriptor */
    if((sp = newsession(argv[1],TELNET,0)) == NULLSESSION){
        j2tputs(TooManySessions);
        keywait(NULLCHAR,1);
        return 1;
    }
    sp->ttystate.echo = sp->ttystate.edit = 0;
    sp->flowmode = 1;
    sock.sin_family = AF_INET;
    sock.sin_port = IPPORT_CALLDB;
    thesocket = Curproc->output;
    for(i=1;i<argc;i++){
        if (!Callserver || !*Callserver)    {
            tprintf("\n");
            if ((err = cb_lookup (Curproc->output, argv[i], (FILE *) 0)) != 0)
                tprintf ((err == 1) ? "SAMAPI not loaded!\n" : "Amateur Call '%s' not found or invalid!\n", argv[i]);
            continue;
        }
        tprintf("Resolving %s... ",Callserver);
        if((sock.sin_addr.s_addr = resolve(Callserver)) == 0){
            tprintf("Host %s unknown\n",Callserver);
            continue;
        }
        tprintf("trying %s",psocket((struct sockaddr *)&sock));
        if((sp->s = s = j2socket(AF_INET,SOCK_STREAM,0)) == -1){
            j2tputs(Nosock);
            break;
        }
        sockmode(s,SOCK_ASCII);
        if(j2connect(s,(char *)&sock,sizeof(sock)) == -1){
            cp = sockerr(s);
            tprintf(" -- Connect failed: %s\n",cp != NULLCHAR ? cp : "");
            close_s(s);
            sp->s = -1;
            continue;
        }
        tprintf("\n");
        usflush(thesocket);
        usprintf(s,"%s\n",argv[i]);
        while(recv_mbuf(s,&bp,0,NULLCHAR,(int *)0) > 0){
            send_mbuf(thesocket,bp,0,NULLCHAR,0);
        }
        close_s(s);
        sp->s = -1;
    }
    keywait(NULLCHAR,1);
    freesession(sp);
    return 0;
}
  
  
static int
cb_look (str, incall, outcall, err)
char *str;
cmdfindcall_t *incall;
rspdatarec_t *outcall;
int *err;
{
    if (strlen(str) > 6)
        return 2;       /* not an amateur call */
    /* make sure the resident code (SAMAPI.EXE) has been installed */
    if (LocateSam())
        return 1;
  
    /* build command block and call SAMAPI, function SamFindCall */
    incall->packflags = 0;  /* 0 to unpack all data record fields */
    strncpy(incall->call, str, 6);
    incall->call[6] = 0;
    *err = CallSam(SamFindCall, incall, outcall);
    return 0;
}
  
  
#if 0
char *
cb_lookname (str)
char *str;
{
    int err;
    cmdfindcall_t sam_in;   /* buffer for samapi find command */
    rspdatarec_t sam_out;   /* buffer for result of samapi find command */
    char *name, from[8], *cp;
  
    strncpy (from, str, 6);
    from[6] = 0;
    if ((cp = strchr (from, '@')) != 0)
        *cp = 0;
    if ((cp = strchr (from, '%')) != 0)
        *cp = 0;
  
    if (cb_look (from, &sam_in, &sam_out, &err) || err == SerrNotFound)
        return ((char *) 0);
  
    name = malloc(strlen(sam_out.d.FirstName) + strlen(sam_out.d.LastName) + 7);
    if (name)   {
        leadingCaps (&sam_out.d.FirstName[1], 1);
        leadingCaps (&sam_out.d.LastName[1], 0);
        sprintf(name, " (%s ", sam_out.d.FirstName);
        if (sam_out.d.MidInitial[0] != ' ')
            sprintf (&name[strlen(name)], "%s. ", sam_out.d.MidInitial);
        sprintf (&name[strlen(name)], "%s)", sam_out.d.LastName);
    }
    return (name);
}
#endif  
  
  
/* return values - 1=SAMAPI not found, 2=call not found or invalid, 0=okay */
int
cb_lookup (s, str, fp)
int s;
char *str;
FILE *fp;
{
    int err, response;
    cmdfindcall_t sam_in;   /* buffer for samapi find command */
    rspdatarec_t sam_out;   /* buffer for result of samapi find command */
    cmdfindcounty_t cty_in; /* buffer for find county command */
    rspfindcounty_t cty_out;    /* buffer for find county response */
    rhdr_t date_in;
    rspdbdate_t date_out;
    char *class;
  
    SAMoutbytes = 0;
    if ((response = cb_look (str, &sam_in, &sam_out, &err)) != 0)
        return response;
  
    /* check for unusual error something other that plain ole not found */
    if (err != 0 && err != SerrNotFound)
        return 2;
  
    /* check for just not found */
  
    if (err == SerrNotFound)    {
        err = CallSam(SamGetDatabaseDate, &date_in, &date_out);
        if (fp)
            SAMoutbytes += fprintf (fp, Nofind, date_out.scope, date_out.date);
        else
            SAMoutbytes += usprintf(s, Nofind, date_out.scope, date_out.date);
        return 2;
    }
  
#ifdef SAM_COUNTY
    memcpy(cty_in.zip, sam_out.d.Zip, 6);
    err = CallSam(SamFindCounty, &cty_in, &cty_out);
#endif

    switch (sam_out.d.Class[0]) {
        case 'N':   class = "Novice";
            break;
        case 'T':   class = "Technician";
            break;
        case 'G':   class = "General";
            break;
        case 'A':   class = "Advanced";
            break;
        case 'E':   class = "Extra";
            break;
        case 'C':   class = "Club"; /* WG7J */
            break;
        case 'P':   class = "Tech-Plus";  /* WA4GIU */
            break;
        default:    class = "???";   /* WG7J */
            break;
    }
  
    if (fp) {
        /* display call with leading space stripped */
        SAMoutbytes += fprintf(fp, callhdr, sam_out.d.Call + (sam_out.d.Call[0] == ' '), class, sam_out.d.Dob);
  
        /* display first m last, but leave out middle and space if no middle initial */
        SAMoutbytes += fprintf(fp, "%s ", sam_out.d.FirstName);
        if (sam_out.d.MidInitial[0] != ' ')
            SAMoutbytes += fprintf(fp, "%s ", sam_out.d.MidInitial);
        SAMoutbytes += fprintf(fp, "%s\n", sam_out.d.LastName);
  
        /* address line, then city, st zip */
        SAMoutbytes += fprintf(fp, "%s\n", sam_out.d.Address);
        SAMoutbytes += fprintf(fp, "%s, %s %s\n", sam_out.d.City, sam_out.d.State, sam_out.d.Zip);
#ifdef SAM_COUNTY
        if (err==0) SAMoutbytes += fprintf(fp, "County: %s\n", cty_out.county);
#endif
        SAMoutbytes += fprintf(fp, "\n");
    } else  {
        /* display call with leading space stripped */
        SAMoutbytes += usprintf(s, callhdr, sam_out.d.Call + (sam_out.d.Call[0] == ' '), class, sam_out.d.Dob);
  
        /* display first m last, but leave out middle and space if no middle initial */
        SAMoutbytes += usprintf(s, "%s ", sam_out.d.FirstName);
        if (sam_out.d.MidInitial[0] != ' ')
            SAMoutbytes += usprintf(s, "%s ", sam_out.d.MidInitial);
        SAMoutbytes += usprintf(s, "%s\n", sam_out.d.LastName);
  
        /* address line, then city, st zip */
        SAMoutbytes += usprintf(s, "%s\n", sam_out.d.Address);
        SAMoutbytes += usprintf(s, "%s, %s %s\n", sam_out.d.City, sam_out.d.State, sam_out.d.Zip);
#ifdef SAM_COUNTY
        if (err==0) SAMoutbytes += usprintf(s, "County: %s\n", cty_out.county);
#endif
        SAMoutbytes += usprintf(s, "\n");
    }
    return 0;
}
  
void
leadingCaps (str, others)
char *str;
int others;
{
    char *cp;
  
    cp = str;
    strlwr (cp);
    if (!others)    {
        if ((cp = strchr(cp, ' ')) != 0)
            strupr (cp);
        return;
    }
    do  {
        cp = strchr (cp, ' ');
        if (cp != NULL) {
            cp = skipwhite(cp);
            *cp = toupper (*cp);
        }
    } while (cp);
}
  
#endif
  
  
