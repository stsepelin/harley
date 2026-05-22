// See test_bridge.h. Implementation is host-only — uses BSD sockets +
// pthread directly because the SDL2 simulator runs on a real desktop OS,
// not FreeRTOS.

#include "test_bridge.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "phone_data.h"
#include "phone_protocol.h"
#include "ble_peripheral.h"

// 9000 collides with PHP-FPM on a typical dev macOS; 7700 isn't in
// /etc/services on macOS or Linux and isn't claimed by anything common.
#define BRIDGE_PORT      7700
#define READ_BUF_SIZE    1024

static void handle_client(int client_fd)
{
    uint8_t buf[READ_BUF_SIZE];
    size_t  acc = 0;
    ssize_t n;
    while ((n = read(client_fd, buf + acc, sizeof(buf) - acc)) > 0) {
        acc += (size_t)n;
        // Drain as many complete TLV frames as we have. NEED_MORE means
        // the next frame's header is short — wait for more bytes.
        size_t off = 0;
        for (;;) {
            if (acc - off < 3) break;
            size_t consumed = 0;
            phone_event_t evt;
            phone_parse_result_t pr = phone_protocol_parse(
                buf + off, acc - off, &consumed, &evt);
            if (pr == PHONE_PARSE_NEED_MORE) break;
            off += consumed;
            if (pr == PHONE_PARSE_OK) {
                phone_data_apply(&evt);
                fprintf(stderr, "[bridge] applied frame, %zu bytes\n", consumed);
            } else {
                fprintf(stderr, "[bridge] parse error pr=%d, dropped %zu bytes\n",
                        (int)pr, consumed);
            }
        }
        if (off > 0 && off < acc) memmove(buf, buf + off, acc - off);
        acc -= off;
        if (acc == sizeof(buf)) {
            fprintf(stderr, "[bridge] read buffer full with no frame end; dropping\n");
            acc = 0;
        }
    }
}

static void *bridge_thread(void *arg)
{
    (void)arg;
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("[bridge] socket"); return NULL; }
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(BRIDGE_PORT);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[bridge] bind");
        close(listen_fd);
        return NULL;
    }
    if (listen(listen_fd, 1) < 0) {
        perror("[bridge] listen");
        close(listen_fd);
        return NULL;
    }
    fprintf(stderr, "[bridge] listening on localhost:%d — drive with tools/notify.py\n",
            BRIDGE_PORT);

    for (;;) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("[bridge] accept");
            break;
        }
        fprintf(stderr, "[bridge] client connected\n");
        handle_client(client_fd);
        close(client_fd);
        fprintf(stderr, "[bridge] client disconnected\n");
    }
    close(listen_fd);
    return NULL;
}

void test_bridge_start(void)
{
    pthread_t t;
    if (pthread_create(&t, NULL, bridge_thread, NULL) != 0) {
        perror("[bridge] pthread_create");
        return;
    }
    pthread_detach(t);
}

// Strong override of the __attribute__((weak)) fallback in phone_data.c.
// Every CMD_* the sim fires when a banner button is tapped lands here
// instead of being silently dropped, so we can verify wire bytes by
// inspection during off-bike test work.
bool ble_peripheral_notify(const uint8_t *buf, uint16_t len)
{
    const char *name = "?";
    if (len >= 1) {
        switch (buf[0]) {
        case 0x10: name = "CALL_ACCEPT";     break;
        case 0x11: name = "CALL_REJECT";     break;
        case 0x12: name = "CALL_END";        break;
        case 0x20: name = "MEDIA_PREV";      break;
        case 0x21: name = "MEDIA_PLAY_PAUSE"; break;
        case 0x22: name = "MEDIA_NEXT";      break;
        case 0x30: name = "NOTIF_DISMISS";   break;
        }
    }
    fprintf(stderr, "[bridge] TX %s (%u bytes):", name, len);
    for (uint16_t i = 0; i < len; i++) fprintf(stderr, " %02X", buf[i]);
    fprintf(stderr, "\n");
    return true;
}
