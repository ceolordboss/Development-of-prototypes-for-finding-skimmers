#include <GyverPortal.h>
#include <FileData.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_ADS1X15.h>
#include <ArduinoJson.h>

// =============================================================================
// КОНСТАНТЫ И НАСТРОЙКИ ПИНОВ
// =============================================================================

#define BUTTON_PIN D3        // Кнопка (с подтяжкой к VCC через 10к)
#define BUZZER_PIN D6        // Зуммер (через транзистор)
#define LED_PIN D4           // WS2812B
#define WAKEUP_PIN D0        // Пин для пробуждения (GPIO16)
#define LED_COUNT 1          // Количество светодиодов WS2812B

// Константы по умолчанию
#define SCAN_DURATION_DEFAULT 2000   // Длительность сканирования (мс)
#define SLEEP_TIMEOUT_DEFAULT 30000  // Таймаут до сна (мс)
#define SAMPLE_COUNT 100             // Количество считываний для калибровки
#define AP_TIMEOUT 300000           // Таймаут точки доступа (5 мин)

// =============================================================================
// СТРУКТУРА ДЛЯ ХРАНЕНИЯ НАСТРОЕК В ФАЙЛОВОЙ СИСТЕМЕ
// =============================================================================

struct Settings {
  // Калибровочные значения для каждого датчика
  int hallBaseline[4] = {0, 0, 0, 0};
  
  // Пороги срабатывания (отклонение от базовой линии)
  int hallThresholds[4] = {100, 100, 100, 100};
  
  // Пороговые значения для индикации
  int warningThreshold = 50;    // Желтый свет
  int dangerThreshold = 100;    // Красный свет + звук
  
  // Настройки энергосбережения
  int scanDuration = 2000;      // Длительность сканирования (мс)
  int sleepTimeout = 30000;     // Таймаут до сна (мс)
  
  // Настройки Wi-Fi
  char apSSID[32] = "SkimmerDetector";
  char apPassword[32] = "12345678";
  
  // Флаги
  bool calibrated = false;
  bool soundEnabled = true;
  bool debugMode = false;
};

// =============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// =============================================================================

Settings settings;
FileData memory(&LittleFS, "/antiskim.dat", 'B', &settings, sizeof(settings));

// Объекты устройств
Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_ADS1115 ads;
GyverPortal ui;

// Состояние системы
enum SystemState {
  STATE_SLEEP,
  STATE_SCANNING,
  STATE_CONFIG_MODE
};

SystemState currentState = STATE_SLEEP;
unsigned long buttonPressTime = 0;
bool buttonPressed = false;
bool inDeepSleep = false;
unsigned long lastActivityTime = 0;

// Буфер для показаний датчиков
int sensorValues[4] = {0, 0, 0, 0};
int sensorDeviations[4] = {0, 0, 0, 0};

// =============================================================================
// ФУНКЦИИ ИНДИКАЦИИ И ЗВУКА
// =============================================================================

void updateLED(uint8_t r, uint8_t g, uint8_t b, bool blink = false) {
  if (blink) {
    for (int i = 0; i < 3; i++) {
      led.setPixelColor(0, led.Color(r, g, b));
      led.show();
      delay(200);
      led.setPixelColor(0, led.Color(0, 0, 0));
      led.show();
      delay(200);
    }
  } else {
    led.setPixelColor(0, led.Color(r, g, b));
    led.show();
  }
}

void playSound(int frequency, int duration) {
  if (settings.soundEnabled) {
    analogWriteFreq(frequency);
    analogWrite(BUZZER_PIN, 512);
    delay(duration);
    analogWrite(BUZZER_PIN, 0);
  }
}

// =============================================================================
// ФУНКЦИИ РАБОТЫ С ДАТЧИКАМИ
// =============================================================================

void readSensors() {
  // Чтение 4 каналов ADS1115
  sensorValues[0] = ads.readADC_SingleEnded(0);
  sensorValues[1] = ads.readADC_SingleEnded(1);
  sensorValues[2] = ads.readADC_SingleEnded(2);
  sensorValues[3] = ads.readADC_SingleEnded(3);
  
  // Расчет отклонений от базовых значений
  for (int i = 0; i < 4; i++) {
    sensorDeviations[i] = abs(sensorValues[i] - settings.hallBaseline[i]);
  }
}

int detectAnomaly() {
  int maxDeviation = 0;
  
  for (int i = 0; i < 4; i++) {
    if (sensorDeviations[i] > maxDeviation) {
      maxDeviation = sensorDeviations[i];
    }
  }
  
  return maxDeviation;
}

