
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"


void main(int argc, char **argv){
    printf("after exec\n");
    char *i = sbrk(4096);
    i[0] = 'c';
    printf("bla bla %c\n", i);
    exit(0);
}