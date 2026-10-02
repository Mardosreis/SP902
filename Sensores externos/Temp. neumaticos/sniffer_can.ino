#include <FlexCAN_T4.h>

/* ==============================================================================
   SNIFFER CAN - Identificación de tramas del sensor de temperatura
   ==============================================================================
   Usa CAN1 del Teensy 4.1 (pines 23 = TX, 22 = RX), a través de un transceptor
   externo de 3.3V (ej. SN65HVD230). No decodifica nada todavía: solo imprime
   cada trama cruda que llega, para identificar qué ID trae la temperatura
   y cómo está codificado el valor dentro de los bytes.
*/

FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> canBus;

const uint32_t CAN_BAUDRATE = 500000; // ⚠️ Confirmar con el datasheet del sensor.
                                       // Si no aparece nada, probar 1000000 o 250000.

void mostrarTrama(const CAN_message_t &msg) {
    Serial.printf("ID: 0x%03X  Len: %d  Datos: ", msg.id, msg.len);
    for (uint8_t i = 0; i < msg.len; i++) {
        Serial.printf("%02X ", msg.buf[i]);
    }
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(1000); // Le da tiempo a la PC de abrir el monitor serie

    canBus.begin();
    canBus.setBaudRate(CAN_BAUDRATE);
    canBus.enableFIFO();
    canBus.enableFIFOInterrupt();
    canBus.onReceive(mostrarTrama);

    Serial.println("[CAN] Escuchando el bus...");
    Serial.printf("[CAN] Bitrate configurado: %lu\n", CAN_BAUDRATE);
}

void loop() {
    canBus.events(); // Procesa las tramas recibidas por interrupción
}
