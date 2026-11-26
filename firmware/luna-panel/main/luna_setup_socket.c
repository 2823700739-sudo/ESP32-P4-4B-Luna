#include <string.h>
#include "lwip/sockets.h"
#include "luna_setup_socket.h"

esp_err_t luna_setup_ap_socket(httpd_handle_t handle, int fd)
{
    (void)handle;
    /* IDF httpd uses AF_INET6 when IPv6 is enabled. lwIP then returns an
       IPv4-mapped sockaddr_in6 for IPv4 clients; sockaddr_in would truncate it. */
    struct sockaddr_storage local={0};socklen_t size=sizeof(local);
    if (getsockname(fd,(struct sockaddr *)&local,&size)!=0 || size>(socklen_t)sizeof(local)) return ESP_FAIL;
    if (local.ss_family==AF_INET && size>=(socklen_t)sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *v4=(const struct sockaddr_in *)&local;
        return ntohl(v4->sin_addr.s_addr)==0xc0a80401u?ESP_OK:ESP_FAIL;
    }
#if CONFIG_LWIP_IPV6
    if (local.ss_family==AF_INET6 && size>=(socklen_t)sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *v6=(const struct sockaddr_in6 *)&local;
        static const unsigned char mapped_ap[16]={0,0,0,0,0,0,0,0,0,0,0xff,0xff,192,168,4,1};
        return memcmp(&v6->sin6_addr,mapped_ap,sizeof(mapped_ap))==0?ESP_OK:ESP_FAIL;
    }
#endif
    return ESP_FAIL;
}
