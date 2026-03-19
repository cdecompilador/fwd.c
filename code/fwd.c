struct File_Entry
{
	OS_File os_file;
	usize file_size;
};

enum Decl_Kind
{
	decl_typedef,
	decl_func
};

struct Decl
{
	struct Decl *next;
	enum Decl_Kind kind;
	usize byte_index;
	b32 valid;
	char *keyword;
	char *name;
	char *signature;
};

enum Marker_Kind
{
	marker_comment_open,
	marker_comment_close,
	marker_brace_open,
	marker_brace_close
};

struct Marker
{
	struct Marker *next;
	usize byte_index;
	enum Marker_Kind kind;
};

#define is_ident_first_char(c) (((c) >= 'a' && (c) <= 'z') || \
                          ((c) >= 'A' && (c) <= 'Z') || \
                          (c) == '_')
#define is_ident_char(c) (((c) >= 'a' && (c) <= 'z') || \
                          ((c) >= 'A' && (c) <= 'Z') || \
                          ((c) >= '0' && (c) <= '9') || \
                          (c) == '_')

#define startup_arena_size kb(16)

inline func usize
skip_block_comment(u8 *files_memory, usize byte_index, usize offset_end)
{
	byte_index += 2;
	while (byte_index + 1 < offset_end
			&& !(files_memory[byte_index] == '*' && files_memory[byte_index + 1] == '/'))
		byte_index++;
	if (byte_index + 1 < offset_end)
		byte_index += 2;
	return byte_index;
}

inline func usize
skip_line_comment(u8 *files_memory, usize byte_index, usize offset_end)
{
	byte_index += 2;
	while (byte_index < offset_end && files_memory[byte_index] != '\n')
		byte_index++;
	return byte_index;
}

inline func usize
skip_string_literal(u8 *files_memory, usize byte_index, usize offset_end)
{
	byte_index++;
	while (byte_index < offset_end && files_memory[byte_index] != '"')
	{
		if (files_memory[byte_index] == '\\' && byte_index + 1 < offset_end)
			byte_index++;
		byte_index++;
	}
	if (byte_index < offset_end)
		byte_index++;
	return byte_index;
}

/* NOTE(cdecompilador): It supports character literals like '\xc3' */
inline func usize
skip_char_literal(u8 *files_memory, usize byte_index, usize offset_end)
{
	byte_index++;
	while (byte_index < offset_end && files_memory[byte_index] != '\'')
	{
		if (files_memory[byte_index] == '\\' && byte_index + 1 < offset_end)
			byte_index++;
		byte_index++;
	}
	if (byte_index < offset_end)
		byte_index++;
	return byte_index;
}

func usize
skip_whitespace_and_comments(u8 *files_memory, usize byte_index, usize offset_end)
{
	for (;;)
	{
		while (byte_index < offset_end
				&& (files_memory[byte_index] == ' '  || files_memory[byte_index] == '\t'
				||  files_memory[byte_index] == '\n' || files_memory[byte_index] == '\r'))
			byte_index++;

		if (byte_index + 1 < offset_end && files_memory[byte_index] == '/'
				&& files_memory[byte_index + 1] == '*')
		{
			byte_index = skip_block_comment(files_memory, byte_index, offset_end);
			continue;
		}
		if (byte_index + 1 < offset_end && files_memory[byte_index] == '/'
				&& files_memory[byte_index + 1] == '/')
		{
			byte_index = skip_line_comment(files_memory, byte_index, offset_end);
			continue;
		}

		break;
	}

	return byte_index;
}

