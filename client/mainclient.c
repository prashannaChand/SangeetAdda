#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>
#include <conio.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8081
#define CHUNK_SIZE 16384
#define NUM_BUFFERS 2

typedef struct {
    WAVEHDR header;
    BYTE buffer[CHUNK_SIZE];
} AudioBuffer;

void print_menu(char** songs, int count) {
    printf("Available songs:\n");
    for (int i = 0; i < count; i++) {
        printf("  %d. %s\n", i+1, songs[i]);
    }
    printf("Commands: p=Pause, r=Resume, s=Skip, q=Quit, number=Play song\n");
}

int is_status_message(char* buffer) {
    return strncmp(buffer, "STATUS:", 7) == 0;
}

int main() {
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server_addr;
    int bytes_received;

    // 1. Init Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed. Error Code: %d\n", WSAGetLastError());
        return 1;
    }

    // 2. Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Socket creation failed. Error Code: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // 3. Setup server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // 4. Connect to server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Connect failed. Error Code: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    printf("Connected to server %s:%d\n", SERVER_IP, SERVER_PORT);

    // Receive playlist
    char playlist_buf[1024] = {0};
    recv(sock, playlist_buf, sizeof(playlist_buf)-1, 0);
    char* songs[10];
    int song_count = 0;
    char* p = strstr(playlist_buf, "SONGS:");
    if (p) {
        p += 6;
        char* token = strtok(p, ",\n");
        while (token && song_count < 10) {
            songs[song_count++] = _strdup(token);
            token = strtok(NULL, ",\n");
        }
    }
    print_menu(songs, song_count);

    // Setup audio
    WAVEFORMATEX wf = {0};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = 44100;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (wf.nChannels * wf.wBitsPerSample) / 8;
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    HWAVEOUT hWaveOut;
    MMRESULT result = waveOutOpen(&hWaveOut, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        printf("waveOutOpen failed with error: %d\n", result);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    AudioBuffer audioBuffers[NUM_BUFFERS];
    for (int i = 0; i < NUM_BUFFERS; i++) {
        memset(&audioBuffers[i].header, 0, sizeof(WAVEHDR));
        audioBuffers[i].header.lpData = (LPSTR)audioBuffers[i].buffer;
        audioBuffers[i].header.dwBufferLength = CHUNK_SIZE;
        waveOutPrepareHeader(hWaveOut, &audioBuffers[i].header, sizeof(WAVEHDR));
    }

    int playing = 0;
    int currentBuffer = 0;
    char cmd[256];
    char status_msg[256] = "";

    while (1) {
        // Non-blocking check for status messages from server
        fd_set readfds;
        struct timeval tv = {0, 100000}; // 100ms
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        int activity = select(0, &readfds, NULL, NULL, &tv);

        if (activity > 0 && FD_ISSET(sock, &readfds)) {
            bytes_received = recv(sock, audioBuffers[currentBuffer].buffer, CHUNK_SIZE, 0);
            if (bytes_received <= 0) break;

            if (is_status_message((char*)audioBuffers[currentBuffer].buffer)) {
                audioBuffers[currentBuffer].buffer[bytes_received] = 0;
                strcpy(status_msg, audioBuffers[currentBuffer].buffer + 7);
                printf("\n%s\n", status_msg);

                if (strstr((char*)audioBuffers[currentBuffer].buffer, "PLAYING")) {
                    playing = 1;
                } else if (strstr((char*)audioBuffers[currentBuffer].buffer, "PAUSED") ||
                           strstr((char*)audioBuffers[currentBuffer].buffer, "STOPPED")) {
                    playing = 0;
                }
                continue;
            }

            if (playing) {
                audioBuffers[currentBuffer].header.dwBufferLength = bytes_received;
                while (audioBuffers[currentBuffer].header.dwFlags & WHDR_INQUEUE) Sleep(5);
                waveOutWrite(hWaveOut, &audioBuffers[currentBuffer].header, sizeof(WAVEHDR));
                currentBuffer = (currentBuffer + 1) % NUM_BUFFERS;
            }
        }

        // User input
        if (_kbhit()) {
            fgets(cmd, sizeof(cmd), stdin);

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
            else if (cmd[0] >= '1' && cmd[0] <= '9') {
                int idx = atoi(cmd) - 1;
                if (idx >= 0 && idx < song_count) {
                    char playcmd[256];
                    snprintf(playcmd, sizeof(playcmd), "PLAY %s\n", songs[idx]);
                    send(sock, playcmd, strlen(playcmd), 0);
                }
            }
        }
    }

cleanup:
    for (int i = 0; i < NUM_BUFFERS; i++)
        waveOutUnprepareHeader(hWaveOut, &audioBuffers[i].header, sizeof(WAVEHDR));
    waveOutClose(hWaveOut);
    closesocket(sock);
    WSACleanup();
    printf("Client closed.\n");
    return 0;
}
