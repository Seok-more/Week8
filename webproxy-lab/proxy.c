#include <stdio.h>
#include "csapp.h"
#include <pthread.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

// 1. 포트로 바인딩해서 클라이언트로부터 HTTP 요청 받기
// 2. 클라이언트 요청을 받아서 실제 웹 서버(tiny)로 전달
// 3. 웹 서버의 응답을 받아서 다시 클라이언트(브라우저)에게 전달
// 4. + 동시 병렬 처리
// 5. + 캐싱 웹 오브젝트

// [클라이언트] ----HTTP요청----> [프록시] ----HTTP요청----> [tiny]
//            <----HTTP응답----         <----HTTP응답----    

void doit(int fd);
void read_requesthdrs(rio_t *rp, char *header_host, char *header_other);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void reassemble(char *req, char *method, char *path, char *hostname, char *other_header);
void forward_response(int serverfd, int fd, int is_head);

void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  free(vargp); // 동적 할당 해제
  doit(connfd);
  Close(connfd); // 클라이언트 소켓 해제
  return NULL;
}

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  // 명령 인자 체크 -> 포트 번호 있냐 없냐
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 서버 리스닝 소켓 생성 (포트번호로)
  listenfd = Open_listenfd(argv[1]);
  printf("Proxy listening on port %s\n", argv[1]); 
  fflush(stdout);
  
  while (1)
  {
    // 클라이언트 연결을 기다림
    clientlen = sizeof(clientaddr);
    int *connfdp = malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:tiny:accept
    
    // 클라이언트의 호스트명과 포트 번호 얻기
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    // // 실제 요청 처리 함수 호출
    // doit(connfd);  // 클라이언트 요청 처리
    // // 연결 종료
    // Close(connfd); // 클라이언트와의 연결 닫기

    // 병렬 처리
    pthread_t tid;
    pthread_create(&tid, NULL, thread, connfdp);
    pthread_detach(tid);
  }
}

