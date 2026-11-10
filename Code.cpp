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
BuddyState currentState = STATE_EYES;

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
  checkSensors();

  if (currentState == STATE_EYES){
    drawRandomEyes();
  }

  else if (currentState == STATE_DIZZY){
    drawDizzyEyes();

    // Switch to ANGRY only after it stops shaking and is stable for 1000ms
    if (millis() - reactionTimer > 1000) {
      currentState = STATE_ANGRY;
      reactionTimer = millis(); // Reset the timer for the angry phase
      display.clearDisplay();
    }
  }

  else if (currentState == STATE_ANGRY){
    drawAngryEyes();

     // Once the angry time is up, cool down and go back to normal EYES
    if (millis() - reactionTimer > ANGRY_DURATION) {
      currentState = STATE_EYES;
      display.clearDisplay(); 
    }
  }

  else if (currentState == STATE_INFO) {
    drawTimeAndBattery();
  }

  else if (currentState == STATE_QUOTE) {
    drawQuote();
  }

}



void checkSensors() {
  sensors_event_t a, g, temp;  // Create containers for sensor readings acceleration, gyroscope, and temperature
  mpu.getEvent(&a, &g, &temp); // Fetch the latest data from the motion sensor

  // Calculate overall 3D force using the Pythagorean theorem
  float totalAccel = sqrt(pow(a.acceleration.x, 2) +  
                        pow(a.acceleration.y, 2) +  
                        pow(a.acceleration.z, 2));

  // If it's shaking, shift to the dizzy state immediately
  if (totalAccel > SHAKE_THRESHOLD) {
    
    if (currentState != STATE_DIZZY) {
      currentState = STATE_DIZZY;
      display.clearDisplay(); 
    }
    // Continuously reset the timer as long as it is actively shaking
    reactionTimer = millis();
  }
}



void drawRandomEyes() {
  unsigned long currentMillis = millis(); // Grabs the current system time in milliseconds

  // 1. Handle Blinking Logic (Blink every 2-5 seconds, keep closed for 150ms)

  // If open and 2-5s have passed...  
  if (!isBlinking && (currentMillis - lastBlink > random(2000, 5000))) { 
    isBlinking = true;          // Set state to indicate the eyes are now closed
    lastBlink = currentMillis;  // Reset the blink timer to the current time
  }

  // If closed and 150ms have passed...
   else if (isBlinking && (currentMillis - lastBlink > 150)) { 
    isBlinking = false;         // Set state back to open
    lastBlink = currentMillis;  // Reset the blink timer so the 2-5s countdown starts again
  }

  // 2. Handle Looking Around Logic (Shift gaze every 1-3 seconds)

  // Check if 1-3s have passed since last gaze shift
  if (currentMillis - lastEyeMove > random(1000, 3000)) { 
    eyeOffsetX = random(-8, 9); // Picks a random horizontal shift between -8 (left) and +8 (right)
    eyeOffsetY = random(-6, 7); // Picks a random vertical shift between -6 (up) and +6 (down)
    lastEyeMove = currentMillis; // Reset the eye movement timer
  }

  // 3. Render the Eyes

  display.clearDisplay();       // Wipes the OLED screen buffer clean before drawing

  int leftEyeCenter_X = 36;     // Sets the base horizontal coordinate for the left eye
  int rightEyeCenter_X = 92;    // Sets the base horizontal coordinate for the right eye
  int eyeCenter_Y = 32;         // Sets the base vertical coordinate for both eyes

  if (isBlinking) {
    // Draw closed eyes (simple horizontal lines)
    // Draws a 24-pixel wide horizontal white line to represent the closed left eye
    display.drawLine(leftEyeCenter_X - 12, eyeCenter_Y, leftEyeCenter_X + 12, eyeCenter_Y, SSD1306_WHITE);
    // Draws the closed right eye
    display.drawLine(rightEyeCenter_X - 12, eyeCenter_Y, rightEyeCenter_X + 12, eyeCenter_Y, SSD1306_WHITE);
  } 
  else {
    // Draw open eyes with current gaze offset
    // Draws a filled rounded rectangle for the left eye, applying the random X/Y shift
    display.fillRoundRect(leftEyeCenter_X - 12 + eyeOffsetX, eyeCenter_Y - 16 + eyeOffsetY, 24, 32, 6, SSD1306_WHITE);
    // Draws the shifted right eye to match
    display.fillRoundRect(rightEyeCenter_X - 12 + eyeOffsetX, eyeCenter_Y - 16 + eyeOffsetY, 24, 32, 6, SSD1306_WHITE);
  }

  display.display(); // Pushes the freshly drawn buffer to the physical OLED screen
}



