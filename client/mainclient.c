#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include "portaudio.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "portaudio_x86.lib")  // Update if 64-bit

#define SERVER_IP "127.0.0.1"   // or LAN IP of server
#define SERVER_PORT 8080
#define CHUNK_SIZE 4096

// PortAudio settings
#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 512
#define NUM_CHANNELS 2
#define SAMPLE_FORMAT paInt16  // 16-bit signed PCM

// PortAudio stream callback (optional - for advanced use)
// For now, we use simple blocking write

int main() {
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server_addr;
    char buffer[CHUNK_SIZE];
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

    // 5. Init PortAudio
    PaError err;
    err = Pa_Initialize();
    if (err != paNoError) {
        printf("PortAudio error: %s\n", Pa_GetErrorText(err));
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // 6. Open audio stream
    PaStream* stream;
    err = Pa_OpenDefaultStream(
        &stream,
        0,               // no input channels
        NUM_CHANNELS,    // stereo output
        SAMPLE_FORMAT,
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        NULL, NULL);     // no callback, use blocking write
    if (err != paNoError) {
        printf("PortAudio error: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    Pa_StartStream(stream);
    printf("Audio stream started. Playing audio...\n");

    // 7. Receive loop → write to audio stream
    while ((bytes_received = recv(sock, buffer, CHUNK_SIZE, 0)) > 0) {
        // Blocking write
        err = Pa_WriteStream(stream, buffer, bytes_received / (NUM_CHANNELS * 2)); // 2 bytes/sample
        if (err != paNoError) {
            printf("PortAudio write error: %s\n", Pa_GetErrorText(err));
            break;
        }
    }

    printf("Stream ended or server disconnected.\n");

    // 8. Cleanup
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    closesocket(sock);
    WSACleanup();

    printf("Client closed.\n");
    return 0;
}
