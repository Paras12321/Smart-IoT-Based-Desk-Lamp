#include "RMaker.h"
#include "WiFi.h"
#include "WiFiProv.h"
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>
#include <ESP32Servo.h>

#define servo_base_pin 33
#define servo_head_pin 26
#define Touch_Pin 5
#define PIR_PIN 16
#define LDR_PIN 34
#define LED_PIN 23
#define LED_COUNT 64

const int BASE_SERVO_ANGLE_OFFSET = -45;
const int BASE_SERVO_MAX_ROTATION = 90;

const int HEAD_SERVO_MIN_ANGLE = 20;
const int HEAD_SERVO_MAX_ANGLE = 75;

bool autoOverride = false;  // true = user manually intervened
int last_base_angle;
int last_head_angle;
int target_base_angle;
int target_head_angle;
int base_servo = 90;
int head_servo = 90;
unsigned long lastlcdupdate = 0;
bool lastTouchState = LOW;
unsigned long lastTouchTime = 0;
const unsigned long TOUCH_DEBOUNCE = 300;  // ms
unsigned long lastMotionTime = 0;
const unsigned long MOTION_TIMEOUT = 30000;
bool profile_active = false;
char Profile[30] = "None";
int saturation = 255;
uint16_t hue = 0;
bool Lampon = false;
int brightness = 100;
char control_mode[10] = "Auto";

LiquidCrystal_I2C lcd(0x27, 16, 2);

Servo BaseServo;
Servo HeadServo;

Adafruit_NeoPixel strip(
  LED_COUNT,
  LED_PIN,
  NEO_GRB + NEO_KHZ800);

void apply_huecolor() {
  uint32_t color = strip.ColorHSV(hue, saturation, 255);
  color = strip.gamma32(color);
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, color);
  }
}

const char *service_name = "ESP32_Lamp";
const char *pop = "1234";

Device *lamp = NULL;
Node my_node;

static const char *lamp_mode_list[] = {
  "Auto",
  "Manual"
};

void applyProfileSettings() {

  if (strcmp(Profile, "Reading") == 0) {
    hue = map(40, 0, 360, 0, 65535);  // warm white
    saturation = 40;
    brightness = 220;
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Profile : Reading");
  } else if (strcmp(Profile, "Research") == 0) {
    hue = map(55, 0, 360, 0, 65535);  // neutral white
    saturation = 90;
    brightness = 180;
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Profile : Research");
  } else if (strcmp(Profile, "Night") == 0) {
    hue = map(10, 0, 360, 0, 65535);  // amber
    saturation = 255;
    brightness = 15;
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Profile : Night");
  }
}

// -------- WRITE CALLBACK --------
void write_callback(Device *device, Param *param,
                    const param_val_t val, void *priv_data,
                    write_ctx_t *ctx) {
  Serial.printf("Received param update for: %s\n", param->getParamName());
  if (strcmp(param->getParamName(), "Power") == 0) {
    Lampon = val.val.b;
    profile_active = false;
    if (strcmp(control_mode, "Auto") == 0 && Lampon == false) {
      autoOverride = true;  // user touched power manually
    }
    Serial.printf("Lamp status changed to : %d\n", Lampon);
    param->updateAndReport(val);
  } else if (strcmp(param->getParamName(), "Brightness") == 0) {
    brightness = val.val.i;
    profile_active = false;
    Serial.printf("Brightness changed to : %d\n", brightness);
    param->updateAndReport(val);
  } else if (strcmp(param->getParamName(), "Control Mode") == 0) {
    strcpy(control_mode, val.val.s);
    profile_active = false;
    autoOverride = false;
    Serial.printf("Control Mode : %s\n", control_mode);
    param->updateAndReport(val);
  } else if (strcmp(param->getParamName(), "Saturation") == 0) {
    saturation = val.val.i;
    profile_active = false;
    Serial.printf("Saturation : %d\n", saturation);
    param->updateAndReport(val);
  } else if (strcmp(param->getParamName(), "Color Picker") == 0) {
    int uiHue = val.val.i;
    profile_active = false;
    hue = map(uiHue, 0, 360, 0, 65535);
    Serial.printf("Hue changed: %d\n", uiHue);
    param->updateAndReport(val);
  } else if (strcmp(param->getParamName(), "Lighting Profile") == 0) {
    strcpy(Profile, val.val.s);
    Serial.printf("Profile changed to : %s\n", Profile);
    if (strcmp(Profile, "None") != 0) {
      profile_active = true;
      applyProfileSettings();
      strip.setBrightness(brightness);
      apply_huecolor();
      strip.show();
    } else {
      lcd.setCursor(0, 1);
      lcd.print("Profile : None");
    }
    param->updateAndReport(val);
  } else if (strcmp(param->getParamName(), "Base Rotation") == 0) {
    base_servo = val.val.i;
    BaseServo.write(base_servo - BASE_SERVO_ANGLE_OFFSET);
    param->updateAndReport(val);
  } else if (strcmp(param->getParamName(), "Head Rotation") == 0) {
    head_servo = val.val.i;
    target_head_angle = head_servo;
    // HeadServo.write(head_servo);
    param->updateAndReport(val);
  }

  if (Lampon) {
    // strip.setBrightness(brightness);
    strip.setBrightness(brightness);
    apply_huecolor();
    strip.show();
  }
  if (!Lampon) {
    strip.clear();
    strip.show();
  }
}

