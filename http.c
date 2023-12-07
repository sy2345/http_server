#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void accept_request(int);
void bad_request(int);
void cat(int, FILE *, int, int);
void error_die(const char *);
int get_line(int, char *, int);
void headers(int, const char *, int);
void not_found(int);
void serve_file(int, const char *, int, int);
int startup(u_short *);
void unimplemented(int);
void deal_line1(int, char *, int, int);
void deal_line7(int,  char *, char *);
void stop(int);
void redirect(int);
int file_size = 0;
int server_sock = -1;
/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
// line1: numchars:25,buf:GET /index.html HTTP/1.1
// line2: numchars:16,buf:Host: localhost
// line3: numchars:35,buf:User-Agent: python-requests/2.22.0
// line4: numchars:31,buf:Accept-Encoding: gzip, deflate
// line5: numchars:12,buf:Accept: */*
// line6: numchars:23,buf:Connection: keep-alive
// line7: numchars:21,buf:Range: bytes=100-200

void accept_request(int client)
{
  char buf[1024];
  char line1[1024];
  char range[20] = "Range";
  char line7[1024];
  memset(line7,'\0',sizeof(line7));
  memset(line1,'\0',sizeof(line1));
  int numchars;
    char *str;
    
  //读http 请求的第一行数据（request line），把请求方法存进 method 中
  int line = 1;
  do
  {
    numchars = get_line(client, buf, sizeof(buf));
    str=strstr(buf,range);
    // printf("str: %s",str);
    printf("line%d: numchars:%d,buf:%s\n", line, numchars, buf);

    if (line == 1)
    {
      strcpy(line1, buf); // GET /index.html HTTP/1.1
    }
    else if (str)
    {
      strcpy(line7, buf); // Range: bytes=100-200
    }
    line++;
  } while ((numchars > 0) && strcmp("\n", buf));

  // printf("line1: %s\r", line1);
  // printf("245：sizeof(line1):%ld\n", sizeof(line1));
  deal_line7(client, line7, line1); //
}

void deal_line7(int client,  char *line7_, char *line1)
{
  char *str;
  str = strstr(line7_, "Range");
  if (!str)
  { //没有包含Range字符串，直接返回所需要的内容，然后结束
    deal_line1(client, line1, 0, 0);
    return;
  }
  // 包含Range,206,返回部分内容。
  int start = 0;
  int end = 0;
  int gang = 0; //是否到-的位置？
  char line7[1024];
  int len = 0;
  // for(int i = 10;i<sizeof(line7);i++){
  for (int i = 10; i < sizeof(line7) && !isspace(line7_[i]); i++)
  {
    line7[len++] = line7_[i];
    // printf("line7_[%d]:%c\r\n",i,line7_[i]);
  }
  line7[len] = '\0';
  printf("213:len:%d; line7:%s\r\n", len, line7);
  for (int i = 0; i < len; i++)
  {
    // printf("216,line7[%d]: %c,isdigit:%d;start:%d;end:%d;\n", i, line7[i], isdigit(line7[i]), start, end);
    if (isdigit(line7[i]) && gang == 0)
    {
      start = start * 10 + (line7[i] - '0');
    }
    if (isdigit(line7[i]) && gang == 1)
    {
      end = end * 10 + (line7[i] - '0');
    }
    if (line7[i] == '-')
    {
      gang = 1; // end不存在时end=0,start不存在时start=0
    }
  }
  deal_line1(client, line1, start, end);
  // printf("221,start-end:%d,%d\n", start, end);
  // printf("203:line7_:%s\n", line7_);
  // printf("205:sizeof(line7):%ld\n", sizeof(line7));
}

