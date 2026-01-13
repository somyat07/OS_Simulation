#include <stdio.h>
#include <unistd.h>

#include "dummy_main.h"

int main(int argc, char **argv) {
    
    volatile unsigned long long sum = 0;
    printf("Job with PID %d has started its big computation.\n", getpid());

    for (unsigned long long i = 0; i < 2000000000ULL; i++) {
        sum += i;
    }
    for (unsigned long long i = 0; i < 2000000000ULL; i++) {
        sum += i;
    }
    for (unsigned long long i = 0; i < 2000000000ULL; i++) {
        sum += i;
    }
    for (unsigned long long i = 0; i < 2000000000ULL; i++) {
        sum += i;
    }
    printf("Job with PID %d finished  .\n", getpid());
    
    return 0;
}