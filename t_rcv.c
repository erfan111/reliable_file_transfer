#include "net_include.h"

#define BUF_SIZE 65000

int main()
{
    struct sockaddr_in name;
    int                s, n;
    int                recv_s[10];
    int                valid[10];  
    fd_set             mask;
    fd_set             read_mask, write_mask, excep_mask;
    int                i,j,num;
    int                mess_len;
    int                neto_len;
    char               mess_buf[BUF_SIZE];
    long               on=1;

    FILE *fw; /* Pointer to dest file, which we write  */
    int nread, nwritten;
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s<0) {
        perror("Net_server: socket");
        exit(1);
    }

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) < 0)
    {
        perror("Net_server: setsockopt error \n");
        exit(1);
    }

    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(PORT);

    if ( bind( s, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("Net_server: bind");
        exit(1);
    }
 
    if (listen(s, 4) < 0) {
        perror("Net_server: listen");
        exit(1);
    }

    if((fw = fopen("received_file", "w")) == NULL) {
        perror("fopen");
        exit(0);
    }

    i = 0;
    FD_ZERO(&mask);
    FD_ZERO(&write_mask);
    FD_ZERO(&excep_mask);
    FD_SET(s,&mask);
    for(;;)
    {
        read_mask = mask;
        num = select( FD_SETSIZE, &read_mask, &write_mask, &excep_mask, NULL);
        if (num > 0) {
            if ( FD_ISSET(s,&read_mask) ) {
                recv_s[i] = accept(s, 0, 0) ;
                FD_SET(recv_s[i], &mask);
                valid[i] = 1;
                i++;
            }
            for(j=0; j<i ; j++)
            {   if (valid[j])    
                if ( FD_ISSET(recv_s[j],&read_mask) ) {
                    n = recv(recv_s[j], mess_buf, BUF_SIZE, 0 );
                    if( n > 0) {
                        neto_len = mess_len - sizeof(mess_len);
                        recv(recv_s[j], mess_buf, neto_len, 0 );
                        mess_buf[neto_len] = '\0';
                    
                        nwritten = fwrite(mess_buf, 1, n, fw);
                        if (nwritten < n) {
                            printf("An error occurred...\n");
                            exit(0);
                        }
                    }
                    else
                    {
                        printf("closing %d \n",j);
                        FD_CLR(recv_s[j], &mask);
                        close(recv_s[j]);
                        fclose(fw);
                        valid[j] = 0;  
                    }
                }
            }
        }
    }

    return 0;

}

