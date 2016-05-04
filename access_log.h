/* access logging writes timestamped info after connection finishes - header */
#pragma once

#include <time.h>
#include <string.h>
#include <arpa/inet.h>

#include "listen_loop.h"

void
log_connection(FILE*, struct client_connection*);
