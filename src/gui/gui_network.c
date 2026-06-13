#include "gui_network.h"
#include "gui_protocol.h"

#include <ws2tcpip.h>

#include <string.h>

int gui_message_is_ascii(const char *message)
{
    const unsigned char *current_character = (const unsigned char *)message;

    while (*current_character != '\0') {
        if (*current_character > 127) {
            return 0;
        }

        current_character++;
    }

    return 1;
}

int gui_message_is_control(const char *message)
{
    return strcmp(message, SERVER_READY_NOTICE) == 0 ||
           strcmp(message, CLIENT_DISCONNECT_NOTICE) == 0 ||
           strcmp(message, SERVER_DISCONNECT_NOTICE) == 0 ||
           strcmp(message, SERVER_SHUTDOWN_NOTICE) == 0;
}

void gui_close_socket(SOCKET socket_to_close)
{
    if (socket_to_close != INVALID_SOCKET) {
        shutdown(socket_to_close, SD_BOTH);
        closesocket(socket_to_close);
    }
}

int gui_create_listener_socket(int port, SOCKET *listener_socket)
{
    SOCKET socket_descriptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in server_address;
    int exclusive_address_enabled = 1;

    if (socket_descriptor == INVALID_SOCKET) {
        return 0;
    }

    setsockopt(socket_descriptor,
               SOL_SOCKET,
               SO_EXCLUSIVEADDRUSE,
               (const char *)&exclusive_address_enabled,
               sizeof(exclusive_address_enabled));

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons((unsigned short)port);

    if (bind(socket_descriptor, (struct sockaddr *)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        closesocket(socket_descriptor);
        return 0;
    }

    if (listen(socket_descriptor, PENDING_CONNECTIONS) == SOCKET_ERROR) {
        closesocket(socket_descriptor);
        return 0;
    }

    *listener_socket = socket_descriptor;
    return 1;
}

int gui_create_client_socket(const char *host, int port, SOCKET *client_socket)
{
    SOCKET socket_descriptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in server_address;

    if (socket_descriptor == INVALID_SOCKET) {
        return 0;
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons((unsigned short)port);

    if (inet_pton(AF_INET, host, &server_address.sin_addr) <= 0) {
        closesocket(socket_descriptor);
        return 0;
    }

    if (connect(socket_descriptor, (struct sockaddr *)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        closesocket(socket_descriptor);
        return 0;
    }

    *client_socket = socket_descriptor;
    return 1;
}
