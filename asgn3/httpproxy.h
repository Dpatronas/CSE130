// Despina Patronas CSE130 Fall 2021 asgn3
// Contains definitions of functions and structures

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define HEADER_SIZE       1024
#define PROCESS_BODY_SIZE 4096

//=======================================================================================
// CLIENT REQUESTS TO PROXY
//=======================================================================================

// Commands server supports
typedef enum method {
  _GET_ = 1,
  // _PUT_,
  // _HEAD_,
} method;

// Message object
struct ClientRequest {
  char c_request[PROCESS_BODY_SIZE];     // Entire Client Request

  int status_code;      // Code for Response
  int client_socket;    // Client connection fd

  char method [300];    // Only GET requests will be acknowledged
  char resource [300];
  char version [300];   // ex: HTTP/1.1
  char hostname [300];  // ex: localhost
  char hostvalue [300]; // ex: 8080
  unsigned int len;     // Length of resource contents

} ClientRequest;


//=====================================================================================
// CACHEING 
// =====================================================================================

typedef struct {
  char * age_in_cache;      // the last-modified age is upon entering cache
  char * file_name;         // the resource file
  char * file_contents;     // includes bytes of the header + body
  int    file_size;         // total size the cached header + body
} cache_file;

static cache_file ** cache_directory;  // ptr to cached files


//=======================================================================================
// PROXY SUPPORTED CODES
//=======================================================================================

const char* Status(int code) {
  switch(code) {
    // case 200: return "OK";                    // Response is successful ie: nothing else broke
    case 400: return "Bad Request";           // Request is not valid ie: not parsable
    case 500: return "Internal Server Error"; // Request is valid but cant allocate memory to process
    case 501: return "Not Implemented";       // Request is valid is ok BUT command not valid
  }
  return NULL;
}


//=======================================================================================
// THREADS
//=======================================================================================

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

//=====================================================================================
// FUNCTION DEFINITIONS
//=====================================================================================

uint16_t strtouint16(char number[]);

// Proxy Server to listen for Client connections. Creates socket, returns fd of client
int create_listen_socket(uint16_t port);

// Proxy Server to connect to other Servers. Creates socket, returns fd of Server
int create_client_socket(uint16_t port);

// Deconstruct request of the client. Return 400 or 501 from proxy, if invalid.
void ProcessClientRequest(char *request, int connfd);

// Fulfill client request job
void HandleConnection(int connfd);

//  Worker thread grabs a job. Worker thread takes in thread id.
void * WorkerThread(void * arg);

// Start up worker threads and client queue. Look for new clients to push into queue.
void MultiThreadingProcess(uint16_t threads, uint16_t server_port);

// Responds to Client Request
void ProxyResponse(struct ClientRequest rObj);

// Parse request header (line by line)
// Returns -1 if header was not processed due to bad header request.
int ParseClientHeader(char * c_request, struct ClientRequest * rObj);

// Parses each line into fields to determine validity of client request
int ParseClientLine(char * line, struct ClientRequest * rObj);

// Returns 1 if client request field is bad
int isBadRequest(struct ClientRequest * rObj);

//=====================================================================================
