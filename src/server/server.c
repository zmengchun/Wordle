#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/mman.h>

#define MAX_TRIES 6
static int idle_timeout = 30;   /* 秒，可用 -t 調整 */
static volatile int *g_active;     /* 父子行程共享：目前連線數 */

/* 組出一行系統資訊（協定是一行一訊息，所以全部放在同一行） */
static void build_sysinfo(char *out, size_t n)
{
    struct utsname u;
    struct sysinfo si;
    char host[64];

    uname(&u);                                   /* OS 名稱、核心版本 */
    sysinfo(&si);                                /* uptime、記憶體、負載 */
    gethostname(host, sizeof host);
    host[sizeof host - 1] = '\0';

    unsigned long unit  = si.mem_unit ? si.mem_unit : 1;
    unsigned long total = si.totalram * unit / (1024 * 1024);   /* MB */
    unsigned long freem = si.freeram  * unit / (1024 * 1024);

    snprintf(out, n,
             "SYSINFO os=%s-%s host=%s uptime=%lds load=%.2f mem_free=%lu/%luMB clients=%d",
             u.sysname, u.release, host, (long)si.uptime,
             si.loads[0] / 65536.0, freem, total, *g_active);
}
static const char *WORDS[] = {
    "apple","crane","slate","house","water","light","music","green","tiger","bread",
    "chair","plant","stone","cloud","dream","river","smile","night","beach","pizza",
    "robot","train","zebra","lemon","mouse","shirt","candy","ocean","happy","brain"
};
#define NWORDS (sizeof(WORDS) / sizeof(WORDS[0]))

/* Wordle 比對：G=位置對, Y=字母對但位置錯, B=沒有
   注意重複字母要處理，所以用 used[] 記錄答案中已被配對的位置 */
static void wordle_score(const char *answer, const char *guess, char *out)
{
    int used[5] = {0};
    for (int i = 0; i < 5; i++) out[i] = 'B';
    for (int i = 0; i < 5; i++)                       /* 第一輪：先找綠色 */
        if (guess[i] == answer[i]) { out[i] = 'G'; used[i] = 1; }
    for (int i = 0; i < 5; i++) {                     /* 第二輪：再找黃色 */
        if (out[i] == 'G') continue;
        for (int j = 0; j < 5; j++)
            if (!used[j] && guess[i] == answer[j]) {
                out[i] = 'Y'; used[j] = 1; break;
            }
    }
    out[5] = '\0';
}

