/* network setup for listen sockets, addresses etc */

#include "network_setup.h"

struct sockaddr_storage get_listen_address(
		int address_family, /* from socket.h */
		char *address,
		char *service_or_port) {

	struct addrinfo *address_info_head, *address_info;

	/* sockaddr_storage allocation is required, because sockaddr_in can't
	 * fit an ipv6 address */
	struct sockaddr_storage return_sock_addr;

	/* If address is null ptr, get wildcard address */
	if(address == NULL) {
		return get_wcard_listen_address(address_family,
				service_or_port);
	}

	/* Get address info from address */
	if(getaddrinfo(address, service_or_port, NULL, &address_info) != 0) {
		err(1, "error getting address info");
	}

	/* store head for free later */
	address_info_head = address_info;

	/* Iterate through address info linked list, until we find the address
	 * family that we're looking for */
	while(address_info->ai_family != address_family) {
		
		/* if we're at the end of the list, fail */
		if(address_info->ai_next == NULL) {
			err(1, "error getting address info");
		}

		/* jump to next in ll */
		address_info = address_info->ai_next;
	}

	/* copy the address we've chosen */
	memcpy(&return_sock_addr, address_info->ai_addr,
			address_info->ai_addrlen);

	/* free the address info */
	freeaddrinfo(address_info_head);

	return return_sock_addr;
}

struct sockaddr_storage get_wcard_listen_address(
		int address_family,
		char* service_or_port) {
	
	if(address_family == AF_INET6) {
		/* If we're using ipv6, use ipv6 wildcard */
		return get_listen_address(address_family,
				"::", service_or_port);
	} else {
		/* Otherwise use ipv4 wildcard */
		return get_listen_address(address_family, "0.0.0.0",
				service_or_port);
	}
}

/* set the flags on an fd to indicate we want non-blocking reads and writes.
 * exits application on error */
void set_flags_non_block(int fd) {

	int flags;

	flags = fcntl(fd, F_GETFL);
	if(flags < 0) {
		err(1, "error getting fd flags");
	}

	flags |= O_NONBLOCK;

	if(fcntl(fd, F_SETFL, flags) < 0) {
		err(1, "error setting fd flags");
	}
}

int setup_listen_socket(struct sockaddr_storage* address) {

	int fd, optVal;
	size_t sockaddr_size;

	/* open listen socket of the given address family */
	if((fd = socket(address->ss_family, SOCK_STREAM, 0)) < 0) {
		err(1, "open listen socket failed");
	}

	/* allow address (port number) to be reused immediately */
	optVal = 1;
	if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optVal,
				sizeof(int)) < 0) {
		err(1, "setting listen socket option failed");
	}

	/* listen non blocking */
	set_flags_non_block(fd);

	/* determine socket data size based on address family */
	switch(address->ss_family) {
		case AF_INET:
			sockaddr_size = sizeof(struct sockaddr_in);
			break;
		case AF_INET6:
			sockaddr_size = sizeof(struct sockaddr_in6);
			break;
		default: err(1, "unsupported address type");
	}

	/* bind socket to the address */
	if(bind(fd, (struct sockaddr*)address, sockaddr_size) < 0) {
		err(1, "bind failed");
	}

	/* start listening, queuing the max number of con requests */
	if(listen(fd, SOMAXCONN) < 0) {
		err(1, "listen failed");
	}

	return fd;
}
