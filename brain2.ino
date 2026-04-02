#include <Adafruit_NeoPixel.h>

#define LED_PIN 9
#define NUMPIXELS 196

#define TRIG_PIN 10
#define ECHO_PIN 11

const int relays[] = {2,3,4,5,6,7,8};
const int NUM_RELAYS = 7;

Adafruit_NeoPixel strip(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---- LED state machine ----
// IDLE -> RED -> BLUE -> YELLOW -> GREEN -> ORANGE -> BLUEYELLOW -> ALL -> IDLE -> ...
enum LedState {
  STATE_IDLE,       // neural spark animation
  STATE_RED,        // pin 2
  STATE_BLUE,       // pin 3
  STATE_YELLOW,     // pin 4
  STATE_GREEN,      // pin 5
  STATE_ORANGE,     // pin 6
  STATE_BLUEYELLOW, // pin 7 — alternates blue and yellow
  STATE_ALL         // pin 8 — all previous colors in segments
};
LedState ledState = STATE_IDLE;

// RGB values for single-color pulse states (indexed by enum value, stored in flash)
const uint8_t pulseColors[][3] PROGMEM = {
  {0,   0,   0  },  // STATE_IDLE       (unused)
  {255, 0,   0  },  // STATE_RED
  {0,   80,  255},  // STATE_BLUE
  {255, 180, 0  },  // STATE_YELLOW
  {0,   255, 0  },  // STATE_GREEN
  {255, 80,  0  },  // STATE_ORANGE
};

// ---- Neural spark animation ----
// trail stores brightness 0-255 (uint8_t saves 588 bytes vs float)
#define MAX_SPARKS 8
#define SPAWN_CHANCE 20  // % chance per frame

struct Spark {
  float pos;
  float speed;
  int8_t dir;
  bool   active;
};

Spark  sparks[MAX_SPARKS];
uint8_t trail[NUMPIXELS];

// ---- Timing ----
unsigned long lastPixelUpdate = 0;
const unsigned long pixelInterval  = 25;

unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 100;

bool handWasPresent = false;
unsigned long lastActivityTime = 0;
const unsigned long idleTimeout = 30000;

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
  Serial.println(F("System starting..."));

  strip.begin();
  strip.setBrightness(80);
  strip.show();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  for (int i = 0; i < NUM_RELAYS; i++)
  {
    pinMode(relays[i], OUTPUT);
    digitalWrite(relays[i], HIGH);
  }

  memset(trail,  0, sizeof(trail));
  memset(sparks, 0, sizeof(sparks));
  Serial.println(F("Setup complete."));
}

// ---- Neural animation ----
void trySpawnSpark()
{
  for (int i = 0; i < MAX_SPARKS; i++)
  {
    if (!sparks[i].active)
    {
      sparks[i].pos    = random(NUMPIXELS);
      sparks[i].speed  = 0.8f + random(0, 120) / 100.0f;
      sparks[i].dir    = (random(2) == 0) ? 1 : -1;
      sparks[i].active = true;
      return;
    }
  }
}

void updateNeuralAnimation()
{
  // Decay: multiply by ~0.85 using integer math (217/256 ≈ 0.848)
  for (int i = 0; i < NUMPIXELS; i++)
    trail[i] = (uint8_t)((uint16_t)trail[i] * 217 >> 8);

  if (random(100) < SPAWN_CHANCE) trySpawnSpark();

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

    // Deposit brightness (0-255 scale)
    int add0 = (int)(255 * (1.0f - frac));
    int add1 = (int)(255 * frac);
    trail[p] = (uint8_t)min(255, trail[p] + add0);
    if (p + 1 < NUMPIXELS)
      trail[p + 1] = (uint8_t)min(255, trail[p + 1] + add1);
  }

  // Render: deep blue trail, white-blue tip (quadratic color scaling)
  for (int i = 0; i < NUMPIXELS; i++)
  {
    uint8_t b = trail[i];
    if (b < 2)
    {
      strip.setPixelColor(i, 0);
    }
    else
    {
      strip.setPixelColor(i, strip.Color(
        (uint8_t)((60UL  * b * b) / 65025),
        (uint8_t)((80UL  * b * b) / 65025),
        b
      ));
    }
  }
  strip.show();
}

// ---- Single-color pulse animation ----
void updatePulseAnimation(uint8_t r, uint8_t g, uint8_t b)
{
  float t = millis() / 1000.0f;
  float brightness = 0.575f + 0.425f * sin(t * TWO_PI * 0.8f);

  for (int i = 0; i < NUMPIXELS; i++)
    strip.setPixelColor(i, strip.Color(
      (uint8_t)(r * brightness),
      (uint8_t)(g * brightness),
      (uint8_t)(b * brightness)
    ));

  strip.show();
}

