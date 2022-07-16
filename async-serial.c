#ifdef __linux__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h>
#include <ctype.h>
#include <time.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>                                                      

#include <fcntl.h>
#include <termios.h>
#include <signal.h>

#define MSG_INTERFACE_ACCEPT_COMMAND    "+ACCEPTED\r\n"
#define MSG_INTERFACE_REJECT_COMMAND    "-PROTOCOL ERROR\r\n"
#define MSG_SERIALPORT_ACCEPT_COMMAND   "+DATA RECEIVED\r\n"
#define MSG_SERIALPORT_ENDING_COMMAND   "+DATA END\r\n"

#define STANDARD_BUFF_SIZE              1024
#define MAX_CLIENT_COUNT                10
#define STANDARD_PORT                   2567
#define BAUDRATE                        B9600
#define ON                              1

/* true while no signal is received */
_Bool waitfl =                          true;                         



/* typical usage example: binary --serial-port serial_port */
int accept_arguments(int argc, char * const argv[])
{
    if(argc != 3) {
        fprintf(stderr, "invalid argument count \n");
        return -1;
    }

    if(strncmp(argv[1], "--serial-port", strlen("--serial-port")) != 0) {
        fprintf(stderr, "invalid key input \n");
        return -1;
    }
}



int check_for_device(char * argument) 
{
    struct stat arg_stat;
    if(stat(argument, &arg_stat) != 0) {
        perror("stat(): ");
        return -1;
    }

    if(!S_ISCHR(arg_stat.st_mode)) {
        fprintf(stderr, "%s is not a character device \r\n", argument);
        return -1;
    }
}



int get_port_desc(char * port)
{
    int port_desc;
    /* 
    *   O_RDWR                  read/write 
    *   O_NOCTTY                do not become controlling terminal
    *   O_NONBLOCK(O_NDELAY)    enable non-blocking working
    */      
    port_desc = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(port_desc == -1) {
        perror(NULL);
        return -1;
    }
    else
        return port_desc;
}



/* received SIGIO = sig_handle() function called and data can be read from fd */
void sig_handle(int status)
{
    waitfl = false;
}



int port_config(int pd, int baudrate)
{
    struct termios tty;
    struct sigaction sigio;
    
    bzero(&tty,   sizeof(tty));
    bzero(&sigio, sizeof(sigio));
    
    if(tcflush(pd, TCIFLUSH) == -1) {
        perror("tcflush() function : ");
        return -1;
    }

    /* received SIGIO = sig_handle() function called and data can be read from fd */
    sigio.sa_handler  = sig_handle;  
    
    /* do not call sighandler on alternative stacks */
    sigio.sa_flags    = 0;
    
    /* not specified by POSIX */
    sigio.sa_restorer = NULL;
    
    /* exclude all signals from the set */
    sigemptyset(&sigio.sa_mask);
    sigaction(SIGIO, &sigio, NULL);

    /* allow the process to receive SIGIO */
    fcntl(pd, F_SETOWN, getpid());
    
    /* make pd (port descriptor) asynchronous */
    fcntl(pd, F_SETFL, FASYNC);
    
    if(tcgetattr(pd, &tty) != 0) {
        perror("tcgetattr() function : ");
        return -1;
    }

    /* ignore framing errors and parity errors */
    tty.c_iflag = IGNPAR;
    
    /* set baudrate */
    cfsetospeed(&tty, (speed_t)baudrate);
    cfsetispeed(&tty, (speed_t)baudrate);
    
    /*
    *   CS8         8 bits per byte
    *   CLOCAL      ignore modem control lines.
    *   CREAD       enable receiver.
    */
    tty.c_cflag = CS8 | CLOCAL | CREAD;
    tty.c_oflag = 0;
    
    /* enable canonical mode*/
    tty.c_lflag = ICANON;

    /* wait for up to 1s (10 deciseonds) and return as soon as any data is received */
    tty.c_cc[VTIME] = 10;
    tty.c_cc[VMIN]  = 0;

    if (tcsetattr(pd, TCSANOW, &tty) != 0) {
        perror("tcsetattr() function : ");
        return -1;
    }
}



/* filter out commands larger than 16 bytes and smaller than 0 bytes (excluding \r\n ) */
int scan_input(char * buffer)
{
    int comm_symbols_count = 0;
    while(isalnum(buffer[comm_symbols_count]))
        comm_symbols_count++;
    
    if(comm_symbols_count < 1 || comm_symbols_count > 16)
        return -1;
}



void make_command(char * buffer, char * command)
{
    int index = 0;
    while(isalnum(buffer[index])) {
        command[index] = buffer[index];
        index++;
    }
    command[index]     = '\r';
    command[index + 1] = '\n';
}



void get_sys_time(char * date_string)
{
    time_t t     = time(NULL);
    struct tm tm = *localtime(&t);
    sprintf(date_string, "%d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}



int fill_logfile(struct sockaddr_in6 * client, char * direction, char * data)
{
    char date[STANDARD_BUFF_SIZE]    = {0};
    char log[STANDARD_BUFF_SIZE * 2] = {0};
    char ip_string[INET6_ADDRSTRLEN] = {0};
    if(inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)client)->sin6_addr), ip_string, INET6_ADDRSTRLEN) == NULL) {
        perror("inet_ntop() function : ");
        return -1;
    }
    get_sys_time(date);

    int leftb_count      = 0;
    int processedb_count = 0;
    int sumb_count       = 0;

    FILE * fp = fopen("logfile", "a");
    if(fp == NULL) {
        perror("fopen() function (logfile) : ");
        return -1;
    }
    int fd = fileno(fp);

    /* organize a string for a logfile */
    sprintf(log, "%s [%s: %d] [%s] %s\n", date, ip_string, client->sin6_port, direction, data);
    
    leftb_count = sizeof(char) * strlen(log);
    do {
        processedb_count = write(fd, log + sumb_count, leftb_count);
        if(processedb_count == -1) {
            perror("write() function (logile) : ");
            return -1;
        }
        sumb_count  += processedb_count;
        leftb_count -= processedb_count;
    } while(sumb_count != sizeof(char) * strlen(log));

    if(fclose(fp) != 0) {
        perror("fclose() function error (logfile) : ");
        return -1;
    }
}



