#include "global.h"
#ifdef SAMCALLB
#ifdef MSDOS
#include <dos.h>
#endif
#include "samapi.h"
  
/*
 * samlib.c
 *
 * This contains functions to init and access the API contained
 * in resident program SAMAPI.EXE.
 *
 * SAMAPI.EXE is a resident program that provides an application program
 * interface to the SAM Amateur Radio Callsign Database.
 *
 * Three functions in here:
 *  LocateSam     - determines SAMAPI is loaded
 *  CallSam       - call the API, passing a command parameter block and
 *                  a buffer for result/response.
 *  SetCountrySam - set the country for search, 0 = USA, 1 = Canada
 *                  Note that normally automatic for simple call lookup
 *
 * The API is accessed via int 2fh, ah = dynamic muxid (default is 0x99).
 * This is commonly called the "multiplex interrupt" interface.
 * Per convention, int 2fh, ah = muxid, al = 0 is an install check, and
 * al comes back non-zero if the API is installed.  For other API
 * functions:
 *
 *      ah = muxid
 *      al = 1
 *   ds:si = pointer to command block
 *   es:di = pointer to result block
 *
 * No registers are changed by SAMAPI (except al when input al == 0)
 *
 * See samapi.h for details on the command/response data structures,
 * command codes, error codes, and so on.
 *
 * LocateSam looks for an environment string: SAMAPI=number.
 * If found, "number" becomes the muxid.  SAMAPI.EXE does the same thing
 * when it loads so muxid can be changed when conflicts arise.
 */
  
int SamMuxid = DEFAULT_SAMMUX;
int SamCountry = 0;             /* 0 == USA, 1 == Canada */
  
/**************************
 * LocateSam
 **************************
 *
 * Finds the resident SAMAPI code.  This MUST be called before
 * other functions are used!.  It checks for the environment
 * string SAMAPI=number to see if there is an override for the
 * muxid (SAMAPI.EXE does the same thing).
 *
 * returns: 0 if resident code found and can be used, -1 if not
 */
  
int LocateSam(void)
{
    int v;
    char *p;
    static union REGS r;
  
    p = getenv("SAMAPI");
    if (p != NULL)
    {
        v = atoi(p);
        if (v > 0 && v < 255)
            SamMuxid = v;
    }
    r.h.al = 0;
    r.h.ah = SamMuxid;
    int86(0x2f, &r, &r);
    return (r.h.al == 1) ? 0 : -1;
}
  
/**************************
 * CallSam
 **************************
 *
 * Call the resident SAMAPI code.
 *
 * This expects a filled-in (all but header) command buffer
 * and a to-be-filled-in response buffer.  cmd is stuffed into
 * cmdbuf and the resident code is called (via int 0x2f).
 *
 * returns: error code from response buffer header (0 usually
 *          means no error)
 */
  
int CallSam(int cmd, void far *cmdbuf, void far *rspbuf)
{
    static union REGS r;
    static struct SREGS sr;
    int i;
  
    ((chdr_t far *) cmdbuf)->cmd = cmd;
  
    /* make reserved fields 0 for future compatability. */
    for (i = 0; i < 2; i++)
        ((chdr_t far *) cmdbuf)->fill[i] = 0;
  
    /* select which database */
    ((chdr_t far *) cmdbuf)->country = SamCountry;
  
    r.x.si = FP_OFF(cmdbuf);        /* ds:si is ptr to command */
    sr.ds = FP_SEG(cmdbuf);
    r.x.di = FP_OFF(rspbuf);        /* es:di is ptr to result buf */
    sr.es = FP_SEG(rspbuf);
    r.h.al = 1;
    r.h.ah = SamMuxid;              /* ax = (muxid << 8) + 1 */
    int86x(0x2f, &r, &r, &sr);      /* do the int 0x2f */
    return ((rhdr_t far *) rspbuf)->err;    /* return error code in rsp buf */
}
  
/**************************
 * SetCountrySam
 **************************
 *
 * Selects the country for subsequent operations.  i.e., selects database
 *
 *  0 = USA
 *  1 = Canada
 *
 * returns 0 if success, else !0 if error.  Possible errors are
 * illegal country code or Canada database not installed.
 *
 * Note that for lookup by call, country selection is automatic unless
 * samapi was started with /noauto option.  However, you need to select
 * country to access database by record number, name record number,
 * qth searches, etc.
 */
  
int SetCountrySam(int country)
{
    chdr_t cmd;
    rspnumrecs_t resp;
    int old;
  
    if (LocateSam())
        return -1;
  
    old = SamCountry;       /* save in case can't switch */
    SamCountry = country;
  
    /* get Number of records for new country ...
     * if no error, must have been ok to switch
     */
  
    if (CallSam(SamGetNumRecs, &cmd, &resp))
    {
        SamCountry = old;       /* error, so don't switch */
        return -1;
    }
    return 0;
}
#endif
