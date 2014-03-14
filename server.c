/*
 * Copyright (C) 2012 Mark Hills <mark@pogo.org.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "debug.h"
#include "external.h"
#include "rig.h"
#include "server.h"
#include "xwax.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))

static struct sockaddr_un addr;
static int sd;

/*
 * Start listening for client connections
 *
 * Return: -1 on error, otherwise 0
 */

int server_start(const char *pathname)
{
    socklen_t len;

    debug("listening");

    sd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sd == -1) {
        perror("socket");
        return -1;
    }

    if (fcntl(sd, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl");
        goto fail;
    }

    addr.sun_family = AF_UNIX;

    /* Use the user's chosen pathname if provided */

    if (pathname != NULL) {
        if (strlen(pathname) >= sizeof addr.sun_path) {
            fprintf(stderr, "%s: pathname too long\n", pathname);
            goto fail;
        }
        strcpy(addr.sun_path, pathname);

    } else {
        const char *dir;

        dir = getenv("TMPDIR");
        if (dir == NULL)
            dir = "/tmp";

        snprintf(addr.sun_path, sizeof addr.sun_path, "%s/xwax.%d",
                 dir, getpid());
    }

    debug("listening on %s", addr.sun_path);

    if (unlink(addr.sun_path) == -1 && errno != ENOENT) {
        perror("unlink");
        goto fail;
    }

    len = strlen(addr.sun_path) + sizeof(addr.sun_family);
    if (bind(sd, (struct sockaddr*)&addr, len) == -1) {
        perror("bind");
        goto fail;
    }

    if (listen(sd, 16) == -1) {
        perror("listen");
        goto fail;
    }

    return 0;

fail:
    if (close(sd) != 0)
        abort();
    return -1;
}

/*
 * Stop listening for client connections
 */

void server_stop(void)
{
    if (close(sd) != 0)
        abort();
    if (unlink(addr.sun_path) != 0)
        abort();
}

void client_init(struct client *c, int fd)
{
    debug("%p with fd %d", c, fd);
    c->fd = fd;
    c->fill = 0;
    c->argc = 0;
    rig_post_client(c);
}

void client_clear(struct client *c)
{
    size_t n;

    debug("%p", c);
    list_del(&c->rig);

    for (n = 0; n < c->argc; n++)
        free(c->argv[n]);

    if (close(c->fd) != 0)
        abort();
}

void client_pollfd(struct client *c, struct pollfd *pe)
{
    pe->fd = c->fd;
    pe->revents = 0;
    pe->events = POLLIN;
}

/*
 * Take an argument, including control of its pointer
 */

static char* take_arg(char **arg)
{
    char *d;

    d = *arg;
    *arg = NULL;
    return d;
}

/*
 * Execute a client command
 */

static int cmd(int argc, char *argv[])
{
    int d;
    size_t n;
    struct record *r;

    for (n = 0; n < argc; n++)
        debug("argument %d: '%s'", n, argv[n]);

    if (argc != 4) {
        fprintf(stderr, "client: wrong number of arguments\n");
        return -1;
    }

    d = atoi(argv[0]);
    if (d < 0 || d >= ndeck) {
        fprintf(stderr, "client: deck number out of range\n");
        return -1;
    }

    r = malloc(sizeof *r);
    if (r == NULL) {
        perror("malloc");
        return -1;
    }

    r->pathname = take_arg(&argv[1]);
    r->artist = take_arg(&argv[2]);
    r->title = take_arg(&argv[3]);

    r = library_add(&library, r);
    if (r == NULL) {
        /* FIXME: memory leak, need to do record_clear(r) */
        return -1;
    }

    deck_load(&deck[d], r);

    return 0;
}

static int do_stuff(struct client *c)
{
    ///* Buffer all of the arguments */

    for (;;) {
        char *f;

        errno = 0;
        f = read_field(c->fd, c->buf, &c->fill, sizeof c->buf);
        if (f == NULL) {
            switch (errno) {
            case 0:
                debug("got EOF");
                goto action;

            case EAGAIN:
                return 0;

            default:
                perror("read_field");
                return -1;
            }
        }

        assert(f != NULL);
        debug("got '%s'", f);

        c->argv[c->argc] = f;
        c->argc++;

        if (c->argc == ARRAY_SIZE(c->argv)) {
            fprintf(stderr, "client: too many arguments\n");
            return -1;
        }
    }

action:
    c->argv[c->argc] = NULL;

    cmd(c->argc, c->argv);
    write(c->fd, "pong\n", 5);
    return -1;
}

void client_handle(struct client *c)
{
    if (do_stuff(c) == -1) {
        client_clear(c);
        free(c);
    }
}

void server_pollfd(struct pollfd *pe)
{
    pe->fd = sd;
    pe->revents = 0;
    pe->events = POLLIN;
}

int server_handle(void)
{
    int fd;
    struct sockaddr addr;
    socklen_t len = sizeof(addr);
    struct client *client;

    debug("accept");

    fd = accept(sd, &addr, &len);
    if (fd == -1) {
        perror("accept");
        return -1;
    }

    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl");
        goto fail;
    }

    debug("new connection on fd %d", fd);

    client = malloc(sizeof *client);
    if (client == NULL) {
        perror("malloc");
        goto fail;
    }

    client_init(client, fd);

    return 0;

fail:
    if (close(fd) != 0)
        abort();
    return 0;
}
