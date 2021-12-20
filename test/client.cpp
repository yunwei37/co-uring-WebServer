#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <iostream>

using namespace std;

#define MAXSIZE 1024
#define IPADDRESS "127.0.0.1"
#define SERV_PORT 8000
#define FDSIZE 1024
#define EPOLLEVENTS 20

int setSocketNonBlocking1(int fd)
{
    int flag = fcntl(fd, F_GETFL, 0);
    if (flag == -1)
        return -1;

    flag |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flag) == -1)
        return -1;
    return 0;
}

int main(int argc, char *argv[])
{
    int sockfd;
    struct sockaddr_in servaddr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERV_PORT);
    inet_pton(AF_INET, IPADDRESS, &servaddr.sin_addr);
    char buff[4096];
    buff[0] = '\0';
    const char *p = " ";

    p = "GET / HTTP/1.0";
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == 0)
    {
        setSocketNonBlocking1(sockfd);
        cout << "2:" << endl;
        ssize_t n = write(sockfd, p, strlen(p));
        cout << "strlen(p) = " << strlen(p) << endl;
        sleep(1);
        n = read(sockfd, buff, 4096);
        cout << "n=" << n << endl;
        printf("%s", buff);
        close(sockfd);
    }
    else
    {
        perror("err2");
    }
    sleep(1);

    p = "GET / HTTP/1.0\r\nHost: 192.168.52.135:8888\r\nContent-Type: "
        "application/x-www-form-urlencoded\r\nConnection: Keep-Alive\r\n\r\n";
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == 0)
    {
        setSocketNonBlocking1(sockfd);
        cout << "3:" << endl;
        ssize_t n = write(sockfd, p, strlen(p));
        cout << "strlen(p) = " << strlen(p) << endl;
        sleep(1);
        n = read(sockfd, buff, 4096);
        cout << "n=" << n << endl;
        printf("%s", buff);
        close(sockfd);
    }
    else
    {
        perror("err3");
    }
    return 0;
}