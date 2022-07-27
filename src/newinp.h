#ifndef _INP2011_H
#define	_INP2011_H

/* 01Sep2011, Maiko (VE4KLM), function prototypes - newinp.c module */

extern int inp_rif_recv (struct mbuf*, struct ax25_cb*);
extern int inp_l3rtt (char*);
extern int inp_l3rtt_recv (char*, struct ax25_cb*, struct mbuf*);

/* 15Apr2016, Maiko (VE4KLM), forgot a few prototypes used in nr3.h */

extern int alias2ignore (char*);
extern int callnocall (char*);

#endif	/* end of _INP2011_H */

