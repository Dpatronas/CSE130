// Despina Patronas CSE130 Fall 2021 asgn2

// Sources:
// Hex Conversion: https://www.codezclub.com/c-convert-string-hexadecimal/

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

#define HEADER_SIZE       1024
#define PROCESS_BODY_SIZE 4096
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define GETOPTIONS "N:l:"  // optional CMD line args. If used, requires arguments

pthread_mutex_t lock_file = PTHREAD_MUTEX_INITIALIZER; //lock the flock

static int log_report_fd;
static int failed_jobs;

extern int errno;

typedef struct {
  int tid;         // index of thread in thread pool
  pthread_t * ptr; // thread itself
} threadProcess_t;

static threadProcess_t ** thread_pool;  // ptr to struct thread pool

enum {
  THREAD_INITIALZED = 0,
  THREAD_READY,
  THREAD_BUSY,

  THREAD_TOT,
};

// Commands server supports
typedef enum method {
  _GET_ = 0,
  _PUT_,
  _HEAD_,
} method;

// Message object
struct ClientRequest {
  int socket;           // Client connection fd
  int method;
  char resource [300];  // ex: /cse130
  char version [300];   // ex: HTTP/1.1
  char hostname [300];  // ex: localhost
  char hostvalue [300]; // ex: 8080
  unsigned int len;     // Length of resource contents

  int status_code;      // Code for logging
  char hex [2001];      // If logging, contains <= 1000 bytes from processing

} ClientRequest;

// Codes server supports
const char* Status(int code) {
  switch(code) {
    case 200: return "OK";                    // Response is successful ie: nothing else broke
    case 201: return "Created";               // Successful PUT resource
    case 400: return "Bad Request";           // Request is not valid ie: not parsable
    case 403: return "Forbidden";             // Cannot access valid resource file (for GET)
    case 404: return "File Not Found";        // Valid resource name but server cannot find file
    case 500: return "Internal Server Error"; // Request is valid but cant allocate memory to process
    case 501: return "Not Implemented";       // Request is valid is ok BUT command not valid
  }
  return NULL;
}


// Converts a string to an 16 bits unsigned integer.
// Returns 0 if the string is malformed or out of the range.
uint16_t strtouint16(char number[]) {
  char *last;
  long num = strtol(number, &last, 10);
  if (num <= 0 || num > UINT16_MAX || *last != '\0') {
    return 0;
  }
  return num;
}



// Creates a socket for listening for connections.
// Closes the program and prints an error message on error.
int create_listen_socket(uint16_t port) {
  struct sockaddr_in addr;
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) { err(EXIT_FAILURE, "socket error"); }

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htons(INADDR_ANY); 
  addr.sin_port = htons(port);
  if (bind(listenfd, (struct sockaddr*)&addr, sizeof addr) < 0) { err(EXIT_FAILURE, "bind error"); }
  if (listen(listenfd, 500) < 0) { err(EXIT_FAILURE, "listen error"); }

  return listenfd;
}



// Log request into the report
void ReportLog(struct ClientRequest * rObj) {

  // wait signal
  char log[PROCESS_BODY_SIZE];

  //define string for methods
  char* types[3] = {  
    types[_GET_] =  "GET", types[_PUT_] =  "PUT", types[_HEAD_] = "HEAD",
  };

  // FAIL
  if ((rObj->status_code != 200) && (rObj->status_code != 201)) {
    sprintf(log, "FAIL\t%s /%s %s\t404\n", types[rObj->method], rObj->resource, rObj->version);
    write(log_report_fd, log, strlen(log));
    failed_jobs++;
    return;
  }

  if (rObj->method == _GET_|| rObj->method == _PUT_) {
    // Show hexa representation of first 1k bytes processed...
    sprintf(log, "%s\t/%s\t%s:%s\t%d\t%s\n", 
      types[rObj->method], rObj->resource, rObj->hostname, rObj->hostvalue, rObj->len, rObj->hex);
  }

  // HEAD
  else if (rObj->method == _HEAD_) {
    sprintf(log, "%s\t/%s\t%s:%s\t%d\n", 
      types[rObj->method], rObj->resource, rObj->hostname, rObj->hostvalue, rObj->len);
  }

  write(log_report_fd, log, strlen(log));

}