void calibrateSensors() {
  updateLED(0, 255, 255, true);  // Голубой - калибровка
  
  // Средние значения за несколько измерений
  long sums[4] = {0, 0, 0, 0};
  
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    readSensors();
    for (int j = 0; j < 4; j++) {
      sums[j] += sensorValues[j];
    }
    delay(10);
  }
  
  // Сохранение калибровочных значений
  for (int i = 0; i < 4; i++) {
    settings.hallBaseline[i] = sums[i] / SAMPLE_COUNT;
  }
  
  settings.calibrated = true;
  memory.update();
  
  // Сигнал об успешной калибровки
  updateLED(0, 255, 0, false);  // Зеленый
  playSound(2000, 200);
  delay(200);
  playSound(2000, 200);
}

// =============================================================================
// ФУНКЦИИ РЕЖИМОВ РАБОТЫ
// =============================================================================

void startScanning() {
  currentState = STATE_SCANNING;
  updateLED(0, 0, 255, true);  // Синий - начало сканирования
  
  // Считывание датчиков
  readSensors();
  
  // Определение аномалии
  int anomaly = detectAnomaly();
  
  // Индикация результата
  if (anomaly < settings.warningThreshold) {
    // Норма
    updateLED(0, 255, 0, false);  // Зеленый
    playSound(1000, 100);         // Короткий звук
  } else if (anomaly < settings.dangerThreshold) {
    // Предупреждение
    updateLED(255, 150, 0, true);  // Желтый мигающий
    playSound(1500, 500);          // Средний звук
  } else {
    // Опасность - обнаружен скиммер
    updateLED(255, 0, 0, true);    // Красный мигающий
    playSound(2000, 1000);         // Длинный звук
  }
  
  // Ждем указанное время сканирования
  delay(settings.scanDuration);
  
  // Выключаем индикацию
  updateLED(0, 0, 0, false);
  
  // Возвращаемся в сон
  currentState = STATE_SLEEP;
  goToSleep();
}

void goToSleep() {
  if (inDeepSleep) return;
  
  // Выключаем светодиод
  updateLED(0, 0, 0, false);
  
  // Отключаем периферию
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  
  // Настраиваем пробуждение по кнопке
  pinMode(WAKEUP_PIN, WAKEUP_PULLUP);
  ESP.deepSleep(0);
  
  inDeepSleep = true;
}

// =============================================================================
// ВЕБ-ИНТЕРФЕЙС НА GYVERPORTAL
// =============================================================================

