/*
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * zvibe_webterm — POSIX development server for the webterm Z-machine target.
 *
 * Serves a browser-based Z-machine terminal over HTTP/WebSocket.
 * Single-session: one active game at a time.
 *
 * Usage: zvibe_webterm [-p PORT] [-w HTML] [-s SAVE] [GAME_DIR]
 *   -p PORT     HTTP/WebSocket port (default: 8080)
 *   -w HTML     path to zmachine.html (default: www/zmachine.html)
 *   -s SAVE     save-game file path (default: /tmp/zvibe.sav)
 *   GAME_DIR    directory containing .z3 files (default: ../../../../games/catalog)
 *
 * Open http://localhost:8080/ to play.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <time.h>

#include "../../webterm_session.h"

/* =========================================================================
 * SHA1 — self-contained public domain implementation
 * ========================================================================= */

static uint32_t sha1_rol(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static void sha1_block(uint32_t h[5], const uint8_t b[64]) {
    static const uint32_t K[4] = {
        0x5A827999u, 0x6ED9EBA1u, 0x8F1BBCDCu, 0xCA62C1D6u
    };
    uint32_t w[80], a, b0, c, d, e;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)b[i*4]<<24)|((uint32_t)b[i*4+1]<<16)
              |((uint32_t)b[i*4+2]<<8)|(uint32_t)b[i*4+3];
    for (i = 16; i < 80; i++)
        w[i] = sha1_rol(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);

    a=h[0]; b0=h[1]; c=h[2]; d=h[3]; e=h[4];
#define SHA1_R(f,k) do { \
    uint32_t t = sha1_rol(a,5)+(f)+e+(k)+w[i]; \
    e=d; d=c; c=sha1_rol(b0,30); b0=a; a=t; } while(0)
    for(i= 0;i<20;i++) SHA1_R((b0&c)|(~b0&d),        K[0]);
    for(i=20;i<40;i++) SHA1_R( b0^c^d,                K[1]);
    for(i=40;i<60;i++) SHA1_R((b0&c)|(b0&d)|(c&d),    K[2]);
    for(i=60;i<80;i++) SHA1_R( b0^c^d,                K[3]);
#undef SHA1_R
    h[0]+=a; h[1]+=b0; h[2]+=c; h[3]+=d; h[4]+=e;
}

/* Compute SHA1 of data[0..len-1] into out[0..19]. */
static void sha1(const uint8_t *data, size_t len, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301u,0xEFCDAB89u,0x98BADCFEu,0x10325476u,0xC3D2E1F0u};
    uint64_t total = (uint64_t)len;
    uint8_t  pad[64];

    while (len >= 64) { sha1_block(h, data); data += 64; len -= 64; }

    memcpy(pad, data, len);
    pad[len++] = 0x80;
    if (len > 56) {
        memset(pad + len, 0, 64 - len);
        sha1_block(h, pad);
        len = 0;
    }
    memset(pad + len, 0, 56 - len);
    uint64_t bits = total * 8;
    for (int i = 7; i >= 0; i--) { pad[56+i] = (uint8_t)(bits & 0xFF); bits >>= 8; }
    sha1_block(h, pad);

    for (int i = 0; i < 5; i++) {
        out[i*4]  =(h[i]>>24)&0xFF; out[i*4+1]=(h[i]>>16)&0xFF;
        out[i*4+2]=(h[i]>> 8)&0xFF; out[i*4+3]=(h[i]>> 0)&0xFF;
    }
}

/* Verify SHA1("abc") at startup to catch any SHA1 bugs early. */
static void sha1_selftest(void) {
    uint8_t d[20];
    static const uint8_t expect[20] = {
        0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,
        0xba,0x3e,0x25,0x71,0x78,0x50,0xc2,0x6c,
        0x9c,0xd0,0xd8,0x9d
    };
    sha1((const uint8_t *)"abc", 3, d);
    if (memcmp(d, expect, 20) != 0) {
        fprintf(stderr, "SHA1 self-test failed — aborting\n");
        exit(1);
    }
}

/* =========================================================================
 * Base64 encode
 * ========================================================================= */

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Encode in[0..len-1] to null-terminated base64 in out.
 * out must be at least ((len+2)/3)*4 + 1 bytes. */
