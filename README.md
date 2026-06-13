# Mensajeria TCP full-duplex en C

Aplicacion cliente-servidor escrita en C para intercambiar mensajes ASCII en modo full-duplex usando sockets TCP.

La aclaracion del profesor fue que no se debe implementar DHCP. Los nombres como `DHCPDISCOVER`, `DHCPOFFER` o `DHCPREQUEST` solo sirven como referencia conceptual para entender que en una comunicacion existen pasos o primitivas con funciones semejantes. En este proyecto se identifican las primitivas reales del codigo TCP y se documenta para que sirve cada una.

## Flujo general

```text
Servidor: socket() -> setsockopt() -> bind() -> listen() -> accept()
Cliente:  socket() -> connect()
Ambos:    send() / recv()
Fin de cliente actual: shutdown() -> close() -> accept()
```

Despues de que el cliente se conecta al servidor, ambos programas crean dos hilos:

- un hilo para recibir mensajes con `recv()`;
- un hilo para enviar mensajes con `send()`.

La comunicacion termina cuando cualquiera envia o recibe:

```text
FIN
```

En el servidor, `FIN` termina la comunicacion con el cliente actual, pero no apaga el servidor. El servidor cierra ese socket y vuelve a `accept()` para esperar otro cliente.

El servidor tambien acepta el comando:

```text
APAGAR
```

Ese comando avisa al cliente actual que el servidor se esta apagando, cierra la conexion activa, cierra el socket principal del servidor y termina el programa.

## Gestion de trafico

Este proyecto usa un modelo simple y ordenado:

- un cliente activo a la vez;
- hasta `5` conexiones pendientes en la cola de `listen()`;
- si un segundo cliente intenta conectarse mientras hay uno activo, el sistema operativo puede dejarlo en espera dentro de la cola;
- cuando el cliente actual termina con `FIN`, el servidor acepta el siguiente cliente pendiente.
- si el servidor usa `APAGAR`, se cierra la conexion activa y el servidor deja de aceptar clientes.

Este enfoque evita mezclar mensajes de varios clientes en la misma consola del servidor. Para atender varios clientes simultaneamente se necesitaria un hilo por cliente y una politica adicional para decidir como escribir desde el servidor hacia cada cliente.

## Estructura

```text
.
+-- Makefile
+-- README.md
+-- docs/
|   +-- DOCUMENTACION_CODIGO.md
|   +-- PLAN_TAREA.md
|   +-- referencias/
|       +-- Socket Referencias.pdf
|       +-- pdf_extraido.txt
+-- src/
    +-- client.c
    +-- messaging.c
    +-- messaging.h
    +-- server.c
```

## Primitivas identificadas

La explicacion extendida de directivas, funciones, variables y primitivas esta en `docs/DOCUMENTACION_CODIGO.md`.

| Primitiva | Donde se usa | Funcion dentro del codigo |
| --- | --- | --- |
| `socket()` | cliente y servidor | Crea el descriptor de comunicacion. |
| `setsockopt()` | servidor | Permite reutilizar el puerto durante pruebas. |
| `bind()` | servidor | Asocia el socket con una IP y un puerto. |
| `listen()` | servidor | Deja el servidor esperando conexiones. |
| `accept()` | servidor | Acepta la conexion de un cliente, crea un socket dedicado y se repite cuando el cliente termina. |
| `connect()` | cliente | Solicita conectarse al servidor. |
| `send()` | cliente y servidor | Envia mensajes ASCII. |
| `recv()` | cliente y servidor | Recibe mensajes ASCII. |
| `shutdown()` | cliente y servidor | Interrumpe la comunicacion antes de cerrar. |
| `close()` | cliente y servidor | Libera el descriptor del socket. |
| `htonl()` / `htons()` | servidor | Convierte direccion y puerto al orden de bytes de red. |
| `ntohs()` | servidor | Convierte el puerto del cliente a formato legible. |
| `inet_pton()` | cliente | Convierte la IP escrita como texto a formato binario. |

## Compilacion

En MSYS2 MINGW64, abrir la terminal MINGW64, entrar al proyecto y ejecutar:

```bash
cd /c/Users/Admin/Desktop/Proyecto
make clean
make
```

Tambien funciona en Linux, WSL o un entorno POSIX con `gcc`, `make` y `pthread`.

Si aparece un error parecido a `No se pudo crear el socket del servidor: No error`, revise que se haya recompilado con el `Makefile` actualizado:

```bash
gcc -dumpmachine
make clean
make
```

El `Makefile` detecta el target real del compilador. Si el target contiene `mingw`, enlaza Winsock con `-lws2_32`. Si el target es MSYS/Cygwin/POSIX, no enlaza Winsock.

Esto genera:

```text
build/servidor
build/cliente
```

Nota: en Windows/MSYS2 MINGW64 se usa Winsock y el `Makefile` enlaza `-lws2_32`. En POSIX/MSYS/Cygwin se usan sockets tipo POSIX.

## Ejecucion

En una terminal, iniciar el servidor:

```bash
make run-server
```

En otra terminal, iniciar el cliente:

```bash
make run-client
```

Tambien se puede ejecutar manualmente:

```bash
./build/servidor 5000
./build/cliente 127.0.0.1 5000
```

En MSYS2 MINGW64 los binarios pueden aparecer como:

```bash
./build/servidor.exe 5000
./build/cliente.exe 127.0.0.1 5000
```

## Mensajes de desconexion

El usuario escribe comandos simples, pero internamente el programa envia avisos descriptivos:

| Comando | Quien lo usa | Resultado |
| --- | --- | --- |
| `FIN` | cliente | El servidor muestra: `El cliente ha terminado la conexion.` |
| `FIN` | servidor | El cliente muestra: `El servidor ha terminado la conexion.` |
| `APAGAR` | servidor | El cliente muestra: `El servidor se esta apagando. Conexion finalizada.` |

## Limpiar binarios

```bash
make clean
```
