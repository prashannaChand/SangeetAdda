#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <io.h>
#include <stdint.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8081
#define MAX_CLIENTS 16
#define MAX_NAME_LEN 256
#define CHUNK_SIZE 16384
#define WAV_HEADER_SIZE 44

typedef struct {
    SOCKET client_fd;
    int active;
} ClientSlot;

typedef struct {
    FILE* wav_file;
    int playing;
    int paused;
    int stop_streaming;
    CRITICAL_SECTION lock;
} PlaybackState;

ClientSlot clients[MAX_CLIENTS];
CRITICAL_SECTION cs;
PlaybackState playback_state;
char current_song[MAX_NAME_LEN];
char last_chunk_buffer[CHUNK_SIZE];
size_t last_chunk_size = 0;

int send_all(SOCKET sock, const char* buf, int len) {
    int total_sent = 0;
    while (total_sent < len) {
        int sent = send(sock, buf + total_sent, len - total_sent, 0);
        if (sent <= 0) return -1;
        total_sent += sent;
    }
    return total_sent;
}

int list_playlist(char* out_buf, int buf_size) {
    struct _finddata_t file;
    intptr_t hFile;
    char search_path[256];
    snprintf(search_path, sizeof(search_path), "playlist\\*.wav");

    out_buf[0] = '\0';
    strcat(out_buf, "SONGS:");

    if ((hFile = _findfirst(search_path, &file)) != -1L) {
        do {
            strcat(out_buf, file.name);
            strcat(out_buf, ",");
        } while (_findnext(hFile, &file) == 0);
        _findclose(hFile);
    }

    size_t len = strlen(out_buf);
    if (len > 1 && out_buf[len - 1] == ',') {
        out_buf[len - 1] = '\n';
    } else {
        strcat(out_buf, "\n");
    }

    return len;
}

int send_wav_header(SOCKET client_fd, const char* song_name) {
    char path[512];
    snprintf(path, sizeof(path), "playlist\\%s", song_name);
    FILE* file = fopen(path, "rb");
    if (!file) {
        printf("send_wav_header: Failed to open %s\n", path);
        return -1;
    }

    char header[WAV_HEADER_SIZE];
    size_t read_size = fread(header, 1, sizeof(header), file);
    fclose(file);
    if (read_size != WAV_HEADER_SIZE) {
        printf("send_wav_header: Failed to read header from %s\n", path);
        return -1;
    }

    if (send_all(client_fd, header, sizeof(header)) < 0) {
        printf("send_wav_header: Failed to send header\n");
        return -1;
    }

    return 0;
}

void broadcast_control_message(const char* msg) {
    uint32_t len = htonl((uint32_t)strlen(msg));
    EnterCriticalSection(&cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            if (send_all(clients[i].client_fd, (char*)&len, sizeof(len)) < 0 ||
                send_all(clients[i].client_fd, msg, (int)strlen(msg)) < 0) {
                printf("broadcast: Client disconnected\n");
                closesocket(clients[i].client_fd);
                clients[i].active = 0;
            }
        }
    }
    LeaveCriticalSection(&cs);
}

unsigned __stdcall global_streaming_thread(void* arg) {
    char buffer[CHUNK_SIZE];

    while (1) {
        int should_stream = 0;
        EnterCriticalSection(&playback_state.lock);
        should_stream = playback_state.playing && !playback_state.paused &&
                        !playback_state.stop_streaming && playback_state.wav_file;
        LeaveCriticalSection(&playback_state.lock);

        if (!should_stream) {
            Sleep(20);
            continue;
        }

        size_t read_bytes;
        EnterCriticalSection(&playback_state.lock);
        read_bytes = fread(buffer, 1, CHUNK_SIZE, playback_state.wav_file);
        if (read_bytes == 0) {
            fclose(playback_state.wav_file);
            playback_state.wav_file = NULL;
            playback_state.playing = 0;
            playback_state.stop_streaming = 0;
            strncpy(current_song, "", sizeof(current_song));
            LeaveCriticalSection(&playback_state.lock);

            broadcast_control_message("STATUS:STOPPED\n");
            continue;
        }
        LeaveCriticalSection(&playback_state.lock);

        EnterCriticalSection(&playback_state.lock);
        memcpy(last_chunk_buffer, buffer, read_bytes);
        last_chunk_size = read_bytes;
        LeaveCriticalSection(&playback_state.lock);

        EnterCriticalSection(&cs);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                size_t total_sent = 0;
                while (total_sent < read_bytes) {
                    int sent = send(clients[i].client_fd, buffer + total_sent,
                                    (int)(read_bytes - total_sent), 0);
                    if (sent <= 0) {
                        printf("Client disconnected while streaming\n");
                        closesocket(clients[i].client_fd);
                        clients[i].active = 0;
                        break;
                    }
                    total_sent += sent;
                }
            }
        }
        LeaveCriticalSection(&cs);

        Sleep(10);
    }
    return 0;
}

