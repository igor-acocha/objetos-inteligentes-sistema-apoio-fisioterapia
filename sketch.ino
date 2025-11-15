#include <WiFi.h>
#include <U8g2lib.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

// ------------------ CONFIG WIFI ------------------
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// ------------------ CONFIG ADAFRUIT IO ------------------
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "iacocha"   // usuário Adafruit IO
#define AIO_KEY  "aio_iTol51EVB4xKTVXjPVnQoo6tyP4l"   //  chave AIO

WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// Feeds existentes
Adafruit_MQTT_Publish feedRepeticoes = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/repeticoes");
Adafruit_MQTT_Publish feedFrequencia  = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/frequencia");
Adafruit_MQTT_Publish feedQualidade   = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/qualidade_rep");
Adafruit_MQTT_Publish feedQualidadeMedia = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/qualidade_media_sessao");

// --- Feeds para latência (RTT) ---
Adafruit_MQTT_Publish feedLatencia = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/latencia");         // envia timestamp
Adafruit_MQTT_Publish feedLatRtt  = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/latencia_rtt");     // opcional: publica RTT medido

// subscribe para receber o echo do Adafruit IO (automation deve publicar em latencia_echo)
Adafruit_MQTT_Subscribe feedLatEchoSub = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/latencia_echo");

// ------------------ CONFIG HARDWARE ------------------
#define BUZZER_PIN 16
#define SCL_PIN 22
#define SDA_PIN 21

Adafruit_MPU6050 accel;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN);

// ------------------ VARIÁVEIS ------------------
int repeticoes_meta = 10;
int repeticoes_atuais = 0;
bool estado_exercicio = false;

float LIMITE_Z_MOVIMENTO = 0.5;
unsigned long ultimo_tempo = 0;
unsigned long ultimoOLED = 0;

float soma_tempos = 0;
float tempo_medio = 0;
float freq_media = 0;
float tempo_segundos = 0;

// Timer de pausa
unsigned long inicioPausa = 0;
unsigned long TEMPO_PAUSA = 1800000; // 30mins

// Variavéis de aviso
bool mostrarAviso = false;
unsigned long inicioAviso = 0;
unsigned long DURACAO_AVISO = 5000;

// ------------------ VARIÁVEIS PARA SCORE (coleção por repetição) ------------------
bool coletando_rep = false;
float rep_z_max = -999.0;
float rep_z_min = 999.0;

// jerk (variação de aceleração) para suavidade
float last_accel_z = 0.0;
double jerk_sum_sq = 0.0;
unsigned long jerk_samples = 0;

// constantes de configuração (ajustáveis)
const float TARGET_AMPLITUDE_G = 0.6f; // amplitude esperada em g (proxy)
const float TARGET_TEMPO_S = 2.0f;     // tempo ideal por repetição (segundos)
const float JERK_NORM = 0.5f;          // valor empírico para normalizar jerk RMS

// última qualidade calculada (para exibir)
float qualidade_rep = 0.0f;

// acumulação para média da sessão
double soma_qualidade = 0.0;
float qualidade_media_sessao = 0.0f;

// ------------------ VARIÁVEIS DE LATÊNCIA ------------------
unsigned long lastLatencySentTimestamp = 0; // guarda o timestamp enviado mais recente (ms)
unsigned long lastMeasuredRTT = 0; // último RTT medido (ms)
unsigned long lastLatencyDisplayMillis = 0; // quando mostramos a latência no OLED
const unsigned long LAT_DISPLAY_DURATION = 4000; // ms que mostra o Lat no OLED

// ------------------ FUNÇÕES ------------------
void MQTT_connect() {
  int8_t ret;
  if (mqtt.connected()) return;

  Serial.print("Conectando ao MQTT...");
  while ((ret = mqtt.connect()) != 0) {
    Serial.println(mqtt.connectErrorString(ret));
    delay(5000);
  }
  Serial.println("MQTT conectado!");

  // (Re)subscribe após conectar
  mqtt.subscribe(&feedLatEchoSub);
}

// clamp helper
float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Envia timestamp para medir RTT (publica no feed "latencia")
void enviarTimestampParaLatencia() {
  unsigned long t0 = millis();
  char payload[24];
  // envia valor em ms como string
  snprintf(payload, sizeof(payload), "%lu", t0);
  if (feedLatencia.publish(payload)) {
    lastLatencySentTimestamp = t0;
    Serial.print("Timestamp enviado para latencia: ");
    Serial.println(payload);
  } else {
    Serial.println("Falha ao publicar timestamp de latencia");
  }
}

