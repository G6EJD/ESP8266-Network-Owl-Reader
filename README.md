# ESP8266-Network-Owl-Reader
An ESP8266/Arduino receives a UDP multicast packet broadcast by a Network Owl and displays the (XML) data on an
ILI9341 TFT display, using a graph that auto scales, it also displays the time that the UDP packet was received.
The project demonstrates many facets of Arduino/ESP programming.

See the YouTube video to see it working: https://www.youtube.com/watch?v=v9ai9p3wGjk

This is the list of the components you will need for this project:

• An ESP8266-12E or WEMOS D1 Mini or any of the other ESP8266 development boards
http://www.aliexpress.com/item/New-Wireless-module-CH340-NodeMcu-V3-Lua-WIFI-Internet-of-Things-development-board-based-ESP8266/32654418046.html?spm=2114.30010308.3.328.he7pWL&ws_ab_test=searchweb201556_0,searchweb201602_4_10057_10056_10055_10037_301_10033_10059_10032_10058_10017_10060_10061_10062_413,searchweb201603_4&btsid=c90fb709-8355-4120-b944-30951dae3616

• TFT display SPI bus

• Breadboard & jumper wires

FOr the Arduinio development environment you will need the latest version of the Arduino IDE that you can get from:

https://www.arduino.cc/en/Main/Software

You will need to install the ESP8266 boards definitions by following the process from:

https://github.com/esp8266/Arduino

Note: Currently only Arduinio 1.6.9 is supported for the ESP8266 environment. 

