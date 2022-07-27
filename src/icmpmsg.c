/* ICMP message type tables
 * Copyright 1991 Phil Karn, KA9Q
 */
  
#include "global.h"
  
/* ICMP message types */
char *Icmptypes[] = {
    "Echo Reply",
    NULLCHAR,
    NULLCHAR,
    "Unreachable",
    "Source Quench",
    "Redirect",
    NULLCHAR,
    NULLCHAR,
    "Echo Request",
    NULLCHAR,
    NULLCHAR,
    "Time Exceeded",
    "Parameter Problem",
    "Timestamp",
    "Timestamp Reply",
    "Information Request",
    "Information Reply"
};
  
/* ICMP unreachable messages */
char *Unreach[] = {
    "Network",
    "Host",
    "Protocol",
    "Port",
    "Fragmentation",
    "Source route",
    "Dest net unknown",
    "Dest host unknown",
    "Source host isolated",
    "Net prohibited",
    "Host prohibited",
    "Net TOS",
    "Host TOS",
    "Administratively Prohibited"
};
/* ICMP Time exceeded messages */
char *Exceed[] = {
    "Time-to-live",
    "Fragment reassembly"
};
  
/* ICMP redirect messages */
char *Redirect[] = {
    "Network",
    "Host",
    "TOS & Network",
    "TOS & Host"
};
  
