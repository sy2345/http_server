// 此头文件用于处理响应
//#include <string.h>
//#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
//#include <bits/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

#include "header.h"
#include "http_header_utils.h"
#include "openssl/ssl.h"
#include "openssl/err.h"
#define SSL_PORT 443
#define MAX_SIZE 4096
#define MIDDLE_SIZE 256
#define BREAK_CNT 8
#define MIN_SIZE 16
#define NAME_LEN 64
#define DEFAULT_FILE "index.html"
#define HTML_DIR "dir/"
#define _DEBUG
const char *CRLF ="\r\n";



// 这个数据结构用来向新开的线程传递参数
typedef struct do_method_para_st
{
    int client_sock;
    SSL *client_ssl;
} * p_do_method_para;

SSL_CTX *init_ssl(char *cert_file, char *private_key_file)
{
    SSL_CTX *ctx;
    // 载入所有的加密算法
    OpenSSL_add_all_algorithms();
    // ssl库初始化
    SSL_library_init();
    // 载入所有的SSL错误消息
    ERR_load_crypto_strings();
    ERR_load_SSL_strings();
    SSL_load_error_strings();
    // 以 SSL V2 和 V3 标准兼容方式产生一个 SSL_CTX ，即 SSL Content Text
    // 也可以用 SSLv2_server_method() 或 SSLv3_server_method() 单独表示 V2 或 V3标准
    ctx = SSL_CTX_new(SSLv23_server_method());

    if (ctx == NULL)
    {
        ERR_print_errors_fp(stdout);
        SSL_CTX_free(ctx);
        return NULL;
    }

    // 载入网站的数字证书， 此证书用来发送给浏览器。 证书里包含有网站的公钥
    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0)
    {
        ERR_print_errors_fp(stdout);
        SSL_CTX_free(ctx);
        return NULL;
    }

    // 载入网站的私钥，用来与客户握手用
    if (SSL_CTX_use_PrivateKey_file(ctx, private_key_file, SSL_FILETYPE_PEM) <= 0)
    {
        ERR_print_errors_fp(stdout);
        SSL_CTX_free(ctx);
        return NULL;
    }

    // 检查网站的公钥私钥是否匹配
    if (!SSL_CTX_check_private_key(ctx))
    {
        ERR_print_errors_fp(stdout);
        SSL_CTX_free(ctx);
        return NULL;
    }

    // SSL数据加载成功
    return ctx;
}

// 简单的封装一下非阻塞的从ssl通道读数据
int servette_read(SSL *ssl, int socket, char *buffer, int buffer_size)
{
    int total_write = 0;
    int oneround_write = 0;
    int flag;
    while (1)
    {
        // SSL_read_ex返回值是1或0，1表示成功0表示失败。oneround_count存储成功时读到的字节数
        flag = SSL_read_ex(ssl, buffer + total_write, buffer_size - total_write, (size_t *)&oneround_write);
        int err = SSL_get_error(ssl, oneround_write);
        if (err == SSL_ERROR_NONE)
        {
            // SSL_ERROR_NONE，表示没有错误
            if (oneround_write > 0)
            {
                // 收到至少一个字节的数据，那么继续读下去
                total_write = total_write + oneround_write;
                if (total_write >= buffer_size)
                {
                    // buffer已经满了
                    return total_write;
                }
                else
                {
                    // 继续读
                    continue;
                }
            }
            else if (oneround_write == 0)
            {
                // 没有收到数据说明客户端当前可能并不在发送数据了
                return total_write;
            }
        }
        else if (err == SSL_ERROR_ZERO_RETURN)
        {
            // ssl通道已经断开，但socket连接可能没断开
            return -1;
        }
        else if ((err == SSL_ERROR_WANT_READ) || (err == SSL_ERROR_WANT_WRITE))
        {
            // 这两个错误表示需要重新调用SSL_read_ex
            // 那就休眠一会了再read,其实这里最好还是用select
            usleep(30000);
            continue;
        }
        else
        {
            // 其他类型的错误，可能不会出现其他类型的错误了
            return -2;
        }
    }
}

