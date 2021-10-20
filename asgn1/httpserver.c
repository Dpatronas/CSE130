#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>      // fprintf(stderr, ...), scanf()
#include <errno.h>      // check warnings
#include <fcntl.h>      // open()
#include <sys/stat.h>   // fstat

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#define HEADER_SIZE       1024          // 1KiB max header length
#define PROCESS_BODY_SIZE 2048

extern int errno;

struct stat st;

// status code options
const char* Status(int code) {
  switch(code) {
    case 200: return "OK";            // Response is successful ie: nothing else broke
    case 201: return "Created";       // Successful PUT resource
    case 400: return "Bad Request";   // Request is not valid ie: not parsable
    case 403: return "Forbidden";     // Cannot access valid resource file (for GET)
    case 404: return "Not Found";     // Valid resource name but server cannot find file
    case 500: return "Internal Server Error"; // Request is valid but cannot allocate memory to process request
    case 501: return "Not Implemented";       // Request is valid is ok BUT command not valid
  }
  return NULL;
}

// Send client response for request
void ServerResponse(int connfd, int code, int length) {

  char response[HEADER_SIZE];
  sprintf(response, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\n\r\n", code, Status(code), length);
  int s = send(connfd, response, strlen(response), 0);
  if (s < 0) { warn("send()"); }
}

/**
   Converts a string to an 16 bits unsigned integer.
   Returns 0 if the string is malformed or out of the range.
 */
uint16_t strtouint16(char number[]) {
  char *last;
  long num = strtol(number, &last, 10);
  if (num <= 0 || num > UINT16_MAX || *last != '\0') {
    return 0;
  }
  return num;
}


/**
   Creates a socket for listening for connections.
   Closes the program and prints an error message on error.
 */
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


void processInput(int infile, int len, int outfile, char * op) {
  int isDone = 0; int tot_read = 0;

  char* readbuff = (char *)malloc(PROCESS_BODY_SIZE);
  if(!readbuff) {
    fprintf(stderr, "Bad malloc!");
    return;
  }

  while(!isDone) {
    int lastLine = 0;
    int rdCount = read(infile, readbuff, PROCESS_BODY_SIZE);

    // printf("ReadIN: %d\n", rdCount);
    // printf("buffer: %s", readbuff);

    if(rdCount <= 0)
      break;

    // Buffer contains the last line
    if(rdCount < PROCESS_BODY_SIZE)
      lastLine = 1;

    // Count newlines for the buffer
    for(int i = 0; i < PROCESS_BODY_SIZE; i++) {
      tot_read++;
      // Real all bytes OR at end of last buffer
      if ((lastLine && i == rdCount) || tot_read == len)  {
        if (strncmp(op,"GET",3) == 0) {
            write(STDOUT_FILENO, readbuff, i + 1); //debug server msg
            send(outfile, readbuff, i + 1, 0); //
        }
        else {
          write(STDOUT_FILENO, readbuff, i + 1); //debug server msg
          write(outfile, readbuff, i + 1); //
        }

        isDone = 1;
        break;
      }
    }
    // Write out full buffer
    if(!isDone) {
      if (strncmp(op,"GET",3) == 0) {
        write(STDOUT_FILENO, readbuff, rdCount); //debug server msg
        send(outfile, readbuff, rdCount);
      }
      else {
        write(STDOUT_FILENO, readbuff, rdCount); //debug server msg
        write(outfile, readbuff, rdCount, 0);
      }
    }
  }
  // write(outfile, '\0', 1);  //insert eof?
  free(readbuff);
}


void ParsePut(char *m, char *r, int len, int connfd) {

  int outfile;

  printf("length = %d\n", len);  printf("outfile = %s\n", r);

  outfile = open(r, O_RDWR | O_CREAT | O_TRUNC, 0777);           //create resource file
  if (outfile < 0) { 
    warn("creating outfile error"); 
    return; 
  }

  processInput(connfd, len, outfile, m); //write recv'd bytes into file resource

  printf("file processed\n");
  return;
}


void ParseGet(char *m, char *r, int connfd) {

  int infile; int len;

  // Check file exists
  if ((infile = open(r, O_RDONLY, 0)) < 0) {              // open file
    warn("cannot open '%s' due to ernno: %d", r, errno);  // bad file open
    if (errno == 2) {                                     // check errno for status code
      ServerResponse(connfd, 404, 0);
    }
    else {
      ServerResponse(connfd, 403, 0);
    }
    return;
  }

  stat(r, &st);
  len = st.st_size;
  printf("\nlength = %d\n", len);

  ServerResponse(connfd, 200, len);
  // Otherwise perform GET
  processInput(infile, len, connfd, m);
  return;
}



/**
 * Parse ASCII request fields (space seperated)
 *  m = commands   (GET,PUT,HEAD)
 *  r = resource   (Some file)
 *  v = version    (HTTP/1.1)
*/
void ParseRequest(char *request, int connfd) {
  char m[100];     char r[100]; char v[100];  // Request
  char name[100];  char value[100];           // Header
  char extra[100]; char content[100];         // Body
  int len;

  // Populate request fields
  sscanf(request, "%s %s %s %s %s %s %s %s %s %s %d" , m, r, v, name, value, extra, extra, extra, extra, content, &len);
  printf("command = %s\n resource = %s\n version = %s\n name = %s\n value = %s\n content = %s\n len = %d\n\n", m, r, v, name, value, content, len); 
  
  memmove(r, r+1, strlen(r)); //remove the backslash from file

  // Bad version
  if (!(strncmp(v,"HTTP/1.1",8) == 0)) {
    ServerResponse(connfd, 400, 0);
    return;
  }

  // Check commands
  if (strncmp(m,"GET",3) == 0) {
    ParseGet(m, r, connfd);
    return;
  }

  //----------------------------------------------------------------------
  else if (strncmp(m,"PUT",3) == 0) {
    ParsePut(m, r, len, connfd);
    ServerResponse(connfd, 201, 8);
    return;
  }

  // else if (strncmp(m, "HEAD",4) == 0) {
  //   ParseHead(m,r, connfd);
  //   return;
  // }

  else {
    ServerResponse(connfd, 501, 0);
    return;
  }
}


void handle_connection(int connfd) {

  char request[HEADER_SIZE];   
  int rec = 0;

  // Server maintains connection
  while(1) {

    memset(request,0,sizeof request);

    // Receive new request from client
    rec = recv(connfd, request, HEADER_SIZE, 0);    // Assume request makes it in one line
    if (rec < 0) { warn("recv"); break; }        // Bad recv from client
    if (rec == 0){ break; }                      // Client exits connection

    printf("the request: \n%s\n", request);

    // Parse received request
    ParseRequest(request, connfd);
  }

  close(connfd);
}


int main(int argc, char *argv[]) {
  int listenfd;
  uint16_t port;

  if (argc != 2) { errx(EXIT_FAILURE, "wrong arguments: %s port_num", argv[0]); }
  port = strtouint16(argv[1]);
  if (port == 0) { errx(EXIT_FAILURE, "invalid port number: %s", argv[1]); }

  listenfd = create_listen_socket(port);

  while(1) {
    int connfd = accept(listenfd, NULL, NULL);
    if (connfd < 0) { warn("accept error"); continue; }
    handle_connection(connfd);
  }
  return EXIT_SUCCESS;
}
