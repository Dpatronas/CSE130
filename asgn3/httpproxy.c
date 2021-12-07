// Despina Patronas CSE130 Fall 2021 asgn3
// #define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <time.h> 

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h> 
#include <sys/stat.h>
#include <sys/time.h>
#include <getopt.h>
// multithreading libraries
#include <pthread.h>
#include <semaphore.h>
// network libraries
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "queue.h"
#include "httpproxy.h"

#define GETOPTIONS "N:R:s:m:"  // CLI
#define MIN(x, y) ( ( (x) < (y) ) ? (x) : (y) )

extern int errno;
struct stat st;

static int responses = 0; // # running total req. fulfilled
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static uint16_t * server_pool;
static uint32_t * server_status;    // 0 == offline. 1 == online
static uint32_t * server_errors;
static uint32_t * server_entries;

static int total_servers = 0; 
static int currChosenServer = 0;     // Server index to port forward
static int healthFrequency = 5;         // # to health check

static uint32_t cache_capacity = 3;     // 0 to disable
static uint32_t max_cache_size = 1024;  // 0 to disable
static int cache_enabled = 1;           // default enabled

static uint32_t cached_files = 0;       // running total

// Determines which index of cache to override for storing.FIFO
#define CACHEINDX ((cached_files) % (cache_capacity))

// Returns 1 for 400 code
int isBadRequest(struct ClientRequest * rObj) {

  if (!(strncmp(rObj->version,"HTTP/1.1",8) == 0)) {
    return 1;
  }
  if (!(strncmp(rObj->resource, "//", 1) == 0)) {
    return 1;
  }
  memmove(rObj->resource, rObj->resource+1, strlen(rObj->resource));  
  
  if (strlen(rObj->resource) > 19) {
    return 1;
  }
  // Check characters valid
  for (size_t i = 0; i < strlen(rObj->resource); i++) {
    if ( !(isalnum(rObj->resource[i])) && (rObj->resource[i] != '_') && (rObj->resource[i] != '.')) {
      return 1;
    }
  }
  return 0;
}


// Returns -1 if header was not processed due to bad header request.
int ParseHeader(char * c_request, struct ClientRequest * rObj) {

  char * tok = strtok (c_request, "\r\n");
  sscanf(c_request, "%s %s %s", rObj->method, rObj->resource, rObj->version);
  c_request += strlen(tok) + strlen("\r\n");

  while (tok != NULL)
  {
    c_request += strlen(tok) + strlen("\r\n");
    int parse_status = ParseClientLine(tok, rObj);
    if (parse_status < 0) {
      return -1;
    }
    tok = strtok (c_request, "\r\n");  // get next line
  }
  return 1;
}


// Returns -1 for bad hostname
int ParseClientLine(char * line, struct ClientRequest * rObj) {
  int param_count = 0;
  int param_type = -1;
  char params[20][HEADER_SIZE];
  char * indx;

  // Lines expected
  enum {
    _HOST = 0,
    PARAMTOTAL,
  };
  const int type_count = PARAMTOTAL;

  // Map enum to string
  char* types[PARAMTOTAL] = {
    [_HOST] =     "Host:",
  };

  char * tok = NULL;
  tok = strtok_r (line, " ", &indx);

  while (tok != NULL) 
  {
    if (param_count == 0) {
      for (int i = 0; i < type_count; ++i) {
        if (strstr(tok, types[i])) {
          param_type = i;
          break;
        }
      }
    }
    else {
      strncpy(params[param_count - 1], tok, HEADER_SIZE);
    }

    param_count++;
    tok = strtok_r (NULL, " ", &indx);
  }
  // Unexpected line
  if (param_type == -1) {
    // fprintf(stderr, "Unknown req/param, ignoring line\n" );
  }

  switch(param_type) {
    case _HOST: {
      if (param_count > 2) { 
        return -1;
      }
      tok = strtok(params[0], ":"); 
      strcpy(rObj->hostname, tok); //localhost
      tok = strtok (NULL, " ");
      strcpy(rObj->hostvalue, tok); //8080
      break;
   }
   default: {
      break;
    }
  }
  return 1;
}

uint16_t strtouint16(char number[]) {
  char *last;
  long num = strtol(number, &last, 10);
  if (num <= 0 || num > UINT16_MAX || *last != '\0') {
    return 0;
  }
  return num;
}

