/****************************************************
 * PROYECTO: Sistema de Riego Solar Inteligente
 * Autor: Jesús Villarreal
 * Versión: 6.1
 * Cambios:
 * - Timestamps reales con Firebase Server Timestamp
 * - Fecha legible con NTP
 ****************************************************/
// Librería para conexión WiFi del ESP32
#include <WiFi.h>

// Librería principal para comunicación con Firebase
#include <Firebase_ESP_Client.h>

// Librería para manejo de fecha y hora
#include <time.h>

// Utilidades de ayuda para autenticación de Firebase
#include "addons/TokenHelper.h"

// Utilidades de ayuda para operaciones RTDB de Firebase
#include "addons/RTDBHelper.h"

// Archivo externo con credenciales y datos sensibles
#include "Secrets.h"

// ====================================================
//  DEFINICIÓN DE PINES (ESP32)
// ====================================================

// - PIN_HUMEDAD: entrada analógica del sensor de humedad
#define PIN_HUMEDAD     34

// - PIN_VALVULA1, 2 y 3: salidas hacia relés de válvulas o bombas
#define PIN_VALVULA1    27
#define PIN_VALVULA2    14
#define PIN_VALVULA3    13

// - PIN_LED: LED indicador de estado
#define PIN_LED         4

// ====================================================
//  RUTAS FIREBASE
// ====================================================

// Rutas de Firebase Realtime Database.
// Cada constante representa la ubicación donde se guarda o lee
// información del sistema, control, sensores, estado e historial.
const char* RUTA_AUTOR              = "/RiegoSolar/Configuracion/autor";
const char* RUTA_NOMBRE             = "/RiegoSolar/Configuracion/nombreSistema";
const char* RUTA_VERSION            = "/RiegoSolar/Configuracion/version";

const char* RUTA_MODO               = "/RiegoSolar/Control/modo";
const char* RUTA_UMBRAL             = "/RiegoSolar/Control/umbralHumedad";
const char* RUTA_VALVULA1           = "/RiegoSolar/Control/valvula1";
const char* RUTA_VALVULA2           = "/RiegoSolar/Control/valvula2";
const char* RUTA_VALVULA3           = "/RiegoSolar/Control/valvula3";

const char* RUTA_HUMEDAD            = "/RiegoSolar/Sensores/humedad";
const char* RUTA_HUMEDAD_RAW        = "/RiegoSolar/Sensores/humedadRaw";

const char* RUTA_ESTADO_SISTEMA     = "/RiegoSolar/Estado/sistema";
const char* RUTA_ESTADO_WIFI        = "/RiegoSolar/Estado/wifi";
const char* RUTA_ESTADO_FIREBASE    = "/RiegoSolar/Estado/firebase";
const char* RUTA_ESTADO_IP          = "/RiegoSolar/Estado/ip";
const char* RUTA_ESTADO_ERROR       = "/RiegoSolar/Estado/ultimoError";
const char* RUTA_ESTADO_TIMESTAMP   = "/RiegoSolar/Estado/ultimaActualizacion";
const char* RUTA_ESTADO_FECHA       = "/RiegoSolar/Estado/ultimaActualizacionTexto";

const char* RUTA_HIST_LECTURAS       = "/RiegoSolar/Historial/Lecturas";
const char* RUTA_HIST_EVENTOS        = "/RiegoSolar/Historial/Eventos";
const char* RUTA_HIST_ERRORES        = "/RiegoSolar/Historial/Errores";
const char* RUTA_HIST_CAMBIOS_MODO   = "/RiegoSolar/Historial/CambiosModo";
const char* RUTA_HIST_CAMBIOS_UMBRAL = "/RiegoSolar/Historial/CambiosUmbral";
const char* RUTA_HIST_MANUAL         = "/RiegoSolar/Historial/ActivacionesManual";
const char* RUTA_HIST_CICLOS         = "/RiegoSolar/Historial/CiclosRiego";

// ====================================================
//  CALIBRACIÓN DEL SENSOR CAPACITIVO
// ====================================================

// Valores de calibración del sensor capacitivo.
// SENSOR_SECO   = lectura cuando la tierra está seca
const int SENSOR_SECO   = 2370;

// SENSOR_MOJADO = lectura cuando la tierra está húmeda
const int SENSOR_MOJADO = 1400;

// HISTERESIS evita encendidos y apagados constantes cerca del umbral.
const int HISTERESIS = 5;

// ====================================================
//  OBJETOS FIREBASE
// ====================================================

// Objetos globales necesarios para autenticar, configurar y enviar
// o recibir datos desde Firebase.
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseJson jsonControl;

// ====================================================
//  VARIABLES GLOBALES DEL SISTEMA
// ====================================================

// Variables globales que almacenan el estado actual del sistema,
// lecturas del sensor, modo de operación, salidas, temporizadores
// y datos para historial y control de ciclos.
int humedadRaw = 0;
int humedad = 0;
String modo = "auto";
int umbral = 40;

bool valvula1 = false;
bool valvula2 = false;
bool valvula3 = false;
bool led = false;

int ultimaHumedadEnviada = -1;
bool riegoActivoAnterior = false;
bool sensorValido = true;
bool sensorValidoAnterior = true;
bool hayErrorActivo = false;

unsigned long tiempoLed = 0;
bool estadoLed = false;

const unsigned long TIEMPO_LED_ENCENDIDO = 2000;
const unsigned long TIEMPO_LED_APAGADO = 2000;

unsigned long tiempoSecuenciaBombas = 0;
int estadoSecuenciaBombas = 0;
bool secuenciaActiva = false;

