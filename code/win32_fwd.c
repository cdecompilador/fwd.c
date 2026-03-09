#include "base.h"
#include "os.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* TODO(cdecompilador): Implement a free list for g_win32_arena or implement the EntityPool
 * abstraction to enforce an arena of just 1 same size struct */
global_variable struct Arena g_win32_arena;
global_variable CRITICAL_SECTION g_win32_entity_mutex;

func void
os_win32_initialize()
{
	InitializeCriticalSection(&g_win32_entity_mutex);

	g_win32_arena.reserved   = g_page_size;
	g_win32_arena.committed  = g_page_size;
	g_win32_arena.base_address = os_mem_reserve(0, g_page_size);
	os_mem_commit(g_win32_arena.base_address, g_page_size);
}

func void*
os_mem_reserve(void *base_address, usize size)
{
    return VirtualAlloc(base_address, (SIZE_T)size, MEM_RESERVE, PAGE_READWRITE);
}

func b32
os_mem_commit(void *base_address, usize size)
{
    return VirtualAlloc(base_address, (SIZE_T)size, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

func void
os_mem_release(void *address, usize size)
{
    (void)size;
    VirtualFree(address, 0, MEM_RELEASE);
}

func void
os_con_write(const char *text)
{
    DWORD bytes_written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
              text, lstrlenA(text),
              &bytes_written, 0);
}


func b32
os_file_reader_open(const char *path, u8 *buffer, struct OS_File_Reader *out_reader)
{
    HANDLE file_handle = CreateFileA(path,
                                     GENERIC_READ,
                                     FILE_SHARE_READ,
                                     NULL,
                                     OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL,
                                     NULL);
    if (file_handle == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    out_reader->file_handle          = (void *)file_handle;
    out_reader->buffer               = buffer;
    out_reader->buffer_bytes_loaded  = 0;
    out_reader->buffer_cursor        = 0;
    return 1;
}

func b32
os_file_reader_next_byte(struct OS_File_Reader *reader, u8 *out_byte)
{
    if (reader->buffer_cursor >= reader->buffer_bytes_loaded)
    {
        DWORD bytes_read = 0;
        if (!ReadFile((HANDLE)reader->file_handle,
                      reader->buffer,
                      OS_FILE_READ_BUFFER_BYTE_COUNT,
                      &bytes_read,
                      NULL) || bytes_read == 0)
        {
            return 0;
        }
        reader->buffer_bytes_loaded = (usize)bytes_read;
        reader->buffer_cursor       = 0;
    }

    *out_byte = reader->buffer[reader->buffer_cursor++];
    return 1;
}

func void
os_file_reader_close(struct OS_File_Reader *reader)
{
    CloseHandle((HANDLE)reader->file_handle);
    reader->file_handle = (void *)0;
}

struct Arg_Node
{
	char *argument;
	struct Arg_Node *next;
};

func void
os_parse_cmdline(struct Arena *arena, char ***argument_list_out, usize *argument_list_len_out)
{
    char *cursor		 = GetCommandLineA();
    usize argument_count = 0;
    b32 exe_skipped		 = 0;

	struct Arg_Node *first = 0;
	struct Arg_Node *last  = 0;

    while (*cursor)
    {
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (!*cursor) break;

        char  *token_start;
        usize  token_char_count;

        if (*cursor == '"')
        {
            cursor++;
            token_start = cursor;
            while (*cursor && *cursor != '"') cursor++;
            token_char_count = (usize)(cursor - token_start);
            if (*cursor == '"') cursor++;
        }
        else
        {
            token_start = cursor;
            while (*cursor && *cursor != ' ' && *cursor != '\t') cursor++;
            token_char_count = (usize)(cursor - token_start);
        }

        if (!exe_skipped)
        {
            exe_skipped = 1;
        }
        else
        {
			struct Arg_Node *node = push_struct(arena, struct Arg_Node);

            node->argument = push_array(arena, char, token_char_count + 1);
            for (usize char_index = 0; char_index < token_char_count; char_index++)
            {
                node->argument[char_index] = token_start[char_index];
            }
            node->argument[token_char_count]          = '\0';
            argument_count++;

			queue_push(first, last, node);
        }
    }

	char **argument_list = push_array(arena, char *, argument_count);
	struct Arg_Node *ptr = first;
	for (usize argument_index = 0; argument_index < argument_count; argument_index++)
	{
		argument_list[argument_index] = ptr->argument;
		ptr = ptr->next;
	}

	*argument_list_out	   = argument_list;
	*argument_list_len_out = argument_count;
}

func void
os_exit(u64 code)
{
	ExitProcess(code);
}

struct Win32_Entity
{
	SYNCHRONIZATION_BARRIER barrier;
	CRITICAL_SECTION mutex;
	HANDLE hFile;
};

func OS_Barrier
os_barrier_alloc(u64 count)
{
	EnterCriticalSection(&g_win32_entity_mutex);
		struct Win32_Entity *result = push_struct(&g_win32_arena, struct Win32_Entity);
	LeaveCriticalSection(&g_win32_entity_mutex);

	InitializeSynchronizationBarrier(&result->barrier, count, -1);

	return (OS_Barrier)(usize)result;
}

func void
os_barrier_wait(OS_Barrier barrier)
{
	assert(barrier != 0);

	struct Win32_Entity *win32_entity = (struct Win32_Entity *)(void *)barrier;
	if (win32_entity != 0)
	{
		EnterSynchronizationBarrier(&win32_entity->barrier, 0);
	}
}

func OS_File
os_file_open(const char *path)
{
	HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0,
	                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (hFile == INVALID_HANDLE_VALUE) return 0;

	EnterCriticalSection(&g_win32_entity_mutex);
		struct Win32_Entity *entity = push_struct(&g_win32_arena, struct Win32_Entity);
	LeaveCriticalSection(&g_win32_entity_mutex);

	entity->hFile = hFile;
	return (OS_File)(usize)entity;
}

func u64
os_file_size(OS_File file)
{
	assert(file != 0);
	struct Win32_Entity *entity = (struct Win32_Entity *)(void *)file;
	LARGE_INTEGER size;
	GetFileSizeEx(entity->hFile, &size);
	return (u64)size.QuadPart;
}

func void
os_file_read_at(OS_File file, u64 offset, u64 size, void *dest)
{
	assert(file != 0);
	struct Win32_Entity *entity = (struct Win32_Entity *)(void *)file;

	OVERLAPPED ov = {0};
	ov.Offset     = (DWORD)(offset & 0xFFFFFFFF);
	ov.OffsetHigh = (DWORD)(offset >> 32);

	DWORD bytes_read = 0;
	ReadFile(entity->hFile, dest, (DWORD)size, &bytes_read, &ov);
}

func void
os_file_close(OS_File file)
{
	assert(file != 0);
	struct Win32_Entity *entity = (struct Win32_Entity *)(void *)file;
	CloseHandle(entity->hFile);
#ifdef DEBUG
	entity->hFile = INVALID_HANDLE_VALUE;
#endif
}

func void
os_barrier_release(OS_Barrier barrier)
{
	assert(barrier != 0);

	struct Win32_Entity *win32_entity = (struct Win32_Entity *)(void *)barrier;
	if (win32_entity != 0)
	{
		DeleteSynchronizationBarrier(&win32_entity->barrier);
		/* TODO(cdecompilador): Release entity */
#ifdef DEBUG
		mem_clear((void *)win32_entity, sizeof(*win32_entity));
#endif
	}
}

#include "base.c"
#include "fwd.c"

func DWORD
win32_worker_thread(LPVOID lp_param)
{
	u32 idx = (u32)(usize)lp_param;
	tc_initialize(idx);

	startup();
	return 0;
}

int
main(void)
{
	g_shared_memory = os_mem_reserve(0, shared_memory_size);
	os_mem_commit(g_shared_memory, shared_memory_size);
	init_shared_memory();

	os_parse_cmdline(&shared_memory()->arena,
			&shared_memory()->input_filenames,
			&shared_memory()->input_files_count);

	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	DWORD cpu_count = sysinfo.dwNumberOfProcessors;

	os_win32_initialize();

	g_broadcast_memory = os_mem_reserve(0, max_broadcast_size);
	os_mem_commit(g_broadcast_memory, max_broadcast_size);
	mem_zero(g_broadcast_memory, max_broadcast_size);

	g_lane_count = (u32)cpu_count;
	g_barrier = os_barrier_alloc(cpu_count);

	HANDLE *threads = push_array(&g_win32_arena, HANDLE, cpu_count);
	for (DWORD i = 0; i < cpu_count; i++)
	{
		threads[i] = CreateThread(0, 0, win32_worker_thread, (LPVOID)(usize)i, 0, 0);
	}

	WaitForMultipleObjects(cpu_count, threads, TRUE, INFINITE);

	return 0;
}
