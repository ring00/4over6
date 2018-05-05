#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <linux/if_tun.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <net/if.h>

#define DEFAULT_SERVER_PORT 10002
#define CLIENT_QUEUE_LENGTH 10

pthread_mutex_t mutex;

// Message types
/*
	100: IP request
	101: IP response
	102: Network request
	103: Network response
	104: Keepalive
*/

#define IP_REQUEST 100
#define IP_RESPONSE 101
#define NETWORK_REQUEST 102
#define NETWORK_RESPONSE 103
#define KEEPALIVE 104

struct msg_header_type {
	int length;
	char type;
};

struct msg_type {
	struct msg_header_type header;
	char data[4096];
};

// User info type
/*
	fd: -1 means not used
*/

#define IP_TO_UINT(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

#define N_USERS 100
#define IP_POOL_START IP_TO_UINT(10, 233, 233, 100)

struct user_info {
	int fd;
	int count;
	int secs;
	struct in_addr v4addr;
	struct in6_addr v6addr;
} user_info_table[N_USERS];

int listen_sock_fd;
int tun_fd;

char DNS_string[100];

void init_server(int argc, char **argv) {
	// Create socket
	listen_sock_fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (listen_sock_fd == -1) {
		perror("socket()");
		exit(EXIT_FAILURE);
	}
	
	// Bind address
	int server_port = DEFAULT_SERVER_PORT;
	struct sockaddr_in6 server_addr;
	server_addr.sin6_family = AF_INET6;
	server_addr.sin6_addr = in6addr_any;
	server_addr.sin6_port = htons(server_port);
	int ret = bind(listen_sock_fd, (struct sockaddr *) &server_addr, sizeof(server_addr));
	if (ret == -1) {
		perror("bind()");
		close(listen_sock_fd);
		exit(EXIT_FAILURE);
	}
	
	// Listen
	ret = listen(listen_sock_fd, CLIENT_QUEUE_LENGTH);
	if (ret == -1) {
		perror("listen()");
		close(listen_sock_fd);
		exit(EXIT_FAILURE);
	}
}

int tun_alloc(char *dev) {
	int fd = open("/dev/net/tun", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Error creating TUN\n");
		return fd;
	}
	
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags |= IFF_TUN | IFF_NO_PI;
	
	if (dev && *dev != 0) {
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	}
	
	/*int err = ioctl(tun_fd, TUNSETIFF, (void *) &ifr);
	if (err < 0) {
		fprintf(stderr, "Error setting tunnel name\n");
		close(fd);
		exit(EXIT_FAILURE);
	}*/
	
	if (fcntl(tun_fd, F_SETFL, O_NONBLOCK) < 0) {
		fprintf(stderr, "Error setting nonblock\n");
		close(fd);
		exit(EXIT_FAILURE);
	}
	
	if (dev) {
		strcpy(dev, ifr.ifr_name);
	}
	
	return fd;
}

void init_tun() {
	char tun_name[IFNAMSIZ];
	tun_name[0] = 0;
	tun_fd = tun_alloc(tun_name);
}

void init_user_info_table() {
	for (int i = 0; i < N_USERS; i++) {
		user_info_table[i].v4addr.s_addr = htonl(IP_POOL_START + i);
		user_info_table[i].fd = -1;
	}
}

void *client_thread_func(void *args) {
	struct user_info *user = (struct user_info *) args;
	pthread_mutex_lock(&mutex);
	while (1) {
		pthread_mutex_unlock(&mutex);
		struct msg_type msg;
		int user_fd = user->fd;
		int ret = -1;
		if (user_fd != -1) {
			ret = recv(user_fd, &msg.header, sizeof(msg.header), 0);
		}
		pthread_mutex_lock(&mutex);
		printf("ret = %d\n", ret);
		if (ret != sizeof(msg.header)) {
			break;
		}
		if (user->fd == -1) {
			break;
		}
		int length = ntohl(msg.header.length);
		char type = msg.header.type;
		if (type == NETWORK_REQUEST) {
			ret = recv(user->fd, msg.data, length, 0);
			if (ret != length) {
				break;
			}
			write(tun_fd, msg.data, length);
		} else if (type == IP_REQUEST) {
			char buf[100];
			char ip_str[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &(user->v4addr), ip_str, INET_ADDRSTRLEN);
			sprintf(buf, "%s 0.0.0.0%s", ip_str, DNS_string);
			int length = strlen(buf);
			struct msg_type msg;
			msg.header.length = htonl(length);
			msg.header.type = IP_RESPONSE;
			memcpy(msg.data, buf, length);
			send(user->fd, &msg, sizeof(msg.header) + length, 0);
		} else if (type == KEEPALIVE) {
			user->secs = 0;
		}
	}
	close(user->fd);
	user->fd = -1;
	pthread_mutex_unlock(&mutex);
	return NULL;
}

