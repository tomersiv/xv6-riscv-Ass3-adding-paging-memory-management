
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

void testPageFault(){
  int *page_addresses[MAX_PSYC_PAGES + 1];
  int pid;
  
  printf("parent is now creating MAX_PSYC_PAGES + 1 new pages\n");
  for (int i = 0 ; i < MAX_PSYC_PAGES + 1 ; i++){
        page_addresses[i] = (int *)sbrk(4096);
        *page_addresses[i] = i;
        printf("parent accessed page: %d. value in page: %d\n", i, i);
  }

  if( (pid = fork()) ==0){
    printf("child is now accessing copied pages from parent\n");
      for (int i = 0 ; i < MAX_PSYC_PAGES + 1 ; i++){
        printf("child accessed page: %d. value in page: %d\n", i, *page_addresses[i]);
      }
      exit(0);
  }
  else
  {
    wait(0); 
  }
}

int
main(void)
{
  printf( "--------- test  ---------\n");
  testPageFault();
  exit(0);
  return 0; 
}