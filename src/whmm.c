
// #include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"
/* Set verbose logging only for this file to avoid global redefinition warnings */
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include <esp_log.h>   // Add this line to include the header file that declares ESP_LOGI
#include <esp_flash.h> // Add this line to include the header file that declares esp_flash_t
#include <esp_chip_info.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>
/* SD card support */
#include <dirent.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sys/stat.h"  /* mkdir */

/* Wi-Fi + HTTP provisioning */
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include <ctype.h>
#include <strings.h>

static const char *TAG = "WHMM";

#define BUILD (String(__DATE__) + " - " + String(__TIME__)).c_str()

#define logSection(section) \
  ESP_LOGI(TAG, "\n\n************* %s **************\n", section);

/**
 * @brief WHMM - Warhammer Math Manager
 * Main application for tracking victory points, command points, and game statistics
 * Set the rotation degree:
 *      - 0: 0 degree
 *      - 90: 90 degree
 *      - 180: 180 degree
 *      - 270: 270 degree
 *
 */
#define LVGL_PORT_ROTATION_DEGREE (90)

/**
 * Warhammer Victory Point Counter
 */

void setup();
void create_hello_world_ui();
static void whmm_touch_cb(lv_event_t * e);
#if 0 // temporarily disabled: not used in current home screen
static void whmm_list_sd_under_box(lv_obj_t *anchor_box);
#endif
static esp_err_t whmm_mount_sd(void);
static void whmm_provision_check_and_start(lv_obj_t *anchor_box);
static esp_err_t whmm_wifi_start_sta_from_sd(void);
static esp_err_t whmm_wifi_start_ap(const char *ssid, const char *pass);
static httpd_handle_t whmm_http_start(void);
static void whmm_render_qr_setup(lv_obj_t *anchor_box, const char *ssid, const char *pass);
static esp_err_t whmm_http_send_file(httpd_req_t *req, const char *path);
static const char* whmm_guess_content_type(const char *path);
static esp_err_t static_get_handler(httpd_req_t *req);
static void url_decode_inplace(char *s);
static bool whmm_ensure_wifi_dir(void);
static bool whmm_load_wifi_json(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz);
static bool whmm_save_wifi_json(const char *ssid, const char *pass);

/* SD config (from Arduino demos: CLK=12, CMD=11, D0=13) */
#define WHMM_SD_CLK   GPIO_NUM_12
#define WHMM_SD_CMD   GPIO_NUM_11
#define WHMM_SD_D0    GPIO_NUM_13
#define WHMM_SD_MOUNT_POINT "/sdcard"

static bool s_sd_mounted = false;