const unsigned long TIEMPO_BOMBA_ENCENDIDA = 10000;
const unsigned long TIEMPO_BOMBA_APAGADA = 5000;

String modoAnterior = "auto";
int umbralAnterior = 40;

bool secuenciaActivaAnterior = false;

unsigned long ultimoRegistroLecturaHist = 0;
const unsigned long INTERVALO_HIST_LECTURA = 300000;

unsigned long inicioCicloMillis = 0;
int humedadInicioCiclo = 0;
int bombasEjecutadasCiclo = 0;
int ciclosCompletos = 0;

// ====================================================
//  TEMPORIZADORES (millis)
// ====================================================

// Temporizadores basados en millis() para ejecutar tareas periódicas
// sin bloquear el programa principal.
unsigned long tiempoLectura   = 0;
unsigned long tiempoControl   = 0;
unsigned long tiempoEstado    = 0;
unsigned long tiempoReconexion = 0;
unsigned long tiempoRiego     = 0;

const unsigned long INTERVALO_LECTURA   = 3000;
const unsigned long INTERVALO_CONTROL   = 2000;
const unsigned long INTERVALO_ESTADO    = 10000;
const unsigned long INTERVALO_RECONEXION = 5000;
const unsigned long TIEMPO_MAX_RIEGO    = 600000UL;

// ====================================================
//  PROTOTIPOS DE FUNCIONES
// ====================================================

// Prototipos de funciones.
// Se declaran aquí para que el compilador conozca su existencia
// antes de llegar a su implementación completa.
void apagarTodo();
void conectarWiFi();
void verificarWiFi();
void iniciarFirebase();
void configurarHora();
String obtenerFechaHora();
int  convertirHumedad(int raw);
void leerSensorHumedad();
void enviarDatosFirebase();
void leerComandosFirebase();
void ejecutarControl();
void modoAutomatico();
void modoManual();
void imprimirEstado();
void actualizarEstadoFirebase();
bool fbSetBool(const char* ruta, bool valor);
bool fbSetInt(const char* ruta, int valor);
bool fbSetString(const char* ruta, const String& valor);
bool fbSetTimestamp(const char* ruta);
bool verificarHoraSincronizada();
void verificarFirebase();
void actualizarTemporizadorRiego(bool estadoActual);
void registrarErrorFirebase(const char* contexto);
void actualizarSalida(int pin, bool &estadoActual, bool nuevoEstado, const char* ruta);
void escribirRele(int pin, bool encendido);
void escribirLed(bool encendido);
void actualizarLedEstado();
void apagarBombas();
void ejecutarSecuenciaBombas();
bool fbPushJSON(const char* ruta, FirebaseJson* json);
void registrarLecturaHistorial();
void registrarEvento(const String& tipo, const String& detalle);
void registrarErrorHistorial(const String& tipo, const String& mensaje);
void registrarCambioModo(const String& anterior, const String& nuevo);
void registrarCambioUmbral(int anterior, int nuevo);
void registrarActivacionManual(int bomba, const String& estado);
void registrarInicioCiclo();
void registrarFinCiclo();
void detectarCambiosControl();
void actualizarUltimaActualizacion();
void establecerErrorEstado(const String& mensaje);
void limpiarErrorEstado();

// ====================================================
//  SETUP
// ====================================================
void setup() {
  // Inicia la comunicación serial para monitoreo y depuración
  Serial.begin(115200);
  delay(1000);

// Muestra encabezado de inicio en el monitor serial
  Serial.println();
  Serial.println("=======================================");
  Serial.println("   SISTEMA DE RIEGO SOLAR CON ESP32");
  Serial.println("=======================================");

// Configura los pines de válvulas y LED como salidas
  pinMode(PIN_VALVULA1, OUTPUT);
  pinMode(PIN_VALVULA2, OUTPUT);
  pinMode(PIN_VALVULA3, OUTPUT);
  pinMode(PIN_LED, OUTPUT);

// Asegura que al arrancar todas las bombas y el LED estén apagados
  apagarBombas();
  escribirLed(false);

 // Inicializa variables de control del LED
  estadoLed = false;
  led = false;
  tiempoLed = millis();

// Inicializa variables de la secuencia automática de riego
  secuenciaActiva = false;
  estadoSecuenciaBombas = 0;
  tiempoSecuenciaBombas = 0;

// Configura el ADC del ESP32 para leer correctamente el sensor
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_HUMEDAD, ADC_11db);

// Conecta a WiFi, sincroniza la hora e inicia Firebase
  conectarWiFi();
  configurarHora();
  iniciarFirebase();

// Si Firebase quedó listo, publica información inicial del sistema
  if (Firebase.ready()) {
    fbSetString(RUTA_AUTOR, "Jesus Villarreal");
    fbSetString(RUTA_NOMBRE, "Sistema de Riego Solar");
    fbSetString(RUTA_VERSION, "6.1");

// Guarda estado inicial del dispositivo
    fbSetString(RUTA_ESTADO_SISTEMA, "Iniciado");
    fbSetBool(RUTA_ESTADO_WIFI, WiFi.status() == WL_CONNECTED);
    fbSetBool(RUTA_ESTADO_FIREBASE, Firebase.ready());
    fbSetString(RUTA_ESTADO_IP, WiFi.localIP().toString());
    fbSetString(RUTA_ESTADO_ERROR, "Sin errores");
    actualizarUltimaActualizacion();

  }

// Mensaje final de arranque correcto
  Serial.println();
  Serial.println("Sistema iniciado correctamente.");
}

