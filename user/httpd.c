#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define OK "HTTP/1.1 200 OK\r\n"
#define NOT_FOUND "HTTP/1.1 404 Not Found\r\nContent-Length: 18\r\n\r\n<h1>Not found</h1>"

typedef struct http_request
{
    char method[8];
    char path[256];
    char protocol[16];
} http_request_t;

http_request_t parse_http_request(const char* buf)
{
    http_request_t request = {0};
    sscanf(buf, "%7s %255s %15s", request.method, request.path, request.protocol);
    return request;
}

ssize_t send_200ok(const int sockfd, const char* page, struct sockaddr_in client_addr)
{
    char response[2048] = {0};
    snprintf(response, sizeof(response), OK"Connection: close\r\nContent-Length: %d\r\n\r\n%s", strlen(page), page);
    return sendto(sockfd, response, strlen(response), 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
}

[[noreturn]] int main()
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

    FILE* log_file = fopen("/var/log/httpd", "a");
    if (log_file == nullptr) panic("failed to open log file\n");

    while (1)
    {
        struct sockaddr_in client_addr = {0};
        const int connfd = accept(sockfd, (struct sockaddr*)&client_addr, sizeof(client_addr));
        if (connfd < 0)
            continue;

        char client_ip_buf[16];
        uint32_t client_ip = 0;
        bytes_to_ip(client_addr.sin_addr, &client_ip);
        inet_ntoa_r(ntohl(client_ip), client_ip_buf);

        char buf[2048] = {0};
        const ssize_t req_len = read(connfd, buf, sizeof(buf) - 1);
        if (req_len <= 0)
        {
            close(connfd);
            continue;
        }
        buf[req_len] = '\0';

        http_request_t request = parse_http_request(buf);
        fprintf(log_file, "request from %s: %s %s %s\n", client_ip_buf, request.method,
                request.path, request.protocol);

        if (strcmp(request.method, "GET") != 0)
        {
            sendto(connfd, NOT_FOUND, sizeof(NOT_FOUND), 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
            close(connfd);
            continue;
        }

        if (strcmp(request.path, "/") == 0)
        {
            const int homefd = open("/web/index.html", O_RDONLY);
            char homebuf[2048] = {0};
            read(homefd, homebuf, sizeof(homebuf));
            send_200ok(connfd, homebuf, client_addr);
            close(homefd);
        }
        else
        {
            char path[256] = {0};
            snprintf(path, sizeof(path), "/web%s", request.path);
            const int pagefd = open(path, O_RDONLY);
            if (pagefd < 0)
            {
                sendto(connfd, NOT_FOUND, sizeof(NOT_FOUND), 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
            }
            else
            {
                char pagebuf[2048] = {0};
                read(pagefd, pagebuf, sizeof(pagebuf));
                send_200ok(connfd, pagebuf, client_addr);
                close(pagefd);
            }
        }

        close(connfd);
    }
}
