#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
static void* c(void*a){ int fd=(int)(long)a; char b[8192]; int n;
    while((n=read(fd,b,sizeof b))>0){ int p=0; while(p<n){ int w=write(fd,b+p,n-p); if(w<=0) break; p+=w; } }
    close(fd); return 0; }
int main(int ac,char**av){ int port=ac>1?atoi(av[1]):21444;
    int lf=socket(AF_INET,SOCK_STREAM,0); int one=1; setsockopt(lf,SOL_SOCKET,SO_REUSEADDR,&one,4);
    struct sockaddr_in s={0}; s.sin_family=AF_INET; s.sin_addr.s_addr=htonl(INADDR_LOOPBACK); s.sin_port=htons(port);
    bind(lf,(void*)&s,sizeof s); listen(lf,128);
    for(;;){ int fd=accept(lf,0,0); if(fd<0)continue; pthread_t t; pthread_create(&t,0,c,(void*)(long)fd); pthread_detach(t); }
    return 0; }
