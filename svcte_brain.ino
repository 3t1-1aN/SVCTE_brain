#include <Adafruit_NeoPixel.h>

#define LED_PIN 9
#define NUMPIXELS 40

#define TRIG_PIN 10
#define ECHO_PIN 11

const int relays[] = {2,3,4,5,6,7,8};
const int NUM_RELAYS = 7;

Adafruit_NeoPixel strip(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

unsigned long lastPixelUpdate = 0;
const unsigned long pixelInterval = 80;

int pixelOffset = 0;

int currentRelay = -1;
bool finished = false;

uint8_t red = 29;
uint8_t green = 78;
uint8_t blue = 255;

long readDistanceCM()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  long distance = duration * 0.034 / 2;

  //Serial.print("Ultrasonic duration: ");
  //Serial.print(duration);
  //Serial.print("  Distance(cm): ");
  //Serial.println(distance);

  return distance;
}

void setup()
{
  Serial.begin(115200);
  Serial.println("System starting...");

  strip.begin();
  strip.setBrightness(50);
  strip.show();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  for(int i=0;i<NUM_RELAYS;i++)
  {
    pinMode(relays[i],OUTPUT);
    digitalWrite(relays[i],HIGH);

    Serial.print("Relay initialized on pin ");
    Serial.println(relays[i]);
  }

  Serial.println("Setup complete.");
}

void updatePixels()
{
  strip.clear();

  for(int i=0;i<NUMPIXELS;i++)
  {
    if((i + pixelOffset) % 2 == 0)
    {
      strip.setPixelColor(i, strip.Color(red,green,blue));
    }
  }

  strip.show();

  // Serial.print("Pixel animation step: ");
  // Serial.println(pixelOffset);

  pixelOffset++;
  if(pixelOffset >= NUMPIXELS) pixelOffset = 0;
}

void activateNextRelay()
{
  currentRelay++;

  Serial.print("Activating relay index: ");
  Serial.println(currentRelay);

  if(currentRelay >= NUM_RELAYS)
  {
    Serial.println("All relays activated. FINAL STATE.");

    finished = true;

    for(int i=0;i<NUM_RELAYS;i++)
    {
      digitalWrite(relays[i],LOW);
      Serial.print("Relay ON pin ");
      Serial.println(relays[i]);
    }

    return;
  }

  for(int i=0;i<NUM_RELAYS;i++)
  {
    digitalWrite(relays[i],HIGH);
  }

  digitalWrite(relays[currentRelay],LOW);

  Serial.print("Relay ON at pin ");
  Serial.println(relays[currentRelay]);
}

void loop()
{
  unsigned long now = millis();

  if(now - lastPixelUpdate >= pixelInterval)
  {
    lastPixelUpdate = now;
    updatePixels();
  }

  if(!finished)
  {
    long distance = readDistanceCM();

    if(distance > 0 && distance < 15)
    {
      Serial.println("HAND DETECTED — advancing relay");

      activateNextRelay();

      delay(800);
    }
  }
}
