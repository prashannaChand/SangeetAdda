#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>       // Windows sockets
#include <ws2tcpip.h>
#include <windows.h>        // for Sleep()

#pragma comment(lib, "ws2_32.lib") // Winsock library

#define PORT 8080
#define CHUNK_SIZE 4096

int main() {
    WSADATA wsa;
    SOCKET server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // 1. Initialize Winsock
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed. Error Code: %d\n", WSAGetLastError());
        return 1;
    }

    // 2. Create TCP socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Socket failed. Error Code: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // 3. Bind to port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        printf("Bind failed. Error Code: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    // 4. Listen
    if (listen(server_fd, 1) == SOCKET_ERROR) {
        printf("Listen failed. Error Code: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    printf("Server listening on port %d...\n", PORT);

    // 5. Accept client
    if ((client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen)) == INVALID_SOCKET) {
        printf("Accept failed. Error Code: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    printf("Client connected.\n");

    // 6. Open song1.wav (skip header)
    FILE* fp = fopen("playlist/song1.wav", "rb");
    if (!fp) {
        perror("Failed to open song1.wav");
        closesocket(client_fd);
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    fseek(fp, 44, SEEK_SET); // Skip 44-byte WAV header

    // 7. Loop: read chunk → send(chunk) → Sleep()
    char buffer[CHUNK_SIZE];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, fp)) > 0) {
        int sent = send(client_fd, buffer, (int)bytes_read, 0);
        if (sent == SOCKET_ERROR) {
            printf("Send failed. Error Code: %d\n", WSAGetLastError());
            break;
        }
        Sleep(25);  // Sleep 25 ms (instead of usleep)
    }

    // 8. Cleanup
    fclose(fp);
    closesocket(client_fd);
    closesocket(server_fd);
    WSACleanup();
    printf("Streaming finished. Server closed.\n");
    return 0;
}
