#include "base.h"
#include "os.h"
#include "os_win32.c"
#include "base.c"

#define PARSER_BUFFER_BYTE_COUNT 4096

typedef enum
{
    PARSE_STATE_SCAN,
    PARSE_STATE_LINE_COMMENT,
    PARSE_STATE_BLOCK_COMMENT,
    PARSE_STATE_BLOCK_COMMENT_STAR,
    PARSE_STATE_STRING,
    PARSE_STATE_STRING_ESCAPE,
    PARSE_STATE_CHAR_LIT,
    PARSE_STATE_CHAR_LIT_ESCAPE,
    PARSE_STATE_COLLECT_FUNC,
    PARSE_STATE_COLLECT_STRUCT_NAME,
    PARSE_STATE_SKIP_BODY,
    PARSE_STATE_SAW_SLASH,
    PARSE_STATE_PREPROCESSOR,
} Parse_State;

struct Parser
{
    Parse_State state;
    Parse_State return_state;

    u8   prev_byte;
    b32  prev_was_ident;

    i32  func_match_index;
    i32  struct_match_index;

    char output_buffer[PARSER_BUFFER_BYTE_COUNT];
    i32  output_buffer_used;
    i32  paren_depth;

    char name_buffer[256];
    i32  name_buffer_used;

    i32  brace_depth;
};

