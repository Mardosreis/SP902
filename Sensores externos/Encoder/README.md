Es un encoder incremental de 600 pulsos por vuelta, cuadratura (OUT A / OUT B desfasados 90°), alimentación 5-24V DC.

- Rojo → VCC (5V)
- Negro → GND
- Verde → OUT A
- Blanco → OUT B
- Gris (sin aislante) → Tierra/blindaje.

La salida es tipo push-pull NPN/PNP (según el modelo exacto), normalmente a la misma tensión que alimentás el VCC. Si lo alimentás a 5V, las salidas A/B van a conmutar entre 0 y 5V — y ahí volvemos al mismo problema que con el extensómetro: los pines del Teensy 4.1 no toleran 5V.
