#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define GET_BUF_SZ 1024
#define FILE_READ_BUF_SZ 1024

int server_socket;

void my_sig( int signo ) {
    shutdown( server_socket, SHUT_RDWR );
    close( server_socket );
    exit(0);
}

char *get_file_name( int client_socket ) {
    char *buf = (char*)malloc( GET_BUF_SZ );
    ssize_t num_read = recv( client_socket, buf, GET_BUF_SZ, MSG_NOSIGNAL );
    char *space1 = index( buf, ' ' );
    while( ' ' == *(++space1) && space1 < buf + GET_BUF_SZ );
    if( space1 != NULL && ( space1 - buf ) + 3 < GET_BUF_SZ ) {
        char *space2 = index( space1, ' ' );
        if( space2 != NULL ) {
            *space2 = 0;
            strcpy( buf, space1 );
            fprintf( stderr, "Requested file: %s\n", buf );
            return buf;

        }
    }
    free( buf );
    return NULL;
}

void work( int client_socket ) {
    char *req_file_name = get_file_name( client_socket );
    if( req_file_name == NULL )
        return;
    FILE *req_file = fopen( req_file_name, "r" );
    if( req_file != NULL ) {
        char *buf = (char*)malloc( FILE_READ_BUF_SZ );
        size_t num_read;
        while( 0 != ( num_read = 
                    fread( buf, 1, FILE_READ_BUF_SZ, req_file ) ) ) {
            send( client_socket, buf, num_read, MSG_NOSIGNAL );
        }
        fclose( req_file );
        free( buf );
    }
    free( req_file_name );
}

int main( int argc, char **argv ) {
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
        work( client_socket );
        shutdown( client_socket, SHUT_RDWR );
        close( client_socket );
    }

    return 0;
}
