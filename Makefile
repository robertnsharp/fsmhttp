release:
	gcc -std=c99 -Wall -pedantic fsmhttp.c args.c listen_loop.c \
		http-parser/http_parser.c network_setup.c rfc1123_date.c \
		access_log.c -l event -o fsmhttp

debug:
	gcc -g -std=c99 -Wall -pedantic fsmhttp.c args.c listen_loop.c \
		http-parser/http_parser.c network_setup.c rfc1123_date.c \
		access_log.c -l event -o fsmhttp

linux:
	gcc -D_BSD_SOURCE -std=c99 -Wall -pedantic fsmhttp.c args.c \
		listen_loop.c http-parser/http_parser.c network_setup.c \
       		access_log.c rfc1123_date.c -l event -o fsmhttp

linux_debug:
	gcc -D_BSD_SOURCE -g -std=c99 -Wall -pedantic fsmhttp.c args.c \
		listen_loop.c http-parser/http_parser.c network_setup.c \
	       	access_log.c rfc1123_date.c -l event -o fsmhttp
