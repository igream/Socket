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
        shutdown(socket_to_close, SD_BOTH); /* shutdown(): notifica cierre de envio y recepcion en el socket TCP. */
        closesocket(socket_to_close); /* closesocket(): libera el descriptor Winsock. */
    }
}

int gui_create_listener_socket(int port, SOCKET *listener_socket)
{
    SOCKET socket_descriptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); /* socket(): crea un descriptor TCP IPv4. */
    struct sockaddr_in server_address;
    int exclusive_address_enabled = 1;

    if (socket_descriptor == INVALID_SOCKET) {
        return 0;
    }

    setsockopt(socket_descriptor, /* setsockopt(): exige uso exclusivo del puerto en la GUI. */
               SOL_SOCKET,
               SO_EXCLUSIVEADDRUSE,
               (const char *)&exclusive_address_enabled,
               sizeof(exclusive_address_enabled));

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons((unsigned short)port);

    if (bind(socket_descriptor, (struct sockaddr *)&server_address, sizeof(server_address)) == SOCKET_ERROR) { /* bind(): asocia IP local y puerto al socket servidor. */
        closesocket(socket_descriptor); /* closesocket(): libera el socket si bind falla. */
        return 0;
    }

    if (listen(socket_descriptor, PENDING_CONNECTIONS) == SOCKET_ERROR) { /* listen(): deja el socket servidor esperando clientes. */
        closesocket(socket_descriptor); /* closesocket(): libera el socket si listen falla. */
        return 0;
    }

    *listener_socket = socket_descriptor;
    return 1;
}

int gui_create_client_socket(const char *host, int port, SOCKET *client_socket)
{
    SOCKET socket_descriptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); /* socket(): crea el descriptor TCP del cliente GUI. */
    struct sockaddr_in server_address;

    if (socket_descriptor == INVALID_SOCKET) {
        return 0;
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons((unsigned short)port);

    if (inet_pton(AF_INET, host, &server_address.sin_addr) <= 0) { /* inet_pton(): convierte la IP textual a direccion binaria. */
        closesocket(socket_descriptor); /* closesocket(): libera el socket si la IP no es valida. */
        return 0;
    }

    if (connect(socket_descriptor, (struct sockaddr *)&server_address, sizeof(server_address)) == SOCKET_ERROR) { /* connect(): solicita conexion TCP al servidor registrado. */
        closesocket(socket_descriptor); /* closesocket(): libera el socket si connect falla. */
        return 0;
    }

    *client_socket = socket_descriptor;
    return 1;
}