#if !CONFIG_AUTOSTART_ARDUINO
void app_main()
{
  setup();
}
#endif
void setup()
{
  //  String title = "LVGL porting example";

  // Serial.begin(115200);
  logSection("WHMM - Warhammer Math Manager start");
  esp_chip_info_t chip_info;
  uint32_t flash_size;
  esp_chip_info(&chip_info);
  ESP_LOGI(TAG, "This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

  unsigned major_rev = chip_info.revision / 100;
  unsigned minor_rev = chip_info.revision % 100;
  ESP_LOGI(TAG, "silicon revision v%d.%d, ", major_rev, minor_rev);
  if (esp_flash_get_size(NULL, &flash_size) != ESP_OK)
  {
    ESP_LOGI(TAG, "Get flash size failed");
    return;
  }

  ESP_LOGI(TAG, "%" PRIu32 "MB %s flash", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

  ESP_LOGI(TAG, "Minimum free heap size: %" PRIu32 " bytes", esp_get_minimum_free_heap_size());
  size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  ESP_LOGI(TAG, "Free PSRAM: %d bytes", freePsram);
  logSection("Initialize panel device");
  // ESP_LOGI(TAG, "Initialize panel device");
  bsp_display_cfg_t cfg = {
      .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
      .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
#if LVGL_PORT_ROTATION_DEGREE == 90
      .rotate = LV_DISP_ROT_90,
#elif LVGL_PORT_ROTATION_DEGREE == 270
      .rotate = LV_DISP_ROT_270,
#elif LVGL_PORT_ROTATION_DEGREE == 180
      .rotate = LV_DISP_ROT_180,
#elif LVGL_PORT_ROTATION_DEGREE == 0
      .rotate = LV_DISP_ROT_NONE,
#endif
  };

  bsp_display_start_with_config(&cfg);
  bsp_display_backlight_on();

  logSection("Create Home Screen");
  /* Lock the mutex due to the LVGL APIs are not thread-safe */
  bsp_display_lock(0);

  /* Create our Home Screen (QR for Wi‑Fi provisioning) */
  create_hello_world_ui();

  /* Release the mutex */
  bsp_display_unlock();

  logSection("WHMM - Warhammer Math Manager ready");
}

void loop()
{
  ESP_LOGI(TAG, "IDLE loop");
  // delay(1000);
}

/**
 * Create a simple Home Screen
 * Displays provisioning QR codes and info, centered on screen
 */
void create_hello_world_ui()
{
  /* Get the active screen */
  lv_obj_t *scr = lv_scr_act();
  
  /* Set screen background to black */
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  /* Title */
  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "WHMM Setup\nConnect or Provision Wi-Fi");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  /* Attach touch logging to the whole screen */
  lv_obj_add_event_cb(scr, whmm_touch_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(scr, whmm_touch_cb, LV_EVENT_RELEASED, NULL);
  // Optional: uncomment to see continuous coordinates while moving
  // lv_obj_add_event_cb(scr, whmm_touch_cb, LV_EVENT_PRESSING, NULL);

  /* Mount SD and show Wi‑Fi provisioning UI (QR codes) */
  if (whmm_mount_sd() == ESP_OK) {
    whmm_provision_check_and_start(scr);
  } else {
    ESP_LOGW(TAG, "SD card mount failed; skipping listing");
    /* Show a small note about failure */
    lv_obj_t *note = lv_label_create(scr);
    lv_label_set_text(note, "SD card not mounted");
    lv_obj_set_style_text_color(note, lv_color_hex(0xFF8080), 0);
    lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -8);
  }
  
  ESP_LOGI(TAG, "Home Screen created successfully");
}

/* Touch event logger: prints coordinates to terminal */
static void whmm_touch_cb(lv_event_t * e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t * indev = lv_indev_get_act();
  if (!indev) {
    return;
  }

  lv_point_t p;
  lv_indev_get_point(indev, &p);

  switch (code) {
    case LV_EVENT_PRESSED:
      ESP_LOGI(TAG, "Touch DOWN at (%d, %d)", p.x, p.y);
      break;
    case LV_EVENT_RELEASED:
      ESP_LOGI(TAG, "Touch UP at (%d, %d)", p.x, p.y);
      break;
    case LV_EVENT_PRESSING:
      ESP_LOGI(TAG, "Touch MOVE at (%d, %d)", p.x, p.y);
      break;
    default:
      break;
  }
}

/* Mount SD card using SDMMC 1-bit on custom pins */
static esp_err_t whmm_mount_sd(void)
{
  if (s_sd_mounted) return ESP_OK;

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 8,
    .allocation_unit_size = 16 * 1024,
    .disk_status_check_enable = false
  };

  sdmmc_card_t *card;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT; /* Board is wired for 1-bit */
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;
  host.slot = SDMMC_HOST_SLOT_1; /* Explicitly use slot 1 (GPIO matrix) */

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.clk = WHMM_SD_CLK;
  slot_config.cmd = WHMM_SD_CMD;
  slot_config.d0  = WHMM_SD_D0;
  slot_config.width = 1;
  /* Rely on external pull-ups if present; enable internal as fallback */
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  esp_err_t err = esp_vfs_fat_sdmmc_mount(WHMM_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(err));
    return err;
  }

  s_sd_mounted = true;
  sdmmc_card_print_info(stdout, card);
  ESP_LOGI(TAG, "SD mounted at %s", WHMM_SD_MOUNT_POINT);
  return ESP_OK;
}