// ====================================================
//  LOOP PRINCIPAL
// ====================================================
void loop() {
  // Actualiza el parpadeo del LED de estado
  actualizarLedEstado();

// Cada cierto tiempo verifica reconexión de WiFi y Firebase
  if (millis() - tiempoReconexion >= INTERVALO_RECONEXION) {
    tiempoReconexion = millis();
    verificarWiFi();
    verificarFirebase();
  }

// Si Firebase no está listo, no continúa con las tareas principales
  if (!Firebase.ready()) {
    delay(100);
    return;
  }

// Lee sensor, envía datos y muestra estado en intervalos definidos
  if (millis() - tiempoLectura >= INTERVALO_LECTURA) {
    tiempoLectura = millis();
    leerSensorHumedad();
    enviarDatosFirebase();
    imprimirEstado();
  }

// Guarda una lectura histórica cada cierto tiempo
  if (millis() - ultimoRegistroLecturaHist >= INTERVALO_HIST_LECTURA) {
    ultimoRegistroLecturaHist = millis();
    registrarLecturaHistorial();
  }

// Lee comandos remotos y ejecuta la lógica de control
  if (millis() - tiempoControl >= INTERVALO_CONTROL) {
    tiempoControl = millis();
    leerComandosFirebase();
    detectarCambiosControl();
    ejecutarControl();
  }

// Watchdog de seguridad: apaga el riego su dura demaciado tiempo
  if ((valvula1 || valvula2 || valvula3) && tiempoRiego > 0) {
    if (millis() - tiempoRiego > TIEMPO_MAX_RIEGO) {
      Serial.println();
      Serial.println("WATCHDOG: Tiempo maximo de riego alcanzado. Apagando todo.");

      apagarTodo();

      // Refleja el apagado en Firebase
      fbSetBool(RUTA_VALVULA1, false);
      fbSetBool(RUTA_VALVULA2, false);
      fbSetBool(RUTA_VALVULA3, false);

      // Registra error y evento de seguridad
      establecerErrorEstado("Watchdog: tiempo maximo de riego");
      fbSetString(RUTA_ESTADO_SISTEMA, "Watchdog activado");
      actualizarUltimaActualizacion();

      registrarErrorHistorial("Watchdog", "Tiempo maximo de riego alcanzado");
      registrarEvento("Watchdog", "Apagado de seguridad por exceso de tiempo");
    }
  }

// Actualiza el nodo de estado general en Firebase
  if (millis() - tiempoEstado >= INTERVALO_ESTADO) {
    tiempoEstado = millis();
    actualizarEstadoFirebase();
  }
}

// ====================================================
//  IMPLEMENTACIÓN DE FUNCIONES
// ====================================================

// ----------------------------------------------------
// configurarHora()
// Sincroniza la fecha y hora del ESP32 usando servidores NTP.
// Esto permite guardar registros con hora legible.
// ----------------------------------------------------
void configurarHora() {
  configTime(-5 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("Sincronizando hora");
  struct tm timeinfo;
  int intentos = 0;

  while (!getLocalTime(&timeinfo) && intentos < 20) {
    Serial.print(".");
    delay(500);
    intentos++;
  }
  Serial.println();

  if (getLocalTime(&timeinfo)) {
    Serial.println("Hora sincronizada correctamente.");
    Serial.println(obtenerFechaHora());
  } else {
    Serial.println("No se pudo sincronizar la hora.");
  }
}

// ----------------------------------------------------
// verificarHoraSincronizada()
// Verifica si el ESP32 ya tiene hora válida sincronizada.
// Devuelve true si la hora está disponible.
// ----------------------------------------------------
bool verificarHoraSincronizada() {
  struct tm timeinfo;
  return getLocalTime(&timeinfo);
}

// ----------------------------------------------------
// obtenerFechaHora()
// Obtiene la fecha y hora actual en formato legible
// YYYY-MM-DD HH:MM:SS.
// Si no hay hora sincronizada, devuelve "sin_hora".
// ----------------------------------------------------
String obtenerFechaHora() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "sin_hora";
  }

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

// ----------------------------------------------------
// actualizarUltimaActualizacion()
// Actualiza en Firebase el timestamp del servidor y la
// fecha legible de la última actualización del sistema.
// ----------------------------------------------------
void actualizarUltimaActualizacion() {
  fbSetTimestamp(RUTA_ESTADO_TIMESTAMP);
  fbSetString(RUTA_ESTADO_FECHA, obtenerFechaHora());
}

// ----------------------------------------------------
// establecerErrorEstado()
// Marca que existe un error activo y lo guarda en Firebase.
// ----------------------------------------------------
void establecerErrorEstado(const String& mensaje) {
  hayErrorActivo = true;
  fbSetString(RUTA_ESTADO_ERROR, mensaje);
}

// ----------------------------------------------------
// limpiarErrorEstado()
// Limpia el estado de error activo si existía uno.
// ----------------------------------------------------
void limpiarErrorEstado() {
  if (hayErrorActivo) {
    fbSetString(RUTA_ESTADO_ERROR, "Sin errores");
    hayErrorActivo = false;
  }
}

// ----------------------------------------------------
// actualizarLedEstado()
// Hace parpadear el LED usando temporizadores con millis()
// para indicar actividad del sistema sin bloquear el programa.
// ----------------------------------------------------
void actualizarLedEstado() {
  unsigned long ahora = millis();

  if (!estadoLed) {
    if (ahora - tiempoLed >= TIEMPO_LED_APAGADO) {
      estadoLed = true;
      led = true;
      tiempoLed = ahora;
      escribirLed(true);
    }
  } else {
    if (ahora - tiempoLed >= TIEMPO_LED_ENCENDIDO) {
      estadoLed = false;
      led = false;
      tiempoLed = ahora;
      escribirLed(false);
    }
  }
}

