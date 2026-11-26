# csi presence detection - project handover

this project uses an esp32-s3-eye board to collect csi (channel state information) data and simultaneously label it with the number of people in a room. the goal is to create a dataset for training a machine learning model that can predict room occupancy from wi-fi signals alone.

the code is a combination of several espressif examples, primarily `pedestrian_detect` from `esp-who` and `csi_recv_router` from `esp-csi`.

## hardware required
- esp32-s3-eye board
- a micro sd card (formatted as fat32)
- a micro-usb cable for power and programming
- a wi-fi router

## software setup

1.  **esp-idf:** this project is built with the esp-idf 5.4.0 framework. make sure you have it installed and configured correctly.

2.  **get the code:** the base project is `esp-who`. you need to clone it:
    ```
    git clone --recursive https://github.com/espressif/esp-who.git
    ```

3.  **place project files:** this project should be placed inside the `esp-who/examples/` directory of the cloned repo. the structure should look like this:
    ```
    esp-who/
    ├── examples/
    │   ├── YOUR_NEW_PROJECT/  <-- put all your project files here
    │   │   ├── main/
    │   │   ├── components/
    │   │   └── ...
    │   └── ...
    └── ...
    ```

4.  **critical modification:** for this to work, you must modify one of the files that `esp-who` downloads as a managed component. this is the file that links the person detection results to my csi data logger.

    -   **file to modify:** `esp-who/components/who_detect/src/who_detect_lcd.cpp`
    -   **what to do:** open this file and apply the changes i made. you can see the modified version **at the bottom of csi_app.c**. the key changes are:
        -   add `#include "csi_app.h"`.
        -   in the `on_new_detect_result` function, add the code that calls `csi_set_flag()`, `csi_set_det_ts()`, and `csi_set_conf()`. these functions send the number of people, the timestamp, and the confidence score to my csi logger.

    **important:** without this change, the csv files will be created but the 'people', 'conf', and 'det ts' columns will always be zero.

5.  **configure wi-fi:** navigate to the project directory (`esp-who/examples/csi_pedestrian_detect/`) and run:
    ```
    idf.py menuconfig
    ```
    go to `example connection configuration` and enter the ssid and password of your wi-fi router. save and exit.

6.  **build and flash:**
    ```
    idf.py build flash monitor
    ```

## how it works

the system is designed to be efficient by splitting tasks across the esp32's two cores to prevent crashes and data loss.

-   **core 0:** handles all the heavy lifting for person detection. it runs the wi-fi driver, the camera, and the tensorflow lite model for detecting people. it also runs the `ping` task, which is essential.
-   **core 1:** dedicated entirely to collecting and saving data. it runs the csi receiving task and the sd card writer task. this separation prevents the sd card writes from lagging or interfering with the person detection.

### data flow

1.  **ping task (core 0):** a continuous `ping` is sent to the router. this is necessary to force the router to send back packets containing csi data. without this, no csi is generated.
2.  **person detection (core 0):** the camera captures frames, and the `pedestrian_detect` model finds people.
3.  **labeling (core 0):** when a person is detected, the modified `who_detect_lcd.cpp` code calls my `csi_set_*` functions. this updates a few shared variables with the number of people, the detection confidence, and the exact timestamp of the camera frame.
4.  **csi reception (core 1):** the wi-fi hardware receives a csi packet. the `wifi_csi_rx_cb` callback fires.
5.  **filtering & packaging (core 1):** the callback first checks if the packet is from our router (by bssid) and if it's not coming too fast (rate-limited to ~20hz). if it's a good packet, it creates a `csi_packet_t` struct, copying the csi data and grabbing the latest person detection info from the shared variables.
6.  **queuing (core 1):** this `csi_packet_t` is put into a freertos queue. using a queue safely passes the data from the high-priority network callback to the lower-priority writer task.
7.  **sd card writing (core 1):** the `csi_writer_task` pulls packets from the queue and writes them as a new row in a csv file on the sd card. to avoid having giant files, a new csv file is created every 5 minutes.

### data format

the data is saved in csv files named `csi_[timestamp].csv`. each row in the csv represents one csi packet and its corresponding label.

-   **columns 1 to n:** the raw csi data. these are pairs of i/q values for each subcarrier. the header is `1,1i,2,2i,...`.
-   **`people` column:** the number of people detected in the frame. `0` for empty, `1` for one person, etc.
-   **`conf` column:** the average confidence score (0.0 to 1.0) of the detection.
-   **`det ts` column:** the timestamp (in microseconds since boot) of when the camera frame was captured for the detection.
-   **`csi ts` column:** the timestamp of when the csi packet was received.

this setup allows for leaving the board running for hours to collect large, labeled datasets for further research.

**P.S.** some changes have been made to the configuration of the board. they are scattered around in "sdkconfig.defaults" and "sdkconfig.defaults.esp32s3". these changes will override the ones in the standard "sdkconfig.defaults" which is a very long config. these configs can be manually written in the files indicated or they can be trakced down inside the menuconfig menu and changed there. MODIFY AT YOUR OWN RISK !! also, these changes can only be applied if the required components responsible with them are present in the project directory. the reason for these changes is i was desperately trying to have the board run without crashing, without errors, without rebooting and with optimal CSI collecting performance. sporradic errors were happening at very random times, for example let's say when you are 18 minutes deep into a data collection session and you are not watching the board. i had to stare at the monitor for long periods of time to make sure i do not get chaotic reboots or crashes or errors. this is the configuration that allowed me to consistently collect data without issues, resulting in 1.0-1.2 MB per 5 minute file in both rooms and in all locations. with the errors mentioned, you could have between 300 KB and 1.5 MB of data randomly.

**P.S. 2** the partition files came with the standard "pedestrian_detect" project and i did not modify them them. same goes for dependencies. IIRC, the code uses partitions.csv not partitions2.csv. this can be addressed from menuconfig