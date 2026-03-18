#include <WiFi.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <Wire.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include <Arduino.h>

// sudo chmod a+rw /dev/ttyUSB0
// sudo chmod a+rw /dev/ttyACM0

#define R1 GPIO_NUM_18
#define G1 GPIO_NUM_33
#define B1 GPIO_NUM_19
#define R2 GPIO_NUM_23    //23
#define G2 GPIO_NUM_22    //22
#define B2 GPIO_NUM_32    //32
#define BTN GPIO_NUM_15
#define datchik GPIO_NUM_5
#define motor GPIO_NUM_4

#define termod GPIO_NUM_34

#define POROG     1

BluetoothSerial SerialBT;
Preferences prefs;

struct FeedingTime {
  int hour;
  int minute;
  int portions;
};

const int MAX_SCHEDULE = 10;   // можно задать до 10 событий
FeedingTime schedule[MAX_SCHEDULE];
int scheduleCount = 0;

// Замените на свои данные Wi-Fi
const char* ssid = "*****";
const char* password = "#####";

// Укажите временной сервер и часовой пояс
const char* ntpServer = "pool.ntp.org";
const long utcOffsetInSeconds = 10800; // 10800 -> GMT+3
unsigned long lastRunCounter1m = 0;
int run = 0;

WiFiUDP udp;
NTPClient timeClient(udp, ntpServer, utcOffsetInSeconds);

int blink = 0;

/* этапы:
- тест заряда
+ два светодиода - 6 контактов
+ мотор
+ оптический датчик
+ получение команд с блютус
- приложение на смартфон
- сервер вайфай для настроек
*/

void allLedsOff()
{
    //Serial.println("allOff");
    digitalWrite(R1, 0x1);
    digitalWrite(G1, 0x1);
    digitalWrite(B1, 0x1);
    digitalWrite(R2, 0x1);
    digitalWrite(G2, 0x1);
    digitalWrite(B2, 0x1);
    digitalWrite(motor, 0x0);
}

void blue() {
  digitalWrite(B1, 0x0);
  digitalWrite(B2, 0x0);
  delay(1);
  digitalWrite(B1, 0x1);
  digitalWrite(B2, 0x1);
}

void red() {
  digitalWrite(R1, 0x0);
  digitalWrite(R2, 0x0);
  delay(1);
  digitalWrite(R1, 0x1);
  digitalWrite(R2, 0x1);
}

void green() {
  digitalWrite(G1, 0x0);
  digitalWrite(G2, 0x0);
  delay(1);
  digitalWrite(G1, 0x1);
  digitalWrite(G2, 0x1);
}

void leds() {
  //blue();
  //red();
  green();
}

void connectingIndicate() {
  if(blink % 2 == 0) {
    digitalWrite(R1, 0x0);
    digitalWrite(G1, 0x0);
    digitalWrite(B1, 0x0);
    digitalWrite(R2, 0x0);
    digitalWrite(G2, 0x0);
    digitalWrite(B2, 0x0);
  }
  else {
    digitalWrite(R1, 0x1);
    digitalWrite(G1, 0x1);
    digitalWrite(B1, 0x1);
    digitalWrite(R2, 0x1);
    digitalWrite(G2, 0x1);
    digitalWrite(B2, 0x1);
  }
  blink += 1;
}

bool datchikChanged() {
  static int prevD = 1;
  if(prevD==1 && digitalRead(datchik)==0) {
    prevD = digitalRead(datchik);
    return true;
  }
  prevD = digitalRead(datchik);
  return false;
}

void setup() {
  pinMode(R1, OUTPUT);
  pinMode(G1, OUTPUT);
  pinMode(B1, OUTPUT);
  pinMode(R2, OUTPUT);
  pinMode(G2, OUTPUT);
  pinMode(B2, OUTPUT);
  //pinMode(BTN, INPUT);
  pinMode(datchik, INPUT);
  pinMode(motor, OUTPUT);
  pinMode(termod, INPUT);
  allLedsOff();
  pinMode(BTN, INPUT_PULLUP);
  //pinMode(datchik, INPUT_PULLUP);

  Serial0.begin(115200);
  SerialBT.begin("CatFeeder");

  // загрузка расписания
  prefs.begin("catfeed", true);
  scheduleCount = prefs.getInt("count", 0);
  if (scheduleCount > 0 && scheduleCount <= MAX_SCHEDULE) {
    prefs.getBytes("sched", schedule, sizeof(FeedingTime) * scheduleCount);
  }
  prefs.end();

  // Подключение к Wi-Fi
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial0.println("Подключение к Wi-Fi...");
    connectingIndicate();
  }
  Serial0.println("Подключено к Wi-Fi");
  allLedsOff();

  // Инициализация NTP клиента
  timeClient.begin();

  Serial.println("Bluetooth CatFeeder ready");
}

