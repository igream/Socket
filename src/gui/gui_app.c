#ifndef _WIN32
#error "La interfaz grafica requiere Windows/MSYS2 MINGW64."
#endif

/* Capas propias de la aplicacion grafica. */
#include "gui_network.h"
#include "gui_protocol.h"
#include "gui_registry.h"

/* Directivas de Windows para ventana nativa y controles. */
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

/* Directivas estandar usadas para buffers, memoria y cadenas. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Datos basicos de la ventana principal. */
#define APP_CLASS_NAME "SocketGuiAppWindow"
#define APP_TITLE "Socket GUI"

/* Valores iniciales visibles en los campos de dominio y puerto. */
#define DEFAULT_DOMAIN "local"
#define DEFAULT_PORT "5000"

/* Identificadores de controles Win32 usados en WM_COMMAND. */
#define ID_DOMAIN_EDIT 1001
#define ID_PORT_EDIT 1002
#define ID_CREATE_SERVER_BUTTON 1003
#define ID_CONNECT_CLIENT_BUTTON 1004
#define ID_MESSAGE_EDIT 1005
#define ID_SEND_BUTTON 1006
#define ID_DISCONNECT_BUTTON 1007
#define ID_SHUTDOWN_BUTTON 1008
#define ID_LOG_EDIT 1009
#define ID_STATUS_STATIC 1010

/* Mensajes privados para comunicar hilos de sockets con el hilo de UI. */
#define WM_APP_APPEND_LOG (WM_APP + 1)
#define WM_APP_SET_STATUS (WM_APP + 2)
#define WM_APP_CLIENT_READY (WM_APP + 3)
#define WM_APP_CONNECTION_ENDED (WM_APP + 4)
#define WM_APP_SERVER_STOPPED (WM_APP + 5)

/*
 * Estado completo de la aplicacion grafica.
 * Los nombres indican su responsabilidad: controles visuales, modo actual,
 * sockets, hilos, banderas de ejecucion y datos del servidor/cliente activo.
 */
typedef struct {
    /* Ventana principal y controles de interfaz. */
    HWND window;
    HWND domain_edit;
    HWND port_edit;
    HWND create_server_button;
    HWND connect_client_button;
    HWND message_edit;
    HWND send_button;
    HWND disconnect_button;
    HWND shutdown_button;
    HWND log_edit;
    HWND status_static;

    /* Estado funcional de la instancia. */
    AppMode mode;
    SOCKET listener_socket;
    SOCKET active_socket;
    HANDLE listener_thread;
    HANDLE receiver_thread;
    CRITICAL_SECTION socket_lock;

    /* Banderas de control entre UI e hilos de red. */
    int server_running;
    int connection_active;
    int client_is_attended;

    /* Datos del dominio/puerto elegido por esta instancia. */
    char current_domain[128];
    int current_port;
} AppState;

/* Estado global unico de esta instancia de ventana. */
static AppState g_app;

/* Duplica texto para pasarlo de forma segura por PostMessage(). */
static char *duplicate_text(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = (char *)malloc(length);

    if (copy != NULL) {
        memcpy(copy, text, length);
    }

    return copy;
}

/* Publica una linea de log desde cualquier hilo hacia el hilo de ventana. */
static void post_log(const char *text)
{
    PostMessage(g_app.window, WM_APP_APPEND_LOG, 0, (LPARAM)duplicate_text(text));
}

/* Publica un cambio de estado desde cualquier hilo hacia la UI. */
static void post_status(const char *text)
{
    PostMessage(g_app.window, WM_APP_SET_STATUS, 0, (LPARAM)duplicate_text(text));
}

