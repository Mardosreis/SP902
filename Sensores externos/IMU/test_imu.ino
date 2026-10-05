#include <Wire.h>

/* ==============================================================================
   IMU - MPU-6050 (módulo GY-521), por I2C
   ==============================================================================
   VCC del módulo a 3.3V del Teensy (NUNCA a 5V, ver nota de conexión).
   SCL -> pin 19, SDA -> pin 18 (bus I2C por defecto del Teensy 4.1).
   AD0 sin conectar o a GND -> dirección 0x68.
*/

const uint8_t MPU_ADDR = 0x68; // AD0 a GND/flotante -> 0x68. AD0 a VCC -> 0x69.

// Registros relevantes del MPU-6050
const uint8_t REG_WHO_AM_I     = 0x75;
const uint8_t REG_PWR_MGMT_1   = 0x6B;
const uint8_t REG_ACCEL_XOUT_H = 0x3B; // Primer registro de un bloque de 14 bytes:
                                        // Accel X,Y,Z (6) + Temp (2) + Gyro X,Y,Z (6)

// Sensibilidades para el rango por defecto del sensor (±2g y ±250°/s)
const float ACCEL_ESCALA = 16384.0; // LSB por g
const float GYRO_ESCALA  = 131.0;   // LSB por °/s

int16_t ax_raw, ay_raw, az_raw;
int16_t temp_raw;
int16_t gx_raw, gy_raw, gz_raw;

float ax_g, ay_g, az_g;
float gx_dps, gy_dps, gz_dps;
float temp_c;

unsigned long ultimoDebug = 0;
const unsigned long DEBUG_PERIODO_MS = 100; // 10 Hz, suficiente para esta prueba de banco

void escribirRegistro(uint8_t reg, uint8_t valor) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(reg);
    Wire.write(valor);
    Wire.endTransmission();
}

uint8_t leerRegistro(uint8_t reg) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false); // repeated start: no suelta el bus entre escritura y lectura
    Wire.requestFrom(MPU_ADDR, (uint8_t)1);
    return Wire.read();
}

// Lee los 14 bytes (accel + temp + gyro) en una sola transacción, así todos los
// valores corresponden al mismo instante y no se mezclan datos de lecturas distintas.
bool leerDatosIMU() {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(REG_ACCEL_XOUT_H);
    if (Wire.endTransmission(false) != 0) return false; // error de comunicación I2C

    uint8_t recibidos = Wire.requestFrom(MPU_ADDR, (uint8_t)14);
    if (recibidos != 14) return false;

    ax_raw   = (Wire.read() << 8) | Wire.read();
    ay_raw   = (Wire.read() << 8) | Wire.read();
    az_raw   = (Wire.read() << 8) | Wire.read();
    temp_raw = (Wire.read() << 8) | Wire.read();
    gx_raw   = (Wire.read() << 8) | Wire.read();
    gy_raw   = (Wire.read() << 8) | Wire.read();
    gz_raw   = (Wire.read() << 8) | Wire.read();

    ax_g = ax_raw / ACCEL_ESCALA;
    ay_g = ay_raw / ACCEL_ESCALA;
    az_g = az_raw / ACCEL_ESCALA;

    gx_dps = gx_raw / GYRO_ESCALA;
    gy_dps = gy_raw / GYRO_ESCALA;
    gz_dps = gz_raw / GYRO_ESCALA;

    temp_c = (temp_raw / 340.0) + 36.53; // Fórmula del datasheet

    return true;
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin();
    Wire.setClock(400000); // I2C a 400kHz (fast mode); el MPU-6050 lo soporta sin problema

    delay(100);

    uint8_t whoami = leerRegistro(REG_WHO_AM_I);
    Serial.printf("[IMU] WHO_AM_I: 0x%02X (esperado 0x68)\n", whoami);

    if (whoami != 0x68) {
        Serial.println("[IMU] ERROR: no se detecta el MPU-6050. Revisar cableado y dirección I2C.");
    }

    // El sensor arranca en modo "sleep" por defecto (bit 6 del PWR_MGMT_1 en 1).
    // Hay que escribir 0x00 ahí para despertarlo y que empiece a muestrear.
    escribirRegistro(REG_PWR_MGMT_1, 0x00);
    delay(50);

    Serial.println("[IMU] Configurado y despierto.");
}

void loop() {
    unsigned long ahora = millis();

    if (Serial && (ahora - ultimoDebug >= DEBUG_PERIODO_MS)) {
        if (leerDatosIMU()) {
            Serial.printf("Accel (g): X=%6.2f Y=%6.2f Z=%6.2f | Gyro (°/s): X=%7.1f Y=%7.1f Z=%7.1f | Temp: %.1f°C\n",
                          ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps, temp_c);
        } else {
            Serial.println("[IMU] Error de lectura I2C");
        }
        ultimoDebug = ahora;
    }
}
