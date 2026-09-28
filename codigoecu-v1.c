#include <Arduino.h>
#include <USBHost_t36.h> // Librería exclusiva del Teensy 3.6/4.x para actuar como "computadora" (Host USB)

/* ==============================================================================
   1. CONFIGURACIÓN DEL PUERTO USB Y OBJETOS
   ============================================================================== */
USBHost myusb;               // Inicializa el hardware USB Host del Teensy
USBHub hub1(myusb);          // Soporte por si la ECU se conecta a través de un hub USB
USBSerial ecuSerial(myusb);  // Crea el puerto serie virtual sobre la conexión USB

/* ==============================================================================
   2. PARÁMETROS DE COMUNICACIÓN Y TIMEOUTS
   ============================================================================== */
const uint32_t ECU_BAUDRATE = 115200;            // Velocidad de transmisión estándar de la ECU
const unsigned long ECU_PERIODO_MS = 40;         // Pide datos cada 40 ms (Equivale a 25 Hz o 25 veces por segundo)
const unsigned long ECU_OK_TIMEOUT_MS = 1000;    // Si pasa 1 segundo (1000 ms) sin datos, declara "SIN DATOS"
const unsigned long ECU_RESYNC_TIMEOUT_MS = 2000;// Si pasan 2 segundos trabado, borra la memoria y empieza de cero
const unsigned long DEBUG_PERIODO_MS = 200;      // Actualiza la pantalla/consola cada 200 ms (5 Hz) para no saturarla
const int ECU_BUFFER_MAX = 2048;                 // Tamaño máximo de la memoria temporal (buffer) para recibir datos

uint8_t buffer[ECU_BUFFER_MAX]; // El "tubo" donde van cayendo los bytes crudos que manda la ECU
int bufIndex = 0;               // Nos indica por qué posición del tubo vamos llenando

/* ==============================================================================
   3. MÁQUINA DE ESTADOS (El "Semáforo" de la conexión)
   ============================================================================== 
   Usamos una máquina de estados para no usar "delays" (pausas). Si el cable 
   vibra en pista y se desconecta, el sistema sabe en qué paso está sin bloquear 
   al resto del auto (como la lectura de suspensiones o la pantalla).
*/
enum EstadoECU { 
    ECU_DESCONECTADA, // Cable desenchufado o sin señal
    ECU_HANDSHAKE,    // Cable enchufado, diciendo "Hola" a la ECU
    ECU_ACTIVA        // Comunicación establecida y recibiendo datos
};
EstadoECU estadoEcu = ECU_DESCONECTADA;
unsigned long tHandshake = 0; // Cronómetro para el saludo inicial

/* ==============================================================================
   4. VARIABLES DE ESTADO Y CRONÓMETROS DEL SISTEMA
   ============================================================================== */
bool ecuOk = false;       // ¿La ECU está mandando datos reales y válidos ahora mismo?
bool huboFrame = false;   // Bandera que avisa si recibimos un paquete completo de datos

unsigned long ultimoPedido = 0;  // Cuándo fue la última vez que le pedimos datos a la ECU
unsigned long ultimoFrameOk = 0; // Cuándo llegó el último paquete perfecto
unsigned long ultimoResync = 0;  // Cuándo fue la última vez que reseteamos el sistema por error
unsigned long ultimoDebug = 0;   // Cuándo imprimimos en pantalla por última vez

/* ==============================================================================
   5. CONTADORES DE DIAGNÓSTICO (Para medir la salud del cableado)
   ============================================================================== */
uint32_t framesOk = 0;         // Paquetes perfectos
uint32_t framesRechazados = 0; // Paquetes que llegaron bien pero con datos imposibles (ej: 300 Volts)
uint32_t framesOtros = 0;      // Paquetes que son de otro tipo o basura electrónica
uint32_t lastFramesOk = 0;     // Auxiliar para calcular cuántos paquetes por segundo (Hz) están llegando

/* ==============================================================================
   6. DATOS DE TELEMETRÍA (Lo que realmente importa del motor)
   ============================================================================== */
int16_t  rpm = 0;                // Revoluciones por minuto del motor
float    tps = 0.0;              // Posición del acelerador (0 a 100%)
float    afr = 0.0;              // Mezcla Aire/Combustible (Air-Fuel Ratio)
float    temp_refrigerante = 0.0;// Temperatura del agua (°C)
float    temp_aire = 0.0;        // Temperatura del aire de admisión (°C)
float    bateria_v = 0.0;        // Voltaje de la batería del monoplaza

