// captive_portal.ino - an access point whose web page opens by itself.
// Join the "Baseline-Portal" WiFi network with a phone: the page pops up.
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>

DNSServer dns;
WebServer server(80);
int visits;

void handleRoot()
{
    visits++;
    String page = "<html><body style='font-family:sans-serif'><h1>Hello from Baseline!</h1>"
                  "<p>This page has been opened " + String(visits) + " times.</p></body></html>";
    server.send(200, "text/html", page);
}

void setup()
{
    Serial.begin(115200);
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Baseline-Portal");
    IPAddress ip = WiFi.softAPIP();
    dns.start(53, "*", ip);             // every name resolves to us
    server.on("/", handleRoot);
    server.onNotFound(handleRoot);      // phones probe odd URLs: answer them all
    server.begin();
    Serial.print("Portal ready at ");
    Serial.println(ip);
}

void loop()
{
    dns.processNextRequest();
    server.handleClient();
    delay(2);
}
