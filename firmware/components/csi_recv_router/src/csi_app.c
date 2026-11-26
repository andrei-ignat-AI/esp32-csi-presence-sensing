#include "csi_app.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>    
#include <errno.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "ping/ping_sock.h"
#include "protocol_examples_common.h"
#include "esp_attr.h"

// The modified upstream detector code (who_detect_lcd.cpp) that supplies the people count
// and confidence score is included, commented out, at the bottom of this file.

#define MOUNT_POINT     "/sdcard"
#define CSI_QUEUE_LEN   16        // small queue to save heap memory
#define CSI_MAX_LEN     512       // max csi buffer length to store

static const char *TAG = "csi_recv_router";

// detection metadata, set from who_detect_lcd.cpp via the setter functions below
// marked 'volatile' because they are changed by another task

static volatile int      s_detection_flag   = 0;
static volatile uint64_t s_det_timestamp_us = 0;
static volatile float    s_det_conf         = 0;

// functions for the detection component to update our metadata
// iram_attr helps speed up these calls from the detection callback

IRAM_ATTR void csi_set_flag(int flag)    { s_detection_flag   = flag; }
IRAM_ATTR void csi_set_det_ts(uint64_t t){ s_det_timestamp_us = t;    }
IRAM_ATTR void csi_set_conf(float conf)  { s_det_conf         = conf;  }

// stores the bssid of our access point to filter csi packets
static uint8_t s_my_bssid[6];

// defines the data packet structure that goes into the queue
typedef struct {
    uint64_t det_ts_us;     // timestamp from the detection frame
    float    det_conf;      // confidence score from detection
    int      det_flag;      // number of people detected
    uint64_t csi_ts_us;     // timestamp when csi was received
    int      buf_len;       // length of the csi data buffer
    int8_t   buf[CSI_MAX_LEN]; // the csi data itself
} csi_packet_t;

static QueueHandle_t     s_csi_queue    = NULL; // queue to pass packets from csi callback to writer task
static SemaphoreHandle_t s_file_mutex   = NULL; // mutex to protect sd card file access
static sdmmc_card_t    * sd_card        = NULL;
static FILE            * csi_file       = NULL;
static bool              header_written = false;

// internal: mount sd card
// these settings (1-bit bus, 10 MHz clock, internal pull-ups, no card-detect pin) were required
// for reliable mounting on the ESP32-S3-EYE; the default configuration did not mount reliably.
static esp_err_t init_sd_card(void)
{
    ESP_LOGI(TAG, "Initializing SD card...");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = 10000;
    host.flags      &= ~SDMMC_HOST_FLAG_DDR;

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.width   = 1;
    slot_cfg.clk     = GPIO_NUM_39;
    slot_cfg.cmd     = GPIO_NUM_38;
    slot_cfg.d0      = GPIO_NUM_40;
    slot_cfg.gpio_cd = -1; // disable card detect and write protect pins
    slot_cfg.gpio_wp = -1;
    slot_cfg.flags  |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files              = 5,
        .allocation_unit_size   = 16 * 1024
    };

    sdmmc_card_t *card = NULL;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(
        MOUNT_POINT, &host, &slot_cfg, &mcfg, &card
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    sd_card = card;
    ESP_LOGI(TAG, "SD mounted");
    return ESP_OK;
}

// internal: open a new csi csv file with a timestamp in the name
void open_csi_file(void)
{
    int64_t ts_us = esp_timer_get_time();
    int64_t secs  = ts_us / 1000000;
    int64_t micros= ts_us % 1000000;

    char path[128];
    snprintf(path, sizeof(path),
             MOUNT_POINT "/csi_%lld.%06lld.csv",
             secs, micros);

    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) failed: %s", path, strerror(errno));
        return;
    }
    // use a large buffer for file writes to improve performance
    setvbuf(f, NULL, _IOFBF, 16 * 1024);
    csi_file = f;
    ESP_LOGI(TAG, "CSI file opened: %s", path);
}

// internal: close only the csv (leave sd mounted)
void close_csi_file(void)
{
    if (csi_file) {
        fclose(csi_file);
        csi_file = NULL;
    }
}

