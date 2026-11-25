/* mywmctl — thin command-line client for the mywm IPC socket.
 *
 * The WM-side socket is not implemented in the scaffold yet, so commands
 * currently fail with "cannot connect". The protocol and path logic are
 * finalised here so the server has a matching client to test against.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int make_socket_path(char *buffer, size_t size)
{
    const char *display = getenv("DISPLAY");
    const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
    const char *colon;
    char number[32];
    size_t length = 0;

    if (!display || !*display) {
        fprintf(stderr, "mywmctl: DISPLAY is not set\n");
        return -1;
    }

    colon = strrchr(display, ':');
    if (!colon) {
        fprintf(stderr, "mywmctl: cannot parse DISPLAY '%s'\n", display);
        return -1;
    }

    length = strcspn(colon + 1, ".");
    if (length == 0 || length >= sizeof(number)) {
        fprintf(stderr, "mywmctl: cannot parse DISPLAY '%s'\n", display);
        return -1;
    }

    memcpy(number, colon + 1, length);
    number[length] = '\0';

    if (xdg_runtime && *xdg_runtime) {
        int written = snprintf(buffer, size, "%s/mywm-%s.sock",
                               xdg_runtime, number);
        if (written < 0 || (size_t)written >= size)
            return -1;
    } else {
        int written = snprintf(buffer, size, "/tmp/mywm-%u-%s.sock",
                               (unsigned int)getuid(), number);
        if (written < 0 || (size_t)written >= size)
            return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    char line[4096];
    char response[4096];
    size_t used = 0;
    int fd;
    struct sockaddr_un address;
    ssize_t n;

    if (argc < 2) {
        fprintf(stderr, "usage: mywmctl <command> [args...]\n");
        return 2;
    }

    if (make_socket_path(path, sizeof(path)) < 0) {
        fprintf(stderr, "mywmctl: IPC socket path is too long\n");
        return 2;
    }

    for (int i = 1; i < argc; i++) {
        size_t length = strlen(argv[i]);
        if (used + length + (i > 1 ? 1 : 0) + 2 > sizeof(line)) {
            fprintf(stderr, "mywmctl: command is too long\n");
            return 2;
        }
        if (i > 1)
            line[used++] = ' ';
        memcpy(line + used, argv[i], length);
        used += length;
    }
    line[used++] = '\n';

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "mywmctl: socket: %s\n", strerror(errno));
        return 2;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path) + 1);

    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        fprintf(stderr, "mywmctl: cannot connect to %s: %s\n",
                path, strerror(errno));
        close(fd);
        return 2;
    }

    if (write(fd, line, used) != (ssize_t)used) {
        fprintf(stderr, "mywmctl: write: %s\n", strerror(errno));
        close(fd);
        return 2;
    }
    shutdown(fd, SHUT_WR);

    n = read(fd, response, sizeof(response) - 1);
    if (n < 0) {
        fprintf(stderr, "mywmctl: read: %s\n", strerror(errno));
        close(fd);
        return 2;
    }
    response[n] = '\0';

    fputs(response, stdout);
    close(fd);

    return strncmp(response, "ok", 2) == 0 ? 0 : 1;
}
