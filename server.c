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
#include <signal.h>


#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)
#define GET_VARIABLE_NAME(Variable) (#Variable)
#define WAIT_FOR_OPERATION 0
#define WAIT_FOR_DATA_SIZE 1
#define WAIT_FOR_DATA 2
#define WAIT_FOR_NAME 3
#define PROCESSING_GET 4
#define PROCESSING_PUT 5
#define PROCESSING_LS 6
#define BUFSIZE 1024
#define SIGPIPE 13
#define SIG_IGN ((__sighandler_t) 1)


typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[BUFSIZE];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    char file_path[50];
    char username[10];
    char operation[10];
    int status;
    ssize_t filesize;
    off_t offset;
    int wait_for_write;  // used by handle_read to know if the header is read or not.
} request;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list
int max_fd;

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
void get_operation(int conn_fd);
void put_operation(int conn_fd,ssize_t numbytes);
int send_msg(int conn_fd,char* msg)
{
    char buffer[BUFSIZE];
    bzero(buffer,sizeof(buffer));
    strcpy(buffer,msg);
    write(conn_fd,buffer,sizeof(buffer));
    fprintf(stderr,"####Server sending %s to client %d\n",buffer,conn_fd);
}

void sort(char arr[][50],int num)
{
    char tmp[50] = {0};
    for(int i=0;i<num;i++)
    {
        for(int j = i+1;j<num;j++)
        {
            if(strcmp(arr[i],arr[j]) > 0)
            {
                strcpy(tmp,arr[i]);
                strcpy(arr[i],arr[j]);
                strcpy(arr[j],tmp);
            }
        }
    }
}

void list_files(int conn_fd)
{
    DIR *d;
    struct dirent *dir;
    char buffer[BUFSIZE];
    bzero(buffer,sizeof(buffer));
    char arr[100][50] = {0};
    int num = 0;
    d = opendir("./server_dir");

    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            
            if(dir->d_type == DT_REG)
            {
                strcpy(arr[num],dir->d_name);
                num++;
            }    
        }
    }
    sort(arr,num);
    for(int i=0;i<num;i++)
    {
        sprintf(buffer+strlen(buffer),"%s\n",arr[i]);
    }
    fprintf(stderr,"listing file:\n");
    fprintf(stderr,"%s",buffer);
    send_msg(conn_fd,buffer);
    closedir(d);
}


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
    char buf[BUFSIZE];
    int buf_len;
    
    DIR* dp;
    dp = opendir("server_dir");
    if(errno == ENOENT)
    {
        int status = mkdir("server_dir",0777);
        if(status == -1)
            fprintf(stderr,"fail to create directory\n");
        else
            fprintf(stderr,"the server folder has been created\n");
    }

    // Initialize server
    init_server((unsigned short) atoi(argv[1]));
    fprintf(stderr,"the server has been initialized\n");
    signal(SIGPIPE,SIG_IGN);
    
    // Loop for handling connections
    //fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

    struct timeval tv;
    fd_set original_set,workingset;
    FD_ZERO(&original_set);
    FD_SET(svr.listen_fd,&original_set);
    max_fd = svr.listen_fd;

    while (1) 
    {
        // TODO: Add IO multiplexing
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        //memcpy(&workingset,&original_set,sizeof(original_set));
        workingset = original_set;
        int ret = select(max_fd+1,&workingset,NULL,NULL,&tv); 
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
       for(int i = 0;i<max_fd+1;i++)
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
                        if(conn_fd > max_fd)
                            max_fd = conn_fd;
                        // Check new connection
                        requestP[conn_fd].conn_fd = conn_fd;
                        strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
                    }
                }

                else // data from existing connection not establishing new connection, receive it
                {
                    
                    memset(requestP[i].buf,'\0',BUFSIZE);
                    ssize_t numbytes = read(i,requestP[i].buf,BUFSIZE);
                    if (numbytes == 0)
                    {
                        FD_CLR(requestP[i].conn_fd,&original_set);
                        close(requestP[i].conn_fd);
                        free_request(&requestP[i]);
                        fprintf(stderr,"client %d has left\n",i);
                        continue;

                    }
                    else if (numbytes < 0){
                        fprintf(stderr, "bad request from %s\n", requestP[i].host);
                        continue;
                    }
                    else{
                        fprintf(stderr,"request from %d request is %s\n",requestP[i].conn_fd,requestP[i].buf);
                    }

                    switch (requestP[i].status)
                    {
                    case WAIT_FOR_NAME: ;
                        /* code */
                        int ret = get_username(requestP[i].conn_fd,requestP);
                        if(ret > 0)
                            continue; // username in used;
                        else //username not in used
                        {
                            strcpy(requestP[i].username,requestP[i].buf);
                            strcpy(requestP[i].buf,"connect successfully");
                            send_msg(i,requestP[i].buf);
                            requestP[i].status = WAIT_FOR_OPERATION;
                            //continue;
                        }
                        break;
                    case WAIT_FOR_OPERATION: ;
                        char file_path[100] = "./server_dir/";
                        char filename[100];
                        int parsing = parsing_request(requestP[i].conn_fd,requestP,file_path,filename);
                        if(parsing == 0)//ls operatration from client
                        {
                            list_files(requestP[i].conn_fd);
                        }
                        else if (parsing == 1) // get operation from client
                        {
                            if(access(file_path,F_OK)!=0) //file does not exist
                            {
                                sprintf(requestP[i].buf,"%d",-1);
                                send_msg(i,requestP[i].buf);
                            }
                            else
                            {
                                struct stat st;
                                stat(file_path,&st);
                                int size = st.st_size;


                                //sending file size
                                sprintf(requestP[i].buf,"%d",size);
                                send_msg(i,requestP[i].buf);
            
                                requestP[i].filesize = size;
                                requestP[i].status = PROCESSING_GET;
                                requestP[i].offset = 0;
                                strcpy(requestP[i].file_path,file_path);
                            }
                            
            
                        }

                        else if(parsing == 2)//put operation from client
                        {
                          
                            requestP[i].status = WAIT_FOR_DATA_SIZE;
                            strcpy(requestP[i].file_path,file_path);
                        }
                        break;
                        
                    case WAIT_FOR_DATA_SIZE:
                        requestP[i].filesize = atoi(requestP[i].buf);
                        requestP[i].status = PROCESSING_PUT;
                        break;

                    case PROCESSING_PUT: 
                        fprintf(stderr,"In PROCESSING_PUT\n");
                        put_operation(i,numbytes);
                        if(requestP[i].filesize == 0)
                        {
                            requestP[i].status = WAIT_FOR_OPERATION;
                            memset(requestP[i].file_path,0,sizeof(requestP[i].file_path));
                        }
                        break;
                    case PROCESSING_GET:
                        
                        if(strcmp("Finished get operation",requestP[i].buf) ==0)
                        {
                            requestP[i].status = WAIT_FOR_OPERATION;
                            memset(requestP[i].file_path,0,sizeof(requestP[i].file_path));
                
                        }
                        else
                        {
                            get_operation(i);
                        }
                        break;      
                        

                    default:
                        break;
                    }
                    

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
    reqP->status = WAIT_FOR_NAME;
    reqP->offset = 0;
    memset(reqP->username,0,sizeof(reqP->username));
    memset(reqP->file_path,0,sizeof(reqP->file_path));
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
    fprintf(stderr,"starting initializing requests\n");
    for (int i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    fprintf(stderr,"the requests has bee initialized\n");
    fprintf(stderr,"svr.listen_fd = %d\n",svr.listen_fd);
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);
    fprintf(stderr,"hostname has been copied\n");

    return;
}