void drawDizzyEyes() {
  display.clearDisplay();                 // Wipes the OLED screen buffer clean before drawing
  unsigned long currentTime = millis();   // Grabs the current system time in milliseconds for animation pacing
  
  // Add a slight vertical wobble to the whole face
  // Uses a sine wave function based on time to create a smooth up-and-down bouncing effect
  int wobbleY = 3 * sin(currentTime * 0.01); 
  
  // Rotate the inner pupils in opposite directions for a cute, confused look
  // Time is multiplied by small decimals to calculate changing rotation angles
  float angleL = currentTime * 0.006;     // Left pupil spins forward
  float angleR = -currentTime * 0.005;    // Right pupil spins backward at a slightly different speed
  
  int lx = 36;                            // Base horizontal coordinate for the left eye
  int rx = 92;                            // Base horizontal coordinate for the right eye
  int cy = 32 + wobbleY;                  // Base vertical coordinate, shifted by the sine wave wobble
  
  // Draw the wide white eye bases
  // Draws the left white eye base, slightly larger than normal open eyes
  display.fillRoundRect(lx - 14, cy - 18, 28, 36, 8, SSD1306_WHITE);
  // Draws the right white eye base
  display.fillRoundRect(rx - 14, cy - 18, 28, 36, 8, SSD1306_WHITE);
  
  // Draw the uncoordinated spinning black pupils
  int pupilRadius = 6;                    // Sets how large the black pupil circles will be
  int offsetR = 5;                        // Sets how far from the center the pupils will orbit
  
  // Calculates the left pupil's X/Y orbit position using cosine/sine and draws it in black
  display.fillCircle(lx + offsetR * cos(angleL), cy + offsetR * sin(angleL), pupilRadius, SSD1306_BLACK);
  // Calculates the right pupil's X/Y orbit position and draws it in black
  display.fillCircle(rx + offsetR * cos(angleR), cy + offsetR * sin(angleR), pupilRadius, SSD1306_BLACK);
  
  display.display();                      // Pushes the freshly drawn dizzy face to the physical OLED screen
}



void drawAngryEyes() {
  display.clearDisplay();         // Wipes the OLED screen buffer clean before drawing
  
  int leftX = 28;                 // Base horizontal coordinate for the left eye
  int rightX = 76;                // Base horizontal coordinate for the right eye
  int y = 20;                     // Base vertical coordinate for both eyes
  int w = 24;                     // Width of the eyes
  int h = 28;                     // Height of the eyes

  // 1. Draw solid open eyes

  // Draws the white rounded rectangle for the left eye
  display.fillRoundRect(leftX, y, w, h, 4, SSD1306_WHITE);
  // Draws the white rounded rectangle for the right eye
  display.fillRoundRect(rightX, y, w, h, 4, SSD1306_WHITE);

  // 2. Draw black triangles over the top inner corners to simulate angry brows
  // By drawing black shapes over the white eyes, we create "negative space" masks
  
  // Left eye mask (slants down towards the right)
  display.fillTriangle(
    leftX - 5, y - 5,        // Top left corner of the masking triangle
    leftX + w + 5, y - 5,    // Top right corner of the masking triangle
    leftX + w + 5, y + 14,   // Bottom right corner (creates the downward inward slant)
    SSD1306_BLACK            // Draws in black to "erase" the top-right corner of the white eye
  );

  // Right eye mask (slants down towards the left)
  display.fillTriangle(
    rightX - 5, y - 5,       // Top left corner of the masking triangle
    rightX + w + 5, y - 5,   // Top right corner of the masking triangle
    rightX - 5, y + 14,      // Bottom left corner (creates the downward inward slant)
    SSD1306_BLACK            // Draws in black to "erase" the top-left corner of the white eye
  );

  display.display();              // Pushes the freshly drawn angry face to the physical OLED screen
}



void drawTimeAndBattery() {

}



void drawQuote() {

}