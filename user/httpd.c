// A very basic web server

#include <stdio.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <hashtable.h>
#include <stdlib.h>

#define OK "HTTP/1.1 200 OK\r\nConnection: close\r\n"
#define NOT_FOUND "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 18\r\n\r\n<h1>Not found</h1>"
#define METHOD_NOT_ALLOWED "HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\nContent-Length: 27\r\n\r\n<h1>Method Not Allowed</h1>"
#define INTERNAL_SERVER_ERROR "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\nContent-Length: 30\r\n\r\n<h1>Internal Server Error</h1>"

#define MIME_TABLE_SIZE 64

typedef struct http_request
{
    char method[8];
    char path[256];
    char protocol[16];
} http_request_t;

typedef struct http_conn
{
    int connfd;
    char client_ip_buf[16];
} http_conn_t;

static ht_str_entry_t mime_table_entries[MIME_TABLE_SIZE];
static ht_str_table_t mime_table;
static int mime_table_ready      = 0;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *log_file            = nullptr;

http_request_t parse_http_request(const char *buf)
{
    http_request_t request = {0};
    sscanf(buf, "%7s %255s %15s", request.method, request.path, request.protocol);
    return request;
}

static ssize_t send_all(const int sockfd, const void *buf, const size_t len)
{
    auto data         = (const char *)buf;
    size_t sent_total = 0;
    while (sent_total < len) {
        const ssize_t sent = send(sockfd, data + sent_total, len - sent_total, 0);
        if (sent <= 0)
            return sent;
        sent_total += (size_t)sent;
    }
    return (ssize_t)sent_total;
}

static void mime_table_init(void)
{
    static const ht_str_entry_t entries[] = {
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".png", "image/png"},
        {".jpg", "image/jpg"},
        {".svg", "image/svg+xml"},
        {".ttf", "application/x-font-ttf"},
        {".woff", "application/font-woff"},
        {".woff2", "application/font-woff2"},
        {".eot", "application/vnd.ms-fontobject"},
        {".otf", "font/otf"},
        {".json", "application/json"},
        {".wasm", "application/wasm"},
        {".txt", "text/plain"},
        {".ico", "image/x-icon"},
        {".xml", "text/xml"},
        {".pdf", "application/pdf"},
        {".zip", "application/zip"},
        {".gz", "application/gzip"},
        {".mp3", "audio/mpeg"},
        {".mp4", "video/mp4"},
        {".ogg", "audio/ogg"},
        {".wav", "audio/wav"},
        {".avi", "video/avi"},
        {".mov", "video/quicktime"},
        {".flv", "video/x-flv"},
        {".exe", "application/x-msdownload"},
        {".bin", "application/octet-stream"},
        {".torrent", "application/x-bittorrent"},
        {".dmg", "application/x-apple-diskimage"},
        {".iso", "application/x-iso9660-image"},
        {".rar", "application/x-rar-compressed"},
        {".7z", "application/x-7z-compressed"},
        {".tar", "application/x-tar"},
        {".bz2", "application/x-bzip2"},
        {".xz", "application/x-xz"},
        {".zst", "application/zstd"},
    };

    if (ht_str_init(&mime_table, mime_table_entries, MIME_TABLE_SIZE) != 0) {
        mime_table_ready = 1;
        return;
    }
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i)
        ht_str_insert(&mime_table, entries[i].key, entries[i].value);
    mime_table_ready = 1;
}

static const char *mime_table_lookup(const char *ext)
{
    if (!mime_table_ready)
        mime_table_init();

    return ht_str_get(&mime_table, ext);
}

static const char *get_content_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext == nullptr)
        return "application/octet-stream";

    const char *type = mime_table_lookup(ext);
    return type ? type : "application/octet-stream";
}

