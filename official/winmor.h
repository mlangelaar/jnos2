#ifndef	_WINMOR_H
#define _WINMOR_H

typedef struct iwmp {
	char *host;
	int port;
	int ctrl;
} IWMP;

extern int connect_winmor (IWMP*);

#endif
