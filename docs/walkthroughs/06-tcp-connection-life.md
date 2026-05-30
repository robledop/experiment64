# Life of a TCP connection: arriving SYN to recv()

This walkthrough follows one server-side TCP connection from the moment a SYN
frame lands in the e1000 receive ring until a user thread blocked in `recv()`
gets its first byte of payload. It matters because almost the entire input path
runs inside the network card's hard interrupt handler — there is no bottom half
or deferred work queue — so the rendezvous between the IRQ and the sleeping user
thread *is* the whole story. The handoff is done entirely through
`thread_sleep`/`thread_wakeup` on the socket pointer, and that contract is not
spelled out anywhere in the code, so this doc makes it explicit.

## Scope

This traces the **server side only**: a listening socket accepting an inbound
connection. There is no client path in this tree — no `sys_connect.c`, no
`tcp_connect()`, no `SYN_SENT` state. The kernel only ever *responds* to a SYN;
it never originates one. Listener setup (`socket`/`bind`/`listen`) is assumed
already done; the trace starts at the arriving SYN.

The TCP implementation is deliberately minimal: no retransmission, no real
sequence-number validation beyond an exact in-order check, a fixed receive
window, and a global incrementing ISN. Limits are listed at the end.

## Files in play

- `kernel/drivers/e1000.c` — NIC driver; the RX interrupt handler that feeds
  every frame into the stack.
- `kernel/net/network.c` — `network_receive()`: Ethernet/IPv4 demux that routes
  TCP frames to `tcp_receive()`.
- `kernel/net/tcp.c` — `tcp_receive()`: the handshake state machine. Creates the
  child socket on SYN, flips it to ESTABLISHED on the completing ACK, queues it
  on the listener, and delivers payload.
- `kernel/net/socket.c` — the global socket list and its lookups
  (`socket_find_tcp_listener`, `socket_find_tcp_connected`), the RX queue, and
  the `socket_rx_pop`/`socket_enqueue_rx` blocking primitives.
- `kernel/syscalls/sys_accept.c` — sleeps on the listener, dequeues a completed
  child, wraps it in a file descriptor.
- `kernel/syscalls/sys_recvfrom.c` — `recv()`/`recvfrom()`: pops one payload
  packet off the connected socket, blocking if empty.
- `kernel/task/thread.c` — `thread_sleep`/`thread_wakeup`: the channel-based
  block/wake primitive both rendezvous points are built on.

## The walk

### 1. The frame arrives in interrupt context

The NIC raises its IRQ and `e1000_interrupt_handler()`
(`e1000.c:291`) runs. It masks device interrupts, reads the cause register, and
on an RX event calls `e1000_receive()` (`e1000.c:419`).

`e1000_receive()` drains the RX ring in a loop: for each descriptor with the
done bit set (`E1000_RXD_STAT_DD`, `e1000.c:425`) it hands the buffer straight
to `network_receive()` (`e1000.c:460`), then clears the descriptor and advances
the tail. **There is no deferral.** Everything below this point — the demux, the
SYN-ACK transmit, the child-socket allocation, the handshake completion, and the
`thread_wakeup` — executes inside this single hard-IRQ handler with no thread of
its own. This is *why* the design must be wakeup-based: you cannot block in an
interrupt, so the only thing the IRQ can do for a waiting `accept()`/`recv()`
caller is mark its thread runnable and move on.

### 2. Ethernet/IPv4 demux

`network_receive()` (`network.c:164`) reads the EtherType, and for `ETHERTYPE_IP`
validates the IPv4 header (version, IHL, length bounds). For
`IP_PROTOCOL_TCP` it checks the destination IP is ours and calls
`tcp_receive()` (`network.c:199-201`). Note the early-length checks here and
again at the top of `tcp_receive` — untrusted input is bounds-checked before any
field is dereferenced.

### 3. tcp_receive: is this for an existing connection?

`tcp_receive()` (`tcp.c:131`) reparses headers, extracts `flags`, `seq_num`,
`ack_num`, and opportunistically caches the sender's MAC (`tcp.c:153-155`). It
then does its first lookup:

