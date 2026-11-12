#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    int port = PORT;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) g_log_level++;
        else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) host = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) port = atoi(argv[++i]);
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { LOG_ERR("socket: %s", strerror(errno)); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        LOG_ERR("bad address: %s", host); return 1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        LOG_ERR("connect: %s", strerror(errno)); return 1;
    }
    LOG_INFO("connected to %s:%d", host, port);

    char line[MAX_LINE], reply[MAX_LINE];
    if (recv_line(fd, reply, sizeof reply) <= 0) { LOG_ERR("no welcome"); return 1; }
    printf("%s\n", reply);

    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;      /* Ctrl-D 或輸入結束 */
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;

        LOG_DEBUG("send: \"%s\"", line);
        if (send_line(fd, line) < 0) { LOG_ERR("send: %s", strerror(errno)); break; }

        int r = recv_line(fd, reply, sizeof reply);
        if (r <= 0) { printf("server closed connection\n"); break; }
        printf("%s\n", reply);
        if (strcmp(reply, "BYE") == 0) break;
    }
    close(fd);
    return 0;
}