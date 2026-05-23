#include <arpa/inet.h>
#include <microhttpd.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LISTEN_PORT 80
#define BLOCKY_HOST "127.0.0.1"
#define BLOCKY_PORT 4000

#ifndef MHD_USE_INTERNAL_POLLING_THREAD
#define MHD_USE_INTERNAL_POLLING_THREAD MHD_USE_SELECT_INTERNALLY
#endif

static volatile sig_atomic_t running = 1;

struct blocky_response {
    int ok;
    int status;
    char body[4096];
    char error[128];
};

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }

    return 0;
}

static void blocky_request(const char *method, const char *path, struct blocky_response *res)
{
    int fd;
    char req[512];
    char raw[8192];
    size_t used = 0;
    struct sockaddr_in addr;

    memset(res, 0, sizeof(*res));
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(res->error, sizeof(res->error), "socket failed");
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(BLOCKY_PORT);
    inet_pton(AF_INET, BLOCKY_HOST, &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        snprintf(res->error, sizeof(res->error), "Blocky unavailable");
        close(fd);
        return;
    }

    snprintf(req, sizeof(req),
             "%s %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
             method, path, BLOCKY_HOST);

    if (send_all(fd, req, strlen(req)) != 0) {
        snprintf(res->error, sizeof(res->error), "request failed");
        close(fd);
        return;
    }

    while (used + 1 < sizeof(raw)) {
        ssize_t n = recv(fd, raw + used, sizeof(raw) - used - 1, 0);
        if (n <= 0)
            break;
        used += (size_t)n;
    }
    close(fd);

    raw[used] = '\0';
    if (used == 0) {
        snprintf(res->error, sizeof(res->error), "empty response");
        return;
    }

    sscanf(raw, "HTTP/%*s %d", &res->status);
    char *body = strstr(raw, "\r\n\r\n");
    body = body ? body + 4 : raw;
    snprintf(res->body, sizeof(res->body), "%s", body);
    res->ok = res->status >= 200 && res->status < 300;
}

static void html_escape(const char *in, char *out, size_t out_len)
{
    size_t j = 0;

    for (size_t i = 0; in[i] && j + 6 < out_len; i++) {
        const char *rep = NULL;
        switch (in[i]) {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '"': rep = "&quot;"; break;
        default:
            out[j++] = in[i];
            continue;
        }
        while (*rep && j + 1 < out_len)
            out[j++] = *rep++;
    }

    out[j] = '\0';
}

static enum MHD_Result queue_html(struct MHD_Connection *connection, const char *html, int status)
{
    int ret;
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(html),
        (void *)html, MHD_RESPMEM_MUST_COPY);

    if (!response)
        return MHD_NO;

    MHD_add_response_header(response, "Content-Type", "text/html; charset=utf-8");
    ret = MHD_queue_response(connection, status, response);
    MHD_destroy_response(response);
    return ret;
}

static enum MHD_Result render_page(struct MHD_Connection *connection, const char *message)
{
    struct blocky_response status;
    char escaped_body[8192];
    char escaped_msg[512];
    char page[16384];
    const char *state;

    blocky_request("GET", "/api/blocking/status", &status);
    state = status.ok ? "Blocky available" : "Blocky unavailable";
    html_escape(status.ok ? status.body : status.error, escaped_body, sizeof(escaped_body));
    html_escape(message ? message : "", escaped_msg, sizeof(escaped_msg));

    snprintf(page, sizeof(page),
        "<!doctype html>"
        "<html><head><title>blockyd</title>"
        "<style>body{font-family:sans-serif;max-width:720px;margin:40px auto;padding:0 16px}button{margin:4px;padding:10px 14px}pre{background:#eee;padding:12px;overflow:auto}</style>"
        "</head><body>"
        "<h1>blockyd</h1>"
        "<p><strong>Status:</strong> %s</p>"
        "%s%s%s"
        "<form method=post action=/enable><button>Enable blocking</button></form>"
        "<form method=post action=/disable><button>Disable blocking</button></form>"
        "<form method=post action=/refresh><button>Refresh lists</button></form>"
        "<form method=post action=/flush><button>Flush cache</button></form>"
        "<h2>Blocky response</h2><pre>%s</pre>"
        "</body></html>",
        state,
        escaped_msg[0] ? "<p>" : "", escaped_msg, escaped_msg[0] ? "</p>" : "",
        escaped_body);

    return queue_html(connection, page, MHD_HTTP_OK);
}

static enum MHD_Result handle_action(struct MHD_Connection *connection, const char *label,
                                     const char *method, const char *path)
{
    struct blocky_response res;
    char message[256];

    blocky_request(method, path, &res);
    snprintf(message, sizeof(message), "%s: %s", label, res.ok ? "ok" : (res.error[0] ? res.error : "failed"));
    return render_page(connection, message);
}

static enum MHD_Result answer(void *cls, struct MHD_Connection *connection,
                              const char *url, const char *method, const char *version,
                              const char *upload_data, size_t *upload_data_size, void **con_cls)
{
    (void)cls;
    (void)version;
    (void)upload_data;
    (void)con_cls;

    if (*upload_data_size) {
        *upload_data_size = 0;
        return MHD_YES;
    }

    if (strcmp(method, "GET") == 0 && strcmp(url, "/") == 0)
        return render_page(connection, NULL);

    if (strcmp(method, "POST") == 0) {
        if (strcmp(url, "/enable") == 0)
            return handle_action(connection, "enable", "GET", "/api/blocking/enable");
        if (strcmp(url, "/disable") == 0)
            return handle_action(connection, "disable", "GET", "/api/blocking/disable");
        if (strcmp(url, "/refresh") == 0)
            return handle_action(connection, "refresh lists", "POST", "/api/lists/refresh");
        if (strcmp(url, "/flush") == 0)
            return handle_action(connection, "flush cache", "POST", "/api/cache/flush");
    }

    return queue_html(connection, "not found", MHD_HTTP_NOT_FOUND);
}

int main(void)
{
    struct MHD_Daemon *daemon;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, LISTEN_PORT,
                              NULL, NULL, &answer, NULL, MHD_OPTION_END);
    if (!daemon) {
        fprintf(stderr, "failed to start blockyd-httpd\n");
        return 1;
    }

    while (running)
        pause();

    MHD_stop_daemon(daemon);
    return 0;
}
