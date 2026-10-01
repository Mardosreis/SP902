#include <Arduino.h>
#include <USBHost_t36.h> // Librería exclusiva del Teensy 3.6/4.x para actuar como "computadora" (Host USB)

/* ==============================================================================
   1. CONFIGURACIÓN DEL PUERTO USB Y OBJETOS (ECU)
   ============================================================================== */
USBHost myusb;               // Inicializa el hardware USB Host del Teensy
USBHub hub1(myusb);          // Soporte por si la ECU se conecta a través de un hub USB
USBSerial ecuSerial(myusb);  // Crea el puerto serie virtual sobre la conexión USB

const uint32_t ECU_BAUDRATE = 115200;            // Velocidad de la ECU
const unsigned long ECU_PERIODO_MS = 40;         // Pide datos cada 40 ms (25 Hz)
const unsigned long ECU_OK_TIMEOUT_MS = 1000;    // 1 segundo sin datos = "SIN DATOS"
const unsigned long ECU_RESYNC_TIMEOUT_MS = 2000;// 2 segundos trabado = Resetea memoria
const unsigned long DEBUG_PERIODO_MS = 200;      // Actualiza pantalla cada 200 ms (5 Hz)
const int ECU_BUFFER_MAX = 2048;                 // Tamaño máximo de la memoria temporal

uint8_t buffer[ECU_BUFFER_MAX]; 
int bufIndex = 0;               

// Semáforo de estado de conexión
enum EstadoECU { ECU_DESCONECTADA, ECU_HANDSHAKE, ECU_ACTIVA };
EstadoECU estadoEcu = ECU_DESCONECTADA;
unsigned long tHandshake = 0; 

// Contadores y timers de la ECU
bool ecuOk = false;       
bool huboFrame = false;   
unsigned long ultimoPedido = 0;  
unsigned long ultimoFrameOk = 0; 
unsigned long ultimoResync = 0;  
unsigned long ultimoDebug = 0;   
uint32_t framesOk = 0, framesRechazados = 0, framesOtros = 0, lastFramesOk = 0;     

// Telemetría del motor
int16_t  rpm = 0;                
float    tps = 0.0;              
float    afr = 0.0;              
float    temp_refrigerante = 0.0;
float    temp_aire = 0.0;        
float    bateria_v = 0.0;        

/* ==============================================================================
   2. CONFIGURACIÓN DEL EXTENSÓMETRO (Ej: Pushrod Delantero Izquierdo)
   ============================================================================== */
const int PIN_EXT1 = A0;                   // Pin analógico de lectura
const unsigned long EXT_PERIODO_MS = 10;   // Lee el sensor cada 10 ms (100 Hz)
unsigned long ultimoLecturaExt1 = 0;

// Variables de calibración del chasis (Ajustar en el taller con pesas)
const float EXT1_CERO = 2048.0;            // Centro crudo (Sin fuerza aplicada)
const float EXT1_FACTOR = 0.52;            // Multiplicador crudo -> Kg/Newtons
const float EXT_SUAVIZADO = 0.2;           // Filtro EMA (0.1 = muy suave, 1.0 = crudo)

// Datos resultantes
float ext1_raw_filtrado = 2048.0;          
float ext1_fuerza = 0.0;                   
bool ext1_saturado = false;                // Bandera de error físico en el sensor

/* ==============================================================================
   3. FUNCIONES DE LA ECU (Armado, Decodificado y Procesamiento)
   ============================================================================== */
void enviarPedido(uint8_t cmd1, uint8_t cmd2, uint8_t cmd3) {
    uint8_t base[] = {0xFF, 0x7F, cmd1, cmd2, cmd3, 10, 0x7F, 0xFF};
    uint32_t chk = 0;
    for (int i = 0; i < 8; i++) chk += base[i];

    uint8_t trama[12] = {
        0xFF, 0x7F, cmd1, cmd2,                       
        (uint8_t)(chk & 0xFF),                        
        (uint8_t)((chk >> 8) & 0xFF),                 
        (uint8_t)((chk >> 16) & 0xFF),                
        (uint8_t)((chk >> 24) & 0xFF),                
        cmd3, 10,                                     
        0x7F, 0xFF                                    
    };
    if (ecuSerial) ecuSerial.write(trama, 12);
}

void decodificarTrama(int ini, int fin) {
    int trama_len = (fin + 2) - ini; 
    if (trama_len >= 36) {
        if (buffer[ini + 2] == 180) { // ID 180 = Paquete de telemetría
            int16_t tmp_rpm = (int16_t)((buffer[ini + 18] << 8) | buffer[ini + 19]); 
            float tmp_bateria = (int16_t)((buffer[ini + 32] << 8) | buffer[ini + 33]) / 100.0;
            
            // Filtro de plausibilidad (evita picos locos por ruido)
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
            }
        } else {
            framesOtros++; 
        }
    }
}

