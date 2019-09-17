#include "net_include.h"

#define BUF_SIZE 65000

int main(int argc, char **argv)
{
    struct sockaddr_in host;
    struct hostent     h_ent, *p_h_ent;

    int                s;
    int                ret;

    struct timeval     send_start, send_end, report_start, report_end;
    unsigned long      send_duration, sent_bytes = 0, report_sent_bytes = 0;

    FILE *fr; /* Pointer to source file, which we read */
    char buf[BUF_SIZE];
    int nread = 0;

  if(argc != 4) {
    printf("Usage: t_ncp <source_file> <destination_file> <comp_name>\n");
    exit(0);
  }

  /* Open the source file for reading */
  if((fr = fopen(argv[1], "r")) == NULL) {
    perror("fopen");
    exit(0);
  }
  printf("Opened %s for reading...\n", argv[1]);
    s = socket(AF_INET, SOCK_STREAM, 0); /* Create a socket (TCP) */
    if (s<0) {
        perror("Net_client: socket error");
        exit(1);
    }

    host.sin_family = AF_INET;
    host.sin_port   = htons(PORT);

    printf("Your server is %s\n",argv[3]);

    p_h_ent = gethostbyname(argv[3]);
    if ( p_h_ent == NULL ) {
        printf("net_client: gethostbyname error.\n");
        exit(1);
    }

    memcpy( &h_ent, p_h_ent, sizeof(h_ent) );
    memcpy( &host.sin_addr, h_ent.h_addr_list[0],  sizeof(host.sin_addr) );

    ret = connect(s, (struct sockaddr *)&host, sizeof(host) ); /* Connect! */

    gettimeofday(&send_start, NULL);
    gettimeofday(&report_start, NULL);

    if( ret < 0)
    {
        perror( "Net_client: could not connect to server"); 
        exit(1);
    }

    for(;;)
    {

        nread = fread(buf, 1, BUF_SIZE, fr);
        /* If there is something to write, write it */
        if(nread > 0)
        {
            ret = send( s, buf, nread, 0);
            if(ret != nread) 
            {
                perror( "Net_client: error in writing");
                exit(1);
            }
            sent_bytes += nread;
            report_sent_bytes += nread;
            if(report_sent_bytes >= 100000000)
            {
                gettimeofday(&report_end, NULL);
                send_duration = (report_end.tv_sec - report_start.tv_sec)*1000000 + (report_end.tv_usec - report_start.tv_usec);
                printf("Reporting: Total Bytes = %lu , Total Time = %lu, Average Transfer Rate = %lu \n", report_sent_bytes, send_duration, (report_sent_bytes*8)/send_duration);
                gettimeofday(&report_start, NULL);
                report_sent_bytes = 0;

            }
        }
    
        /* fread returns a short count either at EOF or when an error occurred */
        if(nread < BUF_SIZE) {

            /* Did we reach the EOF? */
            if(feof(fr)) {
                printf("Finished writing.\n");
                gettimeofday(&send_end, NULL);
                        send_duration = (send_end.tv_sec - send_start.tv_sec)*1000000 + (send_end.tv_usec - send_start.tv_usec);
                        printf("Total Bytes = %lu , Total Time = %lu, Average Transfer Rate = %lu \n", sent_bytes, send_duration, (sent_bytes*8)/send_duration);
                break;
            }
            else {
                printf("An error occurred...\n");
                exit(0);
            }
        }
    }
    fclose(fr);
    return 0;

}

