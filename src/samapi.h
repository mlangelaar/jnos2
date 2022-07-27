/*
 * samapi.h
 *
 * Structures, command codes, etc that define the application program
 * interface to SAM Amateur Radio Callsign Database.
 *
 * Copyright 1991 by Tom Thompson, Athens, AL and by RT Systems
 *                   [n4yos]
 *
 * SAMAPI.EXE implements this API.  When run, it stays resident and
 * is called via int 2f, ah = dynamically assigned id.
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
 *
 * The command block always starts with a header (chdr_t).  The
 * header contains a command code to select a particular API
 * function.  The header is followed by additional parameters as
 * needed for the selected function.
 *
 * The response, or result block, also starts with a header (rhdr_t).
 * This contains an error code (which is 0 for no error).  Following
 * the header are additional results depending on the API function.
 *
 * The main item of interest returned by the API functions is the
 * data record for a given ham.  See datarec_t below for the layout
 * of the data record.
 *
 * The data record can be in various stages of "unpackedness".  Many
 * of the string fields in the data base are implemented by pointers
 * in the main data record to compressed dictionaries.  Since unpacking
 * these fields is time consuming, each API record retrieval function
 * allows you to specify what fields will/won't be unpacked.  There
 * is also a function to get the dictionary code (or pointer) for
 * a particular string.  All these features can be combined to
 * implement fast sequential searches without having to unpack each
 * record.
 *
 * the field PackFlags in the datarecord describes which fields
 * are unpacked.  A bit is clear (0) to indicate a corresponding
 * field is unpacked.
 *   Bit  Field
 *  ----  -----
 *    0    LastName
 *    1    FirstName
 *    2    Address
 *    3    City
 *    4    State
 *    5    Zip
 *    6    Date of Birth
 *
 * (Note, Zip and Dob are not in dictionaries, but avoiding converting
 *  them to strings saves time over long searches)
 *
 * When you are faced with a packed record and you want it unpacked, the
 * method is to issue API command SamGetRecordByCall with index = Cindex.
 *
 * There are API commands to find records by callsign, by name,
 * by record number (in callsign order), and by name record number.
 *
 * There is also a command to find a county from a zip code string.  The
 * optional file SAMDBZ.DAT must be present.
 *
 * Cindex is the absolute record number.
 * Nindex indexes an index of the database sorted by name.
 *
 * Given no bugs (ha!), the normal error return for the find functions
 * (and dictionary code get) is SerrNotFound.  The normal error return
 * for the set version business is SerrFail.
 *
 * Whenever a find record function fails, a record is always returned.
 * This is the closest lower record whenever a match cannot be made.
 * In the case of an illegal index, its usually record #0 that is
 * returned.  So if you don't get an exact match, you can start
 * browsing starting at the returned record.
 *
 * The get dict code function DOES NOT return anything useful unless
 * there is an exact match.
 */
  
#define DEFAULT_SAMMUX 0x99 /* default mux id (ah on int 0x2f to access API) */
  
typedef struct datarecord
{
    long Cindex;            /* sorted by call index */
    long Nindex;            /* sorted by name index */
    long LastNameCode;      /* last name dictionary code */
    long FirstNameCode;     /* first name dict. code */
    long AddressCode;       /* address dict. code */
    long CityCode;          /* city dict. code */
    long StateCode;         /* state dict. code (note this one 16 bits) */
    long ZipNumber;         /* zip code number 0-99999 (SEE CANADA NOTE) */
    short DobNumber;        /* date of birth number 0-99*/
    short PackFlags;        /* bitmask of fields that are not unpacked */
    char Call[6+1];         /* call, area is always Call[2] */
    char Class[1+1];        /* class, N,T,G,A, or E */
    char LastName[20+1];    /* last name, 20 max */
    char FirstName[11+1];   /* first name, 11 max */
    char MidInitial[1+1];   /* middle initial */
    char Address[35+1];     /* mailing address, 35 max */
    char City[20+1];        /* city, 20 max */
    char State[2+1];        /* state */
    char Zip[5+1];          /* zip code string (SEE CANADA NOTE) */
    char Dob[2+1];          /* date of birth string "00" to "99" */
    char reserved[11];
} datarec_t;
  
/*
 * CANADA NOTE
 *
 * When a datarec_t contains a canadian call:
 *
 * The .Zip field contains 6 characters for the postal code.  The
 * terminating zero is in the first byte of the .Dob field (therefore
 * .Dob is a 0-length string).
 *
 * .ZipNumber is the postal code packed as follows:
 *
 * Bit # |31  27 |26  22 |21 18 |17  13 |12  9 | 8   4 | 3  0 |
 *       | 00000 | xxxxx | xxxx | xxxxx | xxxx | xxxxx | xxxx |
 *       |       |  c0   |  c1  |  c2   |  c3  |  c4   |  c5  |
 *
 * Add 'A' to c0, c2, and c4 to obtain alpha character
 * Add '0' to c1, c3, and c5 to obtain numeric character
 *
 * Examples: PostalCode  Binary--------------------------------
 *           A0A 0A0     00000 00000 0000 00000 0000 00000 0000
 *           Z9Z 9Z9     00000 11111 1111 11111 1111 11111 1111
 *           B7C 8D9     00000 00001 0111 00010 1000 00011 1001
 */
  