int get_username(int conn_fd,request* reqP)
{
    char* username = reqP[conn_fd].buf;
    int dup_username = 0;
    for(int i =0;i<max_fd+1;i++){
        if(strcmp(reqP[i].username,username) == 0)
        {
            strcpy(reqP[conn_fd].buf,"username is in used, please try another:\n");
            send_msg(conn_fd,reqP[conn_fd].buf);
            dup_username = 1;
            break;
        }
    }
    return dup_username;
}

int parsing_request(int conn_fd,request* reqP,char* filepath,char* filename)
{
    char* req = reqP[conn_fd].buf;
    char op[20];
    char file[100];
    //char file_path[100] = "./server_dir/";
    char parse_result[10][50];
    int index = 0;
    char* delim = " ";
    char* p = strtok(req,delim);
    while(p!=NULL)
    {
        strcpy(parse_result[index],p);
        index++;
        p = strtok(NULL,delim);
    }
    
    strcpy(filename,parse_result[1]);
    strcat(filepath,filename);
    
    //command checking
    
    if(strcmp("ls",parse_result[0])==0)
        return 0;

    if(strcmp("get",parse_result[0])==0)
    {
        return 1;
    }
    if(strcmp("put",parse_result[0])==0)
        return 2;
}


void get_operation(int conn_fd)
{
    char buf[BUFSIZE];
    int fd = open(requestP[conn_fd].file_path,O_RDONLY);
    if(fd == -1)
        perror("File open Error");

    lseek(fd,requestP[conn_fd].offset,SEEK_SET);
    bzero(buf,sizeof(buf));
    ssize_t readbytes = read(fd,buf,BUFSIZE);
    ssize_t writebytes = write(conn_fd,buf,readbytes);
    fprintf(stderr,"-----Server sending data = %s to client %d\n",buf,conn_fd);
    requestP[conn_fd].offset += writebytes;
    requestP[conn_fd].filesize -= writebytes;
    fprintf(stderr,"Server send %ld bytes from files data and remaining data = %ld\n",writebytes,requestP[conn_fd].filesize);
    close(fd);
}

void put_operation(int conn_fd,ssize_t numbytes)
{
    FILE* received_file;
    received_file = fopen(requestP[conn_fd].file_path,"a");
    if(received_file == NULL)
        perror("File open error:");

    fwrite(requestP[conn_fd].buf,sizeof(char),numbytes,received_file);
    requestP[conn_fd].filesize -= numbytes;
    fprintf(stderr,"Receive %ld bytes and we hope :- %ld bytes\n", numbytes, requestP[conn_fd].filesize);
    fclose(received_file);
}