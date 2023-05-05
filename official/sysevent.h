
/*
 * New module for February 2, 2008 - Maiko Langelaar / VE4KLM
 */

enum j2sysevent {
	convconn,
	convdisc,
	convmsg,
	bbsconn,
	bbsdisc
};

typedef enum j2sysevent J2SYSEVENT;

extern void j2sysevent (J2SYSEVENT, char*);

