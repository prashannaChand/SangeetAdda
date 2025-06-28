#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8081
#define CHUNK_SIZE 16384
#define CMD_BUF 256
#define SAMPLE_RATE 44100
#define NUM_CHANNELS 2
#define BITS_PER_SAMPLE 16
#define MAX_CLIENTS 16

const char* playlist[] = {"song1.wav", "song3.wav"};
const int playlist_size = 2;

// Global playback state
SOCKET client_fds[MAX_CLIENTS];
int num_clients = 0;
CRITICAL_SECTION cs; // For thread safety

int current_song = 0, paused = 0, streaming = 0;
FILE* fp = NULL;

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

unsigned __stdcall client_handler(void* arg) {
    SOCKET client_fd = (SOCKET)arg;
    send_playlist(client_fd);

    char cmd[CMD_BUF];
    while (1) {
        int r = recv(client_fd, cmd, CMD_BUF-1, 0);
        if (r <= 0) break;
        cmd[r] = 0;

        EnterCriticalSection(&cs);
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
            if (found) {
                if (fp) fclose(fp);
                char path[256];
                snprintf(path, sizeof(path), "playlist/%s", playlist[current_song]);
                fp = fopen(path, "rb");
                if (fp) {
                    fseek(fp, 44, SEEK_SET);
                    paused = 0;
                    streaming = 1;
                    printf("Now playing: %s\n", playlist[current_song]);
                    char status_msg[256];
                    snprintf(status_msg, sizeof(status_msg), "STATUS:PLAYING %s\n", playlist[current_song]);
                    for (int i = 0; i < num_clients; i++) {
                        send(client_fds[i], status_msg, strlen(status_msg), 0);
                    }
                }
            }
        } else if (strncmp(cmd, "PAUSE", 5) == 0) {
            paused = 1;
            printf("Paused\n");
            char status_msg[256];
            snprintf(status_msg, sizeof(status_msg), "STATUS:PAUSED\n");
            for (int i = 0; i < num_clients; i++) {
                send(client_fds[i], status_msg, strlen(status_msg), 0);
            }
        } else if (strncmp(cmd, "RESUME", 6) == 0) {
            paused = 0;
            streaming = 1;
            printf("Resumed\n");
            char status_msg[256];
            snprintf(status_msg, sizeof(status_msg), "STATUS:PLAYING %s\n", playlist[current_song]);
            for (int i = 0; i < num_clients; i++) {
                send(client_fds[i], status_msg, strlen(status_msg), 0);
            }
        } else if (strncmp(cmd, "SKIP", 4) == 0) {
            current_song = (current_song + 1) % playlist_size;
            if (fp) fclose(fp);
            char path[256];
            snprintf(path, sizeof(path), "playlist/%s", playlist[current_song]);
            fp = fopen(path, "rb");
            if (fp) {
                fseek(fp, 44, SEEK_SET);
                paused = 0;
                streaming = 1;
                printf("Skipped to: %s\n", playlist[current_song]);
                char status_msg[256];
                snprintf(status_msg, sizeof(status_msg), "STATUS:PLAYING %s\n", playlist[current_song]);
                for (int i = 0; i < num_clients; i++) {
                    send(client_fds[i], status_msg, strlen(status_msg), 0);
                }
            }
        }
        LeaveCriticalSection(&cs);
    }

    // Remove client from list
    EnterCriticalSection(&cs);
    for (int i = 0; i < num_clients; i++) {
        if (client_fds[i] == client_fd) {
            for (int j = i; j < num_clients - 1; j++)
                client_fds[j] = client_fds[j + 1];
            num_clients--;
            break;
        }
    }
    LeaveCriticalSection(&cs);

    closesocket(client_fd);
    printf("Client disconnected\n");
    _endthreadex(0);
    return 0;
}

unsigned __stdcall streaming_thread(void* arg) {
    while (1) {
        EnterCriticalSection(&cs);
        if (fp && streaming && !paused && num_clients > 0) {
            char buffer[CHUNK_SIZE];
            size_t bytes_read = fread(buffer, 1, CHUNK_SIZE, fp);
            if (bytes_read > 0) {
                for (int i = 0; i < num_clients; i++) {
                    send(client_fds[i], buffer, (int)bytes_read, 0);
                }
                double bytes_per_sec = SAMPLE_RATE * NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
                double chunk_duration_ms = ((double)bytes_read / bytes_per_sec) * 1000.0;
                if (chunk_duration_ms < 1) chunk_duration_ms = 1;
                LeaveCriticalSection(&cs);
                Sleep((DWORD)chunk_duration_ms);
                continue;
            } else {
                fclose(fp);
                fp = NULL;
                streaming = 0;
                printf("Song finished.\n");
            }
        }
        LeaveCriticalSection(&cs);
        Sleep(10);
    }
    return 0;
}

int main() {
    WSADATA wsa;
    SOCKET server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    InitializeCriticalSection(&cs);

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        printf("Bind failed\n");
        return 1;
    }
    if (listen(server_fd, SOMAXCONN) == SOCKET_ERROR) {
        printf("Listen failed\n");
        return 1;
    }
    printf("Server listening on port %d...\n", PORT);

    // Streaming thread
    _beginthreadex(NULL, 0, streaming_thread, NULL, 0, NULL);

    // Accept clients
    while (1) {
        printf("Waiting for client...\n");
        client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (client_fd == INVALID_SOCKET) {
            printf("Accept failed: %d\n", WSAGetLastError());
            continue;
        }
        printf("Client connected.\n");
        EnterCriticalSection(&cs);
        if (num_clients < MAX_CLIENTS) {
            client_fds[num_clients++] = client_fd;
            uintptr_t th = _beginthreadex(NULL, 0, client_handler, (void*)client_fd, 0, NULL);
            CloseHandle((HANDLE)th);
        } else {
            printf("Too many clients!\n");
            closesocket(client_fd);
        }
        LeaveCriticalSection(&cs);
    }

    closesocket(server_fd);
    WSACleanup();
    DeleteCriticalSection(&cs);
    return 0;
}
