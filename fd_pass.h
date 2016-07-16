int
send_file_descriptor(
     int socket, /* Socket through which the file descriptor is passed */
     int fd_to_send);/* File descriptor to be passed, could be another socket */

int
recv_file_descriptor(
     int socket); /* Socket from which the file descriptor is read */
