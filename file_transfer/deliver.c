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
#include <time.h>
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
void send_file_to_server(char * file_name, int socket_fd, struct sockaddr_in server_addr, socklen_t addrlen);
char packet_str[2048];

int
main(int argc, char **argv) {
    // step 0: declaration
    int port_num = 0;
    int socket_fd = 0;
    char protocol_name[512] = {0};
    char file_name[512] = {0};
    struct sockaddr_in server_addr;
    // make sure the struct is empty as said in the Beej's book
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    char buffer[512] = {0};
    socklen_t addrlen = sizeof(struct sockaddr_in);

    ////////// section 2 ///////////
    clock_t start_time, end_time;
    double RTT;

    // step 1: handle pass-in
    if (argc != 3 || (argc >= 1 && strcmp(argv[0], "deliver")!=0)) {
        printf("wrong pass-in.\n");
        return -1;
    }
    port_num = atoi(argv[2]);

    // step 2: create socket
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    // step 3: ask user to passin filename
    printf("enter protocol <file name>\n");
    scanf("%s %s", protocol_name, file_name);
    //if (strcmp(ftp, "ftp")!=0) {
        //printf("should enter ftp\n");
        //return -1;
    //}

    // step 4: check file existence
    if (access(file_name, F_OK) != 0) {
        printf("file not exists\n");
        return -1;
    }

    // step 4: sendto server
    int rt_inet_aton = inet_aton(argv[1], &server_addr.sin_addr);
    // check if server addr is valid
    if (rt_inet_aton == 0) {
        printf("server addr not valid\n");
        return -1;
    }
    server_addr.sin_port = htons(port_num);
    server_addr.sin_family = AF_INET;
    //time(&start_time);
    start_time = clock();
    ssize_t rt_sendto = sendto(socket_fd, protocol_name, 512, 0, (struct sockaddr *) &server_addr, sizeof(struct sockaddr_in));
    if (rt_sendto <= 0) {
        printf("send to fail\n");
        return -1;
    }

    // step 5: recvfrom
    ssize_t rt_recvfrom = recvfrom(socket_fd, buffer, 512, 0, (struct sockaddr *) &server_addr, &addrlen);
    //time(&end_time);
    end_time = clock();
    RTT = (double) (end_time - start_time) / CLOCKS_PER_SEC;
    printf("Round trip time is: %f\n", RTT);
    if (rt_recvfrom <= 0) {
        printf("recvfrom fail\n");
        return -1;
    }

    // step 6: compare result
    if (strcmp(buffer, "yes") == 0) {
        printf("A file transfer can start\n");

        // start to transfer file
        send_file_to_server(file_name, socket_fd, server_addr, addrlen);

    } else {
        printf("buffer is not yes\n");
        return -1;
    }

    // step 7: close socket
    close(socket_fd);
    return 0;
}

// send file steps:
void send_file_to_server(char * file_name, int socket_fd, struct sockaddr_in server_addr, socklen_t addrlen) {
    // 0. declaration
    struct packet p;
    // 1. calculate number of fragments
    FILE * file = fopen(file_name, "rb");
    if (file == NULL) {
        printf("fopen fail\n");
        return;
    }
    fseek(file, 0, SEEK_END);
    int total_num_frag = ftell(file)/1000 + 1;
    rewind(file); // reset the pointer of the file stream

    int file_fd = open(file_name, O_RDONLY);

    // 2. for each fragment, create packets
    for (int i=1; i<total_num_frag+1; i++) {
        memset(&p, 0, sizeof(struct packet));
        p.frag_no = i;
        p.total_frag = total_num_frag;
        p.filename = (char*) malloc(256*sizeof(char));
        strncpy(p.filename, file_name, 256);
        // read the file
        int rt_read = read(file_fd, p.filedata, 1000);
        p.size = rt_read;

        // 3. transfer packets to strings
        memset(packet_str, 0, 2048);
        // store all entries except file data
        sprintf(packet_str, "%u:%u:%u:%s:", p.total_frag, p.frag_no, p.size, p.filename);
        // store file data using memcpy
        int file_size = 1000 * sizeof(char);
        memcpy(packet_str + strlen(packet_str), p.filedata, file_size);

        // 4. send strings to server
        int rt_sendto = sendto(socket_fd, packet_str, 2048, 0, (struct sockaddr *) &server_addr, sizeof(struct sockaddr_in));
        if (rt_sendto <= 0) {
            printf("send to fail in send file to server\n");
            return;
        }

        // 5. receive ack
        int rt_recvfrom = recvfrom(socket_fd, packet_str, 2048, 0, (struct sockaddr *) &server_addr, &addrlen);
        if (rt_recvfrom <= 0) {
            printf("receive from fail in send file to server\n");
            return;
        }

        // 6. check ack
        int ack = strtoul(packet_str, NULL, 10);
        if (ack == p.frag_no) {
            printf("ack is correct, same as frag_no\n");
        } else {
            printf("ack is not correct, ack: %u, frag_no: %u", ack, p.frag_no);
        }
    }
    printf("file transfer is done successfully\n");
    close(file_fd);
}