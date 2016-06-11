Memory file system based http server for ESP8266-12 modules

This project is for ESP-12 modules or those with 4MBytes or more of flash.

The ESP8266 is not well suited to this role, this is a "just for fun" project.
When I started writing this I could only see one other esp http server, since
then I have found a couple more.
This code is early Beta quaility at best.

With care and careful image quality/size tweaks a reasonable size static web
site could fit in the 3 Megabytes available for it on the ESP12.

TCP Performance has been an issue on these modules, over clocked and under ideal 
conditions it may sqeak 110 KBytes a second but expect much less.

This is compiled and tested against esp_iot_sdk_v1.5.2 only.



To build:
	Edit Makefile,  set the paths at the top for your build environment

	Edit user/main.c, set ssid, pass and OVERCLOCK options at the top
	Place the static web page content in www directory, keep filenames short

	$ make clean;make
	$ ./flashesp


To use:
	If the ssid and pass have been set the module should appear on your network, or :

	Directly connect using Wifi
	Browse to:
			http://192.168.4.1
	If index.html is in the flash image then the server will present that as the page /
	If index.html is not present a list of files in flash will be returned.

	A list of files can be had by using a URL containing a single "?" character
		IE:	http://192.168.4.1/?

	At the moment the server only recognises .html .htm .c .h .jpg and .png files - others
	may require a bit of tweaking of the server. File type is based solely on the last 3
	digits of the file extension.


ToDo:
	Fix bug, sending some extra 0x00 bytes at the end of the each file,
	Add a number of files in memory file system field to mfs code rather than use an include

Reuse:
	Feel free to use the code, a credit and reference to the original projects would be nice.
	Please Fork, contibutions back are welcome.


Sample web page:
	Very simple and not pretty, just to prove it works, 


Credits:
	Thanks to the hard work of others I did not need to start from scratch and borrowed code and
	ideas from the following:

     	The basic HTTP server is based on the well structured TCP server demo code by Tom Trebisky
	His web site:		http://cholla.mmto.org
	code used is here:	http://cholla.mmto.org/esp8266/OLD/sdk/tcp_server.c

	The memory filesystem code is a slightly modified version of the memory file system code by Charles Lohr
	from this project:	https://github.com/cnlohr/esp8266ws2812i2s

	DNS spoofing by Israel Lot
	https://github.com/israellot/esp-ginx/blob/master/app/dns/dns.c



Other packages available:
	Having written most of this I found a couple of better written projects. Google for 
	"esp-ginx" and  "esphttpd"