int create_listen_socket(uint16_t port) {
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) { err(EXIT_FAILURE, "socket error"); }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htons(INADDR_ANY); 
  addr.sin_port = htons(port);
  if (bind(listenfd, (struct sockaddr*)&addr, sizeof addr) < 0) { err(EXIT_FAILURE, "bind error"); }
  if (listen(listenfd, 500) < 0) { err(EXIT_FAILURE, "listen error"); }

  return listenfd;
}

int create_client_socket(uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) { err(EXIT_FAILURE, "socket error"); } 

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (connect(fd, (struct sockaddr*) &addr, sizeof addr) < 0) {return -1;}

  return fd;
}


// Proxy Responds w/ 400 and 501.
void ProxyResponse(struct ClientRequest rObj) {

  char response[HEADER_SIZE];
  int slen = strlen(Status(rObj.status_code))+1;

  sprintf(response, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\n\r\n%s\n", 
    rObj.status_code, Status(rObj.status_code), slen, Status(rObj.status_code));
  
  send(rObj.client_socket, response, strlen(response), 0);
}


// Everyone is offline
void internalServerError(struct ClientRequest rObj) {
    rObj.status_code = 500;
    responses++;
    ProxyResponse(rObj);
}


void forwardResponse(int sourcefd, int destfd, char * resourcename, int server) {

  int serv_status = 0;
  uint32_t content_len = 0;
  char resourceName[PROCESS_BODY_SIZE];

  struct timeval tv2;
  int rd_indx = 0;
  int cached = 0;

  char* readbuff = (char *)calloc(PROCESS_BODY_SIZE, sizeof(char));
  if(!readbuff) { fprintf(stderr, "Bad malloc!"); return; }

  int msgs = 0;
  while(1)
  {
    tv2.tv_usec = 200;
    setsockopt(sourcefd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv2, sizeof tv2);

    int rdCount = read(sourcefd, readbuff, PROCESS_BODY_SIZE);
    if (rdCount <= 0) {
      break;
    }

    // Header
    if (msgs == 0) {
      sscanf(readbuff, "HTTP/1.1 %d %s\r\nContent-Length: %u\r\n", 
          &serv_status, resourceName, &content_len);

      char * tmp  = strstr(readbuff, "Last-Modified:");

      if (serv_status != 200) {
        server_errors[server]++;
      }
      server_entries[server]++;
      
      if ((cache_enabled) && (serv_status == 200) && (content_len <= max_cache_size)) {
        cached = 1;

        memset(cache_directory[CACHEINDX]->file_contents, 0, sizeof(char) * max_cache_size * HEADER_SIZE);
        memset(cache_directory[CACHEINDX]->file_name,     0, sizeof(char) * HEADER_SIZE);
        memset(cache_directory[CACHEINDX]->age_in_cache,  0, sizeof(char) * HEADER_SIZE);
        cache_directory[CACHEINDX]->file_size = 0;

        // Store Header
        memcpy(cache_directory[CACHEINDX]->file_contents + rd_indx, readbuff, rdCount * sizeof(char)); 
        memcpy(cache_directory[CACHEINDX]->file_name, resourcename, strlen(resourcename) * sizeof(char));
        memcpy(cache_directory[CACHEINDX]->age_in_cache, tmp, 40 * sizeof(char));
      }
    }
    // Body
    else {
      if ((cache_enabled) && (serv_status == 200) && (content_len <= max_cache_size)) {
        memcpy(cache_directory[CACHEINDX]->file_contents + rd_indx, readbuff, rdCount * sizeof(char)); 
      }
    }

    rd_indx += rdCount;
    write(destfd, readbuff, rdCount); // Sends data to destination
    msgs++;
  }

  if (cached) {
    cache_directory[CACHEINDX]->file_size = rd_indx;
    // Done storing the cache object
    printf("\nCache_directory[%d]\nNAME = %s\nSIZE = %d\nMODIFIED = %s\n", 
      CACHEINDX, cache_directory[CACHEINDX]->file_name, cache_directory[CACHEINDX]->file_size, cache_directory[CACHEINDX]->age_in_cache);
    
    cached_files++; // Increment for FIFO
  }

  free(readbuff);
  readbuff = NULL;
}


void relayRequesttoServer(char* header, char *resourcename, size_t len, int dst_fd, int src_fd, int server) {
  // send header req.
  int sent = send(dst_fd, header, len, 0);
  if (sent > 0)
  {
    forwardResponse(dst_fd, src_fd, resourcename, server);
  }
}


