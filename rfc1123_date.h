/* date formatting in rfc822 / rfc1123 date format */
#pragma once

#include <stddef.h>
#include <time.h>

int write_rfc1123_date(char*, time_t, size_t);
