#ifndef OS_H
#define OS_H

#define OS_FILE_READ_BUFFER_BYTE_COUNT (2 * 1024 * 1024)

struct OS_File_Reader
{
    void  *file_handle;
    u8    *buffer;
    usize  buffer_bytes_loaded;
    usize  buffer_cursor;
};

func void  *os_mem_reserve(usize size);
func b32    os_mem_commit(void *address, usize size);
func void   os_mem_release(void *address, usize size);
func void   os_con_write(const char *text);
func b32    os_file_reader_open(const char *path, u8 *buffer, struct OS_File_Reader *out_reader);
func b32    os_file_reader_next_byte(struct OS_File_Reader *reader, u8 *out_byte);
func void   os_file_reader_close(struct OS_File_Reader *reader);
func i32    os_parse_cmdline(struct Arena *arena, char **argument_list_out, i32 max_argument_count);

#endif /* OS_H */
