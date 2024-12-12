#include <WiFi.h>  // Include WiFi library for ESP32's WiFi functionality
#include <VS1053.h>  // Include library to control the VS1053 MP3 decoder
#include <U8g2lib.h>  // Include library for controlling the OLED display

// Define the VS1053 MP3 decoder pins
#define VS1053_CS     32  // Chip Select for VS1053
#define VS1053_DCS    33  // Data Command Select for VS1053
#define VS1053_DREQ   35  // Data Request pin for VS1053

// Button pins to switch between radio stations
#define BUTTON_NEXT  13  // Pin for the 'next station' button
#define BUTTON_PREV  12  // Pin for the 'previous station' button

// OLED Display setup with I2C communication
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);  // Create OLED display object

// WiFi settings: replace with your own network credentials
const char *ssid = "Your SSID";  // Your WiFi network name
const char *password = "Your Wifi Password";  // Your WiFi network password

// Radio station details
const char* stationNames[] = {"ERT Kosmos", "FFH Lounge", "FFH Summer"};  // Array of station names
const char* stationHosts[] = {"radiostreaming.ert.gr", "mp3.ffh.de", "mp3.ffh.de"};  // Host URLs for the stations
const char* stationPaths[] = {"/ert-kosmos", "/ffhchannels/hqlounge.mp3", "/ffhchannels/hqsummerfeeling.mp3"};  // Paths to the radio streams
int currentStation = 0;  // Index of the currently playing station
const int totalStations = sizeof(stationNames) / sizeof(stationNames[0]);  // Calculate the number of available stations

// VS1053 MP3 player object
VS1053 player(VS1053_CS, VS1053_DCS, VS1053_DREQ);  // Create VS1053 object to control MP3 playback
WiFiClient client;  // WiFi client object to connect to the radio stream

// Variables for scrolling text on the OLED display
int textPosition = 128;  // Initial text position for scrolling
unsigned long previousMillis = 0;  // Store the last time the display was updated
const long interval = 50;  // Time interval for updating the display (50 ms)

void setup() {
    Serial.begin(115200);  // Start the serial monitor for debugging

    // Wait for VS1053 and PAM8403 amplifier to power up
    delay(3000);

    u8g2.begin();  // Initialize the OLED display
    u8g2.setFlipMode(1);  // Flip the display 180 degrees
    u8g2.setFont(u8g2_font_ncenB08_tr);  // Set font for OLED display

    // Display startup messages on OLED
    u8g2.clearBuffer();
    u8g2.drawStr(0, 16, "Starting Radio...");  // Initial message
    u8g2.sendBuffer();
    delay(2000);

    u8g2.clearBuffer();
    u8g2.drawStr(0, 16, "Starting Engine...");  // Second message
    u8g2.sendBuffer();
    delay(2000);

    u8g2.clearBuffer();
    u8g2.drawStr(0, 16, "Connecting to WiFi...");  // WiFi connection message
    u8g2.sendBuffer();

    Serial.println("\n\nSimple Radio Node WiFi Radio");  // Debug message in the serial monitor

    SPI.begin();  // Initialize SPI communication for VS1053

    player.begin();  // Start the VS1053 decoder
    if (player.getChipVersion() == 4) {  // Check for correct version of VS1053
        player.loadDefaultVs1053Patches();  // Load patches for MP3 decoding if needed
    }
    player.switchToMp3Mode();  // Switch VS1053 to MP3 decoding mode
    player.setVolume(100);  // Set the volume (range: 0-100)

    Serial.print("Connecting to SSID ");
    Serial.println(ssid);  // Debug message: attempting WiFi connection
    WiFi.begin(ssid, password);  // Start WiFi connection

    // Disable WiFi power saving mode for a more stable connection
    WiFi.setSleep(false);

    // Attempt to connect to WiFi with retries
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");  // Print dots to indicate connection progress
        attempts++;
    }

    // Check if WiFi connection is successful
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());  // Print the assigned IP address

        // Display success message on OLED
        u8g2.clearBuffer();
        u8g2.drawStr(0, 16, "Connected Yay!");
        u8g2.sendBuffer();
        delay(2000);
    } else {
        // Display failure message on OLED if WiFi connection fails
        Serial.println("WiFi not connected");
        u8g2.clearBuffer();
        u8g2.drawStr(0, 16, "Not Connected");
        u8g2.sendBuffer();
        delay(2000);
    }

    // Initialize button pins with pull-up resistors
    pinMode(BUTTON_NEXT, INPUT_PULLUP);
    pinMode(BUTTON_PREV, INPUT_PULLUP);

    // Set font for displaying station names
    u8g2.setFont(u8g2_font_profont17_mr);

    displayStation();  // Display the initial station name on the OLED
    connectToHost();  // Connect to the radio station stream
}

