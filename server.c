#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/sendfile.h>


#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)
#define GET_VARIABLE_NAME(Variable) (#Variable)

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.
    char username[10];
    int wait_for_write;  // used by handle_read to know if the header is read or not.
} request;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list

const char* accept_read_header = "ACCEPT_FROM_READ";
const char* accept_write_header = "ACCEPT_FROM_WRITE";

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance

int get_username(int conn_fd,request* reqP);
int parsing_request(int conn_fd,request* reqP,char* filepath,char* filename);

void list_files(int conn_fd)
{
    DIR *d;
    struct dirent *dir;
    char buffer[100];
    d = opendir("./server_folder");
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            sprintf(buffer,"%s\n", dir->d_name);
            write(conn_fd,buffer,sizeof(buffer));
        }
        closedir(d);
    }
}

typedef struct {
    int id;          //902001-902020
    int AZ;          
    int BNT;         
    int Moderna;     
}registerRecord;

int handle_read(request* reqP) {
    int r;
    char buf[512];

    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
    char* p1 = strstr(buf, "\015\012");
    int newline_len = 2;
    if (p1 == NULL) {
       p1 = strstr(buf, "\012");
        if (p1 == NULL) {
            ERR_EXIT("this really should not happen...");
        }
    }
    size_t len = p1 - buf + 1;
    memmove(reqP->buf, buf, len);
    reqP->buf[len - 1] = '\0';
    reqP->buf_len = len-1;
    return 1;
}

int main(int argc, char** argv) {

    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    int conn_fd;  // fd for a new connection with client
    int file_fd;  // fd for file that we open for reading
    char buf[512];
    int buf_len;
    struct flock lock;
    bool write_lock = false;
    bool read_lock = false;

    int status = mkdir("server_folder",0777);
    if(status == -1)
        fprintf(stderr,"fail to crearte directory");
    
    // Initialize server
    init_server((unsigned short) atoi(argv[1]));
    
    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

    struct timeval tv;
    struct fd_set original_set,workingset;
    FD_ZERO(&original_set);
    FD_SET(svr.listen_fd,&original_set);

    while (1) 
    {
        // TODO: Add IO multiplexing
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        //memcpy(&workingset,&original_set,sizeof(original_set));
        workingset = original_set;
        int ret = select(maxfd,&workingset,NULL,NULL,&tv); 
        if(ret< 0)
        {
            fprintf(stderr,"select error \n");
            perror("select");
            exit(1);
        }
        if (ret == 0)
        {
            continue;
            //break;
        }
       
        /*如果不用select "accept為slow syscall process may be blocked eternally, so we use select to make sure the reading data  is ready*/
       for(int i = 0;i<maxfd;i++)
       {
            if(FD_ISSET(i,&workingset))
            {
                if(i == svr.listen_fd)
                {    
                    fprintf(stderr,"Listening socket is readable\n");
                    clilen = sizeof(cliaddr);
                    conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);//new connection established
                    if (conn_fd < 0) {
                        if (errno == EINTR || errno == EAGAIN) continue;  // try again
                        if (errno == ENFILE) 
                        {
                            (void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                            continue;
                        }
                        ERR_EXIT("accept");
                    }
                    else // accept succeed
                    {
                        fprintf(stderr,"New connection incoming%d\n",conn_fd);
                        FD_SET(conn_fd,&original_set);
                        // Check new connection
                        requestP[conn_fd].conn_fd = conn_fd;
                        strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
                        fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
                        char* entry_buf = "input your username:\n";
                        write(conn_fd,entry_buf,strlen(entry_buf));
                    }
                }

                else // data from existing connection not establishing new connection, receive it
                {
                    
                    
                    int ret = handle_read(&requestP[conn_fd]); // parse data from client to requestP[conn_fd].buf
	                if (ret < 0) {
                        fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
                        continue;
                    }
                    
                    if(requestP[conn_fd].username == NULL)
                    {
                        int ret = get_username(conn_fd,requestP);
                        if(ret > 0)
                            continue; // username in used;
                        else
                        {
                            strcpy(requestP[conn_fd].buf,"connect successfully\n");
                            requestP[conn_fd].buf_len = strlen(requestP[conn_fd].buf);
                            write(conn_fd,requestP[conn_fd].buf,requestP[conn_fd].buf_len);
                            continue;
                        }
                    }

                    char* file_path;
                    char* filename;
                    int ret = parsing_request(conn_fd,requestP,file_path,filename);
                    if(ret == 0)//ls operatration from client
                    {
                        list_file(conn_fd);
                    }
                
                    else if(ret == 1) // get operation from client
                    {
                        
                        struct stat st;
                        stat(file_path,&st);
                        int size = st.st_size;
                        int fd = open(file_path,O_RDONLY);
                        if(fd == -1)
                            perror("File open Error");
                        
                        int ret = sendfile(conn_fd,fd,NULL,size);
                        if(ret < 0)
                            perror("Sendfile Error:");
                        fprintf(stderr,"file sended to client\n");
                        sprintf(requestP[conn_fd].buf,"get <%s> successfully\n",filename);
                        write(conn_fd,requestP[conn_fd].buf,strlen(requestP[conn_fd].buf));
                        close(fd);

                    }
                    else if(ret == 2)// put operation from client
                    {
                        
                        char buffer[1024];
                        FILE* received_file = fopen(file_path,"w");
                        if(received_file == NULL)
                            perror("File open error:");
                        struct stat st;
                        stat(file_path,&st);
                        int remain_data = st.st_size;
                        int len;
                        while((remain_data > 0)&& (len = read(conn_fd,buffer,sizeof(buffer) > 0)))
                        {
                            fwrite(buffer, sizeof(char), len, received_file);
                            remain_data -= len;
                        }
                        fprintf(stderr,"file received from client\n");
                        sprintf(requestP[conn_fd].buf,"put <%s> successfully\n",filename);
                        write(conn_fd,requestP[conn_fd].buf,strlen(requestP[conn_fd].buf));
                        fclose(received_file);
                    }
                    else //invalid operation
                    {
                        continue;
                    }
                    /*
                    close(requestP[conn_fd].conn_fd);
                    free_request(&requestP[conn_fd]);
                    FD_CLR(i,&original_set);
                    */
                }
            }
       }
    }
    free(requestP);
    return 0;
}

// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    strcpy(reqP->username,NULL);
    
}

static void free_request(request* reqP) {
    /*if (reqP->filename != NULL) {
        free(reqP->filename);
        reqP->filename = NULL;
    }*/
    init_request(reqP);
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }

    // Get file descripter table size and initialize request table
    maxfd = getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (int i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    return;
}

int get_username(int conn_fd,request* reqP)
{
    char* username = requestP[conn_fd].buf;
    int dup_username = 0;
    for(int i =0;i<maxfd;i++){
        if(strcmp(requestP[i].username,username))
        {
            strcpy(requestP[conn_fd].buf,"username is in used, please try another:\n");
            requestP[conn_fd].buf_len = strlen(requestP[conn_fd].buf);
            write(conn_fd,requestP[conn_fd].buf,requestP[conn_fd].buf_len);
            dup_username = 1;
            break;
        }
    }
    
    return dup_username;
}

int parsing_request(int conn_fd,request* reqP,char* filepath,char* filename)
{
    char* req = reqP[conn_fd].buf;
    char op[10];
    char file[100];
    char file_path[100] = "./server_folder/";
    char parse_result[2][20];
    int index = 0;
    char* delim = " ";
    char* p = strtok(req,delim);
    while(p!=NULL)
    {
        strcpy(parse_result[index],p);
        index++;
        p = strtok(NULL,delim);
    }
    

    //format checking
    if(index != 2 || index !=1)
    {
        strcpy(reqP[conn_fd].buf , "Command format error\n");
        reqP[conn_fd].buf_len = strlen(reqP[conn_fd].buf);
        write(conn_fd,reqP[conn_fd].buf,reqP[conn_fd].buf_len);
        return -1;
    }

    strcpy(op,parsing_request[0]);
    strcpy(filename,parsing_request[1]);
    strcat(file_path,filename);

    //command checking
    if(strcmp(op,"get")!=0 && strcmp(op,"put")!=0 && strcmp(op,"ls")!=0)
    {
        strcpy(reqP[conn_fd].buf , "Command not found\n");
        reqP[conn_fd].buf_len = strlen(reqP[conn_fd].buf);
        write(conn_fd,reqP[conn_fd].buf,reqP[conn_fd].buf_len);
        return -1;
    }

    if(strcmp("ls",op)==0)
        return 0;

    //file checking
    
    if(access(file_path,F_OK)!=0)
    {
        sprintf(reqP[conn_fd].buf,"The '%s' doesn't exist\n",file);
        reqP[conn_fd].buf_len = strlen(reqP[conn_fd].buf);
        write(conn_fd,reqP[conn_fd].buf,reqP[conn_fd].buf_len);
        return -1;
    }

    filepath = file_path;
    if(strcmp("get",op)==0)
        return 1;
    if(strcmp("put",op)==0)
        return 2;
    

    
}