void *keepalive_thread_func(void *args) {
	pthread_mutex_lock(&mutex);
	while (1) {
		pthread_mutex_unlock(&mutex);
		sleep(1);
		pthread_mutex_lock(&mutex);
		for (int i = 0; i < N_USERS; i++) {
			struct user_info *user = user_info_table + i;
			if (user->fd == -1) continue;
			if (--user->count <= 0) {
				struct msg_type msg;
				msg.header.length = 0;
				msg.header.type = KEEPALIVE;
				send(user->fd, &msg.header, sizeof(msg.header), 0);
				user->count = 20;
			}
			if (++user->secs > 60) {
				close(user->fd);
				user->fd = -1;
			}
		}
	}
	return NULL;
}

// ip is in network byte order
struct user_info *find_user_by_ip(uint32_t ip) {
	for (int i = 0; i < N_USERS; i++) {
		if (user_info_table[i].fd != -1 && *(uint32_t *) &user_info_table[i].v4addr == ip) {
			return user_info_table + i;
		}
	}
	return NULL;
}

void *read_tun_thread_func(void *args) {
	pthread_mutex_lock(&mutex);
	while (1) {
		pthread_mutex_unlock(&mutex);
		struct msg_type msg;
		int length = read(tun_fd, msg.data, 4096);
		pthread_mutex_lock(&mutex);
		if (length > 0) {
			msg.header.length = htonl(length);
			msg.header.type = NETWORK_RESPONSE;
			struct user_info *user = find_user_by_ip(*((uint32_t *)(msg.data + 16)));
			if (!user) continue;
			int fd = user->fd;
			if (fd == -1) continue;
			send(fd, &msg, sizeof(msg.header) + length, 0);
		}
	}
	return NULL;
}

void init_DNS() {
	system("cat /etc/resolv.conf | grep -i nameserver | cut -c 12-30 > DNS.txt");
	FILE *f = fopen("DNS.txt", "r");
	char DNS_string_tmp[100];
	for (int i = 0; i < 3; i++) {
		char buf[100];
		if (fscanf(f, "%s\n", buf) == 1) {
			sprintf(DNS_string_tmp, "%s %s", DNS_string, buf);
			sprintf(DNS_string, "%s", DNS_string_tmp);
		}
	}
	fclose(f);
}

int main(int argc, char **argv) {
	init_server(argc, argv);
	init_tun();
	init_user_info_table();
	init_DNS();
	pthread_t keepalive_thread;
	pthread_t read_tun_thread;
	int ret = pthread_create(&keepalive_thread, NULL, keepalive_thread_func, NULL);
	if (ret == -1) {
		perror("pthread_create()");
		close(listen_sock_fd);
		return EXIT_FAILURE;
	}
	ret = pthread_create(&read_tun_thread, NULL, read_tun_thread_func, NULL);
	if (ret == -1) {
		perror("pthread_create()");
		close(listen_sock_fd);
		return EXIT_FAILURE;
	}
	
	printf("Starting server main loop\n");
	
	while (1) {
		struct sockaddr_in6 client_addr;
		int client_addr_len = sizeof(client_addr);
		int client_sock_fd = accept(listen_sock_fd, (struct sockaddr *) &client_addr, &client_addr_len);
		if (client_sock_fd == -1) {
			perror("accept()");
			close(listen_sock_fd);
			exit(EXIT_FAILURE);
		}
		
		char str_addr[INET6_ADDRSTRLEN];
		inet_ntop(AF_INET6, &(client_addr.sin6_addr), str_addr, sizeof(str_addr));
		printf("New connection from %s %d\n", str_addr, ntohs(client_addr.sin6_port));
		
		pthread_mutex_lock(&mutex);
		struct user_info *user = NULL;
		
		for (int i = 0; i < N_USERS; i++) {
			if (user_info_table[i].fd == -1) {
				user_info_table[i].fd = client_sock_fd;
				user = user_info_table + i;
				user->secs = 0;
				user->count = 20;
				break;
			}
		}
		
		pthread_mutex_unlock(&mutex);
		
		if (!user) {
			printf("No available IP\n");
			close(client_sock_fd);
			continue;
		}
		
		pthread_t client_thread;
		int ret = pthread_create(&client_thread, NULL, client_thread_func, user);
		if (ret == -1) {
			perror("pthread_create()");
			close(client_sock_fd);
			continue;
		}
		
		printf("Client thread created\n");
	}
	
	return 0;
}
