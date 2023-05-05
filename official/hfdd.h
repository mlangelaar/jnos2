
/*
 * 21Jan2005, Maiko, Guess I should setup a header file for declaring
 * stuff like function prototypes, external variables, and other stuff
 * related to the HFDD (HF Digital Device) functionality.
 */

#ifndef	_HFDD_INCLUDE_
#define	_HFDD_INCLUDE_

/*
 * 02Feb2005, Maiko, Need a structure to pass more data to newproc
 */
typedef struct {

	int keyboard;	/* someone at the keyboard or forwarding ? */

	char *call;	/* station being called or connect with */

	char *iface;	/* name of async (tnc) interface */

	int pactor;	/* to flag use of changeovers */

} HFDD_PARAMS;

extern int hfdd_debug;	/* located in hfddrtns.c module */

/* file : hfddrtns.c */

extern int hfdd_is_ptc (char*);
extern int hfdd_is_dxp (char*);
extern int hfdd_iface (char*);
extern int hfdd_is_kam (char*);
extern int hfdd_is_pk232 (char*);

extern void hfdd_console (int, void*, void*);

extern int hfdd_connect (char*);

extern int hfdd_client (int, char**, void*);

extern void hfdd_send (int, char*, int);

extern void hfdd_mbox (int, int*, void*, void*);

extern void set_hfdd_iss ();
extern void set_hfdd_irs ();

extern int hfdd_iss ();
extern int hfdd_irs ();

/* file : hfdd.c */

extern void dxp_machine (int, HFDD_PARAMS*);
extern void hfdd_out (int, void*, void*);
extern void dxp_disconnect (int);
extern void dxp_changeover (int, int);
extern void dxp_FEC (int, int);

/* file : ptcpro.c */

extern void ptc_machine (int, char*);
extern void ptc_send_data (int, char*, int);
extern void ptc_changeover (int, char*, int);
extern void ptc_out (int, void*, void*);
extern void set_ptc_disc_flag ();
extern void ptc_FEC ();
extern void ptc_make_call (char*);
extern void ptc_exit_hostmode (int);

/* file : kam.c */

extern void kam_machine (int, HFDD_PARAMS*);
extern void kam_changeover (int, char*, int);
extern void kam_send_data (int, char*, int);
extern void set_kam_disc_flag ();
extern void kam_FEC ();
extern void kam_make_call (char*);
extern void kam_exit_hostmode (int);
extern void kam_dump (int, unsigned char*);

/* file : pk232.c - 10Mar2009, Maiko, Should add these prototypes */

extern void pk232_connect (char*, char*);
extern void pk232_machine (int, HFDD_PARAMS*);
extern void pk232_changeover (int, char*, int);
extern void pk232_disconnect (int);
extern void pk232_send_data (int, char*, int);
extern void pk232_FEC (int);
extern void pk232_exit_hostmode (int);
extern void pk232_dump (int, unsigned char*);

/* file : hfddinit.c */

extern void hfdd_init_tnc (int); /* new for 24May07 */
extern void hfdd_empty_tnc (int); /* 10Mar2009, Maiko, Forgot this one */

/* file : hfddq.c - 10Mar2009, Maiko, Should add proper prototypes */

extern void NEW_hfdd_send (int, char*, int);
extern void priority_send (int, char*, int);
extern int send_tnc_block ();
extern void delete_priority ();

/* file : winmor.c - 07Apr2010, Maiko, new WINMOR support */


extern void winmor_machine (int, HFDD_PARAMS*);
extern void winmor_make_call (char*);
extern void winmor_send_data (char*, int);
extern void winmor_changeover (int);
extern void set_winmor_disc_flag ();

#endif
