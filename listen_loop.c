/* Libevent v1 http server event loop */

#include "listen_loop.h"
#include "access_log.h"

/* cheeky file-scope vars to avoid throwing duplicate pointers around for     
 * file serving directory and access log */
static char *file_serving_directory;
static int file_serving_directory_len;
static FILE *access_log_file;

int listen_loop(char *directory, FILE *access_log, int listen_fd) {

	struct event accept_event;

	/* store file serving directory and its length in file scope global */
	file_serving_directory = directory;
	file_serving_directory_len = strlen(file_serving_directory);

	/* store access log (could be null ptr if logging off) */
	access_log_file = access_log;

	/* init libevent */
	event_init();

	/* setup event for connection accepts, with no argument */
	event_set(&accept_event, listen_fd, EV_READ|EV_PERSIST,
			event_handler_accept, NULL);

	/* listen for connection accept events forever */
	event_add(&accept_event, NULL);

	/* start libevent event loop - only exits on error */
	event_dispatch();

	/* error? */
	return -1;
}

/* ---------- request handling ---------- */

/* by this point we know that we've got a HEAD or GET method (we'll need to
 * check which to determine whether to send the body), and we should have
 * a pointer to the URL and its length. we can therefore determine whether
 * the requested file exists in our mirror directory, and prepare to service
 * the request by opening files etc, or alternatively prepare an error
 * response */
void process_request(struct client_connection *con) {

	struct http_parser_url parsed_url;
	char *req_path, *real_path;
	uint16_t off, len; /* offset and length for parsed url in url buf */
	struct stat file_stat; /* file status, used for getting sizes */

	/* if URL length is 0, fail */
	if(con->url_length == 0) {
		prepare_error_code_response(con, RESPONSE_CODE_BAD_REQ);
		return;
	}

	/* try to parse URL using http_parser_parse_url and see if we can find
	 * the local file */
	if(http_parser_parse_url(con->url, con->url_length, 0, &parsed_url)
		       	!= 0) {
		/* parse failure */
		prepare_error_code_response(con, RESPONSE_CODE_BAD_REQ);
		return;
	}

	/* parse was successful - the only thing we care about is the path.
	 * If there's no path present in the URL, return 404 */
	if(!(parsed_url.field_set & (1 << UF_PATH))) {
		/* no path in URL */
		prepare_error_code_response(con, RESPONSE_CODE_NOT_FOUND);
		return;
	}

	/* get offset and length for URL in buf */
	off = parsed_url.field_data[UF_PATH].off;
	len = parsed_url.field_data[UF_PATH].len;

	/* if path URL length is now 0, fail */
	if(len == 0) {
		prepare_error_code_response(con, RESPONSE_CODE_NOT_FOUND);
		return;
	}

	/* malloc enough space for the file serving directory prefix,
	 * the file path (from the parsed URL), and nul terminator */
	req_path = malloc((sizeof(char) * (file_serving_directory_len + len))
			+ 1);

	/* on malloc failure, try to return internal server error */
	if(req_path == NULL) {
		prepare_error_code_response(con,
				RESPONSE_CODE_INTERNAL_SERVER_ERROR);
		return;
	}

	/* concat file serving directory and request path */
	memcpy(req_path, file_serving_directory, file_serving_directory_len);
	memcpy(req_path + file_serving_directory_len, con->url + off, len);
	req_path[file_serving_directory_len + len] = '\0';

	/* protect against up directory paths in the trailing path part,
	 * but not the preceding file serving part of the path */
	if(strstr(req_path + file_serving_directory_len, "../") != NULL) {
		free(req_path); /* clean up */
		prepare_error_code_response(con, RESPONSE_CODE_NOT_FOUND);
		return;
	}

	/* get real path (this will follow symlinks for us) - note this is
	 * malloc'd memory so needs to be free'd */
	if((real_path = realpath(req_path, NULL)) == NULL) {
		/* getting real path failed */
		free(req_path);

		switch(errno) {
			/* malloc failure in realpath */
			case ENOMEM:
				prepare_error_code_response(con,
						RESPONSE_CODE_INTERNAL_SERVER_ERROR);
				break;
			/* path access denied */
			case EACCES:
				prepare_error_code_response(con,
						RESPONSE_CODE_FORBIDDEN);
				break;
			/* other failure */
			default:
				prepare_error_code_response(con,
						RESPONSE_CODE_NOT_FOUND);
		}

		return;
	}

	/* try to open the given file (real path) in read binary mode.
	 * if it doesn't exist, return 404 */
	if((con->file_being_sent = fopen(real_path, "rb")) == NULL) {

		free(req_path); /*clean up */
		free(real_path);

		/* if access denied, return forbidden */
		if(errno == EACCES) {
			prepare_error_code_response(con,
					RESPONSE_CODE_FORBIDDEN);
		} else {
			/* other error, presume not found */
			prepare_error_code_response(con,
					RESPONSE_CODE_NOT_FOUND);
		}

		return;
	}

	/* by this point we no longer need the paths */
	free(req_path);
	free(real_path);

	/* get file size, optimal read size and last modified date of the
	 * REAL path */
	fstat(fileno(con->file_being_sent), &file_stat);
	con->file_size = file_stat.st_size;
	con->file_read_size = file_stat.st_blksize;
	con->file_last_modified = file_stat.st_mtim.tv_sec;

	/* only serve real files */
	if(!S_ISREG(file_stat.st_mode)) {
		/* not a regular file */
		prepare_error_code_response(con, RESPONSE_CODE_NOT_FOUND);
		return;
	}

	/* malloc buffer for file reads */
	con->file_read_buf = malloc(sizeof(char) * con->file_read_size);

	/* on malloc failure, return internal server error */
	if(con->file_read_buf == NULL) {
		free(req_path);
		free(real_path);
		prepare_error_code_response(con,
				RESPONSE_CODE_INTERNAL_SERVER_ERROR);
		return;
	}

	/* update state to indicate we're in a valid file sending state */
	con->status = SENDING_RESPONSE_FILE;

	/* update response code to 'OK' because we're sending the file */
	con->resp_code = RESPONSE_CODE_OK;
}

