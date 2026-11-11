# Desarrollo del Serial Monitor para Bruce

Este documento cuenta cómo está hecha la app, cómo se sumó al firmware Bruce, con qué
herramientas se compiló y qué problemas aparecieron en el camino.

## 1. Punto de partida

- **Equipo:** M5Stack Cardputer (ESP32-S3, pantalla de 240×135, teclado de 56 teclas).
  Por USB, Windows lo ve como "Dispositivo serie USB" (VID `303A`, PID `1001`), en este caso COM4.
- **Firmware:** [Bruce](https://github.com/pr3y/Bruce), que ya estaba instalado. Bruce no admite
  apps externas en C++: para agregar una hay que sumarla al código fuente y recompilar todo el
  firmware.
- **Versión de Bruce usada:** commit `a59213f3ec6cd302fa32c2042b2700c9c24d991a`
  (9 de septiembre de 2026). `tools/build.ps1` descarga exactamente esa versión, porque el parche
  de integración está hecho contra ella.

## 2. Cómo se organiza Bruce (lo necesario para agregar una app)

| Pieza | Dónde | Para qué la usamos |
|---|---|---|
| Menús principales | `src/core/menu_items/*Menu.cpp` | Cada ícono del menú principal es una clase. Agregamos una entrada en `OthersMenu::optionsMenu()`. |
| Módulos | `src/modules/<área>/` | Ahí van las apps. La nuestra está en `src/modules/others/serial_monitor.*`. |
| `loopOptions()` | `src/core/display.h` | Muestra una lista de opciones (`Option{etiqueta, función}`) y ejecuta la elegida. Devuelve `-1` si se sale con ESC. Lo usamos para todos los menús de configuración. |
| `KeyStroke` | `include/globals.h` | Tarea en segundo plano que lee el teclado del Cardputer y deja la última tecla en `KeyStroke` (caracteres en `word`, más `enter`, `del`, `fn`, etc.). La terminal lo lee directamente, igual que hace `keyboard()` en `src/core/mykeyboard.cpp`. |
| Flags de botones | `EscPress`, `SelPress`, `NextPress`… | En el Cardputer los generan teclas como Backspace, ESC y `; , . /`. Dentro de la terminal se descartan en cada vuelta, porque esas teclas se tratan como texto. |
| Pines | `bruceConfigPins.uart_bus` | UART configurado en Bruce (Config → UART Pins). En el Cardputer es RX = G1 (GPIO1) y TX = G2 (GPIO2). Con `setUARTPinsMenu()` se cambia desde la app. |
| Consola serie | `src/core/serialcmds.cpp` | Tarea `serialcmds` (handle `serialcmdsTaskHandle`) que lee comandos de Bruce del USB. |
| Pantalla | `tft` (`src/core/display.h`) | API estilo TFT_eSPI: `fillRect`, `setCursor`, `print`, `setTextSize`… |

## 3. Diseño de la app (`src/modules/others/serial_monitor.cpp`)

Flujo general: `serial_monitor_setup()` → menú de configuración → terminal → vuelta al menú o
salida.

### Puertos

- **USB:** se usa el objeto `Serial` de Arduino. En el Cardputer es el USB CDC nativo del ESP32-S3
  (HWCDC), así que velocidad, paridad y bits no existen del lado del dispositivo: los fija el
  programa de la PC. Por eso el menú los oculta cuando el puerto es USB.
  Al abrir la terminal se suspende la tarea `serialcmds` de Bruce (`vTaskSuspend`) para que no se
  quede con los datos que llegan, y se vuelve a activar al salir (`releaseUsb()`).
- **Grove UART:** se usa `HardwareSerial smUart(2)` (UART2). Bruce usa `Serial1` (UART1) para el
  GPS y para la opción "Serial use UART", así que tomar el UART2 evita conflictos.
  El formato se arma con una tabla `SERIAL_CFG[bits de datos][paridad][stop]` que contiene las
  constantes de Arduino (`SERIAL_5N1` … `SERIAL_8O2`). El buffer de recepción es de 4 KB.

Las dos opciones se manejan con un puntero `Stream *port`, así la terminal no depende del puerto.

### Terminal

- **Recepción:** en cada vuelta se leen hasta 2048 bytes, para no trabar la interfaz.
  - En ASCII: CR, LF o CR+LF cortan la línea, el tabulador se muestra como dos espacios,
    Backspace borra y los demás caracteres no imprimibles se ven como `.`.
  - En HEX: cada byte se muestra como `XX `.
- **Historial:** `std::deque<String>` de hasta 300 líneas, ya cortadas al ancho de la pantalla.
  `scrollBack` indica cuántas líneas se está mirando hacia atrás.
- **Pantalla:** se redibuja solo si hubo cambios y como máximo cada 40 ms. Tiene tres zonas:
  - Barra superior: puerto o formato y contadores.
  - Área de lo recibido.
  - Línea de entrada.

  La fuente es la de 6×8 px de TFT_eSPI, escalada por `cfgFont` (×2 por defecto).
- **Teclado:**
  - Enter envía la línea más el fin de línea elegido.
  - Backspace borra.
  - La tecla `` ` `` (ESC) abre el menú de la terminal.
  - Fn + `;` y Fn + `.` llegan como los códigos `0xDA` y `0xD9`, y desplazan el historial.
- **Configuración:** se guarda en variables estáticas. Se mantiene mientras el Cardputer esté
  encendido, pero no se graba en la flash.

## 4. Integración con Bruce (`patches/bruce-integration.patch`)

Son dos cambios chicos sobre el commit indicado:

1. **`src/core/menu_items/OthersMenu.cpp`:** `#include "modules/others/serial_monitor.h"` y la
   entrada `{"Serial Monitor", serial_monitor_setup}` en el menú Others.
2. **`platformio.ini`:** `fastled/FastLED @^3.10.3` pasa a `fastled/FastLED @3.10.3` (versión fija).
   Con `^`, PlatformIO baja la última 3.10.x (3.10.9), que no compila con Bruce: la placa define
   `-DFP=1` (tamaño de fuente) y FastLED 3.10.9 usa `FP` como nombre de parámetro en
   `fl/stl/json.h`.

Los archivos nuevos (`serial_monitor.h/.cpp`) no van en el parche: `build.ps1` los copia desde
`src/` del repo.

Para hacerlo a mano:

```powershell
git clone https://github.com/pr3y/Bruce.git
cd Bruce
git checkout a59213f3ec6cd302fa32c2042b2700c9c24d991a
Copy-Item -Recurse <repo>\src\* .\src\
git apply <repo>\patches\bruce-integration.patch
```

## 5. Herramientas y versiones

| Herramienta | Versión | Origen |
|---|---|---|
| Python | 3.13.15 | Instalado en Windows |
| PlatformIO Core | 6.2.0 | `pip install --user platformio` |
| truststore | 0.10.5 | `pip install --user truststore` (ver sección 7) |
| Plataforma | pioarduino `platform-espressif32` 55.03.39 | La define `platformio.ini` de Bruce |
| Framework Arduino ESP32 | 3.3.9, con ESP-IDF 5.5.4 | Lo baja PlatformIO |
| Compilador | `xtensa-esp-elf` GCC 14.2.0 | Lo baja PlatformIO |
| SCons | 4.11.1 (`tool-scons`) | Lo baja PlatformIO |
| esptool | 5.3.0 | Viene con PlatformIO (`%USERPROFILE%\.platformio\penv\Scripts\esptool.exe`) |
| git | 2.56 | Para bajar Bruce |

Las librerías de Bruce (FastLED, RadioLib, IRremoteESP8266, etc.) las baja PlatformIO según
`lib_deps`. La única que se fijó a mano es FastLED, como se explica en la sección 4.

## 6. Compilación

Todo el proceso está en `tools/build.ps1`. Equivale a:

```powershell
# 1. Bruce en %USERPROFILE%\src\Bruce (fuera de Documentos, ver sección 7)
# 2. Copiar src\ y aplicar el parche (sección 4)
# 3. Compilar:
$env:PYTHONPATH = '<repo>\tools\pio-winstore-site'
python <repo>\tools\pio_winstore.py run -e m5stack-cardputer -d $env:USERPROFILE\src\Bruce
```

- `pio_winstore.py` es `platformio` con `truststore` activado (ver sección 7).
- Al terminar, el script `build.py` de Bruce une bootloader, tabla de particiones y aplicación
  en `Bruce-m5stack-cardputer.bin` (unos 4 MB), que se graba en la dirección `0x0`.
  `build.ps1` lo copia a `firmware/`.
- Resultado de referencia: RAM 37,1 %, flash 49 % (4,1 MB de 8 MB).
- La primera compilación tarda bastante, porque baja unos 2 GB de herramientas. Las siguientes
  tardan entre 2 y 3 minutos.

**Particiones** (`custom_8Mb.csv`): nvs en `0x9000`, aplicación (factory) en `0x10000`,
spiffs en `0x4F0000`.

## 7. Problemas encontrados y cómo se resolvieron

### 7.1 Proxy corporativo y certificados SSL

**Síntoma:** PlatformIO falla con `HTTPClientError` al bajar paquetes de `registry.platformio.org`.
Con más detalle, el error es `CERTIFICATE_VERIFY_FAILED` y luego
`CA cert does not include key usage extension`.

**Causa:** el proxy de la red corporativa abre las conexiones HTTPS y las vuelve a firmar con una
CA propia. Esa CA está instalada en Windows, pero Python usa su propia lista de certificados
(`certifi`), donde no figura. Además, Python 3.13 rechaza esa CA porque no tiene la extensión
"key usage".

**Solución:** usar [`truststore`](https://pypi.org/project/truststore/) para que Python valide
contra el almacén de certificados de Windows, que es lo que hacen `pip` y el navegador. La
verificación HTTPS sigue completa: no se desactiva nada, solo cambia la lista de certificados
confiables.

- `tools/pio_winstore.py`: llama a `truststore.inject_into_ssl()` y después ejecuta PlatformIO.
- `tools/pio-winstore-site/sitecustomize.py`: hace lo mismo en los procesos hijos (SCons y los
  scripts de Bruce). Se activa poniendo esa carpeta en `PYTHONPATH`. Hace falta porque
  `patch.py` de Bruce llama a un servicio web para minificar las páginas del firmware.

No hay que desactivar la verificación SSL (`strict_ssl` en `false`, `verify=False`, etc.).

### 7.2 "Acceso controlado a carpetas" de Windows Defender

Defender bloquea que git, bash o Python creen carpetas y archivos bajo `C:\Documentos\…`.
Por eso el árbol de Bruce y la compilación van en `%USERPROFILE%\src\Bruce`, que se puede cambiar
con `build.ps1 -BruceDir`. El repo en sí se puede editar normalmente.

### 7.3 Compilador descomprimido en una carpeta de más

**Síntoma:** `"xtensa-esp32s3-elf-g++" no se reconoce como un comando…`

**Causa:** una descarga cortada dejó el compilador en
`.platformio\packages\toolchain-xtensa-esp-elf\xtensa-esp-elf\bin` en lugar de
`…\toolchain-xtensa-esp-elf\bin`.

**Solución:** subir el contenido de esa carpeta un nivel. Ojo: adentro hay otra carpeta
`xtensa-esp-elf` (las cabeceras y `as.exe`), que tiene que quedar como
`…\toolchain-xtensa-esp-elf\xtensa-esp-elf`. Otra opción es borrar
`%USERPROFILE%\.platformio\packages\toolchain-xtensa-esp-elf` y dejar que PlatformIO lo baje de
nuevo.

### 7.4 FastLED 3.10.9 no compila

**Síntoma:** errores en `FastLED/src/fl/stl/json.h` (`expected ')' before 'value'`).

**Solución:** fijar FastLED 3.10.3 (sección 4).

### 7.5 `cannot find -lnet80211`

**Causa:** `patch.py` de Bruce modifica `libnet80211.a` del framework con `objcopy` para debilitar
`ieee80211_raw_frame_sanity_check`, que Bruce necesita para sus funciones de WiFi. Si ese paso
falla, el script igual renombra el original a `.old` y se marca como hecho, así que la librería
queda faltando.

**Solución:** `build.ps1` lo detecta y la regenera:

```powershell
xtensa-esp32s3-elf-objcopy --weaken-symbol=ieee80211_raw_frame_sanity_check libnet80211.a.old libnet80211.a
```

Se hace en `%USERPROFILE%\.platformio\packages\framework-arduinoespressif32-libs\esp32s3\lib`.

### 7.6 `No module named platformio` en otra consola

PlatformIO se instaló con `pip install --user`, que queda en el perfil del usuario. Si la consola
corre con otro usuario (por ejemplo, como administrador), no lo encuentra. Para grabar no hace
falta: `flash.ps1` usa directamente `esptool.exe` con su ruta completa, que se puede indicar con
`-Esptool`.

## 8. Grabación y verificación

```powershell
.\tools\flash.ps1 -Port COM4
# equivale a:
esptool --chip esp32s3 --port COM4 --baud 921600 write-flash 0x0 firmware\Bruce-m5stack-cardputer.bin
```

Para comprobar que quedó grabado:

- **Consola de Bruce:** abrir el puerto COM a 115200 y mandar `info`. Una compilación propia
  responde `Bruce vdev` / `Homebrew` y la versión del SDK (`v5.5.4`).
- **Contenido de la flash:**

  ```powershell
  esptool --chip esp32s3 --port COM4 verify-flash 0x8000 <Bruce>\.pio\build\m5stack-cardputer\partitions.bin 0x10000 <Bruce>\.pio\build\m5stack-cardputer\firmware.bin
  ```

  Si se verifica la imagen completa en `0x0`, da diferencia. Es normal: al grabar, esptool
  ajusta la cabecera del bootloader (tamaño y modo de la flash). Por eso se verifican la tabla de
  particiones y la aplicación por separado.

## 9. Limitaciones e ideas

- La configuración no se guarda: se pierde al reiniciar. Se podría guardar en la flash o en la SD.
- No hay 1,5 bits de stop: Arduino ESP32 no tiene constante para eso.
- En modo USB, si se abre el menú con ESC, Bruce puede mandar por USB alguna línea de diagnóstico
  (`Selected: …`), porque `loopOptions()` escribe en `Serial`.
- Con letra grande entran unos 20 caracteres por línea y 5 líneas de texto recibido.
- Se podría agregar: guardar lo recibido en la SD, enviar archivos, control de flujo RTS/CTS.

## 10. Licencia

Bruce se distribuye bajo **AGPL-3.0**. Esta app se compila dentro de Bruce y el firmware
resultante es una obra derivada, así que tanto el código de este repo como el `.bin` publicado
quedan bajo los términos de esa licencia (por ejemplo, publicar el código fuente junto con el
binario).
