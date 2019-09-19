CC=gcc

CFLAGS = -g -c -Wall -pedantic
#CFLAGS = -ansi -c -Wall -pedantic

all: ucast myip file_copy rcv t_rcv t_ncp ncp

t_rcv: t_rcv.o
	    $(CC) -o t_rcv t_rcv.o  

t_ncp: t_ncp.o
	    $(CC) -o t_ncp t_ncp.o

ucast: ucast.o
	    $(CC) -o ucast ucast.o

myip: myip.o
	    $(CC) -o myip myip.o

file_copy: file_copy.o
	    $(CC) -o file_copy file_copy.o

rcv: rcv.o
		$(CC) -o rcv rcv.o

ncp: ncp.o
		$(CC) -o ncp ncp.o

clean:
	rm *.o
	rm t_ncp 
	rm t_rcv
	rm ucast
	rm myip
	rm file_copy
	rm rcv
	rm ncp

%.o:    %.c
	$(CC) $(CFLAGS) $*.c