/* API command buffer (DS:SI) header */
  
typedef struct cmd_header
{
    unsigned char cmd;
    unsigned char fill[2];
    unsigned char country;  /* 0: USA, 1:Canada */
} chdr_t;
  
/* API response buffer (ES:DI) header */
  
typedef struct rsp_header
{
    unsigned char err;
    unsigned char xerr;
    unsigned char fill[2];
} rhdr_t;
  
/* API commands */
                                /*--------------------------------------*/
#define SamGetVersion       0
    /* in: */                   /* chdr_t */
    /* out: */                  typedef struct {
    rhdr_t h; short version; short level;
} rspversion_t;
                                /*--------------------------------------*/
                                /* if you find a later version/level    */
                                /* than you programmed for, you can chk */
                                /* compat. by attempting to set level   */
#define SamSetLevel         1
    /* in: */                   typedef struct {
    chdr_t h; short level;
} cmdsetlevel_t;
    /* out: */                  /* rhdr_t */
                                /*--------------------------------------*/
                                /* over 500k at last count and growing! */
#define SamGetNumRecs       2
    /* in: */                   /* chdr_t */
    /* out: */                  typedef struct {
    rhdr_t h; long numrecs;
} rspnumrecs_t;
                                /*--------------------------------------*/
                                /* date of data. scope is               */
                                /* something like "All USA Calls" or    */
                                /* "District 4" or "New York"           */
#define SamGetDatabaseDate  3
    /* in: */                   /* chdr_t */
    /* out: */                  typedef struct {
    rhdr_t h;
    char date[9+1]; char scope[24+1];
} rspdbdate_t;
                                /*--------------------------------------*/
                                /* find record containing matching call */
                                /* (packflags = 0 for all unpacked)     */
#define SamFindCall         4
    /* in: */                   typedef struct {
    chdr_t h;
    short packflags;
    char call[6+1];
} cmdfindcall_t;
    /* out: */                  typedef struct {
    rhdr_t h; datarec_t d;
} rspdatarec_t;
                                /*--------------------------------------*/
                                /* get record with matching Cindex      */
#define SamGetRecordByCall  5
    /* in: */                   typedef struct {
    chdr_t h;
    short packflags;
    long index;    /* Cindex (or Nindex) */
} cmdgetrecs_t;
    /* out: */                  /* rspdatarec_t */
                                /*--------------------------------------*/
                                /* find record with matching (or close) */
                                /* name                                 */
#define SamFindName         6
    /* in: */                   typedef struct {
    chdr_t h;
    short packflags;
    char lastname[20+1];
    char firstname[11+1];
    char midinitial[1+1];
} cmdfindname_t;
    /* out: */                  /* rspdatarec_t */
                                /*--------------------------------------*/
                                /* find record with matching Nindex     */
                                /* NOTE: SamFindCall and                */
                                /* SamGetRecordbyCall return with       */
                                /* Nindex == -1                         */
#define SamGetRecordByName  7
    /* in: */                   /* cmdgetrecs_t */
    /* out: */                  /* rspdatarec_t */
                                /*--------------------------------------*/
                                /* Use this to implement fast sequential*/
                                /* searches.  Look thru records without */
                                /* unpacking them                       */
#define SamGetDictCode      8
    /* in: */                   typedef struct {
    chdr_t h;
    short dno;    /* dictionary number */
    char string[35+1];
} cmdgetdictcode_t;
    /* out: */                  typedef struct {
    rhdr_t h; long dictcode;
} rspgetdictcode_t;
                                /*--------------------------------------*/
                                /* preps SAMAPI.EXE for removal (best   */
                                /* not used, do it with SAMAPI /r)      */
#define SamRemove          20
    /* in: */                   /* chdr_t */
    /* out: */                  /* rhdr_t */
                                /*--------------------------------------*/
                                /* lookup county name from zip code     */
#define SamFindCounty      21
    /* in: */                   typedef struct {
    chdr_t h;
    char zip[5+1];
    char reserved[2];
} cmdfindcounty_t;
    /* out: */                  typedef struct {
    rhdr_t h;
    char county[31+1];
} rspfindcounty_t;
  
  
  
/* API error returns */
#define SerrNoError         0
#define SerrNotFound        1   /* normal error */
#define SerrFail            2   /* normal error for setlevel */
#define SerrBadCmd          3   /* bad input from API caller */
#define SerrFatal           4   /* SAMAPI internal error */
#define SerrCountry         6   /* invalid country */
  
  
/* Dictionary Numbers */
#define DnoLastName         0
#define DnoFirstName        1
#define DnoAddress          2
#define DnoCity             3
#define DnoState            4
  
/* Pack bits */
#define PB_LASTNAME  1
#define PB_FIRSTNAME 2
#define PB_ADDRESS   4
#define PB_CITY      8
#define PB_STATE    16
#define PB_ZIP      32
#define PB_DOB      64
#define PB_ALL      (0x7f)
  
