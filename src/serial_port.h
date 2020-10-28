/* serial_port.h */


#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H 1


#if __WIN32__

#include <stdlib.h>

typedef HANDLE MYFDTYPE;
#define MYFDERROR INVALID_HANDLE_VALUE

#else

typedef int MYFDTYPE;
#define MYFDERROR (-1)

#endif


extern MYFDTYPE serial_port_open (char *devicename, int baud);

extern int serial_port_write (MYFDTYPE fd, char *str, int len);

extern int serial_port_get1 (MYFDTYPE fd);

extern void serial_port_close (MYFDTYPE fd);


#endif