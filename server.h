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

/*
 * Functions for external control via a socket
 */

#ifndef SOCKET_H
#define SOCKET_H

#include <poll.h>
#include <stdlib.h>

#include "list.h"

struct client {
    int fd;
    struct list rig;
    char buf[4096];
    size_t fill;

    char *argv[32];
    int argc;
};

int server_start(const char *pathname);
void server_stop(void);

void server_pollfd(struct pollfd *pe);
int server_handle(void);

void client_pollfd(struct client *c, struct pollfd *pe);
void client_handle(struct client *c);

#endif
