#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")  // Link with winmm.lib for waveOut functions

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define CHUNK_SIZE 16384

// Audio settings
#define SAMPLE_RATE 44100
#define NUM_CHANNELS 2
#define BITS_PER_SAMPLE 16

// Number of audio buffers for double buffering
#define NUM_BUFFERS 2

typedef struct {
    WAVEHDR header;
    BYTE buffer[CHUNK_SIZE];
} AudioBuffer;

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

    // 5. Setup wave format
    WAVEFORMATEX wf = {0};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = NUM_CHANNELS;
    wf.nSamplesPerSec = SAMPLE_RATE;
    wf.wBitsPerSample = BITS_PER_SAMPLE;
    wf.nBlockAlign = (wf.nChannels * wf.wBitsPerSample) / 8;
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    // 6. Open audio device
    HWAVEOUT hWaveOut;
    MMRESULT result = waveOutOpen(&hWaveOut, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        printf("waveOutOpen failed with error: %d\n", result);
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // 7. Prepare buffers for double buffering
    AudioBuffer audioBuffers[NUM_BUFFERS];
    for (int i = 0; i < NUM_BUFFERS; i++) {
        memset(&audioBuffers[i].header, 0, sizeof(WAVEHDR));
        audioBuffers[i].header.lpData = (LPSTR)audioBuffers[i].buffer;
        audioBuffers[i].header.dwBufferLength = CHUNK_SIZE;

        result = waveOutPrepareHeader(hWaveOut, &audioBuffers[i].header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            printf("waveOutPrepareHeader failed with error: %d\n", result);
            waveOutClose(hWaveOut);
            closesocket(sock);
            WSACleanup();
            return 1;
        }
    }

    printf("Audio playback started. Receiving and playing...\n");

    int currentBuffer = 0;

    // 8. Receive loop
    while ((bytes_received = recv(sock, audioBuffers[currentBuffer].buffer, CHUNK_SIZE, 0)) > 0) {
        audioBuffers[currentBuffer].header.dwBufferLength = bytes_received;

        // Wait until buffer is done playing
        while (audioBuffers[currentBuffer].header.dwFlags & WHDR_INQUEUE) {
            Sleep(5);
        }

        // Send buffer to audio device
        result = waveOutWrite(hWaveOut, &audioBuffers[currentBuffer].header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR) {
            printf("waveOutWrite failed with error: %d\n", result);
            break;
        }

        currentBuffer = (currentBuffer + 1) % NUM_BUFFERS;
    }

    if (bytes_received == 0) {
        printf("Server closed connection.\n");
    } else if (bytes_received == SOCKET_ERROR) {
        printf("recv() failed: %d\n", WSAGetLastError());
    }

    // 9. Wait for queued audio to finish playing
    while (waveOutGetPosition(hWaveOut, NULL, 0) == MMSYSERR_NOERROR) {
        BOOL buffersPlaying = FALSE;
        for (int i = 0; i < NUM_BUFFERS; i++) {
            if (audioBuffers[i].header.dwFlags & WHDR_INQUEUE) {
                buffersPlaying = TRUE;
                break;
            }
        }
        if (!buffersPlaying) break;
        Sleep(50);
    }

    // 10. Cleanup
    for (int i = 0; i < NUM_BUFFERS; i++) {
        waveOutUnprepareHeader(hWaveOut, &audioBuffers[i].header, sizeof(WAVEHDR));
    }

    waveOutClose(hWaveOut);
    closesocket(sock);
    WSACleanup();

    printf("Client closed.\n");
    return 0;
}
