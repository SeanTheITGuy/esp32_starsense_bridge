# Component dependency discovery runs before CONFIG_* values are available.
# Declare both dependencies here; main.c selects the one active test at runtime.
idf_component_register(SRCS "main.c" "hc05_test.c" "hc05_diag.c" INCLUDE_DIRS "."
                       REQUIRES hc05_uart usb_probe pl2303_host)
