struct File_Entry
{
	OS_File os_file;
	usize file_size;
};

struct SharedMemory
{
	/* NOTE(cdecompilador): Only a single lane can write here per lane_sync */
	struct Arena arena;

	char **input_filenames;
	struct File_Entry *input_files;
	usize input_files_count;

	usize total_files_size;
	u8 *files_memory;

	usize *func_count;
	usize *struct_count;
	usize *union_count;
	usize *enum_count;
};

#define shared_memory_size kb(16)
global_variable u8 *g_shared_memory;
#define shared_memory()((struct SharedMemory *)g_shared_memory)

func void init_shared_memory()
{
	struct SharedMemory *_shared_memory = (struct SharedMemory *)g_shared_memory;
#ifdef DEBUG
	mem_clear(g_shared_memory, shared_memory_size);
#endif
	_shared_memory->arena.reserved  = shared_memory_size - sizeof(struct SharedMemory);
	_shared_memory->arena.committed = shared_memory_size - sizeof(struct SharedMemory);
	_shared_memory->arena.base_address = align_forward_ptr(g_shared_memory + sizeof(struct SharedMemory), 16);
	_shared_memory->arena.used = 0;
}

/* FIXME(cdecompilador): The worst case scenario that the code has no comments is very bad, a
 * better option is that all threads store comment start - end they encounter and then invalid 
 * parsing results based on that information */
func b32
file_chunk_in_comment(u8 *files_memory, usize start_offset)
{
	b32 in_comment = 0;
	if (start_offset > 0)
	{
		usize byte_index = start_offset;
		while (byte_index >= 2)
		{
			byte_index--;
			if (files_memory[byte_index - 1] == '*' && files_memory[byte_index] == '/')
			{
				in_comment = 0;
				break;
			}
			if (files_memory[byte_index - 1] == '/' && files_memory[byte_index] == '*')
			{
				in_comment = 1;
				break;
			}
		}
	}

	return in_comment;
}

func void
file_chunk_line_state(u8 *files_memory, usize start_offset, b32 *out_in_string, b32 *out_in_line_comment)
{
	*out_in_string = 0;
	*out_in_line_comment = 0;
	if (start_offset == 0) return;

	usize line_start = start_offset;
	while (line_start > 0 && files_memory[line_start - 1] != '\n')
		line_start--;

	b32 in_block = file_chunk_in_comment(files_memory, line_start);
	b32 in_str = 0;

	for (usize i = line_start; i < start_offset; i++)
	{
		if (in_block)
		{
			if (i + 1 < start_offset && files_memory[i] == '*' && files_memory[i + 1] == '/')
			{ in_block = 0; i++; }
		}
		else if (in_str)
		{
			if (files_memory[i] == '\\') i++;
			else if (files_memory[i] == '"') in_str = 0;
		}
		else if (files_memory[i] == '"') in_str = 1;
		else if (files_memory[i] == '\'')
		{
			i++;
			if (i < start_offset && files_memory[i] == '\\') i++;
			if (i < start_offset) i++;
		}
		else if (i + 1 < start_offset && files_memory[i] == '/' && files_memory[i + 1] == '*')
		{ in_block = 1; i++; }
		else if (i + 1 < start_offset && files_memory[i] == '/' && files_memory[i + 1] == '/')
		{
			*out_in_line_comment = 1;
			return;
		}
	}

	*out_in_string = in_str;
}

