/*
*	JAHTTP, A Memory file system based HTTP server
*	2016 Jonathan Andrews (jon @ jonshouse.co.uk )
*
*/

#define OVERCLOCK										// uncomment this line to overclock
static char *ssid = "";										// Optionally set these to join your local network
static char *pass = "";
#define AP_SSID "ESP-HTTP-DEMO"								// If this is non blank then the AP name is set 


#include "ets_sys.h"
#include "osapi.h"
#include "os_type.h"

#include "user_interface.h"
#include "espconn.h"
#include "mem.h"
#include "driver/uart.h"




void ICACHE_FLASH_ATTR init_done( void )
{
     struct station_config station_config;

     wifi_set_opmode_current(STATIONAP_MODE);
     wifi_set_broadcast_if(3);									// 1:station, 2:soft-AP, 3:both

     strncpy(station_config.ssid, ssid, 32);
     strncpy(station_config.password, pass, 32);
     wifi_station_set_config(&station_config);
     wifi_station_connect();
}



void ICACHE_FLASH_ATTR user_init()
{
    struct station_config conf;
    uart_init(BIT_RATE_115200, BIT_RATE_115200);

#ifdef OVERCLOCK
    REG_SET_BIT(0x3ff00014, BIT(0));
    system_update_cpu_freq(160);
#endif

    os_printf("\n");
    os_printf("SDK version:%s\n", system_get_sdk_version());
    system_print_meminfo();
    os_delay_us ( 1 );
    os_printf ( "CPU Hz = %d\n", system_get_cpu_freq() );

    if (strlen(AP_SSID)>=1)
    {
    	os_printf("Change AP name to :%s\n",AP_SSID);
    	struct softap_config config;
    	wifi_softap_get_config(&config);
    	os_strncpy(config.ssid, AP_SSID, sizeof(config.ssid));
    	config.ssid_len=strlen(AP_SSID);
    	ETS_UART_INTR_DISABLE();
    	wifi_softap_set_config(&config);
    	ETS_UART_INTR_ENABLE();
    }

    show_mac();
    show_ip();
    system_phy_set_powerup_option(3);
    system_phy_set_max_tpw(82);

    init_dns();
    init_jahttp();

    system_init_done_cb(init_done);
    system_os_post(0, 0, 0 );
}


void ExitCritical()
{
}

void EnterCritical()
{
}
