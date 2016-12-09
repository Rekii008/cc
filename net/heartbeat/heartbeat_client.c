/*****************************************************************************/
/*** heartbeat-client.c ***/
/*** ***/
/*** Demonstrates how to keep track of the server using a "heartbeat". If ***/
/*** the heartbeat is lost, the connection can be reestablished and the ***/
/*** session resumed. ***/
/*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <resolv.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_PORT 11111    //监听端口
#define SERVER_HOST "127.0.0.1"    //服务器IP地址
#define HEARTBEAT_INTERNAL 10            /*seconds */

int serverfd = 0;
int got_reply = 0;

int reconnect() {

    struct tcp_info info;
    int leng = sizeof(info);
    if (getsockopt(serverfd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t * ) & leng) == 0) {
        if (info.tcpi_state == TCP_ESTABLISHED) {
            printf("already connected(%d), skip\n", serverfd);
            send(serverfd, "?", 1, MSG_OOB);
            return 1;
        } else {
            close(serverfd);
        }
    }

    serverfd = socket(PF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) {
        perror("eval");
        exit(EXIT_FAILURE);
    }

    printf("to connect, %d\n", serverfd);
    struct sockaddr_in seraddr;
    seraddr.sin_family = PF_INET;
    seraddr.sin_port = htons(SERVER_PORT);
    seraddr.sin_addr.s_addr = inet_addr(SERVER_HOST);
    if (connect(serverfd, (struct sockaddr *) &seraddr, sizeof(seraddr)) != 0) {
        perror("connect failed\n");
        return -1;
    }
    if (fcntl(serverfd, F_SETOWN, getpid()) != 0) { //设置接受SIGURG信号的进程为本进程(SIGIO也可以这样设置）
        perror("Can't claim SIGURG\n");
        return -2;
    }
    got_reply = 1;
    alarm(HEARTBEAT_INTERNAL); //开始带外心跳

    return 0;
}

/*---------------------------------------------------------------------
    sig_handler - if the single is OOB, set flag. If ALARM, send heartbeat.
 ---------------------------------------------------------------------*/
void sig_handler(int signum) {

    struct tcp_info info;
    int len = sizeof(info);

    if (signum == SIGURG) {
        if (getsockopt(serverfd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t * ) & len) == 0) {
            if ((info.tcpi_state != TCP_ESTABLISHED)) {
                printf("socket is not ready. cannot ping.\n");
                return;
            }
        } else {
            printf("socket error: %s\n", strerror(errno));
            return;
        }

        char c;
        if (recv(serverfd, &c, sizeof(c), MSG_OOB) <= 0) {
            return;
        }
        got_reply = (c == 'Y'); //收到心跳，设置心跳标志
        printf("recd pong\n");
    } else if (signum == SIGALRM) {
        if (got_reply) {    //心跳标志，初始化为1
            if (send(serverfd, "?", 1, MSG_OOB) <= 0) {
                perror("send ping failed.\n");
                return;
            }
            printf("send ping\n");
            got_reply = 0;  //发送心跳之后，重置心跳标志
        } else {
            fprintf(stderr, "connection lost, reconnect\n");
            reconnect();
        }
        //unsigned int alarm(unsigned int seconds);
        //seconds秒之后发送一个SIGALARM信号给当前进程; 0表示取消该闹钟
        alarm(HEARTBEAT_INTERNAL);
    }
}

/*---------------------------------------------------------------------
    main - set up client and begin the heartbeat.
 ---------------------------------------------------------------------*/
int main(int count, char *strings[]) {
    int bytes, len;
    char line[100];

    struct sigaction act;
    bzero(&act, sizeof(act));
    act.sa_handler = sig_handler;
    //系统调用被信号中断，中断处理程序完成之后，重新开始这个系统调用。（默认行为是让系统调用失败，errno=EINTR)
    act.sa_flags = SA_RESTART;
    //sigaction(int signum, const struct sigaction *act, struct sigaction *old_act);
    //act=NULL,则get信号处理函数; act!=NULL则set信号处理函数; 旧的信号处理函数存在old_act
    sigaction(SIGURG, &act, 0);
    sigaction(SIGALRM, &act, 0);

    while (1)   //try to connect until success
    {
        if (reconnect() < 0) {
            sleep(2);
            continue;
        }

        do {
            memset(line, 0x00, 100);
            scanf("%s", line);
            printf("send [%s]\n", line);

            //get the options for the socket, Options may exist at multiple protocol levels
            //TCP_INFO = 'infomation about this connection'
            //struct_info::tcpi_state = 'TCP_ESTABLISHED,TCP_SYN_SENT...' tcp状态转移图中的11种状态
            struct tcp_info info;
            int leng = sizeof(info);
            getsockopt(serverfd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t * ) & leng);
            if (info.tcpi_state != TCP_ESTABLISHED) {
                perror("not connected now.\n");
                break;
            }
            if ((len = send(serverfd, line, strlen(line), 0)) < 0) {
                perror("send fail.\n");
                break;
            }

            if ((bytes = recv(serverfd, line, sizeof(line), 0)) < 0) {
                perror("recv fail.\n");
                break;
            }
        } while (bytes > 0);
    }
    return 0;
}

