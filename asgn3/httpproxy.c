// Despina Patronas CSE130 Fall 2021 asgn3

#include <stdlib.h>
#include <stdint.h>
#include <string.h>     // strtok(), strstr(), strncpy(), strlen()
#include <stdio.h>      // fprintf()
#include <err.h>        // warn()
#include <errno.h>      // check warn codes
#include <fcntl.h>      // open()
#include <unistd.h>     // close(), write()
#include <ctype.h>      // isalnum()
#include <sys/stat.h>   // stat()
#include <sys/file.h>   // flock()
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

#define GETOPTIONS "N:R:s:m:"  // optional CMD line args
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

extern int errno;
struct stat st;

static int responses = 0;           // # of responses fulfilled guarded
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;  //guard the shared responses variable

static uint16_t * server_pool;      // holds server ports
static uint32_t * server_status;    // 0 == offline for balancing
static uint32_t * server_errors;    // bad logs
static uint32_t * server_entries;   // total logs

static int total_servers = 0;             // # of server
static int currentChosenServer = 0;       // Server index to port forward

static uint32_t cache_capacity = 3;   // 0 means no caching
static uint32_t max_file_size = 1024; // 0 means no caching
static int cache_enabled = 1;         // default. cache is enabled

static int healthFrequency = 5;     // # of times to healthcheck servers

// Returns 1 if request field is bad
int isBadRequest(struct ClientRequest * rObj) {

  // Check version matches protocol
  if (!(strncmp(rObj->version,"HTTP/1.1",8) == 0)) {
    return 1;
  }
  // Check file name begins with backslash
  if (!(strncmp(rObj->resource, "//", 1) == 0)) {
    return 1;
  }
  // Remove file name backslash
  memmove(rObj->resource, rObj->resource+1, strlen(rObj->resource));  
  
  // Check file length
  if (strlen(rObj->resource) > 19) {  
    return 1;
  }
  // Check file name characters is valid
  for (size_t i = 0; i < strlen(rObj->resource); i++) {
    if ( !(isalnum(rObj->resource[i])) && (rObj->resource[i] != '_') && (rObj->resource[i] != '.')) {
      return 1;
    }
  }
  return 0;
}


// Parse request header (line by line)
// Returns -1 if header was not processed due to bad header request.
int ParseClientHeader(char * c_request, struct ClientRequest * rObj) {

  char * tok = strtok (c_request, "\r\n");
  while (tok != NULL)
  {
    c_request += strlen(tok) + strlen("\r\n");      // manually set index
    int parse_status = ParseClientLine(tok, rObj);  // return status of parseLine
    if (parse_status < 0) {
      return -1;
    }
    tok = strtok (c_request, "\r\n");  // get next line
  }
  return 1;
}


