#include <dns.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <stdio.h>

static int encode_question(
    const char domain_name[256],
    const uint16_t qtype,
    const uint16_t qclass,
    char encoded_question[static 256]
    )
{
    if (!encoded_question)
        return -1;

    char *domain_name_copy = strdup(domain_name);

    const char *token = strtok(domain_name_copy, ".");

    int i = 0;
    while (token != nullptr) {
        const int len       = (int)strlen(token);
        encoded_question[i] = (char)len;
        i                   += 1;
        strcpy(encoded_question + i, token);
        i     += len;
        token = strtok(nullptr, ".");
    }

    encoded_question[i++] = '\0';

    encoded_question[i++] = (char)(qtype >> 8);
    encoded_question[i++] = (char)(qtype & 0xff);
    encoded_question[i++] = (char)(qclass >> 8);
    encoded_question[i++] = (char)(qclass & 0xff);

    free(domain_name_copy);

    return i;
}

static int decode_domain_name_internal(const char *base, size_t base_len,
                                       size_t offset, char out[static 256],
                                       int *out_len, int depth)
{
    size_t i = 0;
    while (offset + i < base_len && base[offset + i] != 0) {
        const uint8_t len = (uint8_t)base[offset + i];
        if ((len & 0xC0) == 0xC0) {
            if (depth >= 8)
                return (int)(i + 2);
            if (offset + i + 1 >= base_len)
                return (int)(i + 1);
            const uint16_t ptr = (uint16_t)((len & 0x3F) << 8) |
                (uint8_t)base[offset + i + 1];
            if (ptr >= base_len)
                return (int)(i + 2);
            decode_domain_name_internal(base, base_len, ptr, out, out_len, depth + 1);
            return (int)(i + 2);
        }

        i += 1;
        if (*out_len > 0 && *out_len < 255)
            out[(*out_len)++] = '.';
        for (int j = 0; j < (int)len && offset + i + (size_t)j < base_len && *out_len < 255; j++)
            out[(*out_len)++] = base[offset + i + (size_t)j];
        i += len;
    }

    if (offset + i >= base_len)
        return (int)i;
    return (int)(i + 1);
}

static struct q_name *decode_domain_name(const char *base, size_t base_len,
                                         const char *buffer)
{
    struct q_name *q_name = calloc(sizeof(struct q_name), 1);

    if (!base || !buffer || buffer < base)
        return q_name;
    const size_t offset = (size_t)(buffer - base);
    if (offset >= base_len)
        return q_name;

    int out_len        = 0;
    const int consumed = decode_domain_name_internal(base,
                                                     base_len,
                                                     offset,
                                                     q_name->name,
                                                     &out_len,
                                                     0);
    q_name->name[out_len] = '\0';
    q_name->qname_length  = consumed;
    q_name->offset        = 0;

    return q_name;
}

struct dns_record *parse_answers(char *buffer, size_t buffer_len,
                                 int questions_length, int ancount,
                                 int *answers_length)
{
    struct dns_record *answers = calloc(sizeof(struct dns_record), ancount);

    for (int i = 0; i < ancount; i++) {
        const size_t record_offset = (size_t)HEADER_SIZE +
            (size_t)questions_length + (size_t)*answers_length;
        if (record_offset >= buffer_len)
            break;
        struct q_name *q_name = decode_domain_name(buffer,
                                                   buffer_len,
                                                   buffer + record_offset);
        if (q_name->qname_length <= 0) {
            free(q_name);
            break;
        }
        memcpy(answers[i].name, q_name->name, 256);

        const size_t rr_offset = record_offset + (size_t)q_name->qname_length;
        if (rr_offset + 10 > buffer_len) {
            free(q_name);
            break;
        }

        answers[i].type     = *(uint16_t *)(buffer + rr_offset);
        answers[i].class    = *(uint16_t *)(buffer + rr_offset + 2);
        answers[i].ttl      = *(uint32_t *)(buffer + rr_offset + 4);
        answers[i].rdlength = *(uint16_t *)(buffer + rr_offset + 8);

        const uint16_t rdlength   = ntohs(answers[i].rdlength);
        const size_t rdata_offset = rr_offset + 10;
        if (rdata_offset + rdlength > buffer_len) {
            free(q_name);
            break;
        }

        if (rdlength >= 4 && rdata_offset + 4 <= buffer_len &&
            ntohs(answers[i].type) == DNS_TYPE_A) {
            answers[i].rdata[0] = buffer[rdata_offset + 0];
            answers[i].rdata[1] = buffer[rdata_offset + 1];
            answers[i].rdata[2] = buffer[rdata_offset + 2];
            answers[i].rdata[3] = buffer[rdata_offset + 3];
        }

        *answers_length += (int)(q_name->qname_length + 10 + rdlength);
        free(q_name);
    }

    return answers;
}

