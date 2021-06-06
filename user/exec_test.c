
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define PGSIZE 4096

void main(int argc, char **argv){
    printf("---after exec---\n");
    printf("now trying to allocate 20 more pages...\n");
    int *ptrs = (int *)sbrk(20 * PGSIZE);
    for(int i = 0; i < 20; i++)
    {
        ptrs[i] = i;
    }
    printf("Test passed!! successfully allocated 20 pages\n");
    exit(0);
}