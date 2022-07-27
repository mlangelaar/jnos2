
/*
 * APRS Services for JNOS 111x, TNOS 2.30 & TNOS 2.4x
 *
 * April,2001-June,2004 - Release (C-)1.16+
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 */

#ifndef	_MSGDB_H
#define	_MSGDB_H

typedef struct {

	long	time;
	char	from[10];
	char	to[10];
	char	status[7];	/* 12Apr2004 */
	char	msg[100];

} MSGDBREC;

extern MSGDBREC *msgdb_tailrecs (int);

#endif
