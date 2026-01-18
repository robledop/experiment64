#include <net/socket.h>
#include <task/process.h>
#include <mem/heap.h>
#include <lib/string.h>
#include <arpa/inet.h>

static spinlock_t socket_lock;
static bool socket_lock_ready = false;
static list_item_t socket_list = LIST_HEAD_INIT(socket_list);
static uint16_t socket_next_ephemeral_port = 49152;

static void socket_lock_init_once(void)
{
    if (socket_lock_ready)
        return;
    spinlock_init(&socket_lock);
    socket_lock_ready = true;
}

static bool socket_addr_is_any(const uint8_t ip[static 4])
{
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

static bool socket_port_conflict_locked(uint16_t port, const uint8_t ip[static 4], int protocol, const socket_t* skip)
{
    socket_t* s;
    list_foreach_entry(s, &socket_list, list)
    {
        if (!s || s == skip)
            continue;
        if (s->state == SOCKET_STATE_UNBOUND || s->state == SOCKET_STATE_CLOSED)
            continue;
        if (s->protocol != protocol)
            continue;
        if (s->local.port != port)
            continue;
        if (socket_addr_is_any(s->local.ip) || socket_addr_is_any(ip) ||
            memcmp(s->local.ip, ip, sizeof(s->local.ip)) == 0)
            return true;
    }
    return false;
}

static void socket_rx_purge(socket_t* sock)
{
    if (!sock) return;

    spinlock_acquire(&sock->rx_lock);
    while (!list_empty(&sock->rx_queue))
    {
        socket_rx_packet_t* pkt = list_entry(sock->rx_queue.next, socket_rx_packet_t, list);
        list_del(&pkt->list);
        if (sock->rx_queue_len > 0)
            sock->rx_queue_len--;
        spinlock_release(&sock->rx_lock);

        if (pkt->data)
            kfree(pkt->data);
        kfree(pkt);

        spinlock_acquire(&sock->rx_lock);
    }
    spinlock_release(&sock->rx_lock);
}

void socket_register(socket_t* sock)
{
    if (!sock) return;

    socket_lock_init_once();
    list_init_head(&sock->list);
    list_init_head(&sock->rx_queue);
    spinlock_init(&sock->rx_lock);
    sock->rx_queue_len = 0;

    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(socket_lock, rflags);
    list_add_tail(&sock->list, &socket_list);
    SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
}

void socket_unregister(socket_t* sock)
{
    if (!sock) return;

    socket_lock_init_once();
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(socket_lock, rflags);
    if (sock->list.next && sock->list.prev)
        list_del(&sock->list);
    sock->state = SOCKET_STATE_CLOSED;
    SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);

    socket_rx_purge(sock);
    thread_wakeup(sock);
}

/**
 * Assigns a port to the socket, checking for conflicts.
 */
int socket_assign_port(socket_t* sock, const uint8_t ip[static 4], uint16_t requested_port, uint16_t* out_port)
{
    if (!sock || !out_port) return -1;

    if (sock->protocol != IPPROTO_UDP)
    {
        *out_port = requested_port;
        return 0;
    }

    socket_lock_init_once();
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(socket_lock, rflags);

    if (requested_port != 0)
    {
        if (socket_port_conflict_locked(requested_port, ip, sock->protocol, sock))
        {
            SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
            return -1;
        }
        *out_port = requested_port;
        SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
        return 0;
    }

    // Find the next available port in the dynamic/private range (49152-65535)
    const uint16_t start = socket_next_ephemeral_port;
    uint16_t candidate = start;
    bool found = false;
    do
    {
        uint16_t candidate_net = htons(candidate);
        if (!socket_port_conflict_locked(candidate_net, ip, sock->protocol, sock))
        {
            *out_port = candidate_net;
            found = true;
            socket_next_ephemeral_port = (candidate == 65535) ? 49152 : (uint16_t)(candidate + 1);
            break;
        }
        candidate = (candidate == 65535) ? 49152 : (uint16_t)(candidate + 1);
    } while (candidate != start);

    SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
    return found ? 0 : -1;
}

static int socket_enqueue_rx(socket_t* sock, const uint8_t* payload, size_t payload_len, const socket_addr_t* from)
{
    if (!sock || !from) return -1;

    socket_rx_packet_t* pkt = kmalloc(sizeof(socket_rx_packet_t));
    if (!pkt) return -1;
    memset(pkt, 0, sizeof(*pkt));
    pkt->from = *from;
    pkt->len = payload_len;
    if (payload_len > 0)
    {
        pkt->data = kmalloc(payload_len);
        if (!pkt->data)
        {
            kfree(pkt);
            return -1;
        }
        memcpy(pkt->data, payload, payload_len);
    }

    spinlock_acquire(&sock->rx_lock);
    if (sock->rx_queue_len >= SOCKET_RX_MAX_PACKETS)
    {
        spinlock_release(&sock->rx_lock);
        if (pkt->data)
            kfree(pkt->data);
        kfree(pkt);
        return -1;
    }
    list_add_tail(&pkt->list, &sock->rx_queue);
    sock->rx_queue_len++;
    spinlock_release(&sock->rx_lock);

    thread_wakeup(sock);
    return 0;
}

socket_rx_packet_t* socket_rx_pop(socket_t* sock, bool block)
{
    if (!sock) return nullptr;

    spinlock_acquire(&sock->rx_lock);
    while (list_empty(&sock->rx_queue))
    {
        if (!block)
        {
            spinlock_release(&sock->rx_lock);
            return nullptr;
        }
        thread_sleep(sock, &sock->rx_lock);
    }

    socket_rx_packet_t* pkt = list_entry(sock->rx_queue.next, socket_rx_packet_t, list);
    list_del(&pkt->list);
    if (sock->rx_queue_len > 0)
        sock->rx_queue_len--;
    spinlock_release(&sock->rx_lock);
    return pkt;
}

