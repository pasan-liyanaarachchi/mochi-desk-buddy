#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <RTClib.h>
#include "OswaldFont.h" 
#include "quotes.h"

RTC_DS3231 rtc; 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Adafruit_MPU6050 mpu;

enum BuddyState { STATE_EYES, STATE_SHAKING, STATE_DIZZY, STATE_ANGRY, STATE_INFO, STATE_QUOTE, STATE_BIRTHDAY };
BuddyState currentState = STATE_EYES;

// Variables and Constants
unsigned long reactionTimer = 0; 
const unsigned long SHAKE_SETTLE_TIME = 300; // Wait 300ms after shaking stops before turning dizzy
const unsigned long DIZZY_DURATION = 1000;   // Dizzy spiral phase lasts 1000ms before turning angry
const unsigned long ANGRY_DURATION = 2000; 
const float SHAKE_THRESHOLD = 15.0; 
const int BATTERY_PIN = 0; 

// Shake Eye-Slide Tracking Variables (normalized -1.0 to 1.0)
float gazeX = 0.0f;
float gazeY = 0.0f;
float gazeTargetX = 0.0f;
float gazeTargetY = 0.0f;

// Eye Animation Timing Variables
unsigned long lastEyeMove = 0;
unsigned long lastBlink = 0;
unsigned long nextBlinkDelay = 3000; // Stores the rolled delay until the next blink
unsigned long nextMoveDelay = 2000;  // Stores the rolled delay until the next gaze shift
int eyeOffsetX = 0;
int eyeOffsetY = 0;
bool isBlinking = false;

// Quote & Static Screen Redraw Flags
int currentQuoteIndex = -1;
bool quoteDrawn = false;         // Ensures drawQuote() only computes & renders once per quote
unsigned long lastInfoDraw = 0;  // Throttles STATE_INFO redraws to once per second
bool infoNeedsRedraw = true;     // Forces an immediate draw when entering STATE_INFO
int lastBirthdayFrame = -1;      // Tracks flame animation frame (-1 forces immediate draw)

// RTC & Birthday Tracking Variables
bool rtcInvalid = false;         // Set to true if the DS3231 lost power (dead/missing coin cell)
bool birthdayDismissed = false;  // Tracks if the user has closed the birthday screen today
unsigned long lastDateCheck = 0; // Stores the last timestamp (in ms) the RTC was queried

// Random Quote Timer Variables (triggers randomly every 1.5 to 2.5 hours)
unsigned long lastRandomQuoteTime = 0;           // Timestamp (in ms) of the last auto-triggered quote
unsigned long nextQuoteInterval = 7200000;       // Randomized wait time before the next auto-quote
bool isAutoQuote = false;                        // Distinguishes auto-triggered quotes from manual quotes
unsigned long quoteDisplayStart = 0;             // Timestamp (in ms) when an auto-triggered quote started
const unsigned long AUTO_QUOTE_DURATION = 15000; // Auto-triggered quotes stay on screen for 15 seconds

// Function Prototypes (required for PlatformIO / standard C++ compilation)
void checkSensors();
void checkBirthday();
bool checkDoubleTap();
void drawRandomEyes();
void drawShakeEyes();
void drawSpiral(int cx, int cy, float rot, int maxR);
void drawDizzyEyes();
void drawAngryEyes();
void drawTimeAndBattery();
void drawQuote();
void drawBirthdayScreen();



void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9); 

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    for(;;);
  }

  // Check if the DS3231 lost power (e.g., dead or missing coin cell)
  if (rtc.lostPower()) {
    rtcInvalid = true;
    Serial.println("RTC lost power, time is invalid!");
    // Uncomment both lines below to reset the RTC to the sketch compile time:
    // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // rtcInvalid = false;
  }

  // Use the below line in initial code upload to extract PC's date and time
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  
  if(!display.begin(0x3C, true)) {
    Serial.println("SH110X allocation failed");
    for(;;);
  }
  display.clearDisplay();
  display.display();

  if (!mpu.begin(0x69, &Wire)) {
    Serial.println("Failed to find MPU6050 at 0x69");
    for(;;);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);

  // Pick a random initial interval between 1.5 hours (5,400,000 ms) and 2.5 hours (9,000,000 ms)
  nextQuoteInterval = random(5400000, 9000000);
}



