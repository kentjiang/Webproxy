/*
 * Webproxy Xinyun Jiang
 *
 */

#include <stdio.h>
#include "csapp.h"
#include <pthread.h>

#define   FILTER_FILE   "proxy.filter"
#define   LOG_FILE      "proxy.log"
#define   DEBUG_FILE	"proxy.debug"



int  find_target_address(char * uri,
			 char * target_address,
			 char * path,
			 int  * port);


void  format_log_entry(char * logstring,
		       int sock,
		       char * uri,
		       int size);
		       
void *forwarder(void* args);
void *webTalk(void* args);
void secureTalk(int clientfd, rio_t client, char *inHost, char *version, int serverPort);
void ignore();

int debug;
int proxyPort;
int debugfd;
int logfd;
pthread_mutex_t mutex;


int main(int argc, char *argv[])
{
  int count = 0;
  int listenfd, connfd, clientlen, optval, serverPort, i;
  struct sockaddr_in clientaddr;
  struct hostent *hp;
  char *haddrp;
  sigset_t sig_pipe; 
  pthread_t tid;
  int *args;
  
  if (argc < 2) {
    printf("Usage: ./%s port [debug] [webServerPort]\n", argv[0]);
    exit(1);
  }
  if(argc == 4)
    serverPort = atoi(argv[3]);
  else
    serverPort = 80;
  
  Signal(SIGPIPE, ignore);
  
  if(sigemptyset(&sig_pipe) || sigaddset(&sig_pipe, SIGPIPE))
    unix_error("creating sig_pipe set failed");
  if(sigprocmask(SIG_BLOCK, &sig_pipe, NULL) == -1)
    unix_error("sigprocmask failed");
 
  proxyPort = atoi(argv[1]);

  if(argc > 2)
    debug = atoi(argv[2]);
  else
    debug = 0;


  /* start listening on proxy port */

  listenfd = Open_listenfd(proxyPort);

  optval = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int)); 
  
  if(debug) debugfd = Open(DEBUG_FILE, O_CREAT | O_TRUNC | O_WRONLY, 0666);

  logfd = Open(LOG_FILE, O_CREAT | O_TRUNC | O_WRONLY, 0666);    

  
  pthread_mutex_init(&mutex, NULL);
  
  while(1) {
    clientlen = sizeof(clientaddr);

    /* accept a new connection from a client */

    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    args = malloc(2 * sizeof(int));
    args[0] = connfd;
    args[1] = serverPort;
    pthread_create(&tid, NULL, webTalk, (void*) args);
    pthread_detach(tid);

  }
  
  if(debug) Close(debugfd);
  Close(logfd);
  pthread_mutex_destroy(&mutex);
  
  return 0;
}


void parseAddress(char* url, char* host, char** file, int* serverPort)
{
	char *point1;
        char *point2;
        char *saveptr;

	if(strstr(url, "http://"))
		url = &(url[7]);
	*file = strchr(url, '/');
	
	strcpy(host, url);


	strtok_r(host, "/", &saveptr);

	point1 = strchr(host, ':');
	if(!point1) {
		*serverPort = 80;
		return;
	}
	
	strtok_r(host, ":", &saveptr);

	*serverPort = atoi(strtok_r(NULL, "/",&saveptr));
}


