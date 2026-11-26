#include "csi_app.h"
#include "pedestrian_detect.hpp"
#include "who_detect_app.hpp"

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "protocol_examples_common.h" // for example_connect()
#include "ping/ping_sock.h"          // for wifi_ping_router_start()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#include <sys/time.h>
#include <time.h>
#include <string.h>  

using namespace who::cam;
using namespace who::lcd;
using namespace who::app;

static const char* MEMTAG = "MEMLOG";

// forward declaration of the ping helper in csi_app.c
extern "C" esp_err_t wifi_ping_router_start(void);

// helper to convert month string to an integer for rtc setup
static int month_str_to_int(const char *m) {
    if      (!strcmp(m,"Jan")) return 1;
    else if (!strcmp(m,"Feb")) return 2;
    else if (!strcmp(m,"Mar")) return 3;
    else if (!strcmp(m,"Apr")) return 4;
    else if (!strcmp(m,"May")) return 5;
    else if (!strcmp(m,"Jun")) return 6;
    else if (!strcmp(m,"Jul")) return 7;
    else if (!strcmp(m,"Aug")) return 8;
    else if (!strcmp(m,"Sep")) return 9;
    else if (!strcmp(m,"Oct")) return 10;
    else if (!strcmp(m,"Nov")) return 11;
    else if (!strcmp(m,"Dec")) return 12;
    return 0;
}

/** initialize rtc from compile date/time. */
// this ensures the timestamps in the csv files are somewhat accurate on boot
static void set_build_datetime()
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    char month[4];
    int day, year, hour, min, sec;
    sscanf(__DATE__, "%3s %d %d", month, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &min, &sec);
    t.tm_year = year - 1900;
    t.tm_mon  = month_str_to_int(month) - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = min;
    t.tm_sec  = sec;
    time_t tt = mktime(&t);
    timeval tv = { .tv_sec = tt, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    // central european time
    setenv("TZ","CET-1CEST,M3.5.0/2,M10.5.0/3",1);
    tzset();
    ESP_LOGI("RTC","RTC set to %04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year+1900, t.tm_mon+1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
}

// csi + sd writer task runs entirely on core 1
// this separates it from the camera and detection logic on core 0
static void csi_task(void*)
{
    // mount sd card and start csi collection
    ESP_ERROR_CHECK(csi_start());

    // periodically flush data to the sd card and blink led
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10 * 1000));
        csi_flush();
    #if CONFIG_IDF_TARGET_ESP32S3
        static bool led = false;
        led = !led;
        bsp_led_set(BSP_LED_GREEN, led);
    #endif
    }
}

// control logs from various components
static void __attribute__((constructor)) init_log_levels()
{
    esp_log_level_set("*",               ESP_LOG_WARN);
    esp_log_level_set("example_connect", ESP_LOG_INFO);
    esp_log_level_set("csi_recv_router", ESP_LOG_INFO);
    esp_log_level_set("ping_sock",       ESP_LOG_WARN);
}

extern "C" void app_main()
{
    // set the internal clock on boot
    set_build_datetime();

  #if CONFIG_IDF_TARGET_ESP32S3
    // initialize on-board leds
    ESP_ERROR_CHECK(bsp_leds_init());
    ESP_ERROR_CHECK(bsp_led_set(BSP_LED_GREEN,false));
  #endif

    //
    // 1) set up wi-fi (nvs, netif, event loop, example_connect)
    //
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // this connects to the ssid/password from menuconfig
    ESP_ERROR_CHECK(example_connect());

    // give all heap-hungry subsystems (wi-fi driver, lwip, camera, sd) 3 s to finish
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 2) launch ping session exactly once, on core 0
    // this generates constant traffic, which makes the router send csi data
    // it's deferred to avoid startup memory issues that caused ping_sock errors
    esp_err_t ping_err = wifi_ping_router_start();
    if (ping_err != ESP_OK) {
        ESP_LOGW("csi_recv_router",
                 "ping start failed after heap settle: %s",
                 esp_err_to_name(ping_err));
    }

    //
    // 3) camera + detector init (from the original pedestrian_detect example)
    //
  #if CONFIG_IDF_TARGET_ESP32P4
    auto cam = new WhoP4Cam(VIDEO_PIX_FMT_RGB565,3,
                            V4L2_MEMORY_USERPTR,true);
  #elif CONFIG_IDF_TARGET_ESP32S3
    auto cam = new WhoS3Cam(PIXFORMAT_RGB565,
                            FRAMESIZE_240X240,2,true);
  #endif
    auto lcd    = new WhoLCD();
    auto model  = new PedestrianDetect();
    auto detect = new WhoDetectAppLCD({{255,0,0}});
    detect->set_cam(cam);
    detect->set_lcd(lcd);
    detect->set_model(model);
    detect->set_fps(10.0f);

    //
    // 4) kick off csi on core 1 (handles csi_start, writer, sd rollover, etc.)
    //
    xTaskCreatePinnedToCore(
        csi_task, "csi", 4*1024, nullptr,
        tskIDLE_PRIORITY+1, nullptr, 1
    );

    //
    // 5) enter the detection loop (this blocks and runs forever on core 0)
    //
    detect->run();
}