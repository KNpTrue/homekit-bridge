// Copyright (c) 2021 KNpTrue and homekit-bridge contributors
//
// Licensed under the MIT License.
// You may not use this file except in compliance with the License.
// See [CONTRIBUTORS.md] for the list of homekit-bridge project authors.

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pal/net/udp.h>
#include <pal/memory.h>

#include <HAPLog.h>
#include <HAPPlatform.h>
#include <HAPPlatformFileHandle.h>

#define PAL_NET_UDP_BUF_LEN 2048
#define PAL_NET_ADDR_MAX_LEN HAPMax(INET6_ADDRSTRLEN, INET_ADDRSTRLEN)

/**
 * Log with type.
 *
 * @param type [Debug|Info|Default|Error|Fault]
 * @param udp The pointer to udp object.
 */
#define UDP_LOG(type, udp, fmt, arg...) \
    HAPLogWithType(&udp_log_obj, kHAPLogType_ ## type, \
    "(id=%u) " fmt, (udp) ? (udp)->id : 0, ##arg)

#define UDP_LOG_ERRNO(udp, func) \
    UDP_LOG(Error, udp, "%s: %s() error: %s.", __func__, func, strerror(errno))

typedef struct pal_net_udp_mbuf {
    char to_addr[PAL_NET_ADDR_MAX_LEN];
    uint16_t to_port;
    struct pal_net_udp_mbuf *next;
    size_t len;
    char buf[0];
} pal_net_udp_mbuf;

struct pal_net_udp {
    bool bound:1;
    bool connected:1;
    uint16_t id;
    int fd;
    pal_net_domain domain;
    char remote_addr[PAL_NET_ADDR_MAX_LEN];
    uint16_t remote_port;
    pal_net_udp_mbuf *mbuf_list_head;

    HAPPlatformFileHandleRef handle;
    HAPPlatformFileHandleEvent interests;

    pal_net_udp_recv_cb recv_cb;
    void *recv_arg;

    pal_net_udp_err_cb err_cb;
    void *err_arg;
};

static const HAPLogObject udp_log_obj = {
    .subsystem = kHAPPlatform_LogSubsystem,
    .category = "UDP",
};

static size_t gudp_pcb_count;

static void pal_net_udp_file_handle_callback(
        HAPPlatformFileHandleRef fileHandle,
        HAPPlatformFileHandleEvent fileHandleEvents,
        void* context);

static bool
pal_net_addr_get_ipv4(struct sockaddr_in *dst_addr, const char *src_addr, uint16_t port) {
    dst_addr->sin_family = AF_INET;
    int ret = inet_pton(AF_INET, src_addr, &dst_addr->sin_addr.s_addr);
    if (ret <= 0) {
        return false;
    }
    dst_addr->sin_port = htons(port);
    return true;
}

static bool
pal_net_addr_get_ipv6(struct sockaddr_in6 *dst_addr, const char *src_addr, uint16_t port) {
    dst_addr->sin6_family = AF_INET6;
    int ret = inet_pton(AF_INET6, src_addr, &dst_addr->sin6_addr);
    if (ret <= 0) {
        return false;
    }
    dst_addr->sin6_port = htons(port);
    return true;
}

static void pal_net_udp_add_mbuf(pal_net_udp *udp, pal_net_udp_mbuf *mbuf) {
    pal_net_udp_mbuf **node = &udp->mbuf_list_head;
    while (*node) {
        node = &(*node)->next;
    }
    *node = mbuf;
}

static pal_net_udp_mbuf *pal_net_udp_get_mbuf(pal_net_udp *udp) {
    pal_net_udp_mbuf *mbuf = udp->mbuf_list_head;
    if (mbuf) {
        udp->mbuf_list_head = udp->mbuf_list_head->next;
    }
    return mbuf;
}

static void pal_net_udp_del_mbuf_list(pal_net_udp *udp) {
    pal_net_udp_mbuf *cur;
    while (udp->mbuf_list_head) {
        cur = udp->mbuf_list_head;
        udp->mbuf_list_head = udp->mbuf_list_head->next;
        pal_mem_free(cur);
    }
}

static void pal_net_udp_raw_recv(pal_net_udp *udp) {
    pal_net_err err = PAL_NET_ERR_OK;
    char buf[PAL_NET_UDP_BUF_LEN];
    char from_addr[PAL_NET_ADDR_MAX_LEN] = { 0 };
    uint16_t from_port;
    ssize_t rc;
    if (udp->connected) {
        rc = recv(udp->fd, buf, sizeof(buf), 0);
        if (rc <= 0) {
            UDP_LOG_ERRNO(udp, "recv");
            err = PAL_NET_ERR_UNKNOWN;
            goto err;
        }
        HAPRawBufferCopyBytes(from_addr, udp->remote_addr, PAL_NET_ADDR_MAX_LEN);
        from_port = udp->remote_port;
    } else {
        switch (udp->domain) {
        case PAL_NET_DOMAIN_INET: {
            struct sockaddr_in sa;
            socklen_t addr_len = sizeof(sa);
            rc = recvfrom(udp->fd, buf, sizeof(buf),
                0, (struct sockaddr *)&sa, &addr_len);
            if (rc <= 0) {
                UDP_LOG_ERRNO(udp, "recvfrom");
                err = PAL_NET_ERR_UNKNOWN;
                goto err;
            }
            from_port = ntohs(sa.sin_port);
            inet_ntop(AF_INET, &sa.sin_addr.s_addr,
                from_addr, sizeof(from_addr));
            break;
        }
        case PAL_NET_DOMAIN_INET6: {
            struct sockaddr_in6 sa;
            socklen_t addr_len = sizeof(sa);
            rc = recvfrom(udp->fd, buf, sizeof(buf),
                0, (struct sockaddr *)&sa, &addr_len);
            if (rc <= 0) {
                UDP_LOG_ERRNO(udp, "recvfrom");
                err = PAL_NET_ERR_UNKNOWN;
                goto err;
            }
            from_port = ntohs(sa.sin6_port);
            inet_ntop(AF_INET, &sa.sin6_addr,
                from_addr, sizeof(from_addr));
            break;
        }
        default:
            HAPAssertionFailure();
        }
    }
    HAPLogBufferDebug(&udp_log_obj, buf, rc, "(id=%u) Receive packet(len=%zd) from %s:%u",
        udp->id, rc, from_addr, from_port);
    if (udp->recv_cb) {
        udp->recv_cb(udp, buf, rc, from_addr, from_port, udp->recv_arg);
    }
    return;

err:
    if (udp->err_cb) {
        udp->err_cb(udp, err, udp->err_arg);
    }
}

static void pal_net_udp_raw_send(pal_net_udp *udp) {
    pal_net_err err = PAL_NET_ERR_OK;
    pal_net_udp_mbuf *mbuf = pal_net_udp_get_mbuf(udp);
    if (!mbuf) {
        err = PAL_NET_ERR_UNKNOWN;
        goto err;
    }
    if (udp->mbuf_list_head == NULL) {
        udp->interests.isReadyForWriting = false;
        HAPPlatformFileHandleUpdateInterests(udp->handle, udp->interests,
            pal_net_udp_file_handle_callback, udp);
    }

    ssize_t rc;
    if (mbuf->to_addr[0]) {
        switch (udp->domain) {
        case PAL_NET_DOMAIN_INET: {
            struct sockaddr_in sa;
            if (!pal_net_addr_get_ipv4(&sa, mbuf->to_addr, mbuf->to_port)) {
                UDP_LOG(Error, udp, "%s: Invalid address \"%s\".",
                    __func__, mbuf->to_addr);
                err = PAL_NET_ERR_UNKNOWN;
                goto err;
            }
            rc = sendto(udp->fd, mbuf->buf, mbuf->len, 0,
                (struct sockaddr *)&sa, sizeof(sa));
            break;
        }
        case PAL_NET_DOMAIN_INET6: {
            struct sockaddr_in6 sa;
            if (!pal_net_addr_get_ipv6(&sa, mbuf->to_addr, mbuf->to_port)) {
                UDP_LOG(Error, udp, "%s: Invalid address \"%s\".",
                    __func__, mbuf->to_addr);
                err = PAL_NET_ERR_UNKNOWN;
                goto err;
            }
            rc = sendto(udp->fd, mbuf->buf, mbuf->len, 0,
                (struct sockaddr *)&sa, sizeof(sa));
            break;
        }
        default:
            HAPAssertionFailure();
        }
    } else {
        rc = send(udp->fd, mbuf->buf, mbuf->len, 0);
    }
    if (rc != mbuf->len) {
        if (rc <= 0) {
            UDP_LOG_ERRNO(udp, "send");
        } else {
            UDP_LOG(Error, udp, "%s: Only sent %zd byte.", __func__, rc);
        }
        err = PAL_NET_ERR_UNKNOWN;
        goto err;
    }

    HAPLogBufferDebug(&udp_log_obj, mbuf->buf, mbuf->len,
        "(id=%u) Sent packet(len=%zd) to %s:%u", udp->id,
        mbuf->len, mbuf->to_addr, mbuf->to_port);
    pal_mem_free(mbuf);
    return;

err:
    pal_mem_free(mbuf);
    if (udp->err_cb) {
        udp->err_cb(udp, err, udp->err_arg);
    }
}

static void pal_net_udp_raw_exception(pal_net_udp *udp) {
    UDP_LOG(Error, udp, "%s", __func__);
    if (udp->err_cb) {
        udp->err_cb(udp, PAL_NET_ERR_UNKNOWN, udp->err_arg);
    }
}

static void pal_net_udp_file_handle_callback(
        HAPPlatformFileHandleRef fileHandle,
        HAPPlatformFileHandleEvent fileHandleEvents,
        void* context) {
    HAPPrecondition(context);

    pal_net_udp *udp = context;
    HAPAssert(udp->handle == fileHandle);

    if (fileHandleEvents.hasErrorConditionPending) {
        pal_net_udp_raw_exception(udp);
        return;
    }

    if (fileHandleEvents.isReadyForReading) {
        pal_net_udp_raw_recv(udp);
    }

    if (fileHandleEvents.isReadyForWriting) {
        pal_net_udp_raw_send(udp);
    }
}

pal_net_udp *pal_net_udp_new(pal_net_domain domain) {
    pal_net_udp *udp = pal_mem_calloc(sizeof(*udp));
    if (!udp) {
        UDP_LOG(Error, udp, "%s: Failed to calloc memory.", __func__);
        return NULL;
    }

    int _domain;
    switch (domain) {
    case PAL_NET_DOMAIN_INET:
        _domain = AF_INET;
        break;
    case PAL_NET_DOMAIN_INET6:
        _domain = AF_INET6;
        break;
    default:
        HAPAssertionFailure();
    }

    udp->id = ++gudp_pcb_count;

    udp->fd = socket(_domain, SOCK_DGRAM, 0);
    if (udp->fd == -1) {
        UDP_LOG_ERRNO(udp, "socket");
        pal_mem_free(udp);
        return NULL;
    }
    udp->domain = domain;
    udp->interests.isReadyForReading = true;
    udp->interests.hasErrorConditionPending = true;
    HAPError err = HAPPlatformFileHandleRegister(&udp->handle, udp->fd,
        udp->interests, pal_net_udp_file_handle_callback, udp);
    if (err != kHAPError_None) {
        UDP_LOG(Error, udp, "%s: Failed to register handle callback", __func__);
        return NULL;
    }
    UDP_LOG(Debug, udp, "%s() = %p", __func__, udp);
    return udp;
}

pal_net_err pal_net_udp_enable_broadcast(pal_net_udp *udp) {
    HAPPrecondition(udp);

    int optval = 1;
    int ret = setsockopt(udp->fd, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval));
    if (ret) {
        return PAL_NET_ERR_UNKNOWN;
    }
    return PAL_NET_ERR_OK;
}

