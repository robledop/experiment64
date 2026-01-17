#include <stdio.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

int main()
{
    struct netinfo netinfo;
    const int fd = open("/dev/eth0", O_RDONLY);
    ioctl(fd, GETNETINFO, &netinfo);

    printf("%-17s %s\n", "Interface:", "eth0");

    char ip_buf[16];
    inet_ntoa_r(netinfo.ip, ip_buf);
    printf("%-17s %s\n", "IP address:", ip_buf);

    char mac_buf[18];
    snprintf(mac_buf, sizeof(mac_buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             netinfo.mac[0], netinfo.mac[1], netinfo.mac[2],
             netinfo.mac[3], netinfo.mac[4], netinfo.mac[5]);

    printf("%-17s %s\n", "MAC address:", mac_buf);

    char mask_buf[16];
    inet_ntoa_r(netinfo.subnet_mask, mask_buf);
    printf("%-17s %s\n", "Subnet mask:", mask_buf);

    char gw_buf[16];
    inet_ntoa_r(netinfo.default_gateway, gw_buf);
    printf("%-17s %s\n", "Default gateway:", gw_buf);

    char dns_buf[16];
    inet_ntoa_r(ntohl(netinfo.dns_server), dns_buf);
    printf("%-17s %s\n", "DNS server:", dns_buf);

    close(fd);

    return 0;
}
