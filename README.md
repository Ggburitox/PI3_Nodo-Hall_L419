# Sensor magnético KY-024 — Gemelo Digital L419

**Nodo 6 · ESP32 · ESP-NOW · Panel web local**

## 1. Descripción

Este proyecto monitoriza la apertura y el cierre de una puerta con el sensor Hall KY-024 (SS49E). El ESP32 lee el sensor, detecta la presencia de un imán, calcula el estado de la puerta, registra aperturas y tiempos y envía los datos al gateway mediante ESP-NOW. La interfaz de la computadora consulta la API del gateway, sin depender del monitor serie ni de una conexión a Internet.

Flujo: `KY-024 → ESP32 nodo 6 → ESP-NOW → gateway → API HTTP → panel web`.

Se asume que el imán queda cerca del sensor con la puerta cerrada y lejos con la puerta abierta. La detección utiliza la diferencia absoluta respecto a una calibración inicial sin imán.

## 2. Archivos

```text
Hall_L419_v2/
├── nodo_magnetico/
│   ├── nodo_magnetico.ino   # Firmware del ESP32
│   └── paquetes.h           # Protocolo binario oficial, sin modificaciones
├── interfaz.py              # Servidor local y puente HTTP al gateway
├── panel.html               # Interfaz gráfica sin dependencias de Internet
├── README.md                # Esta guía
└── LEEME.txt                # La misma guía en texto plano
```

El firmware y `paquetes.h` deben permanecer en la misma carpeta. El gateway (`gateway_ap.ino`) y el simulador externo (`simulador_gateway.py`) no están incluidos: corresponden al equipo del gateway. No se necesita `espnow_nodo.h` para este firmware.

## 3. Requisitos y conexiones

- ESP32 DOIT ESP32 DEVKIT V1, módulo KY-024, imán, cables y alimentación USB.
- Arduino IDE con el paquete de placas ESP32 instalado (usar una versión compatible con la placa).
- Python 3 y un navegador moderno para el panel. No se requiere instalar paquetes con pip.
- Gateway encendido y con el mismo `paquetes.h` para las pruebas de integración.

| Pin KY-024 | Pin ESP32 |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| DO | GPIO 32 |
| AO | GPIO 34 |

Usar alimentación de 3.3 V y verificar las etiquetas físicas del módulo antes de cablear. No aplicar señales de 5 V a los GPIO del ESP32. GPIO 34 pertenece a ADC1 en el ESP32 clásico y se usa para la detección analógica.

## 4. Parámetros del firmware

| Parámetro | Valor | Función |
|---|---|---|
| `NODO_ID` | 6 | Identificador oficial del sensor magnético |
| `SSID_ROUTER` | `GEMELO-L419` | Red que busca el sensor para determinar el canal Wi-Fi |
| `MAC_GATEWAY` | `58:2A:BD:72:03:DC` | MAC STA del receptor; confirmar con el equipo del gateway |
| `CANAL_DEFECTO` | 1 | Canal de respaldo si no encuentra la red |
| `UMBRAL` | 80 | Diferencia ADC mínima para detectar imán |
| `LECTURA_MS` | 500 | Periodo nominal de lectura y monitor serie |
| `ENVIO_MS` | 2000 | Periodo nominal de envío ESP-NOW |
| `LIMITE_PUERTA_ABIERTA_S` | 60 | Umbral provisional de alerta, modificable |

La guía fija el nodo 6 y la red. El periodo de envío y el límite de alerta son decisiones de esta implementación. La MAC de ejemplo no se ha confirmado físicamente. El sensor no necesita la contraseña del gateway: busca el SSID para conocer el canal, sin asociarse a la red.

Para aumentar sensibilidad, probar `const int UMBRAL = 40;` y volver a cargar el firmware. Un umbral menor detecta variaciones menores, pero puede causar falsos cambios por ruido. Comprobar la fluctuación sin imán antes de elegirlo. El potenciómetro del módulo ajusta DO; la detección de este programa depende de AO.

## 5. Cargar y probar el ESP32

1. Enciende el gateway y confirma en su monitor serie la MAC STA real. Si difiere, cambia `MAC_GATEWAY` en el `.ino`.
2. Conecta el KY-024 y el ESP32 conforme a la tabla anterior.
3. Abre `nodo_magnetico/nodo_magnetico.ino` en Arduino IDE. Selecciona DOIT ESP32 DEVKIT V1 (o ESP32 Dev Module si corresponde a tu placa) y el puerto USB correcto.
4. Mantén el imán alejado, verifica el programa y súbelo. Si ya está instalado en la puerta, realiza la calibración con ella abierta y el imán suficientemente lejos.
5. Abre el monitor serie a **115200 baudios**. Si es necesario, reinicia el ESP32 mientras mantienes el imán alejado. El programa espera 500 ms y toma 100 muestras separadas por 5 ms para determinar `valorBase`.
6. Acerca el imán: debe aparecer puerta CERRADA. Aléjalo: debe aparecer ABIERTA. El contador aumenta una vez por transición de cerrada a abierta; no aumenta continuamente al permanecer abierta.
7. Mantén el imán alejado al menos 60 s: debe aparecer `PUERTA_ABIERTA_PROLONGADA`. Acércalo y comprueba que el tiempo de la apertura actual vuelve a cero y la alerta desaparece.

