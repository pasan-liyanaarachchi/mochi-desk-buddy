#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include "Oswald_VariableFont_wght9pt7b.h" 
#include "quotes.h"

RTC_DS1307 rtc; 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Adafruit_MPU6050 mpu;

enum BuddyState { STATE_EYES, STATE_DIZZY, STATE_ANGRY, STATE_INFO, STATE_QUOTE };
BuddyState currentState = STATE_QUOTE;

// Variables and Constants
unsigned long reactionTimer = 0; 
const unsigned long ANGRY_DURATION = 2000; 
const float SHAKE_THRESHOLD = 15.0; 
const int BATTERY_PIN = 0; 

unsigned long lastEyeMove = 0;
unsigned long lastBlink = 0;
int eyeOffsetX = 0;
int eyeOffsetY = 0;
bool isBlinking = false;
int currentQuoteIndex = -1;

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9); 

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    for(;;);
  }
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    for(;;);
  }
  display.clearDisplay();
  display.display();

  if (!mpu.begin(0x69, &Wire)) {
    Serial.println("Failed to find MPU6050 at 0x69");
    for(;;);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
}

void loop() {
  // Main state machine logic will be added here
}

// Custom functions (checkSensors, drawRandomEyes, drawDizzyEyes, etc.) will be added below