// csi writer task: pulls packets from the queue and writes them to the csv
static void csi_writer_task(void *arg)
{
    csi_packet_t pkt;
    int64_t last_roll = esp_timer_get_time();

    while (true) {
        if (xQueueReceive(s_csi_queue, &pkt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        int64_t now = esp_timer_get_time();
        // create a new csv file every 5 minutes (300 seconds) to keep file sizes manageable
        if (now - last_roll >= 300LL * 1000000LL) {
            xSemaphoreTake(s_file_mutex, portMAX_DELAY); // lock file access
            close_csi_file();
            header_written = false;
            open_csi_file();
            xSemaphoreGive(s_file_mutex); // unlock file access
            last_roll = now;
        }

        xSemaphoreTake(s_file_mutex, portMAX_DELAY);

        // write the csv header if this is a new file
        if (!header_written) {
            for (int i = 0; i < pkt.buf_len; i++) {
                if ((i & 1) == 0) { // real part
                    fprintf(csi_file, "%d,", (i/2) + 1);
                } else { // imaginary part
                    fprintf(csi_file, "%di,", (i/2) + 1);
                }
            }
            fprintf(csi_file, "people,conf,det ts,csi ts\n");
            header_written = true;
        }
        
        // write the csi data array
        for (int i = 0; i < pkt.buf_len; i++) {
            fprintf(csi_file, "%d,", pkt.buf[i]);
        }
        // write the metadata
        fprintf(csi_file, "%d,%.3f,%llu,%llu\n",
                pkt.det_flag,
                pkt.det_conf,
                (unsigned long long)pkt.det_ts_us,
                (unsigned long long)pkt.csi_ts_us);

        xSemaphoreGive(s_file_mutex);
    }
}

// only allow one csi packet every csi_interval_us to avoid overwhelming the system
static uint64_t s_last_csi_us = 0;
#define CSI_INTERVAL_US  (50 * 1000ULL)  // 50 ms = 20hz target rate

// csi rx callback (runs in isr context): pushes into queue
IRAM_ATTR static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !s_csi_queue) {
        return;
    }

    // rate‐limit to ~20 hz: drop anything arriving faster than 50 ms
    uint64_t now = esp_timer_get_time();
    if (now - s_last_csi_us < CSI_INTERVAL_US) {
        return;  // too soon: just drop it
    }
    s_last_csi_us = now;

    // ctx points to our 6-byte ap bssid
    uint8_t *my_bssid = (uint8_t *)ctx;

    // only accept csi frames from our ap
    // info->mac is the source mac for csi
    if (memcmp(info->mac, my_bssid, 6) != 0) {
        return;
    }

    // build the packet to send to the writer task
    csi_packet_t pkt = {
        .csi_ts_us = esp_timer_get_time(),
        .det_ts_us = s_det_timestamp_us, // grab the latest detection data
        .det_conf  = s_det_conf,
        .det_flag  = s_detection_flag,
        .buf_len   = info->len > CSI_MAX_LEN ? CSI_MAX_LEN : info->len
    };
    memcpy(pkt.buf, info->buf, pkt.buf_len);

    // send the packet to the queue from the isr
    BaseType_t woke = pdFALSE;
    xQueueSendFromISR(s_csi_queue, &pkt, &woke);
    if (woke) {
        portYIELD_FROM_ISR();
    }
}

// configure the wi-fi csi hardware
static void wifi_csi_init(void)
{
    // 1) get the bssid of the connected ap
    wifi_ap_record_t ap = { 0 };
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi_csi_init: get_ap_info failed (%s)",
                 esp_err_to_name(err));
    } else {
        // store the bssid for filtering in the csi callback
        memcpy(s_my_bssid, ap.bssid, sizeof(s_my_bssid));
    }

    // 2) configure csi hardware
    wifi_csi_config_t cfg = {
        .lltf_en           = true,
        .htltf_en          = false,
        .stbc_htltf2_en    = false,
        .ltf_merge_en      = true,
        .channel_filter_en = true,
        .manu_scale        = true,
        .shift             = true,
    };
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_csi_config(&cfg);
    // pass our bssid array as the “ctx” pointer to the callback for filtering
    esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, s_my_bssid);
    esp_wifi_set_csi(true);

    ESP_LOGI(TAG, "wifi_csi_init complete, filtering BSSID %02x:%02x:%02x:%02x:%02x:%02x",
             s_my_bssid[0], s_my_bssid[1], s_my_bssid[2],
             s_my_bssid[3], s_my_bssid[4], s_my_bssid[5]);
}