// ----------------------------------------------------
// ejecutarSecuenciaBombas()
// Ejecuta la secuencia automática de riego:
// bomba 1, pausa, bomba 2, pausa, bomba 3, pausa.
// También registra eventos e informa cambios a Firebase.
// ----------------------------------------------------
void ejecutarSecuenciaBombas() {
  unsigned long ahora = millis();

  switch (estadoSecuenciaBombas) {
    case 0:
    // Enciende bomba 1 y apaga las demás
      escribirRele(PIN_VALVULA1, true);
      escribirRele(PIN_VALVULA2, false);
      escribirRele(PIN_VALVULA3, false);
      valvula1 = true;
      valvula2 = false;
      valvula3 = false;
      tiempoSecuenciaBombas = ahora;
      estadoSecuenciaBombas = 1;
      fbSetBool(RUTA_VALVULA1, true);
      fbSetBool(RUTA_VALVULA2, false);
      fbSetBool(RUTA_VALVULA3, false);
      bombasEjecutadasCiclo++;
      registrarEvento("Bomba1_ON", "Bomba 1 encendida por secuencia automatica");
      break;

    case 1:
    // Espera el tiempo de encendido de la bomba 1 y luego la apaga
      if (ahora - tiempoSecuenciaBombas >= TIEMPO_BOMBA_ENCENDIDA) {
        apagarBombas();
        tiempoSecuenciaBombas = ahora;
        estadoSecuenciaBombas = 2;
        fbSetBool(RUTA_VALVULA1, false);
        fbSetBool(RUTA_VALVULA2, false);
        fbSetBool(RUTA_VALVULA3, false);
        registrarEvento("Bomba1_OFF", "Bomba 1 apagada");
      }
      break;

    case 2:
    // Tras la pausa, enciende bomba 2
      if (ahora - tiempoSecuenciaBombas >= TIEMPO_BOMBA_APAGADA) {
        escribirRele(PIN_VALVULA1, false);
        escribirRele(PIN_VALVULA2, true);
        escribirRele(PIN_VALVULA3, false);
        valvula1 = false;
        valvula2 = true;
        valvula3 = false;
        tiempoSecuenciaBombas = ahora;
        estadoSecuenciaBombas = 3;
        fbSetBool(RUTA_VALVULA1, false);
        fbSetBool(RUTA_VALVULA2, true);
        fbSetBool(RUTA_VALVULA3, false);
        bombasEjecutadasCiclo++;
        registrarEvento("Bomba2_ON", "Bomba 2 encendida por secuencia automatica");
      }
      break;

    case 3:
    // Mantiene bomba 2 encendida el tiempo configurado y luego la apaga
      if (ahora - tiempoSecuenciaBombas >= TIEMPO_BOMBA_ENCENDIDA) {
        apagarBombas();
        tiempoSecuenciaBombas = ahora;
        estadoSecuenciaBombas = 4;
        fbSetBool(RUTA_VALVULA1, false);
        fbSetBool(RUTA_VALVULA2, false);
        fbSetBool(RUTA_VALVULA3, false);
        registrarEvento("Bomba2_OFF", "Bomba 2 apagada");
      }
      break;

    case 4:
    // Tras la pausa, enciende bomba 3
      if (ahora - tiempoSecuenciaBombas >= TIEMPO_BOMBA_APAGADA) {
        escribirRele(PIN_VALVULA1, false);
        escribirRele(PIN_VALVULA2, false);
        escribirRele(PIN_VALVULA3, true);
        valvula1 = false;
        valvula2 = false;
        valvula3 = true;
        tiempoSecuenciaBombas = ahora;
        estadoSecuenciaBombas = 5;
        fbSetBool(RUTA_VALVULA1, false);
        fbSetBool(RUTA_VALVULA2, false);
        fbSetBool(RUTA_VALVULA3, true);
        bombasEjecutadasCiclo++;
        registrarEvento("Bomba3_ON", "Bomba 3 encendida por secuencia automatica");
      }
      break;

    case 5:
    // Mantiene bomba 3 encendida y luego la apaga
      if (ahora - tiempoSecuenciaBombas >= TIEMPO_BOMBA_ENCENDIDA) {
        apagarBombas();
        tiempoSecuenciaBombas = ahora;
        estadoSecuenciaBombas = 6;
        fbSetBool(RUTA_VALVULA1, false);
        fbSetBool(RUTA_VALVULA2, false);
        fbSetBool(RUTA_VALVULA3, false);
        registrarEvento("Bomba3_OFF", "Bomba 3 apagada");
      }
      break;

    case 6:
    // Espera la pausa final y reinicia la secuencia completa
      if (ahora - tiempoSecuenciaBombas >= TIEMPO_BOMBA_APAGADA) {
        estadoSecuenciaBombas = 0;
        ciclosCompletos++;
        registrarEvento("CicloCompleto", "Se completo la secuencia de 3 bombas");
      }
      break;
  }
}

// ----------------------------------------------------
// registrarErrorFirebase()
// Muestra en serial el error ocurrido con Firebase,
// lo guarda como estado actual y además lo registra
// en el historial de errores.
// ----------------------------------------------------
void registrarErrorFirebase(const char* contexto) {
  Serial.print("Error Firebase en ");
  Serial.print(contexto);
  Serial.print(": ");
  Serial.println(fbdo.errorReason());

  String mensaje = String(contexto) + ": " + fbdo.errorReason();
  hayErrorActivo = true;

  if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
    Firebase.RTDB.setString(&fbdo, RUTA_ESTADO_ERROR, mensaje);
    Firebase.RTDB.setString(&fbdo, RUTA_ESTADO_FECHA, obtenerFechaHora());
    Firebase.RTDB.setTimestamp(&fbdo, RUTA_ESTADO_TIMESTAMP);
    registrarErrorHistorial("Firebase", mensaje);
  }
}

