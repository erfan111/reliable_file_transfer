#include "net_include.h"

#define NAME_LENGTH 80
#define READ_BUF_SIZE 1395

int window_size_override;
int debug_mode = 0;

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
    Window_slot *slots;
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
    usleep(100);
    ret = sendto_dbg( session.socket, message, size+3, 0, 
                (struct sockaddr *)&session.connection, sizeof(session.connection) );
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
    send_packet(1, message, 2);
    return 0;
}

int start_sending_the_file()
{
    int i, nread;
    char buf[READ_BUF_SIZE+2];
    uint8_t second;
    uint8_t first;
    if(debug_mode)
        printf("DBG: starting to send the file\n");
    for(i=0;i < window_size_override;i++)
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
        if(session.recent_progress_bytes_sent >= 104857600 )
        {
            long send_duration;
            gettimeofday(&session.recent_progress_end_timestamp, NULL);
            send_duration = (session.recent_progress_end_timestamp.tv_sec - session.recent_progress_start_timestamp.tv_sec)*1000000 + (session.recent_progress_end_timestamp.tv_usec - session.recent_progress_start_timestamp.tv_usec);
            printf("Reporting: Total Bytes = %luMB , Total Time = %luMSecs, Average Transfer Rate = %luMbPS \n", session.file.total_bytes_sent/1048576, send_duration/1000, (session.recent_progress_bytes_sent*8)/send_duration);
            gettimeofday(&session.recent_progress_start_timestamp, NULL);
            session.recent_progress_bytes_sent = 0;

        }
    }
    return 0;
}

int is_seqnum_in_window(int seq_number)
{
    if(session.seq_number_to_send +1 >= window_size_override)  //overlapping
    {
        if((seq_number >= session.seq_number_to_send && seq_number <= ((2*window_size_override)-1)) || (seq_number <= (session.seq_number_to_send - window_size_override)))
            return 1;
    }
    else
    {
        if(seq_number >= session.seq_number_to_send && seq_number < (session.seq_number_to_send + window_size_override+1))
            return 1;
    }
    if(debug_mode)
        printf("DBG: seqnum received was not in window and wasn't the start of the next window.\n");
    return 0;
}

int handle_acknowledge(int sequence_number)
{
    if(debug_mode)
        printf("DBG: handling ack %d\n", sequence_number);
    if(session.finalize_flag)
    {
        if(debug_mode)
            printf("DBG: finalize flag is set, our last slot is %d, seqnum recvd is %d\n", session.last_slot_to_send_sequence_number, sequence_number);
        if(sequence_number == (session.last_slot_to_send_sequence_number + 1)%(2*window_size_override))
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
        while(session.seq_number_to_send != sequence_number && !session.finalize_flag)
        {
            if(debug_mode)
                printf("DBG: session seqnum = %d, seqnum received = %d, finalize = %d \n", session.seq_number_to_send, sequence_number, session.finalize_flag);

            nread = fread(session.slots[session.window_start_pointer].data, 1, READ_BUF_SIZE, session.file.fr);
            session.slots[session.window_start_pointer].size = nread;
            if(nread < READ_BUF_SIZE) {
                if(debug_mode)
                    printf("DBG: read less than buffer size = %d \n", nread);
                if(feof(session.file.fr))
                {
                    if(debug_mode)
                        printf("DBG: eof reached \n");
                    session.finalize_flag = 1;
                    session.last_slot_to_send_sequence_number = (session.seq_number_to_send+window_size_override) % (2*window_size_override);
                }
                    
            }
            new_seq_num = (session.seq_number_to_send+window_size_override) % (2*window_size_override);
            second = new_seq_num & 0x000000ff;
            first = (new_seq_num >> (8)) & 0x000000ff;
            buf[0] = second;
            buf[1] = first;
            memcpy(buf + 2, session.slots[session.window_start_pointer].data, session.slots[session.window_start_pointer].size);
            if(debug_mode)
                printf("DBG: sending data with seq = %d, our window start is=%d\n", new_seq_num, session.seq_number_to_send);
            send_packet(2, buf, session.slots[session.window_start_pointer].size+2);

            
            session.seq_number_to_send = (session.seq_number_to_send +1) % (2*window_size_override);
            session.window_start_pointer = (session.window_start_pointer+1) % window_size_override;

            session.file.total_bytes_sent += nread;
            session.recent_progress_bytes_sent += nread;
            if(session.recent_progress_bytes_sent >= 104857600)
            {
                long send_duration;
                gettimeofday(&session.recent_progress_end_timestamp, NULL);
                send_duration = (session.recent_progress_end_timestamp.tv_sec - session.recent_progress_start_timestamp.tv_sec)*1000000 + (session.recent_progress_end_timestamp.tv_usec - session.recent_progress_start_timestamp.tv_usec);
                printf("Reporting: Total Bytes = %luMB , Total Time = %luMSecs, Average Transfer Rate = %luMbPS \n", session.file.total_bytes_sent/1048576, send_duration/1000, (session.recent_progress_bytes_sent*8)/send_duration);
                gettimeofday(&session.recent_progress_start_timestamp, NULL);
                session.recent_progress_bytes_sent = 0;

            }
        }
    }
    return 0;
}

int handle_nacknowledge(int sequence_number)
{
    if(debug_mode)
        printf("DBG: handling nack %d\n", sequence_number);
    if(is_seqnum_in_window(sequence_number))
    {
        uint8_t first, second;
        char buf[READ_BUF_SIZE+2];
        int index;
        if(sequence_number < session.seq_number_to_send)
            sequence_number += (2*window_size_override);
        index = ((sequence_number - session.seq_number_to_send) + session.window_start_pointer) % window_size_override;
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
        printf("Total Bytes = %luMB , Total Time = %lu MSecs, Average Transfer Rate = %luMbPS \n", session.file.total_bytes_sent/1048576, send_duration/1000, (session.file.total_bytes_sent*8)/send_duration);
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
    struct timeval        timeout;
    char                  delim[] = "@";
    int                   file_name_size;
    char*                 destination_string;

    window_size_override = WINDOW_SIZE;
    if(argc != 4 && argc != 6) {
        printf("Usage: ncp <loss_rate> <source_file> <destination_file>@<comp_name> [window_size_overrride] [debug mode = {1}]\n");
        exit(0);
    }
    session.loss_rate = atoi(argv[1]);
    file_name_size = strlen(argv[2]);
    session.file.file_name = malloc(file_name_size+1);
    memcpy(session.file.file_name, argv[2], strlen(argv[2]));
    session.file.filename_length = file_name_size;

    destination_string = strtok(argv[3], delim);
    session.dest_file_name_size = strlen(destination_string);
    session.dest_file_name = malloc(session.dest_file_name_size+1);
    memcpy(session.dest_file_name, destination_string, strlen(destination_string));
    destination_string = strtok(NULL, delim);
    strncpy(host_name, destination_string, strlen(destination_string));

    printf("loss=%d, source = %s, dest = %s, hostname = %s \n", session.loss_rate, session.file.file_name, session.dest_file_name, host_name);
    sendto_dbg_init(session.loss_rate);
    if(argc == 6)
    {
        debug_mode = atoi(argv[5]);
        window_size_override = atoi(argv[4]);
        printf("debug mode = %d, window size = %d \n", debug_mode, window_size_override);
    }

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

    session.slots = malloc(window_size_override * sizeof(Window_slot));
    session.connection = send_addr;
    session.status = STARTING;
    session.window_start_pointer = 0;
    session.seq_number_to_send = 0;
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

            }
        } else {
            if(debug_mode)
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