```c
socket_t* sock = socket_find_tcp_connected(dest_ip, dst_port, src_ip, src_port);
```

(`tcp.c:161`) `socket_find_tcp_connected()` (`socket.c:286`) walks the global
socket list for a `SOCKET_STATE_CONNECTED` TCP socket whose local *and* remote
4-tuple matches. For a brand-new SYN there is no such socket, so `sock` is
`nullptr` and we fall to the SYN branch.

### 4. SYN with no connection: build the child socket, send SYN-ACK

When `sock` is null and the segment is a bare SYN (`flags & TCP_FLAG_SYN` and not
`TCP_FLAG_ACK`, `tcp.c:164`), `tcp_receive` looks for a listener with
`socket_find_tcp_listener()` (`socket.c:261`): a `SOCKET_STATE_LISTENING` TCP
socket on the matching local IP/port. No listener → drop.

If found, it checks the backlog (default 1 if unset, `tcp.c:170`) under
`listener->accept_lock`, then `kzalloc`s a **child socket** (`tcp.c:181`) and
copies the listener's local address into it. Key state it sets:

- `child->state = SOCKET_STATE_CONNECTED` (`tcp.c:190`)
- `child->flags = SOCKET_FLAG_TCP_SYN_RCVD | SOCKET_FLAG_HEAP_ALLOC` (`tcp.c:195`)
- `child->tcp_recv_next = seq_num + 1` (ack the SYN, `tcp.c:196`)
- `child->tcp_send_next = isn + 1`, where `isn` comes from `tcp_generate_isn()`
  (`tcp.c:197-198`)

It registers the child on the global list (`socket_register`, `tcp.c:199`) and
sends a SYN-ACK with `tcp_send_segment()` (`tcp.c:202`) — **a packet transmitted
from inside the IRQ handler.**

Crucial subtlety: the child is now `SOCKET_STATE_CONNECTED`, but it is **not yet
acceptable**. `state == CONNECTED` only means the data-path lookups in step 3
and step 8 will match it; the handshake is still half-open. Acceptability is
tracked separately, in `flags`, and is conferred only in step 6.

### 5. The completing ACK matches the child

The client replies with the final ACK of the handshake. This time
`socket_find_tcp_connected()` (`tcp.c:161`) *does* match — it returns the child
created in step 4 (its remote 4-tuple now matches the sender). So `sock` is the
child, and we skip the SYN branch.

### 6. SYN_RCVD -> ESTABLISHED, and queue on the listener

Because `sock->flags & SOCKET_FLAG_TCP_SYN_RCVD` is set (`tcp.c:216`),
`tcp_receive` checks that this is an ACK acknowledging exactly our SYN
(`ack_num == sock->tcp_send_next`, `tcp.c:218`). If so it flips the flags:

```c
sock->flags &= ~SOCKET_FLAG_TCP_SYN_RCVD;
sock->flags |=  SOCKET_FLAG_TCP_ESTABLISHED;
```

(`tcp.c:220-221`) It re-finds the listener, and under `listener->accept_lock`
appends the child to `listener->accept_queue` via its `accept_list` link and
bumps `accept_queue_len` (`tcp.c:222-231`). Then — still in IRQ context — it
calls:

```c
thread_wakeup(listener);
```

(`tcp.c:236`) **This is the accept rendezvous.** The wakeup channel is the
*listener socket pointer*. If the backlog filled between step 4 and now, the
child is unregistered and torn down instead (`tcp.c:237-242`).

### 7. sys_accept wakes, dequeues the child, wraps it in an fd

Meanwhile a user thread is parked in `sys_accept()`. After validating the fd and
confirming the socket is `SOCKET_STATE_LISTENING`, it loops:

```c
WITH_LOCK(listener->accept_lock) {
    while (list_empty(&listener->accept_queue)) {
        if (nonblock) { ... break; }
        thread_sleep(listener, &listener->accept_lock);
    }
    child = list_entry(listener->accept_queue.next, socket_t, accept_list);
    list_del(&child->accept_list);
    ...
}
```