// ----------------------------------------------------
// fbSetBool()
// Envía un valor booleano a Firebase.
// Si falla, registra el error.
// ----------------------------------------------------
bool fbSetBool(const char* ruta, bool valor) {
  if (!Firebase.RTDB.setBool(&fbdo, ruta, valor)) {
    registrarErrorFirebase(ruta);
    return false;
  }
  return true;
}

// ----------------------------------------------------
// fbSetInt()
// Envía un valor entero a Firebase.
// Si falla, registra el error.
// ----------------------------------------------------
bool fbSetInt(const char* ruta, int valor) {
  if (!Firebase.RTDB.setInt(&fbdo, ruta, valor)) {
    registrarErrorFirebase(ruta);
    return false;
  }
  return true;
}

// ----------------------------------------------------
// fbSetString()
// Envía un texto a Firebase.
// Si falla, registra el error.
// ----------------------------------------------------
bool fbSetString(const char* ruta, const String& valor) {
  if (!Firebase.RTDB.setString(&fbdo, ruta, valor)) {
    registrarErrorFirebase(ruta);
    return false;
  }
  return true;
}

// ----------------------------------------------------
// fbSetTimestamp()
// Guarda un timestamp generado por el servidor de Firebase.
// Si falla, registra el error.
// ----------------------------------------------------
bool fbSetTimestamp(const char* ruta) {
  if (!Firebase.RTDB.setTimestamp(&fbdo, ruta)) {
    registrarErrorFirebase(ruta);
    return false;
  }
  return true;
}

// ----------------------------------------------------
// fbPushJSON()
// Inserta un objeto JSON dentro de una ruta de Firebase,
// normalmente usado para historiales.
// ----------------------------------------------------
bool fbPushJSON(const char* ruta, FirebaseJson* json) {
  if (!Firebase.RTDB.pushJSON(&fbdo, ruta, json)) {
    registrarErrorFirebase(ruta);
    return false;
  }
  return true;
}

// ----------------------------------------------------
// verificarFirebase()
// Verifica que Firebase esté operativo.
// Si no está listo, intenta reiniciarlo.
// También re-sincroniza la hora si hace falta.
// ----------------------------------------------------
void verificarFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!verificarHoraSincronizada()) {
    configurarHora();
  }

  if (!Firebase.ready()) {
    Serial.println("Firebase no listo. Reintentando inicio...");
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
  } else {
    if (!hayErrorActivo) {
      fbSetString(RUTA_ESTADO_ERROR, "Sin errores");
    }
  }
}

// ----------------------------------------------------
// registrarLecturaHistorial()
// Guarda una lectura periódica del sistema en el historial:
// humedad, lectura raw, modo, umbral y fecha.
// ----------------------------------------------------
void registrarLecturaHistorial() {
  if (!Firebase.ready() || !sensorValido) return;

  FirebaseJson json;
  json.set("humedad", humedad);
  json.set("humedadRaw", humedadRaw);
  json.set("modo", modo);
  json.set("umbral", umbral);
  json.set("fechaHora", obtenerFechaHora());
  json.set("timestamp/.sv", "timestamp");

  fbPushJSON(RUTA_HIST_LECTURAS, &json);
}

// ----------------------------------------------------
// registrarEvento()
// Guarda un evento general del sistema en Firebase.
// Ejemplo: inicio de riego, fin de ciclo, bomba encendida.
// ----------------------------------------------------
void registrarEvento(const String& tipo, const String& detalle) {
  if (!Firebase.ready()) return;

  FirebaseJson json;
  json.set("tipo", tipo);
  json.set("detalle", detalle);
  json.set("modo", modo);
  json.set("humedad", humedad);
  json.set("umbral", umbral);
  json.set("fechaHora", obtenerFechaHora());
  json.set("timestamp/.sv", "timestamp");

  fbPushJSON(RUTA_HIST_EVENTOS, &json);
}

// ----------------------------------------------------
// registrarErrorHistorial()
// Guarda un error dentro del historial de errores.
// ----------------------------------------------------
void registrarErrorHistorial(const String& tipo, const String& mensaje) {
  if (!Firebase.ready()) return;

  FirebaseJson json;
  json.set("tipo", tipo);
  json.set("mensaje", mensaje);
  json.set("fechaHora", obtenerFechaHora());
  json.set("timestamp/.sv", "timestamp");

  fbPushJSON(RUTA_HIST_ERRORES, &json);
}

// ----------------------------------------------------
// registrarCambioModo()
// Registra en historial cuando el sistema cambia de modo
// entre automático y manual.
// ----------------------------------------------------
void registrarCambioModo(const String& anterior, const String& nuevo) {
  if (!Firebase.ready()) return;

  FirebaseJson json;
  json.set("anterior", anterior);
  json.set("nuevo", nuevo);
  json.set("fechaHora", obtenerFechaHora());
  json.set("timestamp/.sv", "timestamp");

  fbPushJSON(RUTA_HIST_CAMBIOS_MODO, &json);
}

// ----------------------------------------------------
// registrarCambioUmbral()
// Registra en historial cuando cambia el valor del umbral
// de humedad configurado.
// ----------------------------------------------------
void registrarCambioUmbral(int anterior, int nuevo) {
  if (!Firebase.ready()) return;

  FirebaseJson json;
  json.set("valorAnterior", anterior);
  json.set("valorNuevo", nuevo);
  json.set("fechaHora", obtenerFechaHora());
  json.set("timestamp/.sv", "timestamp");

  fbPushJSON(RUTA_HIST_CAMBIOS_UMBRAL, &json);
}