## 6. Monitor serie: cómo leerlo

Desde que termina la inicialización, se imprime una línea legible aproximadamente cada 500 ms, con o sin gateway y con o sin interfaz abierta. La salida solo se puede observar si existe una conexión serie disponible (por ejemplo, por USB con Arduino IDE). El ESP32 puede seguir leyendo y transmitiendo sin la computadora si dispone de alimentación independiente.

Ejemplo ilustrativo (los valores reales dependen de la calibración y del campo magnético):

```text
Tiempo: 2000 ms | Digital: 1 | Analógico: 2320 | Diferencia: 272 | Imán: DETECTADO | Puerta: CERRADA | Aperturas: 0 | Tiempo abierta: 0 s | Anomalía: NINGUNA | ESP-NOW: 1
Tiempo: 6000 ms | Digital: 0 | Analógico: 2050 | Diferencia: 2 | Imán: NO DETECTADO | Puerta: ABIERTA | Aperturas: 1 | Tiempo abierta: 3 s | Anomalía: NINGUNA | ESP-NOW: 1
```

| Campo | Interpretación |
|---|---|
| Tiempo | Milisegundos desde el arranque del ESP32 (`millis()`). No es la hora del día. |
| Digital | Estado de DO (0 o 1), dependiente del comparador del módulo. No decide por sí solo el estado de la puerta. |
| Analógico | Lectura ADC de AO, rango 0–4095 (12 bits). No es voltaje ni campo en teslas. |
| Diferencia | Lectura analógica menos `valorBase`; puede ser negativa. |
| Imán | DETECTADO si `abs(diferencia) >= UMBRAL`, en caso contrario NO DETECTADO. |
| Puerta | CERRADA con imán detectado; ABIERTA sin imán. |
| Aperturas | Contador local acumulado desde el arranque. No cuenta una apertura ficticia en la primera lectura. |
| Tiempo abierta | Segundos de la apertura en curso; cero al cerrar. |
| Anomalía | NINGUNA o PUERTA_ABIERTA_PROLONGADA según el límite configurado. |
| ESP-NOW | Último resultado conocido de transmisión: 1 = éxito a nivel MAC; 0 = fallo o aún no hay un envío confirmado. |

El último número de cada línea de medición es 1 o 0, como pide la guía. Mientras hay un nuevo envío pendiente se conserva el último resultado conocido. Un 1 **no** demuestra que el gateway haya interpretado los datos: compruébalo en `/api/ultimos`.

Los mensajes de arranque y diagnóstico comienzan por `#` y no son líneas de medición. Si el gateway no está disponible, el sensor sigue realizando lecturas locales una vez concluida la inicialización. Si no encuentra la red al encender, utiliza el canal de respaldo; reiniciar con el gateway encendido para descubrir su canal real.

### Monitor serie, transmisión e interfaz: diferencias

| Proceso | Frecuencia nominal | Destino |
|---|---|---|
| Lecturas del sensor | 500 ms | ESP32 del nodo |
| Monitor serie | 500 ms | Arduino IDE por USB |
| Envío ESP-NOW | 2 s | ESP32 gateway |
| Consulta del panel | 2 s después de cada ciclo de consulta | API del gateway |

El formato legible del monitor es solo para depuración. La interfaz **no lee ni procesa el monitor serie**: recibe JSON desde el gateway. Cambiar la presentación del monitor no altera el algoritmo, el formato binario `paquetes.h` ni la comunicación inalámbrica. El nodo transmite aunque la interfaz esté cerrada. El panel requiere gateway y datos recientes para mostrar el estado real.

## 7. Protocolo oficial de comunicación

Se usa `PaqueteMagnetico` de 21 bytes, con `Cabecera` de 11 bytes y `PROTO_VERSION = 1`, `TIPO_MAGNETICO = 6`, `nodo_id = 6`, secuencia y tiempo de funcionamiento. Se conserva `paquetes.h` exactamente como fue proporcionado.

| Campo del paquete | Significado |
|---|---|
| `puerta_abierta` | 0 = cerrada, 1 = abierta |
| `aperturas` | Aperturas ocurridas en la ventana desde el envío anterior (no acumulado local) |
| `seg_abierta` | Segundos abierta dentro de esa ventana, estimados por muestreo |
| `campo_raw` | Promedio ADC de la ventana |
| `anomalias` | Máscara de bits definida en el protocolo |

`MAG_BIT_FALLO` vale 1; `MAG_BIT_ABIERTA_PROLONGADA` vale 2. La alerta prolongada está implementada. El bit de fallo físico se deja sin activar hasta contar con un diagnóstico eléctrico validado: una lectura constante o la ausencia de imán no prueban un fallo.