static void b64_encode(const uint8_t *in, size_t len, char *out) {
    size_t i, o = 0;
    for (i = 0; i + 2 < len; i += 3) {
        uint32_t v = ((uint32_t)in[i]<<16)|((uint32_t)in[i+1]<<8)|in[i+2];
        out[o++]=B64[(v>>18)&63]; out[o++]=B64[(v>>12)&63];
        out[o++]=B64[(v>> 6)&63]; out[o++]=B64[(v>> 0)&63];
    }
    if (i < len) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i+1 < len) v |= (uint32_t)in[i+1] << 8;
        out[o++]=B64[(v>>18)&63]; out[o++]=B64[(v>>12)&63];
        out[o++]=(i+1<len) ? B64[(v>>6)&63] : '=';
        out[o++]='=';
    }
    out[o] = '\0';
}

/* =========================================================================
 * Network I/O helpers
 * ========================================================================= */

static int write_all(int fd, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n <= 0) return -1;
        p += n; len -= (size_t)n;
    }
    return 0;
}

static ssize_t read_all(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, p + got, len - got);
        if (n <= 0) return (n == 0 && got > 0) ? (ssize_t)got : -1;
        got += (size_t)n;
    }
    return (ssize_t)got;
}

/* =========================================================================
 * WebSocket
 * ========================================================================= */

#define WS_MAGIC "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* Compute the Sec-WebSocket-Accept value for a given client key.
 * out must be at least 29 bytes (28 chars + null). */
static void ws_accept_key(const char *client_key, char out[32]) {
    char combined[128];
    snprintf(combined, sizeof(combined), "%s%s", client_key, WS_MAGIC);
    uint8_t digest[20];
    sha1((const uint8_t *)combined, strlen(combined), digest);
    b64_encode(digest, 20, out);
}

/* Read HTTP request headers until blank line.
 * Returns bytes written to buf (including trailing NUL) or -1 on error. */
static ssize_t http_read_request(int fd, char *buf, size_t max) {
    size_t pos = 0;
    while (pos + 2 < max) {
        if (read_all(fd, buf + pos, 1) != 1) return -1;
        pos++;
        if (pos >= 4 && memcmp(buf + pos - 4, "\r\n\r\n", 4) == 0) {
            buf[pos] = '\0';
            return (ssize_t)pos;
        }
    }
    return -1;
}

/* Extract the Sec-WebSocket-Key header value.
 * Returns 1 on success, 0 if not found. */
static int http_ws_key(const char *req, char key[64]) {
    const char *p = strstr(req, "Sec-WebSocket-Key: ");
    if (!p) p = strstr(req, "Sec-WebSocket-key: ");
    if (!p) return 0;
    p += strlen("Sec-WebSocket-Key: ");
    int i = 0;
    while (*p && *p != '\r' && *p != '\n' && i < 63)
        key[i++] = *p++;
    key[i] = '\0';
    return i > 0;
}

/* Return 1 if the request is a WebSocket upgrade. */
static int http_is_ws_upgrade(const char *req) {
    return strstr(req, "Upgrade: websocket") != NULL
        || strstr(req, "Upgrade: WebSocket") != NULL;
}

/* Perform the WebSocket handshake.  Returns 0 on success, -1 on failure. */
static int ws_handshake(int fd, const char *req) {
    char key[64] = {0};
    if (!http_ws_key(req, key)) return -1;
    char accept[32];
    ws_accept_key(key, accept);
    char resp[256];
    int n = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept);
    return write_all(fd, resp, (size_t)n);
}

/* Send a WebSocket text frame (server→client, unmasked).
 * Returns 0 on success, -1 on error. */
static int ws_send(int fd, const char *data, size_t len) {
    uint8_t hdr[10];
    size_t  hlen = 0;
    hdr[hlen++] = 0x81;             /* FIN=1, opcode=1 (text) */
    if (len < 126) {
        hdr[hlen++] = (uint8_t)len;
    } else if (len < 65536) {
        hdr[hlen++] = 126;
        hdr[hlen++] = (uint8_t)(len >> 8);
        hdr[hlen++] = (uint8_t)(len & 0xFF);
    } else {
        hdr[hlen++] = 127;
        for (int i = 7; i >= 0; i--)
            hdr[hlen++] = (uint8_t)((len >> (i * 8)) & 0xFF);
    }
    if (write_all(fd, hdr, hlen) != 0) return -1;
    return write_all(fd, data, len);
}

