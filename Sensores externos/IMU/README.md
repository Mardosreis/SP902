
Conectar los 4 cables
VCC del módulo → 3.3V del Teensy (NO a 5V). GND → GND del Teensy. SCL → pin 19 del Teensy (bus I2C por defecto). SDA → pin 18. AD0 dejálo sin conectar (o a GND): define la dirección I2C en 0x68. Si en el futuro sumás un segundo MPU-6050 en el mismo bus, ese segundo módulo lleva AD0 a VCC para que tome la dirección 0x69 y no choque con el primero.
2
No agregar pull-ups extra
No hace falta agregar resistencias de pull-up: el módulo GY-521 ya las trae puestas, referenciadas a su VCC. Si sumás pull-ups extra vas a bajar demasiado la resistencia equivalente del bus, sin necesidad.
3
Verificar con el multímetro antes de programar
Con el Teensy alimentado por USB (y el módulo conectado a su 3.3V), medí con el multímetro entre SDA y GND, y entre SCL y GND, con el bus en reposo (sin código corriendo aún, o con el Teensy recién arrancado antes de que el código use el I2C). Deberían marcar algo cercano a 3.3V, nunca 5V. Si da 5V, revisá que el VCC del módulo realmente esté en el pin de 3.3V y no en el VIN/5V del Teensy.
4
Cargar el código y confirmar la identidad del sensor
Subí el código de abajo. Lo primero que hace es leer el registro WHO_AM_I del sensor y debería imprimir 0x68. Si imprime otra cosa o nada, es cableado (SDA/SCL invertidos, dirección mal, o falta de alimentación) — no sigas hasta que ese chequeo dé bien.
5
Probar que los valores tengan sentido físico
Con el sensor quieto sobre una superficie plana, el eje que apunta hacia arriba debería marcar cerca de 1.00g, y los otros dos cerca de 0.00g. Mové el sensor con la mano en cada eje y confirmá que el giroómetro (°/s) reacciona cuando lo rotás, y vuelve a 0 cuando lo dejás quieto.
