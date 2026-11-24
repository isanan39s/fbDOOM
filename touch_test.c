#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

int main(void) {
    int fd = open("/dev/input/event1", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/input/event1");
        return 1;
    }

    printf("Listening on /dev/input/event1...\n");
    struct input_event ev;
    while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_SYN)
            continue;
        printf("type=0x%02x code=0x%04x value=%d\n", ev.type, ev.code, ev.value);
        fflush(stdout);
    }

    close(fd);
    return 0;
}
