#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <signal.h>
#include <semaphore.h>

#include <string.h>

#define PORT 5576
#define BUFFER_SIZE 512
// #define SHM_SIZE 8 // stores one long int
#define SHM_NAME "COMMON"

void reap_children(int sig){
    (void) sig;
    int status;
    pid_t pid;
    char buffer[128];
    while((pid = waitpid(-1,&status,WNOHANG)) > 0){
        if(WIFEXITED(status)){
            snprintf(buffer,sizeof(buffer),"[Parent] Child %d exited with status %d\n", pid, WEXITSTATUS(status));
        }
        else if(WIFSIGNALED(status)){
            snprintf(buffer,sizeof(buffer),"[Parent] Child process %d terminated by signal %d\n", pid, WTERMSIG(status));
        }

        write(STDOUT_FILENO,buffer,strlen(buffer));
    }
}

/**
 * USE STRUCT CONSISTING OF COUNTER AND BINARY SEMAPHORE
 * OTHERWISE POINTER ARITHMETIC HAS TO DONE MANUALLY FOR THE
 * SHARED REGION. STRUCT IS A SAFER OPTION, COMPILER TAKES CARE
 * OF THINGS
 */

struct shared {
    long int counter;
    sem_t lock;
};
int main(){
    // buffer array to receive data from client 
    char buffer[BUFFER_SIZE] = {0};

    /**
     * REGISTERING SIGNAL HANDLER
     */
    // registering SIGCHILD handler using sigaction
    // this handler will fire when kernel sends the signal
    // to the parent stating the child process has finished its execution
    struct sigaction sa;

    sa.sa_handler = reap_children; // asign the handler function
    sigemptyset(&sa.sa_mask); // clear mask (block no extra signals)
    sa.sa_flags = SA_RESTART; // automatically restart interrupted system calls.   

    int sigaction_status = sigaction(SIGCHLD,&sa,NULL);
    if(sigaction_status < 0){
        perror("sigaction");
        return 1;
    }

    /**
     * SOCKET STUFF 
     */
    // initalizing the semaphore
    // create a socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket < 0 ){
        perror("socket");
        return 1;
    }

    // assign address to socket
    struct sockaddr_in server_socket_address;
    server_socket_address.sin_family = AF_INET;
    server_socket_address.sin_port = htons(PORT);
    server_socket_address.sin_addr.s_addr = INADDR_ANY;

    // allow server to reuse address
    int opt = 1;
    int setsockopt_status = setsockopt(server_socket,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    if(setsockopt_status < 0){
        perror("setsockopt");
        return 1;
    }

    // bind the socket
    int bind_status = bind(server_socket,(struct sockaddr *)&server_socket_address, sizeof(server_socket_address));
    if(bind_status < 0){
        perror("bind");
        return 1;
    }

    // listen for clients
    int listen_status = listen(server_socket, 5);
    if(listen_status < 0){
        perror("listen");
        return 1;
    }

    /**
     * SHARED MEMORY
     */
    // creating a shared memory object whose size is 0
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if(shm_fd < 0){
        perror("shm_open");
        return 1;
    }

    // setting up shared memory
    struct shared * shared_struct;
    const long int SHM_SIZE = sizeof(*shared_struct);

    // assigning size to the shared memory
    int trunc_stat = ftruncate(shm_fd,SHM_SIZE);
    if(trunc_stat < 0){
        perror("ftruncate");
        shm_unlink(SHM_NAME);
        return 1;
    }

    // map shm object to this process so child process inherits the mapping
    // and both process have the same physical page
    void * ptr = mmap(0,SHM_SIZE,PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(ptr == MAP_FAILED){
        perror("mmap");
        shm_unlink(SHM_NAME);
        return 1;
    }
    
    
    
    // casting void ptr to a long int ptr
    // and initializing memory 
    shared_struct = ptr;
    shared_struct->counter = 0;
    int sem_init_status = sem_init(&shared_struct->lock,1,1);
    if(sem_init_status < 0){
        perror("sem_init");
        return 1;
    }

    while(1){
       

        struct sockaddr_in client_address;
        socklen_t client_address_len = sizeof(client_address);

        // accept the client if there is any, this is a blocking call
        int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_len);
        if(client_socket < 0){
            perror("accept");
            return 1;
        }

        // once a client is accepted create a child process
        // to service the client
        pid_t pid = fork();
        if(pid == 0){
            // child process 
            // close the server socket for the child as it is not used here
            // the child has its own copy of the descriptor but the kernel keeps a count of how many references are made to that object
            close(server_socket);

            // receive the data from the client
            ssize_t recv_bytes, sent_bytes; // keep a track of received and sent bytes

            // keep a flag to stop sendin if there are any errors while sending data
            int stop_recv = 0;

            // this is the echo loop. Client can send multiple bytes
            // which can be greater than the size of the buffer
            // so the loop receives the data in chunks and also sends it
            // back in chunks until all the data in stream is received and sent 
            // back to the client 
            while(1){
                ssize_t sent = 0;
                recv_bytes = recv(client_socket,buffer,BUFFER_SIZE,0);

                if(recv_bytes == 0) break;

                if(recv_bytes < 0){
                    perror("recv");
                    break;
                }

                // send all the data that is received and not the entire buffer
                // as buffer may contain old data. Furthermore, send can be interrupted
                // by a signal so use a loop to send all the data
                while(sent < recv_bytes){
                    sent_bytes = send(client_socket,buffer + sent,recv_bytes - sent, 0);
                    if(sent_bytes < 0){
                        perror("send");
                        stop_recv = 1;
                        break;
                    }

                    sent += sent_bytes;
                }

                if(stop_recv) break; // stop the recevivng loop if any errors occur while sending

                memset(buffer,0,BUFFER_SIZE); // reset the buffer after sending all the data
            }

            // close the client connection
            close(client_socket);

            // increment number of clients in shared memory
            // *shared += 1;
            // usleep(1000);
            // *shared = temp + 1;
            sem_wait(&shared_struct->lock);
            long int temp = shared_struct->counter;
            usleep(1000);
            shared_struct->counter += 1;
            printf("Served Client #%ld (pid %d)\n", temp + 1, getpid());
            sem_post(&shared_struct->lock);

            // return back to parent without any errors
            return 0;
        }
        
        // closing client socket from the parent process
        close(client_socket);

        // reap the child processes
        /*
            * Reaping here, after accept(), leaves zombies until the next client connects.
            *
            * Client 1 connects: the parent forks child 1 and runs this loop. Child 1 is
            * still serving, so waitpid() returns 0 and the parent goes back to accept(),
            * where it blocks. Client 1 disconnects and child 1 exits, but the parent is
            * stuck in accept() and does not run this loop, so child 1 stays a zombie.
            * Only when client 2 connects does accept() return and this loop reap child 1.
            *
            * So children that have exited are reaped only when the next client connects.
            * If no client comes, the last one stays a zombie indefinitely. The loop has to
            * be a while loop because several children may have exited in the meantime.
            *
            * The fix is a SIGCHLD handler. When a child exits, the kernel sends SIGCHLD to
            * the parent; the handler runs immediately, even if the parent is blocked in
            * accept(), and calls waitpid() there.
 */

        // while(waitpid(-1,NULL,WNOHANG));
    }



}