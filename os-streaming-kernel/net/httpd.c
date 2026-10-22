/* HTTP/1.1 server using lwIP's raw API: GET/HEAD + Range (lets browsers seek video) */
#include "kernel.h"
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "lwip/tcp.h"

#define CHUNK 8192
#define MAX_CONN 24                   /* cap on concurrent connections: prevents heap exhaustion from many clients */
static volatile int nconn;
static u8 iobuf[CHUNK];               /* safe to share: every callback runs on the single net thread */

struct hs {
    struct tcp_pcb *pcb;
    char req[4096];                   /* 4 KB so Cloudflare Tunnel (Cf-*) + browser headers never get cut off */
    unsigned req_len;
    int started;
    int keep_alive;                   /* HTTP/1.1 persistent connection: serve multiple requests per pcb */
    const struct tar_file *file;
    char *mem;                        /* in-memory body (status page, 404, etc.) */
    u64 pos, remaining;
    int polls;
};

static const char *ext_of(const char *name);
static const char *mime(const char *name) {
    const char *ext = ext_of(name);
    if (!ext) return "application/octet-stream";
    if (!strcmp(ext, ".html") || !strcmp(ext, ".htm")) return "text/html; charset=utf-8";
    if (!strcmp(ext, ".css"))  return "text/css";
    if (!strcmp(ext, ".js"))   return "text/javascript";
    if (!strcmp(ext, ".json")) return "application/json; charset=utf-8";
    if (!strcmp(ext, ".txt") || !strcmp(ext, ".md")) return "text/plain; charset=utf-8";
    if (!strcmp(ext, ".mp4") || !strcmp(ext, ".m4v")) return "video/mp4";
    if (!strcmp(ext, ".webm")) return "video/webm";
    if (!strcmp(ext, ".mkv"))  return "video/x-matroska";
    if (!strcmp(ext, ".m3u8")) return "application/vnd.apple.mpegurl";
    if (!strcmp(ext, ".ts"))   return "video/mp2t";
    if (!strcmp(ext, ".m4s"))  return "video/iso.segment";
    if (!strcmp(ext, ".mp3"))  return "audio/mpeg";
    if (!strcmp(ext, ".aac") || !strcmp(ext, ".m4a")) return "audio/mp4";
    if (!strcmp(ext, ".jpg") || !strcmp(ext, ".jpeg")) return "image/jpeg";
    if (!strcmp(ext, ".png"))  return "image/png";
    if (!strcmp(ext, ".svg"))  return "image/svg+xml";
    if (!strcmp(ext, ".ico"))  return "image/x-icon";
    return "application/octet-stream";
}

/* ---------- helpers for building HTML/JSON pages in memory --------- */
struct sbuf { char *p; size_t cap, len; };

