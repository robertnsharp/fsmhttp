#include "fsmhttp.h"

int main(int argc, char** argv) {

	struct cl_args cl_args;
	struct sockaddr_storage listen_addr;
	int listen_fd;

	/* get command line args - this will exit() on bad args error */
	cl_args = get_args(argc, argv);

	/* ignore SIGPIPE */
	signal(SIGPIPE, SIG_IGN);

	/* get listen address from args */
	listen_addr = get_listen_address(cl_args.address_family,
			cl_args.address, cl_args.service_or_port);

	/* listen on socket, non blocking */
	listen_fd = setup_listen_socket(&listen_addr);

	/* daemonise if required */
	if(cl_args.daemonise) {
		/* daemonise, and:
		 * nochdir = 1, so don't change running directory
		 * noclose = 0, redirect stdin, out and err to /dev/null */
		if(daemon(1, 0) < 0) {
			err(1, "daemonise failed");
		}
	}

	/* start event loop */
	return listen_loop(cl_args.directory, cl_args.access_log_file,
			listen_fd);
}