// Returns index of server to load balance on
// if all offline return -1
int loadBalance() {
  int chosen = -1;
  unsigned int lowestTotal = 1234567;  //some arbitrarily large number
  for (int i = 0 ; i < total_servers; i++) {
    //skip server set to offline
    if (server_status[i] == 0) {
      continue;
    }

    // Prioritize least requested server
    if (server_entries[i] < lowestTotal) {
      lowestTotal = server_entries[i];
      chosen = i;
    }

    // Break tie w/ lowest errors
    else if (server_entries[i] == lowestTotal) {
      if (server_errors[i] < server_errors[chosen]) {
        chosen = i;
      }
    }
  }

  if (chosen < 0) {
    printf("all servers offline\n");
  }
  return chosen;
}


// Querie all servers & Load balances the current server to use. 
void healthCheckServers() {

  char healthRequest[HEADER_SIZE];
  char buff[HEADER_SIZE];
  int serv_status;

  int i= 0;
  while(server_pool[i]) {
    // printf("       old %d %d\n", server_errors[i], server_entries[i]);
    i++;
  }

  for (int i = 0 ; i < total_servers; i++) {

    memset(healthRequest,0,sizeof (healthRequest));
    memset(buff, 0, sizeof (buff));
    char * healthStatus = (char *)calloc(PROCESS_BODY_SIZE, sizeof(char));

    sprintf(healthRequest, "GET /healthcheck HTTP/1.1\r\nHost: localhost:%d\r\n\r\n", server_pool[i]);

    int serverfd = create_client_socket(server_pool[i]);

    // set offline, skip
    if (serverfd < 0) {
      server_status[i] = 0;
      continue;
    }

    int s = send(serverfd, healthRequest, strlen(healthRequest), 0);
    if (s < 0) { continue;}

    int msgs = 0;
    char * pch;

    while(1)
    {
      int rdCount = read(serverfd, healthStatus, PROCESS_BODY_SIZE);
      if (( pch = strstr(healthStatus,"\n")) != NULL) {
        close(serverfd);
        break;
      }
      
      if (rdCount <= 0) {
        // printf("Exitting w/ condition rdCount= %d\n", rdCount);
        close(serverfd);
        break;
      }
      msgs++;
    }

    // Multiple messages
    if (msgs > 1) {
      sscanf(healthStatus, "%u\r\n%u\r\n%s %d",
          &server_errors[i], &server_entries[i], buff, &serv_status);
    }
    // One complete message
    else {
      sscanf(healthStatus, "HTTP/1.1 %d %s\r\nContent-Length: %s\r\nLast-Modified: %s %s %s %s %s %s\r\n\r\n%u\n%u\n", 
          &serv_status, buff, buff, buff, buff, buff, buff, buff, buff, &server_errors[i], &server_entries[i]);
    }

    free(healthStatus);
    healthStatus = NULL;

    if (serv_status != 200) {
      server_status[i] = 0;
      continue;
    } 
    
    else {// set online
      server_status[i] = 1;
    }

    // printf("[!healthck] %d <%u><%u>\n", server_pool[i], server_errors[i], server_entries[i]);
    close(serverfd);
  }
}


// return the diff of times from "last modified" for proxy file and server file
int headReq(char * proxyMod, char * resourcename, int server) {
  int ret = -1;

  struct tm tm1 = {0};
  struct tm tm2 = {0};

  char headRequest[HEADER_SIZE];
  sprintf(headRequest, "HEAD /%s HTTP/1.1\r\nHost: localhost:%d\r\n\r\n", resourcename, server_pool[server]);

  int serverfd = create_client_socket(server_pool[server]);
  if (serverfd < 0) { 
    server_status[server] = 0;
    return -1;
  }

  char* readbuff = (char *)calloc(PROCESS_BODY_SIZE, sizeof(char)); //read response from server HEAD
  char* servMod  = (char *)calloc(PROCESS_BODY_SIZE, sizeof(char));

  int s = send(serverfd, headRequest, strlen(headRequest), 0);
  if (s < 0) { return -1;}

  int r = read(serverfd, readbuff, PROCESS_BODY_SIZE);
  if (r < 0) { return -1;}
  close(serverfd);

  char * tmp  = strstr(readbuff, "Last-Modified:");
  memcpy(servMod, tmp, 40 * sizeof(char));

  strptime(proxyMod, "Last-Modified: %a, %d %b %Y %H:%M:%S", &tm1);
  strptime(servMod,  "Last-Modified: %a, %d %b %Y %H:%M:%S", &tm2);

  tm1.tm_isdst= -1;
  tm2.tm_isdst= -1;

  time_t t1 = mktime(&tm1);
  time_t t2 = mktime(&tm2);

  double diff = difftime(t2, t1);
  // printf("Same or na? %f\n", difftime(t1,t2));

  free(readbuff);
  free(servMod);
  readbuff = servMod = NULL;

  ret = (diff > 0) ? 1 : 0;
  return ret;

}