/* Create a label below the box and populate with top-level directory list */
#if 0 // temporarily disabled: not used in current home screen
static void whmm_list_sd_under_box(lv_obj_t *anchor_box)
{
  /* Create a readable panel under the box */
  lv_obj_t *scr = lv_obj_get_parent(anchor_box);
  lv_obj_t *panel = lv_obj_create(scr);
  lv_obj_set_width(panel, lv_pct(95));
  lv_obj_set_height(panel, LV_SIZE_CONTENT);
  lv_obj_align_to(panel, anchor_box, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x404040), 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, 6, 0);
  lv_obj_set_style_pad_all(panel, 6, 0);

  lv_obj_t *list_label = lv_label_create(panel);
  lv_obj_set_width(list_label, lv_pct(100));
  lv_label_set_long_mode(list_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(list_label, lv_color_white(), 0);

  char buf[1024];
  size_t used = 0;
  used += snprintf(buf + used, sizeof(buf) - used, "SD: %s\n", WHMM_SD_MOUNT_POINT);

  DIR *dir = opendir(WHMM_SD_MOUNT_POINT);
  if (!dir) {
    lv_label_set_text(list_label, "SD open failed");
    return;
  }

  struct dirent *ent;
  int count = 0;
  while ((ent = readdir(dir)) != NULL) {
    const char *name = ent->d_name;
    if (name[0] == '.') continue; /* skip hidden/./.. */
    const char *type = (ent->d_type == DT_DIR) ? "[DIR]" : "     ";
    used += snprintf(buf + used, sizeof(buf) - used, "%-5s %s\n", type, name);
    if (++count >= 20 || used >= sizeof(buf) - 32) break; /* cap output */
  }
  closedir(dir);

  if (count == 0) {
    used += snprintf(buf + used, sizeof(buf) - used, "<empty>\n");
  }

  lv_label_set_text(list_label, buf);
}
#endif

/* ===== Wi‑Fi provisioning helpers ===== */

#define WHMM_WIFI_DIR        "/sdcard/wifi"
#define WHMM_WIFI_JSON_PATH  "/sdcard/wifi/wifi_settings.json"

/* Basic JSON helpers (minimal, without cJSON for now) */
static bool json_extract_str(const char *json, const char *key, char *out, size_t out_sz)
{
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *p = strstr(json, pattern);
  if (!p) return false;
  p = strchr(p + strlen(pattern), ':');
  if (!p) return false;
  p = strchr(p, '"');
  if (!p) return false;
  const char *q = strchr(p + 1, '"');
  if (!q) return false;
  size_t len = (size_t)(q - (p + 1));
  if (len >= out_sz) len = out_sz - 1;
  memcpy(out, p + 1, len);
  out[len] = '\0';
  return true;
}

static bool whmm_ensure_wifi_dir(void)
{
  DIR *d = opendir(WHMM_WIFI_DIR);
  if (d) { closedir(d); return true; }
  int res = mkdir(WHMM_WIFI_DIR, 0777);
  if (res == 0) return true;
  ESP_LOGE(TAG, "Failed to create %s", WHMM_WIFI_DIR);
  return false;
}

static bool whmm_load_wifi_json(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz)
{
  FILE *f = fopen(WHMM_WIFI_JSON_PATH, "rb");
  if (!f) return false;
  char buf[512];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';
  bool ok1 = json_extract_str(buf, "ssid", ssid, ssid_sz);
  bool ok2 = json_extract_str(buf, "password", pass, pass_sz);
  return ok1 && ok2;
}