// ----------------------------------------------------
// registrarActivacionManual()
// Registra cuándo una bomba o válvula fue activada o apagada
// manualmente desde Firebase.
// ----------------------------------------------------
void registrarActivacionManual(int bomba, const String& estado) {
  if (!Firebase.ready()) return;

  FirebaseJson json;
  json.set("bomba", bomba);
  json.set("estado", estado);
  json.set("modo", "manual");
  json.set("fechaHora", obtenerFechaHora());
  json.set("timestamp/.sv", "timestamp");

  fbPushJSON(RUTA_HIST_MANUAL, &json);
}

// ----------------------------------------------------
// registrarInicioCiclo()
// Guarda el inicio de un ciclo de riego automático y
// reinicia contadores relacionados con ese ciclo.
// ----------------------------------------------------
void registrarInicioCiclo() {
  if (!Firebase.ready()) return;

  inicioCicloMillis = millis();
  humedadInicioCiclo = humedad;
  bombasEjecutadasCiclo = 0;
  ciclosCompletos = 0;

  FirebaseJson json;
  json.set("evento", "InicioCicloRiego");
  json.set("humedadInicial", humedadInicioCiclo);
  json.set("umbral", umbral);
  json.set("modo", modo);
  json.set("fechaHora", obtenerFechaHora());
  json.set("timestamp/.sv", "timestamp");

  fbPushJSON(RUTA_HIST_CICLOS, &json);
}

// ----------------------------------------------------
// registrarFinCiclo()
// Guarda el final de un ciclo de riego con duración,
// humedad inicial/final y cantidad de bombas ejecutadas.
// ----------------------------------------------------
void registrarFinCiclo() {
  if (!Firebase.ready()) return;

  FirebaseJson json;
  json.set("evento", "FinCicloRiego");
  json.set("humedadInicial", humedadInicioCiclo);
  json.set("humedadFinal", humedad);
  json.set("umbral", umbral);
  json.set("modo", modo);
  json.set("duracionSegundos", (millis() - inicioCicloMillis) / 1000);
  json.set("bombasEjecutadas", bombasEjecutadasCiclo);
  json.set("ciclosCompletos", ciclosCompletos);
  json.set("fechaHora", obtenerFechaHora());
  json.set("timestamp/.sv", "timestamp");

  fbPushJSON(RUTA_HIST_CICLOS, &json);
}

// ----------------------------------------------------
// detectarCambiosControl()
// Detecta si cambiaron el modo o el umbral respecto al valor
// anterior y registra esos cambios en Firebase.
// ----------------------------------------------------
void detectarCambiosControl() {
  if (modo != modoAnterior) {
    registrarCambioModo(modoAnterior, modo);
    modoAnterior = modo;
  }

  if (umbral != umbralAnterior) {
    registrarCambioUmbral(umbralAnterior, umbral);
    umbralAnterior = umbral;
  }
}

// ----------------------------------------------------
// actualizarTemporizadorRiego()
// Lleva el control del tiempo total de riego activo para
// poder aplicar el watchdog de seguridad.
// ----------------------------------------------------
void actualizarTemporizadorRiego(bool estadoActual) {
  if (estadoActual && !riegoActivoAnterior) {
    tiempoRiego = millis();
  }

  if (!estadoActual) {
    tiempoRiego = 0;
  }

  riegoActivoAnterior = estadoActual;
}

// ----------------------------------------------------
// escribirRele()
// Controla el relé asociado a una válvula.
// Usa lógica invertida: LOW enciende y HIGH apaga.
// ----------------------------------------------------
void escribirRele(int pin, bool encendido) {
  digitalWrite(pin, encendido ? LOW : HIGH);
}

// ----------------------------------------------------
// escribirLed()
// Enciende o apaga el LED indicador del sistema.
// ----------------------------------------------------
void escribirLed(bool encendido) {
  digitalWrite(PIN_LED, encendido ? HIGH : LOW);
}

// ----------------------------------------------------
// apagarBombas()
// Apaga todas las válvulas o bombas y actualiza
// las variables de estado local.
// ----------------------------------------------------
void apagarBombas() {
  escribirRele(PIN_VALVULA1, false);
  escribirRele(PIN_VALVULA2, false);
  escribirRele(PIN_VALVULA3, false);
  valvula1 = false;
  valvula2 = false;
  valvula3 = false;
}

// ----------------------------------------------------
// apagarTodo()
// Apaga bombas, LED y reinicia variables críticas
// relacionadas con riego y secuencias.
// ----------------------------------------------------
void apagarTodo() {
  apagarBombas();
  escribirLed(false);
  led = false;
  tiempoRiego = 0;
  riegoActivoAnterior = false;
  secuenciaActiva = false;
  estadoSecuenciaBombas = 0;
}

// ----------------------------------------------------
// actualizarSalida()
// Cambia el estado de una salida solo si realmente hubo
// un cambio, y además lo refleja en Firebase.
// ----------------------------------------------------
void actualizarSalida(int pin, bool &estadoActual, bool nuevoEstado, const char* ruta) {
  if (estadoActual != nuevoEstado) {
    estadoActual = nuevoEstado;
    escribirRele(pin, nuevoEstado);
    fbSetBool(ruta, nuevoEstado);
  }
}

// ----------------------------------------------------
// conectarWiFi()
// Intenta conectar el ESP32 a la red WiFi al iniciar.
// Muestra el resultado en el monitor serial.
// ----------------------------------------------------
void conectarWiFi() {
  Serial.print("Conectando a WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi conectado.");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("No fue posible conectar al WiFi.");
  }
}

