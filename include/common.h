#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

#define PORT      9000
#define MAX_LINE  256

/* ---- 日誌：執行期等級 ---- */
#define LV_ERROR 0
#define LV_INFO  1
#define LV_DEBUG 2
extern int g_log_level;                 /* 執行期用 -v 調整 */
void log_msg(int level, const char *fmt, ...);

#define LOG_ERR(...)   log_msg(LV_ERROR, __VA_ARGS__)
#define LOG_INFO(...)  log_msg(LV_INFO,  __VA_ARGS__)

/* ---- 日誌：編譯期開關 ----
   make DEBUG=1 才會編入 LOG_DEBUG，否則完全不產生程式碼 */
#ifdef DEBUG
#define LOG_DEBUG(...) log_msg(LV_DEBUG, __VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void)0)
#endif

/* ---- 網路工具 ---- */
int send_all(int fd, const char *buf, size_t len);   /* 0 成功, -1 失敗 */
int send_line(int fd, const char *s);                /* 自動補 \n */
int recv_line(int fd, char *buf, size_t maxlen);     /* 1=成功 0=對方關閉 -1=錯誤 -2=行太長 */

#endif