void prepare_error_code_response(struct client_connection *con,
	       enum response_code resp_code) {

	/* update state to indicate we're going to send an error response */
	con->status = SENDING_ERROR_RESPONSE_CODE;

	/* store the response code */
	con->resp_code = resp_code;
}

/* ---------- http-parser callbacks ---------- */

/* since this callback can be called an arbitrary number of times, each of
 * which could have a bit more data, we just store the url pointer and
 * the length each time, and wait for the headers complete callback */
int on_url_parsed(http_parser *parser, const char *at, size_t length) {
	
	struct client_connection *con;

	/* get connection data ptr from parser void ptr */
	con = parser->data;

	/* store current ptr to URL and current length */
	con->url = at;
	con->url_length = length;

	return 0; /* return indicating all OK to the parser */
}

/* called by the parser once we've read all request headers. by this point
 * we should have a URL, and can decide whether there's a file to respond
 * with, or whether we need to return an error code response */
int on_headers_complete(http_parser *parser) {

	struct client_connection *con;
	con = parser->data;

	/* headers complete should only happen once, so only continue if
	 * the current state indicates it hasn't happened already */
	if(con->status != NEW_CONNECTION_HEADERS_INCOMPLETE) {
		return 0;
	}

	/* set state to indicate headers complete */
	con->status = HEADERS_COMPLETE;
	
	/* if method is not GET or HEAD, fail. otherwise parse the request
	 * using the URL that we've stored (presumably) from on_url_parsed
	 * callbacks, and transition the connection status to one of the
	 * SENDING_ response states */
	switch(parser->method) {
		case HTTP_GET:
		case HTTP_HEAD:
			process_request(con);
			break;
		default:
			prepare_error_code_response(con,
					RESPONSE_CODE_METHOD_NOT_ALLOWED);
	}

	/* now that we've processed the request and know how we're responding,
	 * setup the write event where we'll start sending the request.
	 * The write event is setup as persistent - we'll need to delete
	 * it once we've done writing the request and have cleaned up memory
	 * allocations etc */
	event_set(&con->ev_write, con->fd, EV_WRITE|EV_PERSIST,
			event_handler_write, con);
	event_add(&con->ev_write, NULL); /* add with no timeout */

	return 0; /* indicate to parser that all is OK */
}

/* ---------- response builders & writers ---------- */

/* read a defined buffer size of data (or a smaller amount) from the
 * file, and write as much of it as possible to the socket. if we write an
 * incomplete buffer (so not all data is successfully written to the socket),
 * then seek the file back so that the unsent data is read again later.
 *
 * Returns 1 iff there's still unsent data to be read from the file, otherwise
 * returns 0. */
