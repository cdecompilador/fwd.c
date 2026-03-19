#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "os.h"

func void *os_mem_reserve(void *base_address, usize size)
{ 
	(void)base_address; 
	return malloc(size); 
}

func b32 os_mem_commit(void *base_address, usize size)
{ 
	(void)base_address; 
	(void)size; 
	return 1; 
}

func void os_mem_release(void *address, usize size)
{ 
	(void)size; 
	free(address); 
}

func void os_con_write(const char *text)
{
	fputs(text, stdout); 
}

func void os_exit(u64 code)
{ 
	exit((int)code); 
}

func OS_File os_file_open(const char *path)
{ 
	(void)path; return 0; 
}

func u64 os_file_size(OS_File file)
{ 
	(void)file; 
	return 0; 
}

func void os_file_read_at(OS_File file, u64 offset, u64 size, void *dest)
{ 
	(void)file; 
	(void)offset; 
	(void)size; 
	(void)dest; 
}

func void os_file_close(OS_File file)
{ 
	(void)file; 
}

func b32 os_file_reader_open(const char *path, u8 *buffer, struct OS_File_Reader *out_reader)
{ 
	(void)path; (void)buffer; (void)out_reader; 
	return 0; 
}

func b32 os_file_reader_next_byte(struct OS_File_Reader *reader, u8 *out_byte)
{ 
	(void)reader; (void)out_byte; 
	return 0; 
}

func void os_file_reader_close(struct OS_File_Reader *reader)
{ 
	(void)reader; 
}

func void os_parse_cmdline(struct Arena *arena, char ***argument_list_out, usize *argument_list_len_out)
{ 
	(void)arena; (void)argument_list_out; (void)argument_list_len_out; 
}

func OS_Barrier os_barrier_alloc(u64 count)
{ 
	(void)count; 
	return 0; 
}

func void os_barrier_wait(OS_Barrier barrier)
{ 
	(void)barrier; 
}
