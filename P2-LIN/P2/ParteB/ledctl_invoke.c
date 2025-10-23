#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <errno.h>

#define SYS_LEDCTL 451 //Nuevo valor en la tabla del kernel para llamadas al sistema que se encarga de gestionar los leds

int ledctl(unsigned int leds){
    return (int) syscall(SYS_LEDCTL, leds);
}

int main(int argc, char* argv[]){

    if (argc!= 2) {
		fprintf(stderr,"Usage: ./ledctl_invoke <ledmask>\n");
		exit(EXIT_FAILURE);
	}

    int mask;

    if (sscanf(argv[1],"%d",&mask)!=1){
        errno=EINVAL;
        perror("First argument");
        exit(EXIT_FAILURE); 
    }

    if(mask >7 || mask < 0){
        perror("Be sure you enter a number and that it's between 0-7.\n");
        exit(EXIT_FAILURE);
    }

    if((ledctl(mask)<0)){
        perror("Error in ledctl\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}