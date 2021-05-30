
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

void main (int *argc, char **argv){
    int pid;
    char *ptrs[10];

    for (int i = 0; i < 10; i++) {
        ptrs[i] = sbrk(4096);
        *ptrs[i] = '0' + i;
    }

    if ((pid = fork()) == 0) {
        printf("before exec\n");
        char *args[]={"ttt2",0};
        exec(args[0],args);
    }
    else{
        wait(0);
        exit(0);
    }

}