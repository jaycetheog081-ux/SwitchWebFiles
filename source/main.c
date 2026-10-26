#include <switch.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define PORT 8080
#define MAX_HEADER 8192
#define MAX_UPLOAD (16 * 1024 * 1024)
#define ROOT "sdmc:/"

static const char PAGE[] =
"<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>SwitchWebFiles</title><style>"
"body{background:#10131c;color:#eee;font:16px system-ui;margin:20px}"
"button,input{padding:10px;margin:5px;border-radius:8px}"
"a{color:#8ee6b0}li{margin:12px 0}code{color:#b7b9ff}"
"</style></head><body><h1>SwitchWebFiles</h1>"
"<p>SD card browser · Local network only</p>"
"<p id='path'>/</p><input type='file' id='file'>"
"<button onclick='upload()'>Upload</button><ul id='items'></ul><p id='status'></p>"
"<script>"
"let current='';"
"async function refresh(){"
"let r=await fetch('/api/list?path='+encodeURIComponent(current));"
"if(!r.ok){status.textContent='Could not list directory';return}"
"let d=await r.json();path.textContent='/'+current;"
"items.replaceChildren();"
"if(current){let li=document.createElement('li'),a=document.createElement('a');"
"a.href='#';a.textContent='.. (parent)';a.onclick=()=>{current=current.split('/').slice(0,-1).join('/');refresh()};"
"li.append(a);items.append(li)}"
"for(let f of d){let li=document.createElement('li'),a=document.createElement('a');"
"a.textContent=(f.dir?'📁 ':'📄 ')+f.name;"
"if(f.dir){a.href='#';a.onclick=()=>{current=current?current+'/'+f.name:f.name;refresh()}}"
"else{a.href='/api/download?path='+encodeURIComponent(current?current+'/'+f.name:f.name)}"
"li.append(a);items.append(li)}"
"}"
"async function upload(){let f=file.files[0];if(!f)return;"
"let name=f.name;if(name.includes('/')||name.includes('\\\\')||name==='.'||name==='..'){status.textContent='Invalid filename';return}"
"let dest=(current?current+'/':'')+name;"
"status.textContent='Uploading '+name+'...';"
"let r=await fetch('/api/upload?path='+encodeURIComponent(dest),{method:'POST',body:f});"
"status.textContent=r.ok?'Upload complete':('Upload failed: '+await r.text());if(r.ok)refresh()}"
"refresh();</script></body></html>";

static int send_all(int fd, const void *data, size_t len) {
    const char *p = data;
    while (len) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static void respond(int fd, int code, const char *type,
                    const void *body, size_t len) {
    char h[512];
    const char *reason = code == 200 ? "OK" :
                         code == 400 ? "Bad Request" :
                         code == 404 ? "Not Found" :
                         code == 413 ? "Payload Too Large" :
                         code == 500 ? "Internal Server Error" :
                         "Error";
    int n = snprintf(h, sizeof(h),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Cache-Control: no-store\r\n\r\n",
        code, reason, type, len);
    if (n > 0 && (size_t)n < sizeof(h)) {
        send_all(fd, h, (size_t)n);
        if (body && len) send_all(fd, body, len);
    }
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (*r == '%' && r[1] && r[2]) {
            int a = hexval(r[1]), b = hexval(r[2]);
            if (a >= 0 && b >= 0) {
                *w++ = (char)((a << 4) | b);
                r += 3;
                continue;
            }
        }
        if (*r == '+') *w++ = ' ';
        else *w++ = *r;
        r++;
    }
    *w = '\0';
}

/* Only allow relative paths beneath sdmc:/. */
static bool safe_path(const char *p) {
    if (!p || p[0] == '/' || strchr(p, '\\')) return false;
    if (strchr(p, ':')) return false;
    if (strstr(p, "..")) return false;
    for (const unsigned char *q = (const unsigned char *)p; *q; q++) {
        if (*q < 32) return false;
    }
    return true;
}

