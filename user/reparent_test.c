#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int pid1 = fork();
    if(pid1 == 0){

        int pid2 = fork();
        if(pid2 == 0){

            sleep(50);
            printf("Child2 alive after reparenting. My PID = %d\n", getpid());
            exit(0);
        }

        exit(0);
    }


    wait(0);

    sleep(100);
    printf("Grandparent (%d): waiting for child2...\n", getpid());

    wait(0);
    printf("Reparent test completed.\n");
    exit(0);
}
