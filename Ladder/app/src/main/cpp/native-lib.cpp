#include <jni.h>
#include <android/log.h>

#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>

const char* init(const char* server_address, int server_port);

void start(int fd);

void stop();

const char* stat();

extern "C"
JNIEXPORT jstring JNICALL
Java_io_github_ring00_ladder_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    const char* hello = "Hello from C++";
    return env->NewStringUTF(hello);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_io_github_ring00_ladder_LadderService_init(
        JNIEnv* env,
        jobject /* this */,
        jstring server_addr,
        jint server_port) {
    const char* str = init(env->GetStringUTFChars(server_addr, NULL), server_port);
    return env->NewStringUTF(str);
}

extern "C"
JNIEXPORT void JNICALL
Java_io_github_ring00_ladder_LadderService_start(
        JNIEnv* env,
        jobject /* this */,
        jint fd) {
    start(fd);
}

extern "C"
JNIEXPORT void JNICALL
Java_io_github_ring00_ladder_LadderService_stop(
        JNIEnv* env,
        jobject /* this */) {
    stop();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_io_github_ring00_ladder_MainActivity_getStatistics(
        JNIEnv* env,
        jobject /* this */) {
    const char* str = stat();
    return env->NewStringUTF(str);
}

#define LOG(...) __android_log_print(ANDROID_LOG_DEBUG, __FILE__, __VA_ARGS__)

struct Header {
    int length;
    char type;
};

struct Message {
    struct Header hdr;
    char data[4096];
};

enum {
    REGISTER = 100,
    APPROVE,
    REQUEST,
    RESPONSE,
    HEARTBEAT
};

int sockfd;
int tunfd;

bool alive;

time_t timestamp;

struct Statistics {
    int bytes;
    int packets;
    int flow;
    time_t time;
};

struct Statistics in, out;

const char* stat() {
    static char buffer[4096];
    sprintf(buffer, "%d %d %d %d %d %d %d %d", in.bytes, in.packets, in.flow,
            (int)(time(NULL) - in.time), out.bytes, out.packets, out.flow,
            (int)(time(NULL) - out.time));
    time(&in.time);
    time(&out.time);
    in.flow = 0;
    out.flow = 0;
    return buffer;
}

const char* init(const char* server_addr, int server_port) {
    LOG("const char* init(const char* server_addr, int server_port)\n");

    sockfd = socket(AF_INET6, SOCK_STREAM, 0);
    if (sockfd == -1) {
        LOG("Failed to create IPv6 socket: %s.\n", strerror(errno));
        return NULL;
    }

    struct sockaddr_in6 server = {.sin6_family = AF_INET6, .sin6_port = htons(server_port)};
    if (inet_pton(AF_INET6, server_addr, &server.sin6_addr) == -1) {
        LOG("Failed to set IPv6 address: %s.\n", strerror(errno));
        return NULL;
    }
    if (connect(sockfd, (struct sockaddr*)&server, sizeof(struct sockaddr_in6))) {
        LOG("Failed to connect to IPv6 address: %s.\n", strerror(errno));
        return NULL;
    }

    struct Message msg;
    msg.hdr.length = 0;
    msg.hdr.type = REGISTER;
    if (send(sockfd, &msg.hdr, sizeof(struct Header), 0) == -1) {
        LOG("Failed to send register request: %s.\n", strerror(errno));
        return NULL;
    }

    if (recv(sockfd, &msg, sizeof(struct Message), 0) == -1) {
        LOG("No approve response: %s.\n", strerror(errno));
        return NULL;
    }

    if (msg.hdr.type != APPROVE) {
        LOG("No approve response.\n");
        return NULL;
    }

    static char buffer[4096];

    sprintf(buffer, "%d %s", sockfd, msg.data);

    return buffer;
}

void* forward(void*) {
    struct Message msg;

    while (alive) {
        int length = read(tunfd, msg.data, 4096);
        if (length > 0) {
            msg.hdr.type = REQUEST;
            msg.hdr.length = length;
            if (send(sockfd, &msg, length + sizeof(struct Header), 0) == -1) {
                LOG("Failed to forward message: %s.\n", strerror(errno));
            }

            out.bytes += length + sizeof(struct Header);
            out.flow += length + sizeof(struct Header);
            out.packets++;
        }
    }
    return NULL;
}

void* receive(void*) {
    struct Message msg;

    time(&timestamp);

    while (alive) {
        if (recv(sockfd, &msg, sizeof(struct Header), 0) !=
            sizeof(struct Header)) {
            LOG("Failed to receive message header: %s.\n", strerror(errno));
        }

        int type = msg.hdr.type;
        if (type == RESPONSE) {
            int length = 0;
            while (length < msg.hdr.length) {
                length +=
                        recv(sockfd, msg.data + length, msg.hdr.length - length, 0);
            }
            write(tunfd, msg.data, length);

            in.bytes += length + sizeof(struct Header);
            in.flow += length + sizeof(struct Header);
            in.packets++;
        } else if (type == HEARTBEAT) {
            time(&timestamp);
        } else {
            LOG("Unknown packet type: %d\n", type);
        }
    }
    return NULL;
}

void* count(void*) {
    struct Header hdr = {.type = HEARTBEAT, .length = 0};
    int seconds = 0;
    while (alive) {
        time_t now = time(NULL);
        double diff = difftime(now, timestamp);
        if (diff > 60.0) {
            alive = false;
        } else if (seconds > 20) {
            seconds = 0;
            send(sockfd, &hdr, sizeof(struct Header), 0);
        }
        seconds++;
        sleep(1);
    }
    return NULL;
}

void start(int fd) {
    tunfd = fd;
    alive = true;

    pthread_t forwarder;
    if (pthread_create(&forwarder, NULL, &forward, NULL) != 0) {
        LOG("Failed to create thread: %s.\n", strerror(errno));
    }
    if (pthread_detach(forwarder) != 0) {
        LOG("Failed to detach thread: %s.\n", strerror(errno));
    }

    pthread_t receiver;
    if (pthread_create(&receiver, NULL, &receive, NULL) != 0) {
        LOG("Failed to create thread: %s.\n", strerror(errno));
    }
    if (pthread_detach(receiver) != 0) {
        LOG("Failed to detach thread: %s.\n", strerror(errno));
    }

    pthread_t counter;
    if (pthread_create(&counter, NULL, &count, NULL) != 0) {
        LOG("Failed to create thread: %s.\n", strerror(errno));
    }
    if (pthread_detach(counter) != 0) {
        LOG("Failed to detach thread: %s.\n", strerror(errno));
    }
}

void stop() {
    LOG("Stopping...\n");
    alive = false;
    sockfd = 0;
    tunfd = 0;
    memset(&in, 0, sizeof(struct Statistics));
    memset(&out, 0, sizeof(struct Statistics));
    time(&in.time);
    time(&out.time);
}
