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

#define PORT	     10350

#define MAX_MESS_LEN 1400

#define WINDOW_SIZE 100
