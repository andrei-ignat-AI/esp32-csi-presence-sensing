#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 
 * mount sd, start csi callback + writer task, open first csv.
 * assumes wi-fi & event loop already up. 
 */
esp_err_t csi_start(void);

/** 
 * close csv. sd remains mounted. 
 */
void      csi_stop(void);

/** flush any buffered csv data to card immediately. */
void      csi_flush(void);

/** set the number of people detected. called from detection code. */
void      csi_set_flag(int flag);

/** set the detection timestamp (µs since boot). */
void      csi_set_det_ts(uint64_t ts_us);

/** set the detection average confidence (0.0–1.0). */
void      csi_set_conf(float conf);

/** (internal) open a new csi csv on the sd card. */
void      open_csi_file(void);

/** (internal) close only the csv file (sd stays mounted). */
void      close_csi_file(void);

/** start the on-board ping session to generate csi traffic. */
esp_err_t wifi_ping_router_start(void);

#ifdef __cplusplus
}
#endif