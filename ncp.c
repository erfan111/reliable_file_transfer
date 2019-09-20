#include "net_include.h"

#define NAME_LENGTH 80
#define READ_BUF_SIZE 1395

int gethostname(char*,size_t);

void PromptForHostName( char *my_name, char *host_name, size_t max_len ); 


enum STATUS {
    WAITING,
    STARTING,
    SENDING,
    FINALIZING,
    INORDER_RECEIVING,
    OUTOFORDER_RECEIVING
};

typedef struct Window_slot_t {
    char* data;
    int size;
    int valid;
    int sent;
} Window_slot;

typedef struct file_to_transmit_t {
    int fd;
    FILE *fr; /* Pointer to source file, which we read  */
    char* file_name;
    int filename_length;
    int file_size;
    int file_offset_to_send;
    unsigned long total_bytes_sent;
} file_to_transmit;

typedef struct Session_t {
    struct sockaddr_in connection;
    Window_slot slots[WINDOW_SIZE];
    int seq_number_to_send;
    int window_start_pointer;
    enum STATUS status;
    file_to_transmit file;
    struct timeval send_start, send_end;
    struct timeval recent_progress_start_timestamp, recent_progress_end_timestamp;
    unsigned long recent_progress_bytes_sent;
    int socket;
    int loss_rate;
    char *dest_file_name;
    int dest_file_name_size;
    int finalize_flag;
    int last_slot_to_send_sequence_number;
} Session;

Session session;

int send_packet(int type, char * payload, int size)
{
    char message[1400];
    int ret;
    uint8_t second = size & 0x000000ff;
    uint8_t first = (size >> (8)) & 0x000000ff;
    message[0] = type & 0x000000ff;
    message[2] = first;
    message[1] = second;
    memcpy( message + 3, payload, size );
    if(size != 1397)
    {
        printf("payload = %x - %x, %d \n", (unsigned char)message[1], (unsigned char)message[2], second);
    }
    usleep(100);
    ret = sendto( session.socket, message, size+3, 0, 
                (struct sockaddr *)&session.connection, sizeof(session.connection) );
    printf("DBG: sent %d bytes\n", ret);
    if(ret != size+3) 
    {
        perror( "Net_client: error in writing");
        exit(1);
    }
    return 0;
}

int send_initialize_request()
{
    printf("DBG: sending initialize message\n");
    send_packet(0, session.dest_file_name, session.dest_file_name_size);
    return 0;
}

int send_poll_message()
{
    printf("DBG: sending poll message\n");
    send_packet(4, NULL, 0);
    return 0;
}

int send_finalize_message()
{
    char message[2];
    uint8_t second = session.seq_number_to_send & 0x000000ff;
    uint8_t first = (session.seq_number_to_send >> (8)) & 0x000000ff;
    printf("DBG: sending finalize message\n");
    message[1] = first;
    message[2] = second;
    send_packet(1, message, 2); // WARNING: DO NOT INCREMENT THIS WHEN WINDOW SENDING IS FINISHED!!!!!!!!
    return 0;
}

int start_sending_the_file()
{
    int i, nread;
    char buf[READ_BUF_SIZE+2];
    uint8_t second;
    uint8_t first;
    printf("DBG: starting to send the file\n");
    for(i=0;i < WINDOW_SIZE;i++)
    {
        session.slots[i].data = malloc(1395);
        session.slots[i].valid = 1;
        session.slots[i].sent = 1;
        nread = fread(session.slots[i].data, 1, READ_BUF_SIZE, session.file.fr);
        session.slots[i].size = nread;
        second = i & 0x000000ff;
        first = (i >> (8)) & 0x000000ff;
        buf[0] = second;
        buf[1] = first;
        memcpy(buf + 2, session.slots[i].data, session.slots[i].size);
        send_packet(2, buf, session.slots[i].size+2);

        session.file.total_bytes_sent += nread;
        session.recent_progress_bytes_sent += nread;
        if(session.recent_progress_bytes_sent >= 100000000)
        {
            long send_duration;
            gettimeofday(&session.recent_progress_end_timestamp, NULL);
            send_duration = (session.recent_progress_end_timestamp.tv_sec - session.recent_progress_start_timestamp.tv_sec)*1000000 + (session.recent_progress_end_timestamp.tv_usec - session.recent_progress_start_timestamp.tv_usec);
            printf("Reporting: Total Bytes = %lu , Total Time = %lu, Average Transfer Rate = %lu \n", session.recent_progress_bytes_sent, send_duration, (session.recent_progress_bytes_sent*8)/send_duration);
            gettimeofday(&session.recent_progress_start_timestamp, NULL);
            session.recent_progress_bytes_sent = 0;

        }
    }
    return 0;
}

