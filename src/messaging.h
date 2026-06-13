#ifndef MESSAGING_H
#define MESSAGING_H

#include <signal.h>

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET SocketDescriptor;
#define INVALID_SOCKET_DESCRIPTOR INVALID_SOCKET
#else
typedef int SocketDescriptor;
#define INVALID_SOCKET_DESCRIPTOR (-1)
#endif

/* Tamano maximo de cada mensaje ASCII enviado o recibido por TCP. */
#define MESSAGE_BUFFER_SIZE 1024

/* Mensaje reservado para solicitar el cierre ordenado de la comunicacion. */
#define END_MESSAGE "FIN"

/* Comando local del servidor para apagarse despues de avisar al cliente actual. */
#define SERVER_SHUTDOWN_COMMAND "APAGAR"

/* Mensajes internos de control enviados por TCP para explicar la desconexion. */
#define CLIENT_DISCONNECT_NOTICE "__CLIENTE_TERMINO_CONEXION__"
#define SERVER_DISCONNECT_NOTICE "__SERVIDOR_TERMINO_CONEXION__"
#define SERVER_SHUTDOWN_NOTICE "__SERVIDOR_APAGANDO__"

typedef enum {
    ENDPOINT_CLIENT,
    ENDPOINT_SERVER
} EndpointRole;

/*
 * Contexto compartido por los hilos de envio y recepcion.
 * socket_descriptor identifica el socket TCP conectado.
 * connection_active indica si la comunicacion debe continuar ejecutandose.
 * server_running se usa solo en el servidor para solicitar apagado general.
 */
typedef struct {
    SocketDescriptor socket_descriptor;
    volatile sig_atomic_t *connection_active;
    volatile sig_atomic_t *server_running;
    EndpointRole local_role;
} MessageThreadContext;

/* Funcion usada por pthread_create() para recibir mensajes en paralelo. */
void *receive_messages(void *argument);

/* Funcion usada por pthread_create() para enviar mensajes en paralelo. */
void *send_messages_thread(void *argument);

/* Lee mensajes desde teclado, valida ASCII y los envia por el socket TCP. */
void send_messages(SocketDescriptor socket_descriptor,
                   volatile sig_atomic_t *connection_active,
                   volatile sig_atomic_t *server_running,
                   EndpointRole local_role);

/* Verifica que el mensaje solo contenga caracteres ASCII basicos. */
int message_is_ascii(const char *message);

/* Verifica si el mensaje recibido o enviado es la palabra de cierre FIN. */
int message_is_end_signal(const char *message);

/* Verifica si el mensaje interno recibido indica cierre de conexion. */
int message_is_control_signal(const char *message);

/* Elimina saltos de linea generados por fgets() o recibidos por red. */
void remove_line_break(char *message);

/* Cierra un descriptor de socket si es valido. */
void close_socket_safely(SocketDescriptor socket_descriptor);

/* Interrumpe envio y recepcion del socket antes de cerrarlo. */
void shutdown_socket_safely(SocketDescriptor socket_descriptor);

/* Inicializa la biblioteca de sockets cuando el sistema lo requiere. */
void initialize_socket_library(void);

/* Libera la biblioteca de sockets cuando el sistema lo requiere. */
void cleanup_socket_library(void);

/* Muestra un error del sistema y termina el programa. */
void exit_with_error(const char *error_message);

#endif