// ——— public api ———

// start csi writer.  assumes wi-fi, nvs, event loop, example_connect() already done.
esp_err_t csi_start(void)
{
    ESP_LOGI(TAG, "Starting CSI writer + SD…");

    // 1) sd mount + first file
    if (init_sd_card() == ESP_OK) {
        s_file_mutex = xSemaphoreCreateMutex();
        if (!s_file_mutex) {
            ESP_LOGE(TAG, "Could not create file mutex");
            return ESP_ERR_NO_MEM;
        }
        xSemaphoreTake(s_file_mutex, portMAX_DELAY);
        open_csi_file();
        xSemaphoreGive(s_file_mutex);
    }

    // 2) create queue and writer task
    s_csi_queue = xQueueCreate(CSI_QUEUE_LEN, sizeof(csi_packet_t));
    xTaskCreate(csi_writer_task, "csi_writer", 4*1024,
                NULL, tskIDLE_PRIORITY+2, NULL);

    // 3) initialize csi hardware
    wifi_csi_init();

    ESP_LOGI(TAG, "csi_start complete");
    return ESP_OK;
}

// flush any buffered csv data to card immediately.
void csi_flush(void)
{
    if (!csi_file) {
        return;
    }
    // take the file‐mutex so we never flush while someone else is closing/reopening
    xSemaphoreTake(s_file_mutex, portMAX_DELAY);

    fflush(csi_file);
    fsync(fileno(csi_file));

    xSemaphoreGive(s_file_mutex);
    ESP_LOGI(TAG, "Flushed + synced");
}

// stop csi collection and close the file
void csi_stop(void)
{
    close_csi_file();
    ESP_LOGI(TAG, "CSI stopped");
}

// -----------------------------------------------------------------------------
// deferred‐start task for ping
// this was created to solve a "ping_sock" memory error on startup which hindered collection to the point of self-rebooting
// -----------------------------------------------------------------------------
static void ping_defer_task(void *arg)
{
    // give the heap a couple seconds to settle after wi-fi and other things initialize
    vTaskDelay(pdMS_TO_TICKS(2000));

    // configure ping settings
    esp_ping_config_t pcfg = ESP_PING_DEFAULT_CONFIG();
    pcfg.count           = 0;        // infinite ping
    pcfg.interval_ms     = 50;       // one ping every 50 ms to generate traffic
    pcfg.data_size       = 1;        // smallest possible payload
    pcfg.task_stack_size = 4 * 1024;
    pcfg.task_prio = tskIDLE_PRIORITY + 3;  // lower priority than detection

    esp_netif_ip_info_t ip;
    esp_err_t err = esp_netif_get_ip_info(
        esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"),
        &ip
    );
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ping_defer: get_ip_info failed (%s)",
                 esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    pcfg.target_addr.u_addr.ip4.addr = ip.gw.addr;
    pcfg.target_addr.type            = ESP_IPADDR_TYPE_V4;

    // create & start the ping session
    esp_ping_handle_t h = NULL;
    err = esp_ping_new_session(&pcfg, NULL, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ping_defer: new_session failed (%s)",
                 esp_err_to_name(err));
    } else {
        err = esp_ping_start(h);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ping_defer: start failed (%s)",
                     esp_err_to_name(err));
        }
    }
    // task is done after starting the ping, so it can be deleted
    vTaskDelete(NULL);
}

