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

/* Puerto usado cuando el usuario no proporciona uno por linea de comandos. */
#define DEFAULT_SERVER_PORT 5000

/* Cantidad maxima de conexiones pendientes aceptadas por listen(). */
#define PENDING_CONNECTIONS 5

/*
 * Crea, configura y deja listo el socket principal del servidor.
 * Este socket no intercambia mensajes directamente; solo escucha clientes.
 */
static SocketDescriptor create_server_socket(int server_port)
{
    /* socket(): crea el punto inicial de comunicacion TCP del servidor. */
    SocketDescriptor server_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_address;
    int reuse_address_enabled = 1;

    if (server_socket_descriptor == INVALID_SOCKET_DESCRIPTOR) {
        exit_with_error("[TCP] No se pudo crear el socket del servidor");
    }

    /* setsockopt(): permite reutilizar el puerto durante pruebas sucesivas. */
    if (setsockopt(server_socket_descriptor,
                   SOL_SOCKET,
                   SO_REUSEADDR,
#ifdef _WIN32
                   (const char *)&reuse_address_enabled,
#else
                   &reuse_address_enabled,
#endif
                   sizeof(reuse_address_enabled)) == -1) {
        close_socket_safely(server_socket_descriptor);
        exit_with_error("[TCP] No se pudo configurar SO_REUSEADDR");
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(server_port);

    /* bind(): asigna direccion IP y puerto al socket del servidor. */
    if (bind(server_socket_descriptor, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        close_socket_safely(server_socket_descriptor);
        exit_with_error("[TCP] No se pudo asociar IP y puerto con bind");
    }

    /* listen(): coloca el socket en modo pasivo, esperando clientes. */
    if (listen(server_socket_descriptor, PENDING_CONNECTIONS) == -1) {
        close_socket_safely(server_socket_descriptor);
        exit_with_error("[TCP] No se pudo activar listen");
    }

    return server_socket_descriptor;
}

/*
 * Espera una conexion entrante.
 * accept() devuelve un nuevo descriptor dedicado a hablar con el cliente.
 */
static SocketDescriptor wait_for_client_connection(SocketDescriptor server_socket_descriptor)
{
    struct sockaddr_in client_address;
#ifdef _WIN32
    int client_address_size = sizeof(client_address);
#else
    socklen_t client_address_size = sizeof(client_address);
#endif

    /* accept(): acepta una solicitud de conexion y crea un socket dedicado. */
    SocketDescriptor connection_socket_descriptor = accept(server_socket_descriptor,
                                                           (struct sockaddr *)&client_address,
                                                           &client_address_size);

    if (connection_socket_descriptor == INVALID_SOCKET_DESCRIPTOR) {
        exit_with_error("[TCP] No se pudo aceptar la conexion del cliente");
    }

    printf("[TCP] Cliente conectado desde %s:%d\n",
           inet_ntoa(client_address.sin_addr),
           ntohs(client_address.sin_port));

    return connection_socket_descriptor;
}

/* Lee el puerto recibido por argumento o devuelve el puerto por defecto. */
static int read_port_argument(int argument_count, char *argument_values[])
{
    if (argument_count < 2) {
        return DEFAULT_SERVER_PORT;
    }

    return atoi(argument_values[1]);
}

/*
 * Atiende una conexion activa.
 * Al terminar el cliente actual, la funcion regresa y el servidor vuelve a accept().
 */
static void serve_connected_client(SocketDescriptor connection_socket_descriptor,
                                   volatile sig_atomic_t *server_running)
{
    pthread_t receiver_thread;
    pthread_t sender_thread;

    /* Bandera compartida por ambos hilos para controlar el ciclo full-duplex. */
    volatile sig_atomic_t connection_active = 1;
    MessageThreadContext thread_context;

    thread_context.socket_descriptor = connection_socket_descriptor;
    thread_context.connection_active = &connection_active;
    thread_context.server_running = server_running;
    thread_context.local_role = ENDPOINT_SERVER;

    if (pthread_create(&receiver_thread, NULL, receive_messages, &thread_context) != 0) {
        close_socket_safely(connection_socket_descriptor);
        exit_with_error("[TCP] No se pudo crear el hilo de recepcion");
    }

    if (pthread_create(&sender_thread, NULL, send_messages_thread, &thread_context) != 0) {
        connection_active = 0;
        shutdown_socket_safely(connection_socket_descriptor);
        pthread_join(receiver_thread, NULL);
        close_socket_safely(connection_socket_descriptor);
        exit_with_error("[TCP] No se pudo crear el hilo de envio");
    }

    while (connection_active) {
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    shutdown_socket_safely(connection_socket_descriptor);
    pthread_cancel(sender_thread);
    pthread_join(receiver_thread, NULL);
    pthread_join(sender_thread, NULL);
    close_socket_safely(connection_socket_descriptor);
}

int main(int argument_count, char *argument_values[])
{
    /* Variables con nombres descriptivos para documentar su responsabilidad. */
    int server_port = read_port_argument(argument_count, argument_values);
    SocketDescriptor server_socket_descriptor;
    SocketDescriptor connection_socket_descriptor;
    volatile sig_atomic_t server_running = 1;

    initialize_socket_library();
    server_socket_descriptor = create_server_socket(server_port);
    printf("=== Servidor TCP full-duplex ===\n");
    printf("[TCP] Servidor escuchando en el puerto %d.\n", server_port);
    printf("[TCP] Use FIN para terminar la comunicacion con el cliente actual.\n");
    printf("[TCP] Use APAGAR para avisar al cliente actual y cerrar el servidor.\n");
    printf("[TCP] El servidor seguira activo y aceptara otro cliente despues.\n");
    printf("[TCP] Modelo de trafico: un cliente activo; hasta %d conexiones en cola.\n",
           PENDING_CONNECTIONS);

    while (server_running) {
        connection_socket_descriptor = wait_for_client_connection(server_socket_descriptor);
        serve_connected_client(connection_socket_descriptor, &server_running);

        if (server_running) {
            printf("[TCP] Cliente desconectado. Esperando otro cliente...\n");
        }
    }

    close_socket_safely(server_socket_descriptor);
    cleanup_socket_library();

    printf("[TCP] Servidor finalizado correctamente.\n");
    return 0;
}
