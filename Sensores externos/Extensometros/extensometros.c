const int PIN_SENSOR = A0;

float valor_filtrado = 0.0;
const float SUAVIZADO = 0.1;

unsigned long ultimoDebug = 0;

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);
    analogReadAveraging(32);
    pinMode(PIN_SENSOR, INPUT);
}

void loop() {
    int lectura_cruda = analogRead(PIN_SENSOR);
    valor_filtrado = (lectura_cruda * SUAVIZADO) + (valor_filtrado * (1.0 - SUAVIZADO));

    if (millis() - ultimoDebug >= 100) {
        bool alerta_tope = (lectura_cruda <= 1 || lectura_cruda >= 4094);
        Serial.printf("Valor Crudo: %4d | Filtrado: %6.1f | Tope: %s\n",
                      lectura_cruda, valor_filtrado, alerta_tope ? "SI" : "NO");
        ultimoDebug = millis();
    }
}
