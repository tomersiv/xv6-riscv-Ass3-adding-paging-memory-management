
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
void 
testall(){
  int i = 0;
  uint64 c = 17;
  uint64 pointers[c];
  printf("IN PARENT\n");
  for (i = 0 ; i < c ; i++){
        pointers[i] = (uint64)sbrk(4096);
        * (char *) pointers[i] = (char) ('a' + i);
        printf("%c\n",  * (char *) pointers[i]);
  }

  int pid;
  if( (pid = fork()) ==0){
      printf("IN CHILD \n");
      for (i = 0 ; i < c ; i++){
        printf("letter %c\n", *(char * )pointers[i]);
      }
      exit(0);
  }
  else{
    int status;
    wait(&status); 
  }
}

void
testSecFIFO(){
  int i = 0;
  uint c = 14;
  uint64 pointers[c];
  //create all files
  for (i = 0 ; i < c ; i++){
        pointers[i] = (uint64)sbrk(4096);
        * (char *) pointers[i] = (char) ('a' + i);
        printf("%c\n", *(char * )pointers[i]);
  }
  //accsess only 
  for (i = 1 ; i < c/2 ; i++){
        printf("sec %c\n", *(char * )pointers[i]);
  }
  printf("third %c\n", *(char * )pointers[0]);
}

int
main(void)
{
  printf( "--------- test  ---------\n");
  testall();
//   testSecFIFO();
  exit(0);
  return 0; 
}