// 简单的封装一下非阻塞的向ssl通道写数据
int servette_write(SSL *ssl, int socket, char *buffer, int write_size)
{
    int total_write = 0;
    int oneround_write = 0;
    int flag;
    while (1)
    {
        // SSL_read_ex返回值是1或0，1表示成功0表示失败。oneround_count存储成功时读到的字节数
        flag = SSL_write_ex(ssl, buffer + total_write, write_size - total_write, (size_t *)&oneround_write);
        int err = SSL_get_error(ssl, oneround_write);
        if (err == SSL_ERROR_NONE)
        {
            // SSL_ERROR_NONE，表示没有错误
            if (oneround_write >= 0)
            {
                // 写入了一些数据
                total_write = total_write + oneround_write;
                if (total_write == write_size)
                {
                    // 已经写完了所有的数据
                    return total_write;
                }
                else
                {
                    // 继续写
                    continue;
                }
            }
        }
        else if (err == SSL_ERROR_ZERO_RETURN)
        {
            // ssl通道已经断开，但socket连接可能没断开
            return -1;
        }
        else if ((err == SSL_ERROR_WANT_READ) || (err == SSL_ERROR_WANT_WRITE))
        {
            // 这两个错误表示需要重新调用SSL_write_ex
            // 那就休眠一会了再write,其实这里最好还是用select
            usleep(30000);
            continue;
        }
        else
        {
            // 其他类型的错误，可能不会出现其他类型的错误了
            return -2;
        }
    }
}

/*
Description:
    这个函数用来处理调用openssl库函数之后
    可能产生的SSL_ERROR,并输出错误信息
Parameters:
    SSL *ssl [IN] openssl库函数的操作对象
    int ret_code [IN] openssl库函数调用后的返回值
Return:
    NULL
*/
void handle_ssl_error(SSL *ssl, int ret_code)
{
    int err = SSL_get_error(ssl, ret_code);
    if (err == SSL_ERROR_WANT_ACCEPT)
    {
        printf("SSL_ERROR_WANT_ACCEPT\n");
    }
    else if (err == SSL_ERROR_WANT_CONNECT)
    {
        printf("SSL_ERROR_WANT_CONNECT\n");
    }
    else if (err == SSL_ERROR_ZERO_RETURN)
    {
        printf("SSL_ERROR_ZERO_RETURN\n");
    }
    else if (err == SSL_ERROR_NONE)
    {
        printf("SSL_ERROR_NONE\n");
    }
    else if (err == SSL_ERROR_SSL)
    {
        printf("SSL_ERROR_SSL\n");
        char msg[1024];
        unsigned long e = ERR_get_error();
        ERR_error_string_n(e, msg, sizeof(1024));
        printf("%s\n%s\n%s\n%s\n", msg, ERR_lib_error_string(e), ERR_func_error_string(e), ERR_reason_error_string(e));
    }
    else if (err == SSL_ERROR_SYSCALL)
    {
        printf("SSL_ERROR_SYSCALL\n");
        char msg[1024];
        unsigned long e = ERR_get_error();
        ERR_error_string_n(e, msg, sizeof(1024));
        printf("%s\n%s\n%s\n%s\n", msg, ERR_lib_error_string(e), ERR_func_error_string(e), ERR_reason_error_string(e));
    }
}

/*
Description:
    简单的封装了openSSL库中的SSL_read_ex
    函数。功能是从SSL通道中读取数据。
Parameters:
    SSL *ssl [IN] 要读取的ssl通道
    char * buffer [IN] 读出来的数据的存放位置
    int max_size_of_buffer [IN] 最多可读的数据量
    unsigned long * *size_of_buffer [IN] 实际读出来的数据量
Return:
    int [OUT] 1表示读成功,0表示读失败并需要关闭ssl通道
*/
int ssl_read(SSL *ssl, char *buffer, int max_size_of_buffer, unsigned long *size_of_buffer)
{
    // SSL_read_ex第四个参数是指针值
    // 表示读到的数据的多少。此函数
    // 返回值为1表示成功，0表示错误
    int ret_code = SSL_read_ex(ssl, buffer, max_size_of_buffer, size_of_buffer);
    if (ret_code == 1)
    {
        return 1;
    }
    else
    {
        handle_ssl_error(ssl, ret_code);
        return 0;
    }
}

/*
Description:
    简单的封装了openSSL库中的SSL_write_ex
    函数。功能是向SSL通道中写入数据。
Parameters:
    SSL *ssl [IN] 要写入数据的ssl通道
    char * buffer [IN] 要写入的数据的存放位置
    int max_size_of_buffer [IN] 要写入的数据的量
    int *size_of_buffer [IN] 实际写入的数据的量
Return:
    int [OUT] 1表示写成功,0表示写失败并需要关闭ssl通道
*/
int ssl_write(SSL *ssl, const char *buffer, int want_write, unsigned long *real_write)
{
    // SSL_write_ex第四个参数是指针值
    // 表示写入的数据的多少。此函数
    // 返回值为1表示成功，0表示错误
    int ret_code = SSL_write_ex(ssl, buffer, want_write, real_write);
    if (ret_code == 1)
    {
        return 1;
    }
    else
    {
        handle_ssl_error(ssl, ret_code);
        return 0;
    }
}