socket_t* socket_find_tcp_listener(const uint8_t dest_ip[static 4], uint16_t dest_port)
{
    socket_lock_init_once();
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(socket_lock, rflags);
    socket_t* s;
    list_foreach_entry(s, &socket_list, list)
    {
        if (!s)
            continue;
        if (s->protocol != IPPROTO_TCP || s->type != SOCK_STREAM)
            continue;
        if (s->state != SOCKET_STATE_LISTENING)
            continue;
        if (s->local.port != dest_port)
            continue;
        if (!socket_addr_is_any(s->local.ip) &&
            memcmp(s->local.ip, dest_ip, sizeof(s->local.ip)) != 0)
            continue;

        SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
        return s;
    }
    SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
    return nullptr;
}

socket_t* socket_find_tcp_connected(const uint8_t dest_ip[static 4], uint16_t dest_port,
                                    const uint8_t src_ip[static 4], uint16_t src_port)
{
    socket_lock_init_once();
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(socket_lock, rflags);
    socket_t* s;
    list_foreach_entry(s, &socket_list, list)
    {
        if (!s)
            continue;
        if (s->protocol != IPPROTO_TCP || s->type != SOCK_STREAM)
            continue;
        if (s->state != SOCKET_STATE_CONNECTED)
            continue;
        if (s->local.port != dest_port)
            continue;
        if (!socket_addr_is_any(s->local.ip) &&
            memcmp(s->local.ip, dest_ip, sizeof(s->local.ip)) != 0)
            continue;
        if (!socket_addr_is_any(s->remote.ip) &&
            memcmp(s->remote.ip, src_ip, sizeof(s->remote.ip)) != 0)
            continue;
        if (s->remote.port != 0 && s->remote.port != src_port)
            continue;

        SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
        return s;
    }
    SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
    return nullptr;
}

int socket_deliver_tcp(const uint8_t dest_ip[static 4], uint16_t dest_port,
                       const uint8_t src_ip[static 4], uint16_t src_port, const uint8_t* payload, size_t payload_len)
{
    socket_lock_init_once();
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(socket_lock, rflags);
    socket_t* s;
    list_foreach_entry(s, &socket_list, list)
    {
        if (!s)
            continue;
        if (s->protocol != IPPROTO_TCP || s->type != SOCK_STREAM)
            continue;
        if (s->state != SOCKET_STATE_CONNECTED)
            continue;
        if (s->local.port != dest_port)
            continue;
        if (!socket_addr_is_any(s->local.ip) &&
            memcmp(s->local.ip, dest_ip, sizeof(s->local.ip)) != 0)
            continue;
        if (s->state == SOCKET_STATE_CONNECTED)
        {
            if (!socket_addr_is_any(s->remote.ip) &&
                memcmp(s->remote.ip, src_ip, sizeof(s->remote.ip)) != 0)
                continue;
            if (s->remote.port != 0 && s->remote.port != src_port)
                continue;
        }

        socket_addr_t from = {};
        memcpy(from.ip, src_ip, sizeof(from.ip));
        from.port = src_port;
        int res = socket_enqueue_rx(s, payload, payload_len, &from);
        SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
        return res;
    }
    SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
    return -1;
}

int socket_deliver_udp(const uint8_t dest_ip[static 4], uint16_t dest_port,
                       const uint8_t src_ip[static 4], uint16_t src_port,
                       const uint8_t* payload, size_t payload_len)
{
    socket_lock_init_once();
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(socket_lock, rflags);
    socket_t* s;
    list_foreach_entry(s, &socket_list, list)
    {
        if (!s)
            continue;
        if (s->protocol != IPPROTO_UDP || s->type != SOCK_DGRAM)
            continue;
        if (s->state == SOCKET_STATE_UNBOUND || s->state == SOCKET_STATE_CLOSED)
            continue;
        if (s->local.port != dest_port)
            continue;
        if (!socket_addr_is_any(s->local.ip) &&
            memcmp(s->local.ip, dest_ip, sizeof(s->local.ip)) != 0)
            continue;

        socket_addr_t from = {};
        memcpy(from.ip, src_ip, sizeof(from.ip));
        from.port = src_port;
        int res = socket_enqueue_rx(s, payload, payload_len, &from);
        SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
        return res;
    }
    SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
    return -1;
}

int socket_deliver_icmp(const uint8_t dest_ip[static 4], const uint8_t src_ip[static 4],
                        const uint8_t* payload, size_t payload_len)
{
    socket_lock_init_once();
    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(socket_lock, rflags);
    socket_t* s;
    list_foreach_entry(s, &socket_list, list)
    {
        if (!s)
            continue;
        if (s->protocol != IPPROTO_ICMP || s->type != SOCK_RAW)
            continue;
        if (s->state == SOCKET_STATE_CLOSED)
            continue;
        if (!socket_addr_is_any(s->local.ip) &&
            memcmp(s->local.ip, dest_ip, sizeof(s->local.ip)) != 0)
            continue;

        socket_addr_t from = {};
        memcpy(from.ip, src_ip, sizeof(from.ip));
        from.port = 0;
        int res = socket_enqueue_rx(s, payload, payload_len, &from);
        SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
        return res;
    }
    SPIN_UNLOCK_INT_RESTORE(socket_lock, rflags);
    return -1;
}