static bool whmm_save_wifi_json(const char *ssid, const char *pass)
{
  if (!whmm_ensure_wifi_dir()) return false;
  FILE *f = fopen(WHMM_WIFI_JSON_PATH, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open %s for write", WHMM_WIFI_JSON_PATH);
    return false;
  }
  fprintf(f, "{\n  \"ssid\": \"%s\",\n  \"password\": \"%s\"\n}\n", ssid, pass);
  fclose(f);
  return true;
}

static void whmm_provision_check_and_start(lv_obj_t *anchor_box)
{
  /* Init NVS/Netif/Wi‑Fi once */
  static bool inited = false;
  if (!inited) {
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    inited = true;
  }

  char ssid[64] = {0}, pass[64] = {0};
  if (whmm_load_wifi_json(ssid, sizeof(ssid), pass, sizeof(pass))) {
    ESP_LOGI(TAG, "Found Wi-Fi settings on SD: SSID=%s", ssid);
    if (whmm_wifi_start_sta_from_sd() == ESP_OK) {
      ESP_LOGI(TAG, "Attempting STA connect with saved creds");
      return; /* STA path will continue in background; for first pass we don't show status UI */
    }
  }

  /* No settings or STA failed: start provisioning via AP + HTTP */
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  char ap_ssid[48];
  snprintf(ap_ssid, sizeof(ap_ssid), "WHMM-OmegaClass-SetThisBWordUp-%02X%02X", mac[4], mac[5]);
  const char *ap_pass = "whmm-setup"; /* you approved this */
  if (whmm_wifi_start_ap(ap_ssid, ap_pass) == ESP_OK) {
    httpd_handle_t server = whmm_http_start();
    (void)server;
    whmm_render_qr_setup(anchor_box, ap_ssid, ap_pass);
  }
}

/* Start STA using saved JSON */
static esp_err_t whmm_wifi_start_sta_from_sd(void)
{
  char ssid[64] = {0}, pass[64] = {0};
  if (!whmm_load_wifi_json(ssid, sizeof(ssid), pass, sizeof(pass))) return ESP_FAIL;

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  esp_netif_create_default_wifi_sta();
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  wifi_config_t wcfg = {0};
  strncpy((char*)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid));
  strncpy((char*)wcfg.sta.password, pass, sizeof(wcfg.sta.password));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_connect());
  return ESP_OK;
}

/* Start SoftAP for provisioning */
static esp_err_t whmm_wifi_start_ap(const char *ssid, const char *pass)
{
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  /* Create both AP and STA netifs to allow scanning while AP is up */
  esp_netif_create_default_wifi_ap();
  esp_netif_create_default_wifi_sta();
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  wifi_config_t apcfg = {0};
  strncpy((char*)apcfg.ap.ssid, ssid, sizeof(apcfg.ap.ssid));
  apcfg.ap.ssid_len = strlen(ssid);
  apcfg.ap.channel = 1;
  apcfg.ap.max_connection = 4;
  if (pass && *pass) {
    strncpy((char*)apcfg.ap.password, pass, sizeof(apcfg.ap.password));
    apcfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
  } else {
    apcfg.ap.authmode = WIFI_AUTH_OPEN;
  }
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apcfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "SoftAP started: SSID=%s pass=%s IP=192.168.4.1", ssid, pass ? pass : "");
  return ESP_OK;
}

