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
"<button onclick='upload
