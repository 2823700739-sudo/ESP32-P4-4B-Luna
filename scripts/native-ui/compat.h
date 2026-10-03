#pragma once
#include <stddef.h>
#include <time.h>
size_t strlcpy(char *dest,const char *src,size_t capacity);
struct tm *localtime_r(const time_t *value,struct tm *result);
