#include "messaging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

void initialize_socket_library(void)
{
#ifdef _WIN32
    WSADATA socket_library_data;

    if (WSAStartup(MAKEWORD(2, 2), &socket_library_data) != 0) {
        fprintf(stderr, "[TCP] No se pudo inicializar Winsock.\n");
        exit(EXIT_FAILURE);
    }
#endif
}

void cleanup_socket_library(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/*
 * Elimina '\n' o '\r' para comparar y enviar mensajes limpios.
 * Es util porque fgets() conserva el salto de linea escrito por el usuario.
 */
void remove_line_break(char *message)
{
    size_t message_length = strcspn(message, "\r\n");
    message[message_length] = '\0';
}

/*
 * Recorre byte por byte el mensaje y valida que todos los caracteres
 * pertenezcan al rango ASCII basico: 0 a 127.
 */
int message_is_ascii(const char *message)
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

/* Compara el mensaje con la palabra reservada FIN. */
int message_is_end_signal(const char *message)
{
    return strcmp(message, END_MESSAGE) == 0;
}

int message_is_control_signal(const char *message)
{
    return strcmp(message, CLIENT_DISCONNECT_NOTICE) == 0 ||
           strcmp(message, SERVER_DISCONNECT_NOTICE) == 0 ||
           strcmp(message, SERVER_SHUTDOWN_NOTICE) == 0;
}

static void print_remote_disconnect_message(const char *received_message, EndpointRole local_role)
{
    if (strcmp(received_message, CLIENT_DISCONNECT_NOTICE) == 0) {
        printf("\n[TCP] El cliente ha terminado la conexion.\n");
        return;
    }

    if (strcmp(received_message, SERVER_DISCONNECT_NOTICE) == 0) {
        printf("\n[TCP] El servidor ha terminado la conexion.\n");
        return;
    }

    if (strcmp(received_message, SERVER_SHUTDOWN_NOTICE) == 0) {
        printf("\n[TCP] El servidor se esta apagando. Conexion finalizada.\n");
        return;
    }

    if (local_role == ENDPOINT_SERVER) {
        printf("\n[TCP] El cliente cerro la conexion sin enviar aviso.\n");
    } else {
        printf("\n[TCP] El servidor cerro la conexion sin enviar aviso.\n");
    }
}

static const char *select_disconnect_notice(EndpointRole local_role)
{
    if (local_role == ENDPOINT_SERVER) {
        return SERVER_DISCONNECT_NOTICE;
    }

    return CLIENT_DISCONNECT_NOTICE;
}

/*
 * Hilo de recepcion.
 * Mantiene bloqueada la llamada recv() esperando mensajes remotos sin impedir
 * que otro hilo lea teclado y envie mensajes al mismo tiempo.
 */
void *receive_messages(void *argument)
{
    MessageThreadContext *context = (MessageThreadContext *)argument;
    char received_message[MESSAGE_BUFFER_SIZE];

    while (*(context->connection_active)) {
        /* recv(): recibe mensajes ASCII desde la conexion TCP. */
        int received_bytes = recv(context->socket_descriptor,
                                  received_message,
                                  sizeof(received_message) - 1,
                                  0);

        if (received_bytes <= 0) {
            if (*(context->connection_active)) {
                print_remote_disconnect_message("", context->local_role);
            }

            *(context->connection_active) = 0;
            break;
        }

        received_message[received_bytes] = '\0';
        remove_line_break(received_message);

        if (message_is_control_signal(received_message)) {
            print_remote_disconnect_message(received_message, context->local_role);
            *(context->connection_active) = 0;
            break;
        }

        printf("\n[Remoto] %s\n", received_message);
        printf("> ");
        fflush(stdout);
    }

    return NULL;
}

/*
 * Hilo logico de envio.
 * Lee texto desde stdin, valida que sea ASCII y usa send() para transmitirlo.
 */
void send_messages(SocketDescriptor socket_descriptor,
                   volatile sig_atomic_t *connection_active,
                   volatile sig_atomic_t *server_running,
                   EndpointRole local_role)
{
    char message_to_send[MESSAGE_BUFFER_SIZE];
    const char *message_payload;

    while (*connection_active) {
        printf("> ");
        fflush(stdout);

        if (fgets(message_to_send, sizeof(message_to_send), stdin) == NULL) {
            *connection_active = 0;
            break;
        }

        remove_line_break(message_to_send);

        if (!message_is_ascii(message_to_send)) {
            printf("[Validacion] El mensaje contiene caracteres no ASCII. Intente de nuevo.\n");
            continue;
        }

        if (local_role == ENDPOINT_SERVER &&
            strcmp(message_to_send, SERVER_SHUTDOWN_COMMAND) == 0) {
            message_payload = SERVER_SHUTDOWN_NOTICE;

            if (send(socket_descriptor, message_payload, strlen(message_payload), 0) == -1) {
                perror("[TCP] Error al enviar aviso de apagado del servidor");
            }

            printf("[TCP] Se envio aviso de apagado. Cerrando servidor.\n");
            *connection_active = 0;

            if (server_running != NULL) {
                *server_running = 0;
            }

            break;
        }

        if (message_is_end_signal(message_to_send)) {
            message_payload = select_disconnect_notice(local_role);

            if (send(socket_descriptor, message_payload, strlen(message_payload), 0) == -1) {
                perror("[TCP] Error al enviar aviso de desconexion");
            }

            if (local_role == ENDPOINT_SERVER) {
                printf("[TCP] Se aviso al cliente que el servidor termino la conexion actual.\n");
            } else {
                printf("[TCP] Se aviso al servidor que el cliente termino la conexion.\n");
            }

            *connection_active = 0;
            break;
        }

        message_payload = message_to_send;

        /* send(): envia el mensaje ASCII por la conexion TCP ya establecida. */
        if (send(socket_descriptor, message_payload, strlen(message_payload), 0) == -1) {
            perror("[TCP] Error al enviar mensaje");
            *connection_active = 0;
            break;
        }
    }
}

/*
 * Adaptador para pthread_create().
 * Convierte el argumento generico void* al contexto usado por send_messages().
 */
void *send_messages_thread(void *argument)
{
    MessageThreadContext *context = (MessageThreadContext *)argument;

    send_messages(context->socket_descriptor,
                  context->connection_active,
                  context->server_running,
                  context->local_role);
    return NULL;
}

/* close(): libera el descriptor del socket cuando ya no se necesita. */
void close_socket_safely(SocketDescriptor socket_descriptor)
{
    if (socket_descriptor != INVALID_SOCKET_DESCRIPTOR) {
#ifdef _WIN32
        closesocket(socket_descriptor);
#else
        close(socket_descriptor);
#endif
    }
}

/* shutdown(): avisa al otro extremo que no se seguira enviando ni recibiendo. */
void shutdown_socket_safely(SocketDescriptor socket_descriptor)
{
    if (socket_descriptor != INVALID_SOCKET_DESCRIPTOR) {
#ifdef _WIN32
        shutdown(socket_descriptor, SD_BOTH);
#else
        shutdown(socket_descriptor, SHUT_RDWR);
#endif
    }
}

/* perror() muestra la causa del fallo y exit() termina el proceso. */
void exit_with_error(const char *error_message)
{
#ifdef _WIN32
    fprintf(stderr, "%s. Codigo Winsock: %d\n", error_message, WSAGetLastError());
#else
    perror(error_message);
#endif
    exit(EXIT_FAILURE);
}
