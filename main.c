#define _GNU_SOURCE

#define STD_PORT 2121

#include "string.h"
#include "contacts.h"
#include "io.h"
#include <sys/socket.h>
#include <pthread.h>

typedef struct message
{
	string command;
	string data;
} message;

message parse_message(string raw)
{
	two_strings ts = string_split(raw, ' ');
	message msg = {ts.a, ts.b};
	return msg;
}

// Show the contacts.
void handle_local_contacts_command(contacts* contacts)
{
	for (int i = 1; i < contacts->len; ++i) // Don't print 0 - the user.
	{
		contact* c = &contacts->list[i];
		char buffer[1024];
		string cmd = contact_command(c, buffer, sizeof(buffer));
		io_print(cmd);
	}
}

// Send our contacts to the requesting socket.
void handle_remote_contacts_command(contacts* contacts, sockaddr_in sender_addr, int socket)
{
	for (int i = 0; i < contacts->len; ++i)
	{
		contact* c = &contacts->list[i];
		char buffer[1024];
		string cmd = i == 0 ? iam_command(c, buffer, sizeof(buffer)) : contact_command(c, buffer, sizeof(buffer));

		int l = TEMP_FAILURE_RETRY(sendto(socket, cmd.val, cmd.len, 0, &sender_addr, sizeof(sender_addr)));
		if (l < 0) {} // Ignore, this is not our problem, this is requester's problem.
	}
}

void handle_remote_text_message(sockaddr_in sender_addr, int socket, string raw_message, contacts* contacts)
{
	int sender_id = find_contact_by_address(contacts, sender_addr);

	if (sender_id < 0)
	{
		io_fwrite(stderr, S("Anonymous message received, ignoring.\n"));
		return;
	}

	io_print(contact_name(&contacts->list[sender_id]));
	io_print(S(": "));
	io_print(raw_message);
	io_print(S("\n"));
}

// Handle a message we received over the network.
void handle_received_message(sockaddr_in sender_addr, int socket, string raw_message, contacts* contacts)
{
	if (raw_message.len == 0)
		return;

	if (raw_message.val[0] != '/')
	{
		handle_remote_text_message(sender_addr, socket, raw_message, contacts);
		return;
	}

	raw_message.val += 1;
	raw_message.len -= 1;

	message message = parse_message(raw_message);

	if (string_iequal_c(message.command, "IAM"))
		(void)(handle_remote_iam_command(message.data, contacts, sender_addr) || io_fwrite(stderr, S("Invalid IAM received.\n")));

	else if (string_iequal_c(message.command, "CONTACT"))
		(void)(handle_contact_command(message.data, contacts) || io_fwrite(stderr, S("Invalid contact received.\n")));

	else if (string_iequal_c(message.command, "CONTACTS"))
		handle_remote_contacts_command(contacts, sender_addr, socket);

	else
		io_fwrite(stderr, S("Unknown command received.\n"));
}

void send_message(string message, int socket, contacts* contacts, int* send_to);

// Set the send_to variable to the selected contact.
void handle_to_command(string args, int socket, contacts* contacts, int* send_to)
{
	int s = find_contact_by_name(contacts, args);
	if (s < 0)
	{
		io_fwrite(stderr, S("No such contact.\n"));
		return;
	}
	if (contacts->list[s].last_update + 60 < time(0))
	{
		io_fwrite(stderr, S("WARNING: Contact has expired.\n"));
	}

	int enable = 0;
	setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

	*send_to = s;
}

// Set the send_to variable to -1, which means broadcast.
void handle_to_broadcast_command(string args, int socket, contacts* contacts, int* send_to)
{
	int enable = 1;
	setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

	*send_to = -1;
}

// Return the correct socket address according to the send_to variable.
sockaddr_in get_target_address(contacts* contacts, int* send_to)
{
	if (*send_to >= 0)
		return contacts->list[*send_to].address;

	sockaddr_in target;
	memset(&target, 0, sizeof(target));
	target.sin_family = AF_INET;
	target.sin_port = htons(STD_PORT);
	target.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	return target;
}

// Handle a message that our user has written.
void handle_input_message(string raw_message, int socket, contacts* contacts, int* send_to)
{
	if (raw_message.len == 0)
		return;

	if (raw_message.val[0] != '/')
	{
		send_message(raw_message, socket, contacts, send_to);
		return;
	}

	raw_message.val += 1;
	raw_message.len -= 1;

	message message = parse_message(raw_message);

	if (string_iequal_c(message.command, "IAM"))
		(void)(handle_local_iam_command(message.data, contacts) || io_fwrite(stderr, S(iam_usage)));

	else if (string_iequal_c(message.command, "TO"))
		handle_to_command(message.data, socket, contacts, send_to);

	else if (string_iequal_c(message.command, "TO_BROADCAST"))
		handle_to_broadcast_command(message.data, socket, contacts, send_to);

	else if (string_iequal_c(message.command, "CONTACT"))
		(void)(handle_contact_command(message.data, contacts) || io_fwrite(stderr, S(contact_usage)));

	else if (string_iequal_c(message.command, "CONTACTS"))
		handle_local_contacts_command(contacts);

	else if (string_iequal_c(message.command, "QUERY_CONTACTS"))
		send_message(S("/contacts\n"), socket, contacts, send_to);

	else if (string_iequal_c(message.command, "RAW"))
		send_message(message.data, socket, contacts, send_to);

	else
		io_fwrite(stderr, S("Unknown command.\n"));
}