/* Send a WebSocket close frame. */
static void ws_send_close(int fd) {
    uint8_t frame[2] = {0x88, 0x00};   /* FIN=1, opcode=8 (close), len=0 */
    write_all(fd, frame, 2);
}

/* Receive one WebSocket frame.
 * Unmasks the payload (client→server frames are always masked).
 * Writes payload to buf (NUL-terminated), sets *out_len to payload length.
 * Returns the opcode (1=text, 8=close, 9=ping) or -1 on error/EOF. */
static int ws_recv(int fd, char *buf, size_t buf_max, size_t *out_len) {
    uint8_t hdr[2];
    if (read_all(fd, hdr, 2) != 2) return -1;

    int opcode     = hdr[0] & 0x0F;
    int masked     = (hdr[1] >> 7) & 1;
    uint64_t plen  = hdr[1] & 0x7F;

    if (plen == 126) {
        uint8_t ext[2];
        if (read_all(fd, ext, 2) != 2) return -1;
        plen = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (plen == 127) {
        uint8_t ext[8];
        if (read_all(fd, ext, 8) != 8) return -1;
        plen = 0;
        for (int i = 0; i < 8; i++) plen = (plen << 8) | ext[i];
    }

    uint8_t mask[4] = {0};
    if (masked && read_all(fd, mask, 4) != 4) return -1;

    /* Read payload, capped to buf_max-1 */
    size_t read_len = (plen < buf_max) ? (size_t)plen : buf_max - 1;
    if (read_all(fd, buf, read_len) != (ssize_t)read_len) return -1;

    /* Drain any excess */
    if (plen > read_len) {
        uint8_t trash[256];
        uint64_t skip = plen - read_len;
        while (skip > 0) {
            size_t chunk = (skip < sizeof(trash)) ? (size_t)skip : sizeof(trash);
            read_all(fd, trash, chunk);
            skip -= chunk;
        }
    }

    if (masked) {
        for (size_t i = 0; i < read_len; i++)
            buf[i] ^= mask[i % 4];
    }
    buf[read_len] = '\0';
    *out_len = read_len;
    return opcode;
}

/* =========================================================================
 * HTTP file serving
 * ========================================================================= */

static void http_serve_html(int fd, const char *html, size_t html_len) {
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n", html_len);
    write_all(fd, hdr, (size_t)n);
    write_all(fd, html, html_len);
}

static void http_serve_404(int fd) __attribute__((unused));
static void http_serve_404(int fd) {
    static const char r[] =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 9\r\nConnection: close\r\n\r\nNot Found";
    write_all(fd, r, sizeof(r) - 1);
}

/* =========================================================================
 * Platform adapter (implements webterm_platform_t for POSIX)
 * ========================================================================= */

typedef struct {
    webterm_platform_t base;    /* must be first */
    int   ws_fd;                /* active WebSocket fd; -1 if no client */
    char  save_path[256];
} posix_platform_t;

static void posix_send(webterm_platform_t *p, const char *json, size_t len) {
    posix_platform_t *pp = (posix_platform_t *)p;
    if (pp->ws_fd >= 0) ws_send(pp->ws_fd, json, len);
}

static int posix_save(webterm_platform_t *p, const void *data, size_t len) {
    posix_platform_t *pp = (posix_platform_t *)p;
    FILE *f = fopen(pp->save_path, "wb");
    if (!f) return 0;
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return written == len ? 1 : 0;
}

static size_t posix_restore(webterm_platform_t *p, void *buf, size_t max) {
    posix_platform_t *pp = (posix_platform_t *)p;
    FILE *f = fopen(pp->save_path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, max, f);
    fclose(f);
    return n;
}

static posix_platform_t g_platform = {
    .base    = { posix_send, posix_save, posix_restore },
    .ws_fd   = -1,
    .save_path = "/tmp/zvibe.sav"
};

/* =========================================================================
 * Game file management
 * ========================================================================= */

/* Max Z-machine v3 story size (128 KB).  Game data must outlive the session,
 * so we keep a single static buffer for the currently loaded game. */
#define MAX_Z3_SIZE (128 * 1024)
static uint8_t g_game_buf[MAX_Z3_SIZE];
static size_t  g_game_size;

/* Build a {"type":"games","list":[...]} JSON frame from .z3 files in dir.
 * Returns the number of games found. */
static int build_games_json(const char *dir, char *out, size_t max) {
    DIR *d = opendir(dir);
    size_t pos = 0;
    int count = 0;
    pos += (size_t)snprintf(out + pos, max - pos, "{\"type\":\"games\",\"list\":[");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            size_t nlen = strlen(e->d_name);
            if (nlen > 3 && strcmp(e->d_name + nlen - 3, ".z3") == 0) {
                pos += (size_t)snprintf(out + pos, max - pos,
                    "%s\"%s\"", count ? "," : "", e->d_name);
                count++;
            }
        }
        closedir(d);
    }
    snprintf(out + pos, max - pos, "]}");
    return count;
}

