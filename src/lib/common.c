#include "common.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>

int g_log_level = LV_INFO;

void log_msg(int level, const char *fmt, ...)
{
    if (level > g_log_level) return;        /* 執行期過濾 */

    static const char *names[] = {"ERROR", "INFO", "DEBUG"};
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char ts[16];
    strftime(ts, sizeof ts, "%H:%M:%S", &tm);

    fprintf(stderr, "[%s][%-5s][pid %d] ", ts, names[level], getpid());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ROBUST 機制 1：MSG_NOSIGNAL
   對方已斷線時再 send，預設會收到 SIGPIPE 直接殺死行程。
   加上 MSG_NOSIGNAL 後只會回傳錯誤，程式可自行處理。 */
#ifdef NO_ROBUST
#define SEND_FLAGS 0
#else
#define SEND_FLAGS MSG_NOSIGNAL
#endif

/* send() 不保證一次送完，所以要用迴圈送到全部完成 */
int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, SEND_FLAGS);
        if (n < 0) {
            if (errno == EINTR) continue;   /* 被訊號打斷，重試 */
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int send_line(int fd, const char *s)
{
    char buf[MAX_LINE * 2];
    int n = snprintf(buf, sizeof buf, "%s\n", s);
    if (n < 0 || (size_t)n >= sizeof buf) return -1;
    return send_all(fd, buf, (size_t)n);    /* 一次送出，封包較乾淨 */
}

/* 一次讀一個字元直到 \n（簡單但夠用） */
int recv_line(int fd, char *buf, size_t maxlen)
{
    size_t i = 0;
    while (i < maxlen - 1) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) return 0;               /* 對方關閉連線 */
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (c == '\n') { buf[i] = '\0'; return 1; }
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = '\0';
    return -2;                              /* 超過長度上限，避免緩衝區溢位 */
}