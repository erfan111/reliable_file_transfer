#include "net_include.h"

#define NAME_LENGTH 80

int window_size_override;
int debug_mode = 0;
////

typedef struct Window_slot_t {
    char data[1395];
    int size;
    int valid;
    int nack_sent;
} Window_slot;

typedef struct file_to_transmit_t {
    int fd;
    FILE *fw; /* Pointer to dest file, which we write  */
    char* file_name;
    int filename_length;
    int file_size;
    int file_offset_to_receive;
    unsigned long total_bytes_receive;
} file_to_transmit;

typedef struct Session_t {
    struct sockaddr_in connection;
    Window_slot *slots;
    int window_start_sequence_number;
    int window_start_pointer;
    enum STATUS status;
    file_to_transmit file;
    struct timeval receive_start, receive_end;
    struct timeval recent_progress_start_timestamp, recent_progress_end_timestamp;
    unsigned long recent_progress_bytes_receive;
    int socket;
    int last_valid_index;
} Session;

void check_order();

Session session;

int send_reply(int type, char* payload, int size)
{
    char message[1400];
    int ret;
    char type2 = type & 0x000000ff;
    uint8_t second = size & 0x000000ff;
    uint8_t first = (size >> (8)) & 0x000000ff;
    message[0] = type2;
    message[1] = first;
    message[2] = second;
    memcpy( message + 3, payload, size );
   
    ret = sendto( session.socket, message, size+3, 0, 
                    (struct sockaddr *)&session.connection, sizeof(session.connection) );
    if(ret != size+3) 
    {
        perror( "Net_client: error in writing to socket");
        exit(1);
    }
    return 0;
}

int send_feedback_message() // TODO: call it when window is completely received
{
    int i, nwritten, nack_seq, reached_invalid = 0, window_start_incremnt = 0;
    char msg[1400], amsg[10], nmsg[10];
    if(debug_mode)
        printf("DBG: sending feedback message\n");
    check_order();
    for(i=session.window_start_pointer; i< window_size_override;i++)
    {
        if(session.slots[i].valid)
        {
            nwritten = fwrite(session.slots[i].data, 1, session.slots[i].size, session.file.fw);
            if (nwritten < session.slots[i].size) {
                printf("An error occurred when writing to file...\n");
                exit(0);
            }

            session.file.total_bytes_receive += nwritten;
            session.recent_progress_bytes_receive += nwritten;
            if(session.recent_progress_bytes_receive >= 104857600)
            {
                unsigned long receive_duration;
                gettimeofday(&session.recent_progress_end_timestamp, NULL);
                receive_duration = (session.recent_progress_end_timestamp.tv_sec - session.recent_progress_start_timestamp.tv_sec)*1000000 + (session.recent_progress_end_timestamp.tv_usec - session.recent_progress_start_timestamp.tv_usec);
                
                printf("Reporting: Total Bytes = %luMB , Total Time = %lu MSecs, Average Transfer Rate = %luMbPS \n", session.file.total_bytes_receive/(1048576), receive_duration/1000, (session.recent_progress_bytes_receive*8)/receive_duration);
                gettimeofday(&session.recent_progress_start_timestamp, NULL);
                session.recent_progress_bytes_receive = 0;
            }

            session.slots[i].valid = 0;
            window_start_incremnt++;
            
            session.window_start_sequence_number++;
            if(session.window_start_sequence_number >= 2*window_size_override)
                session.window_start_sequence_number = 0;
        }
        else{
            reached_invalid = 1;
            break;
        }
    }
    for(i=0; i< session.window_start_pointer && !reached_invalid;i++)
    {
        if(session.slots[i].valid)
        {
            nwritten = fwrite(session.slots[i].data, 1, session.slots[i].size, session.file.fw);
            if (nwritten < session.slots[i].size) {
                printf("An error occurred when writing to file...\n");
                exit(0);
            }
            session.file.total_bytes_receive += nwritten;
            session.recent_progress_bytes_receive += nwritten;
            if(session.recent_progress_bytes_receive >= 104857600)
            {
                unsigned long receive_duration;
                gettimeofday(&session.recent_progress_end_timestamp, NULL);
                receive_duration = (session.recent_progress_end_timestamp.tv_sec - session.recent_progress_start_timestamp.tv_sec)*1000000 + (session.recent_progress_end_timestamp.tv_usec - session.recent_progress_start_timestamp.tv_usec);
                
                printf("Reporting: Total Bytes = %luMB , Total Time = %lu MSecs, Average Transfer Rate = %luMbPS \n", session.file.total_bytes_receive/1048576, receive_duration/1000, (session.recent_progress_bytes_receive*8)/receive_duration);
                gettimeofday(&session.recent_progress_start_timestamp, NULL);
                session.recent_progress_bytes_receive = 0;
            }

            session.slots[i].valid = 0;
            window_start_incremnt++;
            session.window_start_sequence_number++;
            if(session.window_start_sequence_number >= 2*window_size_override)
                session.window_start_sequence_number = 0;
        }
        else{
            reached_invalid = 1;
            break;
        }
    }
    session.window_start_pointer = (session.window_start_pointer + window_start_incremnt) % window_size_override;
    sprintf(amsg, "A%d,", session.window_start_sequence_number);
    if(debug_mode)
        printf("DBG: receiving in order, so sending an ack for %d (%s)\n", session.window_start_sequence_number, amsg);
    strcpy(msg, amsg);
    if(session.status == OUTOFORDER_RECEIVING)
    {

        if(session.window_start_pointer  <= session.last_valid_index)
        {
            for(i=session.window_start_pointer; i< session.last_valid_index;i++)
            {
                if(!session.slots[i].valid)
                {
                    nack_seq = (i-session.window_start_pointer + session.window_start_sequence_number)% (2*window_size_override);
                    if(debug_mode)
                        printf("DBG: also receiving out of order, so sending nack for %d\n", nack_seq);
                    sprintf(nmsg, "N%d,", nack_seq);
                    strcat(msg,nmsg);
                }
            }
        }
        else {
            for(i=session.window_start_pointer; i< window_size_override;i++)
            {
                if(!session.slots[i].valid)
                {
                    nack_seq = (i-session.window_start_pointer + session.window_start_sequence_number)% (2*window_size_override);
                    if(debug_mode)
                        printf("DBG: also receiving out of order, so sending nack for %d\n", nack_seq);
                    sprintf(nmsg, "N%d,", nack_seq);
                    strcat(msg,nmsg);
                }
            }
            for(i=0; i<= session.last_valid_index;i++)
            {
                if(!session.slots[i].valid)
                {
                    nack_seq = (window_size_override - session.window_start_pointer + session.window_start_sequence_number + i)% (2*window_size_override);
                    if(debug_mode)
                        printf("DBG: also receiving out of order, so sending nack for %d\n", nack_seq);
                    sprintf(nmsg, "N%d,", nack_seq);
                    strcat(msg,nmsg);
                }
            }
        }
        
    }

    send_reply(3, msg, strlen(msg));
    
    return 0;
}