struct dns_question *parse_questions(char *buffer, size_t buffer_len,
                                     int qdcount, int *questions_length)
{
    struct dns_question *questions = calloc(sizeof(struct dns_question), qdcount);

    for (int i = 0; i < qdcount; i++) {
        const size_t record_offset = (size_t)HEADER_SIZE + (size_t)*questions_length;
        if (record_offset >= buffer_len)
            break;
        struct q_name *q_name = decode_domain_name(buffer,
                                                   buffer_len,
                                                   buffer + record_offset);
        if (q_name->qname_length <= 0) {
            free(q_name);
            break;
        }
        memcpy(questions[i].qname, q_name->name, 256);

        if (record_offset + (size_t)q_name->qname_length + 4 > buffer_len) {
            free(q_name);
            break;
        }

        questions[i].qtype  = (*(uint16_t *)(buffer + record_offset + q_name->qname_length));
        questions[i].qclass = (*(uint16_t *)(buffer + record_offset + q_name->qname_length + 2));

        *questions_length += q_name->qname_length + 4;
        free(q_name);
    }

    return questions;
}

int parse_message(char *buffer, size_t buffer_len, struct dns_message *message_out)
{
    struct dns_header header = {0};
    if (buffer_len < sizeof(struct dns_header))
        return -1;
    memcpy(&header, buffer, sizeof(struct dns_header));

    message_out->header        = header;
    int reply_questions_length = 0;
    message_out->questions     = parse_questions(buffer,
                                             buffer_len,
                                             ntohs(message_out->header.qdcount),
                                             &reply_questions_length);

    int reply_answers_length = 0;
    message_out->answers     = parse_answers(buffer,
                                         buffer_len,
                                         reply_questions_length,
                                         ntohs(message_out->header.ancount),
                                         &reply_answers_length);

#ifdef DEBUG
    for (int i = 0; i < ntohs(message_out->header.qdcount); i++) {
        printf(KBWHT "Question %d: " KRESET, i);
        printf("qname=%s ", message_out->questions[i].qname);
        printf("qtype=%d ", ntohs(message_out->questions[i].qtype));
        printf("qclass=%d\n", ntohs(message_out->questions[i].qclass));
    }

    for (int i = 0; i < ntohs(header.ancount); i++) {
        printf(KBWHT "Answer %d: " KRESET, i);
        printf("name=%s ", message_out->answers[i].name);
        printf("type=");
        switch (ntohs(message_out->answers[i].type)) {
        case DNS_TYPE_A:
            printf("A");
            break;
        case DNS_TYPE_NS:
            printf("NS");
            break;
        case DNS_TYPE_CNAME:
            printf("CNAME");
            break;
        case DNS_TYPE_AAAA:
            printf("AAAA");
            break;
        case DNS_TYPE_SRV:
            printf("SRV");
            break;
        case DNS_TYPE_TXT:
            printf("TXT");
            break;
        default:
            printf("<unknown>");
            break;
        }
        printf(" ");
        printf("class=%d ", ntohs(message_out->answers[i].class));
        printf("ttl=%d ", ntohl(message_out->answers[i].ttl));
        printf("rdlength=%d ", ntohs(message_out->answers[i].rdlength));
        if (ntohs(message_out->answers[i].type) == DNS_TYPE_A &&
            ntohs(message_out->answers[i].rdlength) == 4) {
            printf("rdata=%d.%d.%d.%d\n",
                   message_out->answers[i].rdata[0],
                   message_out->answers[i].rdata[1],
                   message_out->answers[i].rdata[2],
                   message_out->answers[i].rdata[3]);
        } else {
            printf("rdata=<non-A>\n");
        }
    }
    printf(KYEL "==================================\n" KRESET);
#endif

    return HEADER_SIZE + reply_questions_length + reply_answers_length;
}

int pack_message(const struct dns_message *message, int question_count,
                 char (*output)[512])
{
    memcpy(*output, &(message->header), sizeof(struct dns_header));

    int questions_length = 0;

    // Copy questions
    if (message->questions != NULL) {
        for (int i = 0; i < question_count; i++) {
            char *encoded_domain_name = calloc(256, 1);

            const int size = encode_question(
                message->questions[i].qname,
                htons(message->questions[i].qtype),
                htons(message->questions[i].qclass),
                encoded_domain_name);

            memcpy(
                &(*output)[HEADER_SIZE + questions_length],
                encoded_domain_name,
                size
                );

            free(encoded_domain_name);

            questions_length += size;
        }
    }

    return HEADER_SIZE + questions_length;
}

static int send_with_retry(const int sockfd, const struct sockaddr_in *dest,
                           const void *packet, const size_t packet_len)
{
    for (int attempt = 0; attempt < 10; attempt++) {
        const ssize_t sent = sendto(sockfd,
                                    packet,
                                    packet_len,
                                    0,
                                    (const struct sockaddr *)dest,
                                    sizeof(*dest));
        if (sent >= 0)
            return 0;
        usleep(10000);
    }
    return -1;
}