func usize
skip_balanced_for(u8 *files_memory, usize byte_index, usize offset_end, u8 open_chr, u8 close_chr)
{
	assert(byte_index < offset_end && files_memory[byte_index] == open_chr);
	usize depth = 0;
	while (byte_index < offset_end)
	{
		u8 chr = files_memory[byte_index];
		if (chr == open_chr)
		{
			depth++;
			byte_index++;
		}
		else if (chr == close_chr)
		{
			depth--;
			byte_index++;
			if (depth == 0)
				return byte_index;
		}
		else if (chr == '"')
			byte_index = skip_string_literal(files_memory, byte_index, offset_end);
		else if (chr == '\'')
			byte_index = skip_char_literal(files_memory, byte_index, offset_end);
		else if (byte_index + 1 < offset_end && chr == '/' && files_memory[byte_index + 1] == '*')
			byte_index = skip_block_comment(files_memory, byte_index, offset_end);
		else if (byte_index + 1 < offset_end && chr == '/' && files_memory[byte_index + 1] == '/')
			byte_index = skip_line_comment(files_memory, byte_index, offset_end);
		else
			byte_index++;
	}

	return byte_index;
}

func usize
read_ident(u8 *files_memory, usize byte_index, usize offset_end)
{
	if (byte_index >= offset_end || !is_ident_first_char(files_memory[byte_index]))
		return 0;
	usize start_index = byte_index;
	while (byte_index < offset_end && is_ident_char(files_memory[byte_index]))
		byte_index++;
	return byte_index - start_index;
}

func usize
try_parse_typedef(u8 *files_memory, usize kw_start, usize offset_end,
		char *kw_str, usize kw_len,
		struct Arena *arena, struct Decl **decl_first, struct Decl **decl_last)
{
	usize byte_index = kw_start + kw_len;
	byte_index = skip_whitespace_and_comments(files_memory, byte_index, offset_end);

	if (byte_index < offset_end && files_memory[byte_index] == '{')
		return kw_start + kw_len;

	usize name_len = read_ident(files_memory, byte_index, offset_end);
	if (name_len == 0)
		return kw_start + kw_len;

	usize name_start = byte_index;
	byte_index += name_len;
	byte_index = skip_whitespace_and_comments(files_memory, byte_index, offset_end);

	if (byte_index >= offset_end || files_memory[byte_index] != '{')
		return kw_start + kw_len;

	struct Decl *decl = push_struct(arena, struct Decl);
	decl->kind = decl_typedef;
	decl->byte_index = kw_start;
	decl->valid = 1;
	decl->keyword = kw_str;
	decl->name = push_array(arena, char, name_len + 1);
	memcpy(decl->name, &files_memory[name_start], name_len);
	decl->name[name_len] = '\0';
	queue_push(*decl_first, *decl_last, decl);

	return kw_start + kw_len;
}

func usize
try_parse_func(u8 *files_memory, usize kw_start, usize offset_end,
		struct Arena *arena, struct Decl **decl_first, struct Decl **decl_last)
{
	usize func_end = kw_start + (sizeof("func") - 1);
	usize byte_index = func_end;

	usize paren_start = 0;
	b32 found_paren = 0;
	while (byte_index < offset_end)
	{
		byte_index = skip_whitespace_and_comments(files_memory, byte_index, offset_end);
		if (byte_index >= offset_end) break;

		u8 chr = files_memory[byte_index];
		if (chr == '(')
		{
			paren_start = byte_index;
			found_paren = 1;
			break;
		}
		else if (chr == '{' || chr == ';' || chr == '#')
			return func_end;
		else if (chr == '*')
		{
			byte_index++;
			continue;
		}
		else if (is_ident_char(chr))
		{
			while (byte_index < offset_end && is_ident_char(files_memory[byte_index]))
				byte_index++;
			continue;
		}
		byte_index++;
	}

	if (!found_paren)
		return func_end;

	usize after_parens = skip_balanced_for(files_memory, paren_start, offset_end, '(', ')');
	usize after_sig = skip_whitespace_and_comments(files_memory, after_parens, offset_end);
	if (after_sig >= offset_end || files_memory[after_sig] != '{')
		return func_end;