int send_finalize_message()
{
    send_reply(1, NULL, 0);
    return 0;
}

char* get_payload(char *buffer, int size)
{
    char *payload;
    payload = malloc(size+1);
    memcpy( payload, &buffer[3], size );
    payload[size] = '\0';
    return payload;
}

u_int16_t get_payload_size(char *buffer)
{
    char psize[2];
    int size;
    memcpy(psize, buffer + 1, 2);
    size = *((u_int16_t*)psize);
    return size;
}

int handle_file_send_request(int size, char* buffer, struct sockaddr_in connection)
{
    if(debug_mode)
        printf("DBG: handling file send request\n");
    gettimeofday(&session.receive_start, NULL);
    gettimeofday(&session.recent_progress_start_timestamp, NULL);
    session.window_start_sequence_number = 0;
    session.status = INORDER_RECEIVING;
    session.connection = connection;
    session.window_start_pointer = 0;
    session.file.file_name = get_payload(buffer, size);
    
    printf("Receiving file name is %s\n", session.file.file_name);
    if((session.file.fw = fopen(session.file.file_name, "w")) == NULL) {
        perror("fopen");
        exit(0);
    }
    int i = window_size_override;
    for(i=0; i < window_size_override; i++)
    {
        session.slots[i].valid = 0;
        session.slots[i].nack_sent = 0;
    }
    send_feedback_message(); // look at the window and send the feedback
    return 0;
}

int handle_finalize(char *buffer)
{
    unsigned long receive_duration;
    if(debug_mode)
        printf("DBG: handling finalize\n");
    fclose(session.file.fw);
    session.status = WAITING;
    gettimeofday(&session.receive_end, NULL);
    receive_duration = (session.receive_end.tv_sec - session.receive_start.tv_sec)*1000000 + (session.receive_end.tv_usec - session.receive_start.tv_usec);
    printf("Total Bytes = %luMB , Total Time = %lu MSecs, Average Transfer Rate = %luMbPS \n", session.file.total_bytes_receive/1048576, receive_duration/1000, (session.file.total_bytes_receive*8)/receive_duration);

    send_finalize_message();
    return 0;
}

void insert_into_window(int index, char*buffer, int size)
{
    if(debug_mode)
        printf("DBG: inserting into array index = %d, size = %d\n", index, size);
    memcpy( session.slots[index].data, buffer + 5, size);
    session.slots[index].size = size;
    session.slots[index].valid = 1;
}