int write_file_to_sock(struct client_connection* con) {

	off_t bytes_remaining;
	int read_size;
	size_t bytes_read;
	ssize_t bytes_written;
	long seek_pos;
	long seek_back;

	/* get seek position - if getting position failed, just bail out
	 * and say we're done - it shouldn't fail. */
	if((seek_pos = ftell(con->file_being_sent)) == -1) {
		return 0; /* indicate that we're done writing */
	}

	/* we assume that we're seeked to the position of the next byte that
	 * needs to be written out to the client, so get seek position to
	 * determine how many bytes we have left to read */
	bytes_remaining = con->file_size - seek_pos;

	/* if none remaining, bail out, returning 0 indicating we're done */
	if(bytes_remaining == 0) {
		return 0; /* done writing file */
	}

	/* determine our read size. read size is the smaller of the determined
	 * optimal read size, or the bytes remaining. */
	if(bytes_remaining < con->file_read_size) {
		read_size = bytes_remaining;
	} else {
		read_size = con->file_read_size;
	}

	/* read that many bytes into the start of the buffer */
	bytes_read = fread(con->file_read_buf, sizeof(char), read_size,
			con->file_being_sent);

	/* in weird scenarios, we might get end of file or an error before we
	 * expect to be finished. handle these by just stopping the file
	 * transfer - we can't tell the client something went wrong, because
	 * we've already sent the http headers. */
	if(feof(con->file_being_sent)) {
		return 0; /* indicate no more writing to do */
	}

	if(ferror(con->file_being_sent)) {
		return 0; /* indicate no more writing */
	}

	/* write as many of those bytes out into the socket */
	bytes_written = write(con->fd, con->file_read_buf, bytes_read);

	/* check for failed write that isn't telling us to try again */
	if(bytes_written == -1 && errno != EAGAIN) {
		return 0; /* just say we're done writing */
	}

	/* if we wrote less than we read, seek back */
	seek_back = bytes_written - bytes_read;
	if(seek_back < 0) {
		if(fseek(con->file_being_sent, seek_back, SEEK_CUR) == -1) {
			/* file seek failed. too late to tell the client,
			 * because headers are already sent, so just say
			 * that we're done writing */
			return 0;
		}
	}

	/* determine if we're done writing */
	bytes_remaining = bytes_remaining - bytes_written;
	return bytes_remaining != 0; /* return 1 if bytes left to write */
}

/* writes all headers to the socket - returns 1 iff there's still headers that
 * need to be written (in future calls). returns -1 on failure. */
int write_headers_to_sock(struct client_connection* con) {

	int bytes_remaining, bytes_written;

	/* calculate how many bytes left to write */
	bytes_remaining = con->resp_headers_length - con->resp_headers_written;

	/* if 0 bytes remaining, return 0 indicating we're done */
	if(bytes_remaining == 0) {
		return 0;
	}
	
	/* write as many as we can */
	bytes_written =
		write(con->fd, con->resp_headers, bytes_remaining);

	/* check for failed write that isn't telling us to retry */
	if(bytes_written == -1 && errno != EAGAIN) {
		return -1; /* indicate failure */
	}

	/* update bytes written so far */
	con->resp_headers_written += bytes_written;

	/* recalc remaining bytes */
	bytes_remaining = con->resp_headers_length - con->resp_headers_written;

	/* return 1 iff there's still bytes remaining */
	return bytes_remaining != 0;
}

/* write headers common to both success and failure responses,
 * and return how many chars we wrote */
int write_common_headers(struct client_connection *con) {
	
	int off = 0; /* bytes written offset */

	/* write status line with response code */
	off += snprintf(con->resp_headers + off, RESPONSE_BUF_SIZE - off,
		       	"HTTP/1.0 %d \r\n",
			con->resp_code);

	/* get GMT time and write date header */
	off += snprintf(con->resp_headers + off, RESPONSE_BUF_SIZE - off,
		       	"Date: ");
	off += write_rfc1123_date(con->resp_headers + off, time(NULL),
		RESPONSE_BUF_SIZE - off);

	/* date format function doesn't include \r\n, write those */
	off += snprintf(con->resp_headers + off, RESPONSE_BUF_SIZE - off,
			"\r\nServer: fsmhttp\r\n");
	off += snprintf(con->resp_headers + off, RESPONSE_BUF_SIZE - off,
			"Connection: close\r\n");

	return off;
}

/* builds headers for an error response, using the error data in con state */
void build_error_headers(struct client_connection *con) {

	int off;

	/* write common headers */
	off = write_common_headers(con);

	/* terminate headers with additional carriage return & newline,
	 * to indicate that we've finished the headers. we don't write any
	 * additional (body) data for error responses, because it's not
	 * necessary to meet the spec */
	off += snprintf(con->resp_headers + off, RESPONSE_BUF_SIZE - off,
		       	"\r\n");

	/* store headers length now that we're done */
	con->resp_headers_length = off;
}