void ProcessClientRequest(char *c_request, int connfd) {
  int serverfd = -1;
  int skip = 0;

  struct ClientRequest rObj = {0};
  rObj.client_socket = connfd;
  strcpy(rObj.c_request, c_request);

  pthread_mutex_lock(&mtx);

  // HC
  if (responses % healthFrequency == 0) {
    healthCheckServers();
    currChosenServer = loadBalance();
  }
  // between HC
  else {
    currChosenServer = loadBalance();
    if (currChosenServer < 0) {
        skip = 1;
    }
    else {
      // alive between HC?
      while((!skip) && (serverfd = create_client_socket(server_pool[currChosenServer])) < 0) {
        server_status[currChosenServer] = 0; // port offline
        currChosenServer = loadBalance();    // Choose new online port

        // all dead
        if (currChosenServer < 0) {
          skip = 1;
        }
      }
      close(serverfd);
    }
  }
  // printf("Chosen Server = %d\n\n", server_pool[currChosenServer]);
  responses++;
  int server = currChosenServer;
  pthread_mutex_unlock(&mtx);

  int parse_status = ParseHeader(c_request, &rObj);
  if (parse_status < 0 || isBadRequest(&rObj)) {
    rObj.status_code = 400;
    ProxyResponse(rObj);
    return;
  }

  // All down / unresponsive
  else if (server < 0) {
      internalServerError(rObj);
    return;
  }

  // Fulfill GET Request
  else if (strncmp(rObj.method,"GET",3)==0) {

    if (cache_enabled) {
      for (unsigned int i = 0; i < cache_capacity; i++) {
        int len = strlen(rObj.resource);
        if (strncmp(rObj.resource, cache_directory[i]->file_name, len) == 0 ) {
          // ALSO Check modified dates
          if (headReq(cache_directory[i]->age_in_cache, rObj.resource, server) == 0) {
            printf("\n[CACHE HIT!] %s\n", rObj.resource);
            int w = write(rObj.client_socket, cache_directory[i]->file_contents, cache_directory[i]->file_size);
            if (w < 0) {
              internalServerError(rObj);
            }
            return;
          }
          //otherwise continue to cache an updated file... should we override the old files indexs? 
          // Multiple copy problem if not...
        }
      }
    }
    serverfd = create_client_socket(server_pool[server]);
    relayRequesttoServer(rObj.c_request, rObj.resource, strlen(rObj.c_request), serverfd, connfd, server);
    close(serverfd);
  }
  else {
    rObj.status_code = 501;
    ProxyResponse(rObj);
    return;
  }

}


// Receive client requests
void HandleConnection(int connfd) {
  char request[HEADER_SIZE];   
  while(1) 
  {
    memset(request,0,sizeof request);
    int rec = recv(connfd, request, HEADER_SIZE, 0); // Receive new request from client
    if (rec < 0) { warn("recv"); break; }            // Bad recv from client
    if (rec == 0){ break; }                          // Client exits connection
    ProcessClientRequest(request, connfd);
  }
  close(connfd);
}


// Assign jobs to worker threads
void * WorkerThread(void * arg) {
  threadProcess_t* ctx = (threadProcess_t*)arg;
  // printf("Worker %i started\n", ctx->tid);
  while (1)
  {
    int connfd_job = queue_pop();
    if (connfd_job != -1)
    {
      // printf("[Worker %d] Processing connfd_job %d\n", ctx->tid, connfd_job );
      HandleConnection(connfd_job);
      printf("[Worker %d] JOB DONE \n", ctx->tid);
    }
  }

  return NULL;
}