(`sys_accept.c:77-91`) `thread_sleep(listener, ...)` (`sys_accept.c:86`) is the
other half of the rendezvous: it blocks on the *same* listener pointer
`tcp_receive` woke in step 6. `thread_sleep` (`thread.c:87`) atomically drops the
`accept_lock`, marks the thread `THREAD_BLOCKED` with `chan = listener`, and
reschedules; `thread_wakeup` (`thread.c:154`) walks every thread and flips any
`THREAD_BLOCKED` thread whose `chan` equals the listener back to `THREAD_READY`
(`thread.c:146`). The match is on the raw `void *` pointer — nothing about the
channel is socket-specific. The `while` re-checks the queue after waking, so a
spurious or racing wake just loops.

Once it has the child, `sys_accept` allocates a fresh `vfs_inode_t`, tags it
`VFS_PIPE` (`sys_accept.c:107`), points `inode->device` at the child socket and
`inode->iops` at `socket_iops`, then `fd_alloc`/`fd_assign` to get a new fd
(`sys_accept.c:100-128`). It fills in the caller's `sockaddr_in` from
`child->remote` and returns the new fd (`sys_accept.c:130-153`). The application
now has a connected socket fd.

### 8. Data arrives; tcp_receive delivers it to the RX queue

A later segment carrying payload comes in. `socket_find_tcp_connected` matches
the child (`tcp.c:161`); it is now `ESTABLISHED` (`tcp.c:252`). If
`payload_len > 0` and the sequence number is exactly what we expect
(`seq_num == sock->tcp_recv_next`, `tcp.c:258`), `tcp_receive` calls:

```c
socket_deliver_tcp(dest_ip, dst_port, src_ip, src_port, payload, payload_len);
```

(`tcp.c:260`) advances `tcp_recv_next` by the payload length, and arranges to
ACK. `socket_deliver_tcp()` (`socket.c:317`) re-walks the global socket list to
re-find *the same connected socket* (`tcp_receive` already held it — see
Gotchas), then copies the payload into a `socket_rx_packet_t` and appends it via
`socket_enqueue_rx()` (`socket.c:197`). At the end of enqueue:

```c
thread_wakeup(sock);
```

(`socket.c:234`) **This is the recv rendezvous** — identical shape to the accept
one, but the channel is now the *connected child socket pointer*.

### 9. sys_recvfrom wakes and returns the bytes

A user thread blocked in `recv()` is sitting inside `socket_rx_pop()`. From
`sys_recvfrom()` (`sys_recvfrom.c:8`): after validating the buffer and resolving
the fd to its socket, it calls `socket_rx_pop(sock, block)` (`sys_recvfrom.c:45`).
`socket_rx_pop()` (`socket.c:238`) holds `sock->rx_lock` and:

```c
while (list_empty(&sock->rx_queue)) {
    if (sock->rx_closed) return nullptr;
    if (!block)         return nullptr;
    thread_sleep(sock, &sock->rx_lock);
}
```

(`socket.c:244-251`) `thread_sleep(sock, &sock->rx_lock)` (`socket.c:250`) blocks
on the connected socket pointer — exactly the channel `socket_enqueue_rx` woke in
step 8. On wake it pops one packet off `rx_queue`. Back in `sys_recvfrom`, the
payload is `memcpy`'d into the user buffer (clamped to `len`), the source address
is written into `src_addr` if provided, the packet is freed, and the byte count
is returned (`sys_recvfrom.c:52-85`). The journey from arriving SYN to delivered
bytes is complete.

## Gotchas

- **The whole input path runs in the e1000 hard IRQ.** SYN-ACK transmit, child
  `kzalloc`, ESTABLISHED transition, and both `thread_wakeup` calls happen in
  interrupt context (`e1000.c:291` → `e1000.c:419` → `network_receive` →
  `tcp_receive`). No softirq, no tasklet, no worker thread. The wakeup model is a
  consequence of this, not a stylistic choice.

