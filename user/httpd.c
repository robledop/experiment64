#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <dns.h>
#include <arpa/inet.h>

#include "fcntl.h"

int main()
{
    const int fd = open("/dev/eth0", O_RDONLY);
    struct netinfo netinfo;
    ioctl(fd, GETNETINFO, &netinfo);
    close(fd);
    char ip_buf[16];
    inet_ntoa_r(netinfo.ip, ip_buf);
    printf("running httpd on ip %s port %d\n", ip_buf, 80);

    const int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) panic("socket failed\n");

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        panic("bind failed\n");

    listen(sockfd, 10);

    while (1)
    {
        struct sockaddr_in client_addr;
        const int connfd = accept(sockfd, (struct sockaddr*)&client_addr, sizeof(client_addr));

        char client_ip_buf[16];
        uint32_t client_ip = 0;
        bytes_to_ip(client_addr.sin_addr, &client_ip);
        inet_ntoa_r(ntohl(client_ip), client_ip_buf);
        printf("client connected from %s\n", client_ip_buf);

        uint8_t buf[2048] = {0};
        read(connfd, buf, sizeof(buf));

        printf("%s\n", (char*)buf);
        close(connfd);
    }

    close(sockfd);

    return 0;
}