/* 子行程負責的工作：服務單一 client 直到它離開 */
static void handle_client(int fd)
{
    char info[200];
    char line[MAX_LINE], msg[128];
    char answer[6] = "";
    int tries = 0, playing = 0;

    srand((unsigned)time(NULL) ^ (unsigned)getpid()); /* 每個子行程不同亂數種子 */
        __sync_fetch_and_add(g_active, 1);          /* 連線數 +1（原子操作，避免競爭） */
    send_line(fd, "WELCOME Wordle server. Commands: START, GUESS <word>, SYSINFO, QUIT");
    #ifndef NO_ROBUST
    /* ROBUST 機制 5：閒置逾時。超過時間沒收到資料，recv 會回傳 EAGAIN */
    struct timeval tv = { .tv_sec = idle_timeout, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    #endif

    for (;;) {
        int r = recv_line(fd, line, sizeof line);
        if (r < 0) {
            if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                LOG_INFO("client idle for %ds, closing", idle_timeout);
                send_line(fd, "ERR idle timeout");
            } else if (r == -2) {
                LOG_ERR("recv failed: line too long");
                send_line(fd, "ERR line too long");
            } else {
                LOG_ERR("recv failed: %s", strerror(errno));
            }
            break;
        }        
        if (r < 0) {
            LOG_ERR("recv failed: %s", r == -2 ? "line too long" : strerror(errno));
            break;
        }
        LOG_DEBUG("recv: \"%s\"", line);

        if (strcmp(line, "START") == 0) {
            snprintf(answer, sizeof answer, "%s", WORDS[rand() % NWORDS]);
            tries = 0; playing = 1;
            LOG_DEBUG("answer = %s", answer);         /* 只有 DEBUG 編譯才看得到 */
            snprintf(msg, sizeof msg, "OK new game, guess a 5-letter word (%d tries)", MAX_TRIES);
            send_line(fd, msg);

        } else if (strncmp(line, "GUESS ", 6) == 0) {
            if (!playing) { send_line(fd, "ERR send START first"); continue; }

            const char *g = line + 6;
            char guess[6], res[6];
            int ok = (strlen(g) == 5);
            for (int i = 0; ok && i < 5; i++) {
                if (!isalpha((unsigned char)g[i])) ok = 0;
                else guess[i] = (char)tolower((unsigned char)g[i]);
            }
            if (!ok) { send_line(fd, "ERR guess must be exactly 5 letters"); continue; }
            guess[5] = '\0';

            tries++;
            wordle_score(answer, guess, res);
            if (strcmp(guess, answer) == 0) {
                snprintf(msg, sizeof msg, "WIN %s %d/%d", res, tries, MAX_TRIES);
                playing = 0;
            } else if (tries >= MAX_TRIES) {
                snprintf(msg, sizeof msg, "LOSE %s answer=%s", res, answer);
                playing = 0;
            } else {
                snprintf(msg, sizeof msg, "RESULT %s %d/%d", res, tries, MAX_TRIES);
            }
            if (send_line(fd, msg) < 0) {
                LOG_ERR("send failed: %s", strerror(errno));
                break;
            }
        } else if (strcmp(line, "SYSINFO") == 0) {
            build_sysinfo(info, sizeof info);
            if (send_line(fd, info) < 0) {
                LOG_ERR("send failed: %s", strerror(errno));
                break;
            }
        } else if (strcmp(line, "QUIT") == 0) {
            send_line(fd, "BYE");
            break;
        } else {
            send_line(fd, "ERR unknown command");
        }
    }
    __sync_fetch_and_sub(g_active, 1);          /* 連線數 -1 */
}

/* ROBUST 機制 2：回收子行程，避免殭屍行程(zombie) */
#ifndef NO_ROBUST
static void sigchld_handler(int sig)
{
    (void)sig;
    int saved = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) { }   /* 一次可能有多個子行程結束 */
    errno = saved;
}
#endif

int main(int argc, char **argv)
{
    int port = PORT;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) g_log_level++;          /* 執行期除錯：-v 或 -vv */
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) idle_timeout = atoi(argv[++i]);
    }

#ifndef NO_ROBUST
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
#endif
    g_active = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (g_active == MAP_FAILED) { LOG_ERR("mmap: %s", strerror(errno)); return 1; }
    *g_active = 0;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { LOG_ERR("socket: %s", strerror(errno)); return 1; }

    /* ROBUST 機制 3：SO_REUSEADDR，讓 server 重啟時可立刻重用 port */
#ifndef NO_ROBUST
    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        LOG_ERR("bind: %s", strerror(errno)); return 1;
    }
    if (listen(lfd, 16) < 0) { LOG_ERR("listen: %s", strerror(errno)); return 1; }
    LOG_INFO("server listening on port %d (parent pid %d)", port, getpid());

    for (;;) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof caddr;
        int cfd = accept(lfd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            LOG_ERR("accept: %s", strerror(errno));
            continue;
        }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &caddr.sin_addr, ip, sizeof ip);

        pid_t pid = fork();
        if (pid < 0) {
            LOG_ERR("fork: %s", strerror(errno));
            close(cfd);
        } else if (pid == 0) {              /* ---- 子行程 ---- */
            close(lfd);                     /* 子行程不需要監聽 socket */
            LOG_INFO("child serving %s:%d", ip, ntohs(caddr.sin_port));
            handle_client(cfd);
            close(cfd);
            _exit(0);
        } else {                            /* ---- 父行程 ---- */
            close(cfd);                     /* 父行程不需要連線 socket，交給子行程 */
            LOG_INFO("forked child %d for %s:%d", pid, ip, ntohs(caddr.sin_port));
        }
    }
}