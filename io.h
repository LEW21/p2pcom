#pragma once

#include "string.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>

int io_fwrite(FILE* f, string s)
{
	return TEMP_FAILURE_RETRY(fwrite(s.val, s.len, 1, f));
}

int io_print(string s)
{
	return io_fwrite(stdout, s);
}

// It stores a mallocated pointer in line->val, that should be freed by user.
int io_getline(FILE* f, string* line)
{
	size_t zero = 0;
	char* line_val;
	line->len = TEMP_FAILURE_RETRY(getline(&line_val, &zero, f));
	line->val = line_val;
	return line->len;
}

int io_inputline(string* line)
{
	return io_getline(stdin, line);
}
