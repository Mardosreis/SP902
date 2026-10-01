/* ==============================================================================

1. Variables Globales y Calibración

   ============================================================================== */

// UBICACIÓN: Pegá esto en la parte superior del archivo, justo debajo de 
// las variables globales de la ECU y antes de que empiecen las funciones.

/* ==============================================================================
   DATOS DEL EXTENSÓMETRO 1 (Ej: Pushrod Delantero Izquierdo)
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

2. La Tarea de Lectura y Filtrado

   ============================================================================== */
// UBICACIÓN: Pegá esta función entera fuera del setup() y del loop(). 
// Por prolijidad, podés ponerla justo arriba de "void setup()".

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

3. Configuración del Hardware (Setup)

   ============================================================================== */
// UBICACIÓN: Pegá estas tres líneas adentro de tu "void setup()", 
// justo arriba de donde dice "myusb.begin();".

    // Configuración avanzada del ADC interno del Teensy para los extensómetros
    analogReadResolution(12); // Pasa de 10-bits a 12-bits de resolución (0-4095)
    analogReadAveraging(4);   // Limpia picos de ruido electromagnético por hardware
    pinMode(PIN_EXT1, INPUT);

/* ==============================================================================

4. Ejecución (Loop)

   ============================================================================== */

// UBICACIÓN: Pegá esta línea adentro de tu "void loop()", 
// justo debajo de donde dice "tareaECU(ahora);".

    tareaExtensometro1(ahora);


/* ==============================================================================

5. Impresión en Pantalla (Debug)

   ============================================================================== */

// UBICACIÓN: Adentro de tu "void loop()", en la sección final de debug, 
// reemplazá los dos "Serial.printf" que ya tenías por estos dos nuevos. 
// Esto suma el valor del extensómetro y la alerta [SAT] al final del renglón.

        if (ecuOk) {
            Serial.printf("ECU %-12s | Hz: %4.1f | OK: %lu | Rech: %lu | Otr: %lu | RPM: %d | TPS: %.1f%% | VBat: %.2f V | Ext1: %.2f%s\n", 
                          estadoStr, hz_efectivos, framesOk, framesRechazados, framesOtros, rpm, tps, bateria_v, 
                          ext1_fuerza, ext1_saturado ? " [SAT]" : "");
        } else {
            Serial.printf("ECU %-12s | Hz: %4.1f | OK: %lu | Rech: %lu | Otr: %lu | RPM: %d | VBat: %.2f V | Ext1: %.2f%s\n", 
                          estadoStr, hz_efectivos, framesOk, framesRechazados, framesOtros, rpm, bateria_v, 
                          ext1_fuerza, ext1_saturado ? " [SAT]" : "");
        }
