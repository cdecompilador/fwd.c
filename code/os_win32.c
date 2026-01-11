#define WIN32_LEAN_AND_MEAN
#include <windows.h>

func void *
os_mem_reserve(usize size)
{
    return VirtualAlloc(NULL, (SIZE_T)size, MEM_RESERVE, PAGE_READWRITE);
}

func b32
os_mem_commit(void *address, usize size)
{
    return VirtualAlloc(address, (SIZE_T)size, MEM_COMMIT, PAGE_READWRITE) != NULL;
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

func i32
os_parse_cmdline(struct Arena *arena, char **argument_list_out, i32 max_argument_count)
{
    char *cursor         = GetCommandLineA();
    i32   argument_count = 0;
    b32   exe_skipped    = 0;

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
        else if (argument_count < max_argument_count)
        {
            char *argument = push_array(arena, char, token_char_count + 1);
            for (usize char_index = 0; char_index < token_char_count; char_index++)
            {
                argument[char_index] = token_start[char_index];
            }
            argument[token_char_count]          = '\0';
            argument_list_out[argument_count]   = argument;
            argument_count++;
        }
    }

    return argument_count;
}
