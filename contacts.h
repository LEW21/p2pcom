#pragma once

#include "string.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>
#include "io.h"

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

bool sockaddr_in_equal(sockaddr_in a, sockaddr_in b)
{
	return a.sin_family == b.sin_family && a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

typedef struct contact
{
	char name_val[100];
	int name_len;
	sockaddr_in address;
	time_t last_update;
} contact;

const int MYSELF = 0;

string contact_name(const contact* c)
{
	return make_string(c->name_val, c->name_len);
}

void contact_set_name(contact* c, string s)
{
	c->name_len = string_copy_truncate(s, c->name_val, 100);
}

typedef struct contacts
{
	int len;
	contact list[1024];
	pthread_mutex_t mtx;
} contacts;

// Returns contact id (it's position on the list) or -1 if not found.
int find_contact_by_address(const contacts* contacts, sockaddr_in address)
{
	// "Jeśli w bazie pojawia się duplikat danego nicka z innym adresem to należy używać tego nowszego"
	// That's why start from the end.
	for (int i = contacts->len - 1; i >= 0; --i)
		if (sockaddr_in_equal(address, contacts->list[i].address))
			return i;

	return -1;
}

// Returns contact id (it's position on the list) or -1 if not found.
int find_contact_by_name(const contacts* contacts, string name)
{
	// "Jeśli w bazie pojawia się duplikat danego nicka z innym adresem to należy używać tego nowszego"
	// That's why start from the end.
	for (int i = contacts->len - 1; i >= 0; --i)
		if (string_equal(name, contact_name(&contacts->list[i])))
			return i;

	return -1;
}

// Welcome the user! :D
void greet(contacts* contacts)
{
	if (contacts->len > 0)
	{
		if (contact_name(&contacts->list[0]).len)
		{
			io_print(S("Welcome back, "));
			io_print(contact_name(&contacts->list[0]));
			io_print(S("!\n"));
		}

		if (contacts->len-1 == 1) // 0th is myself
			printf("Loaded 1 contact.\n");
		else
			printf("Loaded %d contact(s).\n", contacts->len-1);
	}
	else
		printf("Loaded 0 contacts.\n");
}

void contacts_save(contacts* contacts)
{
	FILE* file = fopen("p2pcom-contacts", "w");
	if (!file)
	{
		perror("saving contacts - opening the file");
		return;
	}
	io_fwrite(file, make_string((char*) &contacts->list, contacts->len * sizeof(contact)));
	if (fclose(file) != 0)
	{
		perror("saving contacts - closing the file");
		return;
	}
}

void contacts_load(contacts* contacts)
{
	FILE* file = fopen("p2pcom-contacts", "r");
	if (!file)
	{
		perror("loading contacts - opening the file");
		return;
	}

	contacts->len = TEMP_FAILURE_RETRY(fread(contacts->list, sizeof(contact), sizeof(contacts->list)/sizeof(contact), file));
	if (contacts->len < 0)
	{
		perror("loading contacts");
		contacts->len = 0;
		return;
	}

	if (fclose(file) != 0)
	{
		perror("loading contacts - closing the file");
		return;
	}
}

void contacts_init(contacts* contacts, sockaddr_in self_addr)
{
	contact_set_name(&contacts->list[0], make_string(0, 0));
	contacts->len = 1;

	contacts_load(contacts);

	contacts->list[0].address = self_addr;
	pthread_mutex_init(&contacts->mtx, 0);

	greet(contacts);
}

void contacts_destroy(contacts* contacts)
{
	pthread_mutex_destroy(&contacts->mtx);
}

void insert_contact(contacts* contacts, contact new_contact)
{
	int c = (contacts->len)++;
	contacts->list[c] = new_contact;

	io_print(S("Learned about "));
	io_print(contact_name(&new_contact));
	io_print(S(".\n"));

	contacts_save(contacts);
}

void update_contact(contacts* contacts, int c, contact new_contact)
{
	bool changed = memcmp(&contacts->list[c].address, &new_contact.address, sizeof(new_contact.address)) != 0;
	contacts->list[c] = new_contact;

	if (changed)
	{
		io_print(S("Updated "));
		io_print(contact_name(&new_contact));
		io_print(S("'s address.\n"));
	}

	contacts_save(contacts);
}

// Updates a contact if it exists (looking by name) - and inserts it otherwise.
bool insert_or_update_contact(contacts* contacts, contact new_contact)
{
	int c = find_contact_by_name(contacts, contact_name(&new_contact));
	if (c < 0)
	{
		insert_contact(contacts, new_contact);
		return true;
	}

	if (c > 0 && new_contact.last_update > contacts->list[c].last_update)
	{
		update_contact(contacts, c, new_contact);
		return true;
	}

	return false;
}

#include <stdio.h>
#include <inttypes.h>

#define xstr(s) str(s)
#define str(s) #s

char     iam_usage[] = "Usage: /iam name (name cannot contain spaces)\n";
char contact_usage[] = "Usage: /contact name ip port=" xstr(STD_PORT) " last_update=now\n";

// Returns "/iam my_name".
string iam_command(contact* c, char* buffer, size_t buffer_len)
{
	char* start = buffer;
	char* end = buffer + buffer_len;
	buffer_append(&start, end, S("/iam "));
	buffer_append(&start, end, contact_name(c));

	if (buffer_append(&start, end, S("\n")) < 0)
		return make_string(0, 0);

	return make_string(buffer, start - buffer);
}

// Serializes the contact to "/contact name ip port last_update" format.
string contact_command(contact* c, char* buffer, size_t buffer_len)
{
	char* start = buffer;
	char* end = buffer + buffer_len;
	buffer_append(&start, end, S("/contact "));
	buffer_append(&start, end, contact_name(c));
	buffer_append(&start, end, S(" "));

	{
		char str[INET_ADDRSTRLEN];
		if (!inet_ntop(AF_INET, &c->address.sin_addr, str, INET_ADDRSTRLEN))
			return make_string(0, 0);
		buffer_append(&start, end, S(str));
	}

	buffer_append(&start, end, S(" "));

	{
		char str[10];
		snprintf(str, sizeof(str), "%u", ntohs(c->address.sin_port));
		buffer_append(&start, end, S(str));
	}

	buffer_append(&start, end, S(" "));

	{
		char str[20];
		snprintf(str, sizeof(str), "%" PRIuLEAST64, c->last_update);
		buffer_append(&start, end, S(str));
	}

	if (buffer_append(&start, end, S("\n")) < 0)
		return make_string(0, 0);

	return make_string(buffer, start - buffer);
}

typedef struct contact_params
{
	string name;
	string ip;
	string port;
	string last_update;
	string rest;
} contact_params;

contact_params split_contact_params(string params)
{
	contact_params cp;
	two_strings ts = {{0,0}, params};
	ts = string_split(ts.b, ' ');
	cp.name = ts.a;
	ts = string_split(ts.b, ' ');
	cp.ip = ts.a;
	ts = string_split(ts.b, ' ');
	cp.port = ts.a;
	ts = string_split(ts.b, ' ');
	cp.last_update = ts.a;
	cp.rest = ts.b;
	return cp;
}

// Parses the parameters to /contact - "name ip port last_update" - and returns them as a contact object.
// In case of error, contact_name(contact).len == 0.
contact parse_contact_params(string args)
{
	contact c;
	memset(&c, 0, sizeof(contact));

	contact_params params = split_contact_params(args);

	if (!params.name.len || !params.ip.len || params.rest.len)
		return c;

	c.address.sin_family = AF_INET;

	{
		char str[INET_ADDRSTRLEN];
		str[string_copy_truncate(params.ip, str, INET_ADDRSTRLEN-1)] = '\0';
		if (inet_pton(AF_INET, str, &(c.address.sin_addr)) == 0)
			return c;
	}

	c.address.sin_port = htons(params.port.len ? string_to_number(params.port) : STD_PORT);
	c.last_update = params.last_update.len ? string_to_number(params.last_update) : time(0);

	contact_set_name(&c, params.name);
	return c;
}

// Inserts/updates a contact.
bool handle_contact_command(string args, contacts* contacts)
{
	contact c = parse_contact_params(args);
	if (!c.name_len)
		return false;

	insert_or_update_contact(contacts, c);

	return true;
}

// Sets user's name.
bool handle_local_iam_command(string args, contacts* contacts)
{
	if (string_contains(args, ' '))
		return false;

	contact_set_name(&contacts->list[0], args);
	contacts_save(contacts);

	return true;
}

// Inserts/updates a contact that has just messaged us.
bool handle_remote_iam_command(string args, contacts* contacts, sockaddr_in sender_addr)
{
	if (string_contains(args, ' '))
		return false;

	contact c;
	contact_set_name(&c, args);
	c.address = sender_addr;
	c.last_update = time(0);

	insert_or_update_contact(contacts, c);
	return true;
}