func b32
is_ident_char(u8 byte)
{
    return (byte >= 'a' && byte <= 'z') ||
           (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') ||
           byte == '_';
}

func void
parser_init(struct Parser *parser)
{
    parser->state              = PARSE_STATE_SCAN;
    parser->return_state       = PARSE_STATE_SCAN;
    parser->prev_byte          = 0;
    parser->prev_was_ident     = 0;
    parser->func_match_index   = 0;
    parser->struct_match_index = 0;
    parser->output_buffer_used = 0;
    parser->paren_depth        = 0;
    parser->name_buffer_used   = 0;
    parser->brace_depth        = 0;
}

func void
output_buffer_push(struct Parser *parser, u8 byte)
{
    if (parser->output_buffer_used < PARSER_BUFFER_BYTE_COUNT - 1)
    {
        parser->output_buffer[parser->output_buffer_used++] = (char)byte;
    }
}

func void
name_buffer_push(struct Parser *parser, u8 byte)
{
    if (parser->name_buffer_used < 255)
    {
        parser->name_buffer[parser->name_buffer_used++] = (char)byte;
    }
}

func void
emit_func_declaration(struct Parser *parser)
{
    parser->output_buffer[parser->output_buffer_used] = '\0';
    os_con_write("static ");
    os_con_write(parser->output_buffer);
    os_con_write(";\n");
}

func void
emit_struct_declaration(struct Parser *parser)
{
    parser->name_buffer[parser->name_buffer_used] = '\0';
    os_con_write("typedef struct ");
    os_con_write(parser->name_buffer);
    os_con_write(" ");
    os_con_write(parser->name_buffer);
    os_con_write(";\n");
}

func b32
parser_try_enter_skip(struct Parser *parser, u8 byte, Parse_State caller_state)
{
    if (byte == '/' && parser->state != PARSE_STATE_SAW_SLASH)
    {
        parser->return_state = caller_state;
        parser->state        = PARSE_STATE_SAW_SLASH;
        return 1;
    }
    if (byte == '"')
    {
        parser->return_state = caller_state;
        parser->state        = PARSE_STATE_STRING;
        return 1;
    }
    if (byte == '\'')
    {
        parser->return_state = caller_state;
        parser->state        = PARSE_STATE_CHAR_LIT;
        return 1;
    }
    return 0;
}

global_variable const char g_keyword_func[]   = "func";
global_variable const char g_keyword_struct[] = "struct";

func void
parser_feed_byte(struct Parser *parser, u8 byte)
{
    switch (parser->state)
    {
        case PARSE_STATE_SAW_SLASH:
        {
            if (byte == '/')
            {
                parser->state = PARSE_STATE_LINE_COMMENT;
            }
            else if (byte == '*')
            {
                parser->state = PARSE_STATE_BLOCK_COMMENT;
            }
            else
            {
                parser->state = parser->return_state;

                if (parser->state == PARSE_STATE_COLLECT_FUNC)
                {
                    output_buffer_push(parser, '/');
                }

                parser_feed_byte(parser, byte);
            }
        } break;

        case PARSE_STATE_LINE_COMMENT:
        {
            if (byte == '\n')
            {
                parser->state = parser->return_state;
            }
        } break;

        case PARSE_STATE_BLOCK_COMMENT:
        {
            if (byte == '*')
            {
                parser->state = PARSE_STATE_BLOCK_COMMENT_STAR;
            }
        } break;

        case PARSE_STATE_BLOCK_COMMENT_STAR:
        {
            if (byte == '/')
            {
                parser->state = parser->return_state;
            }
            else if (byte != '*')
            {
                parser->state = PARSE_STATE_BLOCK_COMMENT;
            }
        } break;

        case PARSE_STATE_STRING:
        {
            if (byte == '\\')
            {
                parser->state = PARSE_STATE_STRING_ESCAPE;
            }
            else if (byte == '"')
            {
                parser->state = parser->return_state;
            }
        } break;

        case PARSE_STATE_STRING_ESCAPE:
        {
            parser->state = PARSE_STATE_STRING;
        } break;

        case PARSE_STATE_CHAR_LIT:
        {
            if (byte == '\\')
            {
                parser->state = PARSE_STATE_CHAR_LIT_ESCAPE;
            }
            else if (byte == '\'')
            {
                parser->state = parser->return_state;
            }
        } break;

        case PARSE_STATE_CHAR_LIT_ESCAPE:
        {
            parser->state = PARSE_STATE_CHAR_LIT;
        } break;

        case PARSE_STATE_PREPROCESSOR:
        {
            if (byte == '\n' && parser->prev_byte != '\\')
            {
                parser->state = PARSE_STATE_SCAN;
            }
        } break;

        case PARSE_STATE_SCAN:
        {
            if (byte == '#')
            {
                parser->state              = PARSE_STATE_PREPROCESSOR;
                parser->func_match_index   = 0;
                parser->struct_match_index = 0;
                break;
            }

            if (parser_try_enter_skip(parser, byte, PARSE_STATE_SCAN))
            {
                parser->func_match_index   = 0;
                parser->struct_match_index = 0;
                break;
            }

            b32 current_is_ident = is_ident_char(byte);

            if (parser->func_match_index < 4 &&
                byte == (u8)g_keyword_func[parser->func_match_index])
            {
                if (parser->func_match_index == 0 && parser->prev_was_ident)
                {
                    parser->func_match_index = 0;
                }
                else
                {
                    parser->func_match_index++;
                }
            }
            else if (parser->func_match_index == 4 && !current_is_ident)
            {
                parser->state              = PARSE_STATE_COLLECT_FUNC;
                parser->output_buffer_used = 0;
                parser->paren_depth        = 0;
                parser->func_match_index   = 0;
                parser->struct_match_index = 0;
                parser_feed_byte(parser, byte);
                return;
            }
            else
            {
                parser->func_match_index = 0;
                if (!parser->prev_was_ident && byte == (u8)g_keyword_func[0])
                {
                    parser->func_match_index = 1;
                }
            }

            if (parser->struct_match_index < 6 &&
                byte == (u8)g_keyword_struct[parser->struct_match_index])
            {
                if (parser->struct_match_index == 0 && parser->prev_was_ident)
                {
                    parser->struct_match_index = 0;
                }
                else
                {
                    parser->struct_match_index++;
                }
            }
            else if (parser->struct_match_index == 6 && !current_is_ident)
            {
                parser->state              = PARSE_STATE_COLLECT_STRUCT_NAME;
                parser->name_buffer_used   = 0;
                parser->func_match_index   = 0;
                parser->struct_match_index = 0;
                break;
            }
            else
            {
                parser->struct_match_index = 0;
                if (!parser->prev_was_ident && byte == (u8)g_keyword_struct[0])
                {
                    parser->struct_match_index = 1;
                }
            }

            parser->prev_was_ident = current_is_ident;
        } break;

        case PARSE_STATE_COLLECT_FUNC:
        {
            if (parser_try_enter_skip(parser, byte, PARSE_STATE_COLLECT_FUNC))
            {
                break;
            }

            if (byte == '(')
            {
                parser->paren_depth++;
            }
            else if (byte == ')')
            {
                parser->paren_depth--;
            }

            if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r')
            {
                if (parser->output_buffer_used > 0 &&
                    parser->output_buffer[parser->output_buffer_used - 1] != ' ')
                {
                    output_buffer_push(parser, ' ');
                }
            }
            else
            {
                output_buffer_push(parser, byte);
            }

            if (parser->paren_depth == 0 && byte == ')')
            {
                emit_func_declaration(parser);
                parser->state       = PARSE_STATE_SKIP_BODY;
                parser->brace_depth = 0;
            }
        } break;

        case PARSE_STATE_COLLECT_STRUCT_NAME:
        {
            if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r')
            {
                break;
            }

            if (byte == '{')
            {
                if (parser->name_buffer_used > 0)
                {
                    parser->state       = PARSE_STATE_SKIP_BODY;
                    parser->brace_depth = 1;
                }
                else
                {
                    parser->name_buffer_used = 0;
                    parser->state            = PARSE_STATE_SKIP_BODY;
                    parser->brace_depth      = 1;
                }
                break;
            }

            if (byte == ';')
            {
                parser->name_buffer_used = 0;
                parser->state            = PARSE_STATE_SCAN;
                break;
            }

            if (parser->name_buffer_used > 0 && !is_ident_char(byte))
            {
                parser->name_buffer_used = 0;
                parser->state            = PARSE_STATE_SCAN;
                parser_feed_byte(parser, byte);
                return;
            }

            if (is_ident_char(byte))
            {
                name_buffer_push(parser, byte);
            }
        } break;

        case PARSE_STATE_SKIP_BODY:
        {
            if (parser_try_enter_skip(parser, byte, PARSE_STATE_SKIP_BODY))
            {
                break;
            }

            if (byte == '{')
            {
                parser->brace_depth++;
            }
            else if (byte == '}')
            {
                parser->brace_depth--;
                if (parser->brace_depth == 0)
                {
                    if (parser->name_buffer_used > 0)
                    {
                        emit_struct_declaration(parser);
                    }
                    parser->name_buffer_used = 0;
                    parser->state            = PARSE_STATE_SCAN;
                    parser->prev_was_ident   = 0;
                }
            }
        } break;
    }

    parser->prev_byte = byte;
}

int main(void)
{
    struct Arena arena = {0};

    char *argument_list[64];
    i32   argument_count = os_parse_cmdline(&arena, argument_list, 64);

    if (argument_count < 1)
    {
        os_con_write("usage: fwd.exe <file> [file ...]\n");
        return 1;
    }

    u8 *read_buffer = push_array(&arena, u8, OS_FILE_READ_BUFFER_BYTE_COUNT);

    for (i32 file_index = 0; file_index < argument_count; file_index++)
    {
        struct OS_File_Reader reader;
        if (!os_file_reader_open(argument_list[file_index], read_buffer, &reader))
        {
            os_con_write("[err] could not open: ");
            os_con_write(argument_list[file_index]);
            os_con_write("\n");
            continue;
        }

        struct Parser parser;
        parser_init(&parser);

        u8 current_byte;
        while (os_file_reader_next_byte(&reader, &current_byte))
        {
            parser_feed_byte(&parser, current_byte);
        }

        os_file_reader_close(&reader);
    }

    return 0;
}
