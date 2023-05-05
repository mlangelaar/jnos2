#ifndef _J2_ACTIVEBID
#define _J2_ACTIVEBID

/*
 * 23Oct2020 - prototypes for activeBID.c functions
 *
 * So 'fbbfwd.c' calls chk4activebid(), 'mboxmail.c' calls delactivebid()
 *
 */

extern int delactivebid (int s, char *msgbid);

extern int chk4activebid (int s, char *system, char *msgbid);

#endif
