// CSE 130
// Homework 1
// Despina Patronas

#include <stdio.h>  // error msg only - fprintf(), perror()
#include <err.h>    // error msg -  warn(3)
#include <unistd.h> // sys calls - read(2) / write(2) / close(2)
#include <fcntl.h>  // open(2)
#include <ctype.h>  // isDigit()
#include <stdlib.h> // strtol()
#include <stdint.h> // uint_8t
#include <string.h>

int const SIZE = 1024;

//Citations / Resources:
// stderr functions:
//    https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_lib_ref/e/err.html

// Returns 1 when chars of string are digits, else returns 0
int isDigit(char* num) {
  int len = strlen(num);
  char *n = num;

  for (int i=0; i<len; i++) 
    if(!isdigit(n[i])) 
      return 0;

  return 1;
}

// Used to populate buffer and write to fd
// fd = file or stdout
void Buffer(char * buff, int lines, int fd) {
  
  int read_bytes = 0;     // bytes read
  int count = 0;          // keeps track of characters for buffer

  //loop through lines                   
  for (int j = 0; j < lines; j++) {
    //loop through characters of lines until EOF
    do {
      char ch = 0;
      read_bytes = read(fd, &ch, 1);     // read 1 char at a time
      *(buff+count) = ch;                // populate buffer
      if(ch == '\n')                     // exit line for \n char
        break;
      count++;                           // otherwise keep looping        
    }
    while (read_bytes != 0);             // EOF break

    if (read_bytes == 0)                 // dont write junk if EOF
      break;                             
    
    write(STDOUT_FILENO, buff, count+1); // write line buffer to stdout
    count = 0; 
    memset( buff, 0, SIZE);              // reset for next line or file
  }
}

int main(int argc, char** argv) {
  char* buff = (char *)malloc(SIZE);      // buffer to hold file contents
  if (!buff)
    fprintf(stderr, "Bad malloc!");
  int lines;                              // argv[1]
  int fd;                                 // file descriptor

  //no CLI args
  if(argc == 1) 
    fprintf(stderr, "shoulders: requires an argument");
  
  //Loop through CLI args
  for (int i = 0; i < argc; i++) {

    //Lines CLI arg
    if(i==1) {
      if(strncmp(argv[1],"-qn", 3)==0)                
        fprintf(stderr,"shoulders: option requires an argument -- 'n' \nTry ‘shoulders --help' for more information.");

      else if(atoi(argv[1])<0 || !isDigit(argv[1])) {
        errx(1, "invalid number of lines: '%s'", argv[1]);
      }
      lines = atoi(argv[1]);

      // no file args
      if (argc == 2) {
        Buffer(buff, lines, STDIN_FILENO);
      }
    }

    // Files CLI args
    if(i>=2) {

      // -file
      if (strncmp(argv[i], "-", 1) ==0) {
        Buffer(buff, lines, STDIN_FILENO);
        continue;
      }

      // open file
      if ((fd = open(argv[i], O_RDONLY, 0)) < 0) { // open file for arg             
        warn("cannot open '%s'", argv[i]);         // bad file open
        continue;
      }
      Buffer(buff, lines, fd);
    }
  }
  free(buff);
  return 0;
}
