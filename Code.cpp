#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include "OswaldFont.h" 
#include "quotes.h"

RTC_DS3231 rtc; 

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
  // Clear the OLED screen buffer from whatever was drawn previously
  display.clearDisplay();

  // --- 1. Fetch Time ---
  // Query the Real-Time Clock (RTC) module to get the current date and time
  DateTime now = rtc.now();

  // --- 2. Calculate Battery Level ---
  // Read the analog voltage value from the battery pin (0 to 4095 on ESP32/similar ADCs)
  int rawADC = analogRead(BATTERY_PIN);
  
  // Convert the raw ADC reading (expected between 2047 and 2605) into a percentage (0 to 100)
  // The constrain() function ensures the final value does not go below 0 or above 100
  int batteryPct = constrain(map(rawADC, 2047, 2605, 0, 100), 0, 100);

  // Determine number of battery bars (0 to 4) based on the calculated percentage
  int numBars = 0;
  if (batteryPct > 80) numBars = 4;        // Full battery
  else if (batteryPct > 50) numBars = 3;   // High battery
  else if (batteryPct > 20) numBars = 2;   // Medium battery
  else if (batteryPct > 5)  numBars = 1;   // Low battery (below 5% gets 0 bars)

  // --- 3. Draw Battery Icon with Bars (Top Right) ---
  // Set the starting X and Y coordinates for the battery icon
  int batX = 96;
  int batY = 2;
  
  // Draw the hollow outer rectangle of the battery shell (24 pixels wide, 10 pixels tall)
  display.drawRect(batX, batY, 24, 10, SSD1306_WHITE);        
  
  // Draw the solid small positive terminal "nub" on the right side of the battery
  display.fillRect(batX + 24, batY + 3, 2, 4, SSD1306_WHITE); 
  
  // Loop to draw the individual filled battery bars inside the shell
  // Each bar is 4 pixels wide and spaced 5 pixels apart (4px bar + 1px gap)
  for (int i = 0; i < numBars; i++) {
    display.fillRect(batX + 2 + (i * 5), batY + 2, 4, 6, SSD1306_WHITE);
  }

  // --- 4. Draw Time (Center) ---
  // Set the text scaling factor to 3 for large time digits
  display.setTextSize(3);
  // Set the text color to white (pixels turned on)
  display.setTextColor(SSD1306_WHITE);

  // Create a character array to hold the formatted time string ("HH:MM\0")
  char timeStr[6];
  // Format the hour and minute variables into the array with leading zeros (e.g., "09:05")
  sprintf(timeStr, "%02d:%02d", now.hour(), now.minute());

  // Move the invisible text cursor to the horizontal and vertical center for the time
  display.setCursor(19, 23);
  // Write the time string to the display buffer
  display.print(timeStr);

  // --- 5. Draw Date Only (Bottom) ---
  // Shrink the text scaling factor to 1 for the smaller date text
  display.setTextSize(1);
  
  // Create a character array to hold the formatted date string ("DD/MM/YYYY\0")
  char bottomStr[15];
  
  // Format the day, month, and year variables into the array with leading zeros
  sprintf(bottomStr, "%02d/%02d/%04d", now.day(), now.month(), now.year());
  
  // Move the text cursor to the bottom center of the screen for the date
  display.setCursor(30, 54); 
  // Write the date string to the display buffer
  display.print(bottomStr);

  // Push the completely rendered buffer (battery, time, and date) to the physical OLED screen
  display.display();
}



int currentQuoteIndex = -1; // -1 indicates we need to pick a fresh quote from the array

