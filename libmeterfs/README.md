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

