Es un encoder incremental de 600 pulsos por vuelta, cuadratura (OUT A / OUT B desfasados 90°), alimentación 5-24V DC.

- Rojo → VCC (5V)
- Negro → GND
- Verde → OUT A
- Blanco → OUT B
- Gris (sin aislante) → Tierra/blindaje.

La salida es tipo push-pull NPN/PNP (según el modelo exacto), normalmente a la misma tensión que alimentás el VCC. Si lo alimentás a 5V, las salidas A/B van a conmutar entre 0 y 5V — y ahí volvemos al mismo problema que con el extensómetro: los pines del Teensy 4.1 no toleran 5V.
# Guía de Conexión: Encoder Incremental LPD3806

## 1. Conseguir los componentes
* **Resistencias:** 4 unidades (dos de 10 KΩ y dos de 15 KΩ) para formar una pareja por canal (A y B). Con 1/4 W alcanza de sobra debido a la baja corriente (aprox. 0.2 mA por rama).
* **Adicionales:** Protoboard o placa perfilada chica para los divisores, y cables para las 5 conexiones.

## 2. Armar los divisores en la protoboard
* **Estructura por canal:** Conectar la resistencia de 10 KΩ desde el cable de salida del encoder (verde para A, blanco para B) hasta un punto intermedio (nodo).
* **Referencia a masa:** Desde el mismo nodo, conectar la resistencia de 15 KΩ hasta GND.
* **Salida:** El nodo central (donde se unen ambas resistencias) es la salida regulada que irá al Teensy. Replicar para ambos canales.

## 3. Cableado del Encoder
* **Rojo (VCC)** → 5V del Teensy (pin VIN) o fuente externa de 5V.
* **Negro (GND)** → GND del Teensy (mismo GND de los divisores).
* **Verde (OUT A)** → Entrada del divisor A (extremo libre de la resistencia de 10 KΩ).
* **Blanco (OUT B)** → Entrada del divisor B (extremo libre de la resistencia de 10 KΩ).
* **Gris (Malla/Blindaje)** → Masa del chasis del auto para drenar ruido (evitar conectarlo al mismo GND de señal para no generar lazos de tierra).

## 4. Verificación con multímetro (SIN Teensy)
* Alimentar el encoder a 5V con el Teensy **desconectado** de los divisores.
* Medir la tensión entre el nodo de cada divisor y GND.
* Girar el eje manualmente: la tensión debe alternar limpiamente entre 0V y ~3.0V. 
* **Advertencia:** Si el multímetro marca 5V en algún punto, hay un error en el divisor. Corregir antes de continuar para no quemar la placa.

## 5. Conexión final y código
* **Nodo Divisor A** → Pin digital del Teensy (ej. Pin 2).
* **Nodo Divisor B** → Pin digital del Teensy (ej. Pin 3).
* Cargar el código y abrir el Monitor Serie a 115200 baudios.
* Girar el eje: los valores (contador/RPM) deben subir en un sentido y bajar en el inverso. Si el giro está invertido respecto a lo deseado, intercambiar físicamente los cables de los nodos A y B.
