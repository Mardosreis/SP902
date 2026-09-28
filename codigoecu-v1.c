#include <Arduino.h>
#include <USBHost_t36.h>

USBHost myusb;
USBHub hub1(myusb);
USBSerial ecuSerial(myusb);

const uint32_t ECU_BAUDRATE = 115200; 
const unsigned long ECU_PERIODO_MS = 40;        // 25 Hz
const unsigned long ECU_OK_TIMEOUT_MS = 1000;   
const unsigned long ECU_RESYNC_TIMEOUT_MS = 2000; 
const unsigned long DEBUG_PERIODO_MS = 200;     // 5 Hz
const int ECU_BUFFER_MAX = 2048; 

uint8_t buffer[ECU_BUFFER_MAX];
int bufIndex = 0;

// Máquina de estados no bloqueante
enum EstadoECU { ECU_DESCONECTADA, ECU_HANDSHAKE, ECU_ACTIVA };
EstadoECU estadoEcu = ECU_DESCONECTADA;
unsigned long tHandshake = 0;

// Variables de estado
bool ecuOk = false;
bool huboFrame = false;          

unsigned long ultimoPedido = 0;
unsigned long ultimoFrameOk = 0;
unsigned long ultimoResync = 0;  
unsigned long ultimoDebug = 0;

// Contadores de diagnóstico
uint32_t framesOk = 0;
uint32_t framesRechazados = 0;
uint32_t framesOtros = 0;
uint32_t lastFramesOk = 0; // Para el cálculo de Hz efectivos

// Datos decodificados
int16_t  rpm = 0;
float    tps = 0.0;
float    afr = 0.0;
float    temp_refrigerante = 0.0;
float    temp_aire = 0.0;
float    bateria_v = 0.0;

void enviarPedido(uint8_t cmd1, uint8_t cmd2, uint8_t cmd3) {
    uint8_t base[] = {0xFF, 0x7F, cmd1, cmd2, cmd3, 10, 0x7F, 0xFF};
    uint32_t chk = 0;
    for (int i = 0; i < 8; i++) chk += base[i];

    uint8_t trama[12] = {
        0xFF, 0x7F, cmd1, cmd2, 
        (uint8_t)(chk & 0xFF), (uint8_t)((chk >> 8) & 0xFF), 
        (uint8_t)((chk >> 16) & 0xFF), (uint8_t)((chk >> 24) & 0xFF), 
        cmd3, 10, 0x7F, 0xFF
    };

    if (ecuSerial) ecuSerial.write(trama, 12);
}

void decodificarTrama(int ini, int fin) {
    int trama_len = (fin + 2) - ini;
    
    if (trama_len >= 36) {
        if (buffer[ini + 2] == 180) {
            int16_t tmp_rpm = (int16_t)((buffer[ini + 18] << 8) | buffer[ini + 19]); 
            float tmp_bateria = (int16_t)((buffer[ini + 32] << 8) | buffer[ini + 33]) / 100.0;
            
            // Filtro de plausibilidad
            if (tmp_rpm >= 0 && tmp_rpm < 18000 && tmp_bateria >= 5.0 && tmp_bateria <= 20.0) {
                rpm = tmp_rpm;
                bateria_v = tmp_bateria;
                afr = buffer[ini + 20] / 10.0; 
                tps = (int16_t)((buffer[ini + 24] << 8) | buffer[ini + 25]) / 10.0;
                temp_refrigerante = (int16_t)((buffer[ini + 28] << 8) | buffer[ini + 29]) / 10.0;
                temp_aire = (int16_t)((buffer[ini + 30] << 8) | buffer[ini + 31]) / 10.0;

                ultimoFrameOk = millis(); 
                huboFrame = true;
                framesOk++;
            } else {
                framesRechazados++;
                // En caso de purgas constantes de 2s sin errores de cableado, 
                // revisar los offsets crudos acá:
                // if (Serial) Serial.printf("[DEBUG] Rechazado -> RPM crudo: %d | VBat crudo: %.2f\n", tmp_rpm, tmp_bateria);
            }
        } else {
            framesOtros++;
        }
    }
}

void procesarBuffer() {
    while (bufIndex >= 36) { 
        int ini = -1;
        for (int i = 0; i < bufIndex - 1; i++) {
            if (buffer[i] == 0xFF && buffer[i+1] == 0x7F) {
                ini = i;
                break;
            }
        }
        
        if (ini == -1) {
            if (buffer[bufIndex - 1] == 0xFF) { buffer[0] = 0xFF; bufIndex = 1; } 
            else { bufIndex = 0; }
            return;
        }
        
        if (ini > 0) {
            memmove(buffer, buffer + ini, bufIndex - ini);
            bufIndex -= ini;
            ini = 0; 
        }
        
        int start_search = 34; 
        if (start_search > bufIndex - 2) return; 

        int fin = -1;
        for (int i = start_search; i < bufIndex - 1; i++) {
            if (buffer[i] == 0x7F && buffer[i+1] == 0xFF) {
                fin = i;
                break;
            }
        }
        
        if (fin == -1) return; 
        
        decodificarTrama(0, fin); 
        
        memmove(buffer, buffer + fin + 2, bufIndex - (fin + 2));
        bufIndex -= (fin + 2);
    }
}

