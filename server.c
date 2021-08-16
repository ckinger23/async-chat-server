/*
 * server.c 
 * Carter King
 * CS 330 Operating Systems
 * Dr. Larkins
 * Due February 28th, 2019
 *
 * This program creates the server side of an asynchronous chat client. This includes
 * a monitor aspect that is connected to a relay server by way of pipes.
 * The relay server uses a TCP socket connection to relay messages between
 * the server and multiple clients
 *
 * Sites used:
 *  https://linux.die.net/man/3/poll
 *  mainly just man pages and your help
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
#include <poll.h>
#define MAX_CLIENTS 12
//int C_enabled = 0;
//int N_enabled = 0;
// constants for pipe FDs
#define WFD 1
#define RFD 0



/*
 *  nonblock - a function that makes a file descriptor non-blocking
 *  @param fd file descriptor
 *  returns: void
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



/*
 * monitor() - provides a local chat window using pipes to read and write to the relay
 *  server as well as read and write to stdout and stdin.
 * @param srfd - server read file descriptor
 * @param swfd - server write file descriptor
 * returns: void
 */


void monitor(int srfd, int swfd) {
  int bytesRead, bytesWritten, pListen, cliHUP;
  int numFDs = 2;
  int timeout = -1;
  char buf[1024];
  char cliHUPBuf[] = "... just hung up";
  char serHUPBuf[] = "... just hung up";
  struct pollfd fds[2];
  //allows printing to stdout regardless of a buffer
  setbuf(stdout, NULL);

  //nonblock all FDs
  nonblock(STDOUT_FILENO);
  nonblock(STDIN_FILENO);
  nonblock(srfd);
  nonblock(swfd);

  //set up listening to user input and relay server
  fds[0].fd = STDIN_FILENO;
  fds[0].events = POLLIN;
  fds[1].fd = srfd;
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

        //check for EOF from STDIN
        if(bytesRead == 0){
          if(i == 0){
            fprintf(stdout, "Connection ended, Goodbye\n");
            exit(1);
          }
        }

        //if read came from relay server
        if (i == 1){
          bytesWritten = write(STDOUT_FILENO, buf, bytesRead);
          if(bytesWritten == -1){
            perror("mon write to stdout");
          }
        }
        else{
          //send out to relay what STDIN said
          bytesWritten = write(swfd, buf, bytesRead);
          if(bytesWritten == -1){
            perror("mon write to relay:");
          }
        }
      }
    }
  } while(1);
  return;
}


/*
 * server() - relays chat messages using pipes to the monitor, and reads and writes using
 *  sockets between the client program
 * @param mrfd - monitor read file descriptor
 * @param mwfd - monitor write file descriptor
 * @param portno - TCP port number to use for client connections
 * returns: void
 */


