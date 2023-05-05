#ifndef _SOCKADDR_H
#define _SOCKADDR_H

/*
 * Adding IPV6 sockets, mid Feb, 2023, by Maiko Langelaar, VE4KLM
 *
 * Berkeley format socket address structures. These things were rather
 * poorly thought out, but compatibility is important (or so they say).
 * Note that all the sockaddr variants must be of the same size, 16 bytes
 * to be specific. Although attempts have been made to account for alignment
 * requirements (notably in sockaddr_ax), porters should check each
 * structure.
 */
  
/* Generic socket address structure */
struct sockaddr {
    short sa_family;
    char sa_data[14];
};
  
/* This is a structure for "historical" reasons (whatever they are) */
struct in_addr {
    uint32 s_addr;	/* 29Sep2009, Maiko, Used to be unsigned long */
};

/* Socket address, DARPA Internet style */
struct sockaddr_in {
    short sin_family;
    unsigned short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

#ifdef	IPV6

#define	AF_INET6	10	/* 19Feb2023, Maiko, temporary locatioin */

/*
 * 17Feb2023, Maiko (VE4KLM), IPV6 for JNOS, ongoing work, I must be nuts !!!
 *
 * for in6_addr, this is unsigned 8 bit 'integer', 16 of them, which of course
 * means I have created work for myself, because my ipv6 address is currently
 * defined as u_int16_t address[16], figure it out later !
 *
 * Also so as to not conflict with OS definitions, note the 'j2' suffix !
 */

struct j2in6_addr {
    unsigned char s6_addr[16];  /* IPv6 address */
};

struct j2sockaddr_in6 {

    short              sin6_family;   	/* AF_INET6                 */
    int16              sin6_port;     	/* Transport layer port #   */
    uint32             sin6_flowinfo; 	/* IPv6 flow information    */
    struct j2in6_addr  sin6_addr;     	/* IPv6 address             */
    uint32             sin6_scope_id;	/* Scope apparently */
};

#endif
  
/* Socket address, AF_FILE style - WG7J */
struct sockaddr_fp {
    short sfp_family;
    char *filename;
    int len;
};

#define SOCKSIZE    (sizeof(struct sockaddr))
#define MAXSOCKSIZE SOCKSIZE /* All sockets are of the same size for now */
  
#endif /* _SOCKADDR_H */
