#ifndef GUI_NETWORK_H
#define GUI_NETWORK_H

#include <winsock2.h>

/* Valida que el mensaje use solo ASCII basico. */
int gui_message_is_ascii(const char *message);

/* Detecta mensajes internos del protocolo de la GUI. */
int gui_message_is_control(const char *message);

/* Crea el socket pasivo del servidor. */
int gui_create_listener_socket(int port, SOCKET *listener_socket);

/* Crea el socket del cliente y solicita conexion al servidor local. */
int gui_create_client_socket(const char *host, int port, SOCKET *client_socket);

/* Cierra un socket valido con shutdown() y closesocket(). */
void gui_close_socket(SOCKET socket_to_close);

#endif
