# NTU-CN-2021

## Spec
- [Link](https://docs.google.com/presentation/d/18NSrp3BokNhquQS3Gt9L9wOR21S9uMLpmXfe4JeZVFo/edit?usp=sharing)

## Introduction

- Client can upload any file (except directory) to server
- Client can download any file (except directory) to server
- Client can list files in server
 After upload and download, you need to ensure the files are identical between source and destination.

  In this assignment, all the transmission should be implemented by the socket of TCP.
  
## Specification
**Server is required to support multiple connections.** That is, there can be more than 1 client connecting to the server simultaneously
``` 

  $ make client	# for client code
  $ make server	# for server code

```
```
  #Launch server
  $ ./server [port]	# [port] will be determined
  #Launch client
  $ ./client [ip:port]	# ip is server ip, port is determined by the command above

```

```
  $ ./client 127.0.0.1:8074
  input your username:
  wayne
  username is in used, please try another:
  ntuwayne
  connect successfully
```


