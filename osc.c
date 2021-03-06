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
 *  Copyright (C) 2004 Steve Harris, Uwe Koloska
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "osc.h"
#include "player.h"
#include "deck.h"
#include "track.h"
#include "player.h"

#include "lo/lo.h"

int done = 0;

lo_server_thread st;
lo_server_thread st_tcp;
pthread_t thread_osc_updater;
struct deck *osc_deck;
int osc_ndeck = 0;
lo_address address[2];
int osc_nconnection = 0;
int osc_nclient = 0;



void osc_add_deck()
{
    ++osc_ndeck;
    fprintf(stderr, "osc.c: osc_add_deck(): osc_ndeck: %i\n", osc_ndeck);
}

void osc_start_updater_thread()
{
    pthread_create( &thread_osc_updater, NULL, osc_start_updater, (void*) NULL);
    
    pthread_setschedprio(thread_osc_updater, 80);
}

void osc_start_updater()
{
    while(!done) {
        int i;
        for(i = 0; i < osc_ndeck; ++i) {
            osc_send_pos(i, player_get_elapsed(&osc_deck[i].player), osc_deck[i].player.pitch);
        }
        // wierd. much less jitter when sleeping between updates
        usleep(1000);
    }
}

int osc_send_ppm_block(struct track *tr)
{
    if(tr) {
        int c;
        for(c = 0; c < osc_nclient%3; ++c) {
            int i = 0;
            while(i < tr->length) {
                int j = 0;
                unsigned char ppm_block[24575];
                
                while(j < 24575 && i < tr->length) {
                    ppm_block[j] = track_get_ppm(tr, i);
                    ++j;
                    i += 64;
                }
                
                /* build a blob object from some data */
                lo_blob blob = lo_blob_new(j, &ppm_block);

                //lo_server server = lo_server_thread_get_server(st_tcp);
                if (lo_send(address[c], "/touchwax/ppm", "ibi", 
                    (int) tr, 
                    blob,
                    tr->length
                ) == -1) {
                    printf("OSC error %d: %s\n", lo_address_errno(address[c]),
                        lo_address_errstr(address[c]));
                }
                lo_blob_free(blob);
            }
            lo_send(address[c], "/touchwax/ppm_end", "i", (int) tr);
            
            printf("Sent %p blocks to %s\n", tr, lo_address_get_url(address[c]));
            sleep(1); // Wierd bug in liblo that makes second track load not catched by client's track_load_handler if sent too fast
        }

    }
    return 0;
}

int osc_send_pos(int d, const float pos, const float pitch)
{
    //lo_address t = lo_address_new(NULL, "7771");
    //lo_address t = lo_address_new(address, "7771");

    /* send a message to /touchwax/position with one float argument, report any
     * errors */
    int c;
    for(c = 0; c < osc_nclient%3; ++c) {
        if (lo_send(address[c], "/touchwax/position", "iff", d, pos, pitch) == -1) {
            printf("OSC error %d: %s\n", lo_address_errno(address[c]),
                   lo_address_errstr(address[c]));
        }
    }
        
    return 0;
}

int osc_send_track_load(struct deck *de)
{
    struct player *pl;
    struct track *tr;
    pl = &de->player;
    tr = pl->track;
    
    if(tr) {
        /* send a message to /touchwax/track_load with two arguments, report any
         * errors */
        int c;
        for(c = 0; c < osc_nclient%3; ++c) { 
            if (lo_send(address[c], "/touchwax/track_load", "iissi",
                    de->ncontrol,
                    (int) tr,
                    de->record->artist, 
                    de->record->title,
                    tr->rate
                ) == -1) {
                printf("OSC error %d: %s\n", lo_address_errno(address[c]),
                       lo_address_errstr(address[c]));
            }
            printf("osc_send_track_load: sent track_load to %s\n", lo_address_get_url(address[c]));
            sleep(1); // Wierd bug in liblo that makes second track load not catched by client's track_load_handler if sent too fast
            
        }
    }
    
    return 0;
}

int osc_send_scale(int scale)
{
    /* send a message to /touchwax/track_load with two arguments, report any
     * errors */
    int c;
    for(c = 0; c < osc_nclient%3; ++c) {
        if (lo_send(address[c], "/touchwax/scale", "i", scale) == -1) {
            printf("OSC error %d: %s\n", lo_address_errno(address[c]),
                   lo_address_errstr(address[c]));
        }
    }
    
    return 0;
}