// -------- PROVISIONING EVENTS --------
void sysProvEvent(arduino_event_t *sys_event) {
  if (sys_event->event_id == ARDUINO_EVENT_PROV_START) {
    Serial.printf("Provisioning Started\nService: %s\nPoP: %s\n",
                  service_name, pop);
    WiFiProv.printQR(service_name, pop, "ble");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(Touch_Pin, INPUT_PULLDOWN);
  pinMode(PIR_PIN, INPUT);
  Serial.println("PIR warming up...");
  // delay(30000);
  Serial.println("PIR Ready!");
  // Register events
  WiFi.onEvent(sysProvEvent);

  configTime(19800, 0, "pool.ntp.org");

  BaseServo.attach(servo_base_pin);
  HeadServo.attach(servo_head_pin);

  strip.begin();
  strip.clear();
  strip.setBrightness(brightness);
  strip.show();


  // Init RainMaker node
  my_node = RMaker.initNode("ESP-32 Lamp");

  // Create device
  lamp = new Device("Lamp", "esp.device.light");

  Param ControlMode("Control Mode", "esp.param.mode",
                    value("Auto"),
                    PROP_FLAG_READ | PROP_FLAG_WRITE);
  ControlMode.addUIType(ESP_RMAKER_UI_DROPDOWN);
  ControlMode.addValidStrList(lamp_mode_list, 2);
  lamp->addParam(ControlMode);

  Param power("Power", "esp.param.power",
              value(Lampon),
              PROP_FLAG_READ | PROP_FLAG_WRITE);
  power.addUIType(ESP_RMAKER_UI_TOGGLE);
  lamp->addParam(power);

  Param saturation("Saturation", "esp.param.saturation",
                   value(50),
                   PROP_FLAG_READ | PROP_FLAG_WRITE);

  saturation.addUIType(ESP_RMAKER_UI_SLIDER);
  saturation.addBounds(value(0), value(255), value(1));
  lamp->addParam(saturation);

  Param colorPicker("Color Picker", "esp.param.hue",
                    value(255),
                    PROP_FLAG_READ | PROP_FLAG_WRITE);

  colorPicker.addUIType("esp.ui.hue-circle");
  colorPicker.addBounds(value(0), value(360), value(1));
  lamp->addParam(colorPicker);

  Param brightness("Brightness", "esp.param.brightness",
                   value(50),
                   PROP_FLAG_READ | PROP_FLAG_WRITE | PROP_FLAG_PERSIST);
  brightness.addUIType(ESP_RMAKER_UI_SLIDER);
  brightness.addBounds(value(0), value(240), value(1));
  lamp->addParam(brightness);

  static const char *lamp_mode_list1[] = { "None", "Research", "Reading", "Night" };
  Param mode("Lighting Profile", "esp.param.mode",
             value("None"),
             PROP_FLAG_READ | PROP_FLAG_WRITE);
  mode.addUIType(ESP_RMAKER_UI_DROPDOWN);
  mode.addValidStrList(lamp_mode_list1, 4);
  lamp->addParam(mode);

  Param slider_param1("Base Rotation", "custom.param.angle1", value(0),
                      PROP_FLAG_READ | PROP_FLAG_WRITE | PROP_FLAG_PERSIST);
  slider_param1.addUIType(ESP_RMAKER_UI_SLIDER);
  slider_param1.addBounds(value(0 + BASE_SERVO_ANGLE_OFFSET), value(BASE_SERVO_MAX_ROTATION + BASE_SERVO_ANGLE_OFFSET), value(1));
  lamp->addParam(slider_param1);

  Param slider_param2("Head Rotation", "custom.param.angle2", value(90),
                      PROP_FLAG_READ | PROP_FLAG_WRITE | PROP_FLAG_PERSIST);
  slider_param2.addUIType(ESP_RMAKER_UI_SLIDER);
  slider_param2.addBounds(value(HEAD_SERVO_MIN_ANGLE), value(HEAD_SERVO_MAX_ANGLE), value(1));
  lamp->addParam(slider_param2);

  lamp->addCb(write_callback);
  my_node.addDevice(*lamp);
  // ---- PROVISIONING (CORRECT WAY) ----
  WiFiProv.initProvision(
    NETWORK_PROV_SCHEME_BLE,
    NETWORK_PROV_SCHEME_HANDLER_FREE_BTDM);

  RMaker.start();

  WiFiProv.beginProvision(
    NETWORK_PROV_SCHEME_BLE,
    NETWORK_PROV_SCHEME_HANDLER_FREE_BTDM,
    NETWORK_PROV_SECURITY_1,
    pop,
    service_name);

  Serial.println("Setup complete. Waiting for provisioning...");

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  last_head_angle = 0;
  target_head_angle = 0;
}

void loop() {
  struct tm timeinfo;
  if (last_head_angle < target_head_angle) {
    HeadServo.write(last_head_angle + 1);
    last_head_angle++;
  } else if (last_head_angle > target_head_angle) {
    HeadServo.write(last_head_angle - 1);
    last_head_angle--;
  }
  if (millis() - lastlcdupdate >= 1000) {
    lastlcdupdate = millis();
    if (getLocalTime(&timeinfo)) {
      char timeStr[17];  // 16x2 LCD line buffer

      sprintf(timeStr, "Time: %02d:%02d:%02d",
              timeinfo.tm_hour,
              timeinfo.tm_min,
              timeinfo.tm_sec);

      lcd.setCursor(0, 0);
      lcd.print(timeStr);

      // Serial.print("Time working");
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Time: --:--:--");
    }
  }
  // ---------- TOUCH SENSOR TOGGLE ----------
  bool currentTouchState = digitalRead(Touch_Pin);

  if (strcmp(control_mode, "Manual") == 0 && currentTouchState && !lastTouchState && (millis() - lastTouchTime > TOUCH_DEBOUNCE)) {

    Lampon = !Lampon;
    autoOverride = false;
    lastTouchTime = millis();

    // Sync with RainMaker UI
    lamp->updateAndReportParam("Power", Lampon);

    Serial.println(Lampon ? "Lamp ON by Touch" : "Lamp OFF by Touch");

    if (!Lampon) {
      strip.clear();
      strip.show();
    } else {
      strip.setBrightness(brightness);
      apply_huecolor();
      strip.show();
    }
  }

  lastTouchState = currentTouchState;


  // ---------- AUTO MODE WITH PIR ----------
  if (strcmp(control_mode, "Auto") == 0) {

    // ---- PIR DETECTION ----
    if (digitalRead(PIR_PIN) == HIGH && !autoOverride) {

      lastMotionTime = millis();

      if (!Lampon) {
        Lampon = true;
        lamp->updateAndReportParam("Power", true);
        Serial.println("Lamp turned ON by PIR");
        strip.setBrightness(brightness);
        apply_huecolor();
        strip.show();
      }
    }

    // ---- AUTO OFF AFTER TIMEOUT ----
    if (Lampon && (millis() - lastMotionTime > MOTION_TIMEOUT)) {
      Lampon = false;
      autoOverride = false;
      lamp->updateAndReportParam("Power", false);
      strip.clear();
      strip.show();
      Serial.println("Lamp turned OFF (No motion)");
    }
  }

  // ---------- AUTO BRIGHTNESS USING LDR ----------
  if (Lampon && strcmp(control_mode, "Auto") == 0 && !profile_active) {

    int ldrvalue = analogRead(LDR_PIN);
    ldrvalue = 4095 - ldrvalue;
    ldrvalue = constrain(ldrvalue, 500, 3500);

    int newBrightness;
    if (ldrvalue < 1000) newBrightness = 250;
    else if (ldrvalue < 2000) newBrightness = 170;
    else if (ldrvalue < 3000) newBrightness = 100;
    else newBrightness = 10;

    if (abs(newBrightness - brightness) > 5) {
      brightness = newBrightness;
      strip.setBrightness(brightness);
      apply_huecolor();
      strip.show();
    }
  }
}