void deal_line1(int client, char *line, int start, int end)
{
  char method[255];
  char url[255];
  char path[512];
  char line1[1024];
  strcpy(line1, line); //传过来的参数line,长度只有8,可能是地址。需要拷贝
  // printf("83:sizeof(line1):%ld\n",sizeof(line1));
  size_t i, j;
  struct stat st;
  char *query_string = NULL;
  i = 0;
  j = 0;
  while (!ISspace(line1[j]) && (i < sizeof(method) - 1))
  {
    method[i] = line1[j];
    i++;
    j++;
  }
  method[i] = '\0';

  //如果请求的方法不是 GET 或 POST 任意一个的话就直接发送 response 告诉客户端没实现该方法:501
  if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
  {
    unimplemented(client);
    return;
  }

  i = 0;
  //跳过所有的空白字符(空格)
  while (ISspace(line1[j]) && (j < sizeof(line1)))
    j++;

  //然后把 URL 读出来放到 url 数组中
  while (!ISspace(line1[j]) && (i < sizeof(url) - 1) && (j < sizeof(line1)))
  {
    url[i] = line1[j];
    i++;
    j++;
  }
  url[i] = '\0';
  printf("114: %s\r\n", url);


  //将前面分隔两份的前面那份字符串，拼接在字符串htdocs的后面之后就输出存储到数组 path 中。相当于现在 path 中存储着一个字符串
  // sprintf(path, "htdocs%s", url);
  // strcat(path, "index.html");
  sprintf(path, "dir%s", "");
  // strcat(path, "dir");
  printf("135: %s\r\n", url);
  if (strcmp(url, "/index.html") == 0 && start == 0 && end == 0)
  {
    printf("301,redirect,%s\r", url);
    redirect(client);
    close(client);
    return;
  }
  else if (strcmp(url, "/dir/index.html") == 0 || strcmp(url, "/index.html") == 0 || strcmp(url, "/") == 0)
  {
    strcat(path, "/index.html");
    // printf("==/dir/index.html");
  }
  else
  {
    strcat(path, url);
  }
  printf("144: %s\r\n", path);
  printf("145: %s\r\n", url);
  // printf("139: %ld\r\n",strlen(url));
  //如果 path 数组中的这个字符串的最后一个字符是以字符 / 结尾的话，就拼接上一个"index.html"的字符串。首页的意思
  if (path[strlen(path) - 1] == '/')
    strcat(path, "index.html");

  // strcat(path, "/index.html");
  //在系统上去查询该文件是否存在
  printf("154: %s\r\n", path);
  if (stat(path, &st) == -1)
  {
    printf("404notfound,%s\r", path);
    //如果不存在，那把这次 http 的请求后续的内容(head 和 body)全部读完并忽略
    //然后返回一个找不到文件的 response 给客户端
    not_found(client);
    // printf("404notfound2,%s",path);
  }
  else
  {
    if ((st.st_mode & S_IFMT) == S_IFDIR) //如果这个文件是个目录
    {
      strcat(path, "/index.html");
    }

      serve_file(client, path, start, end);

  }
  printf("220:%s\r\r", "close(client)");

  close(client);
}

void serve_file(int client, const char *filename, int start, int end) //发送文件给客户端，调用hearder()
{
  FILE *resource = NULL;
  int numchars = 1;
  char buf[1024];

  //确保 buf 里面有东西，能进入下面的 while 循环
  // buf[0] = 'A';
  // buf[1] = '\0';
  //循环作用是读取并忽略掉这个 http 请求后面的所有内容
  // while ((numchars > 0) && strcmp("\n", buf)) /* read & discard headers */
  //   numchars = get_line(client, buf, sizeof(buf));

  //打开这个传进来的这个路径所指的文件
  resource = fopen(filename, "r");
  if (resource == NULL)
    not_found(client);
  else
  {
    //打开成功后，将这个文件的基本信息封装成 response 的头部(header)并返回
    if (start == end && start == 0)
    {
      headers(client, filename, 200);
    }
    else
    {
      printf("579,filename:%s\n", filename);
      headers(client, filename, 206);
      printf("598,headersok:%s\n", filename);
    }
    //接着把这个文件的内容读出来作为 response 的 body 发送到客户端
    cat(client, resource, start, end);
  }

  fclose(resource);
}

void headers(int client, const char *filename, int code) //回复200,供serve_file调用
{
  // 计算文件大小
  struct stat statbuf;
  stat(filename, &statbuf);
  file_size = statbuf.st_size;

  char buf[1024];
  (void)filename; /* could use filename to determine file type */
  if (code == 200)
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
  else if (code == 206)
    strcpy(buf, "HTTP/1.0 206 OK\r\n");
  send(client, buf, strlen(buf), 0);

  strcpy(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);

  sprintf(buf, "Content-Type: text/html\r\n");
  send(client, buf, strlen(buf), 0);

  sprintf(buf, "Content-Length: %d\r\n", file_size);
  send(client, buf, strlen(buf), 0);
}

void cat(int client, FILE *resource, int start, int end)
{
  char buf[1024];
  int end_ = 0;
  if (end == 0)
    end_ = file_size - 1;
  else
  {
    end_ = end;
  }
  sprintf(buf, "Content-Range: bytes %d-%d/%d\r\n", start, end_, file_size);
  send(client, buf, strlen(buf), 0);

  strcpy(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
  // 上边是请求头的内容

  printf("cat,start-end:%d-%d\n", start, end);
  if (0 == end && start == 0)
  { //从文件文件描述符中读取指定内容（全部）
    char buf[1024];
    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
      send(client, buf, strlen(buf), 0);
      fgets(buf, sizeof(buf), resource);
    }
    printf("cat,all ok,start-end:%d-%d\n", start, end);
  }
  else
  {
    int num = 0; //记录第多少个。
    int linenum = 0;
    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
      linenum = strlen(buf);
      if (linenum + num < start)
      {
        //全都不需要，不发送
      }
      else if (num < start && num + linenum > start)
      {                      //只需要后半部分
        int s = start - num; //从再需要s个才能开始
        send(client, buf + s, strlen(buf) - s, 0);
      }
      else if (linenum + num >= start && (linenum + num <= end || end == 0))
      { //全部需要，全部发送
        send(client, buf, strlen(buf), 0);
      }
      else if (linenum + num >= end && end != 0)
      {                                  //只需要前半部分
        int s = num + linenum - end - 1; //后面s个不需要
        send(client, buf, strlen(buf) - s, 0);
        break;
      }
       num += linenum;
      // printf("part%d, len:%d;  send : %s\n", num, strlen(buf), buf);
      fgets(buf, sizeof(buf), resource);
    }
  }
}