/* Agrega una linea al cuadro de historial de mensajes. */
static void append_log_text(const char *text)
{
    int current_length = GetWindowTextLength(g_app.log_edit);

    SendMessage(g_app.log_edit, EM_SETSEL, current_length, current_length);
    SendMessage(g_app.log_edit, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessage(g_app.log_edit, EM_REPLACESEL, FALSE, (LPARAM)"\r\n");
}

/* Cambia el texto de estado visible debajo de los controles principales. */
static void set_status_text(const char *text)
{
    SetWindowText(g_app.status_static, text);
}

/* Habilita controles del menu inicial: crear servidor o conectarse. */
static void set_controls_for_menu(void)
{
    EnableWindow(g_app.domain_edit, TRUE);
    EnableWindow(g_app.port_edit, TRUE);
    EnableWindow(g_app.create_server_button, TRUE);
    EnableWindow(g_app.connect_client_button, TRUE);
    EnableWindow(g_app.message_edit, FALSE);
    EnableWindow(g_app.send_button, FALSE);
    EnableWindow(g_app.disconnect_button, FALSE);
    EnableWindow(g_app.shutdown_button, FALSE);
}

/* Estado visual del cliente cuando ya conecto pero aun espera atencion. */
static void set_controls_for_waiting_client(void)
{
    EnableWindow(g_app.domain_edit, FALSE);
    EnableWindow(g_app.port_edit, FALSE);
    EnableWindow(g_app.create_server_button, FALSE);
    EnableWindow(g_app.connect_client_button, FALSE);
    EnableWindow(g_app.message_edit, FALSE);
    EnableWindow(g_app.send_button, FALSE);
    EnableWindow(g_app.disconnect_button, TRUE);
    EnableWindow(g_app.shutdown_button, FALSE);
}

/* Estado visual de conversacion activa. */
static void set_controls_for_chat(AppMode mode)
{
    EnableWindow(g_app.domain_edit, FALSE);
    EnableWindow(g_app.port_edit, FALSE);
    EnableWindow(g_app.create_server_button, FALSE);
    EnableWindow(g_app.connect_client_button, FALSE);
    EnableWindow(g_app.message_edit, TRUE);
    EnableWindow(g_app.send_button, TRUE);
    EnableWindow(g_app.disconnect_button, TRUE);
    EnableWindow(g_app.shutdown_button, mode == APP_MODE_SERVER);
}

/* Estado visual del servidor activo sin cliente atendido. */
static void set_controls_for_server_waiting(void)
{
    EnableWindow(g_app.domain_edit, FALSE);
    EnableWindow(g_app.port_edit, FALSE);
    EnableWindow(g_app.create_server_button, FALSE);
    EnableWindow(g_app.connect_client_button, FALSE);
    EnableWindow(g_app.message_edit, FALSE);
    EnableWindow(g_app.send_button, FALSE);
    EnableWindow(g_app.disconnect_button, FALSE);
    EnableWindow(g_app.shutdown_button, TRUE);
}

/* Lee texto de un control EDIT asegurando terminacion nula. */
static void get_control_text(HWND control, char *buffer, int buffer_size)
{
    GetWindowText(control, buffer, buffer_size);
    buffer[buffer_size - 1] = '\0';
}

/* Cierra la conexion activa y actualiza banderas compartidas. */
static void close_active_connection(void)
{
    EnterCriticalSection(&g_app.socket_lock);

    gui_close_socket(g_app.active_socket);
    g_app.active_socket = INVALID_SOCKET;
    g_app.connection_active = 0;
    g_app.client_is_attended = 0;

    LeaveCriticalSection(&g_app.socket_lock);
}

/* Espera brevemente a un hilo y libera su handle. */
static void close_thread_handle(HANDLE *thread_handle)
{
    if (*thread_handle != NULL) {
        WaitForSingleObject(*thread_handle, 1000);
        CloseHandle(*thread_handle);
        *thread_handle = NULL;
    }
}

/*
 * Envia un texto por el socket activo.
 * Usa socket_lock porque el hilo de UI y los hilos de red pueden tocar
 * active_socket al mismo tiempo.
 */
static int send_text_to_active_socket(const char *message)
{
    int send_result;

    EnterCriticalSection(&g_app.socket_lock);

    if (g_app.active_socket == INVALID_SOCKET) {
        LeaveCriticalSection(&g_app.socket_lock);
        return 0;
    }

    send_result = send(g_app.active_socket, message, (int)strlen(message), 0);
    LeaveCriticalSection(&g_app.socket_lock);

    return send_result != SOCKET_ERROR;
}

/*
 * Hilo del servidor grafico.
 * Espera clientes con accept(), atiende uno a la vez y deja a los demas
 * en la cola de listen() hasta que el cliente actual termine.
 */
static DWORD WINAPI server_listener_thread(LPVOID parameter)
{
    (void)parameter;

    while (g_app.server_running) {
        SOCKET client_socket = accept(g_app.listener_socket, NULL, NULL);
        char log_message[256];
        char receive_buffer[MESSAGE_BUFFER_SIZE];
        int received_bytes;

        if (client_socket == INVALID_SOCKET) {
            break;
        }

        EnterCriticalSection(&g_app.socket_lock);
        g_app.active_socket = client_socket;
        g_app.connection_active = 1;
        LeaveCriticalSection(&g_app.socket_lock);

        snprintf(log_message, sizeof(log_message), "Cliente atendido en dominio '%s'.", g_app.current_domain);
        post_log(log_message);
        post_status("Cliente conectado. Puede enviar mensajes.");
        PostMessage(g_app.window, WM_APP_CLIENT_READY, APP_MODE_SERVER, 0);
        send(client_socket, SERVER_READY_NOTICE, (int)strlen(SERVER_READY_NOTICE), 0);

        while (g_app.server_running && g_app.connection_active) {
            received_bytes = recv(client_socket, receive_buffer, sizeof(receive_buffer) - 1, 0);

            if (received_bytes <= 0) {
                post_log("El cliente cerro la conexion sin aviso.");
                break;
            }

            receive_buffer[received_bytes] = '\0';

            if (strcmp(receive_buffer, CLIENT_DISCONNECT_NOTICE) == 0) {
                post_log("El cliente ha terminado la conexion.");
                break;
            }

            if (!gui_message_is_control(receive_buffer)) {
                char display_message[MESSAGE_BUFFER_SIZE + 32];
                snprintf(display_message, sizeof(display_message), "Cliente: %s", receive_buffer);
                post_log(display_message);
            }
        }

        close_active_connection();

        if (g_app.server_running) {
            post_status("Servidor activo. Esperando otro cliente...");
            PostMessage(g_app.window, WM_APP_CONNECTION_ENDED, APP_MODE_SERVER, 0);
        }
    }

    PostMessage(g_app.window, WM_APP_SERVER_STOPPED, 0, 0);
    return 0;
}

/*
 * Hilo del cliente grafico.
 * Recibe mensajes del servidor y actualiza la UI mediante PostMessage().
 */
static DWORD WINAPI client_receiver_thread(LPVOID parameter)
{
    SOCKET client_socket = (SOCKET)parameter;
    char receive_buffer[MESSAGE_BUFFER_SIZE];
    int received_bytes;

    while (g_app.connection_active) {
        received_bytes = recv(client_socket, receive_buffer, sizeof(receive_buffer) - 1, 0);

        if (received_bytes <= 0) {
            post_log("El servidor no esta disponible o cerro la conexion.");
            break;
        }

        receive_buffer[received_bytes] = '\0';

        if (strcmp(receive_buffer, SERVER_READY_NOTICE) == 0) {
            g_app.client_is_attended = 1;
            post_status("Servidor conectado. Puede enviar mensajes.");
            post_log("El servidor comenzo a atender este cliente.");
            PostMessage(g_app.window, WM_APP_CLIENT_READY, APP_MODE_CLIENT, 0);
            continue;
        }

        if (strcmp(receive_buffer, SERVER_DISCONNECT_NOTICE) == 0) {
            post_log("El servidor ha terminado la conexion.");
            break;
        }

        if (strcmp(receive_buffer, SERVER_SHUTDOWN_NOTICE) == 0) {
            post_log("El servidor se esta apagando. Conexion finalizada.");
            break;
        }

        if (!gui_message_is_control(receive_buffer)) {
            char display_message[MESSAGE_BUFFER_SIZE + 32];
            snprintf(display_message, sizeof(display_message), "Servidor: %s", receive_buffer);
            post_log(display_message);
        }
    }

    close_active_connection();
    PostMessage(g_app.window, WM_APP_CONNECTION_ENDED, APP_MODE_CLIENT, 0);
    return 0;
}

/* Lee dominio/puerto de la UI, registra el servidor y empieza a escuchar. */
static void start_server(void)
{
    char domain[128];
    char port_text[32];
    int port;
    int registry_result;
    SOCKET listener_socket;
    char status_message[256];

    get_control_text(g_app.domain_edit, domain, sizeof(domain));
    get_control_text(g_app.port_edit, port_text, sizeof(port_text));
    port = atoi(port_text);

    if (domain[0] == '\0' || port <= 0 || port > 65535) {
        MessageBox(g_app.window, "Ingrese dominio y puerto validos.", APP_TITLE, MB_ICONWARNING);
        return;
    }

    registry_result = registry_register_domain(domain, port);

    if (registry_result == REGISTRY_DOMAIN_EXISTS) {
        MessageBox(g_app.window, "Ya existe un servidor registrado con ese dominio.", APP_TITLE, MB_ICONWARNING);
        return;
    }

    if (registry_result == REGISTRY_PORT_EXISTS) {
        MessageBox(g_app.window, "Ya existe un servidor registrado con ese puerto. Use otro puerto.", APP_TITLE, MB_ICONWARNING);
        return;
    }

    if (registry_result != REGISTRY_OK) {
        MessageBox(g_app.window, "No se pudo registrar el servidor local.", APP_TITLE, MB_ICONERROR);
        return;
    }

    if (!gui_create_listener_socket(port, &listener_socket)) {
        registry_unregister_domain(domain);
        MessageBox(g_app.window, "No se pudo crear el servidor. Revise si el puerto ya esta ocupado.", APP_TITLE, MB_ICONERROR);
        return;
    }

    lstrcpyn(g_app.current_domain, domain, sizeof(g_app.current_domain));
    g_app.current_port = port;
    g_app.mode = APP_MODE_SERVER;
    g_app.server_running = 1;
    g_app.listener_socket = listener_socket;
    g_app.listener_thread = CreateThread(NULL, 0, server_listener_thread, NULL, 0, NULL);

    if (g_app.listener_thread == NULL) {
        closesocket(listener_socket);
        g_app.listener_socket = INVALID_SOCKET;
        g_app.server_running = 0;
        registry_unregister_domain(domain);
        MessageBox(g_app.window, "No se pudo crear el hilo del servidor.", APP_TITLE, MB_ICONERROR);
        return;
    }

    set_controls_for_server_waiting();
    snprintf(status_message, sizeof(status_message), "Servidor '%s' activo en puerto %d. Esperando clientes...", domain, port);
    set_status_text(status_message);
    append_log_text(status_message);
}

/* Conecta esta instancia como cliente al servidor registrado por dominio. */
static void connect_client(void)
{
    char domain[128];
    int port;
    SOCKET client_socket;
    char status_message[256];

    get_control_text(g_app.domain_edit, domain, sizeof(domain));

    if (domain[0] == '\0') {
        MessageBox(g_app.window, "Ingrese el dominio del servidor.", APP_TITLE, MB_ICONWARNING);
        return;
    }

    if (!registry_lookup_domain(domain, &port)) {
        MessageBox(g_app.window, "Servidor no disponible para ese dominio.", APP_TITLE, MB_ICONWARNING);
        return;
    }

    if (!gui_create_client_socket("127.0.0.1", port, &client_socket)) {
        MessageBox(g_app.window, "No se pudo conectar al servidor.", APP_TITLE, MB_ICONERROR);
        return;
    }

    lstrcpyn(g_app.current_domain, domain, sizeof(g_app.current_domain));
    g_app.current_port = port;
    g_app.mode = APP_MODE_CLIENT;
    g_app.connection_active = 1;
    g_app.client_is_attended = 0;
    g_app.active_socket = client_socket;
    g_app.receiver_thread = CreateThread(NULL, 0, client_receiver_thread, (LPVOID)client_socket, 0, NULL);

    if (g_app.receiver_thread == NULL) {
        close_active_connection();
        g_app.mode = APP_MODE_MENU;
        MessageBox(g_app.window, "No se pudo crear el hilo del cliente.", APP_TITLE, MB_ICONERROR);
        return;
    }

    set_controls_for_waiting_client();
    snprintf(status_message, sizeof(status_message), "Conectado a '%s'. En cola de atencion...", domain);
    set_status_text(status_message);
    append_log_text(status_message);
}

/*
 * Cierra la conexion actual sin cerrar necesariamente la aplicacion.
 * En modo servidor vuelve a esperar clientes; en modo cliente regresa al menu.
 */
static void disconnect_current_connection(void)
{
    if (g_app.mode == APP_MODE_SERVER) {
        if (g_app.active_socket != INVALID_SOCKET) {
            send_text_to_active_socket(SERVER_DISCONNECT_NOTICE);
            append_log_text("El servidor termino la conexion actual.");
        }

        close_active_connection();
        set_controls_for_server_waiting();
        set_status_text("Servidor activo. Esperando otro cliente...");
        return;
    }

    if (g_app.mode == APP_MODE_CLIENT) {
        if (g_app.active_socket != INVALID_SOCKET) {
            send_text_to_active_socket(CLIENT_DISCONNECT_NOTICE);
            append_log_text("El cliente termino la conexion.");
        }

        close_active_connection();
        close_thread_handle(&g_app.receiver_thread);
        g_app.mode = APP_MODE_MENU;
        set_controls_for_menu();
        set_status_text("Menu inicial. Puede conectarse a otro servidor.");
    }
}

/*
 * Apaga el servidor completo.
 * Libera el dominio registrado para que otra instancia pueda crearlo de nuevo.
 */
static void shutdown_server(void)
{
    if (g_app.mode != APP_MODE_SERVER) {
        return;
    }

    if (g_app.active_socket != INVALID_SOCKET) {
        send_text_to_active_socket(SERVER_SHUTDOWN_NOTICE);
    }

    g_app.server_running = 0;
    close_active_connection();

    if (g_app.listener_socket != INVALID_SOCKET) {
        closesocket(g_app.listener_socket);
        g_app.listener_socket = INVALID_SOCKET;
    }

    close_thread_handle(&g_app.listener_thread);
    registry_unregister_domain(g_app.current_domain);
    g_app.mode = APP_MODE_MENU;
    set_controls_for_menu();
    set_status_text("Servidor apagado. Puede crear otro servidor.");
    append_log_text("Servidor apagado.");
}

/*
 * Toma el texto del cuadro de mensaje y decide si es:
 * - APAGAR: apagar servidor.
 * - FIN: terminar conexion actual.
 * - texto normal: enviarlo por TCP.
 */
static void send_message_from_ui(void)
{
    char message[MESSAGE_BUFFER_SIZE];
    char display_message[MESSAGE_BUFFER_SIZE + 32];

    get_control_text(g_app.message_edit, message, sizeof(message));

    if (message[0] == '\0') {
        return;
    }

    if (!gui_message_is_ascii(message)) {
        MessageBox(g_app.window, "El mensaje debe contener solo caracteres ASCII.", APP_TITLE, MB_ICONWARNING);
        return;
    }

    if (g_app.mode == APP_MODE_SERVER && strcmp(message, "APAGAR") == 0) {
        shutdown_server();
        SetWindowText(g_app.message_edit, "");
        return;
    }

    if (strcmp(message, "FIN") == 0) {
        disconnect_current_connection();
        SetWindowText(g_app.message_edit, "");
        return;
    }

    if (!send_text_to_active_socket(message)) {
        MessageBox(g_app.window, "No hay una conexion activa para enviar.", APP_TITLE, MB_ICONWARNING);
        return;
    }

    snprintf(display_message, sizeof(display_message), "Yo: %s", message);
    append_log_text(display_message);
    SetWindowText(g_app.message_edit, "");
}

/* Crea todos los controles visuales de la ventana principal. */
static void create_controls(HWND window)
{
    CreateWindow("STATIC", "Dominio:", WS_CHILD | WS_VISIBLE, 16, 18, 70, 22, window, NULL, NULL, NULL);
    g_app.domain_edit = CreateWindow("EDIT", DEFAULT_DOMAIN, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                     90, 16, 170, 24, window, (HMENU)ID_DOMAIN_EDIT, NULL, NULL);

    CreateWindow("STATIC", "Puerto:", WS_CHILD | WS_VISIBLE, 280, 18, 55, 22, window, NULL, NULL, NULL);
    g_app.port_edit = CreateWindow("EDIT", DEFAULT_PORT, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                   335, 16, 80, 24, window, (HMENU)ID_PORT_EDIT, NULL, NULL);

    g_app.create_server_button = CreateWindow("BUTTON", "Crear servidor", WS_CHILD | WS_VISIBLE,
                                              430, 14, 125, 28, window, (HMENU)ID_CREATE_SERVER_BUTTON, NULL, NULL);
    g_app.connect_client_button = CreateWindow("BUTTON", "Entrar cliente", WS_CHILD | WS_VISIBLE,
                                               565, 14, 120, 28, window, (HMENU)ID_CONNECT_CLIENT_BUTTON, NULL, NULL);

    g_app.status_static = CreateWindow("STATIC", "Menu inicial.", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       16, 54, 670, 24, window, (HMENU)ID_STATUS_STATIC, NULL, NULL);

    g_app.log_edit = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
                                  ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
                                  16, 84, 670, 300, window, (HMENU)ID_LOG_EDIT, NULL, NULL);

    g_app.message_edit = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                      16, 398, 420, 26, window, (HMENU)ID_MESSAGE_EDIT, NULL, NULL);
    g_app.send_button = CreateWindow("BUTTON", "Enviar", WS_CHILD | WS_VISIBLE,
                                     446, 396, 75, 30, window, (HMENU)ID_SEND_BUTTON, NULL, NULL);
    g_app.disconnect_button = CreateWindow("BUTTON", "Desconectar", WS_CHILD | WS_VISIBLE,
                                           530, 396, 95, 30, window, (HMENU)ID_DISCONNECT_BUTTON, NULL, NULL);
    g_app.shutdown_button = CreateWindow("BUTTON", "Apagar", WS_CHILD | WS_VISIBLE,
                                         635, 396, 52, 30, window, (HMENU)ID_SHUTDOWN_BUTTON, NULL, NULL);

    set_controls_for_menu();
}

