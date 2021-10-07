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

int const SIZE = 4096;

//Citations: 
// warn usage - https://linux.die.net/man/3/warn

// Returns 1 when chars of string are digits, else returns 0
int isDigit(char* num){
  int len = strlen(num);
  char *n = num;

  for (int i=0; i<len; i++) 
    if(!isdigit(n[i])) 
      return 0;

  return 1;
}


int main(int argc, char** argv) {
  int lines;                    // argv[1]
  int fd;                       // file descriptor
  char buff[SIZE];       // buffer to hold file contents
  int read_bytes = 0;           // bytes read
  int count = 0;                // keeps track of characters for buffer

  //no CLI args
  if(argc == 1) 
    fprintf(stderr, "shoulders: requires an argument");
  
  //Loop through CLI args
  for (int i = 0; i < argc; i++) {

    //Lines arg
    if(i==1) {
      if(strncmp(argv[1],"-qn", 3)==0)                
        fprintf(stderr,"shoulders: option requires an argument -- 'n' \nTry ‘shoulders --help' for more information.");

      else if(atoi(argv[1])<0 || !isDigit(argv[1])) {
        fprintf(stderr, "shoulders: invalid number of lines: '%s'", argv[1]);
      }
      lines = atoi(argv[1]);
    }

    // No files in arg || file has '-'
    // In Progress

    // Files args
    if(i>=2) {
      if ((fd = open(argv[i], O_RDONLY, 0)) < 0)  // open file for arg             
          err(1, "%s", argv[0]);                  // bad file open
      
      //loop through files lines                   
      for (int j = 0; j < lines; j++) {
        //loop through characters of line
        do {
          char ch = 0;
          read_bytes = read(fd, &ch, 1);     // read 1 char at a time
          buff[count] = ch;                  // populate buffer
          if(buff[count] == '\n')            // exit line for \n char
            break;
          count++;                           // otherwise keep looping        
        }
        while (read_bytes != 0);             // EOF break

        write(STDOUT_FILENO, buff, count+1); // write line buffer to stdout
        count = 0; 
        memset( buff, 0, SIZE);              // reset for next line or file
      }
    }
  }
  return 0;
}