// ------------------ SETUP ------------------
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);

  // Sensor
  if (!accel.begin()) {
    Serial.println("Ops, não foi possível detectar o MPU6050. Cheque o wiring!");
    while (1);
  }

  // Ajustes do MPU (opcionais)
  accel.setAccelerometerRange(MPU6050_RANGE_8_G);
  accel.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // OLED
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf); // fonte pequena e legível, display menos poluído

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");
  Serial.println(WiFi.localIP());

  // MQTT
  MQTT_connect();

  // opcional: enviar um primeiro timestamp após iniciar (para testar)
  delay(500); // aguarda conexão estabilizar
  enviarTimestampParaLatencia();
}

// ------------------ LOOP ------------------
void loop() {
  // Garante conexão MQTT
  MQTT_connect();

  // processa pacotes MQTT (chave para receber o echo)
  // mantenho processPackets para garantir troca de pacotes; readSubscription também fará leitura
  mqtt.processPackets(50);

  // Lê dados do sensor
  sensors_event_t a, g, t;
  accel.getEvent(&a, &g, &t);
  float z_value = a.acceleration.z;

  // cálculo de jerk (variação de aceleração z)
  float jerk = a.acceleration.z - last_accel_z;
  last_accel_z = a.acceleration.z;

  // lógica de detecção de repetição (mantive sua lógica geral)
  if (repeticoes_atuais < repeticoes_meta) {
    if (z_value > LIMITE_Z_MOVIMENTO && !estado_exercicio) {
      // início de movimento
      estado_exercicio = true;
      digitalWrite(BUZZER_PIN, HIGH);
      delay(50);
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("MOVIMENTO DETECTADO!");

      // inicia coleta para esta repetição
      coletando_rep = true;
      rep_z_max = z_value;
      rep_z_min = z_value;
      jerk_sum_sq = 0.0;
      jerk_samples = 0;
    } 
    else if (z_value < (LIMITE_Z_MOVIMENTO * 0.5) && estado_exercicio) {
      // fim da repetição
      unsigned long agora = millis();
      if (ultimo_tempo > 0) {
        unsigned long tempo_entre_reps = agora - ultimo_tempo;
        tempo_segundos = tempo_entre_reps / 1000.0;
        float freq = 60000.0 / tempo_entre_reps;
        Serial.print("Tempo entre reps: ");
        Serial.print(tempo_segundos);
        Serial.println(" s");
        Serial.print("Frequência instantânea (RPM): ");
        Serial.print(freq);
        Serial.println(" RPM");
      }

      ultimo_tempo = agora;

      repeticoes_atuais++;
      estado_exercicio = false;

      // ---- Ao finalizar a repetição: calcula score de qualidade ----
      float amplitude_g = fabs(rep_z_max - rep_z_min); // proxy de amplitude em g

      // jerk RMS
      float jerk_rms = 0.0f;
      if (jerk_samples > 0) {
        jerk_rms = sqrt(jerk_sum_sq / (double)jerk_samples);
      }

      // amplitude score: 0..1
      float amp_score = clampf(amplitude_g / TARGET_AMPLITUDE_G, 0.0f, 1.0f);

      // tempo score: 1 quando tempo == TARGET_TEMPO_S, linear decay
      float tempo_score = 0.0f;
      if (tempo_segundos > 0) {
        float diff = fabs(tempo_segundos - TARGET_TEMPO_S);
        tempo_score = clampf(1.0f - (diff / TARGET_TEMPO_S), 0.0f, 1.0f);
      }

      // smoothness score: 1 - normalized jerk (lower jerk => closer to 1)
      float jerk_norm = clampf(jerk_rms / JERK_NORM, 0.0f, 1.0f);
      float smooth_score = 1.0f - jerk_norm;

      // combinação final: amplitude 40%, tempo 40%, suavidade 20%
      qualidade_rep = (amp_score * 0.4f + tempo_score * 0.4f + smooth_score * 0.2f) * 100.0f;
      qualidade_rep = clampf(qualidade_rep, 0.0f, 100.0f);

      Serial.print("Amplitude (g): "); Serial.println(amplitude_g, 3);
      Serial.print("Jerk RMS: "); Serial.println(jerk_rms, 4);
      Serial.print("Qualidade rep (%): "); Serial.println(qualidade_rep);

      // Atualiza média da sessão
      soma_qualidade += (double)qualidade_rep;
      qualidade_media_sessao = (float)(soma_qualidade / (double)repeticoes_atuais);

      // Atualiza média de tempo e frequência
      soma_tempos += tempo_segundos;
      tempo_medio = soma_tempos / repeticoes_atuais;
      freq_media = (tempo_medio > 0) ? 60.0 / tempo_medio : 0; // RPM

      Serial.print("REPETIÇÃO: ");
      Serial.println(repeticoes_atuais);

      // Envia repetições
      if (!feedRepeticoes.publish((int32_t)repeticoes_atuais)) {
        Serial.println("Falha ao publicar repeticoes :(");
      } else {
        Serial.println("Repeticoes enviadas!");
      }

      // Envia frequência média
      if (!feedFrequencia.publish(freq_media)) {
        Serial.println("Falha ao publicar frequencia :(");
      } else {
        Serial.println("Frequencia enviada!");
      }

      // Envia qualidade (%) via MQTT
      if (!feedQualidade.publish(qualidade_rep)) {
        Serial.println("Falha ao publicar qualidade :(");
      } else {
        Serial.println("Qualidade enviada!");
      }

      // Envia qualidade média da sessão via MQTT
      if (!feedQualidadeMedia.publish(qualidade_media_sessao)) {
        Serial.println("Falha ao publicar qualidade_media_sessao :(");
      } else {
        Serial.println("Qualidade media (sessao) enviada!");
      }

      // opcional: após cada repetição podemos sondar latência (ou você pode acionar manualmente)
      // aqui envio um timestamp para medir RTT
      enviarTimestampParaLatencia();

      // encerra coleta
      coletando_rep = false;
    }
  }

  // ---- Coleta contínua durante a repetição: atualiza máximos/minimos e acumula jerk ----
  if (coletando_rep) {
    if (z_value > rep_z_max) rep_z_max = z_value;
    if (z_value < rep_z_min) rep_z_min = z_value;

    jerk_sum_sq += (double)jerk * (double)jerk;
    jerk_samples++;
  }

  // ---- TIMER DE PAUSA ----
  if (millis() - inicioPausa > TEMPO_PAUSA) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);

    inicioPausa = millis();

    // Ativa aviso no OLED
    mostrarAviso = true;
    inicioAviso = millis();
  }

  // ----------------------------
  // Processa subscriptions corretamente usando mqtt.readSubscription(...)
  // ----------------------------
  Adafruit_MQTT_Subscribe *subscription;
  // readSubscription(timeout_ms) retorna ponteiro para a subscription que recebeu
  while ((subscription = mqtt.readSubscription(10))) {
    if (subscription == &feedLatEchoSub) {
      // payload recebido do Adafruit IO (o echo da automation)
      char *payload = (char *)subscription->lastread;
      if (payload != NULL) {
        unsigned long t_sent = strtoul(payload, NULL, 10);
        unsigned long now = millis();
        if (t_sent > 0) {
          unsigned long rtt = now - t_sent;
          lastMeasuredRTT = rtt;
          lastLatencyDisplayMillis = millis();

          Serial.print("RTT medido (ms): ");
          Serial.println(rtt);

          // publica RTT no feed latencia_rtt (opcional)
          if (!feedLatRtt.publish((int32_t)rtt)) {
            Serial.println("Falha ao publicar latencia_rtt");
          } else {
            Serial.println("latencia_rtt publicada");
          }
        }
      }
    }
    // caso tenha outros subscriptions no futuro, trate aqui
  }

  // Exibição no OLED (atualiza a cada 200ms; layout minimalista)
  if (millis() - ultimoOLED >= 200) {
    ultimoOLED = millis();
    u8g2.clearBuffer();

    if (mostrarAviso) {
      if (millis() - inicioAviso < DURACAO_AVISO) {
        u8g2.setCursor(0, 12);
        u8g2.print("Hora da pausa!");
        u8g2.setCursor(0, 28);
        u8g2.print("Levante-se :)");
      } else {
        mostrarAviso = false;
      }
    } else {
      // Layout limpo: título, meta, freq média e qualidade (última + média sessão)
      u8g2.setCursor(0, 10);
      u8g2.print("Fisio IoT");

      u8g2.setCursor(0, 26);
      u8g2.print("Meta: ");
      u8g2.print(repeticoes_atuais);
      u8g2.print("/");
      u8g2.print(repeticoes_meta);

      u8g2.setCursor(0, 42);
      u8g2.print("Freq: ");
      u8g2.print(freq_media, 1);
      u8g2.print(" RPM");

      // Qualidade última e média em uma linha compacta
      u8g2.setCursor(0, 58);
      u8g2.print("Qult:");
      u8g2.print((int)qualidade_rep);
      u8g2.print("% ");

      u8g2.print("Qavg:");
      u8g2.print((int)qualidade_media_sessao);
      u8g2.print("%");

      // Se medimos uma RTT recentemente, mostramos Lat: XXms (por curto período)
      if (millis() - lastLatencyDisplayMillis <= LAT_DISPLAY_DURATION && lastMeasuredRTT > 0) {
        u8g2.setCursor(96, 58);
        u8g2.print("Lat:");
        u8g2.print((int)lastMeasuredRTT);
        u8g2.print("ms");
      }

      if (repeticoes_atuais >= repeticoes_meta) {
        u8g2.setCursor(64, 58);
        u8g2.print("CONCLUIDO");
      }
    }

    u8g2.sendBuffer();
  }
}
