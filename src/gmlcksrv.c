
/*
 * 12Jun2010, Maiko (VE4KLM), A simple lock server for any JNOS system
 * to query to see if a particular mail file is locked ...
 */

#ifdef GMLOCKSERVER

static void lockserv (int, void*, void*);

/* Start up Global Mail Lock Server */
int gmlock1 (int argc, char **argv, void *p)
{
    int port;

    if (argc < 2)
        port = 4500;
    else
        port = atoi (argv[1]);

    return start_tcp (port, "Global Mail Lock Server", lockserv, 2048);
}

/* Stop Global Mail Lock Server */
int gmlock0 (int argc, char **argv, void *p)
{
    int port;

    if (argc < 2)
        port = 4500;
    else
        port = atoi (argv[1]);

    return stop_tcp (port);
}

/* Global Mail Lock Server */
static void lockserv (int s, void *unused, void *p)
{
	char cmd[20];

	sockmode(s,SOCK_ASCII);
	sockowner(s,Curproc);

	log(s,"open global mail lock");

	while (recvline (s, cmd, 19) > 0)
	{
		log (-1, "[%s]", cmd);
	}

	log(s,"close global mail lock");

	close_s(s);
}

#endif /* GMLOCKSERVER */