// Send client response for request
void ServerResponse(int code, struct ClientRequest rObj) {

  char response[HEADER_SIZE];

  if (rObj.method == _PUT_) {
    int slen = strlen(Status(code))+1;

    sprintf(response, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\n\r\n%s\n", 
      code, Status(code), slen, Status(code));
    send(rObj.socket, response, strlen(response), 0);
  }

  else { // GET
    sprintf(response, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\n\r\n", 
      code, Status(code), rObj.len);

    send(rObj.socket, response, strlen(response), 0);
  }
}



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



// Put the hex represenation into PUT / GET message
int setHex(struct ClientRequest * rObj, char * readbuff) {

  unsigned int logbytes = MIN(strlen(readbuff), 1000); //read 1000 or less

  unsigned int i = 0; 
  unsigned int j = 0;

  for(; i < logbytes; i++, j += 2) {
    sprintf((char*)rObj->hex + j,"%02X" ,readbuff[i]);  // adds '\0'
  }

  for (i = 0; i < strlen(rObj->hex); i++) {
    rObj->hex[i] = tolower(rObj->hex[i]);
  }

  rObj->hex[logbytes*2+1]='\0'; //adding NULL in the end
  return 1;
}



// Process body for PUT and GET requests
// NOTE: GET sends response before PUT
void processBody(int infile, unsigned int len, int outfile, struct ClientRequest * rObj) {
  unsigned int tot_read = 0;
  int gotLogHex = 0;

  char* readbuff = (char *)calloc(PROCESS_BODY_SIZE, sizeof(char));
  if(!readbuff) { fprintf(stderr, "Bad malloc!"); return; }

  while(1)
  {
    int remainingBytes = len - tot_read;
    int RDSIZE = MIN(PROCESS_BODY_SIZE, remainingBytes);  // dont read over required length

    int rdCount = read(infile, readbuff, RDSIZE);
    tot_read += rdCount;

    write(outfile, readbuff, rdCount);

    // grab hex for log
    if ((log_report_fd > 0) && (gotLogHex == 0)) {
      setHex(rObj, readbuff);
      gotLogHex = 1;
    }

    if(tot_read >= len) {
      break;
    }
  }
  pthread_mutex_unlock(&lock_file);

  // Active log, log GET / PUT
  if (log_report_fd > 0) {
    ReportLog(rObj);
  }

  free(readbuff);
}



// Parses Put request pre operation
//    - Checks whether to create or truncate file requested
int ParsePut(struct ClientRequest * rObj) {

  int code = 0;
  if (access(rObj->resource, F_OK) == 0)
    rObj->status_code = code = 200;  // truncate code OK

  else
    rObj->status_code = code = 201;  // create code CREATED

  int outfile = open(rObj->resource, O_RDWR | O_CREAT | O_TRUNC, 0622); 
  if (outfile < 0) { 
    warn("creating outfile error"); 
    rObj->status_code = 500;

    if (log_report_fd > 0) {
      ReportLog(rObj);
    }
    return 500; // making resource failed.. error occured..
  }

  // lock the resource file
  pthread_mutex_lock(&lock_file);
  flock(outfile, LOCK_EX);
  
  // Empty header body -> nothing to process
  if (rObj->len == 0) {
    close(outfile);
    return code;
  }

  // Write bytes from socket body into file resource
  processBody(rObj->socket, rObj->len, outfile, rObj);
  close(outfile);
  return code;
}



// Parses GET or HEAD pre operation
//    - HEAD does not process any bytes.
//    - GET  checks whether file resources exists and/or is accessible.
void ParseGetHead(struct ClientRequest * rObj) {
  int infile;
  // Attempt to open file requested
  if ((infile = open(rObj->resource, O_RDONLY, 0)) < 0) {
    warn("cannot open '%s' due to ernno: %d", rObj->resource, errno);

    if (errno == 2) {
      rObj->status_code = 404;
      ServerResponse(404, *rObj); // DNE
    }
    else {
      rObj->status_code = 403;
      ServerResponse(403, *rObj); // Forbidden
    }
    // Log is active LOG failure
    if (log_report_fd > 0) {
      ReportLog(rObj);
    }
    return;
  }

  // lock the resource file
  pthread_mutex_lock(&lock_file);
  flock(infile, LOCK_EX);

  struct stat st;
  stat(rObj->resource, &st);
  rObj->len = st.st_size;

  // Otherwise, send OK response
  rObj->status_code = 200;
  ServerResponse(200, *rObj);

  // Process GET
  if (rObj->method == _GET_) {
    processBody(infile, rObj->len, rObj->socket, rObj);
  }

  else {
    // Log is active LOG HEAD
    if (log_report_fd > 0) {
      ReportLog(rObj);
    }
  }

  close(infile);
}