	char *signature = push_string(arena, "static ");
	b32 in_whitespace = 0;
	b32 started = 0;
	for (usize sig_index = func_end; sig_index < after_parens; sig_index++)
	{
		u8 chr = files_memory[sig_index];

		if (sig_index + 1 < after_parens && chr == '/' && files_memory[sig_index + 1] == '*')
		{
			sig_index = skip_block_comment(files_memory, sig_index, after_parens) - 1;
			if (started) in_whitespace = 1;
			continue;
		}
		if (sig_index + 1 < after_parens && chr == '/' && files_memory[sig_index + 1] == '/')
		{
			sig_index = skip_line_comment(files_memory, sig_index, after_parens) - 1;
			if (started) in_whitespace = 1;
			continue;
		}
		if (chr == ' ' || chr == '\t' || chr == '\n' || chr == '\r')
		{
			if (started) in_whitespace = 1;
			continue;
		}

		if (in_whitespace && started)
			*push_array(arena, char, 1) = ' ';
		in_whitespace = 0;
		started = 1;

		*push_array(arena, char, 1) = (char)chr;
	}

	*push_array(arena, char, 1) = ';';
	*push_array(arena, char, 1) = '\0';

	struct Decl *decl = push_struct(arena, struct Decl);
	decl->kind = decl_func;
	decl->byte_index = kw_start;
	decl->valid = 1;
	decl->signature = signature;
	queue_push(*decl_first, *decl_last, decl);

	return func_end;
}

func void
parse_file_range(u8 *files_memory, usize total_files_size,
		usize offset_start, usize offset_end,
		struct Arena *arena,
		struct Decl **out_decl_first, struct Decl **out_decl_last,
		struct Marker **out_marker_first, struct Marker **out_marker_last)
{
	b32 in_string = 0;
	b32 in_line_comment = 0;
	{
		/* NOTE(cdecompilador): Scan backwards to detect if we are inside a block
		 * comment that opened on a previous line. If we find / * before * / then
		 * quotes inside the comment must not flip in_string. */
		b32 in_block_comment = 0;
		{
			usize scan = offset_start;
			while (scan >= 2)
			{
				scan--;
				if (scan > 0
					&& files_memory[scan - 1] == '*' && files_memory[scan] == '/')
					break;
				if (scan > 0
					&& files_memory[scan - 1] == '/' && files_memory[scan] == '*')
				{
					in_block_comment = 1;
					break;
				}
			}
		}

		usize line_start = offset_start;
		while (line_start > 0 && files_memory[line_start - 1] != '\n')
			line_start--;

		/* NOTE(cdecompilador): Extend line_start back through \-continued lines
		 * so that a string opened on a previous physical line is tracked. */
		for (;;)
		{
			if (line_start < 2) break;
			usize before_nl = line_start - 1;
			if (files_memory[before_nl] != '\n') break;
			usize check = before_nl;
			if (check > 0 && files_memory[check - 1] == '\r') check--;
			if (check > 0 && files_memory[check - 1] == '\\')
			{
				line_start = check - 1;
				while (line_start > 0 && files_memory[line_start - 1] != '\n')
					line_start--;
			}
			else break;
		}

		if (!in_block_comment)
		{
			for (usize scan_index = line_start; scan_index < offset_start; scan_index++)
			{
				if (in_string)
				{
					if (files_memory[scan_index] == '\\') scan_index++;
					else if (files_memory[scan_index] == '"') in_string = 0;
				}
				else if (scan_index + 1 < offset_start
						&& files_memory[scan_index] == '/' && files_memory[scan_index + 1] == '*')
				{
					scan_index += 2;
					while (scan_index + 1 < offset_start
							&& !(files_memory[scan_index] == '*' && files_memory[scan_index + 1] == '/'))
						scan_index++;
					if (scan_index + 1 < offset_start)
						scan_index++;
				}
				else if (files_memory[scan_index] == '"') in_string = 1;
				else if (files_memory[scan_index] == '\'')
					scan_index = skip_char_literal(files_memory, scan_index, offset_start) - 1;
				else if (scan_index + 1 < offset_start
						&& files_memory[scan_index] == '/' && files_memory[scan_index + 1] == '/')
				{
					in_line_comment = 1;
					break;
				}
			}
		}
	}