void MultiThreadingProcess(uint16_t threads, uint16_t server_port) {
  queue_init();

  thread_pool = (threadProcess_t **)malloc(sizeof(threadProcess_t*) * threads);

  for (int i = 0; i < threads; i++) {
    thread_pool[i] = (threadProcess_t *)malloc(sizeof(threadProcess_t));  // array of structs

    thread_pool[i]->tid   = i;
    thread_pool[i]->ptr   = (pthread_t *) malloc (sizeof(pthread_t));
    pthread_create( thread_pool[i]->ptr, NULL, &WorkerThread, (void*)thread_pool[i]);
  }
  
  int listenfd = create_listen_socket(server_port);

  while(1) 
  {
    int clientfd = accept(listenfd, NULL, NULL);
    if (clientfd < 0) { warn("accept error"); continue; }
    queue_push(clientfd);
  }

  // Clean up
  for (int i = 0; i < threads; i++) {
    pthread_join(*thread_pool[i]->ptr, NULL);
    free(thread_pool[i]->ptr);
    free(thread_pool[i]);
    thread_pool[i] = NULL;
  }
  
  queue_deinit();
  free(thread_pool);
  thread_pool = NULL;

  close(listenfd);
}


// Grab clients
int main(int argc, char *argv[]) {
  
  uint16_t server_port = 0;
  uint16_t proxy_port = 0;
  uint16_t set_Threads = 5;
  int opt;

  if (argc < 3) { errx(EXIT_FAILURE, "A proxy & server port number is required!"); }
  
  server_pool    = (uint16_t *) malloc ((argc-2) * sizeof (uint16_t));
  server_status  = (uint32_t *) malloc ((argc-2) * sizeof (uint32_t));
  server_errors  = (uint32_t *) malloc ((argc-2) * sizeof (uint32_t));
  server_entries = (uint32_t *) malloc ((argc-2) * sizeof (uint32_t));
  if (!server_pool || !server_status || !server_errors || !server_entries) {
    fprintf(stderr, "Bad malloc!"); return -1;
  }

  while (optind < argc)
  {
    if ((opt = getopt(argc, argv, GETOPTIONS)) != -1 ) {
      switch (opt) 
      {
        case 'N':
          set_Threads = atoi(optarg);
          break;
        case 'R':
          healthFrequency = atoi(optarg);
          break;
        case 's':
          cache_capacity = atoi(optarg);
          break;
        case 'm':
          max_cache_size = atoi(optarg);
          break;
      }
    }
    else {
      // Get proxy port first
      if (proxy_port == 0) {
        proxy_port = strtouint16(argv[optind++]);
      }
      else {
        server_port = strtouint16(argv[optind++]);
        server_pool   [total_servers] = server_port;
        server_status [total_servers] = 1;    // start online
        server_errors [total_servers] = 0;    // bad logs
        server_entries[total_servers] = 0;    // total logs
        total_servers++;
      }
    }
  }

  if (total_servers == 0) { errx(EXIT_FAILURE, "A server port number is required!"); }

  if (cache_capacity > 0 && max_cache_size > 0) {
    cache_directory = (cache_file **) malloc(sizeof(cache_file) * cache_capacity);  // alloc array of structs

    for (unsigned int i = 0; i < cache_capacity; i++) {
      cache_directory[i] = (cache_file *) malloc(sizeof(cache_file));  // alloc each struct

      cache_directory[i]->file_contents = (char *) calloc (max_cache_size * HEADER_SIZE, sizeof(char));
      cache_directory[i]->file_name     = (char *) calloc (HEADER_SIZE, sizeof(char));
      cache_directory[i]->age_in_cache  = (char *) calloc (HEADER_SIZE, sizeof(char));

      cache_directory[i]->file_size     = -1;
    }
  }
  // If both variables are 0, disable
  else {
    cache_enabled = 0;
  }

  MultiThreadingProcess(set_Threads, proxy_port);

//=========================================================================================
// CLEAN UP
//=========================================================================================

  free(server_errors);
  free(server_status);
  free(server_entries);
  free(server_pool);

  for (unsigned int i = 0; i < cache_capacity; i++) {
    free(cache_directory[i]->file_contents);
    free(cache_directory[i]);
    cache_directory[i] = NULL;
  }

  free(cache_directory);
  cache_directory = NULL;

  server_errors = server_status = server_entries = NULL;
  server_pool = NULL;

  return EXIT_SUCCESS;
}
