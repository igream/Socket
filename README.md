# Socket

Aplicacion cliente-servidor en C con sockets TCP para mensajeria ASCII full-duplex.

Incluye:

- `src/gui_app.c`: aplicacion grafica local para crear servidor o entrar como cliente.
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

En MSYS2 MINGW64, `make` tambien genera:

```text
build/socket_gui.exe
```

Mensajes especiales:

- `FIN`: termina la conexion actual.
- `APAGAR`: apaga el servidor.