void server(int mrfd, int mwfd, int portno) {
  int bytesWritten, newClientFD, pollRet, portNetBytes, sockFID, lisCheck, setCheck, accSockFD, bytesRead;
  int listBacklog = 50;
  int timeout = 100;
  int val = 1;
  int numFDs = 2;
  char buf[1024];
  char huBuf[1024];
  char nameBuf[256];
  char * names[100];
  struct sockaddr_in sockAddr, clientAddr, newCliAddr;
  unsigned int sockSize = sizeof(sockAddr);
  unsigned int clientSize = sizeof(clientAddr);
  unsigned int newCliSize = sizeof(newCliAddr);
  struct pollfd fds[MAX_CLIENTS];

  //initialize all fds.fd to -1
  for(int b = 0; b < MAX_CLIENTS; b++){
    fds[b].fd = -1;
  }

  //Allows printing to stdout regardless of a buffer
  setbuf(stdout, NULL);

  //create a TCP socket
  sockFID = socket(AF_INET, SOCK_STREAM, 0);
  if(sockFID == -1){
    perror("socket()...");
    exit(1);
  }
  // port into network byte order
  portNetBytes = htons(portno);
  // set the sockets port to the given one
  sockAddr.sin_port = portNetBytes;
  //set the sockets address to any available interface
  sockAddr.sin_addr.s_addr = INADDR_ANY;
  //set socket family to Address Family
  sockAddr.sin_family = AF_INET;

  //allow the port to be re-used in quick concession
  setCheck = setsockopt(sockFID, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
  if(setCheck == -1){
    perror("setsockopt: ");
    exit(1);
  }


  //bind the newly created socket to an address in the new TCP port
  if((bind(sockFID,(struct sockaddr *) &sockAddr, sockSize)) == -1){
    perror("bind error: ");
    exit(1);
  }

  //listen for the client connection to socket
  lisCheck = listen(sockFID, listBacklog); 
  if(lisCheck == -1){
    perror("listen: ");
    exit(1);
  }



  //nonblock all FDs
  nonblock(sockFID);
  nonblock(mrfd);
  nonblock(mwfd);
  nonblock(STDIN_FILENO);
  nonblock(STDOUT_FILENO);

  //add FDs to listen to to fds array
  fds[0].fd = mrfd;
  fds[0].events = POLLIN;
  fds[1].fd = sockFID; 
  fds[1].events = POLLIN;

  //loop to listen for input
  do{
    pollRet = poll(fds, MAX_CLIENTS, timeout);
    if(pollRet < 0){
      perror("relay poll...");
      exit(1);
    }

    for(int i = 0; i < MAX_CLIENTS; i++){
      //loop through for input
      if(fds[i].revents & POLLIN){
        //Monitor Handler
        if(i == 0){
          bytesRead = read(mrfd, buf, sizeof(buf));  
          if(bytesRead == -1){
            if(errno != EWOULDBLOCK){
              perror("Reading from monitor");
              exit(1);
            }
          }
          //write out what monitor had to say to all clients
          for(int a = 2; a < MAX_CLIENTS; a ++){
            if(fds[a].fd != -1){ 
              write(fds[a].fd, buf, bytesRead);
            }
          }
        }
      //New Client Handler
        if(i == 1){
          newClientFD = accept(sockFID, (struct sockaddr *) &newCliAddr, &newCliSize);
            if(newClientFD == -1){
              if(errno != EWOULDBLOCK){
                perror("new client accept");
                exit(1);
              }
            }   
          //nonblock, add to fds, increment numFDs
          int fullcheck = 0;
          nonblock(newClientFD);
          for(int c = 2; c < MAX_CLIENTS; c++){
            if(fds[c].fd == -1){
              fds[c].fd = newClientFD;
              fds[c].events = POLLIN;
              fullcheck = 1;
              break;
            }
          }
          if(fullcheck == 1){
            numFDs ++;
          }
        }
      //Curr Client Handler
        if(i >= 2){
          bytesRead = read(fds[i].fd, buf, sizeof(buf));
          if(bytesRead == -1){
            if(errno != EWOULDBLOCK){
              perror("reading from client");
              exit(1);
            }
          }
          //client EOF = close and set as -1
          if(bytesRead == 0){
            close(fds[i].fd);
            fds[i].fd = -1;
            numFDs --;
          }
          //write out what was written to monitor and other clients
          if(bytesRead > 0){
            bytesWritten = write(mwfd, buf, bytesRead);
            for(int d = 2; d < MAX_CLIENTS; d++){
              if(fds[d].fd != -1 && fds[d].fd != fds[i].fd){
                bytesWritten = write(fds[d].fd, buf, bytesRead);
              }
            }
          } 
        }
      }
    }
  }while(1);
  for(int e = 0; e < MAX_CLIENTS; e++){
    close(fds[e].fd);
  }
}


int main(int argc, char **argv) {
  int opt, portNum;
  pid_t pid;
  int M2SFds[2];
  int S2MFds[2];


  if(argc > 3){
    perror("argument error: ");
    exit(1);
  }

  //getopt() to help if a -h or -p # added to compile
  while((opt = getopt(argc, argv, "p:nc")) != -1){
    switch(opt){
      case 'p':
        portNum = atoi(optarg);
        break;
      case 'n':
        // N_enabled = 1;
        //maintain a collection of nicknames
        break;
      case 'c':
        //enable connected/disconnected mode
        // C_enabled = 1;
        break;
      default:
        printf("usage: ./server [-h] [-p port #]\n"); 
        printf("%8s -p # - the port to use when connecting to the server\n", " ");
        exit(1);
    }

    // pipe from Monitor to Relay Server
    if(pipe(M2SFds) == -1){
      perror("Monitor to Server: ");
      exit(1);
    }

    //pipe from relay server to monitor
    if(pipe(S2MFds) == -1){
      perror("Server to Monitor: ");
      exit(1);
    }

    //nonblock all FDs
    for(int i = 0; i < 2; i++){
      nonblock(M2SFds[i]);
      nonblock(S2MFds[i]);
    }
    nonblock(STDOUT_FILENO);
    nonblock(STDIN_FILENO);

    //fork into parent and child process
    pid = fork();
    if(pid == -1){
      perror("Fork Error ");
      exit(1);
    }
    else if(pid == 0){
      //child process as monitor function
      close(M2SFds[RFD]);
      close(S2MFds[WFD]);
      monitor(S2MFds[RFD], M2SFds[WFD]);
      close(M2SFds[WFD]);
      close(S2MFds[RFD]);
    }
    else{
      //parent proces as relay server
      close(S2MFds[RFD]);
      close(M2SFds[WFD]);
      server(M2SFds[RFD], S2MFds[WFD], portNum);
      close(M2SFds[RFD]);
      close(S2MFds[WFD]);
      pid = wait(NULL); 
    }
    return 0;
  }
}

