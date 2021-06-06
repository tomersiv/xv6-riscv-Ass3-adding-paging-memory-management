
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define PGSIZE 4096

// checks correction of all algorithms with multiple page faults as well as copying swapfile and memory to child process.
void multiple_pagefaults_and_fork()
{
  printf("------------ started multiple_pagefaults_and_fork_test TEST  ------------\n");
  int pid;
  int size = MAX_PSYC_PAGES + 8;
  int *ptrs[size];
  printf("parent is now creating new pages\n");
  for (int i = 0; i < size; i++)
  {
    ptrs[i] = (int *)sbrk(PGSIZE);
    *ptrs[i] = i;
    printf("parent accessed page: %d. value in page: %d\n", i, *ptrs[i]);
  }

  if ((pid = fork()) == 0)
  {
    printf("child is now accessing copied pages from parent\n");
    for (int i = 0; i < size; i++)
    {
      printf("child accessed page: %d. value in page: %d\n", i, *ptrs[i]);
    }
    exit(0);
  }
  else
  {
    wait(0);
    sbrk(-size * PGSIZE);
    printf("------- TEST multiple_pagefaults done -------\n");
  }
}

// checks reallocation of pages after deallocation
void alloc_and_dealloc()
{
  printf("------------ started multiple_pagefaults_and_fork_test TEST  ------------\n");
  int pid;
  if ((pid = fork()) == 0)
  {
    printf("---started allocating 16 pages---\n");
    int *ptrs = (int *)(sbrk(PGSIZE * 16));
    for (int i = 0; i < 16; i++)
    {
      ptrs[i] = 0;
    }
    sbrk(-PGSIZE * 16);
    printf("---done dealloc---\n");
    printf("---now started allocating 16 pages again---\n");
    ptrs = (int *)(sbrk(PGSIZE * 16));
    for (int i = 0; i < 16; i++)
    {
      ptrs[i] = 1;
    }
    for (int i = 0; i < 16; i++)
    {
      printf("ptrs[%d] = %d\n", i, ptrs[i]);
    }
    exit(0);
  }
  else
  {
    wait(0);
    sbrk(-PGSIZE * 16);
    printf("------- TEST alloc_and_dealloc done -------\n");
  }
}

// checks memory image after performing exec
void exec_test()
{
  printf("------------ started exec_test TEST  ------------\n");
  int pid;
  char *ptrs[13];

  if ((pid = fork()) == 0)
  {
  for (int i = 0; i < 13; i++)
  {
    ptrs[i] = (char *)sbrk(PGSIZE);
    *ptrs[i] = '0' + i;
  }
    printf("---before exec---\n");
    char *args[] = {"exec_test", 0};
    exec(args[0], args);
    exit(0);
  }
  else
  {
    wait(0);
    sbrk(-13 * PGSIZE);
    printf("------- TEST exec_test done -------\n");
  }
}

// checks allocation of more pages than process size
void allocate_35_pages()
{
  printf("------ started allocate_35_pages TEST ------\n");
  printf("panic expected...\n");
  sbrk(PGSIZE * 35);
  printf("--- TEST allocate_35_pages done ---\n");
}

// checks access to a deallocated page
void access_deallocated_page()
{
  printf("--- ------------ started access_deallocated_page TEST  ------------\n");
  int *ptrs = (int *)sbrk(16 * PGSIZE);
  for (int i = 0; i < 16; i++)
  {
    ptrs[i] = i;
  }
  sbrk(-16 * PGSIZE);
  printf("trying to access a page....\n");
  printf("value of this page is: %d\n", ptrs[2]); // should do nothing because page has been deallocated
}

// checks value preserving after swapping pages
void swapped_pages_values()
{
  printf("--- ------------ started swapped_pages_values TEST  ------------\n");
  int pid;
  if ((pid = fork()) == 0)
  {
    printf("----started allocating 27 new pages----\n");
    char *ptrs = (char *)sbrk(27 * PGSIZE);
    printf("----now writing values to each page----\n");
    for (int i = 0; i < 27; i++)
    {
      ptrs[(i * PGSIZE)] = i + '0';
    }
    for (int i = 0; i < 27; i++)
    {
      if (ptrs[i * PGSIZE] != i + '0')
      {
        printf("Test failed - value %c was written on page %d\n", ptrs[i * PGSIZE], i);
        break;
      }
    }
    printf("Test passed!!!\n");
    exit(0);
  }
  else
  {
    wait(0);
    sbrk(-27 * PGSIZE);
    printf("--- TEST swapped_pages_values done ---\n");
  }
}

void main(void)
{
  printf("------------ starting tests  ------------\n");
  // multiple_pagefaults_and_fork();
  // swapped_pages_values();
  // alloc_and_dealloc();
  exec_test();
  // // allocate_35_pages();
  // access_deallocated_page();
  exit(0);
}