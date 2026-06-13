#ifndef GUI_REGISTRY_H
#define GUI_REGISTRY_H

/* Resultados posibles al registrar dominio/puerto localmente. */
#define REGISTRY_OK 0
#define REGISTRY_DOMAIN_EXISTS 1
#define REGISTRY_PORT_EXISTS 2
#define REGISTRY_WRITE_ERROR 3

/* Busca el puerto asociado a un dominio registrado por otra instancia. */
int registry_lookup_domain(const char *domain, int *port);

/* Registra un servidor local por dominio y puerto. */
int registry_register_domain(const char *domain, int port);

/* Elimina del registro el dominio del servidor que se apago. */
void registry_unregister_domain(const char *domain);

#endif