void buildWebInterface() {
  GP.BUILD_BEGIN();
  GP.PAGE_TITLE("Anti-Skimmer Detector");
  GP.THEME(GP_DARK);
  GP.TITLE("Детектор скиммеров банкоматов");
  
  // Текущие показания датчиков
  GP.LABEL("Текущие показания датчиков", "", "#00ff00", 20);
  GP.BREAK();
  
  String sensorInfo = "";
  sensorInfo += "Датчик 1: " + String(sensorValues[0]) + " (откл.: " + String(sensorDeviations[0]) + ")<br>";
  sensorInfo += "Датчик 2: " + String(sensorValues[1]) + " (откл.: " + String(sensorDeviations[1]) + ")<br>";
  sensorInfo += "Датчик 3: " + String(sensorValues[2]) + " (откл.: " + String(sensorDeviations[2]) + ")<br>";
  sensorInfo += "Датчик 4: " + String(sensorValues[3]) + " (откл.: " + String(sensorDeviations[3]) + ")";
  
  GP.LABEL(sensorInfo, "", "#ffffff", 12);
  GP.BREAK();
  
  // Базовые значения
  GP.LABEL("Базовые значения (калибровка)", "", "#00ff00", 20);
  GP.BREAK();
  
  String baselineInfo = "";
  baselineInfo += "Датчик 1: " + String(settings.hallBaseline[0]) + "<br>";
  baselineInfo += "Датчик 2: " + String(settings.hallBaseline[1]) + "<br>";
  baselineInfo += "Датчик 3: " + String(settings.hallBaseline[2]) + "<br>";
  baselineInfo += "Датчик 4: " + String(settings.hallBaseline[3]);
  
  GP.LABEL(baselineInfo, "", "#aaaaaa", 12);
  GP.BREAK();
  
  GP.FORM_BEGIN("/calibrate");
  GP.SUBMIT("Калибровать датчики", GP_GREEN);
  GP.FORM_END();
  
  GP.BREAK();
  GP.HR();
  
  // Настройки порогов
  GP.FORM_BEGIN("/save");
  GP.LABEL("Настройки порогов срабатывания", "", "#0080ff", 20);
  GP.BREAK();
  
  GP.LABEL("Порог предупреждения (желтый)", "", "#0080ff", 12);
  GP.BREAK();
  GP.SPINNER("warning", settings.warningThreshold, 1, 1000, 1, 0, GP_GREEN, "100px");
  GP.BREAK();
  
  GP.LABEL("Порог опасности (красный)", "", "#0080ff", 12);
  GP.BREAK();
  GP.SPINNER("danger", settings.dangerThreshold, 1, 1000, 1, 0, GP_GREEN, "100px");
  GP.BREAK();
  
  GP.LABEL("Пороги для каждого датчика", "", "#0080ff", 12);
  GP.BREAK();
  
  GP.LABEL("Датчик 1:", "", "#0080ff", 10);
  GP.SPINNER("th1", settings.hallThresholds[0], 1, 1000, 1, 0, GP_GREEN, "80px");
  GP.BREAK();
  
  GP.LABEL("Датчик 2:", "", "#0080ff", 10);
  GP.SPINNER("th2", settings.hallThresholds[1], 1, 1000, 1, 0, GP_GREEN, "80px");
  GP.BREAK();
  
  GP.LABEL("Датчик 3:", "", "#0080ff", 10);
  GP.SPINNER("th3", settings.hallThresholds[2], 1, 1000, 1, 0, GP_GREEN, "80px");
  GP.BREAK();
  
  GP.LABEL("Датчик 4:", "", "#0080ff", 10);
  GP.SPINNER("th4", settings.hallThresholds[3], 1, 1000, 1, 0, GP_GREEN, "80px");
  GP.BREAK();
  GP.HR();
  
  // Настройки таймингов
  GP.LABEL("Настройки таймингов", "", "#0080ff", 20);
  GP.BREAK();
  
  GP.LABEL("Длительность сканирования (мс)", "", "#0080ff", 12);
  GP.BREAK();
  GP.SPINNER("scan_duration", settings.scanDuration, 500, 10000, 100, 0, GP_GREEN, "100px");
  GP.BREAK();
  
  GP.LABEL("Таймаут до сна (мс)", "", "#0080ff", 12);
  GP.BREAK();
  GP.SPINNER("sleep_timeout", settings.sleepTimeout, 5000, 60000, 1000, 0, GP_GREEN, "100px");
  GP.BREAK();
  GP.HR();
  
  // Дополнительные настройки - ИСПРАВЛЕННЫЙ СИНТАКСИС
  GP.LABEL("Дополнительные настройки", "", "#0080ff", 20);
  GP.BREAK();
  
  // Правильный синтаксис для чекбоксов в GyverPortal
  GP.LABEL("Включить звук", "", "#0080ff", 12);
  GP.CHECK("sound_enabled", settings.soundEnabled);
  GP.BREAK();
  
  GP.LABEL("Режим отладки", "", "#0080ff", 12);
  GP.CHECK("debug_mode", settings.debugMode);
  GP.BREAK();
  GP.HR();
  
  // Настройки Wi-Fi
  GP.LABEL("Настройки Wi-Fi точки доступа", "", "#0080ff", 20);
  GP.BREAK();
  
  GP.LABEL("Имя точки доступа (SSID)", "", "#0080ff", 12);
  GP.BREAK();
  GP.TEXT("ap_ssid", "SSID", settings.apSSID);
  GP.BREAK();
  
  GP.LABEL("Пароль", "", "#0080ff", 12);
  GP.BREAK();
  GP.TEXT("ap_pass", "Пароль", settings.apPassword);
  GP.BREAK();
  GP.HR();
  
  GP.SUBMIT("Сохранить настройки");
  GP.FORM_END();
  
  GP.BREAK();
  GP.BREAK();
  
  // Кнопки управления
  GP.FORM_BEGIN("/reboot");
  GP.SUBMIT("Перезагрузить устройство", GP_RED);
  GP.FORM_END();
  
  GP.BREAK();
  GP.BREAK();
  
  GP.FORM_BEGIN("/test_scan");
  GP.SUBMIT("Тестовое сканирование", GP_BLUE);
  GP.FORM_END();
  
  GP.BREAK();
  GP.LABEL("После сохранения устройство автоматически перейдет в сон", "", "#ff9900", 12);
  
  GP.BUILD_END();
}