	struct Decl *decl_first = 0;
	struct Decl *decl_last  = 0;
	struct Marker *marker_first = 0;
	struct Marker *marker_last  = 0;

#define is_keyword(keyword) \
    ((byte_index) + (sizeof(keyword) - 1) <= (total_files_size) \
     && mem_match(&(files_memory)[(byte_index)], (keyword), (sizeof(keyword) - 1)) \
     && ((byte_index) + (sizeof(keyword) - 1) >= (total_files_size) \
         || !is_ident_char((files_memory)[(byte_index) + (sizeof(keyword) - 1)])))

#define longest_keyword_size \
	max(max(sizeof("func"), sizeof("struct")), \
	      max(sizeof("enum"), sizeof("union"))) - 1
	usize original_offset_end = offset_end;
	usize scan_end = min(offset_end + longest_keyword_size, total_files_size);

	for (usize byte_index = offset_start;
		 byte_index < scan_end;
		 byte_index++)
	{
		u8 chr = files_memory[byte_index];

		if (in_line_comment)
		{
			if (chr == '\n') in_line_comment = 0;
		}
		else if (in_string)
		{
			if (chr == '\\') byte_index++;
			else if (chr == '"') in_string = 0;
		}
		else if (byte_index + 1 < scan_end
				&& chr == '/' && files_memory[byte_index + 1] == '/')
		{
			in_line_comment = 1;
			byte_index++;
		}
		else if (chr == '"')
		{
			in_string = 1;
		}
		else if (chr == '\'')
		{
			byte_index = skip_char_literal(files_memory, byte_index, scan_end) - 1;
		}
		else if (byte_index + 1 < scan_end
				&& chr == '/' && files_memory[byte_index + 1] == '*')
		{
			if (byte_index < original_offset_end)
			{
				struct Marker *m_open = push_struct(arena, struct Marker);
				m_open->byte_index = byte_index;
				m_open->kind = marker_comment_open;
				queue_push(marker_first, marker_last, m_open);
			}
			byte_index += 2;
			while (byte_index + 1 < scan_end
					&& !(files_memory[byte_index] == '*' && files_memory[byte_index + 1] == '/'))
				byte_index++;
			if (byte_index + 1 < scan_end)
			{
				if (byte_index < original_offset_end)
				{
					struct Marker *m_close = push_struct(arena, struct Marker);
					m_close->byte_index = byte_index;
					m_close->kind = marker_comment_close;
					queue_push(marker_first, marker_last, m_close);
				}
				byte_index++;
			}
		}
		else if (byte_index + 1 < scan_end
				&& chr == '*' && files_memory[byte_index + 1] == '/')
		{
			if (byte_index < original_offset_end)
			{
				struct Marker *m_close = push_struct(arena, struct Marker);
				m_close->byte_index = byte_index;
				m_close->kind = marker_comment_close;
				queue_push(marker_first, marker_last, m_close);
			}
			byte_index++;
		}
		else if (chr == '{')
		{
			if (byte_index < original_offset_end)
			{
				struct Marker *m = push_struct(arena, struct Marker);
				m->byte_index = byte_index;
				m->kind = marker_brace_open;
				queue_push(marker_first, marker_last, m);
			}
		}
		else if (chr == '}')
		{
			if (byte_index < original_offset_end)
			{
				struct Marker *m = push_struct(arena, struct Marker);
				m->byte_index = byte_index;
				m->kind = marker_brace_close;
				queue_push(marker_first, marker_last, m);
			}
		}
		else if (chr == '#')
		{
			/* NOTE(cdecompilador): Check for #if 0 and skip to matching #endif */
			usize pp = byte_index + 1;
			while (pp < scan_end && (files_memory[pp] == ' ' || files_memory[pp] == '\t'))
				pp++;
			b32 is_if0 = 0;
			if (pp + 2 < scan_end
				&& files_memory[pp] == 'i' && files_memory[pp + 1] == 'f'
				&& !is_ident_char(files_memory[pp + 2]))
			{
				usize val = pp + 2;
				while (val < scan_end
					&& (files_memory[val] == ' ' || files_memory[val] == '\t'))
					val++;
				if (val < scan_end && files_memory[val] == '0'
					&& (val + 1 >= scan_end || !is_ident_char(files_memory[val + 1])))
					is_if0 = 1;
			}

			if (is_if0)
			{
				usize depth = 1;
				while (byte_index < scan_end && files_memory[byte_index] != '\n')
					byte_index++;
				if (byte_index < scan_end) byte_index++;

				while (byte_index < scan_end && depth > 0)
				{
					while (byte_index < scan_end
						&& (files_memory[byte_index] == ' '
						 || files_memory[byte_index] == '\t'))
						byte_index++;

					if (byte_index < scan_end && files_memory[byte_index] == '#')
					{
						usize d = byte_index + 1;
						while (d < scan_end
							&& (files_memory[d] == ' ' || files_memory[d] == '\t'))
							d++;
						if (d + 5 <= scan_end
							&& mem_match(&files_memory[d], "endif", 5)
							&& (d + 5 >= scan_end
								|| !is_ident_char(files_memory[d + 5])))
							depth--;
						else if (d + 2 <= scan_end
							&& mem_match(&files_memory[d], "if", 2)
							&& (d + 2 >= scan_end
								|| !is_ident_char(files_memory[d + 2])))
							depth++;
					}

					while (byte_index < scan_end && files_memory[byte_index] != '\n')
						byte_index++;
					if (byte_index < scan_end && depth > 0) byte_index++;
				}
			}
			else
			{
				while (byte_index < scan_end)
				{
					if (files_memory[byte_index] == '\\'
						&& byte_index + 1 < scan_end)
					{
						usize after_backslash = byte_index + 1;
						if (files_memory[after_backslash] == '\r'
							&& after_backslash + 1 < scan_end
							&& files_memory[after_backslash + 1] == '\n')
						{ byte_index = after_backslash + 2; continue; }
						if (files_memory[after_backslash] == '\n')
						{ byte_index = after_backslash + 1; continue; }
					}
					if (files_memory[byte_index] == '\n') break;
					byte_index++;
				}
			}
		}
		else if (byte_index > 0 && is_ident_char(files_memory[byte_index - 1]))
		{
		}
		else if (byte_index < original_offset_end)
		{
			b32 on_preproc_line = 0;
			{
				usize ls = byte_index;
				while (ls > 0 && files_memory[ls - 1] != '\n') ls--;
				while (ls < byte_index && (files_memory[ls] == ' ' || files_memory[ls] == '\t')) ls++;
				if (files_memory[ls] == '#') on_preproc_line = 1;
			}

			if (!on_preproc_line)
			{
				if (is_keyword("func"))
				{
					byte_index = try_parse_func(files_memory, byte_index,
						total_files_size, arena,
						&decl_first, &decl_last) - 1;
				}
				else if (is_keyword("struct"))
				{
					byte_index = try_parse_typedef(files_memory, byte_index,
						total_files_size, "struct", 6, arena,
						&decl_first, &decl_last) - 1;
				}
				else if (is_keyword("union"))
				{
					byte_index = try_parse_typedef(files_memory, byte_index,
						total_files_size, "union", 5, arena,
						&decl_first, &decl_last) - 1;
				}
				else if (is_keyword("enum"))
				{
					byte_index = try_parse_typedef(files_memory, byte_index,
						total_files_size, "enum", 4, arena,
						&decl_first, &decl_last) - 1;
				}
			}
		}
	}

#undef is_keyword
#undef longest_keyword_size

