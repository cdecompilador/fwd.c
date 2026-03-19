#include "base.h"
#include "os.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* TODO(cdecompilador): Implement a free list for g_win32_arena or implement the EntityPool
 * abstraction to enforce an arena of just 1 same size struct */
global_variable struct Arena g_win32_arena;
global_variable CRITICAL_SECTION g_win32_entity_mutex;

typedef BOOL (WINAPI *InitializeSynchronizationBarrier_t)(LPSYNCHRONIZATION_BARRIER, LONG, LONG);
typedef BOOL (WINAPI *EnterSynchronizationBarrier_t)(LPSYNCHRONIZATION_BARRIER, DWORD);
typedef BOOL (WINAPI *DeleteSynchronizationBarrier_t)(LPSYNCHRONIZATION_BARRIER);

struct Win32_Legacy_Barrier
{
	CRITICAL_SECTION cs;
	CONDITION_VARIABLE cv;
	LONG total_count;
	LONG waiting_count;
	LONG generation;
};

func BOOL WINAPI
legacy_barrier_init(LPSYNCHRONIZATION_BARRIER barrier_storage, LONG count, LONG spin_count)
{
	(void)spin_count;
	struct Win32_Legacy_Barrier *legacy_barrier = (struct Win32_Legacy_Barrier *)barrier_storage;
	InitializeCriticalSection(&legacy_barrier->cs);
	InitializeConditionVariable(&legacy_barrier->cv);
	legacy_barrier->total_count = count;
	legacy_barrier->waiting_count = 0;
	legacy_barrier->generation = 0;
	return TRUE;
}

func BOOL WINAPI
legacy_barrier_enter(LPSYNCHRONIZATION_BARRIER barrier_storage, DWORD flags)
{
	(void)flags;
	struct Win32_Legacy_Barrier *legacy_barrier = (struct Win32_Legacy_Barrier *)barrier_storage;
	EnterCriticalSection(&legacy_barrier->cs);
	LONG current_gen = legacy_barrier->generation;
	legacy_barrier->waiting_count++;
	if (legacy_barrier->waiting_count >= legacy_barrier->total_count)
	{
		legacy_barrier->waiting_count = 0;
		legacy_barrier->generation++;
		WakeAllConditionVariable(&legacy_barrier->cv);
		LeaveCriticalSection(&legacy_barrier->cs);
		return TRUE;
	}
	while (legacy_barrier->generation == current_gen)
		SleepConditionVariableCS(&legacy_barrier->cv, &legacy_barrier->cs, INFINITE);
	LeaveCriticalSection(&legacy_barrier->cs);
	return FALSE;
}

func BOOL WINAPI
legacy_barrier_delete(LPSYNCHRONIZATION_BARRIER barrier_storage, ...)
{
	struct Win32_Legacy_Barrier *legacy_barrier = (struct Win32_Legacy_Barrier *)barrier_storage;
	DeleteCriticalSection(&legacy_barrier->cs);
	return TRUE;
}

global_variable b32 g_win32_legacy_barrier = 1;
global_variable InitializeSynchronizationBarrier_t win32_barrier_init  = legacy_barrier_init;
global_variable EnterSynchronizationBarrier_t      win32_barrier_enter = legacy_barrier_enter;
global_variable DeleteSynchronizationBarrier_t     win32_barrier_delete = (DeleteSynchronizationBarrier_t)legacy_barrier_delete;

struct Win32_Entity
{
	SYNCHRONIZATION_BARRIER native_barrier;
	struct Win32_Legacy_Barrier legacy_barrier;
	CRITICAL_SECTION mutex;
	HANDLE hFile;
	b32 is_win7;
};

