#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>
#include <conio.h>
#include <stdint.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8081
#define CHUNK_SIZE 16384
#define NUM_BUFFERS 6
#define MAX_SONGS 1000

typedef struct {
    WAVEHDR header;
    BYTE buffer[CHUNK_SIZE];
} AudioBuffer;

void print_menu(char** songs, int song_count, const char* current_song, const char* status, int show_playlist) {
    if (show_playlist) {
        printf("\nAvailable songs:\n");
        for (int i = 0; i < song_count; i++) {
            printf("    %d. %s%s\n", i + 1, songs[i], strcmp(songs[i], current_song) == 0 ? " [PLAYING]" : "");
        }
    }
    printf("Current status: %s%s%s\n", status, current_song[0] ? " " : "", current_song);
    printf("Commands: p=Pause, r=Resume, s=Skip, q=Quit, number=Play song\n\n");
}

int is_status_message(const char* buffer, int len) {
    return len >= 7 && strncmp(buffer, "STATUS:", 7) == 0;
}

void flush_buffers(HWAVEOUT hWaveOut, AudioBuffer* audioBuffers) {
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (audioBuffers[i].header.dwFlags & WHDR_PREPARED) {
            waveOutReset(hWaveOut);
            waveOutUnprepareHeader(hWaveOut, &audioBuffers[i].header, sizeof(WAVEHDR));
            waveOutPrepareHeader(hWaveOut, &audioBuffers[i].header, sizeof(WAVEHDR));
        }
    }
}

