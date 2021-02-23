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
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>

#define ISspace(x) isspace((int)(x))
#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define STDIN   0
#define STDOUT  1
#define STDERR  2

void accept_request(void *arg);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void execute_cgi(int, const char *, const char *, const char *);
void headers(int, const char *);
int startup(u_short *);
int get_line(int, char *, int);
void unimplemented(int );
void error_die(const char *);
void not_found(int );
void serve_file(int , const char *);

// Http请求的信息
// GET请求
// GET / HTTP/1.1
// Host: 192.168.0.23:47310
// Connection: keep-alive
// Upgrade-Insecure-Requests: 1
// User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.87 Safari/537.36
// Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*; q = 0.8
// Accept - Encoding: gzip, deflate, sdch
// Accept - Language : zh - CN, zh; q = 0.8
// Cookie: __guid = 179317988.1576506943281708800.1510107225903.8862; monitor_count = 5
//
//
// POST请求
// POST / color1.cgi HTTP / 1.1
// Host: 192.168.0.23 : 47310
// Connection : keep - alive
// Content - Length : 10
// Cache - Control : max - age = 0
// Origin : http ://192.168.0.23:40786
// Upgrade - Insecure - Requests : 1
// User - Agent : Mozilla / 5.0 (Windows NT 6.1; WOW64) AppleWebKit / 537.36 (KHTML, like Gecko) Chrome / 55.0.2883.87 Safari / 537.36
// Content - Type : application / x - www - form - urlencoded
// Accept : text / html, application / xhtml + xml, application / xml; q = 0.9, image / webp, */*;q=0.8
// Referer: http://192.168.0.23:47310/
// Accept-Encoding: gzip, deflate
// Accept-Language: zh-CN,zh;q=0.8
// Cookie: __guid=179317988.1576506943281708800.1510107225903.8862; monitor_count=281
// Form Data
// color=gray


