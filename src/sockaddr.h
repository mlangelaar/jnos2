#ifndef _SOCKADDR_H
#define _SOCKADDR_H
  
/* Berkeley format socket address structures. These things were rather
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
  
/* Socket address, AF_FILE style - WG7J */
struct sockaddr_fp {
    short sfp_family;
    char *filename;
    int len;
};

#define SOCKSIZE    (sizeof(struct sockaddr))
#define MAXSOCKSIZE SOCKSIZE /* All sockets are of the same size for now */
  
#endif /* _SOCKADDR_H */