/* ==============================================================================
   FUNCIONES PRINCIPALES
   ============================================================================== */

/* 
 * enviaPedido(): Arma el paquete de bytes que le dice a la ECU "dame datos".
 * El protocolo de esta ECU exige un inicio (Header), los comandos, una suma de
 * seguridad (Checksum) para que no haya errores, y un final (Footer).
 */
void enviarPedido(uint8_t cmd1, uint8_t cmd2, uint8_t cmd3) {
    // Estructura base para calcular el checksum matemáticamente
    uint8_t base[] = {0xFF, 0x7F, cmd1, cmd2, cmd3, 10, 0x7F, 0xFF};
    uint32_t chk = 0;
    
    // Suma todos los bytes para generar el código de seguridad
    for (int i = 0; i < 8; i++) chk += base[i];

    // Ensambla el tren de bytes final
    uint8_t trama[12] = {
        0xFF, 0x7F, cmd1, cmd2,                       // Header (0xFF 0x7F) y comandos
        (uint8_t)(chk & 0xFF),                        // Checksum (Parte 1)
        (uint8_t)((chk >> 8) & 0xFF),                 // Checksum (Parte 2)
        (uint8_t)((chk >> 16) & 0xFF),                // Checksum (Parte 3)
        (uint8_t)((chk >> 24) & 0xFF),                // Checksum (Parte 4)
        cmd3, 10,                                     // Comando 3 y longitud esperada (10 bytes)
        0x7F, 0xFF                                    // Footer (0x7F 0xFF) - Fin del mensaje
    };

    // Si el cable está conectado, dispara el mensaje
    if (ecuSerial) ecuSerial.write(trama, 12);
}

/* 
 * decodificarTrama(): Traduce el paquete incomprensible de bytes en valores reales.
 * 'ini' y 'fin' marcan dónde empieza y termina el paquete útil dentro de nuestra memoria.
 */
void decodificarTrama(int ini, int fin) {
    int trama_len = (fin + 2) - ini; // Calcula el largo total del paquete recibido
    
    // Si el paquete tiene al menos 36 bytes (lo mínimo para traer toda la info)
    if (trama_len >= 36) {
        // Verifica que el byte identificador sea el correcto (180 es el ID de telemetría de esta ECU)
        if (buffer[ini + 2] == 180) {
            
            // MAGIA DE BITS: Une dos bytes de 8 bits en un solo número de 16 bits (Big Endian)
            int16_t tmp_rpm = (int16_t)((buffer[ini + 18] << 8) | buffer[ini + 19]); 
            float tmp_bateria = (int16_t)((buffer[ini + 32] << 8) | buffer[ini + 33]) / 100.0;
            
            // FILTRO DE PLAUSIBILIDAD FÍSICA
            // Evita que un pico de interferencia eléctrica ponga el motor a 90.000 RPM
            if (tmp_rpm >= 0 && tmp_rpm < 18000 && tmp_bateria >= 5.0 && tmp_bateria <= 20.0) {
                
                // Si los datos tienen sentido físico, los guardamos oficialmente
                rpm = tmp_rpm;
                bateria_v = tmp_bateria;
                afr = buffer[ini + 20] / 10.0; 
                tps = (int16_t)((buffer[ini + 24] << 8) | buffer[ini + 25]) / 10.0;
                temp_refrigerante = (int16_t)((buffer[ini + 28] << 8) | buffer[ini + 29]) / 10.0;
                temp_aire = (int16_t)((buffer[ini + 30] << 8) | buffer[ini + 31]) / 10.0;

                // Avisamos que recibimos un dato sano para reiniciar los cronómetros de error
                ultimoFrameOk = millis(); 
                huboFrame = true;
                framesOk++; // Sumamos un paquete exitoso
            } else {
                framesRechazados++; // El formato estaba bien, pero los números eran absurdos
            }
        } else {
            framesOtros++; // Era un paquete de la ECU, pero no era el de telemetría (ID distinto a 180)
        }
    }
}

/* 
 * procesarBuffer(): Es el "recolector de basura" y organizador.
 * Busca constantemente el inicio (0xFF 0x7F) y el fin (0x7F 0xFF) de los paquetes
 * dentro del caótico flujo de bytes que entra por el USB.
 */