// Parse line of request header (from Client)
//    - Populate the message object fields
//    - returns -1 if header fields not accepted.
int ParseClientLine(char * line, struct ClientRequest * rObj) {
  int param_count = 0;
  int param_type = -1;
  char params[20][HEADER_SIZE];
  char * indx;

  // Lines expected
  enum {
    _GET = 0,
    _HOST, 

    PARAMTOTAL,
  };
  const int type_count = PARAMTOTAL;

  // Map enum to string
  char* types[PARAMTOTAL] = {
    [_GET] =            "GET",
    [_HOST] =           "Host:",
  };

  char * tok = NULL;
  tok = strtok_r (line, " ", &indx); // gets first parameter of line

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
    case _GET: {  // todo error check that param_count is within range of expectation..
      rObj->method = _GET_;
      strcpy(rObj->resource, params[0]); 
      strcpy(rObj->version, params[1]);
      break;
    }
   case _HOST: {
      //Additional parameter(s) than expected indicate spaces on hostvalue
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


// Converts a string to an 16 bits unsigned integer.
uint16_t strtouint16(char number[]) {
  char *last;
  long num = strtol(number, &last, 10);
  if (num <= 0 || num > UINT16_MAX || *last != '\0') {
    return 0;
  }
  return num;
}


// Proxy Server to listen for Client connections. Creates socket, returns fd of client
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

// Proxy Server to connect to other Servers. Creates socket, returns fd of Server
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


// Proxy Responds to Client for bad status codes 400 and 501.
// If Proxy has most recent file in cache, it will respond with 200
// Note: All other responses to client are done via server forwarding
void ProxyResponse(struct ClientRequest rObj) {

  char response[HEADER_SIZE];

  // Bad request.
  int slen = strlen(Status(rObj.status_code))+1;

  sprintf(response, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\n\r\n%s\n", 
    rObj.status_code, Status(rObj.status_code), slen, Status(rObj.status_code));
  
  send(rObj.client_socket, response, strlen(response), 0);
}



// Process server response to client
void forwardResponse(int infile, int outfile) {
  // char header[PROCESS_BODY_SIZE];
  int serv_status;
  char resourceName[PROCESS_BODY_SIZE];
  int len;
  char buff[PROCESS_BODY_SIZE];


  struct timeval tv;

  char* readbuff = (char *)calloc(PROCESS_BODY_SIZE, sizeof(char));
  if(!readbuff) { fprintf(stderr, "Bad malloc!"); return; }

  int msgs = 0;
  while(1)
  {
    tv.tv_usec = 200;
    setsockopt(infile, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    int rdCount = read(infile, readbuff, PROCESS_BODY_SIZE);
    if (rdCount <= 0) {
      break;
    }
    // grab the header of the server response to update load balance values
    if (msgs == 0) {
      sscanf(readbuff, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nLast-Modified: %s %s %s %s %s %s\r\n\r\n", 
          &serv_status, resourceName, &len, buff, buff, buff, buff, buff, buff);

      if (serv_status != 200) {
        server_errors[currentChosenServer]++;
      }
      server_entries[currentChosenServer]++;
      
      // strcpy(header, readbuff);
      // printf("header %s\n ", header);
      printf("[!process] %d <%u><%u>\n", server_pool[currentChosenServer], server_errors[currentChosenServer], server_entries[currentChosenServer]);
    }

    write(outfile, readbuff, rdCount);
    msgs++;
  }
  free(readbuff);
  readbuff = NULL;
}


// Forward Server response to client
void relayRequesttoServer(char* header, size_t len, int dst_fd, int src_fd) {
  int ret = send(dst_fd, header, len, 0);
  if (ret > 0)
  {
    forwardResponse(dst_fd, src_fd);
  }
}


// returns the index of the server to load balance on. IE: server_pool[chosen]
int loadBalance() {
  int chosen = -1;
  unsigned int lowestTotal = 5000;

  for (int i = 0 ; i < total_servers; i++) {
    // Response code ! 200 == skip server set to offline
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
  printf("balancing = %d\n\n", server_pool[chosen]);
  return chosen;
}


// Querie all servers
// Load balances the current server to use. Returns that index.
int healthCheckServers() {

  // Timeout
  struct timeval tv;

  // int chosen = -1;
  char healthRequest[HEADER_SIZE];
  char buff[HEADER_SIZE];
  int serv_status;

  int i= 0;
  while(server_pool[i]) {
    printf("       old %d %d\n", server_errors[i], server_entries[i]);
    i++;
  }

  for (int i = 0 ; i < total_servers; i++) {

    memset(healthRequest,0,sizeof (healthRequest));
    memset(buff, 0, sizeof (buff));

    sprintf(healthRequest, "GET /healthcheck HTTP/1.1\r\nHost: localhost:%d\r\n\r\n", server_pool[i]);

    int serverfd = create_client_socket(server_pool[i]);

    // set offline, skip
    if (serverfd < 0) {
      server_status[i] = 0;
      continue;
    }

    // Request health check from the current server
    int s = send(serverfd, healthRequest, strlen(healthRequest), 0);
    if (s < 0) { continue;}
    
    char * healthStatus = (char *)calloc(PROCESS_BODY_SIZE, sizeof(char));

    // muliple recvs use timeout to end recv loop
    int msgs = 0;
    while(1)
    {
      tv.tv_usec = 100;
      setsockopt(serverfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
      int rdCount = read(serverfd, healthStatus, PROCESS_BODY_SIZE);
      
      if (rdCount <= 0) {
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

    //set offline
    if (serv_status != 200) {
      server_status[i] = 0;
      continue;
    } // set online
    else {
      server_status[i] = 1;
    }

    printf("[!healthck] %d <%u><%u>\n", server_pool[i], server_errors[i], server_entries[i]);
    close(serverfd);
  }
  // otherwise load balance based on new healthcheck logs
  return loadBalance();
}


void ProcessClientRequest(char *c_request, int connfd) {
  int serverfd = -1;
  int failed_servers = 0;

  struct ClientRequest rObj = {0}; //reset object
  rObj.client_socket = connfd;
  strcpy(rObj.c_request, c_request);

  pthread_mutex_lock(&mtx);
  // Initiate Healthcheck
  if (responses % healthFrequency == 0) {
    currentChosenServer = healthCheckServers();

    if (currentChosenServer < 0) {
      rObj.status_code = 500;
      ProxyResponse(rObj);
      return;
     }
  }
  // load balance between healthchecks
  else {
    currentChosenServer = loadBalance();
    if (currentChosenServer < 0) {
        rObj.status_code = 500;
        ProxyResponse(rObj);
        return;
    }
    // Determine if the server is alive between healthchecks
    // WHAT IF THE SERVERFD !<0 ? opening a port..
    while((serverfd = create_client_socket(server_pool[currentChosenServer])) < 0) {
      // port fails to create. Set it to offline.
      server_status[currentChosenServer] = 0;
      failed_servers++;
      // Choose a new server from online list
      currentChosenServer = loadBalance();

      if (failed_servers == total_servers) {
        rObj.status_code = 500;
        ProxyResponse(rObj);
      }
    }
    // close the server that is online that was openened
    close(serverfd);
  }

  printf("chosen server port = %d\n\n", server_pool[currentChosenServer]);
  responses++;
  int server_port = server_pool[currentChosenServer];
  pthread_mutex_unlock(&mtx);

  int parse_status = ParseClientHeader(c_request, &rObj);

  if (parse_status < 0 || isBadRequest(&rObj)) {
    rObj.status_code = 400;
    ProxyResponse(rObj);
    return;
  }

  // All servers are down / unresponsive
  if (server_port < 0) {
    rObj.status_code = 500;
    ProxyResponse(rObj);
    return;
  }

  // Fulfill GET Request
  if ((rObj.method == _GET_)) {
    serverfd = create_client_socket(server_pool[currentChosenServer]);
    relayRequesttoServer(rObj.c_request, strlen(rObj.c_request), serverfd, connfd);
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
  // Initialize Job Queue
  queue_init();

  // Allocate thread pool
  thread_pool = (threadProcess_t **)malloc(sizeof(threadProcess_t*) * threads);

  // Start workers..
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
    // printf("\n [!] Accepted connection %d, pushing to queue\n", clientfd);
    queue_push(clientfd);
  }

    // Release thread pool
  for (int i = 0; i < threads; i++) {
    pthread_join(*thread_pool[i]->ptr, NULL); // Threads finish work
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
  
  // Get non-option server ports
  server_pool    = (uint16_t *) malloc ((argc-2) * sizeof (uint16_t));
  server_status  = (uint32_t *) malloc ((argc-2) * sizeof (uint32_t));
  server_errors  = (uint32_t *) malloc ((argc-2) * sizeof (uint32_t));
  server_entries = (uint32_t *) malloc ((argc-2) * sizeof (uint32_t));
  if (!server_pool || !server_status || !server_errors || !server_entries) {
    fprintf(stderr, "Bad malloc!"); return -1;
  }

  // Get opts
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
          max_file_size = atoi(optarg);
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
        server_status [total_servers]  = 1;  // start online
        server_errors [total_servers] = 0;  // bad logs
        server_entries[total_servers] = 0;  // total logs
        total_servers++;
      }
    }
  }

  // Fail if no server port is provided
  if (total_servers == 0) { errx(EXIT_FAILURE, "A server port number is required!"); }

  // create cache memory
  if (cache_capacity > 0 && max_file_size > 0) {
    cache_directory = (cache_file **) malloc(sizeof(cache_file) * cache_capacity);  // caches struct array

    for (unsigned int i = 0; i < cache_capacity; i++) {
      cache_directory[i] = (cache_file *) malloc(sizeof(cache_file));  // caches struct array

      cache_directory[i]->file_contents = (char *) calloc (max_file_size, sizeof(char));
      cache_directory[i]->file_size     = -1;
    }
  }
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