/* builds headers for a successful file transfer response */
void build_file_headers(struct client_connection *con) {

	int off;

	/* write the headers common to all responses, and store how much
	 * we've written so far */
	off = write_common_headers(con);

	/* get length of file and write content length header */
	off += snprintf(con->resp_headers + off, RESPONSE_BUF_SIZE - off,
		       	"Content-Length: %ld\r\n",
			con->file_size);
	
	/* get last modified date of file and format date string header */
	off += snprintf(con->resp_headers + off, RESPONSE_BUF_SIZE - off,
		       	"Last-Modified: ");
	off += write_rfc1123_date(con->resp_headers + off,
			con->file_last_modified, RESPONSE_BUF_SIZE - off);

	/* terminate headers with additional carriage return & newline */
	off += snprintf(con->resp_headers + off, RESPONSE_BUF_SIZE - off,
			"\r\n\r\n");

	/* store headers length */
	con->resp_headers_length = off;
}

/* ---------- libevent event handlers ---------- */

void event_handler_accept(int fd, short event, void *arg) {

	struct client_connection *con;
	socklen_t addrlen;

	/* allocate storage for connection, zero'd out */
	con = calloc(1, sizeof(struct client_connection));

	if(con == NULL) {
		/* if there's a malloc failure here, we can't even store
		 * enough state to keep track of the connection, but be nice
		 * and assume this malloc failure is transient (we might get
		 * more memory later?) */
		return;
	}

	/* allocate storage for request buffer */
	con->request_buf_size = REQUEST_HEADER_BUF_START_SIZE;
	con->request_buf = malloc(con->request_buf_size
			* sizeof(char));
	
	/* same deal as above - just bail out on malloc failure and pray to
	 * the malloc gods for more later */
	if(con->request_buf == NULL) {
		free(con); /* too early to log anything, so just clean up */
		return;
	}
	
	/* accept the incoming connection */
	addrlen = sizeof(struct sockaddr_storage);
	con->fd = accept(fd, (struct sockaddr*)&con->client_addr, &addrlen);

	if(con->fd == -1) {
		free(con); /* not a valid connection, so free allocation */
		return;
	}

	/* set fd to non blocking */
	set_flags_non_block(con->fd);

	/* setup event for when socket is ready for reading. we'll
	 * setup the write event once we've parsed a valid request */
	event_set(&con->ev_read, con->fd, EV_READ|EV_PERSIST,
			event_handler_read, con);

	/* initialise http parser for this connection */
	http_parser_init(&con->parser, HTTP_REQUEST);

	/* set parser data pointer to connection state, so we can access
	 * the state in parser callbacks */
	con->parser.data = con;

	/* setup http parser settings, on_url and on_headers callbacks */
	http_parser_settings_init(&con->parser_settings);
	con->parser_settings.on_url = on_url_parsed;
	con->parser_settings.on_headers_complete = on_headers_complete;

	/* set current state of connection to indicate we haven't got
	 * complete request headers yet */
	con->status = NEW_CONNECTION_HEADERS_INCOMPLETE;

	/* register read event now that we're ready */
	event_add(&con->ev_read, NULL); /* add with no timeout */
}

void event_handler_read(int fd, short event, void *arg) {

	struct client_connection *con;
	int this_read_bytes, bytes_remaining_in_buf, bytes_parsed;
	char *current_buf_position;

	con = arg; /* get connection state */

	/* calc bytes remaining in buffer */
	bytes_remaining_in_buf = con->request_buf_size
		- con->request_buf_bytes_read;

	/* if no room for additional bytes, double buffer */
	if(bytes_remaining_in_buf == 0) {
		con->request_buf_size *= 2; /* double size */
		con->request_buf = realloc(con->request_buf,
				sizeof(char) * con->request_buf_size);

		if(con->request_buf == NULL) {
			/* malloc failed for the request buffer, but on the
			 * off chance that the failure is transient, try to
			 * reply with an internal server error */
			prepare_error_code_response(con,
					RESPONSE_CODE_INTERNAL_SERVER_ERROR);
			return;
		}
	}

	/* get current position in buffer considering bytes already read */
	current_buf_position = con->request_buf + con->request_buf_bytes_read;

	/* read up to the point the buffer is full, or less */
	this_read_bytes = read(con->fd, current_buf_position,
			bytes_remaining_in_buf);

	/* if we get an error read, that isn't telling us to try again, try to
	 * end the connection gracefully, and to be nice we'll try to write
	 * an error response */
	if(this_read_bytes == -1 && errno != EAGAIN) {
		prepare_error_code_response(con,
				RESPONSE_CODE_INTERNAL_SERVER_ERROR);
		return;
	}

	/* if we get a 0 byte read, the other side has closed the connection,
	 * so write how this connection went on the access log, then
	 * clean up this connection, including all sockets, open files
	 * and memory allocations */
	if(this_read_bytes == 0) {
		end_connection(con);
		return;
	}

	/* store bytes read so far */
	con->request_buf_bytes_read += this_read_bytes;

	/* fire parser for bytes read so far. if we get headers complete,
	 * we'll update the state for the connection and setup a write
	 * event so that we can provide our response. otherwise we'll
	 * wait for more data so that eventually we get complete headers */
	bytes_parsed = http_parser_execute(&con->parser, &con->parser_settings,
			con->request_buf, con->request_buf_bytes_read);

	if(bytes_parsed != con->request_buf_bytes_read) {
		/* parse failed - return bad request */
		prepare_error_code_response(con, RESPONSE_CODE_BAD_REQ);
		return;
	}
}

