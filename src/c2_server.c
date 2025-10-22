// src/c2_server.c - Multi-threaded C2
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 6666
#define MAX_CLIENTS 10

void *handle_client(void *arg) {
    int client_sock = *(int*)arg;
    free(arg);
    
    char buffer[4096];
    while (1) {
        int bytes = recv(client_sock, buffer, sizeof(buffer)-1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = 0;
        printf("[C2] %s", buffer);
        fflush(stdout);
    }
    close(client_sock);
    return NULL;
}

int main() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_sock, MAX_CLIENTS);
    
    printf("🚀 C2 Server listening on port %d (Ctrl+C to stop)\n", PORT);
    
    while (1) {
        int client_sock = accept(server_sock, NULL, NULL);
        if (client_sock < 0) continue;
        
        pthread_t thread;
        int *client_ptr = malloc(sizeof(int));
        *client_ptr = client_sock;
        
        pthread_create(&thread, NULL, handle_client, client_ptr);
        pthread_detach(thread);
    }
    return 0;
}
