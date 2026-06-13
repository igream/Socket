#ifndef GUI_PROTOCOL_H
#define GUI_PROTOCOL_H

/* Tamano maximo de mensaje ASCII y cola de clientes pendientes. */
#define MESSAGE_BUFFER_SIZE 1024
#define PENDING_CONNECTIONS 5

/*
 * Mensajes internos del protocolo grafico.
 * No se muestran como texto crudo al usuario; sirven para distinguir
 * atencion, desconexion normal y apagado del servidor.
 */
#define SERVER_READY_NOTICE "__SERVIDOR_ATENDIENDO__"
#define CLIENT_DISCONNECT_NOTICE "__CLIENTE_TERMINO_CONEXION__"
#define SERVER_DISCONNECT_NOTICE "__SERVIDOR_TERMINO_CONEXION__"
#define SERVER_SHUTDOWN_NOTICE "__SERVIDOR_APAGANDO__"

/* Modo actual de la instancia grafica. */
typedef enum {
    APP_MODE_MENU,
    APP_MODE_SERVER,
    APP_MODE_CLIENT
} AppMode;

#endif
