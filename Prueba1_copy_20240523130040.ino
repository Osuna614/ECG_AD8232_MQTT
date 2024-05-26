#include <WiFi.h>
#include <PubSubClient.h>
//ad8232
long instance1 = 0, timer;
double hrv = 0, hr = 72, interval = 0;
int value = 0, count = 0;  
int value1 = 0;
bool flag = 0;

#define shutdown_pin 10 
#define threshold 1500 // to identify R peak
#define timer_value 10000 // 10 seconds timer to calculate hr

const int maxRRIntervals = 10; // maximum number of RR intervals to store
double rrIntervals[maxRRIntervals]; // array to store RR intervals
int rrIndex = 0; // index for storing RR intervals


//declaramos el nombre de la red wifi donde nos conectaremos y su contraseña
const char* ssid = "ProyectosElectronica";
const char* password = "proyectos";
const char* mqtt_server = "broker.emqx.io:1883";


WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;


void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  randomSeed(micros());
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.publish("outTopic", "T1");
      client.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
void graficarDatosTiempo(String etiqueta, float dato, float tiempo) {
  // Enviar etiqueta, dato y tiempo por el puerto serie
  Serial.print(etiqueta);
  Serial.print(": ");
  Serial.print(dato);
  Serial.print(",");
  Serial.println(tiempo); 
}

String clasificarRitmoCardiaco(double hr,double interval) {
  String clasificacion = "";

  // Check for ventricular fibrillation
  if (isIntervalIrregular()) {
    clasificacion = "Posible Fibrilación Ventricular";
  }
  else if (hr > 100) {
    clasificacion = "Taquicardia";
  }
  else if (hr < 60) {
    clasificacion = "Bradicardia";
  }
  else {
    clasificacion = "Ritmo normal en reposo";
  }

  return clasificacion;
}

bool isIntervalIrregular() {
  // Calculate the standard deviation of RR intervals
  double sum = 0;
  double mean = 0;
  double sq_sum = 0;
  int validIntervals = 0;

  // Calculate the mean of the intervals
  for (int i = 0; i < maxRRIntervals; i++) {
    if (rrIntervals[i] > 0) {
      sum += rrIntervals[i];
      validIntervals++;
    }
  }

  if (validIntervals == 0) return false;

  mean = sum / validIntervals;

  // Calculate the squared sum of differences from the mean
  for (int i = 0; i < validIntervals; i++) {
    sq_sum += (rrIntervals[i] - mean) * (rrIntervals[i] - mean);
  }

  double stdev = sqrt(sq_sum / validIntervals);

  // Consider intervals irregular if the standard deviation exceeds a threshold
  return (stdev > 100); // threshold for standard deviation, this value can be adjusted
}


void setup() {
  Serial.begin(115200);
  pinMode(14, INPUT); // Setup for leads off detection LO +
  pinMode(12, INPUT); // Setup for leads off detection LO -

  // Initialize the RR intervals array
  for (int i = 0; i < maxRRIntervals; i++) {
    rrIntervals[i] = 0;
  }

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  delay(1000);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if ((digitalRead(12) == 1) || (digitalRead(14) == 1)) {
    Serial.println("leads off!");
    digitalWrite(shutdown_pin, LOW); //standby mode
    instance1 = micros();
    timer = millis();
  } else {
    digitalWrite(shutdown_pin, HIGH); //normal mode
    value1 = analogRead(25);
    value = map(value1, 250, 400, 0, 100); //to flatten the ecg values a bit
    int senal = map(value, 800, 2500, 0, 100);
    
    if ((value > threshold) && (!flag)) {
      count++;  
      flag = 1;
      interval = micros() - instance1; //RR interval
      instance1 = micros(); 
      rrIntervals[rrIndex] = interval;
      rrIndex = (rrIndex + 1) % maxRRIntervals;
    } else if ((value < threshold)) {
      flag = 0;
    }

    if ((millis() - timer) > 10000) {
      hr = count * 6;
      timer = millis();
      count = 0; 
    }

    hrv = hr / 60 - interval / 1000000;
    String ritmo = clasificarRitmoCardiaco(hr,interval);
    Serial.print("BPM: ");
    Serial.print(hr);
    Serial.print(" , ");
    Serial.print(ritmo);
    float mV = (value1 - 2048) * (3.3 / 4096.0) * 1000; 
    // Tiempo en segundos desde el inicio del programa
    float tiempo = millis() / 1000.0;
    // Llamar a la función para graficar los datos convertidos
    graficarDatosTiempo(", Milivoltios", mV, tiempo);

    delay(10);

    // Publicar datos de ECG
    char ecgStr[8];
    dtostrf(mV, 1, 2, ecgStr);
    client.publish("/ecg_data", ecgStr);

    // Publicar BPM
    char bpmStr[8];
    dtostrf(hr, 1, 2, bpmStr);
    client.publish("/hr", bpmStr);

    // Publicar ritmo
    client.publish("/ritmo", ritmo.c_str());

    // Verificar y publicar anomalía
    if (isIntervalIrregular()) {
      client.publish("/anomaly", "true");
    } else {
      client.publish("/anomaly", "false");
    }
  }
}
