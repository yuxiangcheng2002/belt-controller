#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <BleKeyboard.h>

#define BTN_A_PIN       0
#define BTN_B_PIN       17
#define WS2812_PIN      21
#define WS2812_PWR_PIN  40
#define LED_COUNT       2

Adafruit_NeoPixel strip(LED_COUNT, WS2812_PIN, NEO_GRB + NEO_KHZ800);
BleKeyboard bleKeyboard("Belt Controller", "Fiber Robotics", 100);

bool prevA = false, prevB = false;
bool wasConnected = false;

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("=== BOOT ===");

    // LED power — GPIO 40 open-drain active-low
    // Must be set BEFORE strip.begin()
    gpio_set_direction((gpio_num_t)WS2812_PWR_PIN, GPIO_MODE_OUTPUT_OD);
    gpio_set_level((gpio_num_t)WS2812_PWR_PIN, 0);  // LOW = power ON
    delay(50);
    Serial.println("LED power ON");

    // Init LED strip
    strip.begin();
    strip.setBrightness(255);
    strip.setPixelColor(0, strip.Color(64, 0, 0));
    strip.setPixelColor(1, strip.Color(0, 0, 64));
    strip.show();
    Serial.println("LEDs set to R/B");
    delay(1000);

    // Buttons
    pinMode(BTN_A_PIN, INPUT_PULLUP);
    pinMode(BTN_B_PIN, INPUT_PULLUP);

    // BLE
    bleKeyboard.begin();
    Serial.println("BLE started");

    // Green = ready
    strip.setPixelColor(0, strip.Color(0, 32, 0));
    strip.setPixelColor(1, strip.Color(0, 32, 0));
    strip.show();
}

void loop() {
    bool connected = bleKeyboard.isConnected();

    if (connected != wasConnected) {
        wasConnected = connected;
        Serial.println(connected ? "CONNECTED" : "DISCONNECTED");
        uint32_t c = connected ? strip.Color(0, 32, 0) : strip.Color(64, 0, 0);
        strip.setPixelColor(0, c);
        strip.setPixelColor(1, c);
        strip.show();
    }

    if (!connected) {
        delay(50);
        return;
    }

    bool btnA = digitalRead(BTN_A_PIN) == LOW;
    bool btnB = digitalRead(BTN_B_PIN) == LOW;

    if (btnA && !prevA) {
        Serial.println("LEFT");
        bleKeyboard.press(KEY_LEFT_ARROW);
        strip.setPixelColor(1, strip.Color(0, 0, 255));
        strip.show();
    }
    if (!btnA && prevA) {
        bleKeyboard.release(KEY_LEFT_ARROW);
        strip.setPixelColor(1, strip.Color(0, 32, 0));
        strip.show();
    }
    if (btnB && !prevB) {
        Serial.println("RIGHT");
        bleKeyboard.press(KEY_RIGHT_ARROW);
        strip.setPixelColor(0, strip.Color(0, 0, 255));
        strip.show();
    }
    if (!btnB && prevB) {
        bleKeyboard.release(KEY_RIGHT_ARROW);
        strip.setPixelColor(0, strip.Color(0, 32, 0));
        strip.show();
    }

    prevA = btnA;
    prevB = btnB;
    delay(5);
}