void gestionarConexionECU(unsigned long ahora) {
    if (!ecuSerial) {
        if (estadoEcu != ECU_DESCONECTADA) {
            estadoEcu = ECU_DESCONECTADA;
            ecuOk = false; 
            huboFrame = false; 
            bufIndex = 0;
            if (Serial) Serial.println("[ECU] Desconectada");
        }
        return;
    }
    
    if (estadoEcu == ECU_DESCONECTADA) {
        ecuSerial.begin(ECU_BAUDRATE);
        enviarPedido(0, 0, 0); 
        tHandshake = ahora;
        estadoEcu = ECU_HANDSHAKE;
    } 
    else if (estadoEcu == ECU_HANDSHAKE && (ahora - tHandshake) >= 50) {
        while (ecuSerial.available()) ecuSerial.read(); 
        bufIndex = 0; 
        huboFrame = false;
        ultimoFrameOk = ahora; 
        ultimoResync = ahora;
        estadoEcu = ECU_ACTIVA;
        if (Serial) Serial.println("[ECU] Conectada (USB Serial Host)");
    }
}

void tareaECU(unsigned long ahora) {
    if (estadoEcu != ECU_ACTIVA) return;

    if (ahora - ultimoPedido >= ECU_PERIODO_MS) {
        enviarPedido(6, 0, 0); 
        ultimoPedido = ahora;
    }

    while (ecuSerial.available()) {
        if (bufIndex < ECU_BUFFER_MAX) buffer[bufIndex++] = ecuSerial.read();
        else ecuSerial.read(); 
    }

    procesarBuffer();

    ahora = millis(); 
    ecuOk = huboFrame && (ahora - ultimoFrameOk <= ECU_OK_TIMEOUT_MS);

    if (bufIndex > 0 && 
        (ahora - ultimoFrameOk) > ECU_RESYNC_TIMEOUT_MS && 
        (ahora - ultimoResync) > ECU_RESYNC_TIMEOUT_MS) {
        if (Serial) Serial.println("[ECU] Sin frames válidos hace 2s, purgando buffer...");
        bufIndex = 0;
        ultimoResync = ahora; 
    } 
    else if (bufIndex >= ECU_BUFFER_MAX) {
        if (Serial) Serial.println("[ECU] Buffer físico excedido sin sync, purgando...");
        bufIndex = 0;
        ultimoResync = ahora;
    }
}

void setup() {
    Serial.begin(115200);
    myusb.begin();
    delay(200); 
}

void loop() {
    myusb.Task();
    unsigned long ahora = millis();

    gestionarConexionECU(ahora);
    tareaECU(ahora);
    
    // Aquí irían: tareaSPI(ahora); tareaCAN(ahora); tareaSD(ahora);

    if (Serial && (ahora - ultimoDebug >= DEBUG_PERIODO_MS)) {
        float hz_efectivos = 0.0;
        if (ahora > ultimoDebug) {
            hz_efectivos = (float)(framesOk - lastFramesOk) * (1000.0 / (ahora - ultimoDebug));
        }
        lastFramesOk = framesOk;

        const char* estadoStr;
        if (estadoEcu == ECU_DESCONECTADA) estadoStr = "DESCONECTADA";
        else if (estadoEcu == ECU_HANDSHAKE) estadoStr = "HANDSHAKE";
        else if (ecuOk) estadoStr = "OK";
        else estadoStr = "SIN DATOS";

        if (ecuOk) {
            Serial.printf("ECU %-12s | Hz: %4.1f | OK: %lu | Rech: %lu | Otr: %lu | RPM: %d | TPS: %.1f%% | VBat: %.2f V\n", 
                          estadoStr, hz_efectivos, framesOk, framesRechazados, framesOtros, rpm, tps, bateria_v);
        } else {
            Serial.printf("ECU %-12s | Hz: %4.1f | OK: %lu | Rech: %lu | Otr: %lu | RPM: %d | VBat: %.2f V\n", 
                          estadoStr, hz_efectivos, framesOk, framesRechazados, framesOtros, rpm, bateria_v);
        }
        ultimoDebug = ahora;
    }
}