int is_seqnum_in_window(int seq_number)
{
    if(session.seq_number_to_send > WINDOW_SIZE)
    {
        if(seq_number >= session.seq_number_to_send || seq_number < (2*WINDOW_SIZE - session.seq_number_to_send))
            return 1;
    }
    else
    {
        if(seq_number >= session.seq_number_to_send || seq_number < session.seq_number_to_send + WINDOW_SIZE + 1)
            return 1;
    }
    printf("DBG: seqnum received was not in window\n");
    return 0;
}

int handle_acknowledge(int sequence_number)
{
    printf("DBG: handling ack %d\n", sequence_number);
    if(session.finalize_flag)
    {
        if(sequence_number == (session.last_slot_to_send_sequence_number + 1)%(2*WINDOW_SIZE))
        {
            session.status = FINALIZING;
            send_packet(1, NULL, 0);
            return 0;
        }
    }

    if(is_seqnum_in_window(sequence_number))
    {
        int nread, new_seq_num;
        uint8_t first, second;
        char buf[READ_BUF_SIZE+2];
        while(session.seq_number_to_send != sequence_number)
        {
            if(!session.finalize_flag)
            {
                nread = fread(session.slots[session.window_start_pointer].data, 1, READ_BUF_SIZE, session.file.fr);
                session.slots[session.window_start_pointer].size = nread;
                if(nread < READ_BUF_SIZE) {
                    printf("DBG: FIN read less than buffer size = %d \n", nread);
                    if(feof(session.file.fr))
                    {
                        printf("DBG: FIN eof reached \n");
                        session.finalize_flag = 1;
                        session.last_slot_to_send_sequence_number = (session.seq_number_to_send+WINDOW_SIZE) % (2*WINDOW_SIZE);
                    }
                        
                }
                new_seq_num = (session.seq_number_to_send+WINDOW_SIZE) % (2*WINDOW_SIZE);
                second = new_seq_num & 0x000000ff;
                first = (new_seq_num >> (8)) & 0x000000ff;
                buf[0] = second;
                buf[1] = first;
                memcpy(buf + 2, session.slots[session.window_start_pointer].data, session.slots[session.window_start_pointer].size);
                
                send_packet(2, buf, session.slots[session.window_start_pointer].size+2);

                
                session.seq_number_to_send = (session.seq_number_to_send +1) % (2*WINDOW_SIZE);
                session.window_start_pointer = (session.window_start_pointer+1) % WINDOW_SIZE;
            }
                

            session.file.total_bytes_sent += nread;
            session.recent_progress_bytes_sent += nread;
            if(session.recent_progress_bytes_sent >= 100000000)
            {
                long send_duration;
                gettimeofday(&session.recent_progress_end_timestamp, NULL);
                send_duration = (session.recent_progress_end_timestamp.tv_sec - session.recent_progress_start_timestamp.tv_sec)*1000000 + (session.recent_progress_end_timestamp.tv_usec - session.recent_progress_start_timestamp.tv_usec);
                printf("Reporting: Total Bytes = %lu , Total Time = %lu, Average Transfer Rate = %lu \n", session.recent_progress_bytes_sent, send_duration, (session.recent_progress_bytes_sent*8)/send_duration);
                gettimeofday(&session.recent_progress_start_timestamp, NULL);
                session.recent_progress_bytes_sent = 0;

            }
        }
        // session.slots[session.window_start_pointer]
    }
    return 0;
}

int handle_nacknowledge(int sequence_number)
{
    printf("DBG: handling nack %d\n", sequence_number);
    if(is_seqnum_in_window(sequence_number))
    {
        uint8_t first, second;
        char buf[READ_BUF_SIZE+2];
        int index;
        if(sequence_number < session.seq_number_to_send)
            sequence_number += (2*WINDOW_SIZE);
        index = ((sequence_number - session.seq_number_to_send) + session.window_start_pointer) % WINDOW_SIZE;
        second = sequence_number & 0x000000ff;
        first = (sequence_number >> (8)) & 0x000000ff;
        buf[0] = second;
        buf[1] = first;
        memcpy(buf + 2, session.slots[index ].data, session.slots[index].size);
        send_packet(2, buf, session.slots[index].size+2);

    }
    return 0;
}

int parse_feedback_message(char * buffer, int size)
{
    u_int16_t payload_size =  ((buffer[1] & 0xff) << 8) | (buffer[2] & 0xff);
    int sequence_number;
    char payload[payload_size];
    memcpy(payload, buffer + 3, payload_size);
    char delimiter[] = ",";
    char *ptr = strtok(payload, delimiter);
    printf("DBG: parsing the feedback message %d\n", sequence_number);
    while(ptr != NULL)
    {
        if(ptr[0] == 'A')
        {
            sscanf(ptr, "A%d", &sequence_number);
            handle_acknowledge(sequence_number);
        }
        else if(ptr[0] == 'N')
        {
            sscanf(ptr, "N%d", &sequence_number);
            handle_nacknowledge(sequence_number);
        }
        ptr = strtok(NULL, delimiter);
    }
    return 0;

}