void procesarBuffer() {
    // Mientras haya suficientes bytes como para formar al menos un paquete mínimo (36 bytes)
    while (bufIndex >= 36) { 
        int ini = -1;
        
        // PASO 1: Buscar el saludo de inicio (Header: FF 7F)
        for (int i = 0; i < bufIndex - 1; i++) {
            if (buffer[i] == 0xFF && buffer[i+1] == 0x7F) {
                ini = i;
                break;
            }
        }
        
        // Si no encontró el inicio, borra la basura para hacer espacio
        if (ini == -1) {
            // Salvedad: Si justo el último byte es un FF, lo guardamos por si el 7F llega en la próxima tanda
            if (buffer[bufIndex - 1] == 0xFF) { buffer[0] = 0xFF; bufIndex = 1; } 
            else { bufIndex = 0; }
            return;
        }
        
        // PASO 2: Alinear el paquete al principio de la memoria (para prolijidad)
        if (ini > 0) {
            memmove(buffer, buffer + ini, bufIndex - ini);
            bufIndex -= ini;
            ini = 0; 
        }
        
        // PASO 3: Buscar la despedida (Footer: 7F FF)
        // Empezamos a buscar recién en el byte 34 para no confundirnos si un valor del motor da justo 7F FF
        int start_search = 34; 
        if (start_search > bufIndex - 2) return; // Si no llegaron suficientes bytes, esperamos al próximo ciclo

        int fin = -1;
        for (int i = start_search; i < bufIndex - 1; i++) {
            if (buffer[i] == 0x7F && buffer[i+1] == 0xFF) {
                fin = i;
                break;
            }
        }
        
        if (fin == -1) return; // Aún no llegó el final del paquete, esperamos
        
        // PASO 4: Si tenemos inicio y fin, ¡lo decodificamos!
        decodificarTrama(0, fin); 
        
        // PASO 5: Borramos el paquete ya procesado para dejar lugar a los nuevos
        memmove(buffer, buffer + fin + 2, bufIndex - (fin + 2));
        bufIndex -= (fin + 2);
    }
}

/* 
 * gestionarConexionECU(): Maneja qué pasa si el cable se enchufa o desenchufa en marcha.
 * Esto permite encender la telemetría antes que el auto, o viceversa, sin que nada colapse.
 */
void gestionarConexionECU(unsigned long ahora) {
    // Si el hardware USB no detecta nada conectado
    if (!ecuSerial) {
        if (estadoEcu != ECU_DESCONECTADA) {
            estadoEcu = ECU_DESCONECTADA; // Cambiamos el estado a apagado
            ecuOk = false; 
            huboFrame = false; 
            bufIndex = 0; // Vaciamos la memoria
            if (Serial) Serial.println("[ECU] Desconectada"); // Avisamos al mecánico en boxes
        }
        return;
    }
    
    // Si se acaba de conectar el cable físicamente
    if (estadoEcu == ECU_DESCONECTADA) {
        ecuSerial.begin(ECU_BAUDRATE); // Abre el puerto a 115200 baudios
        enviarPedido(0, 0, 0);         // Manda el "Hola" a la ECU
        tHandshake = ahora;            // Anota a qué hora lo mandó
        estadoEcu = ECU_HANDSHAKE;     // Pasa a estado "Esperando saludo"
    } 
    // Si ya pasaron 50 milisegundos desde que mandamos el "Hola", ya podemos empezar
    else if (estadoEcu == ECU_HANDSHAKE && (ahora - tHandshake) >= 50) {
        while (ecuSerial.available()) ecuSerial.read(); // Limpia cualquier basura vieja que haya quedado en el cable
        bufIndex = 0; 
        huboFrame = false;
        ultimoFrameOk = ahora; 
        ultimoResync = ahora;
        estadoEcu = ECU_ACTIVA; // ¡Todo listo para correr!
        if (Serial) Serial.println("[ECU] Conectada (USB Serial Host)");
    }
}

/* 
 * tareaECU(): Es la tarea principal que corre constantemente pidiendo y leyendo datos.
 * Funciona de manera no-bloqueante (como si operara en segundo plano).
 */