static void sb_put(struct sbuf *b, char c) {
    if (b->len + 1 < b->cap) { b->p[b->len++] = c; b->p[b->len] = 0; }
}
static void sb_add(struct sbuf *b, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    size_t room = b->cap - b->len;
    int n = kvsnprintf(b->p + b->len, room, fmt, ap);
    va_end(ap);
    if (n > 0) b->len += ((size_t)n < room) ? (size_t)n : room - 1;
}
static void sb_esc(struct sbuf *b, const char *s) {         /* escape HTML special characters */
    for (; *s; s++) {
        switch (*s) {
        case '&': sb_add(b, "&amp;"); break;
        case '<': sb_add(b, "&lt;");  break;
        case '>': sb_add(b, "&gt;");  break;
        case '"': sb_add(b, "&quot;"); break;
        default:  sb_put(b, *s);
        }
    }
}
static void sb_json_esc(struct sbuf *b, const char *s) {    /* escape JSON string characters */
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') { sb_put(b, '\\'); sb_put(b, *s); }
        else if ((unsigned char)*s >= 0x20) sb_put(b, *s);
    }
}
static void sb_enc(struct sbuf *b, const char *s) {         /* percent-encode a filename (handles Thai text) */
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalpha(c) || isdigit(c) || c == '-' || c == '_' || c == '.' || c == '~') sb_put(b, (char)c);
        else { sb_put(b, '%'); sb_put(b, "0123456789ABCDEF"[c >> 4]); sb_put(b, "0123456789ABCDEF"[c & 15]); }
    }
}
static int hexv(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static void url_decode(char *s) {
    char *o = s;
    while (*s) {
        if (*s == '%' && hexv(s[1]) >= 0 && hexv(s[2]) >= 0) { *o++ = (char)(hexv(s[1]) * 16 + hexv(s[2])); s += 3; }
        else *o++ = *s++;
    }
    *o = 0;
}
static const char *ext_of(const char *name) {
    const char *ext = 0, *p = name;
    while ((p = strchr(p, '.')) != 0) { ext = p; p++; }
    return ext;
}
static int is_audio(const char *name) {
    const char *e = ext_of(name);
    return e && (!strcmp(e, ".mp3") || !strcmp(e, ".m4a") || !strcmp(e, ".aac"));
}
static int is_playable(const char *name) {            /* files a browser (including Safari on iPhone) can play directly */
    const char *e = ext_of(name);
    return e && (!strcmp(e, ".mp4") || !strcmp(e, ".m4v") || !strcmp(e, ".webm") || is_audio(name));
}

static void hs_free(struct hs *hs) {
    if (hs->mem) kfree(hs->mem);
    kfree(hs);
    nconn--;
}

static void hs_close(struct hs *hs, struct tcp_pcb *pcb) {
    tcp_arg(pcb, 0); tcp_recv(pcb, 0); tcp_sent(pcb, 0); tcp_poll(pcb, 0, 0); tcp_err(pcb, 0);
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
    hs_free(hs);
}

/* called once a response has been fully sent: close the connection, or reset state and wait for the next request */
static void request_done(struct hs *hs, struct tcp_pcb *pcb) {
    if (!hs->keep_alive) { hs_close(hs, pcb); return; }
    if (hs->mem) { kfree(hs->mem); hs->mem = 0; }
    hs->file = 0; hs->started = 0; hs->req_len = 0; hs->polls = 0;
}

/* send as much as the TCP send window currently allows */
static void send_more(struct hs *hs, struct tcp_pcb *pcb) {
    while (hs->remaining > 0) {
        u16_t space = tcp_sndbuf(pcb);
        if (space < 256) break;
        size_t chunk = hs->remaining < CHUNK ? (size_t)hs->remaining : CHUNK;
        if (chunk > space) chunk = space;
        if (hs->file) {
            int r = tarfs_read(hs->file, hs->pos, iobuf, chunk);
            if (r <= 0) { hs_close(hs, pcb); return; }
            chunk = (size_t)r;
        } else {
            memcpy(iobuf, hs->mem + hs->pos, chunk);
        }
        u8_t flags = TCP_WRITE_FLAG_COPY | (hs->remaining > chunk ? TCP_WRITE_FLAG_MORE : 0);
        err_t e = tcp_write(pcb, iobuf, (u16_t)chunk, flags);
        if (e == ERR_MEM) break;                    /* send queue full: wait for the sent callback */
        if (e != ERR_OK) { hs_close(hs, pcb); return; }
        hs->pos += chunk; hs->remaining -= chunk;
    }
    tcp_output(pcb);
    if (hs->remaining == 0) request_done(hs, pcb);
}

static void respond(struct hs *hs, struct tcp_pcb *pcb, int code, const char *ctype,
                    u64 total, u64 start, u64 len, int partial, int head_only) {
    int keep_alive = hs->keep_alive;
    static const char *msg_ok = "OK", *msg_pc = "Partial Content", *msg_nf = "Not Found", *msg_rng = "Range Not Satisfiable";
    const char *msg = code == 200 ? msg_ok : code == 206 ? msg_pc : code == 404 ? msg_nf : msg_rng;
    char h[384];
    int n = ksnprintf(h, sizeof(h), "HTTP/1.1 %d %s\r\nServer: OS-Streaming-Kernel\r\nContent-Type: %s\r\n"
                      "Accept-Ranges: bytes\r\nConnection: %s\r\nContent-Length: %lu\r\n",
                      code, msg, ctype, keep_alive ? "keep-alive" : "close", (unsigned long)len);
    if (partial)
        n += ksnprintf(h + n, sizeof(h) - n, "Content-Range: bytes %lu-%lu/%lu\r\n",
                       (unsigned long)start, (unsigned long)(start + len - 1), (unsigned long)total);
    if (code == 416)
        n += ksnprintf(h + n, sizeof(h) - n, "Content-Range: bytes */%lu\r\n", (unsigned long)total);
    n += ksnprintf(h + n, sizeof(h) - n, "\r\n");
    tcp_write(pcb, h, (u16_t)n, TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
    hs->pos = start;
    hs->remaining = head_only ? 0 : len;
    send_more(hs, pcb);
}

static void make_status(struct hs *hs) {
    hs->mem = kmalloc(1024);
    if (!hs->mem) return;
    char ip[32]; net_get_ip(ip, sizeof(ip));
    int n = ksnprintf(hs->mem, 1024,
        "OS Streaming Kernel (AArch64)\n"
        "uptime      : %lu ms\n"
        "ip          : %s\n"
        "pages free  : %lu / %lu (4 KB each)\n"
        "kernel heap : %lu bytes in use\n"
        "files on disk : %d\n"
        "scheduler   : preemptive round-robin, 10 ms slice (%lu ticks)\n"
        "disk sectors : %lu\n",
        (unsigned long)uptime_ms(), ip, (unsigned long)pmm_free_pages(), (unsigned long)pmm_total_pages(),
        (unsigned long)kheap_used(), tarfs_count(), (unsigned long)timer_ticks(), (unsigned long)blk_sectors());
    hs->remaining = (u64)n;
}

static void make_videos_json(struct hs *hs) {
    hs->mem = kmalloc(8192);
    if (!hs->mem) return;
    struct sbuf b = { hs->mem, 8192, 0 };
    hs->mem[0] = 0;
    sb_add(&b, "{\"kernel\":\"OS-Streaming-Kernel\",\"uptime_ms\":%lu,\"videos\":[", (unsigned long)uptime_ms());
    int n = tarfs_count(), first = 1;
    for (int i = 0; i < n; i++) {
        const struct tar_file *f = tarfs_get(i);
        if (!f || !is_playable(f->name)) continue;
        if (!first) sb_put(&b, ',');
        first = 0;
        sb_add(&b, "{\"id\":%d,\"filename\":\"", i);
        sb_json_esc(&b, f->name);
        sb_add(&b, "\",\"url\":\"/");
        sb_enc(&b, f->name);
        sb_add(&b, "\",\"size\":%lu,\"mime\":\"%s\"}", (unsigned long)f->size, mime(f->name));
    }
    sb_add(&b, "]}\n");
    hs->remaining = b.len;
}

static const char PAGE_HEAD[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>%s</title><style>"
    "body{font-family:system-ui,sans-serif;max-width:760px;margin:1.5rem auto;padding:0 1rem;background:#111;color:#eee}"
    "a{color:#7cc4ff;text-decoration:none}li{margin:.7rem 0;line-height:1.4}"
    ".m{color:#999;font-size:.85rem}video,audio{width:100%%;background:#000;border-radius:8px}"
    "</style></head><body>";

static void make_library(struct hs *hs) {
    hs->mem = kmalloc(16384);
    if (!hs->mem) return;
    struct sbuf b = { hs->mem, 16384, 0 };
    hs->mem[0] = 0;
    sb_add(&b, PAGE_HEAD, "Library");
    sb_add(&b, "<h1>&#127910; Library</h1><ul>");
    int n = tarfs_count();
    for (int i = 0; i < n; i++) {
        const struct tar_file *f = tarfs_get(i);
        if (!strcmp(f->name, "index.html")) continue;
        if (is_playable(f->name)) { sb_add(&b, "<li><a href=\"/watch?f="); sb_enc(&b, f->name); sb_add(&b, "\">"); }
        else { sb_add(&b, "<li><a href=\"/"); sb_enc(&b, f->name); sb_add(&b, "\">"); }
        sb_esc(&b, f->name);
        sb_add(&b, "</a> <span class=\"m\">%lu.%lu MB</span></li>",
               (unsigned long)(f->size >> 20), (unsigned long)(((f->size & 0xFFFFF) * 10) >> 20));
    }
    sb_add(&b, "</ul><p class=\"m\"><a href=\"/status\">status</a> &middot; served by a custom kernel</p></body></html>");
    hs->remaining = b.len;
}

static void make_watch(struct hs *hs, const char *name) {
    hs->mem = kmalloc(2048);
    if (!hs->mem) return;
    struct sbuf b = { hs->mem, 2048, 0 };
    hs->mem[0] = 0;
    sb_add(&b, PAGE_HEAD, "Watch");
    sb_add(&b, "<p><a href=\"/library\">&larr; Library</a></p><h2>");
    sb_esc(&b, name);
    /* playsinline: keeps iPhone playing inline instead of jumping to fullscreen */
    sb_add(&b, is_audio(name) ? "</h2><audio controls autoplay src=\"/" : "</h2><video controls autoplay playsinline src=\"/");
    sb_enc(&b, name);
    sb_add(&b, is_audio(name) ? "\"></audio>" : "\"></video>");
    sb_add(&b, "</body></html>");
    hs->remaining = b.len;
}

static void handle_request(struct hs *hs, struct tcp_pcb *pcb) {
    char *line_end = strstr(hs->req, "\r\n");
    if (!line_end) { hs_close(hs, pcb); return; }
    *line_end = 0;
    int head_only = !strncmp(hs->req, "HEAD ", 5);
    if (strncmp(hs->req, "GET ", 4) && !head_only) { hs_close(hs, pcb); return; }
    /* HTTP/1.1 defaults to keep-alive, HTTP/1.0 defaults to close, unless the header says otherwise.
       Must check this from the request line here, before the path-trimming below overwrites the
       space in front of "HTTP/1.1" with \0 */
    hs->keep_alive = strstr(hs->req, "HTTP/1.1") != 0;

    char *path = hs->req + (head_only ? 5 : 4);
    char *sp = strchr(path, ' ');
    if (sp) *sp = 0;

    for (char *p = line_end + 2; p < hs->req + hs->req_len; ) {
        if (!strncasecmp(p, "connection:", 11)) {
            char *v = p + 11; while (*v == ' ') v++;
            if (!strncasecmp(v, "close", 5)) hs->keep_alive = 0;
            else if (!strncasecmp(v, "keep-alive", 10)) hs->keep_alive = 1;
            break;
        }
        char *nl = strstr(p, "\r\n");
        if (!nl) break;
        p = nl + 2;
    }
    char *query = 0, *q = strchr(path, '?');
    if (q) { *q = 0; query = q + 1; }
    url_decode(path);                                 /* %E0%B8... and %20 -> the real filename */
    if (!strcmp(path, "/")) path = tarfs_find("index.html") ? "/index.html" : "/library";

    u64 rs = 0, re = 0; int has_range = 0, open_end = 0;
    char *hdrs = line_end + 2;
    for (char *p = hdrs; *p; p++) {
        if (strncasecmp(p, "range: bytes=", 13) == 0) {
            p += 13;
            has_range = 1;
            if (*p == '-') {                           /* suffix: bytes=-N */
                u64 nlast = 0; p++; int nd = 0;
                while (isdigit(*p) && nd++ < 18) nlast = nlast * 10 + (*p++ - '0');
                rs = nlast; open_end = 2;               /* bytes=-N: last N bytes (resolved once we know the file size) */
            } else {
                int nd = 0; while (isdigit(*p) && nd++ < 18) rs = rs * 10 + (*p++ - '0');
                if (*p == '-') p++;
                if (isdigit(*p)) { nd = 0; while (isdigit(*p) && nd++ < 18) re = re * 10 + (*p++ - '0'); }
                else open_end = 1;
            }
            break;
        }
        char *nl = strstr(p, "\r\n");
        if (!nl) break;
        p = nl + 1;
    }

    if (!strcmp(path, "/status")) {
        make_status(hs);
        if (!hs->mem) { hs_close(hs, pcb); return; }
        respond(hs, pcb, 200, "text/plain; charset=utf-8", hs->remaining, 0, hs->remaining, 0, head_only);
        return;
    }
    if (!strcmp(path, "/api/videos")) {
        make_videos_json(hs);
        if (!hs->mem) { hs_close(hs, pcb); return; }
        respond(hs, pcb, 200, "application/json; charset=utf-8", hs->remaining, 0, hs->remaining, 0, head_only);
        return;
    }
    if (!strcmp(path, "/api/rescan")) {
        int nf = tarfs_rescan();
        hs->mem = kmalloc(128);
        if (!hs->mem) { hs_close(hs, pcb); return; }
        int n = ksnprintf(hs->mem, 128, "{\"status\":\"ok\",\"files\":%d}\n", nf < 0 ? 0 : nf);
        hs->remaining = (u64)n;
        respond(hs, pcb, 200, "application/json; charset=utf-8", hs->remaining, 0, hs->remaining, 0, head_only);
        return;
    }
    if (!strcmp(path, "/library")) {
        make_library(hs);
        if (!hs->mem) { hs_close(hs, pcb); return; }
        respond(hs, pcb, 200, "text/html; charset=utf-8", hs->remaining, 0, hs->remaining, 0, head_only);
        return;
    }
    if (!strcmp(path, "/watch")) {
        char name[100] = "";
        for (char *p = query; p && *p; ) {                   /* look for f=<filename> in the query string */
            if (p[0] == 'f' && p[1] == '=') {
                size_t i = 0; p += 2;
                while (*p && *p != '&' && i < sizeof(name) - 1) name[i++] = *p++;
                name[i] = 0; url_decode(name); break;
            }
            while (*p && *p != '&') p++;
            if (*p == '&') p++;
        }
        if (name[0] && tarfs_find(name)) {
            make_watch(hs, name);
            if (!hs->mem) { hs_close(hs, pcb); return; }
            respond(hs, pcb, 200, "text/html; charset=utf-8", hs->remaining, 0, hs->remaining, 0, head_only);
            return;
        }
    }
    const struct tar_file *f = tarfs_find(path);
    if (!f) {
        static const char body[] = "404 Not Found\n";
        hs->mem = kmalloc(sizeof(body));
        if (hs->mem) memcpy(hs->mem, body, sizeof(body));
        respond(hs, pcb, 404, "text/plain", sizeof(body) - 1, 0, sizeof(body) - 1, 0, head_only);
        return;
    }
    hs->file = f;
    if (!has_range) { respond(hs, pcb, 200, mime(f->name), f->size, 0, f->size, 0, head_only); return; }
    if (open_end == 2) { u64 nlast = rs; rs = nlast > f->size ? 0 : f->size - nlast; re = f->size - 1; }
    else if (open_end == 1 || re >= f->size) re = f->size - 1;
    if (rs >= f->size || rs > re) { respond(hs, pcb, 416, "text/plain", f->size, 0, 0, 1, head_only); return; }
    respond(hs, pcb, 206, mime(f->name), f->size, rs, re - rs + 1, 1, head_only);
}

static err_t cb_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    struct hs *hs = arg;
    if (!p) { if (hs) hs_close(hs, pcb); return ERR_OK; }           /* the other side closed the connection */
    if (err != ERR_OK || !hs) { pbuf_free(p); return ERR_OK; }
    hs->polls = 0;                                                   /* data arrived = still active, not idle */
    tcp_recved(pcb, p->tot_len);
    if (!hs->started) {
        unsigned room = sizeof(hs->req) - 1 - hs->req_len;
        unsigned n = p->tot_len < room ? p->tot_len : room;
        pbuf_copy_partial(p, hs->req + hs->req_len, (u16_t)n, 0);
        hs->req_len += n; hs->req[hs->req_len] = 0;
        if (strstr(hs->req, "\r\n\r\n")) { hs->started = 1; pbuf_free(p); handle_request(hs, pcb); return ERR_OK; }
        if (hs->req_len >= sizeof(hs->req) - 1) { pbuf_free(p); hs_close(hs, pcb); return ERR_OK; }
    }
    pbuf_free(p);
    return ERR_OK;
}

static err_t cb_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)len;
    struct hs *hs = arg;
    if (!hs) return ERR_OK;
    hs->polls = 0;                                                   /* data actually sent = still active */
    if (hs->started) send_more(hs, pcb);
    return ERR_OK;
}

