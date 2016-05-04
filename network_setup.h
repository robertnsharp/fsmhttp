/* network setup for listen sockets, addresses etc - header */
#pragma once

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <err.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <event.h>
#include <time.h>

struct sockaddr_storage get_listen_address(int, char*, char*);
struct sockaddr_storage get_wcard_listen_address(int, char*);
void set_flags_non_block(int);
int setup_listen_socket(struct sockaddr_storage*);
