#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "cJSON.h"

bool luna_setup_request_allowed(const char *host,const char *origin,const char *provided,const char *expected);
cJSON *luna_setup_request_parse(const char *data,size_t length);
void luna_setup_request_dispose(cJSON *root);