pal_net_err pal_net_udp_bind(pal_net_udp *udp, const char *addr, uint16_t port) {
    HAPPrecondition(udp);
    HAPPrecondition(addr);

    int ret;
    switch (udp->domain) {
    case PAL_NET_DOMAIN_INET: {
        struct sockaddr_in sa;
        if (!pal_net_addr_get_ipv4(&sa, addr, port)) {
            UDP_LOG(Error, udp, "%s: Invalid address \"%s\".", __func__, addr);
            return PAL_NET_ERR_INVALID_ARG;
        }
        ret = bind(udp->fd, (struct sockaddr *)&sa, sizeof(sa));
        break;
    }
    case PAL_NET_DOMAIN_INET6: {
        struct sockaddr_in6 sa;
        if (!pal_net_addr_get_ipv6(&sa, addr, port)) {
            UDP_LOG(Error, udp, "%s: Invalid address \"%s\".", __func__, addr);
            return PAL_NET_ERR_INVALID_ARG;
        }
        ret = bind(udp->fd, (struct sockaddr *)&sa, sizeof(sa));
        break;
    }
    default:
        HAPAssertionFailure();
    }
    if (ret == -1) {
        UDP_LOG_ERRNO(udp, "bind");
        return PAL_NET_ERR_UNKNOWN;
    }
    udp->bound = true;
    UDP_LOG(Debug, udp, "Bound to %s:%u", addr, port);
    return PAL_NET_ERR_OK;
}

