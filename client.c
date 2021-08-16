/*
 * client.c
 * Carter King
 * CS 330 Operating Systems
 * Dr. Larkins
 * Due February 28th, 2019
 *
 * This program is the client of an asynchronous chat server. It takes in a portal number
 * and host name to connect to the server connected to the other side of the socket. From 
 * STDIN and STDOUT, chats are sent back and forth between the server and 
 * clients
 *
 * Sites used:
 * https://linux.die.net/man/3/poll
 * mainly just used your help and manual pages
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <netdb.h>
#include <poll.h>

/*
 * nonblock - a function that makes a file descriptor non-blocking
 * @param fd file descriptor
 * returns: void
 */

void nonblock(int fd) {
  int flags;

  if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
    perror("fcntl (get):");
    exit(1);
  }
  if (fcntl(fd, F_SETFL, flags | FNDELAY) == -1) {
    perror("fcntl (set):");
    exit(1);
  }
}


int main(int argc, char **argv) {
  int pListen, opt, portNum, reuseCheck, sockFID, portNetBytes, connCheck, bytesRead, stdBytesRead, bytesWritten;
  int val = 1;
  int numFDs = 2;
  int timeout = -1;
  char buf[1024];
  char * hostConn = "senna.rhodes.edu";
  char * nickname = NULL;
  //char huBuf[1024];
  struct sockaddr_in sockAddr;
  struct hostent* host;
  unsigned int sockLen = sizeof(sockAddr);
  char hostName[128];
  struct pollfd fds[numFDs];


  //allow stdout to print regardless of a buffer
  setbuf(stdout, NULL);

  //Intake of command line arguments
  while((opt = getopt(argc, argv, "h:p:n:")) != -1){
    switch(opt){
      case 'h':
        hostConn = optarg;
        break;
      case 'p':
        portNum = atoi(optarg);
        break;
      case 'n':
       // nickname = optarg;
        break;
      default:
        printf("Ya messed up your parameters \n");
        exit(1);
    }
  }


  //create a TCP socket
  sockFID = socket(AF_INET, SOCK_STREAM, 0);
  if(sockFID == -1){
    perror("Messed up creating client socket:");
    exit(1);
  }


  host = gethostbyname(hostConn);
  if(host == NULL){
    perror("getting host name:");
    exit(1);
  }
  // port into network byte order
  portNetBytes = htons(portNum);
  // set the sockets port to the given one
  sockAddr.sin_port = portNetBytes;
  //set socket type to AF_INET
  sockAddr.sin_family = AF_INET;
  //connect the socket to the server
  memcpy(&sockAddr.sin_addr.s_addr, host->h_addr, host->h_length);


  //connect socket to proper address on server side
  connCheck = connect(sockFID, (struct sockaddr *) &sockAddr, sockLen);
  if(connCheck == -1){
    perror("connect: ");
    exit(1);
  }
  //nonblock all FDs
  nonblock(sockFID);
  nonblock(STDIN_FILENO);
  nonblock(STDOUT_FILENO);

  //connection success and prompt user
  fprintf(stdout, "Connected to Server...\n>");
 
  // going to listen to user-input and relay server
  fds[0].fd = STDIN_FILENO;
  fds[0].events = POLLIN;
  fds[1].fd = sockFID;
  fds[1].events = POLLIN;
 

  do{
  
    //call poll to listen for read
    pListen = poll(fds, numFDs, timeout);

    //check if error polling
    if(pListen < 0){
      perror("monitor poll: ");
      exit(1);
    }

    //loop through set of FDs
    for(int i = 0; i < numFDs; i++){

      //specific FD has data to be read
      if(fds[i].revents & POLLIN){
        bytesRead = read(fds[i].fd, buf, sizeof(buf));
        
        if(bytesRead == -1){
          perror("reading from pollin file: ");
          exit(1);
        }
        
        if(bytesRead == 0){
          if(i == 0){
            fprintf(stdout, "hanging up\n");
            bytesWritten = write(fds[1].fd, buf, bytesRead);
            exit(1);
          }
          else{
            fprintf(stdout, "hanging up\n");
            exit(1);
          }
        }

        //STDIN to socket
        if (i == 0){
          bytesWritten = write(fds[1].fd, buf, bytesRead);
          if(bytesWritten == -1){
            perror("writing to socket");
          }
        }
        //socket to stdout
        else{
          bytesWritten = write(STDOUT_FILENO, buf, bytesRead);
          if(bytesWritten == -1){
            perror("writing to std out");
          }
        }
      }
     
  }
 } while(1);
  close(sockFID);
  close(fds[1].fd);
  return 0;
}

