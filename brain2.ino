#include <Adafruit_NeoPixel.h>

#define LED_PIN 9
#define NUMPIXELS 40       // increase to 200 when ready

#define TRIG_PIN 10
#define ECHO_PIN 11

const int relays[] = {2,3,4,5,6,7,8};
const int NUM_RELAYS = 7;

Adafruit_NeoPixel strip(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---- LED state machine ----
// IDLE -> GREEN -> RED -> YELLOW -> BLUE -> IDLE -> ...
enum LedState { STATE_IDLE, STATE_GREEN, STATE_RED, STATE_YELLOW, STATE_BLUE };
LedState ledState = STATE_IDLE;

// RGB values for each pulse state
const uint8_t pulseColors[][3] = {
  {0,   0,   0  },  // STATE_IDLE  (unused)
  {0,   255, 0  },  // STATE_GREEN
  {255, 0,   0  },  // STATE_RED
  {255, 180, 0  },  // STATE_YELLOW
  {0,   80,  255},  // STATE_BLUE
};

// ---- Neural spark animation ----
#define MAX_SPARKS 10      // increase to ~25 for 200 pixels
#define TRAIL_DECAY 0.85f
#define SPAWN_CHANCE 20    // % chance per frame

struct Spark {
  float pos;
  float speed;
  int   dir;
  bool  active;
};

Spark sparks[MAX_SPARKS];
float trail[NUMPIXELS];

// ---- Timing ----
unsigned long lastPixelUpdate  = 0;
const unsigned long pixelInterval   = 25;    // ~40 fps

unsigned long lastSensorRead   = 0;
const unsigned long sensorInterval  = 100;   // read sensor every 100ms

bool handWasPresent = false;  // track previous state for edge detection

// ---- Relays ----
int  currentRelay   = -1;
bool relaysFinished = false;


// ---- Ultrasonic ----
long readDistanceCM()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  return duration * 0.034 / 2;
}

// ---- Setup ----
void setup()
{
  Serial.begin(115200);
  Serial.println("System starting...");

  strip.begin();
  strip.setBrightness(80);
  strip.show();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  for (int i = 0; i < NUM_RELAYS; i++)
  {
    pinMode(relays[i], OUTPUT);
    digitalWrite(relays[i], HIGH);
    Serial.print("Relay initialized on pin ");
    Serial.println(relays[i]);
  }

  memset(trail,  0, sizeof(trail));
  memset(sparks, 0, sizeof(sparks));
  Serial.println("Setup complete.");
}

// ---- Neural animation ----
void trySpawnSpark()
{
  for (int i = 0; i < MAX_SPARKS; i++)
  {
    if (!sparks[i].active)
    {
      sparks[i].pos    = random(NUMPIXELS);
      sparks[i].speed  = 0.8f + random(0, 120) / 100.0f;  // 0.8 - 2.0 px/frame
      sparks[i].dir    = (random(2) == 0) ? 1 : -1;
      sparks[i].active = true;
      return;
    }
  }
}

void updateNeuralAnimation()
{
  // Decay trail
  for (int i = 0; i < NUMPIXELS; i++)
  {
    trail[i] *= TRAIL_DECAY;
    if (trail[i] < 0.005f) trail[i] = 0;
  }

  if (random(100) < SPAWN_CHANCE) trySpawnSpark();

  // Move sparks
  for (int i = 0; i < MAX_SPARKS; i++)
  {
    if (!sparks[i].active) continue;

    sparks[i].pos += sparks[i].dir * sparks[i].speed;

    if (sparks[i].pos < 0 || sparks[i].pos >= NUMPIXELS)
    {
      sparks[i].active = false;
      continue;
    }

    int   p    = (int)sparks[i].pos;
    float frac = sparks[i].pos - p;
    trail[p] = min(1.0f, trail[p] + (1.0f - frac));
    if (p + 1 < NUMPIXELS)
      trail[p + 1] = min(1.0f, trail[p + 1] + frac);
  }

  // Render: deep blue trail, white-blue tip
  for (int i = 0; i < NUMPIXELS; i++)
  {
    float b = trail[i];
    if (b < 0.005f)
    {
      strip.setPixelColor(i, 0);
    }
    else
    {
      strip.setPixelColor(i, strip.Color(
        (uint8_t)(60  * b * b),
        (uint8_t)(80  * b * b),
        (uint8_t)(255 * b)
      ));
    }
  }
  strip.show();
}

// ---- Pulse animation ----
void updatePulseAnimation(uint8_t r, uint8_t g, uint8_t b)
{
  // Brightness oscillates between ~15% and 100% at ~0.8 Hz
  float t = millis() / 1000.0f;
  float brightness = 0.575f + 0.425f * sin(t * TWO_PI * 0.8f);

  uint8_t pr = (uint8_t)(r * brightness);
  uint8_t pg = (uint8_t)(g * brightness);
  uint8_t pb = (uint8_t)(b * brightness);

  for (int i = 0; i < NUMPIXELS; i++)
    strip.setPixelColor(i, strip.Color(pr, pg, pb));

  strip.show();
}

// ---- Advance LED state on each hand detection ----
void advanceLedState()
{
  switch (ledState)
  {
    case STATE_IDLE:   ledState = STATE_GREEN;  Serial.println("-> GREEN");  break;
    case STATE_GREEN:  ledState = STATE_RED;    Serial.println("-> RED");    break;
    case STATE_RED:    ledState = STATE_YELLOW; Serial.println("-> YELLOW"); break;
    case STATE_YELLOW: ledState = STATE_BLUE;   Serial.println("-> BLUE");   break;
    case STATE_BLUE:
      ledState = STATE_IDLE;
      Serial.println("-> IDLE (neural)");
      // Clear trail so neural animation starts fresh
      memset(trail,  0, sizeof(trail));
      memset(sparks, 0, sizeof(sparks));
      break;
  }
}

// ---- Relay sequencing (unchanged from original) ----
void activateNextRelay()
{
  currentRelay++;
  Serial.print("Activating relay index: ");
  Serial.println(currentRelay);

  if (currentRelay >= NUM_RELAYS)
  {
    Serial.println("All relays activated. FINAL STATE.");
    relaysFinished = true;
    for (int i = 0; i < NUM_RELAYS; i++) digitalWrite(relays[i], LOW);
    return;
  }

  for (int i = 0; i < NUM_RELAYS; i++) digitalWrite(relays[i], HIGH);
  digitalWrite(relays[currentRelay], LOW);

  Serial.print("Relay ON at pin ");
  Serial.println(relays[currentRelay]);
}

// ---- Main loop ----
void loop()
{
  unsigned long now = millis();

  // Update LED animation on interval
  if (now - lastPixelUpdate >= pixelInterval)
  {
    lastPixelUpdate = now;

    if (ledState == STATE_IDLE)
    {
      updateNeuralAnimation();
    }
    else
    {
      const uint8_t* c = pulseColors[ledState];
      updatePulseAnimation(c[0], c[1], c[2]);
    }
  }

  // Check sensor every 100ms
  if (now - lastSensorRead >= sensorInterval)
  {
    lastSensorRead = now;
    long distance  = readDistanceCM();

    // Hand is present if something is within 30cm
    bool handPresent = (distance > 0 && distance < 30);

    // Only trigger on the moment the hand ENTERS the zone
    if (handPresent && !handWasPresent)
    {
      Serial.println("HAND DETECTED");
      advanceLedState();
      if (!relaysFinished) activateNextRelay();
    }

    handWasPresent = handPresent;
  }
}
