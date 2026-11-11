/**
 * @file serial_monitor.cpp
 * @brief Monitor serie (UART) para Bruce.
 *
 * Puertos:
 *  - USB (por defecto): el USB-C del equipo (en Windows aparece como COMx).
 *    Es USB CDC: velocidad/paridad/bits los fija el programa de la PC, no aplican aca.
 *    Mientras la terminal esta abierta se suspende la consola de comandos de Bruce.
 *  - Grove UART: el UART configurado en Config > UART Pins (por defecto en el
 *    Cardputer RX = G1, TX = G2). Cruzar TX/RX y unir GND. Niveles 3.3V TTL:
 *    NO conectar directo a RS-232 (+-12V).
 *
 * Teclas en la terminal:
 *   texto + Enter : envia la linea (con el fin de linea configurado)
 *   Backspace     : borra
 *   ESC (`)       : menu (seguir / limpiar / configurar / salir)
 *   Fn + ; / Fn + . : desplazar el historial hacia arriba / abajo
 */
#include "serial_monitor.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/serialcmds.h"
#include "core/settings.h"
#include "core/utils.h"
#include <HardwareSerial.h>
#include <deque>
#include <globals.h>

namespace {

// UART2: Serial1 lo usa Bruce para GPS / "Serial use UART"
HardwareSerial smUart(2);

const uint32_t BAUDS[] = {300,   600,   1200,   2400,   4800,   9600,   14400,
                          19200, 38400, 57600,  115200, 230400, 460800, 921600};
const char *PARITY_NAMES[] = {"None", "Even", "Odd"};
const char PARITY_CHR[] = {'N', 'E', 'O'};
const char *EOL_NAMES[] = {"None", "CR", "LF", "CR+LF"};
const char *EOL_SEQ[] = {"", "\r", "\n", "\r\n"};

// [data bits 5..8][paridad N/E/O][stop 1/2]
const uint32_t SERIAL_CFG[4][3][2] = {
    {{SERIAL_5N1, SERIAL_5N2}, {SERIAL_5E1, SERIAL_5E2}, {SERIAL_5O1, SERIAL_5O2}},
    {{SERIAL_6N1, SERIAL_6N2}, {SERIAL_6E1, SERIAL_6E2}, {SERIAL_6O1, SERIAL_6O2}},
    {{SERIAL_7N1, SERIAL_7N2}, {SERIAL_7E1, SERIAL_7E2}, {SERIAL_7O1, SERIAL_7O2}},
    {{SERIAL_8N1, SERIAL_8N2}, {SERIAL_8E1, SERIAL_8E2}, {SERIAL_8O1, SERIAL_8O2}},
};

// Configuracion (se conserva mientras el equipo este encendido)
bool cfgUsb = true; // true = USB (por defecto), false = Grove UART
Stream *port = &Serial;
bool cmdsSuspended = false;
uint32_t cfgBaud = 115200;
uint8_t cfgDataBits = 8; // 5..8
uint8_t cfgParity = 0;   // 0=N 1=E 2=O
uint8_t cfgStopBits = 1; // 1 o 2
uint8_t cfgEol = 3;      // CR+LF
bool cfgEcho = true;
bool cfgHex = false;
uint8_t cfgFont = 2; // tamanio de letra de la terminal (1 = chico, 2 = doble)

// Pantalla (fuente tamanio 1: 6x8 px)
// Pantalla: la fuente base es de 6x8 px, escalada por cfgFont
int CW = 6 * 2;
int CH = 8 * 2;
const size_t MAX_LINES = 300;

std::deque<String> lines;
String rxLine;      // linea recibida en curso (todavia sin \n)
String txLine;      // lo que se esta escribiendo
int scrollBack = 0; // lineas desplazadas hacia arriba
bool dirty = true;
bool lastWasCR = false;
uint32_t rxCount = 0, txCount = 0;

String cfgShort() {
    if (cfgUsb) return "USB";
    return String(cfgBaud) + " " + String(cfgDataBits) + PARITY_CHR[cfgParity] + String(cfgStopBits);
}

int cols() { return (tftWidth - 2) / CW; }

void applyFont() {
    CW = 6 * cfgFont;
    CH = 8 * cfgFont;
}

void pushLine(const String &s) {
    // corta en lineas del ancho de la pantalla
    int c = cols();
    if (s.length() == 0) {
        lines.push_back("");
    } else {
        for (int i = 0; i < (int)s.length(); i += c) lines.push_back(s.substring(i, i + c));
    }
    while (lines.size() > MAX_LINES) lines.pop_front();
    dirty = true;
}

void flushRxLine() {
    pushLine(rxLine);
    rxLine = "";
}

void onRxByte(uint8_t b) {
    rxCount++;
    if (cfgHex) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", b);
        rxLine += buf;
        if ((int)rxLine.length() + 3 > cols()) flushRxLine();
        dirty = true;
        return;
    }
    if (b == '\r') {
        flushRxLine();
        lastWasCR = true;
        return;
    }
    if (b == '\n') {
        if (!lastWasCR) flushRxLine();
        lastWasCR = false;
        return;
    }
    lastWasCR = false;
    if (b == '\t') rxLine += "  ";
    else if (b >= 0x20 && b < 0x7F) rxLine += (char)b;
    else if (b == 0x08) {
        if (rxLine.length()) rxLine.remove(rxLine.length() - 1);
    } else rxLine += '.';
    if ((int)rxLine.length() >= cols()) flushRxLine();
    dirty = true;
}