// Parse line of request header
//    - Populate the message object fields
//    - returns -1 if header fields not accepted.
//    - ignored extraneous lines..
int ParseLine(char * line, struct ClientRequest * rObj) {
  int param_count = 0;
  int param_type = -1;

  char params[20][HEADER_SIZE];
  // Lines expected
  enum {
    _GET = 0, _PUT, _HEAD, _HOST, _USER_AGENT, _ACCEPT, _CONTENT_LENGTH, _EXPECT,

    PARAMTOTAL,
  };
  const int type_count = PARAMTOTAL;

  // Map enum to string
  char* types[PARAMTOTAL] = {
    [_GET] =            "GET",
    [_PUT] =            "PUT",
    [_HEAD] =           "HEAD",
    [_HOST] =           "Host:",
    [_USER_AGENT] =     "User-Agent:",
    [_ACCEPT] =         "Accept:",
    [_CONTENT_LENGTH] = "Content-Length:",
    [_EXPECT] =         "Expect:",
  };

  char * tok = NULL;
  tok = strtok (line, " "); // gets first parameter of line

  while (tok != NULL) 
  {
    // printf("[DBG] %s \n", tok);
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
    tok = strtok (NULL, " ");
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
   case _PUT: {
      rObj->method = _PUT_;
      strcpy(rObj->resource, params[0]); 
      strcpy(rObj->version, params[1]);
      break;
   }
  case _HEAD: {
      rObj->method = _HEAD_;
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

   case _USER_AGENT: { break; }
   case _ACCEPT: { break; }

   // Assume length is a valid number generated by client curl
   case _CONTENT_LENGTH: {
      rObj->len = atoi(params[0]);     
      break;
   }

   case _EXPECT: { break; }

   default: {
      // fprintf(stderr, "Unknown param type: %i\n", param_type);
      break;
    }
  }
  return 1;
}



// Parse request header (line by line)
// Returns -1 if header was not processed due to bad header request.
int ParseHeader(char * request, struct ClientRequest * rObj) {
 char * tok = strtok (request, "\r\n");
 while (tok != NULL)
 {
   request += strlen(tok) + strlen("\r\n"); // manually set index
   // printf("[DBG] Line: %s \n", tok);
   int parse_status = ParseLine(tok, rObj);      // return status of parseLine
   if (parse_status < 0) {
      return -1;
   }
   tok = strtok (request, "\r\n");  // get next line
 }
 return 1;
}



// Deconstruct request by parts
//    - parse header
//    - check bad request line fields are satisfactory
//    - attempt to fulfill request HEAD, GET, PUT
void ProcessRequest(char *request, int connfd) {

  // printf("[DBG] %s\n", request);

  struct ClientRequest rObj = {0}; //reset object
  rObj.socket = connfd;
  rObj.status_code = 000;

  int parse_status = ParseHeader(request, &rObj);

  if (parse_status < 0 || isBadRequest(&rObj)) {
    rObj.status_code = 400;
    ServerResponse(400, rObj);
    return;
  }

  // Check commands
  if ((rObj.method == _GET_) || (rObj.method == _HEAD_)) {
    ParseGetHead(&rObj);
    return;
  }

  else if (rObj.method == _PUT_) {
    int putCode = ParsePut(&rObj);
    ServerResponse(putCode, rObj);
    return;
  }

  else {
    rObj.status_code = 501;
    ServerResponse(501, rObj);
    return;
  }
}



int validateLog(int logfd, char * log_name) {
  int log_len = 0;
  int tabs = 0;
  int tot_log_read = 0;

  struct stat st;
  stat(log_name, &st);
  log_len = st.st_size;
  if (log_len == 0) {
    return 1;
  }

  char* readbuff = (char *)calloc(PROCESS_BODY_SIZE, sizeof(char));
  if(!readbuff) { fprintf(stderr, "Bad malloc!"); return -1; }

  do 
  {
    int rdCount = read(logfd, readbuff, PROCESS_BODY_SIZE);
    tot_log_read += rdCount;
  }
  while(tot_log_read < log_len);

  // validate log
  for (unsigned int i = 0; i < strlen(readbuff); i++) {
    if (readbuff[i] == '\t') {
      tabs++;
    }
    if (readbuff[i] == '\n') {
      if (tabs < 2 || tabs > 4) {
        return -1;
      }
    }
    tabs = 0;
  }
  return 1;
  free(readbuff);
}



// // Listens for client connections dispatches jobs for worker threads
// void * listenerThreadWorkLoop (void * arg) {
//   uint16_t port = *(uint16_t *)arg;
//   int listenfd = create_listen_socket(port);

//   while(1) 
//   {
//     int connfd = accept(listenfd, NULL, NULL);
//     if (connfd < 0) { warn("accept error"); continue; }
//     printf("\n [!] Accepted connection %d, pushing to queue\n", connfd);
//     queue_push(connfd);
//   }
// }



// Receive client requests
void HandleConnection(int connfd) {
  char request[HEADER_SIZE];   
  while(1) 
  {
    memset(request,0,sizeof request);
    int rec = recv(connfd, request, HEADER_SIZE, 0); // Receive new request from client
    if (rec < 0) { warn("recv"); break; }            // Bad recv from client
    if (rec == 0){ break; }                          // Client exits connection
    ProcessRequest(request, connfd);
  }
  close(connfd);
}



// worker thread takes in thread 
void * workerThread(void * arg) {
  threadProcess_t* ctx = (threadProcess_t*)arg;

  printf("Worker %i started\n", ctx->tid);

  while (1)
  {
    int connfd_job = queue_pop();
    if (connfd_job != -1)
    {
      printf("[Worker %d] Processing connfd_job %d\n", ctx->tid, connfd_job );
      HandleConnection(connfd_job);
      printf("[Worker %d] JOB DONE \n", ctx->tid);
    }
  }

  return NULL;
}




// Grab clients
int main(int argc, char *argv[]) {

  uint16_t port; 
  int opt;
  log_report_fd = 0;   // default logfile DNE
  int threads = 5;     // default threads

  // Check port number exists && valid
  if (argc < 2) { errx(EXIT_FAILURE, "A port number is required!"); }

  // Get opts
  while (optind < argc)
  {
    if ((opt = getopt(argc, argv, GETOPTIONS)) != -1 ) {
      switch (opt) 
      {
        case 'N':
          threads = atoi(optarg);
          break;
        
        case 'l':
          log_report_fd = open(optarg, O_RDWR | O_CREAT | O_APPEND, 0644);
          // exit if logfile is bad
          if (!validateLog(log_report_fd, optarg)) {
            errx(EXIT_FAILURE, "bad log!: %s", argv[1]); } 

          break;
      }
    }
    else {
      port = strtouint16(argv[optind]);
      if (port <= 0) { errx(EXIT_FAILURE, "invalid port number: %s", argv[1]); } 
      optind++;
    }
  }

  // printf ("threads = %d, log = %d, port = %d \n", threads, log_report_fd, port);

  // Initialize queue
  queue_init();

  // Allocate thread pool
  thread_pool = (threadProcess_t **)malloc(sizeof(threadProcess_t*) * threads);

  // Start workers..
  for (int i = 0; i < threads; i++) {
    thread_pool[i] = (threadProcess_t *)malloc(sizeof(threadProcess_t));  // array of structs

    thread_pool[i]->tid   = i;
    thread_pool[i]->ptr   = (pthread_t *) malloc (sizeof(pthread_t));
    pthread_create( thread_pool[i]->ptr, NULL, &workerThread, (void*)thread_pool[i]);
  }

  // Start listener thread
  // pthread_t listenerThread;
  // pthread_create(&listenerThread, NULL, &listenerThreadWorkLoop, (void*)&port);
  
  int listenfd = create_listen_socket(port);

  while(1) 
  {
    int connfd = accept(listenfd, NULL, NULL);
    if (connfd < 0) { warn("accept error"); continue; }
    // printf("\n [!] Accepted connection %d, pushing to queue\n", connfd);
    queue_push(connfd);
  }

  // Release thread pool
  for (int i = 0; i < threads; i++) {
    pthread_join(*thread_pool[i]->ptr, NULL); // Threads finish work
    free(thread_pool[i]->ptr);
    free(thread_pool[i]);
  }

  free(thread_pool);
  // pthread_join(listenerThread, NULL);

  return EXIT_SUCCESS;
}
