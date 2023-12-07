#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include<https.c>
#include<http.c>
int main(int argc, char const *argv[])
{
    pid_t pid;
  
    pid = fork();
    if(pid == -1){
        perror("fork error");
        return -1;
    }else if(pid == 0){
        //子进程的代码区
        printf("子进程，启动http\r\n");
        http();
    }else if(pid > 0){
        //父进程的代码区
        printf("父进程，启动https\r\n");
        https();
    }
    //父子进程执行没有先后顺序，子进程如果执行结束了，父进程没有为他收尸，子进程变成僵尸进程
    //将父进程结束的时候，init进程为们收尸
    return 0;
}