unsigned __stdcall client_handler(void* data) {
    SOCKET client_fd = (SOCKET)data;
    char buffer[512];

    printf("Client connected: %llu\n", (unsigned long long)client_fd);

    char playlist_buf[8192];
    list_playlist(playlist_buf, sizeof(playlist_buf));
    uint32_t playlist_len = htonl((uint32_t)strlen(playlist_buf));
    if (send_all(client_fd, (char*)&playlist_len, sizeof(playlist_len)) < 0 ||
        send_all(client_fd, playlist_buf, (int)strlen(playlist_buf)) < 0) {
        closesocket(client_fd);
        return 0;
    }

    EnterCriticalSection(&playback_state.lock);
    int playing = playback_state.playing;
    int paused = playback_state.paused;
    char current_song_copy[MAX_NAME_LEN];
    strncpy(current_song_copy, current_song, MAX_NAME_LEN);
    LeaveCriticalSection(&playback_state.lock);

    char status_msg[512];
    uint32_t status_len;
    if (playing && current_song_copy[0] != '\0') {
        if (paused) {
            snprintf(status_msg, sizeof(status_msg), "STATUS:PAUSED %s\n", current_song_copy);
        } else {
            snprintf(status_msg, sizeof(status_msg), "STATUS:PLAYING %s\n", current_song_copy);
        }
    } else {
        snprintf(status_msg, sizeof(status_msg), "STATUS:STOPPED\n");
    }
    status_len = htonl((uint32_t)strlen(status_msg));
    if (send_all(client_fd, (char*)&status_len, sizeof(status_len)) < 0 ||
        send_all(client_fd, status_msg, (int)strlen(status_msg)) < 0) {
        closesocket(client_fd);
        return 0;
    }

    if (playing && current_song_copy[0] != '\0' && !paused) {
        if (send_wav_header(client_fd, current_song_copy) < 0) {
            closesocket(client_fd);
            return 0;
        }

        EnterCriticalSection(&playback_state.lock);
        if (last_chunk_size > 0) {
            size_t total_sent = 0;
            while (total_sent < last_chunk_size) {
                int sent = send(client_fd, last_chunk_buffer + total_sent,
                                (int)(last_chunk_size - total_sent), 0);
                if (sent <= 0) {
                    closesocket(client_fd);
                    LeaveCriticalSection(&playback_state.lock);
                    return 0;
                }
                total_sent += sent;
            }
        }
        LeaveCriticalSection(&playback_state.lock);
    }

    while (1) {
        int recv_len = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (recv_len <= 0) break;
        buffer[recv_len] = '\0';

        printf("Received from client %llu: %s", (unsigned long long)client_fd, buffer);

        if (strncmp(buffer, "PLAY ", 5) == 0) {
            char* song = buffer + 5;
            char* nl = strchr(song, '\n');
            if (nl) *nl = '\0';

            char path[512];
            snprintf(path, sizeof(path), "playlist\\%s", song);

            FILE* file = fopen(path, "rb");
            if (!file) {
                char err_msg[512];
                snprintf(err_msg, sizeof(err_msg), "STATUS:ERROR opening %s\n", song);
                uint32_t err_len = htonl((uint32_t)strlen(err_msg));
                send_all(client_fd, (char*)&err_len, sizeof(err_len));
                send_all(client_fd, err_msg, (int)strlen(err_msg));
                continue;
            }

            fseek(file, WAV_HEADER_SIZE, SEEK_SET);

            EnterCriticalSection(&playback_state.lock);
            if (playback_state.wav_file) fclose(playback_state.wav_file);
            playback_state.wav_file = file;
            playback_state.playing = 1;
            playback_state.paused = 0;
            playback_state.stop_streaming = 0;
            strncpy(current_song, song, sizeof(current_song));
            last_chunk_size = 0; // Reset last chunk for new song
            LeaveCriticalSection(&playback_state.lock);

            char msg[512];
            snprintf(msg, sizeof(msg), "STATUS:PLAYING %s\n", song);
            broadcast_control_message(msg);

            printf("Broadcasted: %s", msg);
        } else if (strncmp(buffer, "PAUSE", 5) == 0) {
            EnterCriticalSection(&playback_state.lock);
            if (playback_state.playing && !playback_state.paused) {
                playback_state.paused = 1;
                char msg[512];
                snprintf(msg, sizeof(msg), "STATUS:PAUSED %s\n", current_song);
                LeaveCriticalSection(&playback_state.lock);
                broadcast_control_message(msg);
                printf("Broadcasted: %s", msg);
            } else {
                LeaveCriticalSection(&playback_state.lock);
            }
        } else if (strncmp(buffer, "RESUME", 6) == 0) {
            EnterCriticalSection(&playback_state.lock);
            if (playback_state.playing && playback_state.paused) {
                playback_state.paused = 0;
                char msg[512];
                snprintf(msg, sizeof(msg), "STATUS:RESUMED %s\n", current_song);
                LeaveCriticalSection(&playback_state.lock);
                broadcast_control_message(msg);
                printf("Broadcasted: %s", msg);
            } else {
                LeaveCriticalSection(&playback_state.lock);
            }
        } else if (strncmp(buffer, "SKIP", 4) == 0) {
            EnterCriticalSection(&playback_state.lock);
            playback_state.playing = 0;
            playback_state.paused = 0;
            playback_state.stop_streaming = 1;
            if (playback_state.wav_file) {
                fclose(playback_state.wav_file);
                playback_state.wav_file = NULL;
            }
            strncpy(current_song, "", sizeof(current_song));
            last_chunk_size = 0; // Reset last chunk
            LeaveCriticalSection(&playback_state.lock);

            broadcast_control_message("STATUS:SKIPPED\n");

            printf("Broadcasted: STATUS:SKIPPED\n");
        } else {
            char err_msg[512];
            snprintf(err_msg, sizeof(err_msg), "STATUS:UNKNOWN_COMMAND\n");
            uint32_t err_len = htonl((uint32_t)strlen(err_msg));
            send_all(client_fd, (char*)&err_len, sizeof(err_len));
            send_all(client_fd, err_msg, (int)strlen(err_msg));
            printf("Sent to client %llu: STATUS:UNKNOWN_COMMAND\n", (unsigned long long)client_fd);
        }
    }

    printf("Client disconnected: %llu\n", (unsigned long long)client_fd);
    closesocket(client_fd);

    EnterCriticalSection(&cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].client_fd == client_fd) {
            clients[i].active = 0;
            break;
        }
    }
    LeaveCriticalSection(&cs);

    return 0;
}