// Build an UDP package payload with 2 lines: /iam my_name and the message.
string build_package(char* buffer, int buffer_size, contact* myself, string message)
{
	if (!contact_name(myself).len)
	{
		io_fwrite(stderr, S("Select a name first (with /iam).\n"));
		return make_string(0, 0);
	}

	char* start = buffer;
	char* end = buffer + buffer_size - 1;
	if (buffer_append(&start, end, S("/iam ")) < 0) return make_string(0, 0);
	if (buffer_append(&start, end, contact_name(myself)) < 0) return make_string(0, 0);
	if (buffer_append(&start, end, S("\n")) < 0) return make_string(0, 0);
	buffer_append_truncate(&start, end, message);
	++end;
	buffer_append(&start, end, S("\n"));

	return make_string(buffer, start - buffer);
}

// Send the message to send_to-determined target.
void send_message(string message, int socket, contacts* contacts, int* send_to)
{
	char buffer[1024];
	string package = build_package(buffer, sizeof(buffer), &contacts->list[0], message);
	sockaddr_in target = get_target_address(contacts, send_to);

	int l = TEMP_FAILURE_RETRY(sendto(socket, package.val, package.len, 0, &target, sizeof(target)));
	if (l < 0)
		perror("sending message");
}

// A loop that listens for messages over the network and handles them.
void receive_messages(int s, contacts* contacts)
{
	while (1)
	{
		char buffer[1024];
		sockaddr_in sender;
		socklen_t sender_size = sizeof(sender);
		int len = TEMP_FAILURE_RETRY(recvfrom(s, buffer, sizeof(buffer), 0, (sockaddr*) &sender, &sender_size));
		if (len < 0)
		{
			perror("receiving message");
			if (errno != ECONNRESET && errno != ETIMEDOUT && errno != EIO)
				exit(1);
		}

		pthread_mutex_lock(&contacts->mtx);
		two_strings ts = {{}, {buffer, len}};
		while (ts.b.len)
		{
			ts = string_split(ts.b, '\n');
			handle_received_message(sender, s, ts.a, contacts);
		}
		pthread_mutex_unlock(&contacts->mtx);
	}
}

// A loop that listens for user's input and handles it.
void handle_input(int socket, contacts* contacts)
{
	int send_to = 0;
	while (1)
	{
		string line;
		io_inputline(&line);
		line.len--; // drop /n
		pthread_mutex_lock(&contacts->mtx);
		handle_input_message(line, socket, contacts, &send_to);
		pthread_mutex_unlock(&contacts->mtx);
		free((char*) line.val);
	}
}

typedef struct shared_data
{
	int socket;
	contacts* contacts;
} shared_data;

void* receiver_thread(void* arg)
{
	shared_data* data = (shared_data*) arg;
	receive_messages(data->socket, data->contacts);
	return 0;
}

void* input_thread(void* arg)
{
	shared_data* data = (shared_data*) arg;
	handle_input(data->socket, data->contacts);
	return 0;
}

int init_socket(int port, sockaddr_in* self_addr)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
	{
		perror("creating socket");
		return -1;
	}

	self_addr->sin_family = AF_INET;
	self_addr->sin_addr.s_addr = htonl(INADDR_ANY);
	self_addr->sin_port = htons(port);

	if (bind(s, (sockaddr*) self_addr, sizeof(*self_addr)) < 0)
	{
		perror("binding socket");
		return -1;
	}

	return s;
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		printf("Usage: %s [port]\n", argv[0]);
		return -1;
	}

	int port = atoi(argv[1]);

	sockaddr_in self_addr;
	int s = init_socket(port, &self_addr);

	contacts contacts;
	contacts_init(&contacts, self_addr);

	shared_data data;
	data.socket = s;
	data.contacts = &contacts;

	pthread_t rect;
	pthread_create(&rect, 0, receiver_thread, &data);

	pthread_t inputt;
	pthread_create(&inputt, 0, input_thread, &data);

	pthread_join(rect, 0);
	pthread_join(inputt, 0);

	contacts_destroy(&contacts);
	TEMP_FAILURE_RETRY(close(s));

	return 0;
}