static bool make_path(char *out, size_t cap, const char *relative) {
    if (!safe_path(relative)) return false;
    int n = snprintf(out, cap, "%s%s", ROOT, relative);
    return n >= 0 && (size_t)n < cap;
}

static void handle_list(int fd, char *path) {
    url_decode(path);
    char full[1024];
    if (!make_path(full, sizeof(full), path)) {
        const char *e = "Invalid path";
        respond(fd, 400, "text/plain", e, strlen(e));
        return;
    }

    DIR *d = opendir(full);
    if (!d) {
        const char *e = "Directory unavailable";
        respond(fd, 404, "text/plain", e, strlen(e));
        return;
    }

    char *json = calloc(1, 65536);
    if (!json) {
        closedir(d);
        respond(fd, 500, "text/plain", "Out of memory", 13);
        return;
    }

    size_t used = 0;
    bool first = true;
    struct dirent *ent;

    used += snprintf(json + used, 65536 - used, "[");
    while ((ent = readdir(d)) != NULL && used < 64000) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;
        if (strchr(ent->d_name, '"') || strchr(ent->d_name, '\\'))
            continue;

        char child[1200];
        struct stat st;
        int n = snprintf(child, sizeof(child), "%s/%s", full, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(child)) continue;
        if (stat(child, &st) != 0) continue;

        n = snprintf(json + used, 65536 - used,
            "%s{\"name\":\"%s\",\"dir\":%s}",
            first ? "" : ",", ent->d_name,
            S_ISDIR(st.st_mode) ? "true" : "false");
        if (n < 0 || (size_t)n >= 65536 - used) break;
        used += (size_t)n;
        first = false;
    }
    used += snprintf(json + used, 65536 - used, "]");
    closedir(d);
    respond(fd, 200, "application/json", json, used);
    free(json);
}

static void handle_download(int fd, char *path) {
    url_decode(path);
    char full[1024];
    if (!make_path(full, sizeof(full), path)) {
        respond(fd, 400, "text/plain", "Invalid path", 12);
        return;
    }

    FILE *f = fopen(full, "rb");
    if (!f) {
        respond(fd, 404, "text/plain", "File unavailable", 16);
        return;
    }

    struct stat st;
    if (stat(full, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size < 0 || st.st_size > 64 * 1024 * 1024) {
        fclose(f);
        respond(fd, 400, "text/plain", "Invalid file", 12);
        return;
    }

    char h[512];
    int n = snprintf(h, sizeof(h),
        "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
        "Content-Length: %lld\r\nContent-Disposition: attachment\r\n"
        "X-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
        (long long)st.st_size);
    if (n > 0 && (size_t)n < sizeof(h)) send_all(fd, h, (size_t)n);

    char buf[8192];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (send_all(fd, buf, got) != 0) break;
    }
    fclose(f);
}