static err_t cb_poll(void *arg, struct tcp_pcb *pcb) {
    struct hs *hs = arg;
    if (!hs) return ERR_OK;
    if (++hs->polls > 30) { hs_close(hs, pcb); return ERR_ABRT; }       /* ~60s with no progress */
    if (hs->started) send_more(hs, pcb);
    return ERR_OK;
}

static void cb_err(void *arg, err_t err) { (void)err; if (arg) hs_free(arg); }

static err_t cb_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK) return err;
    if (nconn >= MAX_CONN) { tcp_abort(pcb); return ERR_ABRT; }   /* full: reject new connections instead of exhausting memory */
    struct hs *hs = kmalloc(sizeof(*hs));
    if (!hs) return ERR_MEM;
    nconn++;
    memset(hs, 0, sizeof(*hs));
    hs->pcb = pcb;
    tcp_setprio(pcb, TCP_PRIO_MIN);
    tcp_arg(pcb, hs);
    tcp_recv(pcb, cb_recv);
    tcp_sent(pcb, cb_sent);
    tcp_err(pcb, cb_err);
    tcp_poll(pcb, cb_poll, 4);
    return ERR_OK;
}

void httpd_init(void) {
    struct tcp_pcb *pcb = tcp_new();
    tcp_bind(pcb, IP_ANY_TYPE, 80);
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, cb_accept);
}
