#include <Arduino.h>

/* ==============================================================================
   ENCODER ROTATIVO INCREMENTAL - LPD3806-600BM
   ==============================================================================
   600 pulsos por vuelta, cuadratura (OUT A / OUT B desfasados 90°).
   Alimentado a 5V. Las salidas pasan por un divisor resistivo (10K / 15K)
   antes de llegar a los pines digitales del Teensy, para no superar los 3.3V
   que toleran sus pines.
*/

const int PIN_ENC_A = 2;   // Nodo del divisor A
const int PIN_ENC_B = 3;   // Nodo del divisor B

const int PULSOS_POR_VUELTA = 600;                       // Dato del fabricante
const int CUENTAS_POR_VUELTA = PULSOS_POR_VUELTA * 4;     // x4: se cuenta cada flanco de A y de B

volatile long contador = 0;   // Cuentas acumuladas (+ en un sentido, - en el otro)
volatile uint8_t estadoAB = 0; // Historial de los últimos 2 estados de A/B, para la tabla

// Tabla de decodificación de cuadratura: a cada combinación (estado anterior + estado
// nuevo) le corresponde +1, -1, o 0 si la transición es imposible (ruido / rebote).
const int8_t TABLA_CUADRATURA[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

// Se dispara con cada cambio de A o de B. Tiene que ser lo más corta posible.
void encoderISR() {
    estadoAB <<= 2;
    estadoAB |= (digitalReadFast(PIN_ENC_A) << 1) | digitalReadFast(PIN_ENC_B);
    contador += TABLA_CUADRATURA[estadoAB & 0x0F];
}

unsigned long ultimoDebug = 0;
long ultimoContadorDebug = 0;
const unsigned long DEBUG_PERIODO_MS = 200; // 5 Hz

void setup() {
    Serial.begin(115200);

    pinMode(PIN_ENC_A, INPUT);
    pinMode(PIN_ENC_B, INPUT);

    // Registra el estado real A/B antes de habilitar las interrupciones, para que
    // la primera transición que llegue no se interprete como un salto inválido.
    estadoAB = (digitalReadFast(PIN_ENC_A) << 1) | digitalReadFast(PIN_ENC_B);

    attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encoderISR, CHANGE);
}

void loop() {
    unsigned long ahora = millis();

    if (Serial && (ahora - ultimoDebug >= DEBUG_PERIODO_MS)) {

        // Copia atómica del contador (se puede modificar en cualquier momento
        // desde la interrupción, así que lo leemos con las interrupciones pausadas)
        noInterrupts();
        long copiaContador = contador;
        interrupts();

        long delta = copiaContador - ultimoContadorDebug;
        ultimoContadorDebug = copiaContador;

        float vueltas = (float)copiaContador / CUENTAS_POR_VUELTA;

        // delta ocurrió en DEBUG_PERIODO_MS milisegundos
        float revsPorSeg = (float)delta / CUENT