void releaseUsb() {
    if (cmdsSuspended && serialcmdsTaskHandle) vTaskResume(serialcmdsTaskHandle);
    cmdsSuspended = false;
}

bool openUart() {
    smUart.end();
    if (cfgUsb) {
        // que la consola de comandos de Bruce no se coma lo que llega por USB
        if (serialcmdsTaskHandle && !cmdsSuspended) {
            vTaskSuspend(serialcmdsTaskHandle);
            cmdsSuspended = true;
        }
        while (Serial.available()) Serial.read();
        port = &Serial;
        return true;
    }
    releaseUsb();
    int rx = bruceConfigPins.uart_bus.rx;
    int tx = bruceConfigPins.uart_bus.tx;
    if (rx < 0 || tx < 0) {
        displayError("UART pins not set", true);
        return false;
    }
    smUart.setRxBufferSize(4096);
    smUart.begin(cfgBaud, SERIAL_CFG[cfgDataBits - 5][cfgParity][cfgStopBits - 1], rx, tx);
    port = &smUart;
    return true;
}

void render() {
    const int W = tftWidth;
    const int H = tftHeight;
    const int topH = CH + 3;            // barra de estado
    const int inY = H - CH - 2;         // linea de entrada
    const int areaY = topH + 1;         // inicio area recibida
    const int areaH = inY - 2 - areaY;  // alto area recibida
    const int nRows = areaH / CH;

    tft.setTextSize(cfgFont);
    tft.setTextWrap(false);

    // barra superior
    tft.fillRect(0, 0, W, topH, bruceConfig.priColor);
    tft.setTextColor(bruceConfig.bgColor, bruceConfig.priColor);
    tft.setCursor(2, 2);
    String st = cfgShort() + (cfgHex ? " HEX" : "") + " R" + String(rxCount) + " T" + String(txCount);
    if (scrollBack > 0) st += " ^" + String(scrollBack);
    tft.print(st.substring(0, cols()));

    // area de recepcion: ultimas lineas + linea en curso
    tft.fillRect(0, areaY, W, areaH + 1, bruceConfig.bgColor);
    std::vector<const String *> view;
    int total = lines.size() + (rxLine.length() ? 1 : 0);
    int maxScroll = total > nRows ? total - nRows : 0;
    if (scrollBack > maxScroll) scrollBack = maxScroll;
    int end = total - scrollBack; // exclusivo
    int start = end - nRows;
    if (start < 0) start = 0;
    int y = areaY;
    for (int i = start; i < end; i++) {
        const String &s = (i < (int)lines.size()) ? lines[i] : rxLine;
        bool isTx = s.startsWith("> ");
        tft.setTextColor(isTx ? TFT_YELLOW : bruceConfig.priColor, bruceConfig.bgColor);
        tft.setCursor(1, y);
        tft.print(s);
        y += CH;
    }

    // linea de entrada
    tft.drawFastHLine(0, inY - 2, W, bruceConfig.priColor);
    tft.fillRect(0, inY - 1, W, H - inY + 1, bruceConfig.bgColor);
    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
    tft.setCursor(1, inY);
    String shown = "> " + txLine + "_";
    int c = cols();
    if ((int)shown.length() > c) shown = shown.substring(shown.length() - c);
    tft.print(shown);

    dirty = false;
}