void loop() {
    // Reconnect if the WiFi client is disconnected
    if (!client.connected()) {
        Serial.println("Reconnecting...");
        connectToHost();  // Attempt to reconnect to the stream
    }

    // Read data from the radio stream and send it to the VS1053 decoder for playback
    if (client.available() > 0) {
        uint8_t buffer[32];
        size_t bytesRead = client.readBytes(buffer, sizeof(buffer));  // Read data from stream
        player.playChunk(buffer, bytesRead);  // Play the received audio data
    }

    handleButtons();  // Check if buttons are pressed and switch stations accordingly
    scrollText();  // Scroll the station name on the OLED display
}

void connectToHost() {
    // Connect to the current radio station's server
    Serial.print("Connecting to ");
    Serial.println(stationHosts[currentStation]);

    if (!client.connect(stationHosts[currentStation], 80)) {
        Serial.println("Connection failed");  // Display error if connection fails
        return;
    }

    // Send HTTP request to the server to get the radio stream
    Serial.print("Requesting stream: ");
    Serial.println(stationPaths[currentStation]);

    client.print(String("GET ") + stationPaths[currentStation] + " HTTP/1.1\r\n" +
                 "Host: " + stationHosts[currentStation] + "\r\n" +
                 "Connection: close\r\n\r\n");

    // Skip the HTTP headers in the response
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") {
            break;  // End of headers
        }
    }

    Serial.println("Headers received");  // Debug message: headers successfully received
}

void handleButtons() {
    static bool lastButtonNextState = HIGH;  // Track the previous state of the 'next' button
    static bool lastButtonPrevState = HIGH;  // Track the previous state of the 'previous' button

    bool currentButtonNextState = digitalRead(BUTTON_NEXT);  // Read current state of 'next' button
    bool currentButtonPrevState = digitalRead(BUTTON_PREV);  // Read current state of 'previous' button

    // If 'next' button is pressed (LOW), switch to the next station
    if (lastButtonNextState == HIGH && currentButtonNextState == LOW) {
        nextStation();
    }
    // If 'previous' button is pressed (LOW), switch to the previous station
    if (lastButtonPrevState == HIGH && currentButtonPrevState == LOW) {
        previousStation();
    }

    lastButtonNextState = currentButtonNextState;  // Update the last state for the 'next' button
    lastButtonPrevState = currentButtonPrevState;  // Update the last state for the 'previous' button
}

void nextStation() {
    currentStation = (currentStation + 1) % totalStations;  // Move to the next station (wrap around)
    displayStation();  // Update the OLED display with the new station name
    connectToHost();  // Connect to the new station
}


void previousStation() {
    currentStation = (currentStation - 1 + totalStations) % totalStations;
    displayStation();
    connectToHost();
}

void displayStation() {
    textPosition = 128;  // Reset text position to start from the right
    u8g2.clearBuffer();
    u8g2.drawLine(0, 0, 127, 0);
    u8g2.drawLine(0, 31, 127, 31);
    u8g2.setCursor(textPosition, 22);
    u8g2.print(stationNames[currentStation]);
    u8g2.sendBuffer();
}

void scrollText() {
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        textPosition--;  // Move text to the left
        if (textPosition < -u8g2.getUTF8Width(stationNames[currentStation])) {
            textPosition = 128;  // Reset position to start from the right again
        }

        u8g2.clearBuffer();
        u8g2.drawLine(0, 0, 127, 0);
        u8g2.drawLine(0, 31, 127, 31);
        u8g2.setCursor(textPosition, 22);
        u8g2.print(stationNames[currentStation]);
        u8g2.sendBuffer();
    }
}