// 上传下载文件夹位置
// extern char *file_base_path;

/*
Description:
    判断socket是否关闭
Return:
    0：开启
    1：关闭
*/
int judge_socket_closed(int client_socket)
{
    char buff[32];
    int recvBytes = recv(client_socket, buff, sizeof(buff), MSG_PEEK);
    int sockErr = errno;

    if (recvBytes > 0) // Get data
        return 0;

    if ((recvBytes == -1) && (sockErr == EWOULDBLOCK)) // No receive data
        return 0;

    return 1;
}

/*
Description:
    读取html文件，并显示在网页上
Parameters:
    int client_sock [IN] 客户端的socket
    char *file: [IN] 解析的html文件名
Return:
    NULL
*/
void response_webpage(SSL *client_ssl, int client_sock, char *file)
{
    int fd, ret_code;
    unsigned long real_size;
    char buf[MAX_SIZE], header[MAX_SIZE];
    int size = -1;
    char *file_name;
    file_name = (char *)malloc(MIDDLE_SIZE * sizeof(char));
    if (strcmp(file, "/") == 0)
    {
        //如果没有指定文件，则默认打开index.html
        sprintf(file_name, "%s%s", HTML_DIR, DEFAULT_FILE);
    }
    else
    {
          printf("76%s : %s\n",file_name,file+1);
        sprintf(file_name, "%s", file+1);
    }
    //读取文件
    printf("77%s\n",file_name);
    fd = open(file_name, O_RDONLY);
    if (fd == -1)
    {
        //打开文件失败时构造404的相应
        construct_header(header, 404, "text/html");

        // 这里暂时默认其real_write_size就是strlen(header)
        ret_code = ssl_write(client_ssl, header, strlen(header), &real_size);
        return;
    }
    else
    {
        const char *type = get_type_by_name(file_name);
        construct_header(header, 200, type);
        int ret_code = ssl_write(client_ssl, header, strlen(header), &real_size);
        // write(client_sock,echo_str,strlen(echo_str));
        while (size)
        {
            // size代表读取的字节数
            size = read(fd, buf, MAX_SIZE);
            if (size > 0)
            {
                // if(judge_socket_closed(client_sock))
                // {
                //     printf("传输中断\n");
                //     return;
                // }
                ret_code = ssl_write(client_ssl, buf, size, &real_size);
            }
        }
        return;
    }
}

/*
Description:
    对于无法解析的请求，暂用作回声服务器
Parameters:
    int client_sock [IN] 客户端的socket
    char *msg: [IN] 消息内容
Return:
    NULL
*/
void response_echo(int client_sock, char *msg)
{
    write(client_sock, msg, strlen(msg));
}
// 对url进行解码
#include <string.h>



/*
Description:
    将hex字符转换成对应的整数
Parameters:
    char c [IN] 要转换的字符
Return 
    0~15：转换成功，
    -1:表示c 不是 hexchar
 */
int hexchar2int(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else
        return -1;
}

/*
Description:
    对src进行url解码
Parameters:
    char *src [IN] urlencode后的字符串形式
Return 
    null: 字符串src的形式不对
    否则 解析成功后的字符串
 */
char *urldecode(char *src)
{
    int len = strlen(src);
    int count = len;
    char *dst = (char *)malloc(sizeof(char) * (count+1));
    if (! dst ) // 分配空间失败
        return NULL;
    //节约空间，直接用变量len和count来充当临时变量
    char *dst1 = dst;
    while(*src){//字符串没有结束
        if ( *src == '%')
        {//进入解析状态
            src++;
            len = hexchar2int(*src);
            src++;
            count = hexchar2int(*src);
            if (count == -1 || len == -1)
            {//判断字符转换成的整数是否有效
                *dst1++ = *(src-2);
                src--;
                continue;
            }
            *dst1++ =(char)( (len << 4) + count);//存储到目的字符串
        }
        else
        {
            *dst1++ = *src;
        }
        src++;
    }
    *dst1 = '\0';
    return dst;
}

