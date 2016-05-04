A simple event loop based HTTP file server in C, using libevent v1 and
http-parser.

The http-parser sources are included. You will need the libevent v1 shared
libraries. Use 'make' to build on OpenBSD, or 'make linux' to build on linux.

NOTE: This is intended as a minimal tech demo, and is not designed for
production use in public environments.
