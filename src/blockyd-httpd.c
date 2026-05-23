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
        fprintf(stderr, "blocky request failed: %s %s: unavailable\n", method, path);
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
    enum MHD_Result ret;
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
    char page[24576];
    const char *state;
    const char *state_class;

    blocky_request("GET", "/api/blocking/status", &status);
    state = status.ok ? "Blocky available" : "Blocky unavailable";
    state_class = status.ok ? "ok" : "bad";
    html_escape(status.ok ? status.body : status.error, escaped_body, sizeof(escaped_body));
    html_escape(message ? message : "", escaped_msg, sizeof(escaped_msg));

    snprintf(page, sizeof(page),
        "<!doctype html>"
        "<html><head><title>blockyd</title><meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<style>"
        ":root{color-scheme:dark;--bg:#0b1120;--panel:#111827;--panel2:#0f172a;--text:#e5e7eb;--muted:#94a3b8;--line:#263244;--primary:#38bdf8;--primaryText:#082f49;--warn:#fb7185;--ok:#34d399}"
        "*{box-sizing:border-box}"
        "body{margin:0;min-height:100vh;background:radial-gradient(circle at top left,#1e3a5f 0,#0b1120 34rem);color:var(--text);font:16px/1.5 system-ui,-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif}"
        ".wrap{width:min(880px,100%%);margin:0 auto;padding:32px 16px}"
        ".hero{margin-bottom:18px}"
        "h1{margin:0;font-size:32px;letter-spacing:-.04em}"
        ".subtitle{margin:4px 0 0;color:var(--muted)}"
        ".card{background:rgba(17,24,39,.88);border:1px solid var(--line);border-radius:18px;padding:20px;margin-top:16px}"
        ".status{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}"
        ".label{color:var(--muted);font-size:13px;text-transform:uppercase;letter-spacing:.08em}"
        ".pill{display:inline-flex;align-items:center;gap:8px;border:1px solid var(--line);border-radius:999px;padding:7px 11px;font-weight:700}"
        ".pill:before{content:\"\";width:9px;height:9px;border-radius:50%%;background:var(--warn);box-shadow:0 0 16px var(--warn)}"
        ".pill.ok:before{background:var(--ok);box-shadow:0 0 16px var(--ok)}"
        ".notice{margin:14px 0 0;border:1px solid #2563eb66;background:#1d4ed833;color:#bfdbfe;border-radius:12px;padding:10px 12px}"
        ".actions{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;margin-top:16px}"
        "form{margin:0}"
        "button{width:100%%;border:0;border-radius:12px;padding:11px 13px;font:inherit;font-weight:700;cursor:pointer;background:#1f2937;color:var(--text);border:1px solid #334155}"
        "button:hover{filter:brightness(1.1)}"
        ".primary button{background:var(--primary);color:var(--primaryText);border-color:var(--primary)}"
        ".warning button{background:#4c1d24;color:#fecdd3;border-color:#be123c}"
        "h2{font-size:18px;margin:0 0 10px;letter-spacing:-.02em}"
        "pre{margin:0;max-height:420px;overflow:auto;border:1px solid var(--line);border-radius:14px;background:var(--panel2);color:#cbd5e1;padding:14px;font:13px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;white-space:pre-wrap}"
        "@media(max-width:680px){.wrap{padding:22px 12px}.card{padding:16px;border-radius:15px}.actions{grid-template-columns:1fr 1fr}h1{font-size:28px}}"
        "</style>"
        "</head><body>"
        "<main class=wrap>"
        "<header class=hero><h1>blockyd</h1><p class=subtitle>Local controls for your Blocky DNS filter.</p></header>"
        "<section class=card>"
        "<div class=status><div><div class=label>Status</div><div class=subtitle>Blocky API at 127.0.0.1:4000</div></div><span class=\"pill %s\">%s</span></div>"
        "%s%s%s"
        "<div class=actions>"
        "<form class=primary method=post action=/enable><button type=submit>Enable blocking</button></form>"
        "<form class=warning method=post action=/disable><button type=submit>Disable blocking</button></form>"
        "<form method=post action=/refresh><button type=submit>Refresh lists</button></form>"
        "<form method=post action=/flush><button type=submit>Flush cache</button></form>"
        "</div>"
        "</section>"
        "<section class=card><h2>Blocky response</h2><pre>%s</pre></section>"
        "</main>"
        "</body></html>",
        state_class,
        state,
        escaped_msg[0] ? "<p class=notice>" : "", escaped_msg, escaped_msg[0] ? "</p>" : "",
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
    fprintf(stderr, "action %s -> %s\n", label, res.ok ? "ok" : (res.error[0] ? res.error : "failed"));
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

    fprintf(stderr, "request %s %s\n", method ? method : "-", url ? url : "-");

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

    setvbuf(stderr, NULL, _IONBF, 0);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fprintf(stderr, "starting blockyd-httpd on 0.0.0.0:%d, Blocky API %s:%d\n",
            LISTEN_PORT, BLOCKY_HOST, BLOCKY_PORT);

    daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, LISTEN_PORT,
                              NULL, NULL, &answer, NULL, MHD_OPTION_END);
    if (!daemon) {
        fprintf(stderr, "failed to start blockyd-httpd\n");
        return 1;
    }

    while (running)
        pause();

    fprintf(stderr, "stopping blockyd-httpd\n");
    MHD_stop_daemon(daemon);
    return 0;
}
