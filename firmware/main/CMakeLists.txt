# defines main component dependencies
# csi_recv_router is our custom component for data collection

set(src_dirs        ./)
set(include_dirs    ./)

set(requires who_detect_app
             pedestrian_detect csi_recv_router)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})