void procesarBuffer() {
    while (bufIndex >= 36) { 
        int ini = -1;
        // 1. Busca inicio (FF 7F)
        for (int i = 0; i < bufIndex - 1; i++) {
            if (buffer[i] == 0xFF && buffer[i+1] == 0x7F) {
                ini = i; break;
            }
        }
        if (ini == -1) {
            if (buffer[bufIndex - 1] == 0xFF) { buffer[0] = 0xFF; bufIndex = 1; } 
            else { bufIndex = 0; }
            return;
        }
        // 2. Alinea el paquete al principio
        if (ini > 0) {
            memmove(buffer, buffer + ini, bufIndex - ini);
            bufIndex -= ini;
            ini = 0; 
        }
        // 3. Busca fin (7F FF)
        int start_search = 34; 
        if (start_search > bufIndex - 2) return; 

        int fin = -1;
        for (int i = start_search; i < bufIndex - 1; i++) {
            if (buffer[i] == 0x7F && buffer[i+1] == 0xFF) {
                fin = i; break;
            }
        }
        if (fin == -1) return; 
        
        // 4. Decodifica y borra
        decodificarTrama(0, fin); 
        memmove(buffer, buffer + fin + 2, bufIndex - (fin + 2));
        bufIndex -= (fin + 2);
    }
}

void gestionarConexionECU(unsigned long ahora) {
    if (!ecuSerial) {
        if (estadoEcu != ECU_DESCONECTADA) {
            estadoEcu = ECU_DESCONECTADA; 
            ecuOk = false; huboFrame = false; bufIndex = 0; 
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
        while (ecuSerial.available()) ecuSerial.read(); // Limpia basura de inicio
        bufIndex = 0; huboFrame = false;
        ultimoFrameOk = ahora; ultimoResync = ahora;
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

    // Sistemas de auto-recuperación ante ruido o cuelgues
    if (bufIndex > 0 && (ahora - ultimoFrameOk) > ECU_RESYNC_TIMEOUT_MS && (ahora - ultimoResync) > ECU_RESYNC_TIMEOUT_MS) {
        if (Serial) Serial.println("[ECU] Sin frames válidos hace 2s, purgando buffer...");
        bufIndex = 0; ultimoResync = ahora; 
    } 
    else if (bufIndex >= ECU_BUFFER_MAX) {
        if (Serial) Serial.println("[ECU] Buffer físico excedido sin sync, purgando...");
        bufIndex = 0; ultimoResync = ahora;
    }
}

/* ==============================================================================
   4. FUNCIONES DE CHASIS (Extensómetros)
   ============================================================================== */
void tareaExtensometro1(unsigned long ahora) {
    if (ahora - ultimoLecturaExt1 < EXT_PERIODO_MS) return;
    ultimoLecturaExt1 = ahora;

    int lectura_cruda = analogRead(PIN_EXT1);

    // Detección de cable cortado o tope físico mecánico
    ext1_saturado = (lectura_cruda <= 5 || lectura_cruda >= 4090);

    // Filtro Matemático (Exponential Moving Average)
    ext1_raw_filtrado = (lectura_cruda * EXT_SUAVIZADO) + (ext1_raw_filtrado * (1.0 - EXT_SUAVIZADO));
    
    // Escala final
    ext1_fuerza = (ext1_raw_filtrado - EXT1_CERO) * EXT1_FACTOR;
}

/* ==============================================================================
   5. SETUP Y LOOP PRINCIPAL
   ============================================================================== */
void setup() {
    Serial.begin(115200); 
    
    // Configuración avanzada del hardware interno del Teensy
    analogReadResolution(12); // Pasa de 10-bits a 12-bits de resolución (0-4095)
    analogReadAveraging(4);   // Limpia picos de ruido electromagnético por hardware
    pinMode(PIN_EXT1, INPUT);
    
    myusb.begin();            // Inicializa los chips USB de la placa
    delay(200);               // Respiro vital para que las tensiones del USB se estabilicen
}

void loop() {
    // Tarea crítica de hardware interno
    myusb.Task();
    
    unsigned long ahora = millis();

    // Tareas del Motor (ECU)
    gestionarConexionECU(ahora);
    tareaECU(ahora);
    
    // Tareas del Chasis (Suspensión)
    tareaExtensometro1(ahora);
    
    // Debug y Consola Visual (Corre a 5Hz)
    if (Serial && (ahora - ultimoDebug >= DEBUG_PERIODO_MS)) {
        
        float hz_efectivos = 0.0;
        if (ahora > ultimoDebug) hz_efectivos = (float)(framesOk - lastFramesOk) * (1000.0 / (ahora - ultimoDebug));
        lastFramesOk = framesOk;

        const char* estadoStr = (estadoEcu == ECU_DESCONECTADA) ? "DESCONECTADA" :
                                (estadoEcu == ECU_HANDSHAKE) ? "HANDSHAKE" :
                                (ecuOk) ? "OK" : "SIN DATOS";

        if (ecuOk) {
            Serial.printf("ECU %-12s | Hz: %4.1f | OK: %lu | Rech: %lu | Otr: %lu | RPM: %d | TPS: %.1f%% | VBat: %.2f V | Ext1: %.2f%s\n", 
                          estadoStr, hz_efectivos, framesOk, framesRechazados, framesOtros, rpm, tps, bateria_v, 
                          ext1_fuerza, ext1_saturado ? " [SAT]" : "");
        } else {
            Serial.printf("ECU %-12s | Hz: %4.1f | OK: %lu | Rech: %lu | Otr: %lu | RPM: %d | VBat: %.2f V | Ext1: %.2f%s\n", 
                          estadoStr, hz_efectivos, framesOk, framesRechazados, framesOtros, rpm, bateria_v, 
                          ext1_fuerza, ext1_saturado ? " [SAT]" : "");
        }
        ultimoDebug = ahora; 
    }
}
