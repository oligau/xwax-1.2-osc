#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "external.h"

int main(int argc, char *argv[])
{
    int fd;
    char buf[32];
    size_t fill = 0;

    fd = STDIN_FILENO;

    for (;;) {
        char *s;

        errno = 0;
        s = read_field(fd, buf, &fill, sizeof buf);
        if (s == NULL) {
            if (errno == 0) /* EOF */
                break;
            perror("get_field");
            if (errno != EAGAIN)
                return -1;
        } else {
            fprintf(stderr, "field: '%s'\n", s);
            free(s);
        }
    }

    return 0;
}
