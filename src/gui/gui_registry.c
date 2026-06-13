#include "gui_registry.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

/*
 * Registro local de servidores.
 * Se guarda en una ruta temporal para que varias instancias de la app
 * puedan descubrir servidores por dominio sin usar servicios externos.
 */
#define REGISTRY_MUTEX_NAME "Local\\SocketGuiRegistryMutex"
#define REGISTRY_FILE_NAME "socket_gui_servers.txt"
#define REGISTRY_TEMP_FILE_NAME "socket_gui_servers.tmp"

/* Obtiene la ruta del archivo temporal donde se registran dominio y puerto. */
static void get_registry_file_path(char *path_buffer, DWORD path_buffer_size)
{
    DWORD temp_length = GetTempPath(path_buffer_size, path_buffer);

    if (temp_length == 0 || temp_length >= path_buffer_size) {
        lstrcpyn(path_buffer, REGISTRY_FILE_NAME, path_buffer_size);
        return;
    }

    lstrcat(path_buffer, REGISTRY_FILE_NAME);
}

/* Obtiene la ruta del archivo temporal usado para reescribir el registro. */
static void get_registry_temp_file_path(char *path_buffer, DWORD path_buffer_size)
{
    DWORD temp_length = GetTempPath(path_buffer_size, path_buffer);

    if (temp_length == 0 || temp_length >= path_buffer_size) {
        lstrcpyn(path_buffer, REGISTRY_TEMP_FILE_NAME, path_buffer_size);
        return;
    }

    lstrcat(path_buffer, REGISTRY_TEMP_FILE_NAME);
}

/*
 * Bloquea el registro local para que varias instancias no escriban
 * socket_gui_servers.txt al mismo tiempo.
 */
static HANDLE lock_registry(void)
{
    HANDLE mutex_handle = CreateMutex(NULL, FALSE, REGISTRY_MUTEX_NAME);

    if (mutex_handle != NULL) {
        WaitForSingleObject(mutex_handle, INFINITE);
    }

    return mutex_handle;
}

/* Libera el mutex del registro local. */
static void unlock_registry(HANDLE mutex_handle)
{
    if (mutex_handle != NULL) {
        ReleaseMutex(mutex_handle);
        CloseHandle(mutex_handle);
    }
}

/*
 * Busca un dominio en el registro.
 * Esta version asume que el mutex ya fue tomado por el llamador.
 */
static int registry_lookup_domain_unlocked(const char *domain, int *port)
{
    char registry_path[MAX_PATH];
    FILE *registry_file;
    char line[256];
    char stored_domain[128];
    int stored_port;

    get_registry_file_path(registry_path, sizeof(registry_path));
    registry_file = fopen(registry_path, "r");

    if (registry_file == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), registry_file) != NULL) {
        if (sscanf(line, "%127[^|]|%d", stored_domain, &stored_port) == 2 &&
            strcmp(stored_domain, domain) == 0) {
            *port = stored_port;
            fclose(registry_file);
            return 1;
        }
    }

    fclose(registry_file);
    return 0;
}

/*
 * Busca si un puerto ya esta registrado por otro servidor.
 * Evita que dos dominios distintos apunten al mismo 127.0.0.1:puerto.
 */
static int registry_lookup_port_unlocked(int port, char *domain_buffer, int domain_buffer_size)
{
    char registry_path[MAX_PATH];
    FILE *registry_file;
    char line[256];
    char stored_domain[128];
    int stored_port;

    get_registry_file_path(registry_path, sizeof(registry_path));
    registry_file = fopen(registry_path, "r");

    if (registry_file == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), registry_file) != NULL) {
        if (sscanf(line, "%127[^|]|%d", stored_domain, &stored_port) == 2 &&
            stored_port == port) {
            lstrcpyn(domain_buffer, stored_domain, domain_buffer_size);
            fclose(registry_file);
            return 1;
        }
    }

    fclose(registry_file);
    return 0;
}

int registry_lookup_domain(const char *domain, int *port)
{
    int found;
    HANDLE mutex_handle = lock_registry();

    found = registry_lookup_domain_unlocked(domain, port);
    unlock_registry(mutex_handle);

    return found;
}

int registry_register_domain(const char *domain, int port)
{
    char registry_path[MAX_PATH];
    FILE *registry_file;
    int existing_port = 0;
    char existing_domain[128];
    HANDLE mutex_handle = lock_registry();

    if (registry_lookup_domain_unlocked(domain, &existing_port)) {
        unlock_registry(mutex_handle);
        return REGISTRY_DOMAIN_EXISTS;
    }

    if (registry_lookup_port_unlocked(port, existing_domain, sizeof(existing_domain))) {
        unlock_registry(mutex_handle);
        return REGISTRY_PORT_EXISTS;
    }

    get_registry_file_path(registry_path, sizeof(registry_path));
    registry_file = fopen(registry_path, "a");

    if (registry_file == NULL) {
        unlock_registry(mutex_handle);
        return REGISTRY_WRITE_ERROR;
    }

    fprintf(registry_file, "%s|%d\n", domain, port);
    fclose(registry_file);
    unlock_registry(mutex_handle);
    return REGISTRY_OK;
}

void registry_unregister_domain(const char *domain)
{
    char registry_path[MAX_PATH];
    char temporary_path[MAX_PATH];
    FILE *registry_file;
    FILE *temporary_file;
    char line[256];
    char stored_domain[128];
    int stored_port;
    HANDLE mutex_handle = lock_registry();

    get_registry_file_path(registry_path, sizeof(registry_path));
    get_registry_temp_file_path(temporary_path, sizeof(temporary_path));

    registry_file = fopen(registry_path, "r");

    if (registry_file == NULL) {
        unlock_registry(mutex_handle);
        return;
    }

    temporary_file = fopen(temporary_path, "w");

    if (temporary_file == NULL) {
        fclose(registry_file);
        unlock_registry(mutex_handle);
        return;
    }

    while (fgets(line, sizeof(line), registry_file) != NULL) {
        if (sscanf(line, "%127[^|]|%d", stored_domain, &stored_port) == 2 &&
            strcmp(stored_domain, domain) == 0) {
            continue;
        }

        fputs(line, temporary_file);
    }

    fclose(registry_file);
    fclose(temporary_file);
    DeleteFile(registry_path);
    MoveFile(temporary_path, registry_path);
    unlock_registry(mutex_handle);
}
