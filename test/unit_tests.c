#include "base_test.c"
#include "base.c"
#include "fwd.c"

global_variable usize g_test_pass_count = 0;
global_variable usize g_test_fail_count = 0;

func void 
check(b32 condition, const char *test_name)
{
	if (condition)
	{
		printf("  PASS: %s\n", test_name);
		g_test_pass_count++;
	}
	else
	{
		printf("  FAIL: %s\n", test_name);
		g_test_fail_count++;
	}
}

func struct Arena 
make_test_arena(usize size)
{
	struct Arena result = {0};
	result.base_address = (u8 *)malloc(size);
	memset(result.base_address, 0, size);
	result.reserved = size;
	result.committed = size;
	result.debug_owner_lane = (u32)-1;
	return result;
}

func void 
free_test_arena(struct Arena *arena)
{
	free(arena->base_address);
	arena->base_address = 0;
}

func void 
test_cross_lane_block_comment_with_quotes(void)
{
	printf("test_cross_lane_block_comment_with_quotes:\n");

	const char *source = "/* he said \"hello */ struct Foo { int x; };\n";
	usize source_len = strlen(source);
	u8 *test_buffer = (u8 *)malloc(source_len);
	memcpy(test_buffer, source, source_len);

	usize split_point = 14;

	struct Arena lane0_arena = make_test_arena(4096);
	struct Arena lane1_arena = make_test_arena(4096);

	struct Decl *lane0_decl_first = 0, *lane0_decl_last = 0;
	struct Marker *lane0_marker_first = 0, *lane0_marker_last = 0;
	struct Decl *lane1_decl_first = 0, *lane1_decl_last = 0;
	struct Marker *lane1_marker_first = 0, *lane1_marker_last = 0;

	parse_file_range(test_buffer, source_len,
			0, split_point, &lane0_arena,
			&lane0_decl_first, &lane0_decl_last,
			&lane0_marker_first, &lane0_marker_last);

	parse_file_range(test_buffer, source_len,
			split_point, source_len, &lane1_arena,
			&lane1_decl_first, &lane1_decl_last,
			&lane1_marker_first, &lane1_marker_last);

	struct Decl *decl_first_per_lane[2] = { lane0_decl_first, lane1_decl_first };
	struct Marker *marker_first_per_lane[2] = { lane0_marker_first, lane1_marker_first };

	struct Arena merge_arena = make_test_arena(4096);
	merge_and_invalidate(decl_first_per_lane, marker_first_per_lane, 2, &merge_arena);

	b32 found_foo = 0;
	for (usize lane_i = 0; lane_i < 2; lane_i++)
	{
		for (struct Decl *decl = decl_first_per_lane[lane_i]; decl; decl = decl->next)
		{
			if (decl->valid && decl->kind == decl_typedef
				&& decl->name && strcmp(decl->name, "Foo") == 0)
				found_foo = 1;
		}
	}

	check(found_foo, "struct Foo found and valid after cross-lane block comment with quotes");

	free(test_buffer);
	free_test_arena(&lane0_arena);
	free_test_arena(&lane1_arena);
	free_test_arena(&merge_arena);
}

func void 
test_multiline_block_comment_with_quote(void)
{
	printf("test_multiline_block_comment_with_quote:\n");

	const char *source =
		"/* Documentation\n"
		" * He said \"hello\n"
		" */ struct Bar { int x; };\n";
	usize source_len = strlen(source);
	u8 *test_buffer = (u8 *)malloc(source_len);
	memcpy(test_buffer, source, source_len);

	usize split_point = 30;

	struct Arena lane0_arena = make_test_arena(4096);
	struct Arena lane1_arena = make_test_arena(4096);

	struct Decl *lane0_decl_first = 0, *lane0_decl_last = 0;
	struct Marker *lane0_marker_first = 0, *lane0_marker_last = 0;
	struct Decl *lane1_decl_first = 0, *lane1_decl_last = 0;
	struct Marker *lane1_marker_first = 0, *lane1_marker_last = 0;

	parse_file_range(test_buffer, source_len,
			0, split_point, &lane0_arena,
			&lane0_decl_first, &lane0_decl_last,
			&lane0_marker_first, &lane0_marker_last);

	parse_file_range(test_buffer, source_len,
			split_point, source_len, &lane1_arena,
			&lane1_decl_first, &lane1_decl_last,
			&lane1_marker_first, &lane1_marker_last);

	struct Decl *decl_first_per_lane[2] = { lane0_decl_first, lane1_decl_first };
	struct Marker *marker_first_per_lane[2] = { lane0_marker_first, lane1_marker_first };

	struct Arena merge_arena = make_test_arena(4096);
	merge_and_invalidate(decl_first_per_lane, marker_first_per_lane, 2, &merge_arena);

	b32 found_bar = 0;
	for (usize lane_i = 0; lane_i < 2; lane_i++)
	{
		for (struct Decl *decl = decl_first_per_lane[lane_i]; decl; decl = decl->next)
		{
			if (decl->valid && decl->kind == decl_typedef
				&& decl->name && strcmp(decl->name, "Bar") == 0)
				found_bar = 1;
		}
	}

	check(found_bar, "struct Bar found after multiline block comment with quotes");

	free(test_buffer);
	free_test_arena(&lane0_arena);
	free_test_arena(&lane1_arena);
	free_test_arena(&merge_arena);
}

