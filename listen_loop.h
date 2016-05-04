/* Libevent v1 http server event loop - header */
#pragma once

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <errno.h>
#include <netdb.h>
#include <err.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <event.h>
#include <time.h>

#include "network_setup.h"
#include "args.h"
#include "rfc1123_date.h"
#include "http-parser/http_parser.h"

/* start with a 1k buffer for incoming requests, and do a doubling realloc
 * if we need more */
#define REQUEST_HEADER_BUF_START_SIZE (1024)

/* just use a fixed size allocation for the response buffer for now */
#define RESPONSE_BUF_SIZE (1024)

/* the state of a given client connection. we transition forward */
enum con_status {
	NEW_CONNECTION_HEADERS_INCOMPLETE = 0,
	HEADERS_COMPLETE,
	SENDING_ERROR_RESPONSE_CODE,
	SENDING_RESPONSE_FILE,
	CLEAN_CONNECTION_SHUTDOWN
};

enum response_code {
	RESPONSE_CODE_UNINITIALISED = 0,
	RESPONSE_CODE_OK = 200,
	RESPONSE_CODE_BAD_REQ = 400,
	RESPONSE_CODE_FORBIDDEN = 403,
	RESPONSE_CODE_NOT_FOUND = 404,
	RESPONSE_CODE_METHOD_NOT_ALLOWED = 405,
	RESPONSE_CODE_INTERNAL_SERVER_ERROR = 500
};

/* state for each client connection, include http parser and libevent state */
struct client_connection {

	/* we transition forward through the states */
	enum con_status status;

	/* the fd for the client socket connection */
	int fd;

	/* the address of the client we're connected to */
	struct sockaddr_storage client_addr;

	/* incoming data buffer, big enough for a reasonable request.
	 * If we run out of room in the buffer, we realloc to double the
	 * size, up to size HTTP_MAX_HEADER_SIZE */
	int request_buf_size;
	char *request_buf;
	int request_buf_bytes_read;

	/* http parser */
	struct http_parser parser;
	struct http_parser_settings parser_settings;

	/* url data parsed from request */
	const char *url; /* this will NOT be null terminated, use url_length */
	size_t url_length;

	/* libevent */
	struct event ev_read;
	struct event ev_write;

	/* We build the response headers once, when libevent first tells
	 * us we can write.
	 *
	 * Since each time we can write to the socket we could write 0-N
	 * bytes at a time, we also keep track of how many of the header
	 * bytes we've already written.
	 *
	 * Lastly, we store the length of the headers when we build them,
	 * because they're not necessarily nul terminated, and there's
	 * no point scanning for nul if we already know the length */
	char *resp_headers;
	int resp_headers_written;
	int resp_headers_length;

	/* if we got a valid request, this is the file we're sending */
	FILE *file_being_sent;

	/* this is the file size in bytes, determined by a call to fstat() */
	long file_size;

	/* This is the optimal read size for the device the file is
	 * residing on. We get this for each file because files in our mirror
	 * directory could be on different devices. This is determined by a
	 * call to fstat() */
	int file_read_size;

	/* last modified time of the file, determined by a call to fstat() */
	time_t file_last_modified;

	/* File read buffer, used when streaming data from disk to socket.
	 * Size of the buffer (in bytes) is equal to file_read_size */
	char *file_read_buf;

	/* this is the HTTP response code we're sending */
	enum response_code resp_code;
};

int listen_loop(char*, FILE*, int);
void event_handler_accept(int, short, void*);
void event_handler_read(int, short, void*);
void event_handler_write(int, short, void*);
int on_url_parsed(http_parser*, const char*, size_t);
int on_headers_complete(http_parser*);
void process_request(struct client_connection*);
void prepare_error_code_response(struct client_connection*,
		enum response_code);
int write_common_headers(struct client_connection*);
void build_error_headers(struct client_connection*);
void build_file_headers(struct client_connection*);
int get_file_length(FILE*);
struct tm* get_last_file_modified_time_gmt(FILE*);
int write_headers_to_sock(struct client_connection*);
int write_file_to_sock(struct client_connection*);
void clean_shutdown(struct client_connection*);
void end_connection(struct client_connection*);