	*out_decl_first = decl_first;
	*out_decl_last = decl_last;
	*out_marker_first = marker_first;
	*out_marker_last = marker_last;
}

func void
merge_and_invalidate(struct Decl **decl_first_per_lane,
		struct Marker **marker_first_per_lane,
		usize num_lanes, struct Arena *scratch_arena)
{
	usize open_count = 0;
	for (usize lane_i = 0; lane_i < num_lanes; lane_i++)
		for (struct Marker *m = marker_first_per_lane[lane_i]; m; m = m->next)
			if (m->kind == marker_comment_open) open_count++;
	usize *range_starts = push_array(scratch_arena, usize, open_count + 1);
	usize *range_ends   = push_array(scratch_arena, usize, open_count + 1);
	usize range_count = 0;
	{
		b32 have_open = 0;
		usize open_pos = 0;
		for (usize lane_i = 0; lane_i < num_lanes; lane_i++)
		{
			for (struct Marker *m = marker_first_per_lane[lane_i]; m; m = m->next)
			{
				if (m->kind == marker_comment_open && !have_open)
				{
					open_pos = m->byte_index;
					have_open = 1;
				}
				else if (m->kind == marker_comment_close && have_open)
				{
					range_starts[range_count] = open_pos;
					range_ends[range_count]   = m->byte_index + 2;
					range_count++;
					have_open = 0;
				}
			}
		}
	}

	for (usize lane_i = 0; lane_i < num_lanes; lane_i++)
	{
		for (struct Decl *d = decl_first_per_lane[lane_i]; d; d = d->next)
		{
			for (usize range_index = 0; range_index < range_count; range_index++)
			{
				if (d->byte_index >= range_starts[range_index]
					&& d->byte_index < range_ends[range_index])
				{
					d->valid = 0;
					break;
				}
			}
		}
	}

	{
		struct Marker **current_marker = push_array(scratch_arena, struct Marker *, num_lanes);
		struct Decl **current_decl   = push_array(scratch_arena, struct Decl *, num_lanes);
		for (usize lane_j = 0; lane_j < num_lanes; lane_j++)
		{
			current_marker[lane_j] = marker_first_per_lane[lane_j];
			current_decl[lane_j] = decl_first_per_lane[lane_j];
		}

		usize depth = 0;
		for (;;)
		{
			usize next_m_pos = (usize)-1;
			usize next_m_lane = 0;
			usize next_d_pos = (usize)-1;
			usize next_d_lane = 0;

			for (usize lane_j = 0; lane_j < num_lanes; lane_j++)
			{
				if (current_marker[lane_j] && current_marker[lane_j]->byte_index < next_m_pos)
				{
					next_m_pos = current_marker[lane_j]->byte_index;
					next_m_lane = lane_j;
				}
				if (current_decl[lane_j] && current_decl[lane_j]->byte_index < next_d_pos)
				{
					next_d_pos = current_decl[lane_j]->byte_index;
					next_d_lane = lane_j;
				}
			}

			if (next_m_pos == (usize)-1 && next_d_pos == (usize)-1)
				break;

			if (next_m_pos <= next_d_pos)
			{
				struct Marker *m = current_marker[next_m_lane];
				current_marker[next_m_lane] = m->next;

				b32 in_comment = 0;
				for (usize ri = 0; ri < range_count; ri++)
				{
					if (m->byte_index >= range_starts[ri]
						&& m->byte_index < range_ends[ri])
					{ in_comment = 1; break; }
				}
				if (in_comment) continue;

				if (m->kind == marker_brace_open) depth++;
				else if (m->kind == marker_brace_close && depth > 0) depth--;
			}
			else
			{
				struct Decl *decl = current_decl[next_d_lane];
				current_decl[next_d_lane] = decl->next;

				if (decl->valid && depth > 0)
					decl->valid = 0;
			}
		}
	}
}

