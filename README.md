# M5Stack_SerialMonitor

Monitor serie para el **M5Stack Cardputer**, agregado como una app más del firmware
[Bruce](https://github.com/pr3y/Bruce). Permite configurar un puerto serie, escribir texto con
el teclado del Cardputer y enviarlo, y ver en pantalla lo que responde el otro equipo.

En el Cardputer aparece en **Others → Serial Monitor**.

## Funcionalidades

- **Dos puertos a elección**
  - **USB** (por defecto): el USB-C del Cardputer. En Windows aparece como un puerto COM
    (por ejemplo COM4) y del otro lado se usa un programa de terminal (PuTTY, Monitor Serie de
    Arduino, etc.). Es un puerto virtual (USB CDC): la velocidad y la paridad las define el
    programa de la PC.
  - **Grove UART**: el conector Grove del Cardputer (por defecto RX = G1, TX = G2; se puede
    cambiar desde la app). Señales de 3,3 V TTL.
- **Parámetros del UART Grove**: velocidad (300 a 921600 baudios o un valor a mano),
  bits de datos (5 a 8), paridad (ninguna, par, impar) y bits de stop (1 o 2).
- **Fin de línea** al enviar: ninguno, CR, LF o CR+LF.
- **Eco local**: muestra en amarillo lo que enviaste.
- **Vista de lo recibido** en texto (ASCII) o en hexadecimal.
- **Tamaño de letra** grande (por defecto) o chico.
- **Historial** de 300 líneas, con desplazamiento hacia arriba y abajo.
- **Contadores** de bytes recibidos y enviados en la barra superior.
- Envío de **ESC** (0x1B) y **Ctrl+C** (0x03) desde el menú de la terminal.

## Uso

1. En el Cardputer: **Others → Serial Monitor**.
2. Elegí el puerto y los parámetros, y entrá en **Start**.
3. En la terminal:

| Tecla | Acción |
|---|---|
| Texto + **Enter** | Envía la línea (con el fin de línea elegido) |
| **Backspace** | Borra el último carácter |
| **ESC** (tecla `` ` ``) | Menú: seguir, limpiar pantalla, enviar ESC / Ctrl+C, configuración, salir |
| **Fn + ;** / **Fn + .** | Desplaza el historial hacia arriba / abajo |

Con el puerto USB, mientras la terminal está abierta se suspende la consola de comandos de Bruce
(para que no se quede con lo que llega). Al salir vuelve a funcionar.

**Cableado del Grove:** TX del Cardputer al RX del otro equipo, RX al TX y GND con GND.
No conectar directo a un puerto RS-232 (±12 V): hace falta un adaptador tipo MAX3232.

## Cómo cargarlo en el Cardputer

El repo trae el firmware ya compilado: [`firmware/Bruce-m5stack-cardputer.bin`](firmware/).
Es Bruce completo con el Serial Monitor incluido; reemplaza el firmware que tenga el equipo.
No hace falta tarjeta SD.

1. Conectá el Cardputer por USB y fijate el puerto COM en el Administrador de dispositivos.
2. Cerrá cualquier programa que tenga abierto ese puerto.
3. Ejecutá, desde la carpeta del repo:

   ```powershell
   .\tools\flash.ps1 -Port COM4
   ```

   El script usa el `esptool` que instala PlatformIO. Si no lo tenés, instalalo con
   `pip install esptool`, o grabá el `.bin` en la dirección `0x0` con el flasher web de
   Espressif (https://espressif.github.io/esptool-js/) desde Chrome o Edge.

4. Si no conecta, poné el Cardputer en **modo descarga**: apagado, mantené apretado **G0**,
   conectá el USB y soltá.
5. Al terminar, reiniciá el Cardputer.

## Cómo compilarlo

```powershell
.\tools\build.ps1
```

Descarga Bruce en la versión probada, le agrega la app, compila con PlatformIO y deja el
resultado en `firmware/`. La primera vez baja unos 2 GB de herramientas.

Los detalles (dependencias, cómo se integró en Bruce, problemas encontrados y cómo se
resolvieron) están en [doc/DESARROLLO.md](doc/DESARROLLO.md).

## Estructura

```
src/modules/others/serial_monitor.{h,cpp}   La app (se copia dentro del árbol de Bruce)
patches/bruce-integration.patch             Cambios a Bruce: entrada de menú y versión de FastLED
tools/build.ps1                             Arma y compila el firmware
tools/flash.ps1                             Graba el firmware por USB
tools/pio_winstore.py, pio-winstore-site/   PlatformIO detrás de un proxy corporativo
firmware/Bruce-m5stack-cardputer.bin        Firmware listo para grabar
doc/DESARROLLO.md                           Documentación técnica
```