void loop() {
  checkSensors();
  checkBirthday(); // Periodically checks the RTC to see if today is Feb 15th

  // --- 1. Handle Double-Tap State Cycling ---
  // Cycles: STATE_EYES -> STATE_INFO -> STATE_QUOTE -> STATE_EYES (or dismisses STATE_BIRTHDAY)
  if (checkDoubleTap()) {
    if (currentState == STATE_BIRTHDAY) {
      birthdayDismissed = true;  // Prevent the birthday screen from re-triggering for the rest of Feb 15th
      currentState = STATE_EYES; // Return to normal idle eyes
    } 
    else if (currentState == STATE_EYES) {
      infoNeedsRedraw = true;    // Force immediate draw upon entering STATE_INFO
      currentState = STATE_INFO; // Switch from eyes to time & battery screen
    } 
    else if (currentState == STATE_INFO) {
      currentQuoteIndex = -1;    // Reset index so drawQuote() picks a fresh random quote
      quoteDrawn = false;        // Allow drawQuote() to render the new quote
      isAutoQuote = false;       // Mark as manual so it stays open until double-tapped again
      currentState = STATE_QUOTE;
    } 
    else if (currentState == STATE_QUOTE) {
      isAutoQuote = false;       // Clear auto-quote flag if dismissed early
      currentState = STATE_EYES; // Return to normal idle eyes
    }
    display.clearDisplay();      // Wipe the buffer before rendering the new state
  }

  // --- 2. Handle Random ~2-Hour Quote Trigger ---
  // Only triggers when Mochi is idle in STATE_EYES so it won't interrupt other states
  if (currentState == STATE_EYES && (millis() - lastRandomQuoteTime >= nextQuoteInterval)) {
    lastRandomQuoteTime = millis();               // Reset the timer for the next countdown
    nextQuoteInterval = random(5400000, 9000000); // Pick a new random wait time (1.5 to 2.5 hours)
    currentQuoteIndex = -1;                       // Pick a new random quote
    quoteDrawn = false;                           // Allow drawQuote() to render the new quote
    isAutoQuote = true;                           // Flag as auto-triggered so it closes after 15s
    quoteDisplayStart = millis();                 // Record start time for the 15-second display window
    currentState = STATE_QUOTE;
    display.clearDisplay();
  }

  // --- 3. Run Active State ---
  if (currentState == STATE_EYES){
    drawRandomEyes();
  }

  else if (currentState == STATE_SHAKING) {
    drawShakeEyes();

    // Once shaking stops and stays calm for SHAKE_SETTLE_TIME (300ms), enter the dizzy phase
    if (millis() - reactionTimer > SHAKE_SETTLE_TIME) {
      currentState = STATE_DIZZY;
      reactionTimer = millis(); // Reset timer so the 1000ms dizzy calm-down starts cleanly
      display.clearDisplay();
    }
  }

  else if (currentState == STATE_DIZZY){
    drawDizzyEyes();

    // Switch to ANGRY after the dizzy spiral phase finishes (1000ms)
    if (millis() - reactionTimer > DIZZY_DURATION) {
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

    // If this quote popped up automatically, return to eyes after AUTO_QUOTE_DURATION (15s)
    if (isAutoQuote && (millis() - quoteDisplayStart >= AUTO_QUOTE_DURATION)) {
      isAutoQuote = false;
      currentState = STATE_EYES;
      display.clearDisplay();
    }
  }

  else if (currentState == STATE_BIRTHDAY) {
    // Render the animated birthday message and cake (dismissal is handled in the double-tap block above)
    drawBirthdayScreen();
  }

  // Cap loop speed to ~50 FPS to reduce I2C bus spam, save battery, and keep the ESP32-C3 cool
  delay(20);
}



void checkSensors() {
  // Ignore shakes while the birthday screen is active
  if (currentState == STATE_BIRTHDAY) {
    return;
  }

  sensors_event_t a, g, temp;  // Create containers for sensor readings acceleration, gyroscope, and temperature
  mpu.getEvent(&a, &g, &temp); // Fetch the latest data from the motion sensor

  // Calculate overall 3D force using the Pythagorean theorem
  float totalAccel = sqrt(pow(a.acceleration.x, 2) +  
                        pow(a.acceleration.y, 2) +  
                        pow(a.acceleration.z, 2));

  // If it's shaking, shift to STATE_SHAKING and push the eye gaze toward the shake direction
  if (totalAccel > SHAKE_THRESHOLD) {
    // Normalize X/Y acceleration into a -1.0 to 1.0 target for drawShakeEyes()
    gazeTargetX = constrain(a.acceleration.x / SHAKE_THRESHOLD, -1.0f, 1.0f);
    gazeTargetY = constrain(a.acceleration.y / SHAKE_THRESHOLD, -1.0f, 1.0f);

    if (currentState != STATE_SHAKING) {
      isAutoQuote = false; // Clean up auto-quote flag if a shake interrupts a quote
      currentState = STATE_SHAKING;
      display.clearDisplay(); 
    }
    // Continuously reset the timer as long as it is actively shaking
    reactionTimer = millis();
  }
}



void checkBirthday() {
  // Skip date checks if the RTC lost power and holds an invalid date
  if (rtcInvalid) {
    return;
  }

  // Check RTC date once per second (1000ms) to avoid spamming the I2C bus every loop cycle
  if (millis() - lastDateCheck >= 1000) {
    lastDateCheck = millis();  // Reset the 1-second check timer
    DateTime now = rtc.now();  // Fetch the current date and time from the DS3231 RTC

    // Check if the current date is February 15th
    if (now.month() == 2 && now.day() == 15) {
      // Only trigger from STATE_EYES so it doesn't fight with shaking/dizzy/angry/info/quote states
      if (!birthdayDismissed && currentState == STATE_EYES) {
        lastBirthdayFrame = -1; // Force immediate draw
        currentState = STATE_BIRTHDAY;
        display.clearDisplay();
      }
    } else {
      // On any day other than Feb 15th, reset the flag so it triggers again next year
      birthdayDismissed = false;

      // Auto-exit the birthday screen if midnight passed without a double-tap dismissal
      if (currentState == STATE_BIRTHDAY) {
        currentState = STATE_EYES;
        display.clearDisplay();
      }
    }
  }
}



bool checkDoubleTap() {
  // TODO: Add TTP223 double-tap detection logic here once wired
  return false; // Returns false by default until touch sensor logic is implemented
}



void drawBirthdayScreen() {
  // Alternates between 0 and 1 every 300ms to drive the flame flicker and sparkle animation
  int flameToggle = (millis() / 300) % 2;

  // Skip redrawing if the animation frame hasn't changed yet (saves I2C bandwidth & power)
  if (flameToggle == lastBirthdayFrame) {
    return;
  }
  lastBirthdayFrame = flameToggle;

  display.clearDisplay();         // Wipe the OLED buffer before drawing the new frame
  display.setFont(NULL);          // Reset to the default 5x7 font in case OswaldFont was previously active
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  // Center the 15-character title horizontally: 15 chars * 6px width = 90px -> (128 - 90) / 2 = 19
  display.setCursor(19, 8);
  display.print("HAPPY BIRTHDAY!");

  // Draw the cake body as a solid white rectangle
  display.fillRect(46, 38, 36, 16, SH110X_WHITE);
  // Draw a black horizontal line across the middle to represent a frosting layer
  display.drawFastHLine(46, 45, 36, SH110X_BLACK);

  // Horizontal X positions for the three candles on top of the cake
  int candleX[3] = {53, 63, 73};

  // Loop through and draw each of the 3 candles and its animated flame
  for (int i = 0; i < 3; i++) {
    // Draw the vertical candle stick (2px wide, 9px tall)
    display.fillRect(candleX[i], 29, 2, 9, SH110X_WHITE);

    // Alternate between a round flame and a tall, slender flickering flame
    if (flameToggle) {
      display.fillCircle(candleX[i], 25, 2, SH110X_WHITE);
    } else {
      display.drawPixel(candleX[i], 24, SH110X_WHITE);
      display.drawPixel(candleX[i] + 1, 25, SH110X_WHITE);
      display.drawPixel(candleX[i], 26, SH110X_WHITE);
    }
  }

  // Draw twinkling background sparkles that alternate positions every 300ms
  if (flameToggle) {
    display.drawPixel(20, 26, SH110X_WHITE);
    display.drawPixel(108, 30, SH110X_WHITE);
    display.drawPixel(28, 48, SH110X_WHITE);
    display.drawPixel(100, 46, SH110X_WHITE);
  } else {
    display.drawPixel(24, 34, SH110X_WHITE);
    display.drawPixel(104, 24, SH110X_WHITE);
    display.drawPixel(18, 44, SH110X_WHITE);
    display.drawPixel(110, 50, SH110X_WHITE);
  }

  display.display(); // Push the rendered birthday frame to the physical OLED screen
}



void drawRandomEyes() {
  unsigned long currentMillis = millis(); // Grabs the current system time in milliseconds

  // 1. Handle Blinking Logic (Blink every 2-5 seconds, keep closed for 150ms)
  if (!isBlinking && (currentMillis - lastBlink > nextBlinkDelay)) { 
    isBlinking = true;          // Set state to indicate the eyes are now closed
    lastBlink = currentMillis;  // Reset the blink timer to the current time
  }
  else if (isBlinking && (currentMillis - lastBlink > 150)) { 
    isBlinking = false;                  // Set state back to open
    lastBlink = currentMillis;           // Reset the blink timer
    nextBlinkDelay = random(2000, 5000); // Roll the next blink delay once per cycle
  }

  // 2. Handle Looking Around Logic (Shift gaze every 1-3 seconds)
  if (currentMillis - lastEyeMove > nextMoveDelay) { 
    eyeOffsetX = random(-8, 9);         // Picks a random horizontal shift between -8 (left) and +8 (right)
    eyeOffsetY = random(-6, 7);         // Picks a random vertical shift between -6 (up) and +6 (down)
    lastEyeMove = currentMillis;        // Reset the eye movement timer
    nextMoveDelay = random(1000, 3000); // Roll the next gaze delay once per cycle
  }

  // 3. Render the Eyes
  display.clearDisplay();       // Wipes the OLED screen buffer clean before drawing

  int leftEyeCenter_X = 36;     // Sets the base horizontal coordinate for the left eye
  int rightEyeCenter_X = 92;    // Sets the base horizontal coordinate for the right eye
  int eyeCenter_Y = 32;         // Sets the base vertical coordinate for both eyes

  if (isBlinking) {
    // Draw closed eyes (simple horizontal lines)
    display.drawLine(leftEyeCenter_X - 12, eyeCenter_Y, leftEyeCenter_X + 12, eyeCenter_Y, SH110X_WHITE);
    display.drawLine(rightEyeCenter_X - 12, eyeCenter_Y, rightEyeCenter_X + 12, eyeCenter_Y, SH110X_WHITE);
  } 
  else {
    // Draw open eyes with current gaze offset
    display.fillRoundRect(leftEyeCenter_X - 12 + eyeOffsetX, eyeCenter_Y - 16 + eyeOffsetY, 24, 32, 6, SH110X_WHITE);
    display.fillRoundRect(rightEyeCenter_X - 12 + eyeOffsetX, eyeCenter_Y - 16 + eyeOffsetY, 24, 32, 6, SH110X_WHITE);
  }

  display.display(); // Pushes the freshly drawn buffer to the physical OLED screen
}



void drawShakeEyes() {
  // Chase the target quickly so the eyes snap with each shake without jittering
  gazeX += (gazeTargetX - gazeX) * 0.45f;
  gazeY += (gazeTargetY - gazeY) * 0.45f;

  // Convert normalized gaze (-1.0 to 1.0) into pixel offsets
  int offX = (int)(gazeX * 10);   // up to 10 px left/right
  int offY = (int)(gazeY * 8);    // up to 8 px up/down

  display.clearDisplay();
  // Slightly wide, startled eyes pushed in the direction of the shake
  display.fillRoundRect(36 - 14 + offX, 32 - 18 + offY, 28, 36, 8, SH110X_WHITE);
  display.fillRoundRect(92 - 14 + offX, 32 - 18 + offY, 28, 36, 8, SH110X_WHITE);
  display.display();
}



// Draws a rotating spiral (used as the dizzy "pupil")
void drawSpiral(int cx, int cy, float rot, int maxR) {
  const int   steps = 36; // Number of line segments forming the spiral
  const float turns = 2.5;
  int px = cx, py = cy;

  for (int i = 1; i <= steps; i++) {
    float p   = (float)i / steps;
    float ang = rot + p * turns * TWO_PI;
    float r   = p * maxR;
    int x = cx + r * cosf(ang);
    int y = cy + r * sinf(ang);

    // Draw two adjacent lines for a 2px thick spiral stroke
    display.drawLine(px, py, x, y, SH110X_BLACK);
    display.drawLine(px + 1, py, x + 1, y, SH110X_BLACK);
    px = x;
    py = y;
  }
}



void drawDizzyEyes() {
  static unsigned long lastFrame = 0;
  static float spinL = 0, spinR = 0, swayPhase = 0, starPhase = 0;

  // Calculate elapsed time (dt) in seconds since the last frame
  unsigned long now = millis();
  float dt = (now - lastFrame) / 1000.0f;
  if (dt > 0.1f) dt = 0.03f;          // Clamp dt on the first frame after entering dizzy state
  lastFrame = now;

  // "Energy" fades from 1.0 to 0.0 over the 1000ms dizzy duration,
  // so the spin slows down and the wobble calms before the buddy turns angry
  float calm   = constrain((now - reactionTimer) / (float)DIZZY_DURATION, 0.0f, 1.0f);
  float energy = 1.0f - calm;
  float speed  = 0.3f + 0.7f * energy;

  // Integrate angles (instead of using millis() directly) so changing speed never makes them jump
  spinL     += 10.0f * speed * dt;    // Left spiral turns clockwise
  spinR     -= 8.0f  * speed * dt;    // Right spiral turns counter-clockwise, slightly slower
  swayPhase += 6.0f  * speed * dt;
  starPhase += 4.0f  * speed * dt;

  // Whole face sways side to side, and the eyes bob out of sync with each other
  int swayX = (int)(5 * energy * sinf(swayPhase)) + (int)(2 * sinf(swayPhase * 0.5f));
  int bobL  = (int)(2 * sinf(swayPhase * 1.7f));
  int bobR  = (int)(2 * sinf(swayPhase * 1.7f + PI));

  int lx = 36 + swayX;
  int rx = 92 + swayX;
  int cyL = 32 + bobL;
  int cyR = 32 + bobR;

  display.clearDisplay();

  // Round white eyes with spinning black spirals on top
  display.fillCircle(lx, cyL, 15, SH110X_WHITE);
  display.fillCircle(rx, cyR, 15, SH110X_WHITE);
  drawSpiral(lx, cyL, spinL, 13);
  drawSpiral(rx, cyR, spinR, 13);

  // Three little cross-shaped stars orbiting above the head
  for (int i = 0; i < 3; i++) {
    float a = starPhase + i * (TWO_PI / 3.0f);
    int sx = 64 + (int)(34 * cosf(a)) + swayX;
    int sy = 7  + (int)(4  * sinf(a));
    display.drawFastHLine(sx - 2, sy, 5, SH110X_WHITE);
    display.drawFastVLine(sx, sy - 2, 5, SH110X_WHITE);
  }

  // Wobbly sine-wave mouth at the bottom
  int prevX = 52 + swayX, prevY = 57;
  for (int i = 1; i <= 12; i++) {
    int x = 52 + swayX + i * 2;
    int y = 57 + (int)(2 * sinf(swayPhase * 2.0f + i * 1.0f));
    display.drawLine(prevX, prevY, x, y, SH110X_WHITE);
    prevX = x;
    prevY = y;
  }

  display.display();
}



void drawAngryEyes() {
  display.clearDisplay();

  const int w = 26, h = 26, y = 20;
  const int leftX = 24, rightX = 78;   // symmetric around x=64

  // Solid white eyes with soft corners (keeps it cute)
  display.fillRoundRect(leftX,  y, w, h, 10, SH110X_WHITE);
  display.fillRoundRect(rightX, y, w, h, 10, SH110X_WHITE);

  // Black triangles cut the inner top corners to make the angry slant
  display.fillTriangle(leftX - 3, y - 3,  leftX + w + 3, y - 3,  leftX + w + 3, y + 16, SH110X_BLACK);
  display.fillTriangle(rightX + w + 3, y - 3,  rightX - 3, y - 3,  rightX - 3, y + 16, SH110X_BLACK);

  display.display();
}



void drawTimeAndBattery() {
  // Throttle redraws to once per second unless entering the state for the first time
  if (!infoNeedsRedraw && (millis() - lastInfoDraw < 1000)) {
    return;
  }
  lastInfoDraw = millis();
  infoNeedsRedraw = false;

  // Clear the OLED screen buffer from whatever was drawn previously
  display.clearDisplay();
  // Reset to default 5x7 font so OswaldFont from STATE_QUOTE doesn't distort the clock/date
  display.setFont(NULL);

  // --- 1. Calculate Battery Level (averaged over 16 samples to prevent bar flicker) ---
  long adcSum = 0;
  for (int i = 0; i < 16; i++) {
    adcSum += analogRead(BATTERY_PIN);
  }
  int rawADC = adcSum / 16;
  
  // Convert the averaged ADC reading (expected between 2047 and 2605) into a percentage (0 to 100)
  int batteryPct = constrain(map(rawADC, 2047, 2605, 0, 100), 0, 100);

  // Determine number of battery bars (0 to 4) based on the calculated percentage
  int numBars = 0;
  if (batteryPct > 80) numBars = 4;        // Full battery
  else if (batteryPct > 50) numBars = 3;   // High battery
  else if (batteryPct > 20) numBars = 2;   // Medium battery
  else if (batteryPct > 5)  numBars = 1;   // Low battery (below 5% gets 0 bars)

  // --- 2. Draw Battery Icon with Bars (Top Right) ---
  int batX = 96;
  int batY = 2;
  
  display.drawRect(batX, batY, 24, 10, SH110X_WHITE);        
  display.fillRect(batX + 24, batY + 3, 2, 4, SH110X_WHITE); 
  
  for (int i = 0; i < numBars; i++) {
    display.fillRect(batX + 2 + (i * 5), batY + 2, 4, 6, SH110X_WHITE);
  }

  // --- 3. Format Time & Date (show placeholders if RTC lost power) ---
  char timeStr[6];
  char bottomStr[15];

  if (rtcInvalid) {
    strcpy(timeStr, "--:--");
    strcpy(bottomStr, "----/--/--");
  } else {
    DateTime now = rtc.now();
    sprintf(timeStr, "%02d:%02d", now.hour(), now.minute());
    sprintf(bottomStr, "%04d/%02d/%02d", now.year(), now.month(), now.day());
  }

  // --- 4. Draw Time (Center) ---
  display.setTextSize(3);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(19, 23);
  display.print(timeStr);

  // --- 5. Draw Date Only (Bottom) ---
  display.setTextSize(1);
  // Centered: 10 chars * 6px = 60px -> (128 - 60) / 2 = 34
  display.setCursor(34, 54); 
  display.print(bottomStr);

  display.display();
}



void drawQuote() {
  // Only wrap and render the quote once per entry to avoid heap fragmentation and I2C spam
  if (quoteDrawn) {
    return;
  }

  // If no quote is currently selected, pick a random one
  if (currentQuoteIndex == -1) {
    currentQuoteIndex = random(0, NUM_QUOTES);
  }

  // Clear the OLED buffer
  display.clearDisplay();
  
  // Apply the custom Oswald font for the quote text
  display.setFont(&OswaldFont); 
  display.setTextSize(1); 
  display.setTextColor(SH110X_WHITE);
  
  // Disable automatic text wrapping so we can manually calculate clean word breaks
  display.setTextWrap(false); 
  
  // Fetch the quote from flash memory and convert it to a standard String object
  String quote = String(motivationalQuotes[currentQuoteIndex]);
  
  // --- 1. Break the quote into lines based on screen width ---
  // Max 4 lines fit cleanly on a 64px tall OLED with 15px line spacing (4 * 15 = 60px)
  const int MAX_LINES = 4;
  String lines[MAX_LINES];
  int lineCount = 0;
  bool quoteTruncated = false; // Flags if the quote exceeds 4 lines for Serial debugging
  
  int lastSpace = 0;
  int nextSpace = quote.indexOf(' ');
  String currentLine = "";

  // Loop through the quote word by word
  while (nextSpace != -1) {
    String word = quote.substring(lastSpace, nextSpace);
    String testLine = currentLine + (currentLine.length() > 0 ? " " : "") + word;
    
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(testLine, 0, 0, &x1, &y1, &w, &h);
    
    if (w > SCREEN_WIDTH) {
      // Only push currentLine if it's non-empty (prevents blank lines on oversized single words)
      if (currentLine.length() > 0) {
        if (lineCount < MAX_LINES) {
          lines[lineCount++] = currentLine;
        } else {
          quoteTruncated = true;
        }
      }
      currentLine = word;
    } else {
      currentLine = testLine;
    }
    
    lastSpace = nextSpace + 1;
    nextSpace = quote.indexOf(' ', lastSpace);
  }
  
  // Process the final word in the string
  String lastWord = quote.substring(lastSpace);
  String testLine = currentLine + (currentLine.length() > 0 ? " " : "") + lastWord;
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(testLine, 0, 0, &x1, &y1, &w, &h);
  
  if (w > SCREEN_WIDTH) {
    if (currentLine.length() > 0) {
      if (lineCount < MAX_LINES) lines[lineCount++] = currentLine;
      else quoteTruncated = true;
    }
    if (lineCount < MAX_LINES) lines[lineCount++] = lastWord;
    else quoteTruncated = true;
  } else {
    if (lineCount < MAX_LINES) lines[lineCount++] = testLine;
    else quoteTruncated = true;
  }

  // Warn over Serial if a quote in quotes.h is too long to fit in 4 lines
  if (quoteTruncated) {
    Serial.print("Warning: Quote truncated (>4 lines) at index ");
    Serial.println(currentQuoteIndex);
  }

  // --- 2. Calculate true total height and vertical centering ---
  int lineHeight = 15;
  display.getTextBounds(lines[0], 0, 0, &x1, &y1, &w, &h);
  
  int trueTotalHeight = h + ((lineCount - 1) * lineHeight);
  int startY = ((SCREEN_HEIGHT - trueTotalHeight) / 2) - y1;

  // --- 3. Print each line centered horizontally ---
  for (int i = 0; i < lineCount; i++) {
    display.getTextBounds(lines[i], 0, 0, &x1, &y1, &w, &h);
    int startX = (SCREEN_WIDTH - w) / 2;
    if (startX < 0) startX = 0; 
    
    display.setCursor(startX, startY + (i * lineHeight));
    display.print(lines[i]);
  }
  
  display.display();
  quoteDrawn = true; // Mark as drawn so subsequent loop iterations skip re-rendering
}