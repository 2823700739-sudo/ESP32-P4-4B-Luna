"""Real HTTP socket gate with OS address fixtures; no device or live AP access."""
import ctypes as C
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
SOCKETS=r'''
#pragma once
#include <stdint.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
#define CONFIG_LWIP_IPV6 1
#define getsockname test_getsockname
int test_getsockname(int,struct sockaddr*,socklen_t*);
'''
RUNTIME=r'''
#include <string.h>
#include "lwip/sockets.h"
#include "luna_setup_socket.h"
static int fixture,input_size;
void test_fixture(int value){fixture=value;input_size=0;}
int test_input_size(void){return input_size;}
int test_ipv6_size(void){return sizeof(struct sockaddr_in6);}
int test_getsockname(int fd,struct sockaddr*out,socklen_t*length){
    (void)fd;input_size=*length;if(fixture==6)return -1;
    struct sockaddr_storage address={0};socklen_t size;
    if(fixture==0||fixture==2){
        struct sockaddr_in*v4=(struct sockaddr_in*)&address;v4->sin_family=AF_INET;
        v4->sin_addr.s_addr=htonl(fixture==0?0xc0a80401u:0xc0a80102u);size=sizeof(*v4);
    }else{
        struct sockaddr_in6*v6=(struct sockaddr_in6*)&address;v6->sin6_family=AF_INET6;
        unsigned char bytes[16]={0,0,0,0,0,0,0,0,0,0,0xff,0xff,192,168,4,1};
        if(fixture==3)bytes[15]=2; /* Same subnet still forbidden. */
        if(fixture==4){bytes[0]=0xfe;bytes[1]=0x80;} /* Native IPv6 forbidden. */
        memcpy(&v6->sin6_addr,bytes,sizeof(bytes));size=sizeof(*v6);
    }
    if(fixture==5)size=2;
    if(fixture==7)address.ss_family=123;
    memcpy(out,&address,*length<size?*length:size);*length=size;return 0;
}
int test_former_gate(void){
    struct sockaddr_in local={0};socklen_t size=sizeof(local);
    return getsockname(1,(struct sockaddr*)&local,&size)==0 && local.sin_family==AF_INET &&
        ntohl(local.sin_addr.s_addr)==0xc0a80401u?ESP_OK:ESP_FAIL;
}
'''

@unittest.skipUnless(shutil.which('gcc'),'existing GCC required')
class SetupSocketTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='luna-setup-socket-');folder=Path(cls.temp.name)
        (folder/'lwip').mkdir();(folder/'lwip/sockets.h').write_text(SOCKETS)
        (folder/'esp_http_server.h').write_text('#pragma once\ntypedef int esp_err_t;\ntypedef void*httpd_handle_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n')
        (folder/'runtime.c').write_text(RUNTIME)
        main=ROOT/'firmware/luna-panel/main';dll=folder/'socket.dll'
        cmd=['gcc','-std=c11','-shared','-O1','-Wall','-Wextra','-Werror','-I',str(folder),'-I',str(main),
             str(main/'luna_setup_socket.c'),str(folder/'runtime.c'),'-o',str(dll)]
        if sys.platform=='win32':cmd+=['-lws2_32']
        result=subprocess.run(cmd,capture_output=True)
        if result.returncode:raise AssertionError(result.stderr.decode(errors='replace'))
        cls.lib=C.CDLL(str(dll));cls.lib.luna_setup_ap_socket.argtypes=[C.c_void_p,C.c_int]
    @classmethod
    def tearDownClass(cls):
        if sys.platform=='win32':
            C.windll.kernel32.FreeLibrary.argtypes=[C.c_void_p];C.windll.kernel32.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()
    def gate(self,fixture):
        self.lib.test_fixture(fixture);return self.lib.luna_setup_ap_socket(None,1)
    def test_ipv4_setup_destination_allowed(self):self.assertEqual(self.gate(0),0)
    def test_mapped_ipv4_reproduces_former_rejection_then_passes(self):
        self.lib.test_fixture(1);self.assertEqual(self.lib.test_former_gate(),-1)
        self.assertEqual(self.gate(1),0)
        self.assertGreaterEqual(self.lib.test_input_size(),self.lib.test_ipv6_size())
    def test_lan_and_other_mapped_addresses_remain_rejected(self):
        for fixture in [2,3,4]:self.assertEqual(self.gate(fixture),-1)
    def test_truncated_failed_and_unknown_addresses_fail_closed(self):
        for fixture in [5,6,7]:self.assertEqual(self.gate(fixture),-1)
