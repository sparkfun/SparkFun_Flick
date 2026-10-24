/*
 * Sketch upload protocol over the USB console. Text lines, so it coexists
 * with log output on the same port:
 *
 *   host -> @BSK INFO                       <- @BSK OK baseline=0.1.0 abi=1.0 ...
 *   host -> @BSK BEGIN <size> <sha256hex>   <- @BSK OK slot=b      (inactive slot erased)
 *   host -> @BSK DATA <offset> <base64>     <- @BSK OK <next offset>
 *   host -> @BSK END                        <- @BSK OK slot=b name=...   then reboot
 *   host -> @BSK ABORT | SAFE | RESET
 * Errors: @BSK ERR <message>
 * All other console input is passed to the sketch.
 * The same commands are accepted over WiFi once the sketch calls
 * ArduinoOTA.begin() (bl_ota.cpp; TCP port 3232, optional "@BSK AUTH <password>").
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void loader_start(void);

/* Run one "@BSK ..." command from any transport; replies go to sink(text, ctx). */
typedef void (*loader_sink_t)(const char *text, void *ctx);
void loader_handle_line(const char *line, loader_sink_t sink, void *ctx);

/* Console input that is not loader traffic, for the sketch (Serial.read()). */
int loader_sketch_available(void);
int loader_sketch_read(void);       /* -1 if none */

#ifdef __cplusplus
}
#endif
