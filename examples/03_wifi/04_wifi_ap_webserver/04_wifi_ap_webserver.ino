// 04_wifi_ap_webserver.ino - the board makes its own WiFi network and serves a web page.
// Join "Baseline-AP" (password: sketcher123) and open http://192.168.4.1/
// The page toggles the board's LED.
#include <WiFi.h>
#include <WebServer.h>

WebServer server(80);
bool ledOn = false;

void handleRoot()
{
    String html = "<html><body><h1>Flick</h1>";
    html += "<p>LED is " + String(ledOn ? "ON" : "OFF") + "</p>";
    html += "<p><a href='/toggle'>Toggle LED</a></p>";
    html += "<p>Uptime: " + String(millis() / 1000) + " s</p></body></html>";
    server.send(200, "text/html", html);
}

void handleToggle()
{
    ledOn = !ledOn;
    digitalWrite(STAT_LED, ledOn ? HIGH : LOW);
    server.sendHeader("Location", "/");
    server.send(303);
}

void setup()
{
    pinMode(STAT_LED, OUTPUT);
    WiFi.softAP("Baseline-AP", "sketcher123");
    Serial.print("Access point started. Browse to http://");
    Serial.println(WiFi.softAPIP());

    server.on("/", handleRoot);
    server.on("/toggle", handleToggle);
    server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
    server.begin();
}

void loop()
{
    server.handleClient();
    delay(2);
}