void tareaECU(unsigned long ahora) {
    // Si la conexión no está activa, no hace nada y le cede el tiempo al procesador
    if (estadoEcu != ECU_ACTIVA) return;

    // ¿Ya es hora de pedir datos nuevos? (Apunta a 25 veces por segundo)
    if (ahora - ultimoPedido >= ECU_PERIODO_MS) {
        enviarPedido(6, 0, 0); // Código 6: Pide el paquete de telemetría general
        ultimoPedido = ahora;
    }

    // Lee todo lo que haya llegado por el cable USB y lo mete en nuestro tubo (buffer)
    while (ecuSerial.available()) {
        if (bufIndex < ECU_BUFFER_MAX) buffer[bufIndex++] = ecuSerial.read();
        else ecuSerial.read(); // Si por algún error fatal se llenó el tubo, tira los datos al vacío para no colapsar
    }

    // Procesa lo que haya en el tubo
    procesarBuffer();

    // Actualiza el reloj local y verifica si estamos recibiendo datos frescos
    ahora = millis(); 
    ecuOk = huboFrame && (ahora - ultimoFrameOk <= ECU_OK_TIMEOUT_MS);

    // SISTEMAS DE RECUPERACIÓN DE ERRORES:
    // 1. Si pasaron 2 segundos sin datos útiles, reseteamos la memoria por si se trabó con basura
    if (bufIndex > 0 && 
        (ahora - ultimoFrameOk) > ECU_RESYNC_TIMEOUT_MS && 
        (ahora - ultimoResync) > ECU_RESYNC_TIMEOUT_MS) {
        if (Serial) Serial.println("[ECU] Sin frames válidos hace 2s, purgando buffer...");
        bufIndex = 0;
        ultimoResync = ahora; 
    } 
    // 2. Si el tubo se llenó al máximo sin poder formar un solo paquete lógico, lo vaciamos
    else if (bufIndex >= ECU_BUFFER_MAX) {
        if (Serial) Serial.println("[ECU] Buffer físico excedido sin sync, purgando...");
        bufIndex = 0;
        ultimoResync = ahora;
    }
}

/* ==============================================================================
   SETUP Y LOOP (El corazón del programa)
   ============================================================================== */

void setup() {
    Serial.begin(115200); // Inicia el cable USB que va a la PC para leer los prints
    myusb.begin();        // Enciende el hardware USB interno del microcontrolador
    delay(200);           // Pequeño respiro eléctrico antes de arrancar
}

void loop() {
    // 1. Mantiene viva la tarea interna del hardware USB
    myusb.Task();
    
    // 2. Toma el tiempo actual del sistema (como mirar un cronómetro)
    unsigned long ahora = millis();

    // 3. Ejecuta las tareas de la ECU (No bloquean, entran, hacen lo suyo y salen rapidísimo)
    gestionarConexionECU(ahora);
    tareaECU(ahora);
    
    // Aquí es donde el RTOS (Real Time OS) casero brilla. Podés poner todas las tareas 
    // adicionales y ninguna se va a interrumpir por la otra.
    // tareaSPI(ahora); 
    // tareaCAN(ahora); 
    // tareaSD(ahora);

    // 4. Diagnóstico en Pantalla / Consola (Se ejecuta solo 5 veces por segundo)
    // El "if (Serial)" evita que el programa se cuelgue si el auto está en pista sin una PC conectada.
    if (Serial && (ahora - ultimoDebug >= DEBUG_PERIODO_MS)) {
        
        // Calcula cuántos paquetes reales entraron en el último segundo (Debería rondar los 25 Hz)
        float hz_efectivos = 0.0;
        if (ahora > ultimoDebug) {
            hz_efectivos = (float)(framesOk - lastFramesOk) * (1000.0 / (ahora - ultimoDebug));
        }
        lastFramesOk = framesOk;

        // Traduce el estado interno a texto para los humanos
        const char* estadoStr;
        if (estadoEcu == ECU_DESCONECTADA) estadoStr = "DESCONECTADA";
        else if (estadoEcu == ECU_HANDSHAKE) estadoStr = "HANDSHAKE";
        else if (ecuOk) estadoStr = "OK";
        else estadoStr = "SIN DATOS";

        // Imprime en consola un renglón súper limpio y fácil de leer
        if (ecuOk) {
            Serial.printf("ECU %-12s | Hz: %4.1f | OK: %lu | Rech: %lu | Otr: %lu | RPM: %d | TPS: %.1f%% | VBat: %.2f V\n", 
                          estadoStr, hz_efectivos, framesOk, framesRechazados, framesOtros, rpm, tps, bateria_v);
        } else {
            // Si no hay datos, muestra los valores congelados pero no engaña con los RPM
            Serial.printf("ECU %-12s | Hz: %4.1f | OK: %lu | Rech: %lu | Otr: %lu | RPM: %d | VBat: %.2f V\n", 
                          estadoStr, hz_efectivos, framesOk, framesRechazados, framesOtros, rpm, bateria_v);
        }
        ultimoDebug = ahora; // Reinicia el cronómetro del debug
    }
}
