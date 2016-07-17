#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ev.h>
#include <sys/wait.h>
#include <getopt.h>
#include <arpa/inet.h>
//#include <sys/types.h>
//#include <sys/file.h> // flock

#include "fd_pass.h"
#include "worker.h"

//#define DEBUG_OUTPUT
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
#ifdef DEBUG_OUTPUT
            fprintf( stderr, "Got SIGCHLD from %d worker\n", worker_number );
#endif
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
#ifdef DEBUG_OUTPUT
            printf("Worker %d says it's ready\n", worker_number );
#endif
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
#ifdef DEBUG_OUTPUT
    fprintf( stderr, "Got a connection\n" );
#endif
    int next_worker = -1;
    for( int worker_number = 0; worker_number < NUM_WORKER_THREADS;
            ++worker_number ) {
        if( worker_status[ worker_number ] == 1 ) {
            next_worker = worker_number;
            break;
        }
    }
    int client_socket = accept( server_socket, 0, 0 );
#ifdef DEBUG_OUTPUT
    fprintf( stderr, "Passing it to worker %d\n", next_worker );
#endif
    worker_status[ next_worker ] = 0; // now it's busy untl it says the opposit
    --num_threads_ready;
    if( num_threads_ready == 0 )
        ev_io_stop( loop, &server_socket_listener );
    send_file_descriptor( master_sockets[ next_worker ], client_socket );
    close( client_socket );
}




int main( int argc, char **argv ) {
    signal( SIGHUP,  SIG_IGN );

    int option;
    char correct_invocation = 3;
	opterr = 0;
    struct sockaddr_in server_sockaddr;
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = htonl( INADDR_ANY );

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"host",       required_argument, 0,  'h' },
            {"port",       required_argument, 0,  'p' },
            {"directory",  required_argument, 0,  'd' }
        };

        option = getopt_long(argc, argv, "h:p:d:",
                 long_options, &option_index);
        if( option == -1 )
            break; // all options parsed

        switch( option ) {
            case 'h':
                if( 0 == inet_aton( optarg, &server_sockaddr.sin_addr ) ) {
                    fprintf( stderr, "Bad ip address specification!\n" );
                    exit(1);
                }
                correct_invocation &= ~1; // first byte for good ip
                break;
			case 'p':
                {
                int port = atoi( optarg );
                if( port <= 0 ) {
                    fprintf( stderr, "Bad port specification!\n" );
                    exit(1);
                }
                server_sockaddr.sin_port = htons( port );
                correct_invocation &= ~2; // second byte for good port
                break;
                }
			case 'd':
                if( -1 == chdir( optarg ) ) {
                    fprintf( stderr, "Bad working directory specification!\n" );
                    exit(1);
                }
				break;
            case '?':
            default: 
                correct_invocation |= 2;
        }

	} // end of option parcing cycle
    if( 0 != correct_invocation ) {
        fprintf( stderr, "Missing mandatory options!\n" );
        exit(1);
    }

#ifndef DEBUG_OUTPUT
    if( 0 != daemon( 1 /* do not chdir */, 0 /* detach terminal */ ) ) {
        fprintf( stderr, "Failed daemonizing!\n" );
        exit(1);
    }
#endif

    struct ev_loop *loop = EV_DEFAULT;

    num_threads_ready = 0;
    int worker_sockets[ NUM_WORKER_THREADS ];
    setsid();

    for( int worker_number = 0; worker_number < NUM_WORKER_THREADS; ++worker_number ) {
        worker_status[ worker_number ] = 0; // not ready
        int fd[2];
        if( 0 != socketpair( AF_UNIX, SOCK_STREAM, 0, fd ) ) {
#ifdef DEBUG_OUTPUT
            fprintf( stderr, "Failed to create UNIX socket for worker %d\n", 
                    worker_number );
#endif
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

    if( -1 == ( server_socket = socket(
                AF_INET,
                SOCK_STREAM,
                0 ) ) ) {
#ifdef DEBUG_OUTPUT
        fprintf( stderr, "Failed opening a socket!\n" );
#endif
        master_sig_term_catcher( SIGTERM );
        exit(0);
    }
    if( -1 == bind( server_socket, 
                (struct sockaddr *)(&server_sockaddr),
                sizeof( server_sockaddr ) ) ) {
#ifdef DEBUG_OUTPUT
        fprintf( stderr, "Bind socket to %s:%d\n failed!\n", 
                inet_ntoa( server_sockaddr.sin_addr ),
                ntohs( server_sockaddr.sin_port ) );
#endif
        master_sig_term_catcher( SIGTERM );
        exit(0);
    }
    signal( SIGINT,  master_sig_term_catcher );
    signal( SIGTERM, master_sig_term_catcher );
    signal( SIGSEGV, master_sig_term_catcher );
    signal( SIGPIPE, master_sig_term_catcher );
    signal( SIGCHLD, master_sig_chld_catcher );
    if( -1 == listen( server_socket, SOMAXCONN ) ) {
#ifdef DEBUG_OUTPUT
        fprintf( stderr, "Listening on %s:%d\n failed!\n", 
                inet_ntoa( server_sockaddr.sin_addr ),
                ntohs( server_sockaddr.sin_port ) );
#endif
        master_sig_term_catcher( SIGTERM );
        exit(0);
    }

#ifdef DEBUG_OUTPUT
    fprintf( stderr, "Listening on %s:%d\n", 
            inet_ntoa( server_sockaddr.sin_addr ),
            ntohs( server_sockaddr.sin_port ) );
#endif

    ev_io_init( &server_socket_listener,
            server_socket_read_cb, server_socket, EV_READ );
    ev_io_start( loop, &server_socket_listener );

    ev_run( loop, 0 );

    return 0;
}
