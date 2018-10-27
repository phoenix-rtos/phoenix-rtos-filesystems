# MeterFS

MeterFS is a file system created for meter profile data storage. MeterFS writes files as a log file - the file has a fixed, pre-allocated size, and saves records with a fixed, predefined size. The MeterFS API allows new files creation (if there is free space for the file) and change of file parameters (e.g. file size and record, but within a predefined area). File delete is not supported, however the content of the file can be erased. File names can be up to 8 characters in length.

## Preliminary information

### Rules for file allocation

The space for files is allocated in sector granulation. SPI flash memories usually have a sector size of 4096 bytes. The file must be allocated not less than the number of sectors resulting from its size + one sector. 
Calculation of the number of sectors needed:

Let's assume that a file of size x is needed, and it will contain records of size y. The number of records in the file results from the two previous parameters z = x / y (rounded down to unity). This file will need the following number of bytes in flash memory: (y + 4) * z bytes (plus 4 results from the need to store metadata for each of the records). The obtained size should be rounded up to a multiple of the size of the sector and add at least one sector (there may be more for frequently saved files, which will allow better wear leveling).

Example: We allocate a 1 000-size file that holds size 10 records. The file will store up to 100 (1 000/10) records. The total space occupied by a file is 100 * (10 + 4) = 1 400 bytes. 1 400 bytes are in one sector. We add one more sector and eventually we need to allocate 2 sectors for this file.

### Rules for saving files

Data can only be added to the file, the data already saved cannot be modified or deleted (excluding erasing the whole file). It is only possible to record entire record at once. Attempting to save the number of bytes smaller than the size of the record will result in data being added to the full size of the record, attempt on saving a larger number of bytes than the record size will result in saving only the start of the buffer equal to the record size, the rest will be ignored. If the file already holds the maximum number of entries (resulting from its size and size of the record), saving the next record will delete the oldest entry. The maximum number of files is 255.

### Rules for reading files

During the reading, free access to the data is possible, the file is treated as a continuous data buffer (as in a typical file system). To read the nth x size record from the whole file, ask for data from the n * x offset with the length x. The record is numbered from the oldest, i.e. the oldest record starts under offset 0.

##Interprocess message interface
First, get the port number of the meterfs server process. To do this, execute the system call:
>
    lookup("/", &port);

If lookup returned 0, the port variable meterfs contains the meterfs port number. Otherwise initialization of the meterfs process may not have ended yet and you should try again.

Definitions of structures used to communicate with meterfs are found in libphoenix:
>
    include <sys/fs.h>

### New file allocation

To create a new file, follow the steps in the right order. First, open the file:

>
    fsopen_t open;
    unsigned int id;
>
    strncpy(open.name, "filename", 8);
    open.mode = 0; /* not used for now */
    send(port, OPEN, &open, sizeof(open) + strlen("filename"), NORMAL, &id, sizeof(id));

`send` should return the size of id (4). The id variable now has the filename file descriptor.

The new file cannot be used yet, because there is no allocated space in the flash memory for it. As the next step the number of sectors for it (see Rules for file allocating) has to be allocated:
enum { METERFS_ALOCATE = 0, METERFS_RESIZE, METERFS_RECORDSZ };

>
    fsfcntl_t fcntl;
>
    fcntl.id = id;
    fcntl.cmd = METERFS_ALOCATE;
    fcntl.arg = 2; /* number of sectors */
>
    send(port, DEVCTL, &fcntl, sizeof(fcntl), NORMAL, NULL, 0);
    
`send` should return 0.

Next step will configure the file size and record size:

>
    fcntl.id = id;
    fcntl.cmd = METERFS_RESIZE;
    fcntl.arg = 1000; /* File size in bytes */
>
    send(port, DEVCTL, &fcntl, sizeof(fcntl), NORMAL, NULL, 0);
>
    fcntl.id = id;
    fcntl.cmd = METERFS_RECORDSZ;
    fcntl.arg = 10; /* Record size in bytes */
>
    send(port, DEVCTL, &fcntl, sizeof(fcntl), NORMAL, NULL, 0);

`send` return 0 in both cases.

File is created and ready to use.

### Saving data to file

In the example record size equal to 10 is assumed.
>
    unsigned char buff[10]; 
    fsdata_t data;
>
    data.id = id; /* ID from opening the file */
    data.pos = 0; /* doesn't matter */
    data.bufflen = 10; /* record size */
    memcpy(data.buff, buff, 10);
>
    send(port, WRITE, &u.data, sizeof(data) + 10, NORMAL, NULL, 0);
    
`send` should return the number of saved bytes (from zero to record size).

### Reading data from file

In the example, the second record is read. Record size is equal to 10.
>
    fsdata_t data;
    unsigned char buff[10];
>
    data.id = id; /* ID from opening the file */
    data.pos = 10; /* offset of second from last record */
    data.bufflen = 10; /* record size */
>
    send(port, READ, &u.data, sizeof(u), NORMAL, buff, sizeof(buff));
    
`send` should return the numer of read bytes.

### File close
>
    fsclose_t close;
>
    close = id; /* ID from opening the file */
>
    send(port, CLOSE, &close, sizeof(close), NORMAL, NULL, 0);

`send` should return 0.

### Changing file parameters

Record and file size can be changed for existing file. This change will result in erasing the whole content of the file. The number of allocated sectors can not be changed. After the parameters change the file still has to fit into previously allocated number of sectors, otherwise the parameter change will fail.

File size change example:
>
    fcntl.id = id;
    fcntl.cmd = METERFS_RESIZE;
    fcntl.arg = 1000; /* File size in bytes */
>
    send(port, DEVCTL, &fcntl, sizeof(fcntl), NORMAL, NULL, 0);
    Record size change:
    fcntl.id = id;
    fcntl.cmd = METERFS_RECORDSZ;
    fcntl.arg = 10; /* Record size in bytes */
>
    send(port, DEVCTL, &fcntl, sizeof(fcntl), NORMAL, NULL, 0);