func void
startup(char **input_filenames, usize input_files_count)
{
	struct {
		struct Arena *arena;
		struct Decl **decl_first;
		struct Decl **decl_last;
		struct Marker **marker_first;
		struct Marker **marker_last;
		struct File_Entry *input_files;
		usize total_files_size;
		u8 *files_memory;
	} shared = {0};

	if (lane_index() == 0)
	{
		if (input_files_count < 1)
		{
			os_con_write("Usage: fwd.exe <input_file>\n");
			os_exit(1);
		}

		u8 *arena_mem = (u8 *)os_mem_reserve(0, startup_arena_size);
		os_mem_commit(arena_mem, startup_arena_size);
		struct Arena *arena = (struct Arena *)arena_mem;
#ifdef DEBUG
		mem_clear(arena_mem, startup_arena_size);
#endif
		arena->reserved  = startup_arena_size - sizeof(struct Arena);
		arena->committed = startup_arena_size - sizeof(struct Arena);
		arena->base_address = align_forward_ptr(arena_mem + sizeof(struct Arena), 16);
		arena->used = 0;
#ifdef DEBUG
		arena->debug_owner_lane = (u32)-1;
#endif

		shared.arena = arena;

		shared.decl_first =
			push_array(arena, struct Decl *, lane_count());
		shared.decl_last =
			push_array(arena, struct Decl *, lane_count());
		shared.marker_first =
			push_array(arena, struct Marker *, lane_count());
		shared.marker_last =
			push_array(arena, struct Marker *, lane_count());

		shared.total_files_size = 0;
		shared.input_files = push_array(arena,
				struct File_Entry, input_files_count);
		for (usize input_file_index = 0;
			input_file_index < input_files_count;
			input_file_index++)
		{
			struct File_Entry *file_entry = &shared.input_files[input_file_index];
			char *input_filename = input_filenames[input_file_index];
			file_entry->os_file = os_file_open(input_filename);

			if (file_entry->os_file != 0)
			{
				file_entry->file_size = os_file_size(file_entry->os_file);
				shared.total_files_size += file_entry->file_size;
			}
			else
			{
				/* TODO: improve this when string abstractions arrive */
				struct Temp_Arena scratch = get_scratch();
				char *start = push_string(scratch.arena, "Could not find file: ");
				push_string(scratch.arena, input_filename);
				push_string(scratch.arena, "\n");
				char *end = push_struct(scratch.arena, char);
				*end = '\0';

				os_con_write(start);
				os_exit(1);
			}
		}

		if (shared.total_files_size != 0)
		{
			shared.files_memory =
				(u8 *)os_mem_reserve(0, shared.total_files_size);
			os_mem_commit(shared.files_memory, shared.total_files_size);

#ifdef DEBUG
			/* TODO: improve this when string abstractions arrive */
			os_con_write("Total files size: ");
			os_con_write_u64(shared.total_files_size);
			os_con_write("\n");
#endif
		}
		else
		{
			os_exit(0);
		}
	}

	lane_sync_ptr(&shared, 0);

	usize global_file_offset_start;
	usize global_file_offset_end;
	lane_range(shared.total_files_size,
			&global_file_offset_start, &global_file_offset_end);

	usize first_file_index = 0;
	usize last_file_index = 0;
	{
		usize accumulated = 0;
		for (usize i = 0; i < input_files_count; i++)
		{
			usize next = accumulated + shared.input_files[i].file_size;
			if (accumulated <= global_file_offset_start && global_file_offset_start < next)
				first_file_index = i;
			if (accumulated < global_file_offset_end && global_file_offset_end <= next)
			{
				last_file_index = i;
				break;
			}
			accumulated = next;
		}
	}

	usize lane_file_count = last_file_index - first_file_index + 1;
	struct Temp_Arena scratch = get_scratch();
	OS_File *lane_files = push_array(scratch.arena, OS_File, lane_file_count);
	for (usize file_i = 0; file_i < lane_file_count; file_i++)
		lane_files[file_i] =
			os_file_open(input_filenames[first_file_index + file_i]);

	usize total_to_read = global_file_offset_end - global_file_offset_start;
	usize bytes_read = 0;
	usize current_global_offset = global_file_offset_start;
	usize file_index = first_file_index;
	usize accumulated = 0;
	for (usize i = 0; i < file_index; i++)
		accumulated += shared.input_files[i].file_size;
	while (bytes_read < total_to_read)
	{
		usize file_offset;
		for (; file_index < input_files_count; file_index++)
		{
			if (current_global_offset < accumulated + shared.input_files[file_index].file_size)
				break;

			accumulated += shared.input_files[file_index].file_size;
		}
		file_offset = current_global_offset - accumulated;

		usize remaining_in_file = shared.input_files[file_index].file_size - file_offset;

		usize bytes_to_read = remaining_in_file;
		if (bytes_read + bytes_to_read > total_to_read)
		{
			bytes_to_read = total_to_read - bytes_read;
		}

		os_file_read_at(
				lane_files[file_index - first_file_index],
				file_offset,
				bytes_to_read,
				shared.files_memory + current_global_offset);

		bytes_read += bytes_to_read;
		current_global_offset += bytes_to_read;
	}

	for (usize file_i = 0; file_i < lane_file_count; file_i++)
		if (lane_files[file_i] != 0) os_file_close(lane_files[file_i]);
	release_scratch(scratch);

	lane_sync();

	u8 *files_memory = shared.files_memory;
	usize total_files_size = shared.total_files_size;

	struct Temp_Arena parse_scratch = get_scratch();
	struct Decl *decl_first = 0;
	struct Decl *decl_last  = 0;
	struct Marker *marker_first = 0;
	struct Marker *marker_last  = 0;

	parse_file_range(files_memory, total_files_size,
			global_file_offset_start, global_file_offset_end,
			parse_scratch.arena,
			&decl_first, &decl_last, &marker_first, &marker_last);

	for (usize copy_lane = 0; copy_lane < lane_count(); copy_lane++)
	{
		if (lane_index() == copy_lane)
		{
			struct Decl *new_decl_first = 0;
			struct Decl *new_decl_last = 0;
			for (struct Decl *src_decl = decl_first; src_decl; src_decl = src_decl->next)
			{
				struct Decl *dst_decl = push_struct(shared.arena, struct Decl);
				*dst_decl = *src_decl;
				dst_decl->next = 0;
				if (src_decl->name)
				{
					usize name_len = 0;
					while (src_decl->name[name_len] != '\0') name_len++;
					dst_decl->name = push_array(shared.arena, char, name_len + 1);
					memcpy(dst_decl->name, src_decl->name, name_len + 1);
				}
				if (src_decl->signature)
				{
					usize sig_len = 0;
					while (src_decl->signature[sig_len] != '\0') sig_len++;
					dst_decl->signature = push_array(shared.arena, char, sig_len + 1);
					memcpy(dst_decl->signature, src_decl->signature, sig_len + 1);
				}
				queue_push(new_decl_first, new_decl_last, dst_decl);
			}
			shared.decl_first[copy_lane] = new_decl_first;
			shared.decl_last[copy_lane] = new_decl_last;

			struct Marker *new_marker_first = 0;
			struct Marker *new_marker_last = 0;
			for (struct Marker *src_marker = marker_first; src_marker; src_marker = src_marker->next)
			{
				struct Marker *dst_marker = push_struct(shared.arena, struct Marker);
				*dst_marker = *src_marker;
				dst_marker->next = 0;
				queue_push(new_marker_first, new_marker_last, dst_marker);
			}
			shared.marker_first[copy_lane] = new_marker_first;
			shared.marker_last[copy_lane] = new_marker_last;
		}
		lane_sync();
	}

	release_scratch(parse_scratch);

	if (lane_index() == 0)
	{
		struct Temp_Arena inv_scratch = get_scratch();

		merge_and_invalidate(shared.decl_first,
				shared.marker_first,
				lane_count(), inv_scratch.arena);

		{
			struct Temp_Arena out_scratch = get_scratch();

			for (usize lane_i = 0; lane_i < lane_count(); lane_i++)
			{
				for (struct Decl *d = shared.decl_first[lane_i]; d; d = d->next)
				{
					if (!d->valid || d->kind != decl_typedef) continue;

					usize saved = out_scratch.arena->used;
					char *buf = push_string(out_scratch.arena, "typedef ");
					push_string(out_scratch.arena, d->keyword);
					push_string(out_scratch.arena, " ");
					push_string(out_scratch.arena, d->name);
					push_string(out_scratch.arena, " ");
					push_string(out_scratch.arena, d->name);
					push_string(out_scratch.arena, ";\n");
					*push_array(out_scratch.arena, char, 1) = '\0';
					os_con_write(buf);
					out_scratch.arena->used = saved;
				}
			}

			for (usize lane_i = 0; lane_i < lane_count(); lane_i++)
			{
				for (struct Decl *d = shared.decl_first[lane_i]; d; d = d->next)
				{
					if (!d->valid || d->kind != decl_func) continue;
					os_con_write(d->signature);
					os_con_write("\n");
				}
			}

			release_scratch(out_scratch);
		}

		release_scratch(inv_scratch);
	}

	lane_sync();
}