pal_net_err pal_net_udp_connect(pal_net_udp *udp, const char *addr, uint16_t port) {
    size_t addr_len = HAPStringGetNumBytes(addr);

    HAPPrecondition(udp);
    HAPPrecondition(addr);
    HAPPrecondition(addr_len < PAL_NET_ADDR_MAX_LEN);

    int ret;
    switch (udp->domain) {
    case PAL_NET_DOMAIN_INET: {
        struct sockaddr_in sa;
        if (!pal_net_addr_get_ipv4(&sa, addr, port)) {
            UDP_LOG(Error, udp, "%s: Invalid address \"%s\".", __func__, addr);
            return PAL_NET_ERR_INVALID_ARG;
        }
        ret = connect(udp->fd, (struct sockaddr *)&sa, sizeof(sa));
        break;
    }
    case PAL_NET_DOMAIN_INET6: {
        struct sockaddr_in6 sa;
        if (!pal_net_addr_get_ipv6(&sa, addr, port)) {
            UDP_LOG(Error, udp, "%s: Invalid address \"%s\".", __func__, addr);
            return PAL_NET_ERR_INVALID_ARG;
        }
        ret = connect(udp->fd, (struct sockaddr *)&sa, sizeof(sa));
        break;
    }
    default:
        HAPAssertionFailure();
    }
    if (ret == -1) {
        UDP_LOG_ERRNO(udp, "connect");
        return PAL_NET_ERR_UNKNOWN;
    }
    HAPRawBufferCopyBytes(udp->remote_addr, addr, addr_len);
    udp->remote_port = port;
    udp->connected = true;
    UDP_LOG(Debug, udp, "Connected to %s:%u", addr, port);
    return PAL_NET_ERR_OK;
}