/*
 * Procedimiento principal de ventana.
 * Recibe eventos de botones, eventos de cierre y mensajes privados enviados
 * desde los hilos de red para actualizar la interfaz de forma segura.
 */
static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        g_app.window = window;
        create_controls(window);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case ID_CREATE_SERVER_BUTTON:
            start_server();
            return 0;
        case ID_CONNECT_CLIENT_BUTTON:
            connect_client();
            return 0;
        case ID_SEND_BUTTON:
            send_message_from_ui();
            return 0;
        case ID_DISCONNECT_BUTTON:
            disconnect_current_connection();
            return 0;
        case ID_SHUTDOWN_BUTTON:
            shutdown_server();
            return 0;
        default:
            return 0;
        }

    case WM_APP_APPEND_LOG:
        if (lparam != 0) {
            append_log_text((const char *)lparam);
            free((void *)lparam);
        }
        return 0;

    case WM_APP_SET_STATUS:
        if (lparam != 0) {
            set_status_text((const char *)lparam);
            free((void *)lparam);
        }
        return 0;

    case WM_APP_CLIENT_READY:
        set_controls_for_chat((AppMode)wparam);
        return 0;

    case WM_APP_CONNECTION_ENDED:
        if ((AppMode)wparam == APP_MODE_CLIENT) {
            close_thread_handle(&g_app.receiver_thread);
            g_app.mode = APP_MODE_MENU;
            set_controls_for_menu();
            set_status_text("Conexion finalizada. Puede conectarse a otro servidor.");
        } else if (g_app.mode == APP_MODE_SERVER && g_app.server_running) {
            set_controls_for_server_waiting();
        }
        return 0;

    case WM_APP_SERVER_STOPPED:
        return 0;

    case WM_DESTROY:
        if (g_app.mode == APP_MODE_SERVER) {
            shutdown_server();
        } else if (g_app.mode == APP_MODE_CLIENT) {
            disconnect_current_connection();
        }

        close_thread_handle(&g_app.listener_thread);
        close_thread_handle(&g_app.receiver_thread);
        DeleteCriticalSection(&g_app.socket_lock);
        WSACleanup();
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(window, message, wparam, lparam);
    }
}

