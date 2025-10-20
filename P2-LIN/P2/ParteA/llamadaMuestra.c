#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#define __NR_HELLO 451

long lin_hello(void) {
    return (long) syscall(__NR_HELLO);
}

int main(void) {
    return lin_hello();
}

/*
kernel@debian:~/Documentos/LIN-25-26/P2-LIN/P2/ParteA$ gcc -g -Wall llamadaMuestra.c -o llamadaMuestra
kernel@debian:~/Documentos/LIN-25-26/P2-LIN/P2/ParteA$ ./llamadaMuestra 
kernel@debian:~/Documentos/LIN-25-26/P2-LIN/P2/ParteA$ sudo dmesg | tail
...
[  384.207442] Hello world
*/