void check_order()
{
    int i, saw_invalid = 0;
    session.status = INORDER_RECEIVING;
    for(i=session.window_start_pointer; i<window_size_override;i++) //TODO: only 1 for, use ++i%W
    {
        if(session.slots[i].valid == 0)
            saw_invalid = 1;
        else
        {
            session.last_valid_index = i;
            if(saw_invalid)
                session.status = OUTOFORDER_RECEIVING;
        }
    }
    for(i=0; i < session.window_start_pointer;i++)
    {
        if(session.slots[i].valid == 0)
            saw_invalid = 1;
        else
        {
            session.last_valid_index = i;
            if(saw_invalid)
                session.status = OUTOFORDER_RECEIVING;
        }
    }
    if(debug_mode)
        printf("DBG: checking order session.status = %d\n", session.status);

}

int handle_file_receive(int size, char* buffer)
{
    u_int16_t seq_num;
    char sn_buffer[2];
    memcpy(sn_buffer, buffer + 3, 2);
    seq_num = *((u_int16_t*)sn_buffer);
    if(debug_mode)
        printf("DBG: handling file receive, seq_num received is %u, our seqnum start=%d\n", seq_num, session.window_start_sequence_number);
    if (session.window_start_sequence_number > window_size_override)    //e.g.:   0 ] 1  2  3  4 [ 5  6  7 
    {
        if(debug_mode)
            printf("DBG: our window is intercepting the 2W\n");
        if(seq_num >= session.window_start_sequence_number || seq_num <= ((session.window_start_sequence_number + window_size_override -1)% window_size_override))
        {
            if(seq_num >= session.window_start_sequence_number)
            {
                if(debug_mode)
                    printf("DBG: finding array index path1\n");
                int index =  seq_num - session.window_start_sequence_number + session.window_start_pointer;
                if(!session.slots[index%window_size_override].valid)
                    insert_into_window(index%window_size_override, buffer, size-2);
            }
            else 
            {
                int index =  seq_num - session.window_start_sequence_number + session.window_start_pointer;
                if(debug_mode)
                    printf("DBG: finding array index path2, index = %d - \n", index);
                if(!session.slots[index%window_size_override].valid)
                    insert_into_window(index%window_size_override, buffer, size-2);
            }
        }
        else
        {
            if(debug_mode)
                printf("DBG: out of window sequence number\n");
        }
            
    }
    else    //e.g.:   0 [ 1  2  3  4 ] 5  6  7 
    {
        if(debug_mode)
            printf("DBG: our window is not intercepting the 2W\n");
        if(seq_num >= session.window_start_sequence_number && seq_num < (session.window_start_sequence_number + window_size_override))
        {
            int index = seq_num - session.window_start_sequence_number + session.window_start_pointer;
            if(!session.slots[index%window_size_override].valid)
                insert_into_window(index%window_size_override, buffer, size-2);
        }
        else
        {
            if(debug_mode)
                printf("DBG: out of window sequence number\n");
        }

    }
    
    return 0;
}

int parse(char* buffer, int length, struct sockaddr_in connection)
{
    int type = buffer[0];
    int payload_size, file_name_size;
    switch(type){
        case 0:
            printf("DBG: message type is file transfer initialization\n");
            file_name_size = get_payload_size(buffer);
            handle_file_send_request(file_name_size, buffer, connection);
            break;
        case 1:
            printf("DBG: message type is finalize\n");
            handle_finalize(buffer);
            break;
        case 2:
            payload_size = get_payload_size(buffer);
            handle_file_receive(payload_size, buffer);
            break;
        default:
            fprintf(stderr, "invalid message type ...\n");
            break;
    }
    return 0;
}

int main(int argc, char** argv)
{
    struct sockaddr_in    name;
    struct sockaddr_in    from_addr;
    socklen_t             from_len;
    int                   sr;
    fd_set                mask;
    fd_set                read_mask, write_mask, excep_mask;
    int                   bytes;
    int                   num;
    char                  mess_buf[MAX_MESS_LEN];
    struct timeval        timeout;

    window_size_override = WINDOW_SIZE;

    // optional args
    if(argc == 3)
    {
        debug_mode = atoi(argv[2]);
        window_size_override = atoi(argv[1]);
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

    session.slots = malloc(window_size_override * sizeof(Window_slot));
    session.socket = sr;
    session.status = WAITING;

    FD_ZERO( &mask );
    FD_ZERO( &write_mask );
    FD_ZERO( &excep_mask );
    FD_SET( sr, &mask );
    for(;;)
    {
        read_mask = mask;
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000;
        num = select( FD_SETSIZE, &read_mask, &write_mask, &excep_mask, &timeout);
        if (num > 0) {
            if ( FD_ISSET( sr, &read_mask) ) {
                from_len = sizeof(from_addr);
                bytes = recvfrom( sr, mess_buf, sizeof(mess_buf), 0,  
                          (struct sockaddr *)&from_addr, 
                          &from_len );
                mess_buf[bytes] = 0;
                parse(mess_buf, bytes, from_addr);
            }
        } else {    // timeout: send feedback
            if(session.status != WAITING)
                send_feedback_message();
        }
    }
    return 0;
}