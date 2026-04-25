#include "user.h"

int
main(int argc, char *argv[])
{
  int i;
  // printf(" --- HELLO USER SPACE ----- \n");
  // printf("argc %d, argv[0] %s, argv[1] %s\n", argc, argv[0], argv[1]);

  // NB: argv[0] should be path itself
  for(i = 1; i < argc; i++){
    write(1, argv[i], strlen(argv[i]));
    if(i + 1 < argc){
      write(1, " ", 1);
    } else {
      write(1, "\n", 1);
    }
  }
  exit(0);
} 
