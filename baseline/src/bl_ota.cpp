/* API table: OTA - sketch uploads over WiFi.
 * A Baseline task listens on TCP (default port 3232, Arduino's OTA port) and
 * runs the same "@BSK ..." line protocol as the USB loader. With a password
 * set, a connection must send "@BSK AUTH <password>" first.
 * Baseline itself is updated over USB: the 4 MB flash has room for one
 * Baseline image only. */
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "gen/bl_impl.h"
#include "bl_console.h"
#include "bl_mdns.h"
#include "loader_proto.h"

#define OTA_STACK       6144
#define OTA_CORE        0
#define LINE_MAX_LEN    1600
#define IDLE_TIMEOUT_MS 30000

static TaskHandle_t s_task;
static NetworkServer *s_server;
static String s_password;
static volatile bool s_run;

static void send_to_client(const char *text, void *ctx)
{
    ((NetworkClient *)ctx)->print(text);
}

static void serve(NetworkClient &client)
{
    bool authed = s_password.isEmpty();
    String line;
    line.reserve(LINE_MAX_LEN);
    uint32_t last = millis();
    while (s_run && client.connected() && millis() - last < IDLE_TIMEOUT_MS) {
        while (client.available()) {
            char c = (char)client.read();
            last = millis();
            if (c != '\n') {
                if (line.length() < LINE_MAX_LEN) {
                    line += c;
                }
                continue;
            }
            line.trim();
            if (line.startsWith("@BSK AUTH ")) {
                authed = line.substring(10) == s_password;
                client.print(authed ? "@BSK OK authorized\n" : "@BSK ERR wrong password\n");
            } else if (!line.startsWith("@BSK ")) {
                // ignore
            } else if (!authed) {
                client.print("@BSK ERR password required\n");
            } else {
                loader_handle_line(line.c_str(), send_to_client, &client);
            }
            line = "";
        }
        vTaskDelay(1);
    }
    client.stop();
}

static void ota_task(void *arg)
{
    while (s_run) {
        NetworkClient client = s_server->accept();
        if (client) {
            BLOG_I("OTA: connection from %s", client.remoteIP().toString().c_str());
            serve(client);
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    s_server->end();
    delete s_server;
    s_server = NULL;
    s_task = NULL;
    vTaskDelete(NULL);
}

extern "C" int bl_impl_ota_begin(const char *hostname, const char *password, uint16_t port)
{
    if (s_task) {
        return BL_OK;   // already running
    }
    s_password = password ? password : "";
    if (!port) {
        port = 3232;
    }
    if (hostname && *hostname && bl_mdns_ensure(hostname)) {
        MDNS.addService("bsk", "tcp", port);
    }
    s_server = new NetworkServer(port);
    s_server->begin();
    s_run = true;
    if (xTaskCreatePinnedToCore(ota_task, "bsk_ota", OTA_STACK, NULL, 2, &s_task, OTA_CORE) != pdPASS) {
        s_run = false;
        delete s_server;
        s_server = NULL;
        return BL_ERR_NO_MEM;
    }
    BLOG_I("OTA: accepting sketch uploads on port %u%s", port, s_password.isEmpty() ? "" : " (password set)");
    return BL_OK;
}

extern "C" void bl_impl_ota_end(void)
{
    s_run = false;      // the task closes the server and exits
}
