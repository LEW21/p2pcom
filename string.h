#pragma once

#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

typedef struct string
{
	const char* val;
	size_t len;
} string;

string make_string(const char* val, int len)
{
	string s = {val, len};
	return s;
}

string S(const char* val)
{
	string s = {val, strlen(val)};
	return s;
}

bool string_equal(string a, string b)
{
	if (a.len != b.len)
		return false;

	for (int i = 0; i < a.len; ++i)
		if (a.val[i] != b.val[i])
			return false;

	return true;
}

bool string_iequal(string a, string b)
{
	if (a.len != b.len)
		return false;

	for (int i = 0; i < a.len; ++i)
		if (tolower(a.val[i]) != tolower(b.val[i]))
			return false;

	return true;
}

bool string_equal_c(string a, const char* b)
{
	return string_equal(a, make_string(b, strlen(b)));
}

bool string_iequal_c(string a, const char* b)
{
	return string_iequal(a, make_string(b, strlen(b)));
}

int string_copy(string s, char* buffer, int buf_size)
{
	if (s.len > buf_size)
		return -1;
	memcpy(buffer, s.val, s.len);
	return s.len;
}

int string_copy_truncate(string s, char* buffer, int buf_size)
{
	int len = s.len <= buf_size ? s.len : buf_size;
	memcpy(buffer, s.val, len);
	return len;
}

long string_to_number_b(string s, int base)
{
	char buffer[100];
	buffer[string_copy_truncate(s, buffer, sizeof(buffer)-1)] = '\0';
	return strtol(buffer, 0, base);
}

long string_to_number(string s)
{
	return string_to_number_b(s, 10);
}

int buffer_append(char** start, char* end, string s)
{
	int copied = string_copy(s, *start, end - *start);
	if (copied < 0)
		return -1;

	*start = *start + copied;
	return copied;
}

int buffer_append_truncate(char** start, char* end, string s)
{
	int copied = string_copy_truncate(s, *start, end - *start);
	*start = *start + copied;
	return copied;
}

int string_indexof(string s, char c)
{
	for (int i = 0; i < s.len; ++i)
		if (s.val[i] == c)
			return i;

	return -1;
}

bool string_contains(string s, char c)
{
	return string_indexof(s, c) != -1;
}

typedef struct two_strings
{
	string a;
	string b;
} two_strings;

two_strings string_split(string raw, char delim)
{
	int pos = string_indexof(raw, delim);

	if (pos != -1)
	{
		two_strings ts = {{raw.val, pos}, {raw.val + pos + 1, raw.len - pos - 1}};
		return ts;
	}

	two_strings ts = {{raw.val, raw.len}, {0, 0}};
	return ts;
}