bool checkStuck() {
  static int value = 0;
  int raw_current = analogRead(termod);

  float current = raw_current * 3.3 / 4095.0 * 1;
  Serial.print("Motor current = ");
  Serial.print(current);

  if (current > POROG) value++;
  else value = 0;

  Serial.print("; threshold ");
  Serial.println(value);

  if (value > 10) { value = 0; return true; }
  else return false;
}

void avaria() {
  run = 0;
  digitalWrite(motor, 0x0);

  for (int i = 0; i<20; i++) {
    for (int j = 0; j<100; j++) {
      red();
      delay(5);
    }
    delay(500);
  }
  run = 1;
  digitalWrite(motor, 0x1);
}

void loop() {
  
  
  static unsigned long lastUpdate = 0; // Переменная для хранения времени последнего обновления

  static unsigned long timeToUpdate = 10000;
  //if(counter<40) { counter += 1; } else timeToUpdate = 120000;

  // Обновляем время из NTP раз в минуту
  if (millis() - lastUpdate > timeToUpdate) { // Каждые 60 секунд
    timeClient.update();

    // Получаем текущее время из NTP
    unsigned long epochTime = timeClient.getEpochTime();

    // Устанавливаем локальное время
    setTime(epochTime); // Учитываем смещение по времени  + utcOffsetInSeconds
    lastUpdate = millis(); // Обновляем время последнего обновления
  }

  // Получаем текущее локальное время
  int hours = hour();
  int minutes = minute();

  // Форматируем время в строку "HH:MM"
  String formattedTime = String(hours) + ":" + (minutes < 10 ? "0" : "") + String(minutes);

  //***********************************************

  // приём команд Bluetooth
  if (SerialBT.available()) {
    String cmd = SerialBT.readStringUntil('\n');
    cmd.trim();

    if (cmd.startsWith("SET")) {
      int h, m, p;
      sscanf(cmd.c_str(), "SET %d:%d %d", &h, &m, &p);
      if (scheduleCount < MAX_SCHEDULE) {
        schedule[scheduleCount++] = {h, m, p};
        SerialBT.println("OK");
      } else {
        SerialBT.println("ERR FULL");
      }
    }

    else if (cmd.startsWith("DEL")) {
      int h, m;
      sscanf(cmd.c_str(), "DEL %d:%d", &h, &m);
      for (int i = 0; i < scheduleCount; i++) {
        if (schedule[i].hour == h && schedule[i].minute == m) {
          for (int j = i; j < scheduleCount - 1; j++) schedule[j] = schedule[j + 1];
          scheduleCount--;
          SerialBT.println("DELETED");
          break;
        }
      }
    }

    else if (cmd == "LIST") {
      for (int i = 0; i < scheduleCount; i++) {
        SerialBT.printf("%02d:%02d -> %d\n", schedule[i].hour, schedule[i].minute, schedule[i].portions);
      }
      if (scheduleCount == 0) SerialBT.println("EMPTY");
    }

    else if (cmd == "CLEAR") {
      scheduleCount = 0;
      SerialBT.println("CLEARED");
    }

    else if (cmd == "SAVE") {
      prefs.begin("catfeed", false);
      prefs.putInt("count", scheduleCount);
      prefs.putBytes("sched", schedule, sizeof(FeedingTime) * scheduleCount);
      prefs.end();
      SerialBT.println("SAVED");
    }

    else if (cmd == "RUN") {
      run = 1;
      SerialBT.println("RUN");
    }
  }

  //***********************************************


  // раз в минуту
  if (millis() - lastRunCounter1m >= 60000) {
    lastRunCounter1m = millis();
    // Выводим время в Serial Monitor
    Serial0.println(formattedTime);
    //if(minutes%5==0) run = 2;

    //***********************************************
    for (int i = 0; i < scheduleCount; i++) {
      // для отладки наглядность: печать расписания в консоль
      Serial.printf("%02d:%02d -> %d\n", schedule[i].hour, schedule[i].minute, schedule[i].portions);
      if (hours == schedule[i].hour && minutes == schedule[i].minute) {
        run = schedule[i].portions;
      }
    }
    //***********************************************

  }

  if(digitalRead(BTN)==0) {
    Serial0.println("button pressed");
    run = 1;
  }

  if(run) {
    digitalWrite(motor, 0x1);
    Serial.print("datchik = ");
    Serial.print(digitalRead(datchik));
    Serial.print(";  run = ");
    Serial.println(run);
    if (checkStuck()) avaria();
  }
  else digitalWrite(motor, 0x0);

  if(datchikChanged()) {
    run -= 1;
    Serial.print("run - 1;  run = ");
    Serial.println(run);
    if(run<0) run = 0;
  }

  leds();
  
  delay(20);
}
