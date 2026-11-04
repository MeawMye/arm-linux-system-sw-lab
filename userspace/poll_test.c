#include <stdio.h>
#include <unistd.h>
#include <poll.h>

#include <fcntl.h>

#define TIMEOUT 30

int main()
{
    struct pollfd fds[1];
    int ret;
    
    int fd = open("/dev/mydevice", O_RDONLY);
    if(fd < 0) {
        perror("open");
        return 1;
    }
    

    fds[0].fd = fd;
    fds[0].events = POLLIN;

    ret = poll(fds, 1, TIMEOUT * 1000);

    if (ret == -1) {
        perror("poll");
        close(fd);
        return 1;
    }

    if (!ret) {
        printf("%d seconds elapsed.\n", TIMEOUT);
        close(fd);
        return 0;
    }

    if (fds[0].revents & POLLIN){
        printf("mydevice is readable\n");
        char buffer[256];
        ssize_t counted = read(fd, buffer, sizeof(buffer)-1);
        
        if(counted < 0) {
            perror("read");
            close(fd);
            return 1;
        }
        
        if(counted > 0) {
          buffer[counted] = '\0';
          printf("%s\n",buffer);
        }  
    }    
    close(fd);

    return 0;
}