func void
startup()
{
	if (lane_index() == 0)
	{
		if (shared_memory()->input_files_count < 1)
		{
			os_con_write("Usage: fwd.exe <input_file>\n");
			os_exit(1);
		}
		
		shared_memory()->total_files_size = 0;
		shared_memory()->input_files = push_array(&shared_memory()->arena, 
				struct File_Entry, shared_memory()->input_files_count);
		for (usize input_file_index = 0; 
			input_file_index < shared_memory()->input_files_count; 
			input_file_index++)
		{
			struct File_Entry *file_entry = &shared_memory()->input_files[input_file_index];
			char *input_filename = shared_memory()->input_filenames[input_file_index];
			file_entry->os_file = os_file_open(input_filename);

			if (file_entry->os_file != 0)
			{
				file_entry->file_size = os_file_size(file_entry->os_file);
				shared_memory()->total_files_size += file_entry->file_size;
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

		if (shared_memory()->total_files_size != 0)
		{
			shared_memory()->files_memory = 
				(u8 *)os_mem_reserve(0, shared_memory()->total_files_size);
			os_mem_commit(shared_memory()->files_memory, shared_memory()->total_files_size);

#ifdef DEBUG
			/* TODO: improve this when string abstractions arrive */
			os_con_write("Total files size: ");
			os_con_write_u64(shared_memory()->total_files_size);
			os_con_write("\n");
#endif
		}
		else
		{
			os_exit(0);
		}

		shared_memory()->func_count = push_array(&shared_memory()->arena, usize, lane_count());
		shared_memory()->struct_count = push_array(&shared_memory()->arena, usize, lane_count());
		shared_memory()->union_count = push_array(&shared_memory()->arena, usize, lane_count());
		shared_memory()->enum_count = push_array(&shared_memory()->arena, usize, lane_count());
	}

	lane_sync();

	usize global_file_offset_start;
	usize global_file_offset_end;
	lane_range(shared_memory()->total_files_size, &global_file_offset_start, &global_file_offset_end);

	/* NOTE(cdecompilador): Each lane opens its own file handles so reads are truly parallel
	 * instead of serialized on a shared handle. We only open the files this lane touches. */
	usize first_file_index = 0;
	usize last_file_index = 0;
	{
		usize accumulated = 0;
		for (usize i = 0; i < shared_memory()->input_files_count; i++)
		{
			usize next = accumulated + shared_memory()->input_files[i].file_size;
			if (accumulated <= global_file_offset_start && global_file_offset_start < next)
				first_file_index = i;
			if (accumulated < global_file_offset_end && global_file_offset_end <= next)
			{ last_file_index = i; break; }
			accumulated = next;
		}
	}

	usize lane_file_count = last_file_index - first_file_index + 1;
	struct Temp_Arena scratch = get_scratch();
	OS_File *lane_files = push_array(scratch.arena, OS_File, lane_file_count);
	for (usize file_i = 0; file_i < lane_file_count; file_i++)
		lane_files[file_i] = 
			os_file_open(shared_memory()->input_filenames[first_file_index + file_i]);

	usize total_to_read = global_file_offset_end - global_file_offset_start;
	usize bytes_read = 0;
	usize current_global_offset = global_file_offset_start;
	usize file_index = first_file_index;
	usize accumulated = 0;
	for (usize i = 0; i < file_index; i++)
		accumulated += shared_memory()->input_files[i].file_size;
	while (bytes_read < total_to_read)
	{
		usize file_offset;
		for (; file_index < shared_memory()->input_files_count; file_index++)
		{
			if (current_global_offset < accumulated + shared_memory()->input_files[file_index].file_size)
				break;

			accumulated += shared_memory()->input_files[file_index].file_size;
		}
		file_offset = current_global_offset - accumulated;

		usize remaining_in_file = shared_memory()->input_files[file_index].file_size - file_offset;

		usize bytes_to_read = remaining_in_file;
		if (bytes_read + bytes_to_read > total_to_read)
		{
			bytes_to_read = total_to_read - bytes_read;
		}

		os_file_read_at(
				lane_files[file_index - first_file_index],
				file_offset,
				bytes_to_read,
				shared_memory()->files_memory + current_global_offset);

		bytes_read += bytes_to_read;
		current_global_offset += bytes_to_read;
	}

	for (usize file_i = 0; file_i < lane_file_count; file_i++)
		if (lane_files[file_i] != 0) os_file_close(lane_files[file_i]);
	release_scratch(scratch);

	lane_sync();

	u8 *files_memory = shared_memory()->files_memory;
	usize total_files_size = shared_memory()->total_files_size;

	b32 in_comment = file_chunk_in_comment(
			files_memory,
			global_file_offset_start);
	b32 in_string = 0;
	b32 in_line_comment = 0;
	if (!in_comment)
		file_chunk_line_state(files_memory, global_file_offset_start, &in_string, &in_line_comment);

#define is_ident_char(c) (((c) >= 'a' && (c) <= 'z') || \
                          ((c) >= 'A' && (c) <= 'Z') || \
                          ((c) >= '0' && (c) <= '9') || \
                          (c) == '_')
#define is_keyword(keyword) \
    ((byte_index) + (sizeof(keyword) - 1) <= (total_files_size) \
     && mem_match(&(files_memory)[(byte_index)], (keyword), (sizeof(keyword) - 1)) \
     && ((byte_index) + (sizeof(keyword) - 1) >= (total_files_size) \
         || !is_ident_char((files_memory)[(byte_index) + (sizeof(keyword) - 1)])))

	/* NOTE(cdecompilador): Edge case where one of our desired keywords starts mid-lane border and
	 * we want at least the left lane be able to parse it */
#define longest_keyword_size \
	max(max(sizeof("func"), sizeof("struct")), \
	      max(sizeof("enum"), sizeof("union"))) - 1
	global_file_offset_end = min(global_file_offset_end + longest_keyword_size, total_files_size);

	usize func_count = 0;
	usize struct_count = 0;
	usize union_count = 0;
	usize enum_count = 0;
	for (usize byte_index = global_file_offset_start; 
		 byte_index < global_file_offset_end; 
		 byte_index++)
	{
		/* FIXME(cdecompilador): remove all continue by making all of this a big if-elseif */
		if (in_line_comment)
		{
			if (files_memory[byte_index] == '\n') in_line_comment = 0;
		}
		else if (in_comment)
		{
			if (byte_index + 1 < global_file_offset_end 
					&& files_memory[byte_index] == '*'
					&& files_memory[byte_index + 1] == '/')
			{
				in_comment = 0;
				byte_index++;
			}
		}
		else if (in_string)
		{
			if (files_memory[byte_index] == '\\') byte_index++;
			else if (files_memory[byte_index] == '"') in_string = 0;
		}
		else if (byte_index + 1 < global_file_offset_end
				&& files_memory[byte_index] == '/'
				&& files_memory[byte_index + 1] == '*')
		{
			in_comment = 1;
			byte_index++;
		}
		else if (byte_index + 1 < global_file_offset_end
				&& files_memory[byte_index] == '/'
				&& files_memory[byte_index + 1] == '/')
		{
			in_line_comment = 1;
			byte_index++;
		}
		else if (files_memory[byte_index] == '"')
		{
			in_string = 1;
		}
		else if (files_memory[byte_index] == '\'')
		{
			byte_index++;
			if (byte_index < global_file_offset_end && files_memory[byte_index] == '\\')
				byte_index++;
			if (byte_index < global_file_offset_end)
				byte_index++;
		}
		/* NOTE(cdecompilador): ignore edge case that we are in the middle of a word that maybe
		 * contains one the keywords we look for */
		else if (byte_index > 0 && is_ident_char(files_memory[byte_index - 1]))
		{
		}
		else
		{
			/* NOTE(cdecompilador): What if the keyword is between lane boundaries? i think the
			 * lane where it starts should do the parsing */
			if (is_keyword("func"))
			{
				/* TODO(cdecompilador): Verify the function signature properly + skip while
				 * inside function body */
				func_count++;
			}
			else if (is_keyword("struct"))
			{
				/* TODO(cdecompilador): Verify struct signature property + skip while 
				 * inside struct body */
				struct_count++;
			}
			else if (is_keyword("union"))
			{
				/* TODO(cdecompilador): Verify struct signature property + skip while 
				 * inside union body */
				union_count++;
			}
			else if (is_keyword("enum"))
			{
				/* TODO(cdecompilador): Verify struct signature property + skip while 
				 * inside union body */
				enum_count++;
			}
		}
	}

	shared_memory()->func_count[lane_index()]   = func_count;
	shared_memory()->struct_count[lane_index()] = struct_count;
	shared_memory()->union_count[lane_index()]  = union_count;
	shared_memory()->enum_count[lane_index()]   = enum_count;

	lane_sync();

	if (lane_index() == 0)
	{
		usize total_func   = 0;
		usize total_struct = 0;
		usize total_union  = 0;
		usize total_enum   = 0;
		for (usize lane_i = 0; lane_i < lane_count(); lane_i++)
		{
			total_func   += shared_memory()->func_count[lane_i];
			total_struct += shared_memory()->struct_count[lane_i];
			total_union  += shared_memory()->union_count[lane_i];
			total_enum   += shared_memory()->enum_count[lane_i];
		}

		os_con_write("func: ");
		os_con_write_u64(total_func);
		os_con_write("\nstruct: ");
		os_con_write_u64(total_struct);
		os_con_write("\nunion: ");
		os_con_write_u64(total_union);
		os_con_write("\nenum: ");
		os_con_write_u64(total_enum);
		os_con_write("\n");
	}
}
