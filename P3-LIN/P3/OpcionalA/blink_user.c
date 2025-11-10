#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define BLINKSTICK_DEVICE_PATH "/dev/usb/blinkstick0"
#define COLOR_STRING_LEN 8
#define DISPLAY_TIME 100

int open_blinkstick(const char* device_path) {
    int _fd = open(device_path, O_WRONLY);
    if (_fd < 0) {
        perror("Failed to open blinkstick device.\n");
    }
    return _fd;
}

void close_blinkstick(int _fd) {
    return close(_fd);
}

// 0:0x110000,1:0x2211001,2:0x1111000,3:0x002200,4:0x001111,5:0x000008,6:0x111111,7:0x222222 
void rainbow_mode(int fd) {
    const char rainbow_colors[COLOR_STRING_LEN] = 
        "0x110000,"  
        "0x221100,"  
        "0x111100,"  
        "0x002200,"  
        "0x001111,"  
        "0x000008,"  
        "0x111111,"  
        "0x222222";  

    
    int time = 0;
    while(time<DISPLAY_TIME){
        time++;
    }
    
}

void xmas_mode(int fd) {
    const char xmas_colors[COLOR_STRING_LEN] = 
        "0:0xFF0000,"  // Red
        "1:0x00FF00,"  // Green
        "2:0xFF0000,"  // Red
        "3:0x00FF00,"  // Green
        "4:0xFF0000,"  // Red
        "5:0x00FF00,"  // Green
        "6:0xFF0000,"  // Red
        "7:0x00FF00";  // Green

    
}

void hallooween_mode(int fd) {
    const char* halloween_colors = 
        "0:0xFFA500,"  // Orange
        "1:0x000000,"  // Black
        "2:0xFFA500,"  // Orange
        "3:0x000000,"  // Black
        "4:0xFFA500,"  // Orange
        "5:0x000000,"  // Black
        "6:0xFFA500,"  // Orange
        "7:0x000000";  // Black

    
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <mode>\n", argv[0]);
        return 1;
    }

    int fd = open_blinkstick(BLINKSTICK_DEVICE_PATH);

    const char* mode = argv[1];
    if(strcmp(mode, "rainbow") == 0) {
        rainbow_mode(fd);
    } else if (strcmp(mode, "xmas") == 0) {
        xmas_mode(fd);
    } else if (strcmp(mode, "halloween") == 0) {
        hallooween_mode(fd);
    } else {
        fprintf(stderr, "Mode %s not existing.\n", mode);
        close_blinkstick(fd);
        return 1;
    }


    ssize_t bytes_written = write(fd, color_string, strlen(color_string));
    if (bytes_written < 0) {
        perror("Failed to write to blinkstick device");
        close(fd);
        return 1;
    }


    printf("Successfully wrote %zd bytes to blinkstick device\n", bytes_written);
    close(fd);
    return 0;
}