#include "net_include.h"

#define NAME_LENGTH 80

////
#define WINDOW_SIZE 3

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
    int nack_sent;
} Window_slot;

typedef struct file_to_transmit_t {
    int fd;
    FILE *fw; /* Pointer to dest file, which we write  */
    char* file_name;
    int filename_length;
    int file_size;
    int file_offset_to_receive;
    int total_bytes_receive;
} file_to_transmit;

typedef struct Session_t {
    struct sockaddr_in connection;
    Window_slot slots[WINDOW_SIZE];
    int seq_number_to_receive;
    int window_start_pointer;
    enum STATUS status;
    file_to_transmit file;
    struct timeval receive_start;
    struct timeval recent_progress_timestamp;
    int recent_progress_bytes_receive;
    int socket;
} Session;

Session session;

int send_reply(int type, char* payload, int size)
{
    char message[1400];
    int ret;
    char type2 = type & 0x000000ff;
    uint8_t second = size & 0x000000ff;
    uint8_t first = (size >> (8)) & 0x000000ff;
    // printf("size(%d): %d --- %d \n", size, first, second);
    message[0] = type2;
    message[1] = first;
    message[2] = second;
    memcpy( message + 3, payload, size );
    // sprintf(&message[3], "%s",payload);
    // printf("DBG: replying [%x %x %x %x %x] (%s), %d\n", message[0], message[1], message[2], message[3], message[4], message, session.socket);
    ret = sendto( session.socket, message, size+3, 0, 
                    (struct sockaddr *)&session.connection, sizeof(session.connection) );
    printf("DBG: sent %d bytes\n", ret);
    if(ret != strlen(message)) 
    {
        perror( "Net_client: error in writing");
        exit(1);
    }
    return 0;
}

