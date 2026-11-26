#pragma once
#include "esp_http_server.h"

/* HTTP open callback: accept only the setup AP destination on dual-stack sockets. */
esp_err_t luna_setup_ap_socket(httpd_handle_t handle, int fd);
