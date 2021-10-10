// CSE 130
// Homework 1
// Despina Patronas

#include <stdio.h>  // error msg - fprintf(stderr)
#include <err.h>    // error msg - warn(), errx()
#include <unistd.h> // sys calls - read(), write(), close()
#include <fcntl.h>  // open()
#include <ctype.h>  // isdigit()
#include <stdlib.h> // atoi()
#include <string.h> // strncmp()

const int INPUT_BUFFER_SIZE = 2048;

// Returns 1 when chars of string are digits, else returns 0
int isDigit(char* num) {
  int len = strlen(num);

  for(int i=0; i<len; i++)
    if(!isdigit(num[i]))
      return 0;
      
  return 1;
}

void processInput(int inFile, unsigned int totlines) {
  int isDone = 0;
  unsigned int linesFound = 0;

  char* readbuff = (char *)malloc(INPUT_BUFFER_SIZE);
  if(!readbuff) {
    fprintf(stderr, "Bad malloc!");
    return;
  }

  while(!isDone) {
    int rdCount = read(inFile, readbuff, INPUT_BUFFER_SIZE);
    int lastLine = 0;

    if(rdCount <= 0)
      break;

    // Buffer contains the last line
    else if(rdCount < INPUT_BUFFER_SIZE)
      lastLine = 1;

    // Count newlines for the buffer
    for(int i = 0; i < INPUT_BUFFER_SIZE; i++) {
      if (readbuff[i] == '\n') {
        linesFound++;
  
  // Found all lines requested OR at end of last buffer
        if((linesFound == totlines) || (lastLine && i == rdCount)) {
          write(STDOUT_FILENO, readbuff, i + 1);
          isDone = 1;
          break;
        }
      }
    }
    // Write out full buffer
    if(!isDone) {
      write(STDOUT_FILENO, readbuff, rdCount);
    }
  }
  free(readbuff);
}

int main(int argc, char** argv) {
  int lines;
  int fd;

  //no CLI args
  if(argc == 1) 
    fprintf(stderr, "shoulders: requires an argument");
  
  //Loop through CLI args
  for (int i = 0; i < argc; i++) {

    //Line (n)
    if(i == 1) {
      // No n arg
      if(strncmp(argv[1],"-qn", 3) == 0) {
        fprintf(stderr,"shoulders: option requires an argument -- 'n'");
        fprintf(stderr,"\nTry ‘shoulders --help' for more information.");
      }
      
      // Invalid n
      else if(atoi(argv[1]) < 0 || !isDigit(argv[1]))
        errx(1, "invalid number of lines: '%s'", argv[1]);

      // Exit on 0
      else if (atoi(argv[1]) == 0)
        return 0;

      lines = atoi(argv[1]);

      // No file arg
      if (argc == 2) {
        processInput(STDIN_FILENO, lines);
      }
    }

    // Files
    if(i >= 2) {
      // (-) Dash file arg
      if (strncmp(argv[i], "-", 1) == 0) {
        processInput(STDIN_FILENO, lines);
        continue;
      }
      // Open file arg
      if ((fd = open(argv[i], O_RDONLY, 0)) < 0) { // open file for arg
        warn("cannot open '%s'", argv[i]);         // bad file open
        continue;
      }
      processInput(fd, lines);
    }
  }
  close(fd);
  return 0;
}