Las ventanas se reinician tras cada intento de envío; si un paquete se pierde, también se pierden los datos de esa ventana. La secuencia ayuda a detectar pérdidas, pero no hay reenvío de datos ni confirmación de aplicación. El muestreo de 500 ms puede omitir aperturas muy breves. La calibración y los contadores locales se reinician al apagar o reiniciar el nodo.

## 8. Ejecutar la interfaz gráfica

### Modo demostración (sin placas)

Desde una terminal en la carpeta descomprimida `Hall_L419_v2`:

```bash
python3 interfaz.py --demo
```

Abre `http://127.0.0.1:8765` si no se abre automáticamente. El modo demo genera datos de ejemplo; no son lecturas del sensor real. Pulsa Ctrl+C en la terminal para detener el programa.

### Modo real

1. Enciende el gateway y luego el nodo magnético calibrado.
2. Conecta la laptop al Wi-Fi **GEMELO-L419**, contraseña `gemelo2026` (según la guía). Esta red no proporciona Internet.
3. Abre `http://192.168.4.1/api/ultimos` y busca `nodo: 6`, `tipo: magnetico`. Comprueba que cambien las mediciones al acercar y alejar el imán.
4. Desde `Hall_L419_v2`, ejecuta:

```bash
python3 interfaz.py
```

5. Abre `http://127.0.0.1:8765`. Acerca y aleja el imán y comprueba los cambios en el panel.

El servidor Python sirve la página HTML y reenvía las consultas HTTP al gateway. Así la página utiliza un mismo origen local y no necesita depender de permisos CORS del gateway.

Para utilizar el simulador externo de la guía, primero inícialo y luego ejecuta:

```bash
python3 interfaz.py --gateway http://localhost:8000
```

Si el puerto local está ocupado: `python3 interfaz.py --port 8766`, y abrir `http://127.0.0.1:8766`.

### Datos del panel

- Estado del nodo (activo, inactivo, sin datos o sin conexión) y estado de la puerta.
- Aperturas y segundos abierta en el último intervalo, según el paquete oficial.
- Anomalías, antigüedad de la última medición y valor ADC si el gateway publica `campo_raw`.
- Gráfica de estados y suma de aperturas de hasta 60 paquetes recientes recibidos. No equivale al contador acumulado de toda la ejecución que muestra el monitor serie.

El panel consulta `/api/hora?epoch_ms=...` para sincronizar la hora, `/api/ultimos` para el estado y `/api/datos?desde=...` para el historial incremental (JSON por líneas). Si `hace_s > 60`, marca el nodo inactivo. Si faltan datos o el gateway devuelve nulos, se muestran como no disponibles. No requiere Internet ni librerías en CDN.

## 9. Solución de problemas

| Síntoma | Acción |
|---|---|
| No aparece GEMELO-L419 | Comprobar que el gateway esté encendido y su AP configurado. |
| Error de compilación `paquetes.h` | Verificar que esté junto al archivo `.ino` y no haya una copia incompatible. |
| Puerto serie no disponible | Revisar cable USB de datos, puerto y permisos del sistema. |
| El imán no se detecta o detecta sin imán | Reiniciar sin imán, revisar AO/GND y ajustar el umbral según las lecturas. |
| Estado de puerta invertido | Revisar colocación física y calibración sin imán. |
| ESP-NOW termina en 0 | Revisar MAC STA, canal y gateway encendido; reiniciar el nodo después del gateway. |
| ESP-NOW termina en 1, pero nodo 6 no aparece | Comprobar protocolo compartido, identificación del nodo y recepción/decodificación en el gateway. |
| Panel sin conexión | Conectar la laptop a GEMELO-L419, comprobar API HTTP y ejecutar Python sin `--demo`. |
| Hora ausente o `ts` nulo | Comprobar la sincronización mediante `/api/hora`. |
| ADC no aparece en panel | Confirmar que el gateway publique `campo_raw`; el nodo sí lo envía. |

## 10. Lista de verificación y estado

- [ ] Confirmar MAC STA del gateway.
- [ ] Compilar y cargar el firmware en el ESP32.
- [ ] Validar calibración, detección, conteo, tiempos y alerta en monitor serie.
- [ ] Confirmar resultado ESP-NOW 1 y recepción del nodo 6 en la API.
- [ ] Validar interfaz en modo demo y luego con datos reales.
- [ ] Confirmar datos de historial, inactividad y desconexión.

Esta revisión cambia la salida de medición del monitor serie de CSV a texto con etiquetas y amplía la documentación; mantiene las funciones del detector, el protocolo y los archivos de interfaz. El código original de detección había sido probado por el equipo. El firmware integrado y su nuevo formato de salida no se han compilado con un toolchain ESP32 ni probado en la placa en esta revisión. La integración física, MAC real y criterio de fallo eléctrico siguen pendientes de validación.

Referencia: `Guia_gateway_AP.docx`, secciones 2–8, y `paquetes.h` oficial del proyecto.
