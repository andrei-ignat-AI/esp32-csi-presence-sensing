# this is the cmake for the csi_recv_router component

# defines dependencies for this component.
# it needs drivers for sd card, wi-fi, etc. these are taken from the huge set of components and helpers that end up in your computer after cloning repos from espressif's github. 
# some components will be in "esp-idf" main thing, others will be repository-specific such as from esp-csi and esp-who. 
# and esp-idf libraries for tasks, events, and logging.

# these dependencies shall be present in your project_root/components directory. i.e., they are just as much a component as csi_recv_router is so they need to be imported. you need to make sure
# that ESP downloaded them somewhere on your computer and then copy them into your components folder

idf_component_register(
    SRCS    "src/csi_app.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver             
        sdmmc               
        fatfs               
        vfs                 
        esp_timer           
        esp_wifi            
        nvs_flash
        esp_netif
        esp_event
        lwip
        protocol_examples_common
        log                 
        esp_lcd             
)