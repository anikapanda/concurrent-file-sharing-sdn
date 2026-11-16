
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 9000
#define BUFFER_SIZE 4096

int read_line(int sock, char *buf, int size) {
    int i = 0;
    char c;

    while (i < size - 1) {
        int n = recv(sock, &c, 1, 0);
        if (n <= 0) return -1;
        if (c == '\n') break;
        buf[i++] = c;
    }

    buf[i] = '\0';
    return 0;
}

int send_all(int sock, const void *data, size_t len) {
    const char *p = data;

    while (len > 0) {
        ssize_t n = send(sock, p, len, 0);
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s SERVER_IP\n", argv[0]);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server = {0};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    if (inet_pton(AF_INET, argv[1], &server.sin_addr) != 1) {
        printf("Invalid server IP\n");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server,
                sizeof(server)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    char line[BUFFER_SIZE];
    char input[256];

    // Login
    if (read_line(sock, line, sizeof(line)) < 0) goto done;
    printf("%s ", line);
    fflush(stdout);

    if (!fgets(input, sizeof(input), stdin)) goto done;
    if (send_all(sock, input, strlen(input)) < 0) goto done;

    if (read_line(sock, line, sizeof(line)) < 0) goto done;

    if (strcmp(line, "AUTH_FAILED") == 0) {
        printf("Authentication failed!\n");
        goto done;
    }

    printf("%s ", line);
    fflush(stdout);

    if (!fgets(input, sizeof(input), stdin)) goto done;
    if (send_all(sock, input, strlen(input)) < 0) goto done;

    if (read_line(sock, line, sizeof(line)) < 0) goto done;

    if (strcmp(line, "AUTH_OK") != 0) {
        printf("Authentication failed!\n");
        goto done;
    }

    printf("Login successful!\n");

    while (1) {
        printf("\n1. List files\n");
        printf("2. Upload file\n");
        printf("3. Download file\n");
        printf("4. Exit\n");
        printf("Enter choice: ");

        if (!fgets(input, sizeof(input), stdin)) break;

        int choice = atoi(input);

        if (choice == 1) {
            if (send_all(sock, "LIST\n", 5) < 0) break;

            printf("\nFiles on server:\n");

            while (read_line(sock, line, sizeof(line)) == 0) {
                if (strcmp(line, "END") == 0) break;
                printf("%s\n", line);
            }
        }

        else if (choice == 2) {
            char filename[128];
            printf("Enter filename to upload: ");

            if (!fgets(filename, sizeof(filename), stdin)) break;
            filename[strcspn(filename, "\n")] = '\0';

            FILE *f = fopen(filename, "rb");

            if (!f) {
                perror("File not found");
                continue;
            }

            if (fseek(f, 0, SEEK_END) != 0) {
                fclose(f);
                continue;
            }

            long size = ftell(f);
            rewind(f);

            if (size < 0 || size > 100000000L) {
                printf("Invalid file size\n");
                fclose(f);
                continue;
            }

            char request[256];
            snprintf(request, sizeof(request),
                     "UPLOAD %s\n%ld\n", filename, size);

            if (send_all(sock, request, strlen(request)) < 0) {
                fclose(f);
                break;
            }

            char buffer[BUFFER_SIZE];
            size_t n;
            int failed = 0;

            while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
                if (send_all(sock, buffer, n) < 0) {
                    failed = 1;
                    break;
                }
            }

            fclose(f);

            if (failed) break;

            if (read_line(sock, line, sizeof(line)) < 0) break;
            printf("Server: %s\n", line);
        }

        else if (choice == 3) {
            char filename[128];
            printf("Enter filename to download: ");

            if (!fgets(filename, sizeof(filename), stdin)) break;
            filename[strcspn(filename, "\n")] = '\0';

            char request[256];
            snprintf(request, sizeof(request),
                     "DOWNLOAD %s\n", filename);

            if (send_all(sock, request, strlen(request)) < 0) break;

            if (read_line(sock, line, sizeof(line)) < 0) break;

            if (strcmp(line, "NOT_FOUND") == 0 ||
                strcmp(line, "ERROR") == 0) {
                printf("Server: %s\n", line);
                continue;
            }

            char *end;
            long size = strtol(line, &end, 10);

            if (*end != '\0' || size < 0 || size > 100000000L) {
                printf("Invalid file size received\n");
                break;
            }

            FILE *f = fopen(filename, "wb");

            if (!f) {
                perror("Cannot create file");
                break;
            }

            char buffer[BUFFER_SIZE];
            long remaining = size;
            int failed = 0;

            while (remaining > 0) {
                size_t want = remaining > BUFFER_SIZE
                              ? BUFFER_SIZE : (size_t)remaining;

                ssize_t n = recv(sock, buffer, want, 0);

                if (n <= 0 ||
                    fwrite(buffer, 1, (size_t)n, f) != (size_t)n) {
                    failed = 1;
                    break;
                }

                remaining -= n;
            }

            fclose(f);

            if (failed) {
                remove(filename);
                printf("Download failed\n");
                break;
            }

            printf("Downloaded %s successfully!\n", filename);
        }

        else if (choice == 4) {
            send_all(sock, "QUIT\n", 5);
            break;
        }

        else {
            printf("Invalid choice\n");
        }
    }

done:
    close(sock);
    return 0;
}
