#include "csapp.h"

int main(int argc, char **argv) 
{
    int clientfd;
    char *host, *port, buf[MAXLINE];
    rio_t rio;

    // 명령 인자 수 확인하고 제대로 쓰라고 
    if (argc != 3) 
    {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }

    host = argv[1]; // 서버 주소
    port = argv[2]; // 서버 포트

    clientfd = open_clientfd(host, port);   // 서버에 연결, 소켓 fd 반환
    Rio_readinitb(&rio, clientfd);          // robust I/O 초기화

    // 표준 입력에서 한 줄씩 읽어서 서버에 보내고, 서버 응답을 출력
    while (fgets(buf, MAXLINE, stdin) != NULL) 
    {
        Rio_writen(clientfd, buf, strlen(buf)); // 서버로 전송
        Rio_readlineb(&rio, buf, MAXLINE);      // 서버 응답 수신
        fputs(buf, stdout);                     // 응답 출력
    }       

    Close(clientfd);    // 소켓 닫기
    exit(0);
}