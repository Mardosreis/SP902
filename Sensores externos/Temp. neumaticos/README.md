
Antes de armar el código, dos cosas que necesito confirmar porque si las supongo mal el circuito no va a andar (o peor, puede dañar el Teensy), y una pieza de hardware que te va a faltar.

Lo que necesito que me confirmes
¿Qué marca/modelo es el sensor? (o si tenés el datasheet/manual a mano). El cableado físico del CAN es genérico, pero qué significan los bytes dentro de cada mensaje (qué ID lleva la temperatura, en qué unidades, con qué escala) es propio de cada fabricante — no lo puedo adivinar sin romper algo en la decodificación. Mientras tanto te armo un "sniffer" para que veas qué tramas llegan, y con eso (o con el datasheet) decodificamos bien.
¿Sabés a qué velocidad (bitrate) habla el bus? Lo típico en electrónica de competición es 500 kbps o 1 Mbps, pero varía. Si no lo sabés, arrancamos en 500000 y te digo cómo detectar si está mal.
La pieza que te falta: un transceptor CAN

El Teensy 4.1 tiene el controlador CAN integrado (puede armar y entender los mensajes), pero no tiene el chip transceptor que convierte esa lógica interna en la señal diferencial de 2 hilos (CAN_H / CAN_L) que realmente viaja por el cable. Sin ese chip en el medio, no hay forma de conectar el Teensy al bus.

Necesitás comprar un módulo transceptor CAN de 3.3V — el más común para este uso es el SN65HVD230 (hay módulos baratos y ya armados). Importante: elegí uno de 3.3V, no uno de 5V como el MCP2551 o el TJA1050 — si usás uno de 5V, su salida hacia el Teensy va a superar los 3.3V que tolera el pin, y volvemos al mismo problema de siempre con este Teensy.

1
Conseguir el transceptor CAN
Un módulo transceptor CAN de 3.3V (SN65HVD230 o similar, NO uno de 5V como MCP2551/TJA1050). La mayoría de estos módulos ya traen soldado un jumper o resistencia de 120Ω de terminación — fijáte si el tuyo lo trae, porque eso cambia el paso 3.
2
Alimentar el sensor y llevar el bus al transceptor
Rojo (VCC 12V) del sensor → a una fuente de 12V (NO al Teensy, que no entrega esa tensión). Negro (GND) del sensor → a un GND común entre el sensor, el transceptor y el Teensy — los tres tienen que compartir la misma referencia de 0V. Amarillo y verde (probablemente CAN_H y CAN_L, a confirmar con el datasheet) → a los pines CANH y CANL del transceptor.
3
Resolver la terminación de 120Ω
El bus CAN necesita exactamente 120Ω en cada punta física del cable, nunca en cada nodo. Si el sensor ya forma parte de una red con otros dispositivos que ya tienen terminación, NO agregués otra — sumar resistencias de más degrada la señal. Si el Teensy va a ser una de las dos puntas del bus (por ejemplo, conexón directa sensor–Teensy sin nada más), activá o solderá el jumper de 120Ω del módulo transceptor.
4
Conectar el transceptor al Teensy
VCC del transceptor → 3.3V del Teensy. GND del transceptor → GND común. TXD del transceptor → pin 23 del Teensy (CAN1 TX). RXD del transceptor → pin 22 del Teensy (CAN1 RX). Si tu módulo tiene un pin S o SHDN (modo silencioso/apagado), conectalo a GND para que trabaje en modo normal.
5
Cargar el sniffer y ver qué llega
Subí el código de abajo y abrí el monitor serie a 115200. Si empiezan a aparecer líneas con ID y datos, el cableado y el bitrate están bien — copiá un par de líneas y las vemos juntos para identificar cuál ID trae la temperatura. Si no aparece nada, probablemente el bitrate no coincide: probá con 1000000 o 250000 en vez de 500000.
El código: un sniffer, no el decodificador final

Como todavía no sé el formato exacto de los mensajes de tu sensor, este primer código no interpreta la temperatura — solo muestra en crudo cada trama que llega al bus (ID + bytes), para que identifiquemos juntos cuál corresponde al sensor y cómo está codificado el dato.

Antes de compilar

Te va a faltar instalar la librería: en el Arduino IDE, Herramientas → Administrar Bibliotecas, buscá "FlexCAN_T4" (de tonton81) e instalala. Es la librería estándar de la comunidad Teensy para el controlador CAN integrado.

Qué vas a ver

Si todo está bien cableado y el bitrate coincide, cada línea del monitor serie te va a mostrar algo así:

ID: 0x1A0  Len: 8  Datos: 02 1C 00 9A 01 F3 00 00
ID: 0x1A1  Len: 8  Datos: ...

Con eso, y si el sensor manda varios canales (temperatura interior/media/exterior del neumático, por ejemplo), probablemente vayas a ver varios IDs distintos apareciendo de forma repetida a un ritmo constante. Copiame unas cuantas líneas seguidas (o decime si encontrás el datasheet) y de ahí armamos la decodificación: qué ID es, en qué posición de bytes está la temperatura, y con qué escala/offset convertirla a °C.