int send_feedback_message()
{
    if (session.status == INORDER_RECEIVING)
    {
        char msg[1400];
        sprintf(msg, "A%d", session.seq_number_to_receive);
        printf("DBG: receiving in order, so sending an ack for %d (%s)\n", session.seq_number_to_receive, msg);
        send_reply(3, msg, strlen(msg));
    }
    else
    {

    }
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

int get_payload_size(char *buffer)
{
    return (buffer[1] << 8) | buffer[2];
}

int handle_file_send_request(int size, char* buffer, struct sockaddr_in connection)
{
    printf("DBG: handling file send request, %d\n", size);
    session.seq_number_to_receive = 0;
    session.status = INORDER_RECEIVING;
    session.connection = connection;
    session.window_start_pointer = 0;
    session.file.file_name = get_payload(buffer, size);
    printf("DBG: file name is %s\n", session.file.file_name);
    if((session.file.fw = fopen(session.file.file_name, "w")) == NULL) {
        perror("fopen");
        exit(0);
    }
    int i = WINDOW_SIZE;
    for(i=0; i < WINDOW_SIZE; i++)
    {
        session.slots[i].valid = 0;
        session.slots[i].nack_sent = 0;
    }
    send_feedback_message(); // look at the window and send the feedback
    return 0;
}

void flush_to_file()
{

}

int handle_finalize()
{
    flush_to_file();
    fclose(session.file.fw);
    session.status = WAITING;
    send_feedback_message();
    return 0;
}

void insert_into_window(int index, char*buffer, int size)
{
    memcpy( session.slots[index].data, &buffer[5], size);
    session.slots[index].size = size;
    session.slots[index].valid = 1;
}

void check_order(int from, int current)
{
    int i, ordered = 1;
    if(current < session.window_start_pointer)
    {
        for(i=session.window_start_pointer; i<WINDOW_SIZE;i++)
        {
            if(session.slots[i].valid == 0)
            {
                ordered = 0;
                break;
            }
        }
        for(i=0; i<current && ordered;i++)
        {
            if(session.slots[i].valid == 0)
            {
                ordered = 0;
                break;
            }
        }
    }
    else
    {
        for(i=session.window_start_pointer; i<WINDOW_SIZE;i++)
        {
            if(session.slots[i].valid == 0)
            {
                session.status = OUTOFORDER_RECEIVING;
                break;
            }
        }
    }
    if(ordered)
        session.status = INORDER_RECEIVING;
    else
        session.status = OUTOFORDER_RECEIVING;

}

int handle_file_receive(int size, char* buffer)
{
    unsigned short seq_num = buffer[3];
    if (session.seq_number_to_receive > WINDOW_SIZE)    //e.g.:   0 ] 1  2  3  4 [ 5  6  7 
    {
        if(seq_num > session.seq_number_to_receive || seq_num < ((session.seq_number_to_receive + WINDOW_SIZE -1)% WINDOW_SIZE))
        {
            if(seq_num >= session.seq_number_to_receive)
            {
                int index =  seq_num - session.seq_number_to_receive + session.window_start_pointer;
                if(!session.slots[index%WINDOW_SIZE].valid)
                    insert_into_window(index%WINDOW_SIZE, buffer, size-2);
            }
            else 
            {
                int index =  (2*WINDOW_SIZE) - session.seq_number_to_receive + seq_num + session.window_start_pointer;
                if(!session.slots[index%WINDOW_SIZE].valid)
                    insert_into_window(index%WINDOW_SIZE, buffer, size-2);
            }
        }
    }
    else
    {
        if(seq_num > session.seq_number_to_receive && seq_num < (session.seq_number_to_receive + WINDOW_SIZE))
        {
            int index =  seq_num - session.seq_number_to_receive + session.window_start_pointer;
            if(!session.slots[index%WINDOW_SIZE].valid)
                insert_into_window(index%WINDOW_SIZE, buffer, size-2);
        }

    }
    
    return 0;
}

int parse(char* buffer, int length, struct sockaddr_in connection)
{
    printf("DBG: parsing ...\n");
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
            handle_finalize();
            break;
        case 2:
            printf("DBG: message type is data\n");
            payload_size = get_payload_size(buffer);
            handle_file_receive(payload_size, buffer);
            break;
        default:
            fprintf(stderr, "invalid message type ...\n");
            break;
    }
    return 0;
}

////

int main()
{
    struct sockaddr_in    name;
    struct sockaddr_in    from_addr;
    socklen_t             from_len;
    int                   from_ip;
    int                   sr;
    fd_set                mask;
    fd_set                read_mask, write_mask, excep_mask;
    int                   bytes;
    int                   num;
    char                  mess_buf[MAX_MESS_LEN];
    struct timeval        timeout;

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
    session.status = WAITING;

    FD_ZERO( &mask );
    FD_ZERO( &write_mask );
    FD_ZERO( &excep_mask );
    FD_SET( sr, &mask );
    FD_SET( (long)0, &mask ); /* stdin */
    for(;;)
    {
        read_mask = mask;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        num = select( FD_SETSIZE, &read_mask, &write_mask, &excep_mask, &timeout);
        if (num > 0) {
            if ( FD_ISSET( sr, &read_mask) ) {
                from_len = sizeof(from_addr);
                bytes = recvfrom( sr, mess_buf, sizeof(mess_buf), 0,  
                          (struct sockaddr *)&from_addr, 
                          &from_len );
                mess_buf[bytes] = 0;
                from_ip = from_addr.sin_addr.s_addr;

                printf( "Received from (%d.%d.%d.%d): %s\n", 
                                (htonl(from_ip) & 0xff000000)>>24,
                                (htonl(from_ip) & 0x00ff0000)>>16,
                                (htonl(from_ip) & 0x0000ff00)>>8,
                                (htonl(from_ip) & 0x000000ff),
                                mess_buf );
                parse(mess_buf, bytes, from_addr);
            }
        } else {
            printf(".");
            fflush(0);
        }
    }
    return 0;
}