// ----------------------------------------------------
// verificarWiFi()
// Comprueba si se perdió la conexión WiFi y, si es así,
// intenta reconectarla.
// ----------------------------------------------------
void verificarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println();
  Serial.println("WiFi perdido. Intentando reconectar...");
  WiFi.disconnect(true);
  delay(1000);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi reconectado.");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    configurarHora();
  } else {
    Serial.println("No se pudo reconectar WiFi.");
  }
}

// ----------------------------------------------------
// iniciarFirebase()
// Configura credenciales, inicia la conexión con Firebase
// y espera hasta que el servicio esté listo.
// ----------------------------------------------------
void iniciarFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  config.token_status_callback = tokenStatusCallback;
  fbdo.setResponseSize(4096);

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.print("Conectando a Firebase");
  unsigned long inicio = millis();
  while (!Firebase.ready() && millis() - inicio < 20000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (Firebase.ready()) {
    Serial.println("Firebase conectado.");
  } else {
    Serial.println("Error al conectar Firebase.");
  }
}

// ----------------------------------------------------
// convertirHumedad()
// Convierte la lectura analógica raw del sensor a un
// porcentaje de humedad entre 0 y 100.
// ----------------------------------------------------
int convertirHumedad(int raw) {
  int porcentaje = map(raw, SENSOR_SECO, SENSOR_MOJADO, 0, 100);
  return constrain(porcentaje, 0, 100);
}

// ----------------------------------------------------
// leerSensorHumedad()
// Realiza varias lecturas del sensor, calcula un promedio,
// valida si el sensor está conectado y actualiza el valor
// de humedad en porcentaje.
// ----------------------------------------------------
void leerSensorHumedad() {
  long suma = 0;

  // Toma 20 muestras para reducir ruido en la lectura
  for (int i = 0; i < 20; i++) {
    suma += analogRead(PIN_HUMEDAD);
    delay(5);
  }

  // Calcula el promedio de las muestras
  humedadRaw = suma / 20;

  // Valida si la lectura está fuera de un rango razonable.
  // Si lo está, se considera sensor desconectado o defectuoso.
  if (humedadRaw < 100 || humedadRaw > 4000) {
    sensorValido = false;
    humedad = 0;

     // Si el sensor acaba de fallar, registra el evento solo una vez
    if (sensorValidoAnterior) {
  Serial.println("Error: sensor de humedad desconectado o fuera de rango.");
  apagarTodo();
  establecerErrorEstado("Sensor desconectado o fuera de rango");
  fbSetString(RUTA_ESTADO_SISTEMA, "Falla en sensor");
  actualizarUltimaActualizacion();
  registrarErrorHistorial("Sensor", "Sensor desconectado o fuera de rango");
  registrarEvento("FallaSensor", "Sensor desconectado o fuera de rango");
}

    sensorValidoAnterior = sensorValido;
    return;
  }

  sensorValido = true;

  // Si el sensor se recuperó tras una falla, registra restauración
  if (!sensorValidoAnterior) {
  Serial.println("Sensor de humedad restaurado.");
  limpiarErrorEstado();
  fbSetString(RUTA_ESTADO_SISTEMA, "Sensor restaurado");
  actualizarUltimaActualizacion();
  registrarEvento("SensorRestaurado", "Sensor de humedad restaurado");
}

  sensorValidoAnterior = sensorValido;

  // Convierte la lectura raw a porcentaje de humedad
  humedad = convertirHumedad(humedadRaw);

  Serial.println("--------------------------------");
  Serial.print("RAW : ");
  Serial.println(humedadRaw);
  Serial.print("Humedad : ");
  Serial.print(humedad);
  Serial.println("%");
}

// ----------------------------------------------------
// enviarDatosFirebase()
// Envía a Firebase la humedad y la lectura raw solo si
// hubo cambios respecto a la última lectura enviada.
// ----------------------------------------------------
void enviarDatosFirebase() {
  if (!Firebase.ready() || !sensorValido) return;

  if (humedad != ultimaHumedadEnviada) {
  fbSetInt(RUTA_HUMEDAD, humedad);
  fbSetInt(RUTA_HUMEDAD_RAW, humedadRaw);
  actualizarUltimaActualizacion();
  ultimaHumedadEnviada = humedad;
}
}

// ----------------------------------------------------
// leerComandosFirebase()
// Lee desde Firebase el nodo de control para actualizar
// modo, umbral y válvulas manuales.
// ----------------------------------------------------
void leerComandosFirebase() {
  // Si Firebase no está listo, sale de inmediato
  if (!Firebase.ready()) return;

  // Lee todo el nodo de control en una sola consulta
  if (!Firebase.RTDB.getJSON(&fbdo, "/RiegoSolar/Control")) {
  Serial.print("Error leyendo nodo Control: ");
  Serial.println(fbdo.errorReason());
  establecerErrorEstado("Error leyendo nodo Control: " + fbdo.errorReason());
  actualizarUltimaActualizacion();
  return;
}

// Si la lectura fue exitosa, limpia el estado de error
limpiarErrorEstado();

  FirebaseJson &json = fbdo.jsonObject();
  FirebaseJsonData resultado;

// Lee el modo de operación: auto o manual
  if (json.get(resultado, "modo")) {
    String nuevoModo = resultado.stringValue;
    if (nuevoModo == "auto" || nuevoModo == "manual") {
      modo = nuevoModo;
    }
  }

// Lee el umbral de humedad permitido
  if (json.get(resultado, "umbralHumedad")) {
    int nuevoUmbral = resultado.intValue;
    if (nuevoUmbral >= 0 && nuevoUmbral <= 100) {
      umbral = nuevoUmbral;
    }
  }

 // Si está en modo manual, aplica directamente el estado
  //     de cada válvula recibido desde Firebase
  if (modo == "manual") {
    if (json.get(resultado, "valvula1")) {
      bool v1 = resultado.boolValue;
      if (v1 != valvula1) {
        valvula1 = v1;
        escribirRele(PIN_VALVULA1, valvula1);
        registrarActivacionManual(1, valvula1 ? "ON" : "OFF");
      }
    }

    if (json.get(resultado, "valvula2")) {
      bool v2 = resultado.boolValue;
      if (v2 != valvula2) {
        valvula2 = v2;
        escribirRele(PIN_VALVULA2, valvula2);
        registrarActivacionManual(2, valvula2 ? "ON" : "OFF");
      }
    }

    if (json.get(resultado, "valvula3")) {
      bool v3 = resultado.boolValue;
      if (v3 != valvula3) {
        valvula3 = v3;
        escribirRele(PIN_VALVULA3, valvula3);
        registrarActivacionManual(3, valvula3 ? "ON" : "OFF");
      }
    }
  }
}