/* Minimal HTTP server: / (form), /scan (JSON), /save (POST) */
static esp_err_t root_get_handler(httpd_req_t *req)
{
  /* Prefer SD-hosted UI: /sdcard/www/index.html */
  if (s_sd_mounted) {
    struct stat st;
    if (stat("/sdcard/www/index.html", &st) == 0) {
      return whmm_http_send_file(req, "/sdcard/www/index.html");
    }
  }

  const char *html =
    "<!doctype html><html><head><meta name=viewport content=\"width=device-width, initial-scale=1\"><style>body{font-family:sans-serif;margin:16px}label{display:block;margin:8px 0 4px}button{margin-top:12px}</style></head><body>"
    "<h2>WHMM Wi-Fi Setup</h2>"
    "<button onclick=\"fetch('/scan').then(r=>r.json()).then(list=>{let s=document.getElementById('ssid');s.innerHTML='';list.forEach(n=>{let o=document.createElement('option');o.value=n;o.textContent=n;s.appendChild(o);});});\">Scan</button>"
    "<form method=post action=/save>"
    "<label>SSID</label><select id=ssid name=ssid></select>"
    "<label>Password</label><input type=password name=password>"
    "<button type=submit>Save</button></form>"
    "</body></html>";
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
  wifi_scan_config_t sc = {0};
  ESP_LOGI(TAG, "Starting Wi-Fi scan (AP+STA mode expected)...");
  esp_wifi_scan_start(&sc, true);
  uint16_t n = 0;
  esp_wifi_scan_get_ap_num(&n);
  ESP_LOGI(TAG, "Scan complete, found %u APs", (unsigned)n);
  wifi_ap_record_t *recs = calloc(n ? n : 1, sizeof(*recs));
  if (!recs) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
  esp_wifi_scan_get_ap_records(&n, recs);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr_chunk(req, "[");
  for (uint16_t i = 0; i < n; ++i) {
    char item[128];
    snprintf(item, sizeof(item), "%s\"%s\"", (i?",":""), (const char*)recs[i].ssid);
    httpd_resp_sendstr_chunk(req, item);
  }
  httpd_resp_sendstr_chunk(req, "]");
  httpd_resp_sendstr_chunk(req, NULL);
  free(recs);
  return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
  char buf[256]; int received = httpd_req_recv(req, buf, sizeof(buf)-1);
  if (received <= 0) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
  buf[received] = '\0';
  /* crude parsing: ssid=...&password=... */
  char ssid[64] = {0}, pass[64] = {0};
  char *p = strstr(buf, "ssid=");
  if (p) { p += 5; char *e = strchr(p, '&'); size_t len = e? (size_t)(e-p) : strlen(p); if(len>=sizeof(ssid)) len=sizeof(ssid)-1; strncpy(ssid, p, len); ssid[len]='\0'; }
  p = strstr(buf, "password=");
  if (p) { p += 9; size_t len = strlen(p); if(len>=sizeof(pass)) len=sizeof(pass)-1; strncpy(pass, p, len); pass[len]='\0'; }
  /* Full URL decode: handle '+' and %xx */
  url_decode_inplace(ssid);
  url_decode_inplace(pass);

  if (ssid[0]==0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
  bool ok = whmm_save_wifi_json(ssid, pass);
  if (!ok) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save");

  httpd_resp_set_type(req, "text/html");
  httpd_resp_sendstr(req, "<html><body><h3>Saved. Rebooting in 3s...</h3><script>setTimeout(()=>fetch('/reboot'),3000)</script></body></html>");
  return ESP_OK;
}

static esp_err_t reboot_get_handler(httpd_req_t *req)
{
  httpd_resp_sendstr(req, "OK");
  vTaskDelay(pdMS_TO_TICKS(100));
  esp_restart();
  return ESP_OK;
}

static httpd_handle_t whmm_http_start(void)
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  /* Enable wildcard URI matching for static file handler */
  config.uri_match_fn = httpd_uri_match_wildcard;
  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t root = {.uri="/", .method=HTTP_GET, .handler=root_get_handler};
    httpd_register_uri_handler(server, &root);
    httpd_uri_t scan = {.uri="/scan", .method=HTTP_GET, .handler=scan_get_handler};
    httpd_register_uri_handler(server, &scan);
    httpd_uri_t save = {.uri="/save", .method=HTTP_POST, .handler=save_post_handler};
    httpd_register_uri_handler(server, &save);
    httpd_uri_t reboot = {.uri="/reboot", .method=HTTP_GET, .handler=reboot_get_handler};
    httpd_register_uri_handler(server, &reboot);
  // Static files mapping:
  //   /static/...  ->  /sdcard/www/static/...
    httpd_uri_t static_u = {.uri="/static/*", .method=HTTP_GET, .handler=static_get_handler};
    httpd_register_uri_handler(server, &static_u);
    /* Favicon convenience */
    httpd_uri_t favicon = {.uri="/favicon.ico", .method=HTTP_GET, .handler=static_get_handler};
    httpd_register_uri_handler(server, &favicon);
  }
  return server;
}

