/* access logging writes timestamped info after connection finishes */

#include "access_log.h"

#define ACCESS_LOG_BUF_SIZE 1024

void                                                                            
log_connection(FILE *access_log_file, struct client_connection *con) {
	
 	char buf[ACCESS_LOG_BUF_SIZE];
	int len = 0; /* len of chars written */
	char *empty_field = "-"; /* filler where no con state for field */
	char *method;
	int log_response_code = 404; /* default resp code 404 */
	void *addr; /* socket addr, ipv6 or ipv4 */
    long response_bytes_size;

	/* zero out buf to start */
	memset(buf, 0, ACCESS_LOG_BUF_SIZE);

	/* get string of method name */
	switch(con->parser.method) {
		case HTTP_GET:
			method = "GET";
			break;
		case HTTP_HEAD:
			method = "HEAD";
			break;
		default:
			method = empty_field;
	}

	/* unpack ip address (could be ipv6 or ipv4) */
	addr = &((struct sockaddr_in*)(&con->client_addr))->sin_addr;

	/* write ip address */
	if(inet_ntop(con->client_addr.ss_family, addr, buf + len,
		sizeof(struct sockaddr_storage)) == NULL) {
		err(1, "ip address conversion failed during logging");
	}

	/* since ip address length is variable, jump forward until we get back
	 * at the end of the string */
	while(buf[len] != '\0') {
		len++;
	}

	/* write space and open bracket */
	len += snprintf(buf + len, ACCESS_LOG_BUF_SIZE - len, " [");

	/* write timestamp */
	len += write_rfc1123_date(buf + len, time(NULL),
			ACCESS_LOG_BUF_SIZE - len);

	/* write close bracket, method name, space */
	len += snprintf(buf + len, ACCESS_LOG_BUF_SIZE - len, "] \"%s ",
			method);

	/* write URL, or if no URL use default */
	if(con->url == NULL) {
		/* no URL parsed */
		len += snprintf(buf + len, ACCESS_LOG_BUF_SIZE - len, "%s",
				empty_field);
	} else {
		memcpy(buf + len, con->url, con->url_length);
		len += con->url_length;
	}

	/* write close quote, response code. default response code 404 for
	 * connections where response was not generated */
	if(con->resp_code == RESPONSE_CODE_UNINITIALISED) {
		log_response_code = 404;
	} else {
		log_response_code = con->resp_code;
	}

    /* response size is 0 if not a 200 OK */
    if(con->resp_code != RESPONSE_CODE_OK) {
        response_bytes_size = 0;
    } else {
        response_bytes_size = con->file_size;
    }

	len += snprintf(buf + len, ACCESS_LOG_BUF_SIZE - len, "\" %d %ld\n",
			log_response_code, response_bytes_size);

	/* terminate string and write log line */
	buf[len] = '\0';
	if(fputs(buf, access_log_file) == EOF) {
		err(1, "error writing access log line");
	}

	fflush(access_log_file); /* flush just in case */
}