/*
 * Punto de entrada de la aplicacion grafica Win32.
 * Inicializa Winsock, registra la clase de ventana, crea la UI y arranca
 * el ciclo de mensajes de Windows.
 */
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance, LPSTR command_line, int show_command)
{
    WNDCLASS window_class;
    MSG message;
    WSADATA winsock_data;

    (void)previous_instance;
    (void)command_line;

    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
        MessageBox(NULL, "No se pudo inicializar Winsock.", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    memset(&g_app, 0, sizeof(g_app));
    g_app.mode = APP_MODE_MENU;
    g_app.listener_socket = INVALID_SOCKET;
    g_app.active_socket = INVALID_SOCKET;
    InitializeCriticalSection(&g_app.socket_lock);

    memset(&window_class, 0, sizeof(window_class));
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = APP_CLASS_NAME;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClass(&window_class)) {
        MessageBox(NULL, "No se pudo registrar la ventana.", APP_TITLE, MB_ICONERROR);
        WSACleanup();
        return 1;
    }

    g_app.window = CreateWindow(APP_CLASS_NAME,
                                APP_TITLE,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                720,
                                480,
                                NULL,
                                NULL,
                                instance,
                                NULL);

    if (g_app.window == NULL) {
        MessageBox(NULL, "No se pudo crear la ventana.", APP_TITLE, MB_ICONERROR);
        WSACleanup();
        return 1;
    }

    ShowWindow(g_app.window, show_command);
    UpdateWindow(g_app.window);

    while (GetMessage(&message, NULL, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    return (int)message.wParam;
}
