#include "csapp.h"

// 클라이언트와의 연결에서 데이터를 받아서 그대로 다시 보내는 echo 함수
void echo(int connfd) 
{
    size_t n;
    char buf[MAXLINE];             // 데이터를 저장할 버퍼
    rio_t rio;                     // robust I/O 구조체

    Rio_readinitb(&rio, connfd);   // 소켓과 robust I/O 구조체 연결

    // 클라에서 한 줄씩 읽어서 그대로 다시 보냄
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) 
    {
        printf("server received %d bytes: %s", (int)n, buf);
        Rio_writen(connfd, buf, n);  // 받은 데이터를 클라이언트에게 다시 전송(echo)
    }
}

int main(int argc, char **argv) 
{
    int listenfd, connfd;                               // 서버 소켓, 클라이언트 소켓
    socklen_t clientlen;                                // 클라이언트 주소 구조체 크기
    struct sockaddr_storage clientaddr;                 // 다양한 주소 타입을 지원하는 구조체
    char client_hostname[MAXLINE], client_port[MAXLINE];// 클라이언트 주소/포트 저장 버퍼

    // 명령 인자 제대로 넣었냐
    if (argc != 2) 
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    listenfd = open_listenfd(argv[1]); // 지정한 포트로 서버 소켓 생성 및 대기

    // 클라 연결 처리
    while (1) 
    {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 클라이언트 연결 수락, 소켓 생성

        // 접속한 클라이언트의 호스트명과 포트 정보를 얻어서 출력
        Getnameinfo((SA*)&clientaddr,clientlen,client_hostname,MAXLINE, client_port,MAXLINE,0);
        printf("Connectedto (%s,%s)\n",client_hostname,client_port);

        echo(connfd);       // 클라이언트와 echo 통신
        Close(connfd);      // 클라이언트 소켓 닫기
    }

    exit(0); // 근데 이걸 왜 넣어?
}