pal_net_err pal_net_udp_send(pal_net_udp *udp, const void *data, size_t len) {
    HAPPrecondition(udp);
    HAPPrecondition(data);
    HAPPrecondition(len > 0);

    if (!udp->connected) {
        UDP_LOG(Error, udp, "%s: Unknown remote address and port, connect first.", __func__);
        return PAL_NET_ERR_NOT_CONN;
    }
    pal_net_udp_mbuf *mbuf = pal_mem_alloc(sizeof(*mbuf) + len);
    if (!mbuf) {
        UDP_LOG(Error, udp, "%s: Failed to alloc memory.", __func__);
        return PAL_NET_ERR_ALLOC;
    }
    HAPRawBufferCopyBytes(mbuf->buf, data, len);
    mbuf->len = len;
    mbuf->to_addr[0] = '\0';
    mbuf->to_port = 0;
    mbuf->next = NULL;
    pal_net_udp_add_mbuf(udp, mbuf);
    udp->interests.isReadyForWriting = true;
    HAPPlatformFileHandleUpdateInterests(udp->handle, udp->interests,
        pal_net_udp_file_handle_callback, udp);
    UDP_LOG(Debug, udp, "%s(len = %zu)", __func__, len);
    return PAL_NET_ERR_OK;
}

pal_net_err pal_net_udp_sendto(pal_net_udp *udp, const void *data, size_t len,
    const char *addr, uint16_t port) {
    size_t addr_len = HAPStringGetNumBytes(addr);

    HAPPrecondition(udp);
    HAPPrecondition(data);
    HAPPrecondition(len > 0);
    HAPPrecondition(addr);
    HAPPrecondition(addr_len < PAL_NET_ADDR_MAX_LEN);

    switch (udp->domain) {
    case PAL_NET_DOMAIN_INET: {
        struct sockaddr_in sa;
        if (!pal_net_addr_get_ipv4(&sa, addr, port)) {
            UDP_LOG(Error, udp, "%s: Invalid address \"%s\".", __func__, addr);
            return PAL_NET_ERR_INVALID_ARG;
        }
        break;
    }
    case PAL_NET_DOMAIN_INET6: {
        struct sockaddr_in6 sa;
        if (!pal_net_addr_get_ipv6(&sa, addr, port)) {
            UDP_LOG(Error, udp, "%s: Invalid address \"%s\".", __func__, addr);
            return PAL_NET_ERR_INVALID_ARG;
        }
        break;
    }
    default:
        HAPAssertionFailure();
    }
    pal_net_udp_mbuf *mbuf = pal_mem_alloc(sizeof(*mbuf) + len);
    if (!mbuf) {
        UDP_LOG(Error, udp, "%s: Failed to alloc memory.", __func__);
        return PAL_NET_ERR_ALLOC;
    }
    HAPRawBufferCopyBytes(mbuf->buf, data, len);
    mbuf->len = len;
    HAPRawBufferCopyBytes(mbuf->to_addr, addr, addr_len);
    mbuf->to_port = port;
    mbuf->next = NULL;
    pal_net_udp_add_mbuf(udp, mbuf);
    udp->interests.isReadyForWriting = true;
    HAPPlatformFileHandleUpdateInterests(udp->handle, udp->interests,
        pal_net_udp_file_handle_callback, udp);
    UDP_LOG(Debug, udp, "%s(len = %zu, addr = %s, port = %u)", __func__, len, addr, port);
    return PAL_NET_ERR_OK;
}

void pal_net_udp_set_recv_cb(pal_net_udp *udp, pal_net_udp_recv_cb cb, void *arg) {
    HAPPrecondition(udp);
    HAPPrecondition(cb);

    udp->recv_cb = cb;
    udp->recv_arg = arg;
}

void pal_net_udp_set_err_cb(pal_net_udp *udp, pal_net_udp_err_cb cb, void *arg) {
    HAPPrecondition(udp);
    HAPPrecondition(cb);

    udp->err_cb = cb;
    udp->err_arg = arg;
}

void pal_net_udp_free(pal_net_udp *udp) {
    if (!udp) {
        return;
    }
    UDP_LOG(Debug, udp, "%s(%p)", __func__, udp);
    HAPPlatformFileHandleDeregister(udp->handle);
    close(udp->fd);
    pal_net_udp_del_mbuf_list(udp);
    pal_mem_free(udp);
}