// ---- Blue/yellow alternating animation (pin 7) ----
void updateBlueYellowAnimation()
{
  float t     = millis() / 1000.0f;
  float blend = 0.5f + 0.5f * sin(t * TWO_PI * 0.6f);  // 0=blue, 1=yellow

  uint8_t r = (uint8_t)(255 * blend);
  uint8_t g = (uint8_t)(80  * blend);
  uint8_t bv = (uint8_t)(255 * (1.0f - blend));

  for (int i = 0; i < NUMPIXELS; i++)
    strip.setPixelColor(i, strip.Color(r, g, bv));

  strip.show();
}

// ---- All-colors animation (pin 8) ----
// Strip split into 5 segments, one per previous color, all pulsing together.
void updateAllColorsAnimation()
{
  const uint8_t colors[5][3] = {
    {255, 0,   0  },  // red
    {0,   80,  255},  // blue
    {255, 180, 0  },  // yellow
    {0,   255, 0  },  // green
    {255, 80,  0  },  // orange
  };

  float t = millis() / 1000.0f;
  float brightness = 0.575f + 0.425f * sin(t * TWO_PI * 0.8f);

  int segmentSize = NUMPIXELS / 5;

  for (int i = 0; i < NUMPIXELS; i++)
  {
    int seg = i / segmentSize;
    if (seg >= 5) seg = 4;

    strip.setPixelColor(i, strip.Color(
      (uint8_t)(colors[seg][0] * brightness),
      (uint8_t)(colors[seg][1] * brightness),
      (uint8_t)(colors[seg][2] * brightness)
    ));
  }
  strip.show();
}

// ---- Advance LED state on each hand detection ----
void advanceLedState()
{
  switch (ledState)
  {
    case STATE_IDLE:       ledState = STATE_RED;        Serial.println(F("-> RED"));        break;
    case STATE_RED:        ledState = STATE_BLUE;       Serial.println(F("-> BLUE"));       break;
    case STATE_BLUE:       ledState = STATE_YELLOW;     Serial.println(F("-> YELLOW"));     break;
    case STATE_YELLOW:     ledState = STATE_GREEN;      Serial.println(F("-> GREEN"));      break;
    case STATE_GREEN:      ledState = STATE_ORANGE;     Serial.println(F("-> ORANGE"));     break;
    case STATE_ORANGE:     ledState = STATE_BLUEYELLOW; Serial.println(F("-> BLUEYELLOW")); break;
    case STATE_BLUEYELLOW: ledState = STATE_ALL;        Serial.println(F("-> ALL"));        break;
    case STATE_ALL:
      ledState = STATE_IDLE;
      Serial.println(F("-> IDLE"));
      memset(trail,  0, sizeof(trail));
      memset(sparks, 0, sizeof(sparks));
      break;
  }
}

// ---- Relay control ----
void updateRelayForState()
{
  for (int i = 0; i < NUM_RELAYS; i++)
    digitalWrite(relays[i], LOW);

  switch (ledState)
  {
    case STATE_RED:        digitalWrite(relays[0], HIGH); break;  // pin 2
    case STATE_BLUE:       digitalWrite(relays[1], HIGH); break;  // pin 3
    case STATE_YELLOW:     digitalWrite(relays[2], HIGH); break;  // pin 4
    case STATE_GREEN:      digitalWrite(relays[3], HIGH); break;  // pin 5
    case STATE_ORANGE:     digitalWrite(relays[4], HIGH); break;  // pin 6
    case STATE_BLUEYELLOW: digitalWrite(relays[5], HIGH); break;  // pin 7
    case STATE_ALL:        digitalWrite(relays[6], HIGH); break;  // pin 8
    default: break;
  }
}

// ---- Main loop ----
void loop()
{
  unsigned long now = millis();

  if (now - lastPixelUpdate >= pixelInterval)
  {
    lastPixelUpdate = now;

    switch (ledState)
    {
      case STATE_IDLE:
        updateNeuralAnimation();
        break;
      case STATE_BLUEYELLOW:
        updateBlueYellowAnimation();
        break;
      case STATE_ALL:
        updateAllColorsAnimation();
        break;
      default:
      {
        uint8_t r = pgm_read_byte(&pulseColors[ledState][0]);
        uint8_t g = pgm_read_byte(&pulseColors[ledState][1]);
        uint8_t b = pgm_read_byte(&pulseColors[ledState][2]);
        updatePulseAnimation(r, g, b);
        break;
      }
    }
  }

  if (now - lastSensorRead >= sensorInterval)
  {
    lastSensorRead = now;
    long distance  = readDistanceCM();

    bool handPresent = (distance > 0 && distance < 30);

    if (handPresent && !handWasPresent)
    {
      Serial.println(F("HAND DETECTED"));
      lastActivityTime = now;
      advanceLedState();
      updateRelayForState();
    }

    if (ledState != STATE_IDLE && (now - lastActivityTime >= idleTimeout))
    {
      Serial.println(F("Timeout - returning to idle"));
      ledState = STATE_IDLE;
      updateRelayForState();
      memset(trail,  0, sizeof(trail));
      memset(sparks, 0, sizeof(sparks));
    }

    handWasPresent = handPresent;
  }
}