uint32_t dns_lookup(const char *name, struct sockaddr_in *address)
{
    struct netinfo netinfo;
    const int fd = open("/dev/eth0", O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    ioctl(fd, GETNETINFO, &netinfo);
    close(fd);


    const uint32_t dns_server = netinfo.dns_server;
    uint8_t dns_ip_bytes[4];
    ip_to_bytes(dns_server, dns_ip_bytes);
    struct sockaddr_in dest = {0};
    dest.sin_family         = AF_INET;
    dest.sin_port           = htons(53);
    memcpy(&dest.sin_addr, dns_ip_bytes, sizeof(dest.sin_addr));

    const int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0)
        panic("socket failed\n");

    struct sockaddr_in own_addr = {0};
    own_addr.sin_family         = AF_INET;
    if (bind(sockfd, (struct sockaddr *)&own_addr, sizeof(own_addr)) < 0)
        panic("bind failed\n");

    uint8_t packet[512];
    struct dns_message *message = calloc(sizeof(struct dns_message), 1);
    if (!message) {
        close(sockfd);
        return -1;
    }
    message->header.id           = htons(1);
    message->header.flags        = htons(DNS_FLAG_RD);
    constexpr int question_count = 1;
    message->header.qdcount      = htons(question_count);
    message->questions           = calloc(sizeof(struct dns_question), question_count);
    if (!message->questions) {
        free(message->questions);
        free(message);
        close(sockfd);
        return -1;
    }
    message->questions[0].qtype  = htons(1);
    message->questions[0].qclass = htons(1);
    strcpy(message->questions[0].qname, name);

    const int packet_size = pack_message(message, question_count, (char (*)[512])packet);
    send_with_retry(sockfd, &dest, packet, packet_size);

    struct dns_message message_out = {0};
    constexpr uint64_t timeout     = 2000;
    const uint64_t start           = now_ms();
    const uint64_t deadline        = start + timeout;

    while (now_ms() < deadline) {
        uint8_t recvbuf[512] = {0};
        socklen_t srclen     = sizeof(dest);
        const ssize_t n      = recvfrom(sockfd,
                                   recvbuf,
                                   sizeof(recvbuf),
                                   MSG_DONTWAIT,
                                   (struct sockaddr *)&dest,
                                   &srclen);
        if (n > 0) {
            if ((size_t)n < sizeof(struct dns_header))
                continue;
#ifdef DEBUG
            const struct dns_header *rh = (struct dns_header *)recvbuf;
            const uint16_t flags        = ntohs(rh->flags);
            printf(KYEL "==== DNS resolution debug log ====\n" KBWHT);
            printf(KBWHT "id: " KRESET "%d\n", ntohs(rh->id));
            printf(KBWHT "is reply: " KRESET "%s\n", flags & DNS_FLAG_QR ? "yes" : "no");
            printf(KBWHT "opcode: " KRESET "%d\n", flags & DNS_FLAG_OPCODE);
            printf(KBWHT "authoritative answer: " KRESET "%s\n", flags & DNS_FLAG_AA ? "yes" : "no");
            printf(KBWHT "response code: " KRESET);
            switch (flags & DNS_FLAG_RCODE) {
            case DNS_RCODE_NO_ERROR:
                printf("NO_ERROR\n");
                break;
            case DNS_RCODE_FORMAT_ERROR:
                printf("FORMAT_ERROR\n");
                break;
            case DNS_RCODE_SERVER_FAILURE:
                printf("SERVER_FAILURE\n");
                break;
            case DNS_RCODE_NAME_ERROR:
                printf("NAME_ERROR\n");
                break;
            case DNS_RCODE_NOT_IMPLEMENTED:
                printf("NOT_IMPLEMENTED\n");
                break;
            case DNS_RCODE_REFUSED:
                printf("REFUSED\n");
                break;
            case DNS_RCODE_YXDOMAIN:
                printf("YXDOMAIN\n");
                break;
            case DNS_RCODE_YXRRSET:
                printf("YXRRSET\n");
                break;
            case DNS_RCODE_NXRRSET:
                printf("NXRRSET\n");
                break;
            case DNS_RCODE_NOTAUTH:
                printf("NOTAUTH\n");
                break;
            case DNS_RCODE_NOTZONE:
                printf("NOTZONE\n");
                break;
            default:
                printf("UNKNOWN\n");
                break;
            }
            printf(KBWHT "qdcount: " KRESET "%d\n", ntohs(rh->qdcount));
            printf(KBWHT "ancount: " KRESET "%d\n", ntohs(rh->ancount));
#endif

            if (parse_message((char *)recvbuf, (size_t)n, &message_out) < 0)
                break;

            // TODO: Add support for CNAME
            for (int i = 0; i < ntohs(message_out.header.ancount); i++) {
                if (ntohs(message_out.answers[i].type) == DNS_TYPE_A) {
                    memcpy(&address->sin_addr, &message_out.answers[i].rdata, sizeof(address->sin_addr));
                    free(message_out.answers);
                    free(message_out.questions);
                    free(message->questions);
                    free(message);
                    close(sockfd);
                    return 0;
                }
            }

            break;
        }

        usleep(10);
    }

    free(message_out.answers);
    free(message_out.questions);
    free(message->questions);
    free(message);
    close(sockfd);
    return 0;
}
