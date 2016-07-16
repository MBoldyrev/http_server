#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ev.h>
#include <sys/wait.h>
//#include <sys/types.h>
//#include <sys/file.h> // flock

#include "fd_pass.h"
#include "worker.h"

#define NUM_WORKER_THREADS 3

typedef struct {
    ev_io io;
    int worker_number;
}
worker_listener;

static int server_socket;
static int master_sockets[ NUM_WORKER_THREADS ];
static int worker_status [ NUM_WORKER_THREADS ]; // 1 - ready, 0 - not
static pid_t child_pids  [ NUM_WORKER_THREADS ];
static worker_listener worker_listeners[ NUM_WORKER_THREADS ];
static ev_io server_socket_listener;
int num_threads_ready;

static void master_sig_chld_catcher( int signo ) {
    int status;
    pid_t p = wait( &status );
    for( int worker_number = 0; worker_number < NUM_WORKER_THREADS; ++worker_number ) {
        if( p == child_pids[ worker_number ] ) {
            fprintf( stderr, "Got SIGCHLD from %d worker\n", worker_number );
            shutdown( master_sockets[ worker_number ], SHUT_RDWR );
            close( master_sockets[ worker_number ] );
        }
    }
}

static void master_sig_term_catcher( int signo ) {
    shutdown( server_socket, SHUT_RDWR );
    close( server_socket );
    kill( 0, SIGTERM ); // kill process group
    exit(0);
}

static void
worker_read_cb( struct ev_loop *loop, ev_io *w, int revents ) {
    char msg;
    int worker_number = ((worker_listener*)w)->worker_number;
    if( recv( w->fd, &msg, 1, MSG_NOSIGNAL ) > 0 ) {
        if( msg == 1 ) {
            printf("Worker %d says it's ready\n", worker_number );
            if( worker_status[ worker_number ] == 0 )
                ++num_threads_ready;
            worker_status[ worker_number ] = 1;
            if( num_threads_ready == 1 )
                ev_io_start( loop, &server_socket_listener );
        }
    }
}

static void
server_socket_read_cb( struct ev_loop *loop, ev_io *w, int revents ) {
    fprintf( stderr, "Got a connection\n" );
    int next_worker = -1;
    for( int worker_number = 0; worker_number < NUM_WORKER_THREADS;
            ++worker_number ) {
        if( worker_status[ worker_number ] == 1 ) {
            next_worker = worker_number;
            break;
        }
    }
    int client_socket = accept( server_socket, 0, 0 );
    fprintf( stderr, "Passing it to worker %d\n", next_worker );
    worker_status[ next_worker ] = 0; // now it's busy untl it says the opposit
    --num_threads_ready;
    if( num_threads_ready == 0 )
        ev_io_stop( loop, &server_socket_listener );
    send_file_descriptor( master_sockets[ next_worker ], client_socket );
    close( client_socket );
}

int main( int argc, char **argv ) {
    struct ev_loop *loop = EV_DEFAULT;

    num_threads_ready = 0;
    int worker_sockets[ NUM_WORKER_THREADS ];
    setsid();

    for( int worker_number = 0; worker_number < NUM_WORKER_THREADS; ++worker_number ) {
        worker_status[ worker_number ] = 0; // not ready
        int fd[2];
        if( 0 != socketpair( AF_UNIX, SOCK_STREAM, 0, fd ) ) {
            fprintf( stderr, "Failed to create UNIX socket for worker %d\n", 
                    worker_number );
            exit( 1 );
        }
        master_sockets[worker_number] = fd[0];
        worker_sockets[worker_number] = fd[1];

        if( 0 == ( child_pids[ worker_number ] = fork() ) ) {
            // child
            close( master_sockets[ worker_number ] );

            work( worker_sockets[ worker_number ], worker_number );

            shutdown( worker_sockets[ worker_number ], SHUT_RDWR );
            close( worker_sockets[ worker_number ] );
            exit(0);
        }
        else {
            // parent
            close( worker_sockets[ worker_number ] );

            worker_listeners[ worker_number ].worker_number = worker_number;
            ev_io_init( &( worker_listeners[ worker_number ].io ),
                    worker_read_cb, master_sockets[ worker_number ], EV_READ );
            ev_io_start( loop, &( worker_listeners[ worker_number ].io ) );

        }
    }

    server_socket = socket(
            AF_INET,
            SOCK_STREAM,
            0 );
    struct sockaddr_in server_sockaddr;
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons( 6666 );
    server_sockaddr.sin_addr.s_addr = htonl( INADDR_ANY );
    bind( server_socket, 
            (struct sockaddr *)(&server_sockaddr),
            sizeof( server_sockaddr ) );
    signal( SIGINT,  master_sig_term_catcher );
    signal( SIGTERM, master_sig_term_catcher );
    signal( SIGSEGV, master_sig_term_catcher );
    signal( SIGCHLD, master_sig_chld_catcher );
    signal( SIGPIPE, master_sig_term_catcher );
    listen( server_socket, SOMAXCONN );

    ev_io_init( &server_socket_listener,
            server_socket_read_cb, server_socket, EV_READ );
    ev_io_start( loop, &server_socket_listener );

    ev_run( loop, 0 );

    return 0;
}