func void 
test_backslash_continuation_string(void)
{
	printf("test_backslash_continuation_string:\n");

	const char *source =
		"char *s = \"hello \\\n"
		"world\";\n"
		"struct Baz { int x; };\n";
	usize source_len = strlen(source);
	u8 *test_buffer = (u8 *)malloc(source_len);
	memcpy(test_buffer, source, source_len);

	usize line1_len = 19; /* char *s = "hello \\n */
	usize split_point = line1_len + 2; /* into "wo|rld" */

	struct Arena lane0_arena = make_test_arena(4096);
	struct Arena lane1_arena = make_test_arena(4096);

	struct Decl *lane0_decl_first = 0, *lane0_decl_last = 0;
	struct Marker *lane0_marker_first = 0, *lane0_marker_last = 0;
	struct Decl *lane1_decl_first = 0, *lane1_decl_last = 0;
	struct Marker *lane1_marker_first = 0, *lane1_marker_last = 0;

	parse_file_range(test_buffer, source_len,
			0, split_point, &lane0_arena,
			&lane0_decl_first, &lane0_decl_last,
			&lane0_marker_first, &lane0_marker_last);

	parse_file_range(test_buffer, source_len,
			split_point, source_len, &lane1_arena,
			&lane1_decl_first, &lane1_decl_last,
			&lane1_marker_first, &lane1_marker_last);

	struct Decl *decl_first_per_lane[2] = { lane0_decl_first, lane1_decl_first };
	struct Marker *marker_first_per_lane[2] = { lane0_marker_first, lane1_marker_first };

	struct Arena merge_arena = make_test_arena(4096);
	merge_and_invalidate(decl_first_per_lane, marker_first_per_lane, 2, &merge_arena);

	b32 found_baz = 0;
	b32 spurious_decl_in_string = 0;
	for (usize lane_i = 0; lane_i < 2; lane_i++)
	{
		for (struct Decl *decl = decl_first_per_lane[lane_i]; decl; decl = decl->next)
		{
			if (decl->valid && decl->kind == decl_typedef
				&& decl->name && strcmp(decl->name, "Baz") == 0)
				found_baz = 1;
			/* "world" contains 'l' 'd' -- no keyword, but check nothing
			 * spurious shows up before Baz's byte_index */
			if (decl->valid && decl->byte_index < line1_len + 8)
				spurious_decl_in_string = 1;
		}
	}

	check(found_baz, "struct Baz found after backslash-continued string");
	check(!spurious_decl_in_string, "no spurious decl inside continued string");

	free(test_buffer);
	free_test_arena(&lane0_arena);
	free_test_arena(&lane1_arena);
	free_test_arena(&merge_arena);
}

func void 
test_if0_block_not_emitted(void)
{
	printf("test_if0_block_not_emitted:\n");

	const char *source =
		"#if 0\n"
		"struct Ghost { int x; };\n"
		"#endif\n"
		"struct Real { int y; };\n";
	usize source_len = strlen(source);
	u8 *test_buffer = (u8 *)malloc(source_len);
	memcpy(test_buffer, source, source_len);

	struct Arena lane0_arena = make_test_arena(4096);

	struct Decl *decl_first = 0, *decl_last = 0;
	struct Marker *marker_first = 0, *marker_last = 0;

	parse_file_range(test_buffer, source_len,
			0, source_len, &lane0_arena,
			&decl_first, &decl_last,
			&marker_first, &marker_last);

	struct Decl *decl_first_per_lane[1] = { decl_first };
	struct Marker *marker_first_per_lane[1] = { marker_first };

	struct Arena merge_arena = make_test_arena(4096);
	merge_and_invalidate(decl_first_per_lane, marker_first_per_lane, 1, &merge_arena);

	b32 found_ghost = 0;
	b32 found_real = 0;
	for (struct Decl *decl = decl_first_per_lane[0]; decl; decl = decl->next)
	{
		if (decl->valid && decl->kind == decl_typedef && decl->name)
		{
			if (strcmp(decl->name, "Ghost") == 0) found_ghost = 1;
			if (strcmp(decl->name, "Real") == 0) found_real = 1;
		}
	}

	check(!found_ghost, "struct Ghost inside #if 0 should NOT be emitted");
	check(found_real, "struct Real after #endif is found");

	free(test_buffer);
	free_test_arena(&lane0_arena);
	free_test_arena(&merge_arena);
}

int main(void)
{
	l_lane_index = 0;

	test_cross_lane_block_comment_with_quotes();
	test_multiline_block_comment_with_quote();
	test_backslash_continuation_string();
	test_if0_block_not_emitted();

	printf("\n%zu/%zu unit tests passed\n",
			g_test_pass_count, g_test_pass_count + g_test_fail_count);
	return g_test_fail_count > 0 ? 1 : 0;
}
