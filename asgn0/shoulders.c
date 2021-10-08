// CSE 130
// Homework 1
// Despina Patronas

#include <stdio.h>  // fprintf(stderr)
#include <err.h>    // error msg - warn() / err()
#include <unistd.h> // sys calls - read() / write() / close()
#include <fcntl.h>  // open()
#include <ctype.h>  // isDigit()
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

const int INPUT_BUFFER_SIZE = 2048;

// Returns 1 when chars of string are digits, else returns 0
int isDigit(char* num) {

  int len = strlen(num);
  for (int i=0; i<len; i++)
    if(!isdigit(num[i]))
      return 0;

  return 1;
}

void ProcessInput(int infile, unsigned int totlines) {

  char* readbuff = (char *)malloc(INPUT_BUFFER_SIZE);
  if (!readbuff) {
    fprintf(stderr, "Bad malloc!");
    return;
  }
  
  int plines = 0;

  do {
    int rcount = read(infile, readbuff, INPUT_BUFFER_SIZE);
    if (rcount <= 0)
      break;

    char* tmp = readbuff;
    int charcnt = 0;

    //loop through characters of readbuff
    for (int c = 0; c < rcount; c++) {
      charcnt++;

      // end of line, or end of buffer
      int condition_eol = (readbuff[c] == '\n');
      int condition_eob = (c == (rcount - 1));

      if ( condition_eol || condition_eob ){
        if (condition_eol){
          plines++;
        }

        write(STDOUT_FILENO, tmp, charcnt);
        charcnt = 0;
        tmp     = &readbuff[c + 1];
      }

      if (plines >= totlines) 
        break;
    }
  } 
  while(plines < totlines);

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
      else if(atoi(argv[1]) < 0 || !isDigit(argv[1])) {
        errx(1, "invalid number of lines: '%s'", argv[1]);
      }
      
      lines = atoi(argv[1]);

      // No file arg
      if (argc == 2) {
        ProcessInput(STDIN_FILENO, lines);
      }
    }

    // Files
    if(i >= 2) {
      // (-) Dash file arg
      if (strncmp(argv[i], "-", 1) == 0) {
        ProcessInput(STDIN_FILENO, lines);
        continue;
      }
      // Open file arg
      if ((fd = open(argv[i], O_RDONLY, 0)) < 0) { // open file for arg
        warn("cannot open '%s'", argv[i]);         // bad file open
        continue;
      }
      ProcessInput(fd, lines);
    }
  }
  close(fd);
  return 0;
}
