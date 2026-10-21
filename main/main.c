#include "sdkconfig.h"
#if CONFIG_BRINGUP_USB_ENUM
#include "usb_probe.h"
#endif
#if CONFIG_BRINGUP_PL2303_ECHO || CONFIG_BRINGUP_BRIDGE
#include "pl2303_host.h"
#endif

void hc05_test_run(void);
void hc05_diag_run(void);

void app_main(void)
{
#if CONFIG_BRINGUP_USB_ENUM
    usb_probe_run();
#elif CONFIG_BRINGUP_PL2303_ECHO
    pl2303_echo_run();
#elif CONFIG_BRINGUP_BRIDGE
    pl2303_bridge_run();
#elif CONFIG_BRINGUP_HC05_DIAG
    hc05_diag_run();
#else
    hc05_test_run();
#endif
}