void accept_request(void *arg){
	int client = (intptr_t)arg;
	char buf[1024];				//读取客户端发送的内容
	char method[255];			//保存请求方法
	char url[255];				//保存请求的url
	char path[512];				//请求路径
	size_t i,j;
	struct stat st;				//查询文件的数据结构
	int cgi = 0;				//是否调用cgi程序
	char *query_string = NULL;	//
	int numchars = 1;				
	

	//读取http请求的第一行数据(request line),然后把请求方法存进method
	//请求方法 URL 协议版本\r\n
	numchars = get_line(client, buf, sizeof(buf));
	i = 0; j = 0;
	//根据上面的信息，首先到达第一个空格之前的都是请求方法
	while(!ISspace(buf[j]) && (i < sizeof(method) -1) ){
		method[i] = buf[j];
		i++; j++;
	}
	method[i] = '\0';
	
//	printf("请求方法: %s\n", method);
	
	//到这里，请求方法已经获取出来并保存在method中了
	//如果不是get或者post请求
	if(!strcasecmp(method, "GET") == 0 && !strcasecmp(method, "POST") == 0){
		unimplemented(client);
		return ;
	}
	
	//如果是POST，设置cgi=1
	if(strcasecmp(method, "POST") == 0){
		cgi = 1;
	}
	i = 0;
	//跳过所有的空白字符，继续读取
	while(ISspace(buf[j]) && (j < sizeof(buf))){
		j++;
	}
	
	//把URL读出来保存到url数组中
	while(!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))){
		url[i] = buf[j];
		i++; j++;
	}
	url[i] = '\0';
	
	printf("请求url: %s\n", url);
	
	//如果是get请求
	if(strcasecmp(method, "GET") == 0){
		//指针指向url
		query_string = url;
		//去遍历这个url,跳过字符?前面所有的字符
		//如果遍历完没找到字符?则退出
		while((*query_string != '?') && (*query_string != '\0')){
			query_string++;
		}
		//退出循环后检查当前字符是?吗
		if(*query_string == '?'){
			cgi = 1;
			//在字符?处分隔
			*query_string = '\0';
			query_string++;
		}
	}
	
	//？号前的路径
	sprintf(path, "htdocs%s", url);
	//如果path数组最后一个字符是/的话，则拼接上一个index.html
	if(path[strlen(path) - 1] == '/'){
		strcat(path, "index.html");
	}
	
	printf("path: %s\n", path);
	
	//在系统上查看这个文件是否存在
	if(stat(path, &st) == -1){
		//如果不存在，则读取完http后面全部的内容(head)并忽略
		while((numchars > 0) && strcmp("\n", buf)){
			numchars = get_line(client, buf, sizeof(buf));
		}
		//返回值找不到的response
		not_found(client);
	} else{	//如果存在
		//判断请求的文件类型
		if((st.st_mode & S_IFMT) == S_IFDIR){
			//如果是目录，就在path后接/index.html
			strcat(path, "/index.html");
		}
		if( (st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) ||(st.st_mode & S_IXOTH)){
			//S_IXUSR    00100     owner has execute permission
			//S_IXGRP    00010     group has execute permission
			//S_IXOTH    00001     others have execute permission
			//如果是可执行文件，无论是属于用户/组/其他 这三种类型的，将cgi置1
			cgi = 1;
		}
//		printf("cgi = %d\n",cgi);
		if(!cgi){
			//如果不需要cgi的，html页面，直接发送
			serve_file(client, path);
		} else{
			//如果需要cgi调用的
			execute_cgi(client, path, method, query_string);
		}
	}
	close(client);
	printf("close client!\n");
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path, const char *method, const char *query_string){
	
	char buf[1024];			//缓冲区
	int cgi_output[2];		//管道，用于进程交换数据
	int cgi_input[2];
	pid_t pid;				//进程id
	int status;
	int i;
	char c;
	int numchars = 1;
	int content_length = -1;
	//往buf中填东西保证能进入下面的while
	buf[0] = 'A';
	buf[1] = '\0';
	//如果是GET方法，则忽略请求剩下的内容
	if (strcasecmp(method, "GET") == 0) {
		while ((numchars > 0) && strcmp("\n", buf)){  /* read & discard headers */
			numchars = get_line(client, buf, sizeof(buf));
		}
	} else if (strcasecmp(method, "POST") == 0){		//POST请求
		numchars = get_line(client, buf, sizeof(buf));
	//	printf("POST数据读取:\n");
	//	printf("%s\n",buf);
		//这里要对header一行一行读取
		//因为只是POST数据部分长度的请求头数据行为 Content-Length: XX
		//数字部分刚好对应下标16
		while ((numchars > 0) && strcmp("\n", buf)){
            buf[15] = '\0';
			//如果读取到指示数据长度的那一行
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
			
            numchars = get_line(client, buf, sizeof(buf));
			
		//	printf("buf = %s\n",buf);
        }
		//如果请求头中找不到指示数据长度的参数，则报错返回
        if (content_length == -1) {
            bad_request(client);
            return;
        }
	} else{/*HEAD or other*/
    
	}
	
	//表示成功
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	send(client, buf, strlen(buf), 0);
	
	//创建两个管道,用于两个进程的通信
	if(pipe(cgi_output) < 0){
		cannot_execute(client);
		return;
	}
	if(pipe(cgi_input) < 0){
		cannot_execute(client);
		return;
	}
	
	//       fork后管道都复制了一份，都是一样的
	//       子进程关闭2个无用的端口，避免浪费             
	//       ×<------------------------->1    output
	//       0<-------------------------->×   input 

	//       父进程关闭2个无用的端口，避免浪费             
	//       0<-------------------------->×   output
	//       ×<------------------------->1    input
	//       此时父子进程已经可以通信
	
	//创建子进程，fork()从此处开始有两个进程执行相同代码
	if( (pid = fork()) < 0){
		cannot_execute(client);
		return ;
	}
	
	if(pid == 0){				//子进程执行cgi脚本
		char meth_env[255];
		char query_env[255];
		char length_env[255];
		//将子进程的输出由标准输出重定向到 cgi_ouput 的管道写端上，1是stdout
		dup2(cgi_output[1], 1);
		//将子进程的输出由标准输入重定向到 cgi_ouput 的管道读端上，0是stdin
		dup2(cgi_input[0], 0);
		//关闭 cgi_ouput 管道的读端与cgi_input 管道的写端
		
		close(cgi_output[0]);
		close(cgi_input[1]);
		
		//构造CGI环境变量
		sprintf(meth_env, "REQUEST_METHOD=%s", method);
		//将环境变量加入子进程的运行环境中
		putenv(meth_env);
		//根据http请求的方法，构造存储不同的环境变量
		if(strcasecmp(method, "GET") == 0){
			sprintf(query_env, "QUERY_STRING=%s", query_string);
			putenv(query_env);
		} else{	//POST
			sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
			putenv(length_env);
		}
		//替换执行path
		execl(path, path, NULL);
		exit(0);
	} else{		//父进程
		close(cgi_output[1]);		//父进程关闭cgi_output管道的写端和cgi_input管道的读端
		close(cgi_input[0]);
		//如果是 POST 方法的话就继续读 body 的内容，并写到 cgi_input 管道里让子进程去读
		if(strcasecmp(method, "POST") == 0){
			for(i = 0; i < content_length; i++){
				//把post请求的数据(color=xxx)写入input管道中，供子进程读取使用
				recv(client, &c, 1, 0);
			//	printf("recv: %c\n",c);
				write(cgi_input[1], &c, 1);
			}
		}
		//将output管道读取的信息写给客户端，这里是对应的html
		while (read(cgi_output[0], &c, 1) > 0){
			send(client, &c, 1, 0);
		}
		close(cgi_output[0]);			//关闭管道
        close(cgi_input[1]);
        waitpid(pid, &status, 0);		//等待子进程的退出
	}
//	printf("end cgi\n");
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client){
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html;\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client){
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

void serve_file(int client, const char *filename){
	FILE *resource = NULL;
	int numchars = 1;
	char buf[1024];
	//保证buf里面有东西，能进入while循环
	buf[0] = 'A';
	buf[1] = '\0';
	//忽略http请求后面的所有内容
	while((numchars > 0) && strcmp("\n", buf)){
		numchars = get_line(client, buf, sizeof(buf));
	}
	//打开文件
	resource = fopen(filename, "r");
	if(resource == NULL){	//打开失败
		not_found(client);
	} else{					//打开成功
		//打开成功后，把文件的信息封装成response的头部(header)并返回
		headers(client, filename);
		cat(client, resource);
	}
	
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename){
	char buf[1024];
	//伪装手法，避免编译器因为没使用这个变量而发出警告
	(void)filename;  /* could use filename to determine file type */
	
	strcpy(buf, "HTTP/1.0 200 OK\r\n");
	send(client, buf, strlen(buf), 0);
	strcpy(buf, SERVER_STRING);
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(client, buf, strlen(buf), 0);
	strcpy(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource){
	char buf[1024];

	//从文件文件描述符中读取指定内容
	fgets(buf, sizeof(buf), resource);
	while (!feof(resource)){
		send(client, buf, strlen(buf), 0);
		fgets(buf, sizeof(buf), resource);
	}
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client){
	char buf[1024];

	sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, SERVER_STRING);
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
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

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client){
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

//读取http请求
int get_line(int sock, char *buf, int size){
	int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n')){
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0){
            if (c == '\r'){
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
    return(i);
}

int startup(u_short *port){
	int httpd = 0;
	struct sockaddr_in name;
	
	httpd = socket(AF_INET, SOCK_STREAM, 0);
	if(httpd == -1){
		error_die("socket");
	}
	//套接字数据结构初始化
	memset(&name, 0, sizeof(name));
	name.sin_family = AF_INET;
	name.sin_port = htons(*port);
	name.sin_addr.s_addr = htonl(INADDR_ANY);
	// 设置套接字选项避免地址使用错误  
//    int on=1;  
//   if((setsockopt(httpd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on)))<0)  {  
//        error_die("setsockopt failed");  
//        exit(EXIT_FAILURE);  
//    }  
	//将数据结构和创建的httpd套接字绑定
	if(bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0){
		error_die("bind");
	}
	//如果端口没有设置，提供个随机端口
	if (*port == 0) { /* if dynamically allocating a port */ 
		socklen_t  namelen = sizeof(name);
			if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
				error_die("getsockname");
		*port = ntohs(name.sin_port);
	}
	//绑定后开始监听
	if(listen(httpd, 5) < 0){
		error_die("listen");
	}
//	printf("start up OK!\n");
	return httpd;
}

void error_die(const char *sc){
	//包含于<stdio.h>,基于当前的 errno 值，在标准错误上产生一条错误消息。参考《TLPI》P49
	perror(sc); 
	exit(1);
}

int main(void){
	int server_sock = -1;			//服务器的sock文件描述符
	u_short port = 0;
	int client_sock = -1;			//客户端的sock文件描述符
	struct sockaddr_in client_name;	//客户端套接字标签数据结构
	char client_ip[64];				//客户端的ip
	socklen_t client_name_len = sizeof(client_name);
	pthread_t newthread;
	server_sock = startup(&port);	//获得监听sock文件描述符
	
	printf("httpd running on port: %d\n", port);
	while(1){
		//等待客户端连接
		client_sock = accept(server_sock, (struct sockaddr *)&client_name, &client_name_len);
		printf("client ip : %s\t port : %d\n",
			inet_ntop(AF_INET, &client_name.sin_addr.s_addr,client_ip,sizeof(client_ip)),
			ntohs(client_name.sin_port));
		if(client_sock == -1)
			error_die("accept");
		//accept_request(client_sock);
		//每次收到请求，创建一个线程来处理接受到的请求
		//把client_sock转成地址作为参数传入pthread_create
		if (pthread_create(&newthread, NULL, (void *)accept_request, (void *)(intptr_t)client_sock) != 0)
			perror("pthread_create");
	}
	close(server_sock);
	return 0;
}