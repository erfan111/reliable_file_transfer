#include <stdio.h>

#include <stdlib.h>

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h> 
#include <netdb.h>

#include <errno.h>

#include <time.h>
#include <sys/time.h>
#include "sendto_dbg.h"

#define PORT	     10350

#define MAX_MESS_LEN 1400

#define WINDOW_SIZE 500

enum STATUS {
    WAITING,
    STARTING,
    SENDING,
    FINALIZING,
    INORDER_RECEIVING,
    OUTOFORDER_RECEIVING
};

enum MESSAGE_TYPE {
    FILE_SEND_REQUEST_MSG,
    FINALIZE_MSG,
    DATA_MSG,
    FEEDBACK_MSG,
    POLL_MSG,
    WAIT_MSG
};