static void handle_upload(int fd, char *path,
                          const char *body, size_t body_len) {
    url_decode(path);
    char full[1024];
    if (!make_path(full, sizeof(full), path)) {
        respond(fd, 400, "text/plain", "Invalid path", 12);
        return;
    }
    if (body_len > MAX_UPLOAD) {
        respond(fd, 413, "text/plain", "Upload too large", 16);
        return;
    }

    /* Do not overwrite an existing file. */
    int out = open(full, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (out < 0) {
        respond(fd, 400, "text/plain",
                "Cannot create file (it may already exist)", 40);
        return;
    }

    size_t off = 0;
    bool ok = true;
    while (off < body_len) {
        ssize_t n = write(out, body + off, body_len - off);
        if (n <= 0) { ok = false; break; }
        off += (size_t)n;
    }
    if (close(out) != 0) ok = false;

    if (!ok) {
        unlink(full);
        respond(fd, 500, "text/plain", "Write failed", 12);
        return;
    }
    respond(fd, 200, "text/plain", "Uploaded", 8);
}

static void serve_client(int fd) {
    char *buf = calloc(1, MAX_HEADER + MAX_UPLOAD + 1);
    if (!buf) {
        respond(fd, 500, "text/plain", "Out of memory", 13);
        return;
    }

    size_t used = 0;
    char *header_end = NULL;
    while (used < MAX_HEADER) {
        ssize_t n = recv(fd, buf + used, MAX_HEADER - used, 0);
        if (n <= 0) { free(buf); return; }
        used += (size_t)n;
        buf[used] = '\0';
        header_end = strstr(buf, "\r\n\r\n");
        if (header_end) break;
    }
    if (!header_end) {
        respond(fd, 400, "text/plain", "Bad headers", 11);
        free(buf);
        return;
    }

    size_t header_len = (size_t)(header_end - buf) + 4;
    char method[8] = {0}, target[2048] = {0};
    if (sscanf(buf, "%7s %2047s", method, target) != 2) {
        respond(fd, 400, "text/plain", "Bad request", 11);
        free(buf);
        return;
    }

    size_t content_len = 0;
    char *cl = strcasestr(buf, "\r\nContent-Length:");
    if (cl && cl < header_end) {
        unsigned long long v = strtoull(cl + 17, NULL, 10);
        if (v > MAX_UPLOAD) {
            respond(fd, 413, "text/plain", "Upload too large", 16);
            free(buf);
            return;
        }
        content_len = (size_t)v;
    }

    size_t body_have = used - header_len;
    while (body_have < content_len) {
        ssize_t n = recv(fd, buf + used, MAX_HEADER + MAX_UPLOAD - used, 0);
        if (n <= 0) { free(buf); return; }
        used += (size_t)n;
        body_have += (size_t)n;
        buf[used] = '\0';
    }

    char *query = strchr(target, '?');
    if (query) *query++ = '\0';

    if (!strcmp(method, "GET") && !strcmp(target, "/")) {
        respond(fd, 200, "text/html; charset=utf-8",
                PAGE, sizeof(PAGE) - 1);
    } else if (!strcmp(method, "GET") && !strcmp(target, "/api/list")) {
        char path[1024] = {0};
        if (query && !strncmp(query, "path=", 5))
            snprintf(path, sizeof(path), "%s", query + 5);
        handle_list(fd, path);
    } else if (!strcmp(method, "GET") &&
               !strcmp(target, "/api/download")) {
        char path[1024] = {0};
        if (query && !strncmp(query, "path=", 5))
            snprintf(path, sizeof(path), "%s", query + 5);
        handle_download(fd, path);
    } else if (!strcmp(method, "POST") &&
               !strcmp(target, "/api/upload")) {
        char path[1024] = {0};
        if (query && !strncmp(query, "path=", 5))
            snprintf(path, sizeof(path), "%s", query + 5);
        if (!path[0]) {
            respond(fd, 400, "text/plain", "Missing path", 12);
        } else {
            handle_upload(fd, path, buf + header_len, content_len);
        }
    } else {
        respond(fd, 404, "text/plain", "Not found", 9);
    }

    free(buf);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    consoleInit(NULL);
    printf("SwitchWebFiles\n");
    printf("Starting local HTTP server...\n");

    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        printf("Network init failed: 0x%08X\n", rc);
        consoleUpdate(NULL);
        while (appletMainLoop()) { consoleUpdate(NULL); }
        consoleExit();
        return 1;
    }

    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        printf("Socket creation failed\n");
        socketExit();
        consoleExit();
        return 1;
    }

    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(server, 4) < 0) {
        printf("Bind/listen failed\n");
        close(server);
        socketExit();
        consoleExit();
        return 1;
    }

    printf("\nServer port: %d\n", PORT);
    printf("Open the Switch IP in iPhone Safari:\n");
    printf("http://SWITCH-IP:%d/\n", PORT);
    printf("\nKeep the Switch awake while transferring.\n");
    printf("Press + to exit.\n");
    consoleUpdate(NULL);

    while (appletMainLoop()) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client = accept(server,
            (struct sockaddr *)&client_addr, &client_len);
        if (client >= 0) {
            struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            serve_client(client);
            close(client);
        }
        hidScanInput();
        if (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_PLUS) break;
        consoleUpdate(NULL);
    }

    close(server);
    socketExit();
    consoleExit();
    return 0;
}