// ----------------------------------------------------
// ejecutarControl()
// Decide qué lógica ejecutar según el modo actual.
// En manual respeta órdenes remotas.
// En automático usa la humedad para regar.
// ----------------------------------------------------
void ejecutarControl() {
  if (modo == "manual") {
    if (secuenciaActiva) {
      registrarEvento("FinRiego", "Secuencia automatica detenida por cambio a modo manual");
      registrarFinCiclo();
    }

    secuenciaActiva = false;
    estadoSecuenciaBombas = 0;

    bool algunaValvulaActiva = valvula1 || valvula2 || valvula3;
    actualizarTemporizadorRiego(algunaValvulaActiva);
    modoManual();
    return;
  }

  if (!sensorValido) {
    apagarTodo();
    return;
  }

  modoAutomatico();
}

// ----------------------------------------------------
// modoAutomatico()
// Inicia o detiene la secuencia automática de riego según
// la humedad y el umbral configurado.
// Usa histéresis para evitar cambios constantes.
// ----------------------------------------------------
void modoAutomatico() {
  bool riegoActivo = secuenciaActiva;

  // Si no hay riego activo y la humedad está por debajo
  // del umbral, inicia un nuevo ciclo automático
  if (!riegoActivo && humedad < umbral) {
    secuenciaActiva = true;
    estadoSecuenciaBombas = 0;
    tiempoSecuenciaBombas = millis();

    registrarEvento("InicioRiego", "Se inicia secuencia automatica");
    registrarInicioCiclo();
  }

    // Si ya estaba regando y la humedad supera el umbral
  // más la histéresis, detiene el riego
  else if (riegoActivo && humedad > (umbral + HISTERESIS)) {
    secuenciaActiva = false;
    estadoSecuenciaBombas = 0;
    apagarBombas();
    actualizarTemporizadorRiego(false);

    registrarEvento("FinRiego", "Humedad alcanzo umbral + histeresis");
    registrarFinCiclo();
    return;
  }

  // Si la secuencia sigue activa, continúa ejecutándola.
  // Si no, asegura que todas las bombas queden apagadas.
  if (secuenciaActiva) {
    ejecutarSecuenciaBombas();
    actualizarTemporizadorRiego(true);
  } else {
    apagarBombas();
    actualizarTemporizadorRiego(false);
  }
}

// ----------------------------------------------------
// modoManual()
// Función reservada para lógica adicional del modo manual.
// Actualmente las válvulas se controlan directamente desde
// leerComandosFirebase().
// ----------------------------------------------------
void modoManual() {
}

// ----------------------------------------------------
// imprimirEstado()
// Muestra en el monitor serial un resumen completo del
// estado actual del sistema.
// ----------------------------------------------------
void imprimirEstado() {
  Serial.println();
  Serial.println("==============================");
  Serial.print("Modo: ");
  Serial.println(modo);
  Serial.print("Humedad RAW: ");
  Serial.println(humedadRaw);
  Serial.print("Humedad: ");
  Serial.print(humedad);
  Serial.println("%");
  Serial.print("Umbral: ");
  Serial.print(umbral);
  Serial.println("%");
  Serial.print("Válvula1: ");
  Serial.println(valvula1 ? "ON" : "OFF");
  Serial.print("Válvula2: ");
  Serial.println(valvula2 ? "ON" : "OFF");
  Serial.print("Válvula3: ");
  Serial.println(valvula3 ? "ON" : "OFF");
  Serial.print("LED: ");
  Serial.println(led ? "ON" : "OFF");
  Serial.print("WiFi: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado");
  Serial.println("==============================");
}

// ----------------------------------------------------
// actualizarEstadoFirebase()
// Publica en Firebase el estado general del dispositivo:
// WiFi, Firebase, IP, estado del sistema y última actualización.
// ----------------------------------------------------
void actualizarEstadoFirebase() {
  if (!Firebase.ready()) return;

  fbSetBool(RUTA_ESTADO_WIFI, WiFi.status() == WL_CONNECTED);
  fbSetBool(RUTA_ESTADO_FIREBASE, Firebase.ready());
  fbSetString(RUTA_ESTADO_IP, WiFi.localIP().toString());
  fbSetString(RUTA_ESTADO_SISTEMA, "ESP32 en linea");
  actualizarUltimaActualizacion();

  if (!hayErrorActivo) {
    fbSetString(RUTA_ESTADO_ERROR, "Sin errores");
  }
}