static int send_file_response(const int sockfd, const int fd, const char *content_type)
{
    struct stat st = {0};
    if (fstat(fd, &st) < 0)
        return -1;

    char header[256]     = {0};
    const int header_len = snprintf(
        header,
        sizeof(header),
        OK "Content-Type: %s\r\nContent-Length: %ld\r\n\r\n",
        content_type,
        (long)st.size);

    if (header_len <= 0)
        return -1;

    if (send_all(sockfd, header, (size_t)header_len) <= 0)
        return -1;

    char buf[4096];

    ssize_t nread = 0;
    while ((nread = read(fd, buf, sizeof(buf))) > 0) {
        if (send_all(sockfd, buf, (size_t)nread) <= 0)
            return -1;
    }
    return 0;
}

static void handle_connection(http_conn_t *conn)
{
    if (!conn)
        return;

    char buf[2048]        = {0};
    const ssize_t req_len = read(conn->connfd, buf, sizeof(buf) - 1);
    if (req_len <= 0) {
        close(conn->connfd);
        return;
    }
    buf[req_len] = '\0';

    http_request_t request = parse_http_request(buf);
    pthread_mutex_lock(&log_mutex);
    if (log_file) {
        fprintf(log_file,
                "thread %d request from %s: %s %s %s\n",
                pthread_self(),
                conn->client_ip_buf,
                request.method,
                request.path,
                request.protocol);
    }
    pthread_mutex_unlock(&log_mutex);

    if (strcmp(request.method, "GET") != 0) {
        send(conn->connfd, METHOD_NOT_ALLOWED, sizeof(METHOD_NOT_ALLOWED), 0);
        close(conn->connfd);
        return;
    }

    if (strcmp(request.path, "/") == 0) {
        const int homefd = open("/web/index.html", O_RDONLY);
        if (homefd < 0) {
            send(conn->connfd, NOT_FOUND, sizeof(NOT_FOUND), 0);
        } else {
            send_file_response(conn->connfd, homefd, "text/html");
            close(homefd);
        }
    } else {
        char path[256] = {0};
        snprintf(path, sizeof(path), "/web%s", request.path);
        const int filefd = open(path, O_RDONLY);
        if (filefd < 0) {
            send(conn->connfd, NOT_FOUND, sizeof(NOT_FOUND), 0);
        } else {
            send_file_response(conn->connfd, filefd, get_content_type(path));
            close(filefd);
        }
    }

    close(conn->connfd);
}

static void *connection_entry(void *arg)
{
    auto conn = (http_conn_t *)arg;
    handle_connection(conn);
    free(conn);
    return nullptr;
}

[[noreturn]] int main()
{
    const int fd = open("/dev/eth0", O_RDONLY);
    if (fd < 0)
        panic("httpd: cannot open /dev/eth0\n");
    struct netinfo netinfo;
    ioctl(fd, GETNETINFO, &netinfo);
    close(fd);
    char ip_buf[16];
    inet_ntoa_r(netinfo.ip, ip_buf);
    printf("running httpd on ip %s port %d\n", ip_buf, 80);

    const int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0)
        panic("httpd: socket failed\n");

    struct sockaddr_in addr = {0};
    addr.sin_family         = AF_INET;
    addr.sin_port           = htons(80);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        panic("httpd: bind failed\n");

    if (listen(sockfd, 10) < 0)
        panic("httpd: listen failed\n");

    log_file = fopen("/var/log/httpd", "a");
    if (log_file == nullptr)
        panic("httpd: failed to open log file\n");

    while (1) {
        struct sockaddr_in client_addr = {0};
        socklen_t client_addr_len      = sizeof(client_addr);
        const int connfd               = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (connfd < 0)
            continue;

        uint32_t client_ip = ntohl(client_addr.sin_addr.s_addr);
        http_conn_t *conn = malloc(sizeof(*conn));
        if (!conn) {
            close(connfd);
            continue;
        }
        conn->connfd = connfd;
        inet_ntoa_r(ntohl(client_ip), conn->client_ip_buf);

        pthread_t thread;
        if (pthread_create(&thread, nullptr, connection_entry, conn) != 0) {
            close(connfd);
            free(conn);
            continue;
        }
        pthread_detach(thread);
    }
}
