/* dbf.h */

#include "global.h"

/* return results in regular ascii text or in CSO nameserver format */
#define ASCIITEXT 0
#define CSOTEXT   1

#define HEADER_SIZE       32
#define FIELD_REC_SIZE    32
#define FIELD_NAME_LENGTH 12
#define MAX_FIELDS        128  /* ?? */

typedef struct {
	char name[FIELD_NAME_LENGTH];
	char type;
	int length;
	} dbf_fieldrec;

typedef struct {
	int type;
	int year, month, day;
	int hlen;
	int rlen;
	long int nrecs;
	int nflds;    /* not in the header, but why not put it here? */
	} dbf_header;

/* function prototypes */
int dbf_fields(int s, char *file, int OF);
int dbf_search(int s, char *file, char *field, char *pattern);
int dbf_getheader(FILE *f, dbf_header *H);
int dbf_getfields(FILE *f, dbf_header *H);
int dbf_getrecords(int s, FILE *f, dbf_header *H, const char *target, int n);
void dbf_do_search(int s, char *file, int type, char *Database);
void printrec(int s, char *recbuf, int nflds);
void CSO_printrec(int s, char *recbuf, int nflds, int index);
int match(int s, char *recbuf, char *fldbuf, const char *target, 
                                     int fld, int nflds, int *index);
int get_fieldno(char *field);

