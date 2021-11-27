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
#define BUFSIZE 1024

int parsing_request(char* client_input,char* filename);
int handle_read(int client_socket,char* buffer);
int read_buffer(int client_socket,char* buf);
int send_buffer(int client_socket,char* buffer);
char buffer[BUFSIZE];

int main(int argc, char** argv)
{
    if(argc != 2)
    {
        fprintf(stderr,"need ip and port\n");
        exit(1);
    }

    DIR* dp;
    dp = opendir("client_dir");
    if(errno == ENOENT)
    {
        int status = mkdir("client_dir",0777);
        if(status == -1)
            fprintf(stderr,"fail to create directory\n");
        else
            fprintf(stderr,"the client folder has been created\n");
    }


    char* input = argv[1];
    char delim[] = ":";
    char* ptr = strtok(input,delim);
    int index = 0;
    char ip_addr[50];
    int port_num;
    char parsing_result[2][50];
    
    while(ptr!=NULL)
    {
        strcpy(parsing_result[index],ptr);
        index++;
        ptr = strtok(NULL,delim);
    }
    strcpy(ip_addr,parsing_result[0]);
    port_num = atoi(parsing_result[1]);
    fprintf(stderr,"using ip:%s port:%d\n",ip_addr,port_num);

    int client_socket;
    ssize_t len;
    struct sockaddr_in remote_addr;
    /* Zeroing remote_addr struct */
    memset(&remote_addr, 0, sizeof(remote_addr));

    /* Construct remote_addr struct */
    remote_addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip_addr, &(remote_addr.sin_addr));
    remote_addr.sin_port = htons(port_num);

    /* Create client socket */
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1)
    {
            fprintf(stderr, "Error creating socket --> %s\n", strerror(errno));

            exit(EXIT_FAILURE);
    }

    /* Connect to the server */
    if (connect(client_socket, (struct sockaddr *)&remote_addr, sizeof(struct sockaddr)) == -1)
    {
            fprintf(stderr, "Error on connect --> %s\n", strerror(errno));
            exit(EXIT_FAILURE);
    }
    
    
    
    while(1)
    {
        char username[10];
        char username_buf[50];
        printf("input your username:\n");
        fgets(username,sizeof(username),stdin);
        //write(client_socket,username,sizeof(username));
        username[strlen(username) - 1] = '\0';
        send_buffer(client_socket,username);
        read_buffer(client_socket,username_buf);
        
        if(strcmp(username_buf,"connect successfully")==0)
        {
            printf("connect successfully\n");
            break;
        }
        else
            printf("username is in used, please try another:\n");
    }


     while(1)
    {
        char filename[100];
        char client_input[100];
        char read_buf[BUFSIZE];
        char file_path[100] = "./client_dir/";
        
        fprintf(stderr,"please enter operation:\n");
        fgets(client_input,sizeof(client_input),stdin);
        int par_ret = parsing_request(client_input,filename);
        if(par_ret < 0)  //error input;
            continue;

        strcat(file_path,filename);
        client_input[strlen(client_input)-1] = '\0';
        send_buffer(client_socket,client_input);

        if(par_ret == 1) //ls
        {
            read_buffer(client_socket,buffer);
            printf("%s",buffer);
            continue;
        }
        if(par_ret == 2) //get
        {
            //check if file exist
    
            read_buffer(client_socket,buffer);
            
            int file_size = atoi(buffer);
            if(file_size < 0)
            {
                printf("The %s doesn't exist\n",filename);
                continue;
            }
            fprintf(stderr,"get file size %d\n",file_size);
            sprintf(buffer,"receive file size");
            //write(client_socket,buffer,sizeof(buffer));
            send_buffer(client_socket,buffer);

            int remain_data = file_size;
            int len;
            
            FILE* received_file;
            received_file = fopen(file_path,"w");
            //int received_file = open(file_path,O_WRONLY|O_CREAT,S_IRWXU);
            
            if(received_file == NULL)
                perror("File open error:");
            bzero(buffer,sizeof(buffer));
            while((remain_data > 0) && (len = read(client_socket,buffer,sizeof(buffer))))
            {
                
                fwrite(buffer,sizeof(char),len,received_file);
                fprintf(stderr,"----getting file data %s",buffer);

                remain_data -= len;
                fprintf(stderr, "Receive %d bytes and we hope :- %d bytes\n", len, remain_data);
                if(remain_data > 0)
                {
                    sprintf(buffer,"%s","remaining data need to transform");
                    send_buffer(client_socket,buffer);
                    bzero(buffer,sizeof(buffer));
                }
            }
            sprintf(buffer,"%s","Finished get operation");
            send_buffer(client_socket,buffer);
            fclose(received_file);
            printf("get %s successfully\n",filename);
        }
        else //put
        {
            
            struct stat st;
            stat(file_path,&st);
            int size = st.st_size;
            char file_size_buf[256];
            fprintf(stderr,"the file path is %s \n",file_path);
            fprintf(stderr,"the file size is %d \n",size);

            //sending file size
            sprintf(file_size_buf,"%d",size);
            //write(client_socket,file_size_buf,sizeof(file_size_buf));
            send_buffer(client_socket,file_size_buf);
            
            int fd = open(file_path,O_RDONLY);
            if(fd == -1)
                perror("File open Error");

            int remain_data = size;
            int sent_bytes;
            off_t offset = 0;
            while (((sent_bytes = sendfile(client_socket, fd, &offset, BUFSIZE)) > 0) && (remain_data > 0))
            {
                fprintf(stderr, "1. Client sent %d bytes from file's data, offset is now : %ld and remaining data = %d\n", sent_bytes, offset, remain_data);
                remain_data -= sent_bytes;
                fprintf(stderr, "2. Client sent %d bytes from file's data, offset is now : %ld and remaining data = %d\n", sent_bytes, offset, remain_data);
            }
            printf("put %s successfully\n",filename);
            close(fd);
        }
        
    }
    
}