int main() {
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server_addr;
    HWAVEOUT hWaveOut = NULL;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed. Error Code: %d\n", WSAGetLastError());
        return 1;
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Socket creation failed. Error Code: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Connect failed. Error Code: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    printf("Connected to server %s:%d\n", SERVER_IP, SERVER_PORT);

    uint32_t len_net;
    int received = 0;
    while (received < 4) {
        int r = recv(sock, ((char*)&len_net) + received, 4 - received, 0);
        if (r <= 0) {
            printf("Failed to receive playlist length\n");
            goto cleanup;
        }
        received += r;
    }

    uint32_t playlist_len = ntohl(len_net);
    if (playlist_len > 8191) {
        printf("Playlist too large (%u bytes)\n", playlist_len);
        goto cleanup;
    }

    char playlist_buf[8192] = {0};
    received = 0;
    while (received < (int)playlist_len) {
        int r = recv(sock, playlist_buf + received, playlist_len - received, 0);
        if (r <= 0) {
            printf("Failed to receive full playlist data\n");
            goto cleanup;
        }
        received += r;
    }
    playlist_buf[playlist_len] = '\0';

    char** songs = (char**)malloc(MAX_SONGS * sizeof(char*));
    if (!songs) {
        printf("Memory allocation failed for song list\n");
        goto cleanup;
    }
    int song_count = 0;
    char* p = strstr(playlist_buf, "SONGS:");
    if (p) {
        p += 6;
        char* token = strtok(p, ",\n");
        while (token && song_count < MAX_SONGS) {
            songs[song_count] = _strdup(token);
            if (!songs[song_count]) {
                printf("Memory allocation failed for song name\n");
                for (int i = 0; i < song_count; i++) free(songs[i]);
                free(songs);
                goto cleanup;
            }
            song_count++;
            token = strtok(NULL, ",\n");
        }
        if (token) {
            printf("Warning: Playlist truncated at %d songs\n", MAX_SONGS);
        }
    }

    char status_buf[512] = {0};
    char current_song[256] = {0};
    char current_status[32] = "STOPPED";
    received = 0;
    while (received < 4) {
        int r = recv(sock, ((char*)&len_net) + received, 4 - received, 0);
        if (r <= 0) {
            printf("Failed to receive initial status length\n");
            goto cleanup_songs;
        }
        received += r;
    }
    uint32_t status_len = ntohl(len_net);
    if (status_len > sizeof(status_buf) - 1) {
        printf("Status message too large\n");
        goto cleanup_songs;
    }
    received = 0;
    while (received < (int)status_len) {
        int r = recv(sock, status_buf + received, status_len - received, 0);
        if (r <= 0) {
            printf("Failed to receive initial status\n");
            goto cleanup_songs;
        }
        received += r;
    }
    status_buf[status_len] = '\0';

    if (strncmp(status_buf, "STATUS:", 7) == 0) {
        char* status_content = status_buf + 7;
        if (strncmp(status_content, "PLAYING ", 8) == 0) {
            strncpy(current_song, status_content + 8, sizeof(current_song) - 1);
            strncpy(current_status, "PLAYING", sizeof(current_status) - 1);
        } else if (strncmp(status_content, "PAUSED ", 7) == 0) {
            strncpy(current_song, status_content + 7, sizeof(current_song) - 1);
            strncpy(current_status, "PAUSED", sizeof(current_status) - 1);
        } else if (strncmp(status_content, "STOPPED", 7) == 0) {
            strncpy(current_status, "STOPPED", sizeof(current_status) - 1);
            current_song[0] = '\0';
        }
    }

    WAVEFORMATEX wf = {0};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = 44100;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (wf.nChannels * wf.wBitsPerSample) / 8;
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    MMRESULT result = waveOutOpen(&hWaveOut, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        printf("waveOutOpen failed with error: %d\n", result);
        goto cleanup_songs;
    }

    AudioBuffer audioBuffers[NUM_BUFFERS];
    for (int i = 0; i < NUM_BUFFERS; i++) {
        memset(&audioBuffers[i].header, 0, sizeof(WAVEHDR));
        audioBuffers[i].header.lpData = (LPSTR)audioBuffers[i].buffer;
        audioBuffers[i].header.dwBufferLength = CHUNK_SIZE;
        waveOutPrepareHeader(hWaveOut, &audioBuffers[i].header, sizeof(WAVEHDR));
    }

    int playing = (strcmp(current_status, "PLAYING") == 0);
    int paused = (strcmp(current_status, "PAUSED") == 0);
    int currentBuffer = 0;
    char cmd[256];
    int playlist_shown = 1;

    print_menu(songs, song_count, current_song, current_status, playlist_shown);

    while (1) {
        fd_set readfds;
        struct timeval tv = {0, 10};
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        int activity = select(0, &readfds, NULL, NULL, &tv);

        if (activity > 0 && FD_ISSET(sock, &readfds)) {
            char recv_buffer[CHUNK_SIZE];
            int bytes = recv(sock, recv_buffer, CHUNK_SIZE, 0);
            if (bytes <= 0) break;

            if (is_status_message(recv_buffer, bytes)) {
                recv_buffer[bytes] = '\0';
                printf("\n%s\n", recv_buffer + 7);
                char* status_content = recv_buffer + 7;

                if (strncmp(status_content, "PLAYING ", 8) == 0) {
                    strncpy(current_song, status_content + 8, sizeof(current_song) - 1);
                    strncpy(current_status, "PLAYING", sizeof(current_status) - 1);
                    playing = 1;
                    paused = 0;
                    flush_buffers(hWaveOut, audioBuffers);
                } else if (strncmp(status_content, "PAUSED ", 7) == 0) {
                    strncpy(current_song, status_content + 7, sizeof(current_song) - 1);
                    strncpy(current_status, "PAUSED", sizeof(current_status) - 1);
                    playing = 0;
                    paused = 1;
                } else if (strncmp(status_content, "RESUMED ", 8) == 0) {
                    strncpy(current_song, status_content + 8, sizeof(current_song) - 1);
                    strncpy(current_status, "PLAYING", sizeof(current_status) - 1);
                    playing = 1;
                    paused = 0;
                } else if (strncmp(status_content, "STOPPED", 7) == 0 || strncmp(status_content, "SKIPPED", 7) == 0) {
                    strncpy(current_status, "STOPPED", sizeof(current_status) - 1);
                    current_song[0] = '\0';
                    playing = 0;
                    paused = 0;
                    flush_buffers(hWaveOut, audioBuffers);
                } else if (strncmp(status_content, "ERROR ", 6) == 0) {
                    printf("Server error: %s\n", status_content + 6);
                } else if (strncmp(status_content, "UNKNOWN_COMMAND", 15) == 0) {
                    printf("Unknown command sent to server\n");
                }
                print_menu(songs, song_count, current_song, current_status, 0);
                continue;
            }

            if (!playing || paused) {
                continue;
            }

            int wait_count = 0;
            while (audioBuffers[currentBuffer].header.dwFlags & WHDR_INQUEUE) {
                Sleep(5);
                wait_count++;
                if (wait_count > 10) {
                    waveOutReset(hWaveOut);
                    break;
                }
            }

            memcpy(audioBuffers[currentBuffer].buffer, recv_buffer, bytes);
            audioBuffers[currentBuffer].header.dwBufferLength = bytes;
            MMRESULT res = waveOutWrite(hWaveOut, &audioBuffers[currentBuffer].header, sizeof(WAVEHDR));
            if (res != MMSYSERR_NOERROR) {
                printf("waveOutWrite failed: %d\n", res);
            }

            currentBuffer = (currentBuffer + 1) % NUM_BUFFERS;
        }

        if (_kbhit()) {
            fgets(cmd, sizeof(cmd), stdin);
            cmd[strcspn(cmd, "\n")] = '\0';

            if (cmd[0] == 'q') break;
            else if (cmd[0] == 'p') {
                send(sock, "PAUSE\n", 6, 0);
            }
            else if (cmd[0] == 'r') {
                send(sock, "RESUME\n", 7, 0);
            }
            else if (cmd[0] == 's') {
                send(sock, "SKIP\n", 5, 0);
            }
            else if (cmd[0] >= '0' && cmd[0] <= '9') {
                int idx = atoi(cmd) - 1;
                if (idx >= 0 && idx < song_count) {
                    char playcmd[256];
                    snprintf(playcmd, sizeof(playcmd), "PLAY %s\n", songs[idx]);
                    send(sock, playcmd, strlen(playcmd), 0);
                    flush_buffers(hWaveOut, audioBuffers);
                    playing = 0;
                    paused = 0;
                } else {
                    printf("Invalid song number\n");
                }
            }
        }
    }

cleanup_songs:
    for (int i = 0; i < song_count; i++) {
        free(songs[i]);
    }
    free(songs);

cleanup:
    if (hWaveOut) {
        for (int i = 0; i < NUM_BUFFERS; i++) {
            if (audioBuffers[i].header.dwFlags & WHDR_PREPARED) {
                waveOutUnprepareHeader(hWaveOut, &audioBuffers[i].header, sizeof(WAVEHDR));
            }
        }
        waveOutClose(hWaveOut);
    }
    closesocket(sock);
    WSACleanup();

    printf("Client closed.\n");
    return 0;
}