// 클라이언트의 HTTP 요청을 읽고
// 타겟 서버(tiny)에 맞게 요청을 변환해서 전달
// 타겟 서버의 응답을 받아
// 다시 클라이언트에게 전달하는 함수
void doit(int fd)
{
  int is_head = 0;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char header_host[MAXLINE], header_other[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  char request[MAXLINE];
  rio_t rio;

  // 클라이언트와 연결된 소켓을 robust I/O 구조체에 바인딩
  Rio_readinitb(&rio, fd);

  // 요청 라인(첫 번째 줄)을 읽어서 buf에 저장 (예: "GET /index.html HTTP/1.0")
  if (!Rio_readlineb(&rio, buf, MAXLINE)) return;

  // 요청 라인 출력 (디버깅용)
  printf("Request line: %s", buf);

  // GET /index.html HTTP/1.1\r\n
  // GET http://localhost:12345/index.html HTTP/1.1\r\n (프록시 요청)
  // HEAD /image.png HTTP/1.0\r\n
  // 요청 라인을 method, uri, version으로 분리해서 저장
  sscanf(buf, "%s %s %s", method, uri, version);

  // 지원하지 않는 HTTP 메서드일 경우 에러 메시지 전송 후 함수 종료
  if (strcasecmp(method, "HEAD") == 0) 
  {
    strcpy(method, "GET"); 
    is_head = 1;
  } 
  else if (strcasecmp(method, "GET")) 
  {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  // 추가적인 요청 헤더(Host, User-Agent 등) 읽어서 무시 (기능 확장 가능)
  read_requesthdrs(&rio, header_host, header_other);

  // URI를 파싱해서 hostname(웹서버 주소), port(웹서버 포트), path(요청 경로) 추출
  parse_uri(uri, hostname, port, path);

  // 추출한 hostname과 port로 웹서버(tiny)에 연결해서 서버 소켓 생성
  int serverfd = Open_clientfd(hostname, port);

  if (serverfd < 0) 
  {
    clienterror(fd, hostname, "502", "Bad Gateway", "Proxy couldn't connect to server");
    return;
  }

  // 클라이언트의 요청을 웹서버에 맞는 형식(HTTP/1.0 등)으로 재조립
  // request에는 최종적으로 tiny 서버에 보낼 요청이 들어감
  reassemble(request, method,path, hostname, header_other);

  // 재조립된 request를 웹서버(서버 소켓)에 전송
  Rio_writen(serverfd, request, strlen(request));

  // 웹서버의 응답을 받아서 클라이언트에게 그대로 전달
  forward_response(serverfd, fd, is_head);

  // 타겟 서버와의 연결 닫기 (자원 해제)
  Close(serverfd);
}

// HTTP 요청의 헤더 부분을 한 줄씩 읽어서 Host 헤더와 기타 헤더를 분리하는 함수
void read_requesthdrs(rio_t *rp, char *header_host, char *header_other)
{
  char buf[MAXLINE];

  // 출력 버퍼를 빈 문자열로 초기화
  header_host[0] = '\0';
  header_other[0] = '\0';

  // 헤더의 끝은 "\r\n" (빈 줄)이므로, 빈 줄을 만날 때까지 반복
  while (Rio_readlineb(rp, buf, MAXLINE) > 0 && strcmp(buf, "\r\n")) {
    // Host 헤더는 별도로 저장 (여러 개일 때 첫 번째만 저장)
    if (!strncasecmp(buf, "Host:", 5)) 
    {
      if (header_host[0] == '\0')
      {
        strncpy(header_host, buf, MAXLINE - 1);
      }
        
    }
    // 프록시가 직접 생성/수정할 헤더는 무시
    else if (!strncasecmp(buf, "User-Agent:", 11) ||
             !strncasecmp(buf, "Connection:", 11) ||
             !strncasecmp(buf, "Proxy-Connection:", 17)) 
    {
      continue;
    }

    // 나머지 헤더는 header_other에 누적 (오버플로우 방지)
    else 
    {
      // 남은 공간 계산
      int remain = MAXLINE - strlen(header_other) - 1;
      if (remain > 0)
      {
        strncat(header_other, buf, remain);
      }
    }
  }
}

void parse_uri(char *uri, char *hostname, char *port, char *path)
{
  char *hostbegin, *hostend, *portbegin, *pathbegin;
  char buf[MAXLINE];

  // uri를 buf에 복사 (원본 파괴 방지)
  strcpy(buf, uri);

  // "//" 위치를 찾아 hostbegin 지정 (http:// 생략 가능)
  hostbegin = strstr(buf, "//");
  hostbegin = (hostbegin != NULL) ? hostbegin + 2 : buf; 

  // 경로(path) 시작 위치 찾기. 없으면 기본 "/"
  pathbegin = strchr(hostbegin, '/');
  if (pathbegin != NULL)
  {
    strcpy(path, pathbegin);      // path에 경로 복사
    *pathbegin = '\0';            // host 부분 문자열 끝 지정
  }
  else
  {
    strcpy(path, "/");
  }

  // 포트 번호(:포트) 위치 찾기
  portbegin = strchr(hostbegin, ':');
  if (portbegin != NULL) 
  {
    *portbegin = '\0';            // host 부분 문자열 끝 지정
    strcpy(hostname, hostbegin);  // hostname 복사
    strcpy(port, portbegin + 1);  // 포트번호 복사
  } 
  else 
  {
    strcpy(hostname, hostbegin);  // 포트 없으면 hostname 복사
    strcpy(port, "80");           // 기본 포트 80
  }
}

// 프록시가 tiny 서버에 전달할 HTTP 요청을 재조립하는 함수 (GET/HEAD 등 메서드 지원)
void reassemble(char *req, char *method, char *path, char *hostname, char *other_header)
{
  // method 인자를 사용하여 요청 라인을 동적으로 생성
  sprintf(req,
    "%s %s HTTP/1.0\r\n"                // 요청 라인 (GET/HEAD 등 메서드)
    "Host: %s\r\n"                      // Host 헤더
    "%s"                                // User-Agent 헤더 (전역 상수)
    "Connection: close\r\n"             // 연결 종료 명시
    "Proxy-Connection: close\r\n"       // 프록시 연결 종료 명시
    "%s"                                // 기타 헤더
    "\r\n",                             // 헤더 끝 표시
    method,
    path,
    hostname,
    user_agent_hdr,
    other_header
  );
}

// 서버(tiny 등)의 응답을 받아 클라이언트에게 그대로 전달하는 함수
void forward_response(int serverfd, int fd, int is_head)
{
    rio_t rio_server;
    char buf[MAXBUF];
    ssize_t n;

    Rio_readinitb(&rio_server, serverfd);

    while ((n = Rio_readnb(&rio_server, buf, MAXBUF)) > 0) {
        if (!is_head) { 
            // GET 요청이면 읽은 내용을 그대로 클라이언트로 전달
            Rio_writen(fd, buf, n);
        }
        // HEAD 요청은 읽기만 하고 보내지 않음
    }
}
// 클라이언트(웹브라우저)가 잘못된 요청을 보냈을 때,
// HTTP 에러 응답(예: 404 Not Found, 501 Not Implemented 등)을
// HTML 형식으로 만들어서 클라이언트에게 보내주는 함수
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  // 에러 응답의 HTML 본문(body) 생성
  sprintf(body,"<html><title>Tiny Error</title>");
  sprintf(body,"%s<body bgcolor=\"ffffff\">\r\n",body); // 배경색 흰색
  sprintf(body,"%s%s: %s\r\n",body, errnum, shortmsg); // 에러 코드 및 짧은 설명
  sprintf(body,"%s<p>%s: %s\r\n",body, longmsg, cause); // 긴 설명 및 원인
  sprintf(body,"%s<hr><em>The Proxy Web server</em>\r\n",body); // 서버 정보

  // HTTP 상태줄 생성 및 전송
  sprintf(buf,"HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));

  // Content-Type 헤더 전송 (HTML)
  sprintf(buf,"Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));

  // Content-Length 헤더 전송 (본문 길이)
  sprintf(buf,"Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));

  // HTML 본문(body) 전송
  Rio_writen(fd, body, strlen(body));
}