/*
Description:
    对message进行url解码，直接修改message
Parameters:
    char *message [IN]/[OUT] 解码的字符串
Return 
    null
 */
void decode_message(char *message)
{
    char *decode_msg = urldecode(message);
    sprintf(message,"%s",decode_msg);
    free(decode_msg);
}


/*
Description:
    从输入的buffer字符数组中的下标pos开始，到下一个"\r\n"。
    获取完整的一行。输入的数字代表限制长度。此函数仅仅可以
    在
Parameters:

Return:
    返回下一行第一个字符的位置
*/
int get_next_line(char *temp, const char *buffer, int pos_of_buffer, int limit)
{
    int pos_of_temp = 0;
    for (; pos_of_temp < limit;)
    {
        if (buffer[pos_of_buffer] == '\r' || buffer[pos_of_buffer + 1] == '\n')
            break;
        else
            temp[pos_of_temp++] = buffer[pos_of_buffer++];
    }
    temp[pos_of_temp] = '\0';

    // 这里还需要处理！！！
    return (pos_of_buffer + 2);
}

/*
Description:
    从输入的buffer字符数组中的下标pos开始，到下一个"\r\n"。
    获取完整的一行。输入的数字代表限制长度。此函数仅仅可以
    在
Parameters:

Return:
    成功返回0，失败返回1
*/

/*
Description:
    从客户端读取请求并对请求的内容进行处理
    分割为GET和POST请求
Parameters:
    void *p_client_sock [IN] 客户端的socket
Return:
    NULL
*/
void *do_Method(void *paras)
{
    //令当前线程分离
    // pthread_detach(pthread_self());

    // 从主函数中获取参数
    int client_sock = (*(p_do_method_para *)paras)->client_sock;
    SSL *client_ssl = (*(p_do_method_para *)paras)->client_ssl;
    int ret_code;
    unsigned long size_of_buffer;

    // int tid = pthread_self();
    //  int break_cnt = 0; 用于指定tcp连接断开,一定次数的无连接即断开tcp
    char *Connection; //记录Connection的连接信息
    char methods[5];  // GET or POST
    char *buffer, *message;
    buffer = (char *)malloc(MAX_SIZE * sizeof(char));
    message = (char *)malloc(MIDDLE_SIZE * sizeof(char));

    // 设置非阻塞
    // int flags;
    // flags = fcntl(client_sock, F_GETFL, 0);
    // flags |= O_NONBLOCK;
    // fcntl(client_sock, F_SETFL, flags);

    // 进行ssl握手
    // openssl库中的函数只有返回大于0才是正确执行的，否则都会有错误
    ret_code = SSL_accept(client_ssl);
    if (ret_code == 1)
    {
        printf("ssl握手成功\n");
    }
    else
    {
        handle_ssl_error(client_ssl, ret_code);
        goto Exit;
    }


    ret_code = ssl_read(client_ssl, buffer, MAX_SIZE, &size_of_buffer);
    if (ret_code == 0)
    {
        goto Exit;
    }
    else
    {
        if (size_of_buffer < MAX_SIZE)
        {
            // 避免输出buffer时乱码
            buffer[size_of_buffer] = '\0';
        }
    }

    // break_cnt = 0;
    // buffer是接收到的请求，需要处理
    // 从buffer中分离出请求的方法和请求的参数
    int s_ret = sscanf(buffer, "%s %s", methods, message);
    if (s_ret != 2)
    {
        //如果buffer中的数据无法被分割为两个字符串，则默认echo
        response_echo(client_sock, buffer);
    }

    //获得所有的首部行组成的链表
    http_header_chain headers = (http_header_chain)malloc(sizeof(_http_header_chain));
    // begin_pos_of_http_content是buffer中可能存在的HTTP内容部分的起始位置, GET报文是没有的，POST报文有
    int begin_pos_of_http_content = get_http_headers(buffer, &headers);
    // print_http_headers(&headers);
    //  Connection = (char *)malloc(MIN_SIZE * sizeof(char));
    //  int keep_alive = get_http_header_content("Connection", Connection, &headers, MAX_SIZE);

    decode_message(message);

    switch (methods[0])
    {
    // GET
    case 'G':
        printf("250%s",message);
        response_webpage(client_ssl, client_sock, message);
        
        break;
    default:
        printf("不支持的请求:\n");
        response_echo(client_sock, buffer);
    }

Exit:
    free(buffer);
    free((*(p_do_method_para *)paras));
    free(message);
    SSL_free(client_ssl);
    close(client_sock);
}
