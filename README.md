# Socket

Aplicacion cliente-servidor en C con sockets TCP para mensajeria ASCII full-duplex.

Incluye:

- `src/server.c`: servidor TCP.
- `src/client.c`: cliente TCP.
- `src/messaging.c` y `src/messaging.h`: envio, recepcion, validacion ASCII y cierre.
- `Makefile`: compilacion y ejecucion.

Comandos:

```bash
make
make run-server
make run-client
```

Mensajes especiales:

- `FIN`: termina la conexion actual.
- `APAGAR`: apaga el servidor.
