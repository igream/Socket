#include "messaging.h"

/* Directivas necesarias para sockets IPv4, TCP, hilos POSIX y E/S estandar. */
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Valores usados cuando el usuario no pasa IP o puerto por argumentos. */
#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 5000

/*
 * Crea el socket TCP del cliente y solicita la conexion con el servidor.
 * Devuelve un descriptor listo para enviar y recibir mensajes.
 */
static SocketDescriptor create_client_socket(const char *server_ip, int server_port)
{
    /* socket(): crea el punto de comunicacion TCP del cliente. */
    SocketDescriptor client_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_address;

    if (client_socket_descriptor == INVALID_SOCKET_DESCRIPTOR) {
        exit_with_error("[TCP] No se pudo crear el socket del cliente");
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &server_address.sin_addr) <= 0) {
        close_socket_safely(client_socket_descriptor);
        exit_with_error("[TCP] La direccion IP del servidor no es valida");
    }

    /* connect(): solicita establecer conexion con el servidor TCP. */
    if (connect(client_socket_descriptor, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        close_socket_safely(client_socket_descriptor);
        exit_with_error("[TCP] No se pudo conectar con el servidor");
    }

    return client_socket_descriptor;
}

/* Lee la IP del servidor o usa localhost como valor predeterminado. */
static const char *read_ip_argument(int argument_count, char *argument_values[])
{
    if (argument_count < 2) {
        return DEFAULT_SERVER_IP;
    }

    return argument_values[1];
}

/* Lee el puerto del servidor o usa 5000 como valor predeterminado. */
static int read_port_argument(int argument_count, char *argument_values[])
{
    if (argument_count < 3) {
        return DEFAULT_SERVER_PORT;
    }

    return atoi(argument_values[2]);
}

int main(int argument_count, char *argument_values[])
{
    /* Variables con nombres descriptivos para documentar su responsabilidad. */
    const char *server_ip = read_ip_argument(argument_count, argument_values);
    int server_port = read_port_argument(argument_count, argument_values);
    SocketDescriptor client_socket_descriptor;
    pthread_t receiver_thread;
    pthread_t sender_thread;

    /* Bandera compartida por ambos hilos para controlar el ciclo full-duplex. */
    volatile sig_atomic_t connection_active = 1;
    MessageThreadContext thread_context;

    printf("=== Cliente TCP full-duplex ===\n");
    printf("[TCP] Conectando con %s:%d...\n", server_ip, server_port);

    initialize_socket_library();
    client_socket_descriptor = create_client_socket(server_ip, server_port);

    printf("[TCP] Conexion establecida. Use FIN para terminar la comunicacion.\n");

    thread_context.socket_descriptor = client_socket_descriptor;
    thread_context.connection_active = &connection_active;
    thread_context.server_running = NULL;
    thread_context.local_role = ENDPOINT_CLIENT;

    if (pthread_create(&receiver_thread, NULL, receive_messages, &thread_context) != 0) {
        close_socket_safely(client_socket_descriptor);
        exit_with_error("[TCP] No se pudo crear el hilo de recepcion");
    }

    if (pthread_create(&sender_thread, NULL, send_messages_thread, &thread_context) != 0) {
        connection_active = 0;
        shutdown_socket_safely(client_socket_descriptor);
        pthread_join(receiver_thread, NULL);
        close_socket_safely(client_socket_descriptor);
        cleanup_socket_library();
        exit_with_error("[TCP] No se pudo crear el hilo de envio");
    }

    while (connection_active) {
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    shutdown_socket_safely(client_socket_descriptor);
    pthread_cancel(sender_thread);
    pthread_join(receiver_thread, NULL);
    pthread_join(sender_thread, NULL);
    close_socket_safely(client_socket_descriptor);
    cleanup_socket_library();

    printf("[TCP] Cliente finalizado correctamente.\n");
    return 0;
}