// this function launches the deferred ping task on core 0
esp_err_t wifi_ping_router_start(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(
        ping_defer_task,   // function
        "ping_defer",      // name
        4 * 1024,          // stack size
        NULL,              // no parameter
        tskIDLE_PRIORITY + 3,
        NULL,              // we don’t need the handle after launch
        0                  // pin to core 0 (same as wi-fi driver)
    );
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "wifi_ping_router_start: OOM creating defer task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}



// Modified version of esp-who's who_detect_lcd.cpp (person detector component).
// It forwards each detection result (people count, frame timestamp, mean confidence)
// to the CSI logger through the csi_set_* functions above. See firmware/HANDOVER.md.
// To use it, replace esp-who/components/who_detect/src/who_detect_lcd.cpp with the code below;
// without it, the CSV files contain no people count or confidence score.

// #include "who_detect_lcd.hpp"
// #include "who_detect_utils.hpp"
// // i added this include to get access to the csi setter functions
// #include "csi_app.h"
// #include "esp_timer.h"

// namespace who {
// namespace detect {

// WhoDetectLCD::WhoDetectLCD(frame_cap::WhoFrameCap *frame_cap,
//                            dl::detect::Detect *detect,
//                            const std::string &name,
//                            const std::vector<std::vector<uint8_t>> &palette) :
//     WhoDetectBase(frame_cap, detect, name), m_res_mutex(xSemaphoreCreateMutex())
// {
// #if BSP_CONFIG_NO_GRAPHIC_LIB
//     m_palette = palette;
// #else
//     m_palette = cvt_to_lv_palette(palette);
// #endif
// }

// WhoDetectLCD::~WhoDetectLCD()
// {
//     vSemaphoreDelete(m_res_mutex);
// }

// // this function is called every time the model produces a new detection result
// void WhoDetectLCD::on_new_detect_result(const result_t &result)
// {
//     // 1) store the result for drawing (unchanged from original)
//     xSemaphoreTake(m_res_mutex, portMAX_DELAY);
//       m_results.push(result);
//     xSemaphoreGive(m_res_mutex);

//     // -- my modifications start here --
//     // this is where the detection results are passed to the csi component
    
//     // 2) number of detections is used as the 'people' flag
//     int flag = static_cast<int>(result.det_res.size());
//     csi_set_flag(flag);

//     // 3) use the *frame* timestamp for labeling, not the inference finish time such that is most accurate
//     //    result.timestamp is the struct timeval from when the frame was grabbed
//     uint64_t frame_ts_us =
//         (uint64_t)result.timestamp.tv_sec * 1000000ULL
//       + (uint64_t)result.timestamp.tv_usec;
//     csi_set_det_ts(frame_ts_us);

//     // 4) calculate the average confidence of all detections in the frame
//     float avg_conf = 0.0f;
//     for (auto &r : result.det_res) {
//         avg_conf += r.score;
//     }
//     if (!result.det_res.empty()) {
//         avg_conf /= result.det_res.size();
//     }
//     csi_set_conf(avg_conf);
//     // -- my modifications end here --
// }

// void WhoDetectLCD::lcd_display_cb(who::cam::cam_fb_t *fb)
// {
//     xSemaphoreTake(m_res_mutex, portMAX_DELAY);

//     auto compare_timestamp = [](const timeval &t1, const timeval &t2) {
//         return (t1.tv_sec < t2.tv_sec)
//             || (t1.tv_sec == t2.tv_sec && t1.tv_usec < t2.tv_usec);
//     };

//     struct timeval t1 = fb->timestamp;
//     if (!m_results.empty()) {
//         result_t r = m_results.front();
//         if (!compare_timestamp(t1, r.timestamp)) {
//             m_result = r;
//             m_results.pop();
//         }
//     }
//     xSemaphoreGive(m_res_mutex);

// #if BSP_CONFIG_NO_GRAPHIC_LIB
//     draw_detect_results_on_fb(fb, m_result.det_res, m_palette);
// #else
//     draw_detect_results_on_canvas(
//       static_cast<WhoFrameCapLCD *>(m_frame_cap)->get_lcd()->get_canvas(),
//       m_result.det_res, m_palette);
// #endif
// }

// } // namespace detect
// } // namespace who