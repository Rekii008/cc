/*****************************************************************************/
/*** heartbeat-server.c ***/
/*** ***/
/*** Demonstrates how to keep track of the server using a "heartbeat". If ***/
/*** the heartbeat is lost, the connection can be reestablished and the ***/
/*** session resumed. ***/
/*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <resolv.h>
#include <signal.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUF_SIZE 1024            //默认缓冲区
#define SERVER_PORT 11111        //监听端口
#define SERVER_HOST "127.0.0.1"    //服务器IP地址
#define LISTEN_SIZE 10            //监听队列长度

int client;
struct sigaction act;

/*---------------------------------------------------------------------
    sig_handler - catch and send heartbeat.
 ---------------------------------------------------------------------*/
void sig_handler(int signum) {
    if (signum == SIGURG) {
        char c;
        recv(client, &c, sizeof(c), MSG_OOB);
        if (c == '?') {
            send(client, "Y", 1, MSG_OOB);
        }
    }
}

/*---------------------------------------------------------------------
    servlet - process requests
 ---------------------------------------------------------------------*/
void servlet(void) {
    int bytes;
    char buffer[BUF_SIZE];

    bzero(&act, sizeof(act));
    act.sa_handler = sig_handler;
    act.sa_flags = SA_RESTART;
    sigaction(SIGURG, &act, 0);    /* connect SIGURG signal */
    printf("start servlet, client: %d\n", client);

    if (fcntl(client, F_SETOWN, getpid()) != 0) {
        perror("Can't claim SIGIO and SIGURG");
    }

    do {
        bzero(buffer, BUF_SIZE);
        bytes = recv(client, buffer, sizeof(buffer), 0);
        printf("recieve [%s] from client-%d\n", buffer, client);
        if (bytes > 0) {
            send(client, buffer, bytes, 0);  //echo back
        }
    } while (bytes > 0);

    printf("end servlet: %d\n", client);
    return;
}

/*---------------------------------------------------------------------
    main - set up client and begin the heartbeat.
 ---------------------------------------------------------------------*/
int main(int count, char *strings[]) {
    //子进程退出时，若父进程未提前调用wait/waitpid，则内核依旧会继续保留进程的最小信息（PID，退出状态等），以使父进程稍后可以wait/waitpid获取到。
    //这种情况下，子进程已经退出，但是仍然消耗资源，内核进程表依旧占着一项（占满进程表就不能新建进程），所以称为僵尸进程。
    //wait/waitpid 等待子进程状态改变，包括退出,中断和恢复执行。子进程退出时，wait/waitpid会导致系统释放子进程资源。
    //当子进程退出时，内核会发送SIGCHLD信号予其父进程。在默认情况下，父进程会以SIG_IGN函数忽略。
    //父进程可将SIGCHLD的处理函数设为SIG_IGN（默认），或为SIGCHLD设定SA_NOCLDWAIT标记，以使内核可以自动回收已终止的子进程的资源.
    //Linux2.6以上版本，设置了SIGCHLD的SA_NOCLDWAIT标记或者SIG_IGN处理函数会导致wait调用阻塞，直到所有子进程退出，wait返回-1，errno=ECHILD
    bzero(&act, sizeof(act));
    act.sa_handler = SIG_IGN;
    act.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    if (sigaction(SIGCHLD, &act, 0) != 0) {
        perror("sigaction error.\n");
    }

    int listener;
    struct sockaddr_in addr, peer;
    addr.sin_family = PF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(SERVER_HOST);
    socklen_t socklen;
    socklen = sizeof(struct sockaddr_in);
    listener = socket(PF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("eval");
        exit(EXIT_FAILURE);
    }

    //SO_REUSEADDR 允许重用TIME_WAIT状态的端口，web服务器关闭之后可以立即重启，不会发生端口占用的错误
    //SO_REUSEPORT 允许完全使用相同的地址和端口，linux 3.9添加，可以多进程监听同一个端口。在之前的版本只能通过子进程继承方式实现。
    int on = 1;
    if ((setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0) {
        perror("server: setsockopt failed");
        exit(EXIT_FAILURE);
    }

    if (bind(listener, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("server: bind failed.");
        exit(EXIT_FAILURE);
    }

    if (listen(listener, LISTEN_SIZE) < 0) {
        perror("server: listen failed.");
        exit(EXIT_FAILURE);
    }
    printf("server: listening\n");

    while (1) {
        client = accept(listener, (struct sockaddr *) &peer, &socklen);
        printf("accept client-%d\n", client);
        if (client > 0) {
            if (fork() == 0) //in child
            {
                close(listener);
                servlet();  //handler connection in loop
                exit(0);
            } else //in parent
            {
                close(client);
            }
        } else {
            perror("server: accept error");
        }
    }
    close(client);
    return 0;
}