func void
win32_load_functions(void)
{
	HMODULE kernel32_module = GetModuleHandleA("kernel32.dll");
	InitializeSynchronizationBarrier_t init_fn = (InitializeSynchronizationBarrier_t)
		GetProcAddress(kernel32_module, "InitializeSynchronizationBarrier");
	if (init_fn)
	{
		g_win32_legacy_barrier = 0;
		win32_barrier_init = init_fn;
		win32_barrier_enter = (EnterSynchronizationBarrier_t)
			GetProcAddress(kernel32_module, "EnterSynchronizationBarrier");
		win32_barrier_delete = (DeleteSynchronizationBarrier_t)
			GetProcAddress(kernel32_module, "DeleteSynchronizationBarrier");
	}
}

func void
os_win32_initialize()
{
	win32_load_functions();
	InitializeCriticalSection(&g_win32_entity_mutex);

	g_win32_arena.reserved   = g_page_size;
	g_win32_arena.committed  = g_page_size;
	g_win32_arena.base_address = os_mem_reserve(0, g_page_size);
	os_mem_commit(g_win32_arena.base_address, g_page_size);
#ifdef DEBUG
	g_win32_arena.debug_owner_lane = (u32)-1;
#endif
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



func LPSYNCHRONIZATION_BARRIER
win32_entity_barrier_ptr(struct Win32_Entity *entity)
{
	if (g_win32_legacy_barrier)
		return (LPSYNCHRONIZATION_BARRIER)&entity->legacy_barrier;
	return &entity->native_barrier;
}

func OS_Barrier
os_barrier_alloc(u64 count)
{
	EnterCriticalSection(&g_win32_entity_mutex);
		struct Win32_Entity *result = push_struct(&g_win32_arena, struct Win32_Entity);
	LeaveCriticalSection(&g_win32_entity_mutex);

	win32_barrier_init(win32_entity_barrier_ptr(result), (LONG)count, -1);

	return (OS_Barrier)(usize)result;
}

func void
os_barrier_wait(OS_Barrier barrier)
{
	assert(barrier != 0);

	struct Win32_Entity *win32_entity = (struct Win32_Entity *)(void *)barrier;
	if (win32_entity != 0)
	{
		win32_barrier_enter(win32_entity_barrier_ptr(win32_entity), 0);
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
		win32_barrier_delete(win32_entity_barrier_ptr(win32_entity));
		/* TODO(cdecompilador): Release entity */
#ifdef DEBUG
		mem_clear((void *)win32_entity, sizeof(*win32_entity));
#endif
	}
}

#include "base.c"
#include "fwd.c"

struct Worker_Args
{
	u32 lane_index;
	char **input_filenames;
	usize input_files_count;
};

func DWORD
win32_worker_thread(LPVOID lp_param)
{
	struct Worker_Args *args = (struct Worker_Args *)lp_param;
	tc_initialize(args->lane_index);

	startup(args->input_filenames, args->input_files_count);
	return 0;
}

int
main(void)
{
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	DWORD cpu_count = sysinfo.dwNumberOfProcessors;

	os_win32_initialize();

	char **input_filenames;
	usize input_files_count;
	os_parse_cmdline(&g_win32_arena, &input_filenames, &input_files_count);

	g_broadcast_memory = os_mem_reserve(0, max_broadcast_size);
	os_mem_commit(g_broadcast_memory, max_broadcast_size);
	mem_zero(g_broadcast_memory, max_broadcast_size);

	g_lane_count = (u32)cpu_count;
	g_barrier = os_barrier_alloc(cpu_count);

	struct Worker_Args *worker_args = push_array(&g_win32_arena, struct Worker_Args, cpu_count);
	HANDLE *threads = push_array(&g_win32_arena, HANDLE, cpu_count);
	for (DWORD i = 0; i < cpu_count; i++)
	{
		worker_args[i].lane_index = (u32)i;
		worker_args[i].input_filenames = input_filenames;
		worker_args[i].input_files_count = input_files_count;
		threads[i] = CreateThread(0, 0, win32_worker_thread, &worker_args[i], 0, 0);
	}

	WaitForMultipleObjects(cpu_count, threads, TRUE, INFINITE);

	return 0;
}