void drawQuote() {
  // If no quote is currently selected, pick a random one
  if (currentQuoteIndex == -1) {
    currentQuoteIndex = random(0, NUM_QUOTES);
  }

  // Clear the OLED buffer
  display.clearDisplay();
  
  // Apply the custom Oswald font for the quote text
  display.setFont(&OswaldFont); 
  display.setTextSize(1); 
  display.setTextColor(SSD1306_WHITE);
  
  // Disable automatic text wrapping so we can manually calculate clean word breaks
  display.setTextWrap(false); 
  
  // Fetch the quote from flash memory and convert it to a standard String object
  String quote = String(motivationalQuotes[currentQuoteIndex]);
  
  // --- 1. Break the quote into lines based on screen width ---
  String lines[8]; // Array to hold the separated lines (max 8 lines supported)
  int lineCount = 0; // Tracks how many lines we have created
  
  int lastSpace = 0; // Index of the beginning of the current word
  int nextSpace = quote.indexOf(' '); // Index of the next space character
  String currentLine = ""; // Holds the text for the line currently being built

  // Loop through the quote word by word
  while (nextSpace != -1) {
    // Extract the next word from the string
    String word = quote.substring(lastSpace, nextSpace);
    
    // Create a temporary test line by appending the new word (and a space if needed)
    String testLine = currentLine + (currentLine.length() > 0 ? " " : "") + word;
    
    // Variables to hold the pixel dimensions of the text
    int16_t x1, y1;
    uint16_t w, h;
    
    // Measure exactly how many pixels wide and tall the test line would be
    display.getTextBounds(testLine, 0, 0, &x1, &y1, &w, &h);
    
    // If adding this word pushes the line beyond the 128-pixel screen width...
    if (w > SCREEN_WIDTH) {
      // Save the previous valid line to our array and move to the next index
      lines[lineCount++] = currentLine;
      // Start a brand new line with the word that didn't fit
      currentLine = word;
    } else {
      // If it fits, make the test line the official current line
      currentLine = testLine;
    }
    
    // Move our indices forward to find the next word
    lastSpace = nextSpace + 1;
    nextSpace = quote.indexOf(' ', lastSpace);
  }
  
  // Process the final word in the string (which doesn't have a trailing space)
  String lastWord = quote.substring(lastSpace);
  String testLine = currentLine + (currentLine.length() > 0 ? " " : "") + lastWord;
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(testLine, 0, 0, &x1, &y1, &w, &h);
  
  // If the final word makes the line too long, split it into two lines
  if (w > SCREEN_WIDTH) {
    lines[lineCount++] = currentLine;
    lines[lineCount++] = lastWord;
  } else {
    // Otherwise, append the final line as is
    lines[lineCount++] = testLine;
  }

  // --- 2. Calculate true total height and vertical centering ---
  int lineHeight = 15; // Vertical pixel spacing between lines
  
  // Measure the dimensions of the very first line to establish baseline text metrics
  display.getTextBounds(lines[0], 0, 0, &x1, &y1, &w, &h);
  
  // Calculate the total height of the text block: 
  // Height of one line (h) + the spacing needed for all remaining lines
  int trueTotalHeight = h + ((lineCount - 1) * lineHeight);

  // Calculate the Y coordinate to center the block vertically on the 64-pixel screen.
  // We subtract y1 because custom GFX fonts draw from a baseline, not the top-left corner.
  int startY = ((SCREEN_HEIGHT - trueTotalHeight) / 2) - y1;

  // --- 3. Print each line centered horizontally ---
  for (int i = 0; i < lineCount; i++) {
    // Measure the exact pixel width of the current line
    display.getTextBounds(lines[i], 0, 0, &x1, &y1, &w, &h);
    
    // Calculate the X coordinate to center this specific line horizontally
    int startX = (SCREEN_WIDTH - w) / 2;
    
    // Failsafe: If the text is somehow wider than the screen, pin it to the left edge
    if (startX < 0) startX = 0; 
    
    // Set the cursor for this line, moving down by 'lineHeight' for each subsequent line
    display.setCursor(startX, startY + (i * lineHeight));
    display.print(lines[i]);
  }
  
  // Push the final rendered quote to the OLED
  display.display();
}