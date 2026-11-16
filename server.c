
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#define PORT 9000
#define BUFFER_SIZE 4096
#define FILE_DIR "/mnt/files"

static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

static int send_all(int fd, const void *data, size_t len) {
    const char *p = data;

    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int recv_line(int fd, char *buf, size_t cap) {
    size_t i = 0;

    while (i + 1 < cap) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);

        if (n <= 0) return -1;
        if (c == '\n') break;
        if (c == '\r') continue;

        buf[i++] = c;
    }

    buf[i] = '\0';
    return 0;
}

static int safe_name(const char *name) {
    if (!name[0] || strlen(name) > 100) return 0;

    if (strstr(name, "..") ||
        strchr(name, '/') ||
        strchr(name, '\\')) {
        return 0;
    }

    return 1;
}

static void *handle_client(void *arg) {
    int fd = *(int *)arg;
    free(arg);

    char line[256];
    char name[128];
    char path[256];
    char buffer[BUFFER_SIZE];

    // Demonstration authentication
    if (send_all(fd, "Username:\n", 10) < 0 ||
        recv_line(fd, line, sizeof(line)) < 0)
        goto done;

    if (strcmp(line, "student") != 0) {
        send_all(fd, "AUTH_FAILED\n", 12);
        goto done;
    }

    if (send_all(fd, "Password:\n", 10) < 0 ||
        recv_line(fd, line, sizeof(line)) < 0)
        goto done;

    if (strcmp(line, "change-me") != 0) {
        send_all(fd, "AUTH_FAILED\n", 12);
        goto done;
    }

    if (send_all(fd, "AUTH_OK\n", 8) < 0)
        goto done;

    while (recv_line(fd, line, sizeof(line)) == 0) {

        if (strcmp(line, "QUIT") == 0)
            break;

        // LIST files
        if (strcmp(line, "LIST") == 0) {
            DIR *dir = opendir(FILE_DIR);

            if (!dir) {
                send_all(fd, "ERROR\n", 6);
                continue;
            }

            struct dirent *entry;

            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.')
                    continue;

                snprintf(buffer, sizeof(buffer),
                         "%s\n", entry->d_name);

                if (send_all(fd, buffer, strlen(buffer)) < 0)
                    break;
            }

            closedir(dir);
            send_all(fd, "END\n", 4);
            continue;
        }

        // UPLOAD file
        if (strncmp(line, "UPLOAD ", 7) == 0 &&
            sscanf(line + 7, "%127s", name) == 1 &&
            safe_name(name)) {

            unsigned long long size;

            if (recv_line(fd, line, sizeof(line)) < 0)
                break;

            if (sscanf(line, "%llu", &size) != 1 ||
                size > 100000000ULL) {
                send_all(fd, "ERROR\n", 6);
                continue;
            }

            snprintf(path, sizeof(path),
                     "%s/%s", FILE_DIR, name);

            pthread_mutex_lock(&file_mutex);

            FILE *f = fopen(path, "wb");

            if (!f) {
                pthread_mutex_unlock(&file_mutex);
                send_all(fd, "ERROR\n", 6);
                continue;
            }

            unsigned long long remaining = size;
            int failed = 0;

            while (remaining > 0) {
                size_t want = remaining > sizeof(buffer)
                              ? sizeof(buffer)
                              : (size_t)remaining;

                ssize_t n = recv(fd, buffer, want, 0);

                if (n <= 0) {
                    failed = 1;
                    break;
                }

                if (fwrite(buffer, 1, (size_t)n, f) != (size_t)n) {
                    failed = 1;
                    break;
                }

                remaining -= (unsigned long long)n;
            }

            fclose(f);

            if (failed)
                remove(path);

            pthread_mutex_unlock(&file_mutex);

            if (failed) {
                send_all(fd, "ERROR\n", 6);
                break;
            }

            send_all(fd, "OK\n", 3);
            printf("Uploaded: %s\n", name);
            continue;
        }

        // DOWNLOAD file
        if (strncmp(line, "DOWNLOAD ", 9) == 0 &&
            sscanf(line + 9, "%127s", name) == 1 &&
            safe_name(name)) {

            snprintf(path, sizeof(path),
                     "%s/%s", FILE_DIR, name);

            pthread_mutex_lock(&file_mutex);

            FILE *f = fopen(path, "rb");

            if (!f) {
                pthread_mutex_unlock(&file_mutex);
                send_all(fd, "NOT_FOUND\n", 10);
                continue;
            }

            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            rewind(f);

            if (size < 0) {
                fclose(f);
                pthread_mutex_unlock(&file_mutex);
                send_all(fd, "ERROR\n", 6);
                continue;
            }

            snprintf(line, sizeof(line), "%ld\n", size);

            if (send_all(fd, line, strlen(line)) < 0) {
                fclose(f);
                pthread_mutex_unlock(&file_mutex);
                break;
            }

            size_t n;
            int failed = 0;

            while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
                if (send_all(fd, buffer, n) < 0) {
                    failed = 1;
                    break;
                }
            }

            fclose(f);
            pthread_mutex_unlock(&file_mutex);

            if (failed)
                break;

            printf("Downloaded: %s\n", name);
            continue;
        }

        send_all(fd, "INVALID_COMMAND\n", 16);
    }

done:
    close(fd);
    return NULL;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    if (mkdir(FILE_DIR, 0775) < 0 && errno != EEXIST) {
        perror("mkdir");
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_fd, (struct sockaddr *)&addr,
             sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 20) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("File server listening on port %d\n", PORT);
    printf("Files directory: %s\n", FILE_DIR);
    fflush(stdout);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &len);

        if (client_fd < 0) {
            if (errno == EINTR)
                continue;

            perror("accept");
            continue;
        }

        int *arg = malloc(sizeof(*arg));

        if (!arg) {
            close(client_fd);
            continue;
        }

        *arg = client_fd;

        pthread_t thread;

        if (pthread_create(&thread, NULL,
                           handle_client, arg) != 0) {
            close(client_fd);
            free(arg);
            continue;
        }

        pthread_detach(thread);
    }

    close(server_fd);
    return 0;
}