int parsing_request(char* client_input,char* filename)
{
    fprintf(stderr,"the command is %s",client_input);
    char client_request[100];
    strcpy(client_request,client_input);
    client_request[strlen(client_request)-1] = '\0';
    char pars_result[10][20];
    char* delim = " ";
    int index = 0;
    char* ptr = strtok(client_request,delim);
    while(ptr != NULL)
    {
        strcpy(pars_result[index],ptr);
        index++;
        ptr = strtok(NULL,delim);
    }
    if(strcmp(pars_result[0],"ls") && strcmp(pars_result[0],"put")&& strcmp(pars_result[0],"get"))
    {
        printf("Command not found\n");
        return -1;
    }
    

    if(index == 1) //ls
    {
        if(strcmp(pars_result[0],"ls")!=0)
        {
            printf("Command not found\n");
            return -1;
        }
        return 1;
    }
    else if(index ==2)//get or put
    {
        if(strcmp("ls",pars_result[0])==0)
        {
            printf("Command format error\n");
            return -1;
        }
        if(strcmp("put",pars_result[0])==0)
        {
            strcpy(filename,pars_result[1]);
            char filepath[100] = "./client_dir/";
            strcat(filepath,filename);
            if(access(filepath,F_OK)!=0) // file does not exist
            {
                printf("The %s doesn't exist\n",filename);
                return -1;
            }
            return 3;
        }
        if(strcmp("get",pars_result[0])==0)
        {
            strcpy(filename,pars_result[1]);
            return 2;
        }
        printf("Command not found\n");
        return -1;
    }
    else//wrong length
    {
        printf("Command format error\n");
        return -1;
    }
}

int read_buffer(int client_socket,char* buf)
{
    char tmp_buf[BUFSIZE];
    bzero(tmp_buf,sizeof(tmp_buf));
    bzero(buf,sizeof(buf));
    int numbytes = read(client_socket,tmp_buf,sizeof(tmp_buf));
    strcpy(buf,tmp_buf);
    fprintf(stderr,"getting data from server:%s\n",buf);
    return numbytes;
}

int send_buffer(int client_socket,char* buf)
{
    char tmp_buf[BUFSIZE];
    bzero(tmp_buf,sizeof(tmp_buf));
    strcpy(tmp_buf,buf);
    write(client_socket,tmp_buf,sizeof(tmp_buf));
    fprintf(stderr,"----Sending %s to server\n",tmp_buf);
    bzero(buf,sizeof(buf));
}