int write_in_port(int fp, char * command_name)
{
    int leftb_count      = 0;
    int processedb_count = 0;
    int sumb_count       = 0;

    leftb_count = sizeof(char) * strlen(command_name);
    do {
        processedb_count = write(fp, command_name + sumb_count, leftb_count);
        if(processedb_count == -1) {
            perror("write() function (writing in port) : ");
            return -1;
        }
        sumb_count  += processedb_count;
        leftb_count -= processedb_count;
    } while(sumb_count != sizeof(char) * strlen(command_name)); 
}



int read_from_port(int fd, char * buffer)
{
    int processedb_count = 0;
    while(ON) {
        usleep(200);
        if(waitfl = false) {
            processedb_count         = read(fd, buffer, STANDARD_BUFF_SIZE);
            buffer[processedb_count] = '\0';
            
            if(processedb_count == 1)
                break;
            waitfl = true;
        }   
    }
}



int main(int argc, char * argv[])
{
    if(accept_arguments(argc, argv) == -1)              
        exit(EXIT_FAILURE);

    if(check_for_device(argv[2]) == -1)
        exit(EXIT_FAILURE);
    
    int port_desc = get_port_desc(argv[2]);
    if(port_config(port_desc, BAUDRATE) == -1)
        exit(EXIT_FAILURE);

    struct sockaddr_in6 server_addr;
    struct sockaddr_in6 client_addr;
    
    int server_fd;
    int client_fd;
    int sockaddr_len                = sizeof(struct sockaddr_in6);
    int req_bytes_count             = 0;
    char command_name[18]           = {0};
    char buffer[STANDARD_BUFF_SIZE] = {0};

    /*
    *   AF_INET6        compatible with IPv4 and IPv6 internet protocol
    *   SOCK_STREAM     TCP socket
    *   0               default TCP protocol
    */
    server_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if(server_fd == -1) {
        perror("socket() function : ");
        exit(EXIT_FAILURE);
    }
    
    server_addr.sin6_family      = AF_INET6;

    /* htons() converts unsigned short integer from host byte order to network byte order */
    server_addr.sin6_port        = htons(STANDARD_PORT);

    /* connect to my local machine */
    server_addr.sin6_addr        = in6addr_any;
    

    if(bind(server_fd, (struct sockaddr *)&server_addr, sockaddr_len) == -1) {
        perror("bind() function : ");
        exit(EXIT_FAILURE);
    }

    if(listen(server_fd, MAX_CLIENT_COUNT) == -1) {
        perror("listen() function : ");
        exit(EXIT_FAILURE);
    }

    /* while this server is running */
    while(ON) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &sockaddr_len);
        if(client_fd == -1) {
            perror("accept() function : ");
            exit(EXIT_FAILURE);
        }
        req_bytes_count = 1;
        while(req_bytes_count) {
            req_bytes_count = recv(client_fd, buffer, sizeof(buffer), 0);
            if(req_bytes_count) {
                /* COMMAND ACCEPTED CASE */
                if(scan_input(buffer) != -1) {
                    send(client_fd, MSG_INTERFACE_ACCEPT_COMMAND, strlen(MSG_INTERFACE_ACCEPT_COMMAND), 0);
                    make_command(buffer, command_name);
                    
                    if(fill_logfile(&client_addr, "IN", command_name) == -1)
                        exit(EXIT_FAILURE);
                    
                    if(write_in_port(port_desc, command_name) == -1)
                        exit(EXIT_FAILURE);
                    memset(buffer, 0, sizeof(buffer));
                    
                    read_from_port(port_desc, buffer);
                    
                    if(fill_logfile(&client_addr, "OUT", buffer) == -1)
                        exit(EXIT_FAILURE);
                    
                    send(client_fd, MSG_SERIALPORT_ACCEPT_COMMAND, strlen(MSG_INTERFACE_ACCEPT_COMMAND), 0);
                    send(client_fd, buffer, strlen(buffer), 0);
                    send(client_fd, MSG_SERIALPORT_ENDING_COMMAND, strlen(MSG_INTERFACE_ACCEPT_COMMAND), 0);
                }
                /* WRONG COMMAND CASE */
                else {
                    send(client_fd, MSG_INTERFACE_REJECT_COMMAND, strlen(MSG_INTERFACE_ACCEPT_COMMAND), 0);
                    if(fill_logfile(&client_addr, "IN", buffer) == -1)
                        exit(EXIT_FAILURE);
                }
            }
            memset(buffer,       0, sizeof(buffer));
            memset(command_name, 0, sizeof(command_name));
        }
        if(close(client_fd) == -1) {
            perror("close() function (client fd): ");
            exit(EXIT_FAILURE);
        }
    }
    if(close(server_fd) == -1) {
        perror("close() function (server fd): ");
        exit(EXIT_FAILURE);
    }
}
#endif