void event_handler_write(int fd, short event, void *arg) {
	/* the write event is setup once we've decided whether we're replying
	 * with an error code, or sending a file. so this should only be
	 * called if we're in one of these two states:
	 * SENDING_ERROR_RESPONSE_CODE, 
	 * SENDING_RESPONSE_FILE */
	
	struct client_connection *con;
	int header_write_result;

	con = arg; /* get connection state */

	/* Write events should only be setup if we've got a valid request,
	 * so in all instances we need to build headers for a response.
	 * Depending on the response type (as determined by the state),
	 * build the headers if they're not already built */
	if(con->resp_headers == NULL) {

		/* malloc space for response headers */
		con->resp_headers = malloc(sizeof(char) * RESPONSE_BUF_SIZE);

		/* if we can't malloc space for a response, just shutdown the
		 * connection gracefully */
		if(con->resp_headers == NULL) {
			clean_shutdown(con);
			return;
		}

		/* otherwise build the headers for the response */
		switch(con->status) {
			case SENDING_ERROR_RESPONSE_CODE:
				build_error_headers(con);
				break;
			case SENDING_RESPONSE_FILE:
				build_file_headers(con);
				break;
			default: return; /* should not happen */
		}
	}

	/* try to write headers - returns 1 iff headers left to write, or
	 * -1 if there's an error */
	header_write_result = write_headers_to_sock(con);
	if(header_write_result == 1) {
		return; /* we'll write more on the next event fire */
	} else if (header_write_result == -1) {
		clean_shutdown(con); /* failed to write headers */
		return;
	}

	/* If we're sending a file, try to send that now, and if there's
	 * still data to write, stop (we perform socket shutdown below).
	 *
	 * Note that we only send a file for GET requests, for HEAD requests
	 * we don't send the body. */
	if(con->status == SENDING_RESPONSE_FILE
			&& con->parser.method == HTTP_GET) {
		if(write_file_to_sock(con)) {
			return; /* still data to write */
		}
	}

	/* By this point the headers have been written, and if we're sending
	 * a file body, we've finished sending that too. */
	clean_shutdown(con);
}

/* ---------- connection cleanup ---------- */

void clean_shutdown(struct client_connection* con) {
	/* We'll now transition the connection into a shutdown state, we'll
	 * shutdown our end of the socket, then wait for a read() to return 0
	 * indicating EOF, which suggests the client has closed the
	 * connection */
	con->status = CLEAN_CONNECTION_SHUTDOWN;
	shutdown(con->fd, SHUT_WR);
}

/* called once we get a 0 byte read indicating the client has gone away -
 * logs the connection to the access log, cleans up sockets, files and
 * memory allocations */
void end_connection(struct client_connection* con) {

	/* access log connection if logging on */
	if(access_log_file != NULL) {
		log_connection(access_log_file, con);
	}

	/* unregister events */
	event_del(&con->ev_read);
	event_del(&con->ev_write);

	/* free request buffer - this includes the URL */
	if(con->request_buf != NULL) {
		free(con->request_buf);
	}

	/* free response headers if they were created */
	if(con->resp_headers != NULL) {
		free(con->resp_headers);
	}

	/* close file if it was opened */
	if(con->file_being_sent != NULL) {
		fclose(con->file_being_sent);
	}

	/* free file read buffer if it was used */
	if(con->file_read_buf != NULL) {
		free(con->file_read_buf);
	}

	/* close connection socket - we've already performed a write shutdown
	 * and waited for the 0 byte read from the client, so client should
	 * have received all data by now */
	close(con->fd);

	/* free connection struct */
	free(con);
}