int osc_start(struct deck *deck)
{
    osc_deck = deck;
    
    /* start a new server on port 7770 */
    st = lo_server_thread_new("7770", error);

    /* add method that will match any path and args */
    //lo_server_thread_add_method(st, NULL, NULL, generic_handler, NULL);
    lo_server_thread_add_method(st, "/xwax/connect", "", connect_handler, NULL);


    /* add method that will match the path /foo/bar, with two numbers, coerced
     * to float and int */
    lo_server_thread_add_method(st, "/xwax/pitch", "if", pitch_handler, NULL);
    
    /* add method that will match the path /foo/bar, with two numbers, coerced
     * to float and int */
    lo_server_thread_add_method(st, "/xwax/position", "if", position_handler, NULL);
    
    /* add method that will match the path /quit with no args */
    lo_server_thread_add_method(st, "/quit", "", quit_handler, NULL);

    lo_server_thread_start(st);
    
    ///* Start a TCP server used for receiving PPM packets */
    //st_tcp = lo_server_thread_new_with_proto("7771", LO_TCP, error);
    //if (!st_tcp) {
        //printf("Could not create tcp server thread.\n");
        //exit(1);
    //}
    
    //lo_server s = lo_server_thread_get_server(st);   
    
    ///* add method that will match the path /foo/bar, with two numbers, coerced
     //* to float and int */
    
    //lo_server_thread_start(st_tcp);
    
    //printf("Listening on TCP port %s\n", "7771");

    return 0;
}

void osc_stop()
{
    done = 1;
    lo_server_thread_free(st);
}

void error(int num, const char *msg, const char *path)
{
    printf("liblo server error %d in path %s: %s\n", num, path, msg);
    fflush(stdout);
}

/* catch any incoming messages and display them. returning 1 means that the
 * message has not been fully handled and the server should try other methods */
int generic_handler(const char *path, const char *types, lo_arg ** argv,
                    int argc, void *data, void *user_data)
{
    int i;

    printf("path: <%s>\n", path);
    for (i = 0; i < argc; i++) {
        printf("arg %d '%c' ", i, types[i]);
        lo_arg_pp((lo_type)types[i], argv[i]);
        printf("\n");
    }
    printf("\n");
    fflush(stdout);

    return 1;
}

int pitch_handler(const char *path, const char *types, lo_arg ** argv,
                int argc, void *data, void *user_data)
{
    /* example showing pulling the argument values out of the argv array */
    printf("%s <- f:%f\n", path, argv[1]->f);
    fflush(stdout);
    
    struct deck *de;
    struct player *pl;
    de = &osc_deck[argv[0]->i];
    pl = &de->player;
    
    player_set_pitch(pl, argv[1]->f);

    return 0;
}

int connect_handler(const char *path, const char *types, lo_arg ** argv,
                int argc, void *data, void *user_data)
{
    /* example showing pulling the argument values out of the argv array */
    printf("%s\n", path);
    fflush(stdout);
    
    lo_address a = lo_message_get_source(data);
    address[osc_nconnection%2] = lo_address_new_from_url(lo_address_get_url(a));
    printf("OSC client %i address changed to:%s\n", osc_nconnection%2, lo_address_get_url(address[osc_nconnection%2]));
    ++osc_nconnection;
    if(osc_nclient < 2)
        ++osc_nclient;

    struct deck *de;
    struct player *pl;

    fprintf(stderr, "osc_nclient %i osc_ndeck: %i\n", osc_nclient, osc_ndeck);
    int i;
    for(i = 0; i < osc_ndeck; ++i) {
        de = &osc_deck[i];
        pl = &de->player;
        osc_send_track_load(de);
        osc_send_ppm_block(pl->track);
    }
        
    
    return 0;
}

int position_handler(const char *path, const char *types, lo_arg ** argv,
                int argc, void *data, void *user_data)
{
    /* example showing pulling the argument values out of the argv array */
    //printf("%s\n", path);
    //fflush(stdout);
    
    struct deck *de;
    struct player *pl;
    de = &osc_deck[argv[0]->i];
    pl = &de->player;
    
    player_seek_to(pl, argv[1]->f);
    
    return 0;
}

int quit_handler(const char *path, const char *types, lo_arg ** argv,
                 int argc, void *data, void *user_data)
{
    done = 1;
    printf("quiting\n\n");
    fflush(stdout);

    return 0;
}