int main() {
    WSADATA wsa;
    SOCKET server_fd;
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    InitializeCriticalSection(&cs);
    InitializeCriticalSection(&playback_state.lock);

    playback_state.wav_file = NULL;
    playback_state.playing = 0;
    playback_state.paused = 0;
    playback_state.stop_streaming = 0;
    last_chunk_size = 0;
    current_song[0] = '\0';

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        WSACleanup();
        return 1;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("Bind failed\n");
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) == SOCKET_ERROR) {
        printf("Listen failed\n");
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    printf("Server listening on port %d...\n", PORT);

    _beginthreadex(NULL, 0, global_streaming_thread, NULL, 0, NULL);

    while (1) {
        SOCKET client_fd = accept(server_fd, (struct sockaddr*)&addr, &addrlen);
        if (client_fd == INVALID_SOCKET) continue;

        EnterCriticalSection(&cs);
        int added = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) {
                clients[i].client_fd = client_fd;
                clients[i].active = 1;
                _beginthreadex(NULL, 0, client_handler, (void*)client_fd, 0, NULL);
                added = 1;
                break;
            }
        }
        if (!added) {
            printf("Too many clients, rejected.\n");
            closesocket(client_fd);
        }
        LeaveCriticalSection(&cs);
    }

    DeleteCriticalSection(&cs);
    DeleteCriticalSection(&playback_state.lock);
    closesocket(server_fd);
    WSACleanup();
    return 0;
}