int parse(char *buffer, int size)
{
    printf("DBG: parsing message...\n");
    int type = buffer[0];
    if(type == 3)
    {
        switch (session.status)
        {
        case STARTING:
            session.status = SENDING;
            start_sending_the_file();
            break;
        case SENDING:
        case FINALIZING:
            parse_feedback_message(buffer, size);
            break;
        default:
            break;
        }
    }
    else if(type == 1)
    {
        unsigned long send_duration;
        gettimeofday(&session.send_end, NULL);
        send_duration = (session.send_end.tv_sec - session.send_start.tv_sec)*1000000 + (session.send_end.tv_usec - session.send_start.tv_usec);
        printf("Total Bytes = %lu , Total Time = %lu, Average Transfer Rate = %lu \n", session.file.total_bytes_sent, send_duration, (session.file.total_bytes_sent*8)/send_duration);
        exit(0);
    }
    else
    {
        fprintf(stderr, "Wrong msg type\n");
    }
    
    return 0;
}

int main(int argc, char **argv)
{
    struct sockaddr_in    name;
    struct sockaddr_in    send_addr;
    struct sockaddr_in    from_addr;
    socklen_t             from_len;
    struct hostent        h_ent;
    struct hostent        *p_h_ent;
    char                  host_name[NAME_LENGTH] = {'\0'};
    int                   host_num;
    int                   sr;
    fd_set                mask;
    fd_set                read_mask, write_mask, excep_mask;
    int                   bytes;
    int                   num;
    char                  mess_buf[MAX_MESS_LEN];
    char                  input_buf[80];
    struct timeval        timeout;

    int file_name_size;


    if(argc != 5) {
        printf("Usage: ncp <loss_rate> <source_file> <destination_file> <comp_name>\n");
        exit(0);
    }
    session.loss_rate = atoi(argv[1]);
    file_name_size = strlen(argv[2]);
    session.file.file_name = malloc(file_name_size+1);
    memcpy(session.file.file_name, argv[2], strlen(argv[2]));
    session.file.filename_length = file_name_size;

    session.dest_file_name_size = strlen(argv[3]);
    session.dest_file_name = malloc(session.dest_file_name_size+1);
    memcpy(session.dest_file_name, argv[3], strlen(argv[3]));

    strncpy(host_name, argv[4], strlen(argv[4]));

    printf("loss=%d, source = %s, dest = %s, hostname = %s \n", session.loss_rate, session.file.file_name, session.dest_file_name, host_name);
    
    sr = socket(AF_INET, SOCK_DGRAM, 0);  /* socket for receiving (udp) */
    if (sr<0) {
        perror("Ucast: socket");
        exit(1);
    }

    name.sin_family = AF_INET; 
    name.sin_addr.s_addr = INADDR_ANY; 
    name.sin_port = htons(PORT);

    if ( bind( sr, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("Ucast: bind");
        exit(1);
    }

    session.socket = sr;

    p_h_ent = gethostbyname(host_name);
    if ( p_h_ent == NULL ) {
        printf("Ucast: gethostbyname error.\n");
        exit(1);
    }

    memcpy( &h_ent, p_h_ent, sizeof(h_ent));
    memcpy( &host_num, h_ent.h_addr_list[0], sizeof(host_num) );

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = host_num; 
    send_addr.sin_port = htons(PORT);

    session.connection = send_addr;
    session.status = STARTING;
    gettimeofday(&session.recent_progress_start_timestamp, NULL);
    gettimeofday(&session.send_start, NULL);
    
    if((session.file.fr  = fopen(session.file.file_name, "r")) == NULL) {
        perror("fopen");
        exit(0);
    }
    send_initialize_request();

    FD_ZERO( &mask );
    FD_ZERO( &write_mask );
    FD_ZERO( &excep_mask );
    FD_SET( sr, &mask );
    FD_SET( (long)0, &mask ); /* stdin */
    for(;;)
    {
        read_mask = mask;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        num = select( FD_SETSIZE, &read_mask, &write_mask, &excep_mask, &timeout);
        if (num > 0) {
            if ( FD_ISSET( sr, &read_mask) ) {
                from_len = sizeof(from_addr);
                bytes = recvfrom( sr, mess_buf, sizeof(mess_buf), 0,  
                          (struct sockaddr *)&from_addr, 
                          &from_len );
                mess_buf[bytes] = 0;

                parse(mess_buf, bytes);

            }else if( FD_ISSET(0, &read_mask) ) {
                bytes = read( 0, input_buf, sizeof(input_buf) );
                input_buf[bytes] = 0;
                printf( "There is an input: %s\n", input_buf );
                sendto( sr, input_buf, strlen(input_buf), 0, 
                    (struct sockaddr *)&send_addr, sizeof(send_addr) );
            }
        } else {
            printf("DBG: timed out session.status =  %d\n", session.status);
            switch(session.status)
            {
                case STARTING:
                send_initialize_request();
                    break;
                case SENDING:
                    send_poll_message();
                    break;
                case FINALIZING:
                    send_finalize_message();
                    break;
                default:
                    break;
            }
            
        }
    }

    return 0;

}