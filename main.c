#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/file.h> // flock

#include "fd_pass1.h"

#define NUM_WORKER_THREADS 3

int server_socket;

void my_sig( int signo ) {
    shutdown( server_socket, SHUT_RDWR );
    close( server_socket );
    exit(0);
}

int main( int argc, char **argv ) {

    int master_sockets[ NUM_WORKER_THREADS ];
    int worker_sockets[ NUM_WORKER_THREADS ];
    int child_pids[ NUM_WORKER_THREADS ];

    for( int w_thr_num = 0; w_thr_num < NUM_WORKER_THREADS; ++w_thr_num ) {
        int fd[2];
        if( 0 != socketpair( AF_UNIX, SOCK_STREAM, 0, fd ) ) {
            fprintf( stderr, "Failed to create UNIX socket for worker %d\n", 
                    w_thr_num );
            exit( 1 );
        }
        master_sockets[w_thr_num] = fd[0];
        worker_sockets[w_thr_num] = fd[1];

        if( 0 == ( child_pids[ w_thr_num ] = fork() ) ) {
            // child
            close( master_sockets[ w_thr_num ] );

            int fd = recv_file_descriptor( worker_sockets[ w_thr_num ] );
            if( fd <= 0 ) {
                printf( "Worker %d could not recv file descriptor!\n", w_thr_num );
            }
            else {
                char buf[50];
                sprintf( buf, "Hello from worker %d!\n", w_thr_num );
                flock( fd, LOCK_EX );
                write( fd, buf, strlen( buf ) );
                flock( fd, LOCK_UN );
                close( fd );
            }

            shutdown( worker_sockets[ w_thr_num ], SHUT_RDWR );
            close( worker_sockets[ w_thr_num ] );
            exit(0);
        }
        else {
            // parent
            close( worker_sockets[ w_thr_num ] );

            int fd = open( "/tmp/fdpass", O_WRONLY|O_CREAT|O_TRUNC|O_APPEND );
            send_file_descriptor( master_sockets[ w_thr_num ], fd );
            close(fd);

            shutdown( master_sockets[ w_thr_num ], SHUT_RDWR );
            close( master_sockets[ w_thr_num ] );
        }
    }
    exit(0);

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
    signal( SIGINT, my_sig );
    signal( SIGTERM, my_sig );
    signal( SIGSEGV, my_sig );
    listen( server_socket, SOMAXCONN );

    while(1) {
        int client_socket = accept( server_socket, 0, 0 );
        fprintf( stderr, "Got a connection\n" );
        //work( client_socket );
        shutdown( client_socket, SHUT_RDWR );
        close( client_socket );
    }

    return 0;
}
