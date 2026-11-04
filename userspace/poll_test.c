#include <stdio.h>
#include <unistd.h>
#include <poll.h>

#include <fcntl.h>

#define TIMEOUT 30

int main()
{
    struct pollfd fds[1];
    int ret;
    
    int fd = open("/dev/mydevice", O_WRONLY);
    if(fd < 0) {
        perror("open");
        return 1;
    }
    

    fds[0].fd = fd;
    fds[0].events = POLLOUT;

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

    if (fds[0].revents & POLLOUT){
        printf("mydevice is writable\n");
        }    
    close(fd);

    return 0;
}