void *webTalk(void* args)
{
  int numBytes, lineNum, serverfd, clientfd, serverPort;
  int tries;
  int byteCount = 0;
  char buf1[MAXLINE], buf2[MAXLINE], buf3[MAXLINE];
  char host[MAXLINE];
  char url[MAXLINE], logString[MAXLINE];
  char *token, *cmd, *version, *file, *saveptr;
  rio_t server, client;
  char slash[10];
  strcpy(slash, "/");
  clientfd = ((int*)args)[0];
  serverPort = ((int*)args)[1];
  free(args);
  printf("===========Enter WebTalk============\n");
  rio_readinitb(&client, clientfd);
  
  // Determine protocol (CONNECT or GET)
  numBytes = rio_readlineb(&client, buf1, MAXLINE); // Get request from client
  if ((strcmp(buf1, "\0") == 0) || (numBytes <= 0)){ //if request is empty, return
    close(clientfd);
    return NULL;
  }

  cmd = strtok_r(buf1, " \r\n", &saveptr); //cmd is "GET" or "CONNECT"
  strcpy(url, strtok_r(NULL, " \r\n", &saveptr));
  parseAddress(url, host, &file, &serverPort);
  if (!file)
    file = slash;
  if (debug){ // if debug, write host & file & serverport to debug file
    sprintf(buf3, "%s %s %i\n", host, file, serverPort);
    write(debugfd, buf3, strlen(buf3));
  }
  printf("cmd : %s \n", cmd);
  if (strcmp(cmd, "GET") == 0){
    printf("========Enter Get===============\n");
    printf("%s %s %i\n", host, file, serverPort);
    serverfd = open_clientfd(host, serverPort);
    if (serverfd <= 0){
      close(clientfd);
      return NULL;
    }
    sprintf(buf2, "%s %s HTTP/1.1\r\n", cmd, file);
    rio_writen(serverfd, buf2, strlen(buf2));
    char buffer[MAXLINE];
    int temp = 0;
    //printf("=====\n");
    while(strcmp(buffer, "\r\n") != 0){
      temp = rio_readlineb(&client, buffer, MAXLINE);
      if (temp < 0) break;
      if (strncmp(buffer, "Connection: keep-alive", 22) == 0){
        strcpy(buffer, "Connection: close\r\n");
        temp = strlen("Connection: close\r\n");
      }
      if (strncmp(buffer, "Proxy-Connection: keep-alive", 22) == 0){
        strcpy(buffer, "Connection: close\r\n");
        temp = strlen("Connection: close\r\n");
      }
      rio_writen(serverfd, buffer, temp);
    }

  // receive the response
    //printf("=====\n");
    while((temp = rio_readp(serverfd, buffer, MAXLINE)) > 0){
      rio_writen(clientfd, buffer, temp);
    }
    close(clientfd);
    close(serverfd);
    printf("Exit WebTalk\n");
  }
  // CONNECT: call a different function, securetalk, for HTTPS
  else if (strcmp(cmd, "CONNECT") == 0){
    secureTalk(clientfd, client, host, version, serverPort);
    return NULL;
  }
}
void secureTalk(int clientfd, rio_t client, char *inHost, char *version, int serverPort)
{
  int serverfd, numBytes1, numBytes2;
  int tries;
  rio_t server;
  char buf1[MAXLINE], buf2[MAXLINE];
  pthread_t tid;
  int *args;
  printf("Enter SecureTalk\n");
  if (serverPort == proxyPort)
    serverPort = 443;
  
  /* Open connecton to webserver */
  serverfd = open_clientfd(inHost, serverPort);
  if (serverfd <= 0){
    close(clientfd);
    return;
  }
  
  
  /* let the client know we've connected to the server */
  strcpy(buf1, "HTTP/1.1 200 Connection established\r\n\r\n");
  numBytes1 = strlen(buf1);
  rio_writen(clientfd, buf1, numBytes1);

  /* spawn a thread to pass bytes from origin server through to client */
  args = malloc(2 * sizeof(int));
  args[0] = clientfd;
  args[1] = serverfd;
  pthread_create(&tid, NULL, forwarder, (void*)args);
  /* now pass bytes from client to server */
  while((numBytes2 = rio_readp(clientfd, buf2, MAXLINE)) > 0){
    rio_writen(serverfd, buf2, numBytes2);
  }
  close(clientfd);
  close(serverfd);
  pthread_join(tid, NULL);
  return;
}


void *forwarder(void* args)
{
  int numBytes, lineNum, serverfd, clientfd;
  int byteCount = 0;
  char buf1[MAXLINE];
  clientfd = ((int*)args)[0];
  serverfd = ((int*)args)[1];
  free(args);

  while((numBytes = rio_readp(serverfd, buf1, MAXLINE)) > 0) {
    rio_writen(clientfd, buf1, numBytes);
  }
  close(clientfd);
  close(serverfd);
  return NULL;
}


void ignore()
{
	;
}


int  find_target_address(char * uri, char * target_address, char * path,
                         int  * port)

{
  //  printf("uri: %s\n",uri);
  

    if (strncasecmp(uri, "http://", 7) == 0) {
	char * hostbegin, * hostend, *pathbegin;
	int    len;
       
	/* find the target address */
	hostbegin = uri+7;
	hostend = strpbrk(hostbegin, " :/\r\n");
	if (hostend == NULL){
	  hostend = hostbegin + strlen(hostbegin);
	}
	
	len = hostend - hostbegin;

	strncpy(target_address, hostbegin, len);
	target_address[len] = '\0';

	/* find the port number */
	if (*hostend == ':')   *port = atoi(hostend+1);

	/* find the path */

	pathbegin = strchr(hostbegin, '/');

	if (pathbegin == NULL) {
	  path[0] = '\0';
	  
	}
	else {
	  pathbegin++;	
	  strcpy(path, pathbegin);
	}
	return 0;
    }
    target_address[0] = '\0';
    return -1;
}


void format_log_entry(char * logstring, int sock, char * uri, int size)
{
    time_t  now;
    char    buffer[MAXLINE];
    struct  sockaddr_in addr;
    unsigned  long  host;
    unsigned  char a, b, c, d;
    int    len = sizeof(addr);

    now = time(NULL);
    strftime(buffer, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    if (getpeername(sock, (struct sockaddr *) & addr, &len)) {
      
      printf("getpeername failed\n");
      return;
    }

    host = ntohl(addr.sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;

    sprintf(logstring, "%s: %d.%d.%d.%d %s %d\n", buffer, a,b,c,d, uri, size);
}
