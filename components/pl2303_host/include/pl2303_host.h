#pragma once

/* Independent HXD serial echo test. Does not forward HC-05 traffic. */
void pl2303_echo_run(void);
/* Transparent byte transport; never generates telescope commands. */
void pl2303_bridge_run(void);
