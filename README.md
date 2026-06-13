# Socket

Aplicacion cliente-servidor en C con sockets TCP para mensajeria ASCII full-duplex.

Incluye:

- `src/gui/`: aplicacion grafica local, registro de dominios y sockets GUI.
- `src/console/`: servidor y cliente de terminal.
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

En la GUI, cada servidor local debe usar un dominio y puerto unicos.

Mensajes especiales:

- `FIN`: termina la conexion actual.
- `APAGAR`: apaga el servidor.
