// Despina Patronas CSE130 Fall 2021

#include <stdlib.h>
#include <stdint.h>
#include <string.h>     // strtok(), strstr(), strncpy(), strlen()
#include <stdio.h>      // fprintf(), scanf()
#include <err.h>        // warn()
#include <errno.h>      // check warn codes
#include <fcntl.h>      // open()
#include <unistd.h>     // close()
#include <ctype.h>      // isalnum()
#include <sys/stat.h>   // stat()

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#define HEADER_SIZE       1024
#define PROCESS_BODY_SIZE 4096
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

extern int errno;

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
  char resource [300];
  char hostname [300];  // Host version
  char hostvalue [300];
  unsigned int len;     // Length of resource

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
  if (!(strncmp(rObj->hostname,"HTTP/1.1",8) == 0)) {
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



// Process body for PUT and GET requests
void processBody(int infile, unsigned int len, int outfile) {
  int tot_read = 0;

  char* readbuff = (char *)malloc(PROCESS_BODY_SIZE);
  if(!readbuff) { fprintf(stderr, "Bad malloc!"); return; }

 while(1)
 {
    int remainingBytes = len - tot_read;
    int RDSIZE = MIN(PROCESS_BODY_SIZE, remainingBytes);  // dont read over required length

    int rdCount = read(infile, readbuff, PROCESS_BODY_SIZE);

    tot_read += rdCount;

    write(outfile, readbuff, rdCount);

    if(tot_read >= len) {
      break;
    }
  }
  
  free(readbuff);
}



// Parses Put request pre operation
//    - Checks whether to create or truncate file requested
int ParsePut(struct ClientRequest * rObj) {

  int code = 0;
  if (access(rObj->resource, F_OK) == 0)
    code = 200;  // truncate code OK

  else
    code = 201;  // create code CREATED

  int outfile = open(rObj->resource, O_RDWR | O_CREAT | O_TRUNC, 0622); 
  if (outfile < 0) { 
    warn("creating outfile error"); 
    return 500; // making resource failed.. error occured..
  }
  
  // Empty header body -> nothing to process
  if (rObj->len == 0) {
    close(outfile);
    return code;
  }

  // Write bytes from socket body into file resource
  processBody(rObj->socket, rObj->len, outfile);
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
      ServerResponse(404, *rObj); // DNE
    }
    else {
      ServerResponse(403, *rObj); // Forbidden
    }
    return;
  }

  struct stat st;
  stat(rObj->resource, &st);
  rObj->len = st.st_size;

  // Otherwise, send OK response
  ServerResponse(200, *rObj);

  if (rObj->method == _GET_) {
    processBody(infile, rObj->len, rObj->socket);
  }
  close(infile);
}



// Parse line of request header
//    - Populate the message object fields
//    - returns -1 if header fields not accepted.
//    - ignored extraneous lines..
int ParseLine(char* line, struct ClientRequest * rObj) {
  int param_count = 0;
  int param_type = -1;

  char params[5][HEADER_SIZE];
  // Lines expected
  enum {
    _GET = 0, _PUT, _HEAD, _HOST, _USER_AGENT, _ACCEPT, _CONTENT_LENGTH, _EXPECT,

    PARAM_TOTAL,
  };
  const int type_count = PARAM_TOTAL;

  // Map enum to string
  char* types[PARAM_TOTAL] = {
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

  tok = strtok (line, " ");
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
    tok = strtok (NULL, " ");
  }

  // Unexpected line
  if (param_type == -1) {
    fprintf(stderr, "Unknown req/param, ignoring line\n");
    return -1;
  }

  switch(param_type) {
    case _GET: {  // todo error check that param_count is within range of expectation..
      rObj->method = _GET_;
      strcpy(rObj->resource, params[0]); 
      strcpy(rObj->hostname, params[1]);
      break;
    }

   case _PUT: {
      rObj->method = _PUT_;
      strcpy(rObj->resource, params[0]); 
      strcpy(rObj->hostname, params[1]);
      break;
   }

  case _HEAD: {
      rObj->method = _HEAD_;
      strcpy(rObj->resource, params[0]); 
      strcpy(rObj->hostname, params[1]);
      break;
   }

   case _HOST: {
      //Additional parameter(s) than expected indicate spaces on hostvalue
      if (param_count > 2) { 
        return -1;
      }
      // Grab host value from hostname
      tok = strtok(params[0], ":"); 
      tok = strtok (NULL, " ");
      strcpy(rObj->hostvalue, tok);
      break;
   }

   case _USER_AGENT: {
      break;
   }

   case _ACCEPT: {
      break;
   }

   // Assume length is a valid number generated by client curl
   case _CONTENT_LENGTH: {
      rObj->len = atoi(params[0]);     
      break;
   }

   case _EXPECT: {
      break;
   }

   default: {
      fprintf(stderr, "Unknown param type: %i\n", param_type);
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

  struct ClientRequest rObj = {0}; //reset object
  rObj.socket = connfd;

  int parse_status = ParseHeader(request, &rObj);
  if (parse_status < 0) {
    ServerResponse(400, rObj);
    return;
  }

  // Check request fields satisfy requirements
  if (isBadRequest(&rObj)) {
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
    ServerResponse(501, rObj);
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
    if (rec < 0) { warn("recv"); break; }        // Bad recv from client
    if (rec == 0){ break; }                      // Client exits connection
    ProcessRequest(request, connfd);
  }
  close(connfd);
}



// Grab clients
int main(int argc, char *argv[]) {
  int listenfd;
  uint16_t port;

  if (argc != 2) { errx(EXIT_FAILURE, "wrong arguments: %s port_num", argv[0]); }
  port = strtouint16(argv[1]);
  if (port == 0) { errx(EXIT_FAILURE, "invalid port number: %s", argv[1]); }

  listenfd = create_listen_socket(port);

  while(1) 
  {
    int connfd = accept(listenfd, NULL, NULL);
    if (connfd < 0) { warn("accept error"); continue; }
    HandleConnection(connfd);
  }
  return EXIT_SUCCESS;
}
