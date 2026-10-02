Es un encoder incremental de 600 pulsos por vuelta, cuadratura (OUT A / OUT B desfasados 90°), alimentación 5-24V DC.

- Rojo → VCC (5V)
- Negro → GND
- Verde → OUT A
- Blanco → OUT B
- Gris (sin aislante) → Tierra/blindaje.

La salida es tipo push-pull NPN/PNP (según el modelo exacto), normalmente a la misma tensión que alimentás el VCC. Si lo alimentás a 5V, las salidas A/B van a conmutar entre 0 y 5V — y ahí volvemos al mismo problema que con el extensómetro: los pines del Teensy 4.1 no toleran 5V.

Conseguir los componentes
4 resistencias: dos de 10 KΩ y dos de 15 KΩ (una pareja por canal, A y B). Con 1/4 W alcanza de sobra, porque la corriente que circula es mínima (unos 0.2 mA por rama). También necesitás una protoboard o una placa perfilada chica para armar los dos divisores, y cable para las 5 conexiones del encoder.
2
Armar los dos divisores en la protoboard
Por cada canal: la resistencia de 10 KΩ va desde el cable de salida del encoder (verde para A, blanco para B) hasta un punto intermedio (nodo). Desde ese mismo nodo, la resistencia de 15 KΩ va hasta GND. El nodo —el punto donde se juntan las dos resistencias— es el que vas a conectar al Teensy. Armá dos de estos, uno para cada canal.
3
Cablear el encoder
Rojo (VCC) → al 5V del Teensy (pin VIN, o una fuente de 5V aparte). Negro (GND) → a un GND del Teensy, el mismo que usan los divisores. Verde (OUT A) → a la entrada del divisor A (la pata libre de la resistencia de 10 KΩ). Blanco (OUT B) → a la entrada del divisor B. Gris, sin aislante (Tierra/blindaje) → al chasis o masa del auto, no necesariamente al mismo punto que el GND de señal, para drenar ruido sin crear un lazo de tierra.
4
Medir con el multímetro ANTES de tocar el Teensy
Con el encoder alimentado a 5V (y el Teensy todavía desconectado de los divisores), medí con el multímetro en el nodo de cada divisor contra GND. Girá el eje del encoder lentamente a mano: la tensión del nodo tiene que alternar entre 0V y aproximadamente 3.0V, limpio, sin quedarse pegada ni superar nunca los 3.3V. Si ves que llega a 5V en algún punto, hay un error de cableado — revisalo antes de seguir.
5
Conectar los nodos al Teensy y cargar el código
Nodo del divisor A → un pin digital del Teensy (en el código de abajo, pin 2). Nodo del divisor B → otro pin digital (pin 3). Subí el sketch y abrí el monitor serie a 115200 baudios. Girá el eje a mano en un sentido: el contador y el RPM tienen que subir. Girá al revés: tienen que bajar. Si están invertidos, no es un error — simplemente cruzá los cables de los nodos A y B entre sí.
