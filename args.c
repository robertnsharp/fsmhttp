/* command line argument parsing */
#include "args.h"

/* uses getopt() to get command line options */
struct cl_args get_args(int argc, char **argv) {

	int opt, use_ipv4 = 0, use_ipv6 = 0;
	struct cl_args cl_args;
	struct stat dir_stat;

	/* set default to daemonise */
	cl_args.daemonise = 1;

	/* default to no access log */
	cl_args.access_log_file = NULL;

	/* default to listen on wildcard address */
	cl_args.address = NULL;

	/* default service name is http */
	cl_args.service_or_port = "http";

	while((opt = getopt(argc, argv, "46da:l:p:")) != -1) {
		switch (opt) {
			case '4':
				use_ipv4 = 1;
				break;
			case '6':
				use_ipv6 = 1;
				break;
			case 'd': /* do NOT daemonise */
				cl_args.daemonise = 0;
				break;
			case 'a': /* option arg is access log filename */
				cl_args.access_log_file = fopen(optarg, "a");
				if(cl_args.access_log_file == NULL) {
					err(1, "access log file open failed");
				}
				break;
			case 'l': /* option arg is listen address */
				cl_args.address = optarg;
				break;
			case 'p': /* option arg is listen port */
				cl_args.service_or_port = optarg;
				break;
		}
	}

	/* Can only use ipv4 or ipv6, not both */
	if(use_ipv4 && use_ipv6) {
		err(1, "can only use ipv4 or ipv6, not both");
	}

	/* default is ipv4 if not specified */
	cl_args.address_family = AF_INET;

	if (use_ipv6) {
		cl_args.address_family = AF_INET6;
	} 

	/* last get mirror directory, it should be the next and last arg */
	if(optind == argc) { /* directory is compulsory arg */
		usage();
	}

	cl_args.directory = argv[optind];

	/* check directory exists - if stat fails or not a directory, fail*/
	if(stat(cl_args.directory, &dir_stat) || !S_ISDIR(dir_stat.st_mode)) {
		usage();
	}

	return cl_args;
}

#ifdef __OpenBSD__
__dead
#endif
void usage(void) {
	extern char *__progname;
	fprintf(stderr, "usage: %s [-46d] [-a access.log] [-l address] [-p port] directory\n", __progname);
	exit(1);
}