void handleWebInterface(GyverPortal& p) {
  // Калибровка
  if (p.form("/calibrate")) {
    calibrateSensors();
    p.answer("Калибровка завершена");
    delay(1000);
  }
  
  // Сохранение настроек
  if (p.form("/save")) {
    settings.warningThreshold = p.getInt("warning");
    settings.dangerThreshold = p.getInt("danger");
    settings.hallThresholds[0] = p.getInt("th1");
    settings.hallThresholds[1] = p.getInt("th2");
    settings.hallThresholds[2] = p.getInt("th3");
    settings.hallThresholds[3] = p.getInt("th4");
    settings.scanDuration = p.getInt("scan_duration");
    settings.sleepTimeout = p.getInt("sleep_timeout");
    
    // Правильное получение состояния чекбоксов
    settings.soundEnabled = p.getCheck("sound_enabled");
    settings.debugMode = p.getCheck("debug_mode");
    
    p.copyStr("ap_ssid", settings.apSSID);
    p.copyStr("ap_pass", settings.apPassword);
    
    memory.updateNow();
    p.answer("Настройки сохранены. Устройство перейдет в сон.");
    delay(2000);
    goToSleep();
  }
  
  // Перезагрузка
  if (p.form("/reboot")) {
    p.answer("Перезагрузка...");
    delay(1000);
    ESP.restart();
  }
  
  // Тестовое сканирование
  if (p.form("/test_scan")) {
    p.answer("Запуск тестового сканирования...");
    startScanning();
  }
}

void enterConfigMode() {
  currentState = STATE_CONFIG_MODE;
  lastActivityTime = millis();
  
  // Светодиод синий
  updateLED(0, 0, 255, false);
  
  // Запуск точки доступа
  WiFi.mode(WIFI_AP);
  WiFi.softAP(settings.apSSID, settings.apPassword);
  
  // Инициализация веб-интерфейса
  ui.attachBuild(buildWebInterface);
  ui.attach(handleWebInterface);
  ui.start();
  
  // Обновляем показания датчиков для веб-интерфейса
  readSensors();
}

// =============================================================================
// ОСНОВНЫЕ ФУНКЦИИ
// =============================================================================

void setup() {
  // Инициализация пинов
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  analogWrite(BUZZER_PIN, 0);
  
  // Инициализация светодиода
  led.begin();
  led.setBrightness(50);
  updateLED(255, 0, 0, true);  // Красный мигающий - инициализация
  
  // Инициализация файловой системы
  if (!LittleFS.begin()) {
    LittleFS.format();
    LittleFS.begin();
  }
  
  // Загрузка настроек
  memory.read();
  
  // Инициализация ADS1115
  if (!ads.begin()) {
    // Ошибка инициализации ADS1115
    updateLED(255, 0, 0, true);  // Красный мигающий
    playSound(500, 1000);
    delay(2000);
  }
  
  // Установка усиления ADS1115
  ads.setGain(GAIN_ONE);  // ±4.096V
  
  // Проверяем состояние кнопки
  buttonPressed = (digitalRead(BUTTON_PIN) == LOW);
  
  if (buttonPressed) {
    buttonPressTime = millis();
    
    // Ждем, чтобы определить длительность нажатия
    while (digitalRead(BUTTON_PIN) == LOW) {
      if (millis() - buttonPressTime > 3000) {  // Долгое нажатие 3 секунды
        // Долгое нажатие - режим настройки
        updateLED(0, 0, 255, true);  // Синий мигающий
        delay(500);
        enterConfigMode();
        return;
      }
      delay(10);
    }
    
    // Короткое нажатие - сканирование
    startScanning();
  } else if (!settings.calibrated) {
    // Если не откалиброван - автоматически режим настройки
    enterConfigMode();
  } else {
    // Если ничего не нажато и откалиброван - сон
    goToSleep();
  }
}

void loop() {
  if (currentState == STATE_CONFIG_MODE) {
    // Обработка веб-интерфейса
    ui.tick();
    
    // Обновляем показания датчиков каждые 2 секунды
    static unsigned long lastSensorUpdate = 0;
    if (millis() - lastSensorUpdate > 2000) {
      readSensors();
      lastSensorUpdate = millis();
    }
    
    // Проверка таймаута точки доступа (5 минут)
    if (millis() - lastActivityTime > AP_TIMEOUT) {
      // Выключаем точку доступа и переходим в сон
      WiFi.softAPdisconnect(true);
      goToSleep();
    }
    
    // Проверка кнопки для выхода из режима настройки
    if (digitalRead(BUTTON_PIN) == LOW) {
      static unsigned long buttonCheckTime = 0;
      static bool checkingButton = false;
      
      if (!checkingButton) {
        buttonCheckTime = millis();
        checkingButton = true;
      }
      
      if (millis() - buttonCheckTime > 3000) {
        // Долгое нажатие - выход в сон
        WiFi.softAPdisconnect(true);
        goToSleep();
      }
    } else {
      buttonPressed = false;
    }
  }
  
  delay(10);
}