void sendLine() {
    String out = txLine + EOL_SEQ[cfgEol];
    port->write((const uint8_t *)out.c_str(), out.length());
    txCount += out.length();
    if (cfgEcho) {
        if (rxLine.length()) flushRxLine();
        pushLine("> " + txLine);
    }
    txLine = "";
    scrollBack = 0;
    dirty = true;
}

// ---------- menus de configuracion ----------

void chooseBaud() {
    std::vector<Option> opts;
    for (uint32_t b : BAUDS) opts.push_back({String(b), [b]() { cfgBaud = b; }, cfgBaud == b});
    opts.push_back({"Custom...", []() {
                        String v = num_keyboard(String(cfgBaud), 8, "Baud rate:");
                        long n = v.toInt();
                        if (n >= 50 && n <= 5000000) cfgBaud = n;
                        else displayError("Invalid baud", true);
                    }});
    int idx = 0;
    for (size_t i = 0; i < opts.size(); i++)
        if (opts[i].selected) idx = i;
    loopOptions(opts, MENU_TYPE_SUBMENU, "Baud rate", idx);
}

void chooseDataBits() {
    std::vector<Option> opts;
    for (uint8_t d = 5; d <= 8; d++)
        opts.push_back({String(d), [d]() { cfgDataBits = d; }, cfgDataBits == d});
    loopOptions(opts, MENU_TYPE_SUBMENU, "Data bits", cfgDataBits - 5);
}

void chooseParity() {
    std::vector<Option> opts;
    for (uint8_t p = 0; p < 3; p++)
        opts.push_back({PARITY_NAMES[p], [p]() { cfgParity = p; }, cfgParity == p});
    loopOptions(opts, MENU_TYPE_SUBMENU, "Parity", cfgParity);
}

void chooseStopBits() {
    std::vector<Option> opts = {
        {"1", []() { cfgStopBits = 1; }, cfgStopBits == 1},
        {"2", []() { cfgStopBits = 2; }, cfgStopBits == 2},
    };
    loopOptions(opts, MENU_TYPE_SUBMENU, "Stop bits", cfgStopBits - 1);
}

void chooseEol() {
    std::vector<Option> opts;
    for (uint8_t e = 0; e < 4; e++) opts.push_back({EOL_NAMES[e], [e]() { cfgEol = e; }, cfgEol == e});
    loopOptions(opts, MENU_TYPE_SUBMENU, "Line ending", cfgEol);
}

// Devuelve true si hay que iniciar la terminal, false para salir
bool configMenu() {
    int idx = 0;
    while (true) {
        bool start = false, quit = false;
        std::vector<Option> opts = {
            {"Start",                                            [&]() { start = true; }     },
            {String("Port: ") + (cfgUsb ? "USB" : "Grove UART"), []() { cfgUsb = !cfgUsb; }  },
        };
        if (!cfgUsb) {
            // en USB CDC la velocidad/paridad/bits los define la PC
            opts.push_back({"Baud: " + String(cfgBaud), chooseBaud});
            opts.push_back({"Data bits: " + String(cfgDataBits), chooseDataBits});
            opts.push_back({String("Parity: ") + PARITY_NAMES[cfgParity], chooseParity});
            opts.push_back({"Stop bits: " + String(cfgStopBits), chooseStopBits});
            opts.push_back(
                {"Pins RX:" + String(bruceConfigPins.uart_bus.rx) + " TX:" + String(bruceConfigPins.uart_bus.tx),
                 []() { setUARTPinsMenu(bruceConfigPins.uart_bus); }}
            );
        }
        opts.push_back({String("Line end: ") + EOL_NAMES[cfgEol], chooseEol});
        opts.push_back({String("Local echo: ") + (cfgEcho ? "On" : "Off"), []() { cfgEcho = !cfgEcho; }});
        opts.push_back({String("RX view: ") + (cfgHex ? "HEX" : "ASCII"), []() { cfgHex = !cfgHex; }});
        opts.push_back({String("Font size: ") + (cfgFont == 2 ? "Large" : "Small"), []() {
                            cfgFont = (cfgFont == 2) ? 1 : 2;
                            // las lineas guardadas estan cortadas al ancho anterior
                            lines.clear();
                            rxLine = "";
                        }});
        opts.push_back({"Exit", [&]() { quit = true; }});
        if (idx >= (int)opts.size()) idx = 1;
        idx = loopOptions(opts, MENU_TYPE_SUBMENU, "Serial Monitor", idx);
        if (idx < 0 || quit || returnToMenu) return false;
        if (start) return true;
    }
}

