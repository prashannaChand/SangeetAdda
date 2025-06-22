#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define CHUNK_SIZE 16384
#define MAX_SONGS 10
#define CMD_BUF 256
#define SAMPLE_RATE 44100
#define NUM_CHANNELS 2
#define BITS_PER_SAMPLE 16

const char* playlist[] = {"song1.wav", "song3.wav"};
const int playlist_size = 2;

int send_playlist(SOCKET client_fd) {
    char buf[1024] = {0};
    strcat(buf, "SONGS:");
    for (int i = 0; i < playlist_size; i++) {
        strcat(buf, playlist[i]);
        if (i < playlist_size - 1) strcat(buf, ",");
    }
    strcat(buf, "\n");
    send(client_fd, buf, strlen(buf), 0);
    return 0;
}

int main() {
    WSADATA wsa;
    SOCKET server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return 1;
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) return 1;

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) return 1;
    if (listen(server_fd, 1) == SOCKET_ERROR) return 1;
    printf("Server listening on port %d...\n", PORT);

    if ((client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen)) == INVALID_SOCKET) return 1;
    printf("Client connected.\n");

    send_playlist(client_fd);

    char cmd[CMD_BUF];
    int current_song = 0;
    int paused = 0;
    int streaming = 0;
    FILE* fp = NULL;

    while (1) {
        // Check for command (non-blocking)
        fd_set readfds;
        struct timeval tv = {0, 10000}; // 10ms
        FD_ZERO(&readfds);
        FD_SET(client_fd, &readfds);
        int activity = select(0, &readfds, NULL, NULL, &tv);

        if (activity > 0 && FD_ISSET(client_fd, &readfds)) {
            memset(cmd, 0, CMD_BUF);
            int r = recv(client_fd, cmd, CMD_BUF-1, 0);
            if (r <= 0) break;

            if (strncmp(cmd, "PLAY ", 5) == 0) {
                char* song = cmd + 5;
                song[strcspn(song, "\r\n")] = 0;
                int found = 0;
                for (int i = 0; i < playlist_size; i++) {
                    if (strcmp(song, playlist[i]) == 0) {
                        current_song = i;
                        found = 1;
                        break;
                    }
                }
                if (!found) continue;
                if (fp) fclose(fp);
                char path[256];
                snprintf(path, sizeof(path), "playlist/%s", playlist[current_song]);
                fp = fopen(path, "rb");
                if (!fp) continue;
                fseek(fp, 44, SEEK_SET);
                paused = 0;
                streaming = 1;
            } else if (strncmp(cmd, "PAUSE", 5) == 0) {
                paused = 1;
            } else if (strncmp(cmd, "RESUME", 6) == 0) {
                paused = 0;
                streaming = 1;
            } else if (strncmp(cmd, "SKIP", 4) == 0) {
                current_song = (current_song + 1) % playlist_size;
                if (fp) fclose(fp);
                char path[256];
                snprintf(path, sizeof(path), "playlist/%s", playlist[current_song]);
                fp = fopen(path, "rb");
                if (!fp) continue;
                fseek(fp, 44, SEEK_SET);
                paused = 0;
                streaming = 1;
            }
        }

        // Streaming loop
        if (fp && streaming && !paused) {
            char buffer[CHUNK_SIZE];
            size_t bytes_read = fread(buffer, 1, CHUNK_SIZE, fp);
            if (bytes_read > 0) {
                send(client_fd, buffer, (int)bytes_read, 0);
                double bytes_per_sec = SAMPLE_RATE * NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
                double chunk_duration_ms = ((double)bytes_read / bytes_per_sec) * 1000.0;
                if (chunk_duration_ms < 1) chunk_duration_ms = 1;
                Sleep((DWORD)chunk_duration_ms);
            } else {
                fclose(fp);
                fp = NULL;
                streaming = 0;
            }
        } else {
            Sleep(10);
        }
    }

    if (fp) fclose(fp);
    closesocket(client_fd);
    closesocket(server_fd);
    WSACleanup();
    printf("Server closed.\n");
    return 0;
}