/* Load a game file from game_dir/filename into g_game_buf.
 * Returns g_game_size on success, 0 on failure. */
static size_t load_game(const char *game_dir, const char *filename) {
    /* Reject paths with directory separators */
    if (strchr(filename, '/') || strchr(filename, '\\')) return 0;

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", game_dir, filename);

    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0 || (size_t)len > MAX_Z3_SIZE) { fclose(f); return 0; }

    size_t n = fread(g_game_buf, 1, (size_t)len, f);
    fclose(f);
    if (n != (size_t)len) return 0;

    g_game_size = n;
    return n;
}

/* =========================================================================
 * JSON field extraction
 * ========================================================================= */

/* Extract the string value of a JSON field into out[0..max-1].
 * Handles basic escapes: \", \\, \n, \r, \t.
 * Returns 1 on success, 0 if field not found. */
static int json_str(const char *json, const char *key, char *out, size_t max) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < max) {
        if (*p == '\\' && *(p+1)) {
            p++;
            switch (*p) {
                case '"':  out[i++] = '"';  break;
                case '\\': out[i++] = '\\'; break;
                case 'n':  out[i++] = '\n'; break;
                case 'r':  out[i++] = '\r'; break;
                case 't':  out[i++] = '\t'; break;
                default:   out[i++] = *p;   break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return 1;
}

/* =========================================================================
 * WebSocket session handler
 * ========================================================================= */

/* Dispatch one incoming WebSocket frame to the session or handle locally. */
static void dispatch_frame(webterm_session_t *s, const char *game_dir,
                            const char *json, size_t json_len) {
    (void)json_len;
    char type[32];
    if (!json_str(json, "type", type, sizeof(type))) return;

    if (strcmp(type, "input") == 0) {
        char text[WEBTERM_INPUT_MAX + 1];
        if (json_str(json, "text", text, sizeof(text)))
            webterm_session_on_input(s, text, strlen(text));

    } else if (strcmp(type, "load") == 0) {
        char game[256];
        if (json_str(json, "game", game, sizeof(game))) {
            if (load_game(game_dir, game) > 0) {
                webterm_session_load(s, g_game_buf, g_game_size);
            } else {
                const char *err = "{\"type\":\"error\",\"message\":\"Game not found\"}";
                ws_send(g_platform.ws_fd, err, strlen(err));
            }
        }

    } else if (strcmp(type, "reset") == 0) {
        webterm_session_on_reset(s);

    } else if (strcmp(type, "save") == 0) {
        webterm_session_on_save(s);

    } else if (strcmp(type, "restore") == 0) {
        webterm_session_on_restore(s);

    } else if (strcmp(type, "games") == 0) {
        char buf[4096];
        build_games_json(game_dir, buf, sizeof(buf));
        ws_send(g_platform.ws_fd, buf, strlen(buf));
    }
}

/* Run the session until it needs input or finishes. */
static void run_session(webterm_session_t *s) {
    webterm_result_t r;
    do { r = webterm_session_run(s); } while (r == WEBTERM_OK);
}

/* Handle one WebSocket connection for the lifetime of the connection. */
static void handle_ws(int fd, webterm_session_t *s, const char *game_dir) {
    /* Bind this fd to the platform before any session calls */
    g_platform.ws_fd = fd;

    /* Always send the game list first — even when busy, the client needs it */
    char games_json[4096];
    build_games_json(game_dir, games_json, sizeof(games_json));
    ws_send(fd, games_json, strlen(games_json));

    if (!webterm_session_accept(s)) {
        /* Busy — session_accept() already sent the busy frame */
        g_platform.ws_fd = -1;
        return;
    }

    /* Run session: emits output for resumed or newly loaded game */
    run_session(s);

    /* Main message loop */
    char frame[WEBTERM_INPUT_MAX + 256];
    size_t frame_len;

    while (1) {
        int opcode = ws_recv(fd, frame, sizeof(frame), &frame_len);

        if (opcode < 0 || opcode == 8) break;   /* EOF or CLOSE */

        if (opcode == 9) {                        /* PING → PONG */
            uint8_t pong[2] = {0x8A, 0x00};
            write_all(fd, pong, 2);
            continue;
        }

        if (opcode != 1) continue;               /* skip non-text */

        dispatch_frame(s, game_dir, frame, frame_len);
        run_session(s);
    }

    ws_send_close(fd);
    webterm_session_disconnect(s);
    g_platform.ws_fd = -1;
}

/* =========================================================================
 * HTML loading
 * ========================================================================= */

static char *g_html;
static size_t g_html_len;

static void load_html(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open HTML file: %s\n", path);
        fprintf(stderr, "Run the server from the platform/posix/ directory, "
                        "or pass -w PATH to specify the HTML file.\n");
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_html = malloc((size_t)len + 1);
    if (!g_html) { perror("malloc"); exit(1); }
    if (fread(g_html, 1, (size_t)len, f) != (size_t)len) {
        perror("fread"); exit(1);
    }
    g_html[len] = '\0';
    g_html_len  = (size_t)len;
    fclose(f);
}

/* =========================================================================
 * TCP listen socket
 * ========================================================================= */

static int create_server(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(fd, 4) < 0) { perror("listen"); exit(1); }
    return fd;
}

/* =========================================================================
 * Main
 * ========================================================================= */

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [-p PORT] [-w HTML] [-s SAVE] [GAME_DIR]\n"
        "  -p PORT     port (default 8080)\n"
        "  -w HTML     path to zmachine.html (default www/zmachine.html)\n"
        "  -s SAVE     save file path (default /tmp/zvibe.sav)\n"
        "  GAME_DIR    .z3 game files (default ../../../../games/catalog)\n",
        prog);
}

