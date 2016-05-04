/* fsmhttp startup - header */
#pragma once

/* Stdlib includes */
#include <stdio.h>
#include <err.h>
#include <signal.h>

/* local includes */
#include "args.h" /* cl arg parsing */
#include "listen_loop.h"
#include "network_setup.h"

int main(int, char**);
