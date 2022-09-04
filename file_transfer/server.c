//
// Created by lijiani9 on 1/25/22.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
// include library part referred to the Beej's Section 6.3 Datagram Sockets

struct packet {
    unsigned int total_frag;
    unsigned int frag_no;
    unsigned int size;
    char* filename;
    char filedata[1000];
};
void receive_file_from_deliver(int socket_fd, struct sockaddr_storage client_addr, socklen_t addrlen);
char packet_str[2048];

int
main(int argc, char **argv) {
    // step 0: variable declaration
    int socket_fd = 0;
    struct sockaddr_in server_addr;
    // make sure the struct is empty as said in the Beej's book
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    int port_num = 0;
    char buffer[512] = {0};
    struct sockaddr_storage client_addr;
    socklen_t addrlen = sizeof(struct sockaddr_storage);

    // step 1: handle the pass-in
    if (argc != 2 || (argc >= 1 && strcmp(argv[0], "server")!=0)) {
        printf("wrong pass-in.\n");
        return -1;
    }
    port_num = atoi(argv[1]);

    // step 2: create socket
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    // step 3: bind socket with server addr
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port_num);
    int rt_bind = bind(socket_fd, (struct sockaddr *) &server_addr, sizeof(struct sockaddr_in));
    if (rt_bind != 0){ // return and choose another port number
        printf("bind fail\n");
        return -1;
    }

    // step 4: recvfrom
    ssize_t rt_recvfrom = recvfrom(socket_fd, buffer, 512, 0, (struct sockaddr *) &client_addr, &addrlen);
    if (rt_recvfrom <= 0) {
        printf("recvfrom fail\n");
        return -1;
    }

    // step 5: compare result
    int send_yes = 0;
    if (strncmp(buffer, "ftp", 512)==0) {
        send_yes = 1;
    }

    // step 6: sendto client
    ssize_t rt_sendto = 0;
    if (send_yes) {
        rt_sendto  = sendto(socket_fd, "yes", strlen("yes"), 0, \
        (struct sockaddr *) &client_addr, sizeof(struct sockaddr_storage));
    } else {
        rt_sendto = sendto(socket_fd, "no", strlen("no"), 0, \
        (struct sockaddr *) &client_addr, sizeof(struct sockaddr_storage));
    }
    if (rt_sendto <= 0) {
        printf("sendto failed\n");
        return -1;
    } else {
        // receive file from deliver
        receive_file_from_deliver(socket_fd, client_addr, addrlen);
    }

    // step 7: close socket
    close(socket_fd);
    return 0;
}

// receive file steps:
void receive_file_from_deliver(int socket_fd, struct sockaddr_storage client_addr, socklen_t addrlen) {
    // 0. declaration
    struct packet p;
    int file_created = 0;
    int fd;
    // 1. when not done, receive the packet message from deliver
    while (1) {
        ssize_t rt_recvfrom = recvfrom(socket_fd, packet_str, 2048, 0, (struct sockaddr *) &client_addr, &addrlen);
        if (rt_recvfrom <= 0) {
            printf("recvfrom fail in receive file\n");
            return;
        }
        // 2. transfer the packet message to a packet
        memset(&p, 0, sizeof(struct packet));
        char * token;
        token = strtok(packet_str, ":");
        p.total_frag = strtoul(token, NULL, 10); // size of unsigned int = 10
        token = strtok(NULL, ":");
        p.frag_no = strtoul(token, NULL, 10);
        token = strtok(NULL, ":");
        p.size = strtoul(token, NULL, 10);
        token = strtok(NULL, ":");
        p.filename = (char*) malloc(256*sizeof(char));
        strncpy(p.filename, token, 256);
        token = token + strlen(p.filename) + 1; // move to the file data
        int file_size = p.size * sizeof(char);
        memcpy(p.filedata, token, file_size);

        // 3. create new file or update the current file
        if (!file_created) {
            fd = open(p.filename, O_WRONLY|O_TRUNC|O_CREAT, S_IRWXO|S_IRWXG|S_IRWXU);
            file_created = 1;
        }

        // 4. write to new file
        int rt_write = write(fd, p.filedata, p.size);
        if (rt_write < 0) {
            printf("error in write\n");
            return;
        }

        // 5. send ack back to deliver, ack number = frag_no
        // reset packet string
        memset(packet_str, 0, 2048);
        sprintf(packet_str, "%u", p.frag_no);
        int rt_sendto = sendto(socket_fd, packet_str, 2048, 0, (struct sockaddr *) &client_addr, sizeof(struct sockaddr_storage));
        if (rt_sendto <= 0) {
            printf("send to fail in receive file from deliver\n");
            return;
        }

        // 6. check if end or not
        if (p.frag_no == p.total_frag) { // finished sending all fragments
            break;
        }

    }
    printf("file is received from deliver successfully\n");
}