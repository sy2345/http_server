#include "response_ssl.h"

/*
Description:
    启动socket监听
Return:
    socket的文件描述符
*/
int start_server()
{
    int ret, sock_stat;
    struct sockaddr_in server_addr; //创建专用socket地址

    sock_stat = socket(PF_INET, SOCK_STREAM, 0); //创建socket
    if (sock_stat < 0)
    {
        printf("socket error:%s\n", strerror(errno));
        exit(-1);
    }

    //设置接收地址和端口重用
    int opt = 1;
    setsockopt(sock_stat, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sock_stat, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;                // ipv4
    server_addr.sin_port = htons(SSL_PORT);          // port:443
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0
    //将socket和socket地址绑定在一起
    ret = bind(sock_stat, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret != 0)
    {
        close(sock_stat);
        printf("bind error:%s\n", strerror(errno));
        exit(-2);
    }
    //开始监听
    ret = listen(sock_stat, 5);
    if (ret != 0)
    {
        close(sock_stat);
        printf("listen error:%s\n", strerror(errno));
        exit(-3);
    }
    printf("Servette Start\nListening port:%d\n", SSL_PORT);
    return sock_stat;
}

// int main(int argc, char *argv[])
int https()
{
    int server_sock, client_sock;
    server_sock = start_server(); // 函数在上边，端口在配置文件
    // 初始化ssl,ctx包含了服务器的ssl证书公钥私钥等信息
    SSL_CTX *ctx = init_ssl("keys/cnlab.cert", "keys/cnlab.prikey");
    if (ctx == NULL)
    {
        printf("init ssl failed, exit!\n");
        return 0;
    }
    else
    {
        printf("init ssl successful.\n");
    }

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        // client_addr在项目最后进行处理
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &len);
        if (client_sock < 0)
        {
            continue;
        }

        if (getpeername(client_sock, (struct sockaddr *)&client_addr, &len) != 0)
        {
            printf("client get port error: %s(errno: %d))\n", strerror(errno), errno);
            break;
        }

        printf("接收到一个连接请求：%s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // 基于ctx产生一个新的SSL读写结构
        SSL *client_ssl = SSL_new(ctx);
        // 把新建的客户端连接client_sock传给新建的SSL读写结构
        if (client_ssl != NULL)
        {
            SSL_set_fd(client_ssl, client_sock);
        }
        p_do_method_para paras = (p_do_method_para)malloc(sizeof(struct do_method_para_st));
        paras->client_sock = client_sock;
        paras->client_ssl = client_ssl;

        do_Method(&paras); //接受请求等
    }
    close(server_sock);
    SSL_CTX_free(ctx);
    return 0;
}
