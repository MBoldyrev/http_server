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

int client_socket;
FILE *logfile;

static void worker_sig_term_catcher( int signo ) {
    if( client_socket >= 0 ) {
        shutdown( client_socket, SHUT_RDWR );
        close( client_socket );
    }
    exit(0);
}

static int send_header( int http_code, unsigned long content_length ) {
    char *ans = (char*)malloc( ANS_BUF_SZ );
    strcpy( ans, "HTTP/1.0 " );
    switch( http_code ) {
        case 200:
            strcat( ans, "200 OK\r\n" );
            break;
        case 404:
            strcat( ans, "404 Not Found\r\n" );
            break;
        default:
            strcat( ans, "500 Internal Server Error\r\n" );
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

static char *get_file_name() {
    char *buf = (char*)malloc( REQ_BUF_SZ );
    ssize_t num_read = recv( client_socket, buf, REQ_BUF_SZ, MSG_NOSIGNAL );
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
            fprintf( logfile, "Requested file: %s\n", buf );
            return buf;

        }
    }
    free( buf );
    return NULL;
}

void work( int control_socket, int worker_number ) {
    signal( SIGINT,  worker_sig_term_catcher );
    signal( SIGTERM, worker_sig_term_catcher );
    signal( SIGSEGV, worker_sig_term_catcher );
    signal( SIGPIPE, worker_sig_term_catcher );

    while( 1 ) { 
        char ready_code = 1;
        send( control_socket, &ready_code, 1, MSG_NOSIGNAL );
        client_socket = recv_file_descriptor( control_socket );
        if( client_socket < 0 ) {
            fprintf( logfile, "Worker %d: Failed receiving client socket!\n", 
                    worker_number );
            continue;
        }
        fprintf( logfile, "Worker %d: Received client socket fd: %d\n", 
                    worker_number, client_socket );
        char *req_file_name = get_file_name( client_socket );
        if( req_file_name == NULL ) {
            fprintf( logfile, "Requested file not found! (404)\n" );
            send_header( 404, 0 );
            return;
        }
        FILE *req_file = fopen( req_file_name, "r" );
        if( req_file != NULL ) {
            fseek( req_file, 0L, SEEK_END );
            unsigned long fsize = ftell( req_file );
            rewind( req_file );
            fprintf( logfile, "Sending the file (200)\n" );
            send_header( 200, fsize );
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
            fprintf( logfile, "Requested file not found! (404)\n" );
            send_header( 404, 0 );
        }
        free( req_file_name );
        shutdown( client_socket, SHUT_RDWR );
        close( client_socket );
        client_socket = -1; // for signal processing in worker_sig_term_catcher
    }
}

