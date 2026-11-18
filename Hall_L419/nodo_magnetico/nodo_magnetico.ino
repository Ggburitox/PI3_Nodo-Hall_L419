/* KY-024 (SS49E) - Gemelo Digital L419, nodo magnetico 6.
 * Mantener el iman alejado durante la calibracion inicial.
 * Imán próximo = puerta cerrada. Usar alimentación de 3.3 V.
 * Requiere el paquetes.h oficial en esta misma carpeta.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <atomic>
#include <string.h>
#include "paquetes.h"

#define HALL_DIGITAL_PIN 32
#define HALL_ANALOG_PIN 34
const int UMBRAL = 80;
const uint32_t LECTURA_MS = 500;
const uint32_t ENVIO_MS = 2000;
const uint32_t LIMITE_PUERTA_ABIERTA_S = 60; // Ajustable; no fijado por la guia.
uint8_t MAC_GATEWAY[6] = {0x58, 0x2A, 0xBD, 0x72, 0x03, 0xDC}; // Confirmar MAC STA.
const char* SSID_ROUTER = "GEMELO-L419";
const uint8_t CANAL_DEFECTO = 1;
const uint8_t NODO_ID = 6;
static_assert(sizeof(Cabecera) == 11, "Cabecera incompatible");
static_assert(sizeof(PaqueteMagnetico) == 21, "Paquete magnetico incompatible");

int valorBase = 0;
bool espnowListo = false;
bool puertaAnterior = false;
bool primeraLectura = true;
uint32_t inicioApertura = 0, ultimaLectura = 0, ultimoEnvio = 0, secuencia = 0;
uint32_t aperturasTotal = 0, aperturasVentana = 0;
uint64_t msAbiertaVentana = 0, sumaCampo = 0;
uint32_t muestrasCampo = 0;
// -1: esperando callback; 0: fallo / sin envio previo; 1: ACK de la capa MAC.
std::atomic<int> resultadoEnvio{0};
int ultimoOk = 0;

// Deducimos el primer argumento del callback desde la API instalada,
// compatible con las firmas antiguas (MAC) y nuevas (informacion TX).
struct CallbackEnvio {
  template<typename T>
  static void recibir(T, esp_now_send_status_t estado) {
    resultadoEnvio.store(estado == ESP_NOW_SEND_SUCCESS ? 1 : 0);
  }
};

uint8_t obtenerCanal() {
  int redes = WiFi.scanNetworks();
  uint8_t canal = CANAL_DEFECTO;
  bool encontrado = false;
  for (int i = 0; i < redes; ++i) {
    if (WiFi.SSID(i) == SSID_ROUTER) {
      canal = (uint8_t)WiFi.channel(i);
      encontrado = true;
      break;
    }
  }
  WiFi.scanDelete();
  if (!encontrado) Serial.println("# Red no encontrada; usando canal de respaldo. Reiniciar con gateway activo.");
  return canal;
}

bool iniciarESPNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  uint8_t canal = obtenerCanal();
  if (esp_wifi_set_channel(canal, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
  if (esp_now_init() != ESP_OK) return false;
  if (esp_now_register_send_cb(CallbackEnvio::recibir) != ESP_OK) {
    esp_now_deinit();
    return false;
  }
  esp_now_peer_info_t gateway = {};
  memcpy(gateway.peer_addr, MAC_GATEWAY, 6);
  gateway.channel = canal;
  gateway.ifidx = WIFI_IF_STA;
  gateway.encrypt = false;
  if (esp_now_add_peer(&gateway) != ESP_OK) {
    esp_now_deinit();
    return false;
  }
  Serial.print("# MAC local STA: "); Serial.println(WiFi.macAddress());
  Serial.printf("# ESP-NOW listo. Canal: %u | Nodo: %u | Paquete: %u bytes\n", canal, NODO_ID, (unsigned)sizeof(PaqueteMagnetico));
  return true;
}

void setup() {
  Serial.begin(115200);
  pinMode(HALL_DIGITAL_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(HALL_ANALOG_PIN, ADC_11db);
  delay(500);
  long suma = 0;
  for (int i = 0; i < 100; ++i) {
    suma += analogRead(HALL_ANALOG_PIN);
    delay(5);
  }
  valorBase = suma / 100;
  Serial.printf("# KY-024 | Valor base: %d | Umbral: %d\n", valorBase, UMBRAL);
  espnowListo = iniciarESPNow();
  if (!espnowListo) Serial.println("# ESP-NOW no iniciado. El sensor sigue funcionando localmente.");
  Serial.println("# ms,digital,analogico,diferencia,iman,puerta_abierta,aperturas_total,seg_apertura_actual,anom_bits,ultimo_envio_ok");
}

void loop() {
  uint32_t ahora = millis();
  int resultado = resultadoEnvio.load();
  if (resultado >= 0) ultimoOk = resultado;
  if (!primeraLectura && (uint32_t)(ahora - ultimaLectura) < LECTURA_MS) {
    delay(1);
    return;
  }
  int digitalState = digitalRead(HALL_DIGITAL_PIN);
  int analogValue = analogRead(HALL_ANALOG_PIN);
  int diferencia = analogValue - valorBase;
  bool imanDetectado = abs(diferencia) >= UMBRAL;
  bool puertaAbierta = !imanDetectado;

  // Integrar el tiempo usando el estado de la muestra anterior.
  if (primeraLectura) {
    primeraLectura = false;
    ultimoEnvio = ahora;
    if (puertaAbierta) inicioApertura = ahora;
  } else {
    if (puertaAnterior) msAbiertaVentana += (uint32_t)(ahora - ultimaLectura);
    if (puertaAbierta && !puertaAnterior) {
      ++aperturasTotal;
      if (aperturasVentana < UINT16_MAX) ++aperturasVentana;
      inicioApertura = ahora;
    }
  }
  ultimaLectura = ahora;
  puertaAnterior = puertaAbierta;
  uint32_t segundosActual = puertaAbierta ? (uint32_t)(ahora - inicioApertura) / 1000 : 0;
  sumaCampo += (uint16_t)analogValue;
  ++muestrasCampo;

  uint8_t anomalias = 0;
  if (puertaAbierta && segundosActual >= LIMITE_PUERTA_ABIERTA_S) anomalias |= MAG_BIT_ABIERTA_PROLONGADA;
  // MAG_BIT_FALLO reservado: no hay diagnostico electrico validado.
  // No marcar fallo por ausencia del iman o por lecturas constantes.

  if ((uint32_t)(ahora - ultimoEnvio) >= ENVIO_MS && resultado >= 0) {
    PaqueteMagnetico p = {};
    p.h.version = PROTO_VERSION;
    p.h.tipo = TIPO_MAGNETICO;
    p.h.nodo_id = NODO_ID;
    p.h.seq = ++secuencia;
    p.h.uptime_ms = ahora;
    p.puerta_abierta = puertaAbierta ? 1 : 0;
    p.aperturas = (uint16_t)aperturasVentana;
    p.seg_abierta = (float)msAbiertaVentana / 1000.0f;
    p.campo_raw = (uint16_t)(sumaCampo / muestrasCampo);
    p.anomalias = anomalias;
    if (espnowListo) {
      resultadoEnvio.store(-1);
      esp_err_t err = esp_now_send(MAC_GATEWAY, (const uint8_t*)&p, sizeof(p));
      if (err != ESP_OK) {
        resultadoEnvio.store(0);
        ultimoOk = 0;
        Serial.printf("# Error al iniciar envio: %d\n", (int)err);
      }
    }
    // Ventanas disjuntas por intento, incluso si se pierde el paquete.
    // seq permite detectar perdidas; no se duplican aperturas al reenviar.
    aperturasVentana = 0;
    msAbiertaVentana = 0;
    sumaCampo = 0;
    muestrasCampo = 0;
    ultimoEnvio = ahora;
  }
  Serial.printf("%lu,%d,%d,%d,%d,%d,%lu,%lu,%u,%d\n",
    (unsigned long)ahora, digitalState, analogValue, diferencia,
    imanDetectado ? 1 : 0, puertaAbierta ? 1 : 0,
    (unsigned long)aperturasTotal, (unsigned long)segundosActual,
    anomalias, ultimoOk);
}