- **The wait channel is the socket pointer itself.** Both rendezvous use the same
  primitive: `thread_sleep(p, lock)` blocks with `chan = p`, `thread_wakeup(p)`
  wakes anything whose `chan == p` (`thread.c:87`, `thread.c:146`). Accept uses
  the *listener* pointer (`sys_accept.c:86` ↔ `tcp.c:236`); recv uses the
  *connected child* pointer (`socket.c:250` ↔ `socket.c:234`). Nothing in the
  code documents this contract — get the pointer wrong and the sleeper never
  wakes.

- **`state == CONNECTED` does not mean "ready to accept."** The child is
  `SOCKET_STATE_CONNECTED` from the moment the SYN is seen (`tcp.c:190`), so the
  data-path lookups match it during the half-open window. Whether it is
  *acceptable* lives in `flags` (`SOCKET_FLAG_TCP_SYN_RCVD` →
  `SOCKET_FLAG_TCP_ESTABLISHED`, `tcp.c:220-221`) and in whether it has been
  linked onto `listener->accept_queue` (`tcp.c:230`). State and flags track two
  different things.

- **Redundant second lookup on the data path.** `tcp_receive` already holds the
  matched `sock` (`tcp.c:161`), yet to deliver payload it calls
  `socket_deliver_tcp` (`socket.c:317`), which re-walks the entire global socket
  list to find the very same socket before enqueuing. The connected socket is
  effectively looked up twice per data segment.

- **The accepted socket's inode is a `VFS_PIPE`.** `sys_accept` tags the new
  inode `VFS_PIPE` (`sys_accept.c:107`); the socket-ness is carried entirely by
  `inode->iops == &socket_iops` and `inode->device`, not by the flag.

- **Out-of-order data is ACK'd but discarded.** If `seq_num != tcp_recv_next`,
  `tcp_receive` sets `should_ack` but never queues the payload and never advances
  `tcp_recv_next` (`tcp.c:266-269`). There is no reassembly buffer for TCP — the
  only reassembly in this path is for split Ethernet frames in `e1000_receive`.

## Limits and simplifications

- **No client path.** No `connect()`/`tcp_connect`/`SYN_SENT`. The kernel only
  answers SYNs.
- **ISN is a global counter starting at 1**, incremented per connection
  (`tcp_generate_isn`, `tcp.c:38-41`). Not randomized.
- **Fixed 4096-byte advertised window** on every segment (`tcp.c:97`); RX queue
  capped at `SOCKET_RX_MAX_PACKETS` = 16 (`socket.h`, `socket.c:219`).
- **No retransmission, no RTO, no congestion control.** A dropped SYN-ACK or data
  segment is simply lost.
- **Default backlog is 1** when `listen()` is given 0 (`tcp.c:170`,
  `sys_listen.c`); a full backlog silently drops new SYNs (`tcp.c:175-179`).
- **Sequence validation is exact-match only** (`seq_num == tcp_recv_next`); there
  is no window-based acceptance.

## See also

- `docs/networking.md` — the socket syscall surface (`socket`/`bind`/`listen`/
  `accept`/`sendto`/`recvfrom`) and accepted type/protocol combinations.
- `docs/scheduler.md` — thread states and how `thread_sleep`/`thread_wakeup`
  interact with the per-CPU scheduler that both rendezvous depend on.
- `docs/address_space.md` — user-pointer validation (`user_ptr_write_ok`,
  `copy_to_user`) used by `sys_accept`/`sys_recvfrom`.
- Acronyms (SYN, ACK, ISN, SYN_RCVD, ESTABLISHED, IRQ, NIC, RX/TX) are collected
  in `docs/glossary.md`. For convenience: SYN = synchronize (connection-open
  flag); ACK = acknowledgement; ISN = initial sequence number; SYN_RCVD/
  ESTABLISHED = TCP handshake states; IRQ = hardware interrupt; RX/TX =
  receive/transmit.
