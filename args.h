/* command line argument parsing - header */
#pragma once

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/cdefs.h>
#include <sys/stat.h>
#include <err.h>

struct cl_args {
	int address_family; /* AF_INET or AF_INET6 from socket.h */
	FILE *access_log_file;	/* null ptr for no access logging */
	int daemonise;	/* 1 iff we're daemonising, otherwise run foreground */
			/* note that if running in foreground, access log will
			   be written to stdout */
	char *address;	/* listen address. null ptr if use wildcard address */
	char *service_or_port;	/* listen port number or service name */
	char *directory;	/* directory to serve files from */
};

struct cl_args get_args(int, char**);

#ifdef __OpenBSD__
__dead
#endif
void usage(void);

