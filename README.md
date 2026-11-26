# ESP32 Wi-Fi presence sensing with on-board camera auto-labelling

Individual BSc end project, Electrical Engineering, Eindhoven University of Technology (2025), graded 9.5/10.

Wi-Fi channel state information (CSI) can detect whether a person is in a room without a camera, but training a model normally needs a lot of manual labelling. This project runs both on one low-cost ESP32-S3: the board records raw CSI from the room's router while its on-board camera person detector labels the same moments automatically. A Python pipeline then trains a presence classifier and compares automatic labels with manual ground truth.

Paper: [`docs/paper.pdf`](docs/paper.pdf) · Presentation: [`docs/presentation.pdf`](docs/presentation.pdf)

## What I built

**Firmware: C/C++, ESP-IDF 5.4, FreeRTOS on ESP32-S3-EYE (`firmware/`)**
- Captures raw CSI only from the connected access point (BSSID filter in the CSI callback).
- Hooks into Espressif's on-board pedestrian detector to read person count, detection timestamp and confidence.
- Passes synchronised CSI and detection records between tasks through a FreeRTOS queue and logs them to SD card (FAT32).
- Debugged and stabilised it for long recording sessions (SD mounting, memory pressure, crashes). Notes are in [`firmware/HANDOVER.md`](firmware/HANDOVER.md).

**Analysis: Python, TensorFlow/Keras, scikit-learn (`analysis/`)**
- Cleaning, inactive-subcarrier removal (128 → 104 values), wavelet denoising, PCA and windowing, followed by an MLP classifier.
- Three labelling strategies compared: forced ground truth, raw detector labels, confidence-filtered detector labels.

```text
 router ──Wi-Fi──► ESP32-S3-EYE ─────────────────────────────► SD card (CSV)
                   ├─ CSI callback (BSSID-filtered)  ─┐                │
                   └─ camera person detector ─────────┴─► queue ─► logger task
                                                                       ▼
                     Python: clean → denoise → PCA → windows → MLP → evaluation
```

## Repository layout

| Path | Contents |
| --- | --- |
| `firmware/main/` | Application entry (`app_main.cpp`) and FAT timestamp helper. |
| `firmware/components/csi_recv_router/` | CSI capture, synchronisation and SD logging (`csi_app.c`). The modified upstream detector hook is at the bottom of this file. |
| `firmware/components/{lwip,ping,protocol_examples_common,sd_card}/` | Third-party components from Espressif / lwIP, kept under their own licences. |
| `analysis/notebooks/` | One self-contained notebook per labelling strategy (`ABC1` forced ground truth, `XYZ2` raw detector, `XYZ2_filtered` confidence-filtered). |
| `analysis/csi_preprocessing/` | Helper module (cleaning, config, plotting). |
| `analysis/data/raw_data_sample/` | One sample recording per class and device placement. The full dataset (48 recordings, ~150k CSI records, ~50 MB) is available on request. |
| `analysis/saved_models/`, `analysis/plots/` | Trained models and evaluation plots per experiment. |

## Build the firmware

1. Install ESP-IDF 5.4 and clone [esp-who](https://github.com/espressif/esp-who) recursively.
2. Place `firmware/` inside `esp-who/examples/`, then apply the detector hook described in [`firmware/HANDOVER.md`](firmware/HANDOVER.md).
3. Set your Wi-Fi SSID and password with `idf.py menuconfig`. Credentials are not stored in this repository.
4. Run `idf.py set-target esp32s3 && idf.py build flash monitor`.

## Run the analysis

```bash
cd analysis
pip install -r requirements.txt
jupyter notebook notebooks/
```

Put the recordings in `analysis/data/raw_data/{0_pers,1_pers}/{left,right}/` and check the paths in `config.json`.

## Credits

Built on Espressif's [esp-who](https://github.com/espressif/esp-who) (`pedestrian_detect`) and [esp-csi](https://github.com/espressif/esp-csi) (`csi_recv_router`) examples. Third-party components keep their original licences and copyright headers. Supervisor: Dr. Alexios Balatsoukas-Stimming, TU/e.
