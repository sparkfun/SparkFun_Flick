# Baseline API reference (ABI 1.4)

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
| `rgbLedWrite` | `int rgb_write(int pin, uint8_t red, uint8_t green, uint8_t blue)` | 1.1 | Set a WS2812 RGB LED (Thing Plus: pin 46). Returns 0 or BL_ERR_*. |
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
