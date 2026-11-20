# Baseline API reference (ABI 1.10)

Generated from `api/baseline_api.txt` by `tools/gen_api.py`. Do not edit.

Include `baseline.h` in your sketch. Call the **Sketch name** column. Every call goes through the `BL` table pointer.

| Sketch name | Signature | Since | Description |
|---|---|---|---|
| `millis` | `uint32_t millis(void)` | 1.0 | Milliseconds since boot. |
| `micros` | `uint64_t micros(void)` | 1.0 | Microseconds since boot. |
| `BL->delay_ms` | `void delay_ms(uint32_t ms)` | 1.0 | delay(ms): block the sketch task for ms milliseconds (other tasks keep running). |
| `delayMicroseconds` | `void delay_us(uint32_t us)` | 1.0 | Busy-wait for us microseconds. |
| `BL->yield` | `void yield(void)` | 1.0 | yield(): let other tasks run. |
| `bl_printf` | `int printf(const char *fmt, ...)` | 1.0 | printf to the Baseline console. |
| `bl_vprintf` | `int vprintf(const char *fmt, va_list ap)` | 1.0 | vprintf to the Baseline console. |
| `bl_snprintf` | `int snprintf(char *buf, size_t size, const char *fmt, ...)` | 1.0 | Format into a buffer. |
| `bl_vsnprintf` | `int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)` | 1.0 | Format into a buffer (va_list). |
| `bl_log` | `void log(int level, const char *tag, const char *fmt, ...)` | 1.0 | Log through ESP-IDF logging. level: BL_LOG_*. |
| `bl_malloc` | `void * malloc(size_t size)` | 1.0 | Allocate from the Baseline heap. |
| `bl_calloc` | `void * calloc(size_t n, size_t size)` | 1.0 | Allocate zeroed memory. |
| `bl_realloc` | `void * realloc(void *ptr, size_t size)` | 1.0 | Resize an allocation. |
| `bl_free` | `void free(void *ptr)` | 1.0 | Free an allocation. |
| `bl_free_heap` | `uint32_t free_heap(void)` | 1.0 | Free heap bytes. |
| `bl_restart` | `void restart(void)` | 1.0 | Reboot the device. |
| `bl_baseline_version` | `const char * baseline_version(void)` | 1.0 | Baseline firmware version string. |
| `pinMode` | `int gpio_mode(int pin, int mode)` | 1.0 | Configure a pin. mode: INPUT, OUTPUT, INPUT_PULLUP, INPUT_PULLDOWN, OUTPUT_OPEN_DRAIN. Returns 0 or BL_ERR_*. |
| `digitalWrite` | `int gpio_write(int pin, int level)` | 1.0 | Drive an output pin HIGH or LOW. Returns 0 or BL_ERR_*. |
| `digitalRead` | `int gpio_read(int pin)` | 1.0 | Read a pin. Returns HIGH, LOW, or BL_ERR_*. |
| `bl_console_write` | `int console_write(const uint8_t *data, size_t len)` | 1.1 | Write raw bytes to the USB console. Returns bytes written. |
| `bl_console_available` | `int console_available(void)` | 1.1 | Bytes waiting to be read from the USB console. |
| `bl_console_read` | `int console_read(void)` | 1.1 | Read one byte from the USB console, or -1 if none. |
| `analogRead` | `int adc_read(int pin)` | 1.1 | Read a pin's ADC value (0-4095), or BL_ERR_*. |
| `analogReadMilliVolts` | `int adc_read_mv(int pin)` | 1.1 | Read a pin's voltage in millivolts, or BL_ERR_*. |
| `analogWrite` | `int pwm_write(int pin, int value)` | 1.1 | PWM output, value 0-255 (LEDC, 1 kHz). Returns 0 or BL_ERR_*. |
| `rgbLedWrite` | `int rgb_write(int pin, uint8_t red, uint8_t green, uint8_t blue)` | 1.1 | Set a WS2812 RGB LED (RGB_BUILTIN: S3 Thing Plus 46, Thing Plus C 2). Returns 0 or BL_ERR_*. |
| `bl_i2c_begin` | `int i2c_begin(int sda, int scl, uint32_t freq)` | 1.1 | Start I2C. sda/scl -1 = board Qwiic pins (8/9). freq in Hz (0 = 100 kHz). |
| `bl_i2c_write` | `int i2c_write(uint8_t addr, const uint8_t *data, size_t len, bool stop)` | 1.1 | Write bytes (len 0 = probe). 0, BL_ERR_NACK, BL_ERR_TIMEOUT or BL_ERR_FAIL. |
| `bl_i2c_read` | `int i2c_read(uint8_t addr, uint8_t *data, size_t len)` | 1.1 | Read bytes. Returns the number read, or BL_ERR_*. |
| `bl_wifi_begin` | `int wifi_begin(const char *ssid, const char *password)` | 1.1 | Start connecting as a station (returns at once). password may be NULL. |
| `bl_wifi_status` | `int wifi_status(void)` | 1.1 | BL_WIFI_* status (same values as Arduino's WL_*). |
| `bl_wifi_disconnect` | `void wifi_disconnect(void)` | 1.1 | Disconnect from the access point. |
| `bl_wifi_local_ip` | `int wifi_local_ip(char *buf, size_t size)` | 1.1 | Write the IP address as text ("192.168.1.20"). Returns 0 or BL_ERR_STATE. |
| `bl_wifi_rssi` | `int wifi_rssi(void)` | 1.1 | Signal strength in dBm (0 when not connected). |
| `bl_wifi_mac` | `int wifi_mac(uint8_t *mac)` | 1.1 | Copy the 6-byte station MAC address into mac. Returns 0. |
| `bl_http_get` | `int http_get(const char *url, char *body, size_t size, int *status)` | 1.1 | HTTP(S) GET; follows redirects, verifies TLS with the built-in CA bundle. Body is NUL-terminated and truncated to size-1. Returns body length or BL_ERR_*. |
| `bl_ble_serial_begin` | `int ble_serial_begin(const char *name)` | 1.1 | Start advertising a BLE UART (Nordic UART Service) named `name`. |
| `bl_ble_serial_connected` | `int ble_serial_connected(void)` | 1.1 | 1 if a central is connected. |
| `bl_ble_serial_write` | `int ble_serial_write(const uint8_t *data, size_t len)` | 1.1 | Send bytes as notifications. Returns bytes sent (0 if not connected). |
| `bl_ble_serial_available` | `int ble_serial_available(void)` | 1.1 | Bytes received and not yet read. |
| `bl_ble_serial_read` | `int ble_serial_read(void)` | 1.1 | Read one received byte, or -1. |
| `bl_espnow_begin` | `int espnow_begin(void)` | 1.1 | Start ESP-NOW (puts WiFi in station mode if needed). |
| `bl_espnow_send` | `int espnow_send(const uint8_t *mac, const uint8_t *data, size_t len)` | 1.1 | Send up to 250 bytes. mac NULL = broadcast. Returns 0 or BL_ERR_*. |
| `bl_espnow_receive` | `int espnow_receive(uint8_t *mac, uint8_t *data, size_t size)` | 1.1 | Take the next received packet: copies sender MAC (6 bytes, may be NULL) and data. Returns length, 0 if none. |
| `bl_http_request` | `int http_request(const char *method, const char *url, const char *headers, const uint8_t *body, size_t body_len, char * *response, size_t *response_len)` | 1.2 | HTTP(S) request. headers: "Name: value\r\n" lines or NULL. *response gets a malloc'd NUL-terminated body (free with bl_free); may be NULL. Returns HTTP status or BL_ERR_*. |
| `bl_espnow_send_status` | `int espnow_send_status(uint8_t *mac)` | 1.2 | Take the next delivery report: copies the peer MAC; returns 1 delivered, 0 failed, -1 none pending. |
| `bl_system_info` | `uint32_t system_info(int what)` | 1.2 | System facts: what = BL_INFO_* (CPU MHz, heap/PSRAM/flash sizes). |
| `bl_random` | `uint32_t random(void)` | 1.2 | 32-bit hardware random number. |
| `bl_gpio_attach_interrupt` | `int gpio_attach_interrupt(int pin, int mode)` | 1.3 | Watch a pin. mode: BL_RISING, BL_FALLING, BL_CHANGE, BL_ONLOW, BL_ONHIGH. |
| `bl_gpio_detach_interrupt` | `int gpio_detach_interrupt(int pin)` | 1.3 | Stop watching a pin. |
| `bl_gpio_interrupt_take` | `int gpio_interrupt_take(void)` | 1.3 | Next pin that fired, or -1. |
| `bl_spi_begin` | `int spi_begin(int sck, int miso, int mosi, int ss)` | 1.3 | Start the SPI bus. -1 = board default pin. |
| `bl_spi_end` | `void spi_end(void)` | 1.3 | Release the SPI bus. |
| `bl_spi_begin_transaction` | `void spi_begin_transaction(uint32_t clock, int bit_order, int data_mode)` | 1.3 | Lock the bus with these settings. bit_order: BL_MSBFIRST/BL_LSBFIRST, data_mode 0-3. |
| `bl_spi_end_transaction` | `void spi_end_transaction(void)` | 1.3 | Unlock the bus. |
| `bl_spi_transfer` | `void spi_transfer(uint8_t *data, size_t len)` | 1.3 | Full-duplex transfer; received bytes replace data. |
| `bl_spi_write` | `void spi_write(const uint8_t *data, size_t len)` | 1.3 | Send bytes, discard what comes back. |
| `bl_wifi_set_mode` | `int wifi_set_mode(int mode)` | 1.3 | BL_WIFI_MODE_OFF/STA/AP/AP_STA. |
| `bl_wifi_get_mode` | `int wifi_get_mode(void)` | 1.3 | Current BL_WIFI_MODE_*. |
| `bl_wifi_softap` | `int wifi_softap(const char *ssid, const char *password, int channel, int hidden, int max_clients)` | 1.3 | Start an access point (password NULL or >= 8 chars). |
| `bl_wifi_softap_stop` | `int wifi_softap_stop(void)` | 1.3 | Stop the access point. |
| `bl_wifi_softap_stations` | `int wifi_softap_stations(void)` | 1.3 | Stations connected to our access point. |
| `bl_wifi_ip` | `uint32_t wifi_ip(int which)` | 1.3 | IPv4 address, first octet in the low byte. which: BL_IP_*. |
| `bl_wifi_scan` | `int wifi_scan(void)` | 1.3 | Scan for access points (blocks ~2-4 s). Returns the count or BL_ERR_*. |
| `bl_wifi_scan_result` | `int wifi_scan_result(int index, bl_wifi_ap_t *ap)` | 1.3 | Copy one scan result. |
| `bl_wifi_channel` | `int wifi_channel(void)` | 1.3 | Current WiFi channel. |
| `bl_wifi_ssid` | `int wifi_ssid(char *buf, size_t size)` | 1.3 | SSID we are connected to. Returns its length. |
| `bl_wifi_config` | `int wifi_config(uint32_t ip, uint32_t gateway, uint32_t subnet, uint32_t dns)` | 1.3 | Static IP (all 0 = DHCP). |
| `bl_wifi_set_hostname` | `int wifi_set_hostname(const char *name)` | 1.3 | Hostname used for DHCP (call before begin). |
| `bl_net_resolve` | `int net_resolve(const char *host, uint32_t *ip)` | 1.3 | DNS lookup. Returns 0 or BL_ERR_*. |
| `bl_tcp_client_new` | `int tcp_client_new(int secure)` | 1.3 | New client handle (secure = TLS). Reference count starts at 1. |
| `bl_tcp_client_retain` | `void tcp_client_retain(int h)` | 1.3 | Add a reference (the SDK copies clients like Arduino does). |
| `bl_tcp_client_release` | `void tcp_client_release(int h)` | 1.3 | Drop a reference; the connection closes at zero. |
| `bl_tcp_client_tls_config` | `int tcp_client_tls_config(int h, int insecure, const char *ca_pem)` | 1.3 | TLS: skip verification, or use this CA (NULL = built-in CA bundle). Must stay valid. |
| `bl_tcp_client_connect` | `int tcp_client_connect(int h, const char *host, uint16_t port, int32_t timeout_ms)` | 1.3 | Connect. Returns 1 or 0. |
| `bl_tcp_client_write` | `int tcp_client_write(int h, const uint8_t *data, size_t len)` | 1.3 | Returns bytes written. |
| `bl_tcp_client_available` | `int tcp_client_available(int h)` | 1.3 | Bytes ready to read. |
| `bl_tcp_client_read` | `int tcp_client_read(int h, uint8_t *buf, size_t size)` | 1.3 | Read up to size bytes; -1 if none. |
| `bl_tcp_client_peek` | `int tcp_client_peek(int h)` | 1.3 | Next byte without consuming it, or -1. |
| `bl_tcp_client_connected` | `int tcp_client_connected(int h)` | 1.3 | 1 while connected or data is still buffered. |
| `bl_tcp_client_stop` | `void tcp_client_stop(int h)` | 1.3 | Close the connection (the handle stays valid). |
| `bl_tcp_client_remote` | `int tcp_client_remote(int h, uint32_t *ip, uint16_t *port)` | 1.3 | Peer address. |
| `bl_tcp_server_new` | `int tcp_server_new(uint16_t port, int max_clients)` | 1.3 | Listen on port. |
| `bl_tcp_server_accept` | `int tcp_server_accept(int h)` | 1.3 | A newly connected client handle, or -1. |
| `bl_tcp_server_close` | `void tcp_server_close(int h)` | 1.3 | Stop listening and free the handle. |
| `bl_udp_new` | `int udp_new(uint16_t port)` | 1.3 | Open a UDP socket (port 0 = any). |
| `bl_udp_close` | `void udp_close(int h)` | 1.3 | Close and free. |
| `bl_udp_begin_packet` | `int udp_begin_packet(int h, const char *host, uint16_t port)` | 1.3 | Start a packet to host (name or dotted IP). |
| `bl_udp_write` | `int udp_write(int h, const uint8_t *data, size_t len)` | 1.3 | Add bytes to the packet. |
| `bl_udp_end_packet` | `int udp_end_packet(int h)` | 1.3 | Send it. Returns 1 or 0. |
| `bl_udp_parse_packet` | `int udp_parse_packet(int h)` | 1.3 | Size of the next received packet, or 0. |
| `bl_udp_read` | `int udp_read(int h, uint8_t *buf, size_t size)` | 1.3 | Read from the current packet. |
| `bl_udp_remote` | `int udp_remote(int h, uint32_t *ip, uint16_t *port)` | 1.3 | Sender of the current packet. |
| `bl_web_new` | `int web_new(uint16_t port)` | 1.3 | Create and start a web server. |
| `bl_web_close` | `void web_close(int h)` | 1.3 | Stop and free. |
| `bl_web_on` | `int web_on(int h, const char *uri, int method, bl_callback_t fn, void *arg)` | 1.3 | Route uri (method BL_HTTP_* or BL_HTTP_ANY) to fn(arg). |
| `bl_web_on_not_found` | `int web_on_not_found(int h, bl_callback_t fn, void *arg)` | 1.3 | Handler for unmatched requests. |
| `bl_web_handle_client` | `void web_handle_client(int h)` | 1.3 | Serve pending requests (call from loop()). |
| `bl_web_send` | `void web_send(int h, int code, const char *content_type, const uint8_t *content, size_t len)` | 1.3 | Send the response. |
| `bl_web_send_header` | `void web_send_header(int h, const char *name, const char *value, int first)` | 1.3 | Add a response header (before web_send). |
| `bl_web_request` | `int web_request(int h, int what, int index, const char *name, char *buf, size_t size)` | 1.3 | Request details (what = BL_WEB_*). Copies text, returns its full length, or -1 if absent. |
| `bl_web_collect_headers` | `void web_collect_headers(int h, const char * *names, size_t count)` | 1.3 | Request headers to keep for BL_WEB_HEADER. |
| `bl_ble_init` | `int ble_init(const char *name)` | 1.3 | Start BLE with this device name (safe to call twice). |
| `bl_ble_deinit` | `void ble_deinit(void)` | 1.3 | Stop BLE. |
| `bl_ble_address` | `int ble_address(char *buf, size_t size)` | 1.3 | Our BLE address "aa:bb:cc:dd:ee:ff". |
| `bl_ble_set_mtu` | `int ble_set_mtu(uint16_t mtu)` | 1.3 | Preferred ATT MTU. |
| `bl_ble_server_create` | `int ble_server_create(void)` | 1.3 | The GATT server (one per device). |
| `bl_ble_server_connected_count` | `int ble_server_connected_count(void)` | 1.3 | Connected centrals. |
| `bl_ble_server_disconnect` | `void ble_server_disconnect(uint16_t conn_id)` | 1.3 | Drop a connection. |
| `bl_ble_service_create` | `int ble_service_create(const char *uuid, int num_handles)` | 1.3 | New service on the server. |
| `bl_ble_service_start` | `int ble_service_start(int svc)` | 1.3 | Publish the service. |
| `bl_ble_char_create` | `int ble_char_create(int svc, const char *uuid, uint32_t properties)` | 1.3 | New characteristic. properties: BL_BLE_PROP_* bits. |
| `bl_ble_char_set_value` | `int ble_char_set_value(int ch, const uint8_t *data, size_t len)` | 1.3 | Set the value centrals read / get notified with. |
| `bl_ble_char_get_value` | `int ble_char_get_value(int ch, uint8_t *buf, size_t size)` | 1.3 | Copy the value; returns its full length. |
| `bl_ble_char_notify` | `int ble_char_notify(int ch, int indicate)` | 1.3 | Send the value as a notification (or indication). |
| `bl_ble_char_add_descriptor` | `int ble_char_add_descriptor(int ch, const char *uuid, const uint8_t *value, size_t len)` | 1.3 | Add a descriptor (e.g. 2901 user description). |
| `bl_ble_adv_config` | `int ble_adv_config(int what, const char *text, const uint8_t *data, size_t value)` | 1.3 | Advertising setting: what = BL_BLE_ADV_*. |
| `bl_ble_adv_start` | `int ble_adv_start(void)` | 1.3 | Start advertising. |
| `bl_ble_adv_stop` | `int ble_adv_stop(void)` | 1.3 | Stop advertising. |
| `bl_ble_scan_start` | `int ble_scan_start(uint32_t seconds, int active, uint16_t interval_ms, uint16_t window_ms)` | 1.3 | Start scanning in the background. |
| `bl_ble_scan_stop` | `int ble_scan_stop(void)` | 1.3 | Stop scanning. |
| `bl_ble_scan_running` | `int ble_scan_running(void)` | 1.3 | 1 while a scan is in progress. |
| `bl_ble_scan_count` | `int ble_scan_count(void)` | 1.3 | Devices found by the last scan. |
| `bl_ble_scan_result` | `int ble_scan_result(int index, bl_ble_adv_t *dev)` | 1.3 | Copy one scan result. |
| `bl_ble_scan_clear` | `void ble_scan_clear(void)` | 1.3 | Forget scan results. |
| `bl_ble_client_connect` | `int ble_client_connect(const char *address, int addr_type)` | 1.3 | Connect to a peripheral (blocks up to ~5 s). Returns a client handle. |
| `bl_ble_client_disconnect` | `void ble_client_disconnect(int cl)` | 1.3 | Disconnect. |
| `bl_ble_client_connected` | `int ble_client_connected(int cl)` | 1.3 | 1 while connected. |
| `bl_ble_client_rssi` | `int ble_client_rssi(int cl)` | 1.3 | Signal strength of the connection. |
| `bl_ble_remote_char` | `int ble_remote_char(int cl, const char *service_uuid, const char *char_uuid)` | 1.3 | Find a characteristic on the peripheral. Returns a handle. |
| `bl_ble_remote_read` | `int ble_remote_read(int rc, uint8_t *buf, size_t size)` | 1.3 | Read the value; returns its length. |
| `bl_ble_remote_write` | `int ble_remote_write(int rc, const uint8_t *data, size_t len, int response)` | 1.3 | Write the value. |
| `bl_ble_remote_subscribe` | `int ble_remote_subscribe(int rc, int mode)` | 1.3 | 0 off, 1 notifications, 2 indications (delivered as BL_BLE_EV_NOTIFY). |
| `bl_ble_remote_props` | `uint32_t ble_remote_props(int rc)` | 1.3 | BL_BLE_PROP_* bits of a remote characteristic. |
| `bl_ble_event_take` | `int ble_event_take(bl_ble_event_t *ev)` | 1.3 | Take the next BLE event. Returns 1, or 0 if none. |
| `bl_prefs_open` | `int prefs_open(const char *name, int read_only)` | 1.4 | Open a namespace. Returns a handle. |
| `bl_prefs_close` | `void prefs_close(int h)` | 1.4 | Close it. |
| `bl_prefs_clear` | `int prefs_clear(int h)` | 1.4 | Delete every key in the namespace. |
| `bl_prefs_remove` | `int prefs_remove(int h, const char *key)` | 1.4 | Delete one key. |
| `bl_prefs_put` | `int prefs_put(int h, const char *key, int type, const void *data, size_t len)` | 1.4 | Store a value (type BL_PREF_*). Returns bytes stored, 0 on failure. |
| `bl_prefs_get` | `int prefs_get(int h, const char *key, int type, void *buf, size_t size)` | 1.4 | Read a value. Returns its length (strings: without NUL), or -1 if the key is missing. |
| `bl_prefs_info` | `int prefs_info(int h, const char *key, int what)` | 1.4 | BL_PREF_INFO_*: key exists, stored type, length, free entries. |
| `bl_fs_begin` | `int fs_begin(int format_if_failed)` | 1.4 | Mount (optionally format a blank partition). |
| `bl_fs_end` | `void fs_end(void)` | 1.4 | Unmount. |
| `bl_fs_format` | `int fs_format(void)` | 1.4 | Erase everything. |
| `bl_fs_info` | `uint32_t fs_info(int what)` | 1.4 | BL_FS_TOTAL_BYTES or BL_FS_USED_BYTES. |
| `bl_fs_open` | `int fs_open(const char *path, const char *mode, int create)` | 1.4 | Open a file or directory ("r", "w", "a", ...). Returns a handle (reference count 1). |
| `bl_fs_retain` | `void fs_retain(int h)` | 1.4 | Add a reference (File objects are copyable). |
| `bl_fs_release` | `void fs_release(int h)` | 1.4 | Drop a reference; the file closes at zero. |
| `bl_fs_read` | `int fs_read(int h, uint8_t *buf, size_t size)` | 1.4 | Read up to size bytes. |
| `bl_fs_write` | `int fs_write(int h, const uint8_t *data, size_t len)` | 1.4 | Write bytes. |
| `bl_fs_peek` | `int fs_peek(int h)` | 1.4 | Next byte without consuming it, or -1. |
| `bl_fs_seek` | `int fs_seek(int h, uint32_t pos, int mode)` | 1.4 | Move (mode 0 set, 1 current, 2 end). Returns 1 or 0. |
| `bl_fs_flush` | `void fs_flush(int h)` | 1.4 | Flush writes. |
| `bl_fs_file_info` | `uint32_t fs_file_info(int h, int what)` | 1.4 | BL_FS_FILE_*: position, size, is directory, last write time. |
| `bl_fs_file_name` | `int fs_file_name(int h, int full_path, char *buf, size_t size)` | 1.4 | Name (or full path). Returns its length. |
| `bl_fs_next` | `int fs_next(int h)` | 1.4 | Next entry of an open directory: a new handle, or -1 at the end. |
| `bl_fs_path_op` | `int fs_path_op(int op, const char *path, const char *path2)` | 1.4 | BL_FS_* path operation (exists, delete, rename, mkdir, rmdir). Returns 1 or 0. |
| `bl_uart_begin` | `int uart_begin(int port, uint32_t baud, uint32_t config, int rx, int tx)` | 1.4 | Start UART 1 or 2. config = SERIAL_8N1 etc. -1 pins = defaults. |
| `bl_uart_end` | `void uart_end(int port)` | 1.4 | Stop it. |
| `bl_uart_write` | `int uart_write(int port, const uint8_t *data, size_t len)` | 1.4 | Send bytes. |
| `bl_uart_available` | `int uart_available(int port)` | 1.4 | Bytes waiting. |
| `bl_uart_read` | `int uart_read(int port, uint8_t *buf, size_t size)` | 1.4 | Read up to size bytes (0 if none). |
| `bl_uart_peek` | `int uart_peek(int port)` | 1.4 | Next byte without consuming it, or -1. |
| `bl_uart_flush` | `void uart_flush(int port)` | 1.4 | Wait until everything is sent. |
| `bl_mdns_begin` | `int mdns_begin(const char *hostname)` | 1.4 | Answer to hostname.local. |
| `bl_mdns_end` | `void mdns_end(void)` | 1.4 | Stop mDNS. |
| `bl_mdns_add_service` | `int mdns_add_service(const char *service, const char *proto, uint16_t port)` | 1.4 | Advertise a service ("http", "tcp", 80). |
| `bl_mdns_add_txt` | `int mdns_add_txt(const char *service, const char *proto, const char *key, const char *value)` | 1.4 | Add a TXT record to a service. |
| `bl_mdns_query_host` | `uint32_t mdns_query_host(const char *host, uint32_t timeout_ms)` | 1.4 | Resolve name.local (without ".local"). Returns the IPv4 address or 0. |
| `bl_mdns_query_service` | `int mdns_query_service(const char *service, const char *proto)` | 1.4 | Browse for a service. Returns how many were found. |
| `bl_mdns_result` | `int mdns_result(int index, int what, char *buf, size_t size)` | 1.4 | One browse result: BL_MDNS_HOSTNAME (text), BL_MDNS_IP, BL_MDNS_PORT (numbers). |
| `bl_mqtt_new` | `int mqtt_new(void)` | 1.4 | New MQTT client handle. |
| `bl_mqtt_free` | `void mqtt_free(int h)` | 1.4 | Disconnect and free. |
| `bl_mqtt_config` | `int mqtt_config(int h, int what, const char *text, int32_t value)` | 1.4 | Setting BL_MQTT_* (server, client id, credentials, will, TLS, ...). Text is copied. |
| `bl_mqtt_connect` | `int mqtt_connect(int h, uint32_t timeout_ms)` | 1.4 | Connect with the current settings. Returns 1 or 0. |
| `bl_mqtt_disconnect` | `void mqtt_disconnect(int h)` | 1.4 | Disconnect. |
| `bl_mqtt_state` | `int mqtt_state(int h)` | 1.4 | PubSubClient-style state (0 = connected, negative = not). |
| `bl_mqtt_publish` | `int mqtt_publish(int h, const char *topic, const uint8_t *payload, size_t len, int qos, int retain)` | 1.4 | Publish. Returns 1 or 0. |
| `bl_mqtt_subscribe` | `int mqtt_subscribe(int h, const char *topic, int qos)` | 1.4 | Subscribe. Returns 1 or 0. |
| `bl_mqtt_unsubscribe` | `int mqtt_unsubscribe(int h, const char *topic)` | 1.4 | Unsubscribe. Returns 1 or 0. |
| `bl_mqtt_take` | `int mqtt_take(int h, char *topic, size_t topic_size, uint8_t *payload, size_t payload_size)` | 1.4 | Next received message: copies topic and payload; returns the payload length, or -1 if none. |
| `bl_ota_begin` | `int ota_begin(const char *hostname, const char *password, uint16_t port)` | 1.4 | Accept sketch uploads over WiFi (and advertise NAME.local). password may be NULL. |
| `bl_ota_end` | `void ota_end(void)` | 1.4 | Stop accepting uploads over WiFi. |
| `bl_bt_spp_begin` | `int bt_spp_begin(const char *name)` | 1.5 | Start an SPP serial port visible as `name`. BL_ERR_UNSUPPORTED on chips without Bluetooth Classic. |
| `bl_bt_spp_end` | `void bt_spp_end(void)` | 1.5 | Stop it. |
| `bl_bt_spp_connected` | `int bt_spp_connected(void)` | 1.5 | 1 if a device is connected. |
| `bl_bt_spp_write` | `int bt_spp_write(const uint8_t *data, size_t len)` | 1.5 | Send bytes. Returns bytes sent. |
| `bl_bt_spp_available` | `int bt_spp_available(void)` | 1.5 | Bytes received and not yet read. |
| `bl_bt_spp_read` | `int bt_spp_read(uint8_t *buf, size_t size)` | 1.5 | Read up to size bytes (0 if none). |
| `bl_bt_spp_peek` | `int bt_spp_peek(void)` | 1.5 | Next byte without consuming it, or -1. |
| `bl_bt_spp_flush` | `void bt_spp_flush(void)` | 1.5 | Wait until everything is sent. |
| `bl_bt_spp_set_pin` | `int bt_spp_set_pin(const char *pin)` | 1.5 | Legacy pairing PIN (call before begin). 0 or BL_ERR_*. |
| `bl_sd_begin` | `int sd_begin(int bus, int cs, uint32_t freq_hz, int format_if_empty)` | 1.6 | Mount the microSD card. bus = BL_SD_SPI (cs pin, -1 = board default) or BL_SD_MMC (board pins; cs = 1 for 1-bit mode). |
| `bl_sd_end` | `void sd_end(void)` | 1.6 | Unmount the card. |
| `bl_sd_info` | `uint64_t sd_info(int what)` | 1.6 | BL_SD_CARD_TYPE / CARD_SIZE / TOTAL_BYTES / USED_BYTES. |
| `bl_fsx_open` | `int fsx_open(int fs, const char *path, const char *mode, int create)` | 1.6 | Open a file or directory on a file system. Returns a handle. |
| `bl_fsx_path_op` | `int fsx_path_op(int fs, int op, const char *path, const char *path2)` | 1.6 | BL_FS_EXISTS / REMOVE / RENAME / MKDIR / RMDIR. Returns 1 or 0. |
| `bl_sleep_wake` | `int sleep_wake(int source, uint64_t value, int pin, int level)` | 1.6 | Enable a wake source (BL_WAKE_*): TIMER value = us; EXT0 pin+level; EXT1 value = pin mask, level = mode; TOUCH; GPIO/UART (light sleep). |
| `bl_sleep_disable_wake` | `int sleep_disable_wake(int source)` | 1.6 | Disable a wake source (BL_WAKE_ALL = every source). |
| `bl_sleep_deep` | `void sleep_deep(void)` | 1.6 | Deep sleep. The board restarts on wake: Baseline boots and starts the sketch again. |
| `bl_sleep_light` | `int sleep_light(void)` | 1.6 | Light sleep; returns after wake with 0 or BL_ERR_*. |
| `bl_sleep_wake_cause` | `int sleep_wake_cause(void)` | 1.6 | Why the chip woke (esp_sleep_wakeup_cause_t values; 0 = not from sleep). |
| `bl_sleep_ext1_status` | `uint64_t sleep_ext1_status(void)` | 1.6 | Pin mask that caused an EXT1 wake. |
| `bl_rtc_memory` | `void * rtc_memory(uint32_t *size)` | 1.6 | 1 KB of RTC memory kept through deep sleep and resets (cleared at power-on). |
| `bl_timer_new` | `int timer_new(void)` | 1.6 | New timer handle. |
| `bl_timer_free` | `void timer_free(int h)` | 1.6 | Stop and free it. |
| `bl_timer_start` | `int timer_start(int h, uint64_t period_us, int periodic)` | 1.6 | Fire after period_us (repeatedly if periodic). |
| `bl_timer_stop` | `void timer_stop(int h)` | 1.6 | Stop it. |
| `bl_timer_active` | `int timer_active(int h)` | 1.6 | 1 while running. |
| `bl_timer_take` | `int timer_take(void)` | 1.6 | Next timer that fired (one result per firing), or -1. |
| `bl_time_ntp_start` | `void time_ntp_start(const char *server1, const char *server2, const char *server3)` | 1.6 | Start NTP time sync (WiFi must be connected). NULL server1 = pool.ntp.org. |
| `bl_time_get` | `int time_get(int64_t *sec, int32_t *usec)` | 1.6 | gettimeofday(): seconds since 1970 (UTC). |
| `bl_time_set` | `int time_set(int64_t sec, int32_t usec)` | 1.6 | settimeofday(). |
| `bl_time_synced` | `int time_synced(void)` | 1.6 | 1 once NTP (or time_set) has set the clock. |
| `bl_touch_read` | `uint32_t touch_read(int pin)` | 1.6 | touchRead() value (0 if not a touch pin). |
| `bl_touch_attach` | `int touch_attach(int pin, uint32_t threshold)` | 1.6 | Report touches past threshold through touch_take(). |
| `bl_touch_detach` | `int touch_detach(int pin)` | 1.6 | Stop reporting. |
| `bl_touch_take` | `int touch_take(void)` | 1.6 | Next pin that was touched, or -1. |
| `bl_i2s_begin` | `int i2s_begin(int mode, uint32_t rate, int bits, int stereo, int bclk, int ws, int dout, int din, int mclk)` | 1.6 | Start I2S (BL_I2S_*). PDM uses bclk as clock. -1 = unused pin. |
| `bl_i2s_end` | `void i2s_end(void)` | 1.6 | Stop I2S. |
| `bl_i2s_write` | `int i2s_write(const uint8_t *data, size_t len)` | 1.6 | Queue samples. Returns bytes written. |
| `bl_i2s_read` | `int i2s_read(uint8_t *buf, size_t size)` | 1.6 | Read samples. Returns bytes read. |
| `bl_led_strip_write` | `int led_strip_write(int pin, int chipset, const uint8_t *data, size_t len)` | 1.6 | Send bytes (already in the strip's color order) to a WS2812-type strip (BL_STRIP_*). |
| `bl_ledc_attach` | `int ledc_attach(int pin, uint32_t freq, int resolution)` | 1.6 | PWM at any frequency/resolution (servos, tones). 1 or 0. |
| `bl_ledc_write` | `int ledc_write(int pin, uint32_t duty)` | 1.6 | Set the duty. 1 or 0. |
| `bl_ledc_detach` | `int ledc_detach(int pin)` | 1.6 | Stop PWM. 1 or 0. |
| `bl_ledc_write_tone` | `uint32_t ledc_write_tone(int pin, uint32_t freq)` | 1.6 | Square wave at freq (0 = off). Returns the frequency set. |
| `bl_ledc_change_frequency` | `uint32_t ledc_change_frequency(int pin, uint32_t freq, int resolution)` | 1.6 | Change frequency. Returns the frequency set. |
| `bl_dns_start` | `int dns_start(uint16_t port, const char *domain, uint32_t ip)` | 1.6 | Answer DNS queries for domain ("*" = all) with ip. |
| `bl_dns_stop` | `void dns_stop(void)` | 1.6 | Stop it. |
| `bl_dns_error_reply` | `void dns_error_reply(int code)` | 1.6 | Reply code for other names. |
| `bl_usb_begin` | `int usb_begin(uint32_t devices)` | 1.6 | Run as these USB devices (BL_USB_* bits) next to the serial console. Returns BL_OK if they are running; otherwise Baseline saves the set and reboots into it (the sketch starts again). |
| `bl_usb_end` | `void usb_end(void)` | 1.6 | Back to the plain USB serial port (reboots if needed). |
| `bl_usb_devices` | `uint32_t usb_devices(void)` | 1.6 | Devices that are running. |
| `bl_usb_mounted` | `int usb_mounted(void)` | 1.6 | 1 when a computer has set up the device. |
| `bl_usb_keyboard` | `int usb_keyboard(int op, uint32_t key)` | 1.6 | BL_USB_PRESS / RELEASE / RELEASE_ALL / WRITE with an Arduino key code. Returns 1 or 0. |
| `bl_usb_mouse` | `int usb_mouse(int op, int x, int y, int wheel, int buttons)` | 1.6 | BL_USB_MOVE (x, y, wheel), PRESS / RELEASE / CLICK (buttons). |
| `bl_usb_gamepad` | `int usb_gamepad(int x, int y, int z, int rz, int rx, int ry, int hat, uint32_t buttons)` | 1.6 | Send the full gamepad state. Returns 1 or 0. |
| `bl_usb_consumer` | `int usb_consumer(int op, uint16_t usage)` | 1.6 | Media key: BL_USB_PRESS (usage) or BL_USB_RELEASE. |
| `bl_usb_midi_write` | `int usb_midi_write(const uint8_t *packet)` | 1.6 | Send one 4-byte USB-MIDI event packet. |
| `bl_usb_midi_read` | `int usb_midi_read(uint8_t *packet)` | 1.6 | Take one received 4-byte packet. Returns 1, or 0 if none. |
| `ps_malloc` | `void * ps_malloc(size_t size)` | 1.6 | Allocate from PSRAM only. NULL if the board has no PSRAM or it is full. Free with free(). |
| `ps_calloc` | `void * ps_calloc(size_t n, size_t size)` | 1.6 | Allocate zeroed memory from PSRAM only. |
| `ps_realloc` | `void * ps_realloc(void *ptr, size_t size)` | 1.6 | Resize an allocation; the result is in PSRAM. |
| `bl_i2cx_begin` | `int i2cx_begin(int bus, int sda, int scl, uint32_t freq)` | 1.6 | Start I2C bus 0 (Wire) or 1 (Wire1). -1 pins = board defaults (bus 0 only). BL_ERR_UNSUPPORTED if the chip has no such bus. |
| `bl_i2cx_end` | `void i2cx_end(int bus)` | 1.6 | Stop an I2C bus. |
| `bl_i2cx_write` | `int i2cx_write(int bus, uint8_t addr, const uint8_t *data, size_t len, bool stop)` | 1.6 | i2c_write on a given bus. |
| `bl_i2cx_read` | `int i2cx_read(int bus, uint8_t addr, uint8_t *data, size_t len)` | 1.6 | i2c_read on a given bus. |
| `bl_uart_set_rx_buffer` | `int uart_set_rx_buffer(int port, size_t size)` | 1.6 | Receive buffer size in bytes (default 256). Call before uart_begin. |
| `bl_uart_available_for_write` | `int uart_available_for_write(int port)` | 1.6 | Bytes that can be written without waiting. |
| `analogReadResolution` | `int adc_set_resolution(int bits)` | 1.6 | analogRead() result width, 9-12 bits (default 12). |
| `bl_adc_set_attenuation` | `int adc_set_attenuation(int pin, int atten)` | 1.6 | Input range: atten = BL_ADC_ATTEN_* (0, 2.5, 6, 11 dB). pin -1 = every pin. |
| `dacWrite` | `int dac_write(int pin, uint8_t value)` | 1.6 | 8-bit DAC output (classic ESP32: GPIO 25, 26). BL_ERR_UNSUPPORTED on chips without a DAC. |
| `temperatureRead` | `float temperature_read(void)` | 1.6 | Chip temperature in degrees C; NaN if the chip has no sensor. |
| `bl_reset_reason` | `int reset_reason(void)` | 1.6 | Why the chip last reset (esp_reset_reason_t values: 1 power-on, 3 software, 4 panic, 5-7 watchdogs, 8 deep sleep, ...). |
| `bl_last_crash` | `int last_crash(char *buf, size_t size)` | 1.6 | One-line summary of the last crash from the coredump (task, cause, PC). Returns its length, or 0 if there is none. |
| `bl_wifi_event_take` | `int wifi_event_take(void)` | 1.6 | Next WiFi/network event (Arduino-ESP32 3.3 arduino_event_id_t values) for WiFi.onEvent, or -1. |
| `bl_baseline_update_mode` | `int baseline_update_mode(void)` | 1.7 | How Baseline updates over the air: BL_UPDATE_DUAL (two Baseline slots; sketch and files kept), BL_UPDATE_STAGED (4 MB flash; each update erases the sketch and LittleFS) or BL_UPDATE_NONE. |
| `bl_ota_pull_sketch` | `int ota_pull_sketch(const char *url)` | 1.7 | Download a sketch (.flick) over HTTP(S), install it in the free slot and restart into it (it rolls back if it crashes). Returns only on failure. |
| `bl_ota_pull_baseline` | `int ota_pull_baseline(const char *url, const char *sketch_url)` | 1.7 | Download a Baseline update (.bsu) over HTTP(S), install it and restart. sketch_url (may be NULL): a sketch to install once the new Baseline runs (on BL_UPDATE_STAGED boards the update erases the sketch). Returns only on failure. |
| `bl_isr_attach_pin` | `int isr_attach_pin(int pin, int mode, bl_callback_t fn, void *arg)` | 1.8 | Call fn(arg) from the pin's interrupt. mode: BL_RISING, BL_FALLING, BL_CHANGE, BL_ONLOW, BL_ONHIGH. |
| `bl_isr_detach_pin` | `int isr_detach_pin(int pin)` | 1.8 | Stop calling it. |
| `bl_isr_timer_begin` | `int isr_timer_begin(bl_callback_t fn, void *arg, float period_us)` | 1.8 | Periodic timer (IntervalTimer): call fn(arg) every period_us from its interrupt. Returns a handle or BL_ERR_* (BL_ERR_NO_MEM: all timers in use). |
| `bl_isr_timer_update` | `int isr_timer_update(int h, float period_us)` | 1.8 | New period, from the next interval on. |
| `bl_isr_timer_end` | `void isr_timer_end(int h)` | 1.8 | Stop the timer and free it. |
| `bl_isr_timer_priority` | `int isr_timer_priority(int h, int priority)` | 1.8 | Interrupt priority, 0 (highest) to 255; the default is 128. |
| `bl_dma_channel_alloc` | `int dma_channel_alloc(void)` | 1.8 | Reserve a DMA channel for the sketch (i.MX RT eDMA, 0-31) and turn the DMA engine on. Returns the channel or BL_ERR_NO_MEM. |
| `bl_dma_channel_free` | `void dma_channel_free(int channel)` | 1.8 | Give the channel back. |
| `bl_audio_memory` | `int audio_memory(int blocks)` | 1.8 | AudioMemory(): blocks of 128 samples for the audio graph. The first call allocates; later calls return BL_OK if no more are asked for, else BL_ERR_STATE. |
| `bl_audio_create` | `int audio_create(int type, const bl_audio_arg_t *args, int nargs)` | 1.8 | New audio object of type BL_AUDIO_T_* (constructor arguments in args). Returns a handle or BL_ERR_*. |
| `bl_audio_connect` | `int audio_connect(int src, int src_port, int dst, int dst_port)` | 1.8 | AudioConnection from output src_port of src to input dst_port of dst. Returns a connection handle or BL_ERR_*. |
| `bl_audio_disconnect` | `int audio_disconnect(int connection)` | 1.8 | Remove a connection (it can be made again with audio_reconnect). |
| `bl_audio_reconnect` | `int audio_reconnect(int connection)` | 1.8 | Make a removed connection again. |
| `bl_audio_call` | `int audio_call(int h, int method, const bl_audio_arg_t *args, int nargs, bl_audio_arg_t *ret)` | 1.8 | Call method BL_AUDIO_M_* on object h; ret (may be NULL) gets the result. Returns BL_OK or BL_ERR_*. |
| `bl_audio_stat` | `float audio_stat(int what)` | 1.8 | BL_AUDIO_STAT_*: processor use in % and its maximum, audio blocks in use and the maximum; the *_RESET values reset a maximum and return 0. |
| `bl_audio_interrupts` | `void audio_interrupts(int enable)` | 1.8 | AudioNoInterrupts() (0) and AudioInterrupts() (1): change several objects at once, between audio updates. |
| `bl_usbh_begin` | `int usbh_begin(void)` | 1.9 | Start the USB host port (USBHost::begin): power it and enumerate what is plugged in, hubs included. Baseline services it from then on. |
| `bl_usbh_info` | `int usbh_info(int kind, int index, bl_usbh_info_t *info)` | 1.9 | Whether device index of BL_USBH_* kind is connected, and its IDs and strings. BL_OK or BL_ERR_ARG. |
| `bl_usbh_key_take` | `int usbh_key_take(int index, bl_usbh_key_t *ev)` | 1.9 | The next keyboard event (presses, releases, raw codes), queued in the USB interrupt. 1 if one was taken, 0 if none. |
| `bl_usbh_keyboard_leds` | `int usbh_keyboard_leds(int index, int leds)` | 1.9 | Set the keyboard's LEDs (bit 0 Num Lock, 1 Caps Lock, 2 Scroll Lock); -1 only reads them. Returns the LEDs. |
| `bl_usbh_mouse_take` | `int usbh_mouse_take(int index, bl_usbh_mouse_t *m)` | 1.9 | 1 and the mouse's report if it sent one since the last call (mouseDataClear), else 0. |
| `bl_usbh_joystick_take` | `int usbh_joystick_take(int index, bl_usbh_joystick_t *j)` | 1.9 | 1 and the joystick's state if it changed since the last call (joystickDataClear), else 0. |
| `bl_usbh_midi_read` | `int usbh_midi_read(int index, uint8_t *msg)` | 1.9 | One MIDI message: msg[0] type, [1] channel, [2] data1, [3] data2, [4] cable. 1 if one was read, else 0. |
| `bl_usbh_midi_send` | `int usbh_midi_send(int index, uint8_t type, uint8_t data1, uint8_t data2, uint8_t channel, uint8_t cable)` | 1.9 | Send one MIDI message (MIDIDevice::send). |
| `bl_usbh_serial_begin` | `int usbh_serial_begin(int index, uint32_t baud, uint32_t format)` | 1.9 | Open a USB-serial adapter (FTDI, CP210x, CH34x, PL2303, CDC ACM) at baud; format = USBHOST_SERIAL_* (0 = 8N1). |
| `bl_usbh_serial_available` | `int usbh_serial_available(int index)` | 1.9 | Bytes waiting from the adapter, or -1 if it isn't connected. |
| `bl_usbh_serial_read` | `int usbh_serial_read(int index, uint8_t *buf, size_t len)` | 1.9 | Read up to len waiting bytes. Returns how many. |
| `bl_usbh_serial_write` | `int usbh_serial_write(int index, const uint8_t *buf, size_t len)` | 1.9 | Send len bytes. Returns how many were queued. |
| `bl_eth_begin` | `int eth_begin(const uint8_t *mac, uint32_t ip, uint32_t dns, uint32_t gateway, uint32_t subnet, uint32_t timeout_ms)` | 1.9 | Start Ethernet. mac NULL = the board's own; ip 0 = DHCP (waits up to timeout_ms), else these addresses (0 = derived). Returns 1 when it has an address, else 0. |
| `bl_eth_status` | `int eth_status(int what)` | 1.9 | BL_ETH_LINK: 1 cable connected, 0 not, -1 unknown. BL_ETH_HARDWARE: 1 if the board has the PHY, else 0. |
| `bl_eth_ip` | `uint32_t eth_ip(int which)` | 1.9 | IPv4 address, first octet in the low byte. which: BL_IP_STA (local), BL_IP_GATEWAY, BL_IP_SUBNET, BL_IP_DNS. |
| `bl_eth_mac` | `int eth_mac(uint8_t *mac)` | 1.9 | The MAC address in use (6 bytes). |
| `bl_eth_maintain` | `int eth_maintain(void)` | 1.9 | Renew the DHCP lease if due (Ethernet.maintain(): 0 nothing, 1 renew failed, 2 renewed, 3 rebind failed, 4 rebound). |
| `bl_task_create` | `void * task_create(bl_callback_t fn, const char *name, uint32_t stack_bytes, void *arg, int priority, int core)` | 1.10 | Start a task running fn(arg) (xTaskCreatePinnedToCore). stack_bytes is raised to at least 3072; priority is capped at BL_TASK_MAX_PRIORITY (10); core 0 or 1, or -1 for either. A task whose fn returns is deleted. Returns the task, or NULL. |
| `bl_task_delete` | `void task_delete(void *task)` | 1.10 | Delete a task; NULL = the calling task (then it doesn't return). Deleting the sketch's own task (vTaskDelete(NULL) at the end of setup()) ends loop() and leaves the sketch's other tasks running. |
| `bl_task_current` | `void * task_current(void)` | 1.10 | The calling task. |
| `bl_task_name` | `const char * task_name(void *task)` | 1.10 | A task's name; NULL = the calling task. |
| `bl_task_stack_free` | `uint32_t task_stack_free(void *task)` | 1.10 | The least free stack the task has had, in bytes (uxTaskGetStackHighWaterMark); NULL = the calling task. |
| `bl_task_priority` | `int task_priority(void *task, int priority)` | 1.10 | Set a task's priority (capped at BL_TASK_MAX_PRIORITY); -1 only reads it. NULL = the calling task. Returns the priority. |
| `bl_task_suspend` | `void task_suspend(void *task, int suspend)` | 1.10 | vTaskSuspend (suspend 1) or vTaskResume (0). NULL = the calling task. |
| `bl_task_info` | `int task_info(int what)` | 1.10 | BL_TASK_INFO_CORE: the core the caller runs on (xPortGetCoreID). BL_TASK_INFO_COUNT: how many tasks exist, Baseline's included. BL_TASK_INFO_CORES: the chip's cores. |
| `bl_sem_create` | `void * sem_create(int type, uint32_t max, uint32_t initial)` | 1.10 | New semaphore: BL_SEM_MUTEX, BL_SEM_RECURSIVE (recursive mutex), BL_SEM_BINARY (starts empty) or BL_SEM_COUNTING (max and initial count). Returns it, or NULL. |
| `bl_sem_take` | `int sem_take(void *sem, uint32_t ticks, int recursive)` | 1.10 | Take it, waiting up to ticks. recursive 1 for a BL_SEM_RECURSIVE mutex. 1 taken, 0 timed out. |
| `bl_sem_give` | `int sem_give(void *sem, int recursive)` | 1.10 | Give it back. 1, or 0 if it couldn't be given (not the holder, or already full). |
| `bl_sem_count` | `int sem_count(void *sem)` | 1.10 | The count (uxSemaphoreGetCount): for a mutex, 1 free, 0 taken. |
| `bl_sem_delete` | `void sem_delete(void *sem)` | 1.10 | Delete it. |
| `bl_queue_create` | `void * queue_create(uint32_t length, uint32_t item_size)` | 1.10 | New queue of length items of item_size bytes, copied in and out. Returns it, or NULL. |
| `bl_queue_send` | `int queue_send(void *queue, const void *item, uint32_t ticks, int where)` | 1.10 | Copy item in: where = BL_QUEUE_BACK, BL_QUEUE_FRONT or BL_QUEUE_OVERWRITE (length-1 queues). Waits up to ticks for room. 1 sent, 0 full. |
| `bl_queue_receive` | `int queue_receive(void *queue, void *item, uint32_t ticks, int peek)` | 1.10 | Copy the first item out (and remove it, unless peek is 1), waiting up to ticks for one. 1 received, 0 empty. |
| `bl_queue_count` | `int queue_count(void *queue, int spaces)` | 1.10 | Items waiting (spaces 0) or free places (spaces 1). |
| `bl_queue_reset` | `int queue_reset(void *queue)` | 1.10 | Empty it. Returns 1. |
| `bl_queue_delete` | `void queue_delete(void *queue)` | 1.10 | Delete it. |
| `heap_caps_malloc` | `void * idf_heap_malloc(size_t size, uint32_t caps)` | 1.10 | heap_caps_malloc: from a heap with all of caps (MALLOC_CAP_*, ESP-IDF values). Free with free(). |
| `heap_caps_calloc` | `void * idf_heap_calloc(size_t n, size_t size, uint32_t caps)` | 1.10 | heap_caps_calloc. |
| `heap_caps_realloc` | `void * idf_heap_realloc(void *ptr, size_t size, uint32_t caps)` | 1.10 | heap_caps_realloc. |
| `bl_idf_heap_info` | `size_t idf_heap_info(uint32_t caps, int what)` | 1.10 | Size of the heaps with all of caps: what = BL_HEAP_FREE, BL_HEAP_LARGEST (largest free block), BL_HEAP_MIN_FREE or BL_HEAP_TOTAL. |
| `bl_idf_heap_check` | `int idf_heap_check(uint32_t caps, int print)` | 1.10 | heap_caps_check_integrity (caps MALLOC_CAP_INVALID: every heap). print 1 prints what is wrong. 1 intact, 0 corrupt. |
| `heap_caps_malloc_extmem_enable` | `void idf_heap_extmem(size_t limit)` | 1.10 | malloc() of limit bytes or more goes to PSRAM first (Baseline starts with 4096). For the whole board, Baseline too. |
| `esp_read_mac` | `int idf_read_mac(uint8_t *mac, int type)` | 1.10 | esp_read_mac: the 6-byte MAC address of type (ESP_MAC_WIFI_STA, ESP_MAC_WIFI_SOFTAP, ESP_MAC_BT, ESP_MAC_ETH, ...). Returns an esp_err_t. |
| `esp_err_to_name` | `const char * idf_err_name(int err)` | 1.10 | esp_err_to_name: an esp_err_t's name ("ESP_ERR_NO_MEM"). |
| `bl_idf_partition` | `int idf_partition(int index, bl_partition_t *out)` | 1.10 | The index-th entry of the flash partition table (backs esp_partition_find). 0, or BL_ERR_ARG past the last one. |
| `bl_idf_task_wdt` | `int idf_task_wdt(int op, void *task)` | 1.10 | The ESP-IDF task watchdog: op = BL_TWDT_ADD, BL_TWDT_DELETE, BL_TWDT_STATUS or BL_TWDT_RESET; task NULL = the calling task. Returns an esp_err_t. |
| `bl_idf_tls_check` | `int idf_tls_check(int what, const uint8_t *data, size_t len)` | 1.10 | Parse with Baseline's mbedTLS: what = BL_TLS_CERT (mbedtls_x509_crt_parse) or BL_TLS_KEY (mbedtls_pk_parse_key). 0, or the negative mbedTLS error. |
| `bl_uart_set_rx_fifo_full` | `int uart_set_rx_fifo_full(int port, uint8_t bytes)` | 1.10 | Serial1/2 setRxFIFOFull: move received bytes out of the hardware FIFO every bytes bytes (1-127). Returns 0 or BL_ERR_*. |
| `bl_update_begin` | `int update_begin(uint32_t size)` | 1.10 | Start an update of size bytes (0 = not known). Returns 0 or BL_ERR_*. |
| `bl_update_write` | `int update_write(const uint8_t *data, size_t len)` | 1.10 | The next bytes of the image. Returns len, or BL_ERR_* (the update is then abandoned). |
| `bl_update_end` | `int update_end(int commit)` | 1.10 | commit 1: check the image and start it at the next restart. commit 0: abandon it. Returns 0 or BL_ERR_*. |
| `bl_update_error` | `int update_error(char *buf, size_t size)` | 1.10 | Why the last update call failed, as text. Returns its length (0: no error). |
| `bl_vsscanf` | `int vsscanf(const char *str, const char *fmt, va_list ap)` | 1.10 | vsscanf with Baseline's C library (%d %u %x %f %s %c %[...] %n ...). Returns the number of fields assigned, or EOF. ESP32 and RP2 Baselines; BL_ERR_UNSUPPORTED on Teensy (no room for scanf). |
| `bl_net_if_get` | `int net_if_get(int iface, int what, void *out, size_t size)` | 1.10 | Read one property of an interface: flags return 0/1; IPv4 addresses are written to out as a uint32_t (first octet in the low byte); MAC as 6 bytes; text NUL-terminated. Returns the value, the length written, or BL_ERR_*. iface -1 with BL_NETIF_Q_DEFAULT returns the default interface's BL_NETIF_*. |
| `bl_net_if_set` | `int net_if_set(int iface, int what, const void *value, size_t size)` | 1.10 | Change one property: BL_NETIF_S_HOSTNAME (text), BL_NETIF_S_DEFAULT (make it the default route). 0 or BL_ERR_*. |
| `bl_net_event_name` | `const char * net_event_name(int event)` | 1.10 | Network.eventName(): the name of an ARDUINO_EVENT_* (text in Baseline's flash). |
| `bl_wifi_event_take_info` | `int wifi_event_take_info(void *info, size_t size)` | 1.10 | Like wifi_event_take, and copies up to size bytes of the event's arduino_event_info_t (ESP-IDF layout) into info. |
| `bl_wifi_sta_connect` | `int wifi_sta_connect(const char *ssid, const char *password, int channel)` | 1.10 | WiFi.STA.connect(): start connecting (channel 0 = any). Returns 1 if started. |
| `bl_wifi_ap_config` | `int wifi_ap_config(uint32_t ip, uint32_t gateway, uint32_t subnet, uint32_t dhcp_start, uint32_t dns)` | 1.10 | WiFi.AP.config(): the access point's addresses (dhcp_start/dns 0 = default). 0 or BL_ERR_*. |
| `bl_wifi_set_channel` | `int wifi_set_channel(int primary, int secondary)` | 1.10 | esp_wifi_set_channel (secondary: 0 none, 1 above, 2 below). 0 or BL_ERR_*. |
| `bl_wifi_get_channel` | `int wifi_get_channel(int *secondary)` | 1.10 | esp_wifi_get_channel: returns the primary channel, writes the secondary. |
| `bl_wifi_set_long_range` | `int wifi_set_long_range(int iface, int on)` | 1.10 | WiFi.setLongRange(): add (on) or remove the ESP32 long range protocol (WIFI_PROTOCOL_LR) on BL_NETIF_STA or BL_NETIF_AP, keeping 802.11b/g/n. 0 or BL_ERR_*. |
| `bl_wifi_scan_ex` | `int wifi_scan_ex(int show_hidden, int passive, uint32_t max_ms_per_chan, int channel, const char *ssid, const uint8_t *bssid)` | 1.10 | scanNetworks() with Arduino-ESP32's options: hidden networks, passive scan, time per channel (0 = default), one channel (0 = all), one SSID or BSSID (NULL = any). Blocks; returns the count or BL_ERR_*. |
| `bl_espnow_receive_info` | `int espnow_receive_info(bl_espnow_rx_t *info, uint8_t *data, size_t size)` | 1.10 | Like espnow_receive, with the destination MAC, RSSI and channel of the packet. Returns length or 0. |
| `bl_tcp_client_local` | `int tcp_client_local(int h, uint32_t *ip, uint16_t *port)` | 1.10 | Our end of the connection (localIP/localPort). |
| `bl_tcp_client_tls_identity` | `int tcp_client_tls_identity(int h, const char *cert_pem, const char *key_pem)` | 1.10 | TLS client certificate and private key (setCertificate/setPrivateKey). Must stay valid. 0 or BL_ERR_*. |
| `bl_tcp_client_tls_timeout` | `int tcp_client_tls_timeout(int h, uint32_t seconds)` | 1.10 | TLS handshake timeout (setHandshakeTimeout). |
| `bl_tcp_client_last_error` | `int tcp_client_last_error(int h, char *buf, size_t size)` | 1.10 | The last TLS error: its code (0 none), and its text in buf. |
| `bl_uart_set_baud` | `int uart_set_baud(int port, uint32_t baud)` | 1.10 | Serial1/2 updateBaudRate: change the baud rate of a started port. 0 or BL_ERR_*. |
| `bl_bt_spp_config` | `int bt_spp_config(size_t rx_size, size_t tx_size)` | 1.10 | Buffer sizes in bytes: receive (0 = 512) and send (0 = none: write() waits on the Bluetooth stack's own 32-packet queue, as in Arduino-ESP32). In PSRAM when there is some. Call before bt_spp_begin. 0 or BL_ERR_*. |
| `bl_bt_spp_ssp` | `int bt_spp_ssp(int enable, int input, int output)` | 1.10 | Secure Simple Pairing (enableSSP): input/output capabilities. Call before bt_spp_begin. |
| `bl_bt_spp_confirm` | `int bt_spp_confirm(int accept)` | 1.10 | Answer a pairing confirmation request (confirmReply). |
| `bl_bt_spp_disconnect` | `int bt_spp_disconnect(void)` | 1.10 | Drop the connected device. 1 if one was dropped. |
| `bl_bt_spp_unpair_all` | `int bt_spp_unpair_all(void)` | 1.10 | Forget every bonded device (deleteAllBondedDevices; after begin). |
| `bl_ext_find` | `void * ext_find(const char *name)` | 1.10 | What a Baseline extension registered under name (BL_EXTENSION in baseline_extension.h), or NULL when this Baseline was built without it. |