int main(int argc, char *argv[]) {
    int         port     = 8080;
    const char *html_path = "www/zvibe.html";
    const char *game_dir  = "../../../../games/catalog";

    signal(SIGPIPE, SIG_IGN);   /* ignore broken pipe on ws_send */

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            html_path = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            snprintf(g_platform.save_path, sizeof(g_platform.save_path),
                     "%s", argv[++i]);
        } else if (argv[i][0] != '-') {
            game_dir = argv[i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    sha1_selftest();
    load_html(html_path);

    webterm_session_t *session = webterm_session_create(&g_platform.base);
    if (!session) { fprintf(stderr, "Failed to create session\n"); return 1; }

    /* Seed the RNG from the clock for non-deterministic gameplay. */
    webterm_session_set_random_seed(session, (int)time(NULL));

    int server_fd = create_server(port);
    char games_check[4096];
    int ngames = build_games_json(game_dir, games_check, sizeof(games_check));
    printf("zvibe_webterm listening on http://localhost:%d/\n", port);
    printf("Games: %s (%d found)\n", game_dir, ngames);
    if (ngames == 0)
        fprintf(stderr, "Warning: no .z3 games found in '%s'\n", game_dir);
    fflush(stdout);

    char req[8192];

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int client = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept"); break;
        }

        if (http_read_request(client, req, sizeof(req)) < 0) {
            close(client); continue;
        }

        if (http_is_ws_upgrade(req)) {
            if (ws_handshake(client, req) == 0) {
                handle_ws(client, session, game_dir);
            }
        } else {
            /* Serve HTML for any non-WS GET */
            http_serve_html(client, g_html, g_html_len);
        }

        close(client);
    }

    webterm_session_destroy(session);
    free(g_html);
    close(server_fd);
    return 0;
}