void bad_request(int client)
{
  char buf[1024];

  sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "Content-type: text/html\r\n");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "<P>Your browser sent a bad request, ");
  send(client, buf, sizeof(buf), 0);
  sprintf(buf, "such as a POST without a Content-Length.\r\n");
  send(client, buf, sizeof(buf), 0);
}

void error_die(const char *sc)
{
  //包含于<stdio.h>,基于当前的 errno 值，在标准错误上产生一条错误消息。参考《TLPI》P49
  perror(sc);
  exit(1);
}


int get_line(int sock, char *buf, int size) //获取socket的一行，一行
{
  int i = 0;
  char c = '\0';
  int n;

  while ((i < size - 1) && (c != '\n'))
  {
    // recv()包含于<sys/socket.h>,参读《TLPI》P1259,
    //读一个字节的数据存放在 c 中
    n = recv(sock, &c, 1, 0);
    /* DEBUG printf("%02X\n", c); */
    if (n > 0)
    {
      if (c == '\r')
      {
        //
        n = recv(sock, &c, 1, MSG_PEEK);
        /* DEBUG printf("%02X\n", c); */
        if ((n > 0) && (c == '\n'))
          recv(sock, &c, 1, 0);
        else
          c = '\n';
      }
      buf[i] = c;
      i++;
    }
    else
      c = '\n';
  }
  buf[i] = '\0';

  return (i);
}

void not_found(int client) //回复404
{
  char buf[1024];

  sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-Type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "your request because the resource specified\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "is unavailable or nonexistent.\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "</BODY></HTML>\r\n");
  send(client, buf, strlen(buf), 0);
}

void redirect(int client) //回复301
{
  char buf[1024];

  sprintf(buf, "HTTP/1.0 301 redirect\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-Type: text/html\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Location: https://10.0.0.1/index.html\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<HTML><TITLE>redirect</TITLE>\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<BODY><P>Please go to https\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "the file xxx/index.html, you should go to https\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "</BODY></HTML>\r\n");
  send(client, buf, strlen(buf), 0);
}

// 监听端口，返回socket
int startup(u_short *port)
{
  int httpd = 0;
  struct sockaddr_in name;

  httpd = socket(PF_INET, SOCK_STREAM, 0);
  if (httpd == -1)
    error_die("socket");

  memset(&name, 0, sizeof(name));
  name.sin_family = AF_INET;
  name.sin_port = htons(*port);
  name.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
    error_die("bind");

  //如果调用 bind 后端口号仍然是0，则手动调用getsockname()获取端口号
  if (*port == 0) /* if dynamically allocating a port */
  {
    int namelen = sizeof(name);
    // getsockname()包含于<sys/socker.h>中，参读《TLPI》P1263
    //调用getsockname()获取系统给 httpd 这个 socket 随机分配的端口号
    if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
      error_die("getsockname");
    *port = ntohs(name.sin_port);
  }

  //最初的 BSD socket 实现中，backlog 的上限是5.参读《TLPI》P1156
  if (listen(httpd, 5) < 0)
    error_die("listen");
  return (httpd);
}

// 除get,post,其他 method 返回：501
void unimplemented(int client)
{
  char buf[1024];

  sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, SERVER_STRING);
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "Content-Type: video/mpeg4\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "</TITLE></HEAD>\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
  send(client, buf, strlen(buf), 0);
  sprintf(buf, "</BODY></HTML>\r\n");
  send(client, buf, strlen(buf), 0);
}

void stop(int signo) // linux ctrl + C 会产生 SIGINT信号
{
  close(server_sock);
  printf("stop\n");
  _exit(0);
}

// int main(void) // 调用了startup(&port); accept_request(client_sock);
int http()
{
  server_sock = -1;
  u_short port = 80;
  int client_sock = -1;
  struct sockaddr_in client_name;
  int client_name_len = sizeof(client_name);

  server_sock = startup(&port);
  printf("httpd running on port %d\n", port);

  signal(SIGINT, stop); /*注册SIGINT 信号,Ctrl+C, close */
  while (1)
  {
    //阻塞等待客户端的连接，参读《TLPI》P1157
    client_sock = accept(server_sock,
                         (struct sockaddr *)&client_name,
                         &client_name_len);
    if (client_sock == -1)
      error_die("accept");
    accept_request(client_sock);
  }

  close(server_sock);
  return (0);
}
