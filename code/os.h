#define OS_FILE_READ_BUFFER_BYTE_COUNT (2 * 1024 * 1024)

struct OS_File_Reader
{
    void  *file_handle;
    u8    *buffer;
    usize  buffer_bytes_loaded;
    usize  buffer_cursor;
};

func void  *os_mem_reserve(void *base_address, usize size);
func b32    os_mem_commit(void *base_address, usize size);
func void   os_mem_release(void *address, usize size);
func void   os_con_write(const char *text);
func b32    os_file_reader_open(const char *path, u8 *buffer, struct OS_File_Reader *out_reader);
func b32    os_file_reader_next_byte(struct OS_File_Reader *reader, u8 *out_byte);
func void   os_file_reader_close(struct OS_File_Reader *reader);
func void os_parse_cmdline(struct Arena *arena, char ***argument_list_out, usize *argument_list_len_out);
func void   os_exit(u64 code);

typedef usize OS_File;
func OS_File os_file_open(const char *path);
func u64     os_file_size(OS_File file);
func void    os_file_read_at(OS_File file, u64 offset, u64 size, void *dest);
func void    os_file_close(OS_File file);

inline func void   
_os_con_write_number(void *ptr, u32 bits)
{
	char buffer[21];
	usize buffer_index = 20;
	buffer[buffer_index] = '\0';
	u64 value = 0;
	switch (bits)
	{
	case 8: value = *(u8 *)ptr; break;
	case 16: value = *(u16 *)ptr; break;
	case 32: value = *(u32 *)ptr; break;
	case 64: value = *(u64 *)ptr; break;
	default:
#ifdef DEBUG
			 os_con_write("Invalid number of bits to _os_con_write_number\n");
			 os_exit(1);
#else
			 value = *(u64 *)ptr; break;
#endif
	}

	if (value == 0)
	{
		buffer[--buffer_index] = '0';
	}
	else
	{
		while (value > 0)
		{
			buffer[--buffer_index] = '0' + (value % 10);
			value /= 10;
		}
	}

	os_con_write(&buffer[buffer_index]);
}

inline func void os_con_write_u8(u8 number) { _os_con_write_number(&number, 8); }
inline func void os_con_write_u16(u16 number) { _os_con_write_number(&number, 16); }
inline func void os_con_write_u32(u32 number) { _os_con_write_number(&number, 32); }
inline func void os_con_write_u64(u64 number) { _os_con_write_number(&number, 64); }

/* TODO(cdecompilador): Get this from the operating system since its not guaranteed, note
 * that this variable may only be defined once per unity build */
global_variable usize g_page_size = 4096;