// Menu dentro de la terminal (ESC). Devuelve: 0 seguir, 1 reconfigurar, 2 salir
int terminalMenu() {
    int action = 0;
    std::vector<Option> opts = {
        {"Continue",    [&]() { action = 0; }},
        {"Clear screen",
         [&]() {
             lines.clear();
             rxLine = "";
             rxCount = txCount = 0;
             scrollBack = 0;
         }                                     },
        {"Send ESC (0x1B)", [&]() { port->write((uint8_t)0x1B); txCount++; }},
        {"Send Ctrl+C (0x03)", [&]() { port->write((uint8_t)0x03); txCount++; }},
        {"Settings",    [&]() { action = 1; }},
        {"Exit",        [&]() { action = 2; }},
    };
    int r = loopOptions(opts, MENU_TYPE_SUBMENU, "Serial Monitor", 0);
    if (r < 0) return 0;
    return action;
}

void clearButtons() {
    check(EscPress);
    check(SelPress);
    check(NextPress);
    check(PrevPress);
    check(UpPress);
    check(DownPress);
}

// Devuelve true si el usuario quiere volver a la configuracion
bool terminalLoop() {
    applyFont();
    tft.fillScreen(bruceConfig.bgColor);
    dirty = true;
    unsigned long lastDraw = 0;
    clearButtons();
    KeyStroke.Clear();

    while (true) {
        // recepcion (acotada por iteracion para no congelar la UI)
        int n = 0;
        while (port->available() && n < 2048) {
            onRxByte(port->read());
            n++;
        }

        // teclado
        if (KeyStroke.pressed) {
            wakeUpScreen();
            keyStroke k = KeyStroke;
            KeyStroke.Clear();
            bool openMenu = false;

            if (k.enter) {
                sendLine();
            } else if (k.del) {
                if (txLine.length()) txLine.remove(txLine.length() - 1);
                dirty = true;
            } else {
                for (char ch : k.word) {
                    uint8_t u = (uint8_t)ch;
                    if (ch == '`') openMenu = true; // tecla ESC
                    else if (u == 0xDA) scrollBack += 3; // Fn + ;  (arriba)
                    else if (u == 0xD9) scrollBack = scrollBack > 3 ? scrollBack - 3 : 0; // Fn + .
                    else if (u == 0xB3) txLine += '\t';                                   // Tab
                    else if (u >= 0x20 && u < 0x7F) txLine += ch;
                }
                dirty = true;
            }
            clearButtons();

            if (openMenu) {
                int a = terminalMenu();
                if (a == 2) return false;
                if (a == 1) return true;
                tft.fillScreen(bruceConfig.bgColor);
                clearButtons();
                KeyStroke.Clear();
                dirty = true;
            }
        } else {
            // descartar flags de navegacion (Backspace/ESC/flechas los generan)
            clearButtons();
        }

        if (dirty && millis() - lastDraw > 40) {
            render();
            lastDraw = millis();
        }
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

} // namespace

void serial_monitor_setup() {
    while (configMenu()) {
        if (!openUart()) continue;
        bool reconfigure = terminalLoop();
        smUart.end();
        releaseUsb();
        if (!reconfigure) break;
    }
    smUart.end();
    releaseUsb();
    tft.fillScreen(bruceConfig.bgColor);
}
