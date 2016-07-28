#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "fd_pass.h"

#define REQ_BUF_SZ 1024
#define ANS_BUF_SZ 1024
#define FILE_READ_BUF_SZ 1024

int server_socket;

void my_sig( int signo ) {
    shutdown( server_socket, SHUT_RDWR );
    close( server_socket );
    exit(0);
}

int send_header( int client_socket, int http_code, unsigned long content_length ) {
    char *ans = (char*)malloc( ANS_BUF_SZ );
    strcpy( ans, "HTTP/1.0 " );
    switch( http_code ) {
        case 200:
            strcat( ans, "200 OK\n" );
            break;
        case 404:
            strcat( ans, "404 Not Found\n" );
            break;
        default:
            strcat( ans, "500 Internal Server Error\n" );
            break;
    }
    sprintf( ans + strlen( ans ), "Content-length: %lu\r\n", content_length );
    strcat( ans, "Content-Type: text/html\r\n" );
    strcat( ans, "\r\n" );
    ans[ ANS_BUF_SZ - 1 ] = 0;
    if( -1 == send( client_socket, ans, strlen(ans), MSG_NOSIGNAL ) )
        return -1;
    return 0;
}

char *get_file_name( int client_socket ) {
    char *buf = (char*)malloc( REQ_BUF_SZ );
    ssize_t num_read = recv( client_socket, buf, REQ_BUF_SZ - 1, MSG_NOSIGNAL );
    if( num_read < 3 ) {
        free( buf );
        return NULL;
    }
    *( buf + num_read ) = 0;
    char *space1 = index( buf, ' ' );
    while( ' ' == *(++space1) && space1 < buf + num_read );
    while( '/' == *space1 && space1 < buf + num_read )
        ++space1; // skip starting slashes
    if( space1 != NULL && ( space1 - buf ) + 3 < num_read ) {
        char *space2 = index( space1, ' ' );
        char *special = index( space1, '?' );
        if( special != NULL && special < space2 || space2 == NULL )
            space2 = special;
        if( space2 != NULL ) {
            *space2 = 0;
            memmove( buf, space1, space2 - space1 + 1 );
            fprintf( stderr, "Requested file: %s\n", buf );
            return buf;

        }
    }
    free( buf );
    return NULL;
}

void work( int client_socket ) {
    char *req_file_name = get_file_name( client_socket );
    if( req_file_name == NULL ) {
        fprintf( stderr, "Requested file not found! (404)\n" );
        send_header( client_socket, 404, 0 );
        return;
    }
    FILE *req_file = fopen( req_file_name, "r" );
    if( req_file != NULL ) {
        fseek( req_file, 0L, SEEK_END );
        unsigned long fsize = ftell( req_file );
        rewind( req_file );
        fprintf( stderr, "Sending the file (200)\n" );
        send_header( client_socket, 200, fsize );
        char *buf = (char*)malloc( FILE_READ_BUF_SZ );
        size_t num_read;
        while( 0 != ( num_read = 
                    fread( buf, 1, FILE_READ_BUF_SZ, req_file ) ) ) {
            send( client_socket, buf, num_read, MSG_NOSIGNAL );
        }
        fclose( req_file );
        free( buf );
    }
    else {
        fprintf( stderr, "Requested file not found! (404)\n" );
        send_header( client_socket, 404, 0 );
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