static void whmm_render_qr_setup(lv_obj_t *anchor_box, const char *ssid, const char *pass)
{
  /* Place content relative to the screen to avoid running off-screen */
  lv_obj_t *scr = lv_obj_get_parent(anchor_box);
  if (!scr) scr = lv_scr_act();

  /* Info text */
  lv_obj_t *info = lv_label_create(scr);
  lv_label_set_text_fmt(info, "Provisioning AP\nSSID: %s\nPass: %s\nURL: http://192.168.4.1", ssid, pass);
  lv_obj_set_style_text_color(info, lv_color_white(), 0);
  lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 40);

  /* Single QR code for the webpage */
  const int qr_size = 140;
  const char *url = "http://192.168.4.1";
  lv_obj_t *qr_url = lv_qrcode_create(scr, qr_size, lv_color_black(), lv_color_white());
  lv_qrcode_update(qr_url, url, strlen(url));
  lv_obj_align(qr_url, LV_ALIGN_CENTER, 0, 20);

  /* Caption under QR */
  lv_obj_t *cap_url = lv_label_create(scr);
  lv_label_set_text(cap_url, "Open in browser");
  lv_obj_set_style_text_color(cap_url, lv_color_white(), 0);
  lv_obj_align_to(cap_url, qr_url, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
}

/* ===== Static file support from SD ===== */
static const char* whmm_guess_content_type(const char *path)
{
  const char *ext = strrchr(path, '.');
  if (!ext) return "text/plain";
  ext++;
  if (!strcasecmp(ext, "html") || !strcasecmp(ext, "htm")) return "text/html";
  if (!strcasecmp(ext, "css")) return "text/css";
  if (!strcasecmp(ext, "js")) return "application/javascript";
  if (!strcasecmp(ext, "json")) return "application/json";
  if (!strcasecmp(ext, "png")) return "image/png";
  if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg")) return "image/jpeg";
  if (!strcasecmp(ext, "svg")) return "image/svg+xml";
  if (!strcasecmp(ext, "ico")) return "image/x-icon";
  return "application/octet-stream";
}

static esp_err_t whmm_http_send_file(httpd_req_t *req, const char *path)
{
  FILE *f = fopen(path, "rb");
  if (!f) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  httpd_resp_set_type(req, whmm_guess_content_type(path));
  char buf[1024];
  int n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) { fclose(f); return ESP_FAIL; }
  }
  fclose(f);
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t static_get_handler(httpd_req_t *req)
{
  char path[512];
  if (strcmp(req->uri, "/favicon.ico") == 0) {
    snprintf(path, sizeof(path), "/sdcard/www/favicon.ico");
  } else {
    const char *uri = req->uri; /* expected /static/... */
    if (strstr(uri, "..")) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    if (snprintf(path, sizeof(path), "/sdcard/www%s", uri) >= (int)sizeof(path)) {
      return httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "path too long");
    }
  }
  return whmm_http_send_file(req, path);
}

/* URL decode in-place: handles + -> space and %XX */
static void url_decode_inplace(char *s)
{
  char *r = s, *w = s;
  while (*r) {
    if (*r == '+') { *w++ = ' '; r++; }
    else if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
      int hi = r[1] <= '9' ? r[1]-'0' : (toupper((unsigned char)r[1])-'A'+10);
      int lo = r[2] <= '9' ? r[2]-'0' : (toupper((unsigned char)r[2])-'A'+10);
      *w++ = (char)((hi<<4) | lo);
      r += 3;
    } else {
      *w++ = *r++;
    }
  }
  *w = '\0';
}