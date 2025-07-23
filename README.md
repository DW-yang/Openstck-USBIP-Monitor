# USB-IP_Monitor

此项目仅适用于板号UZ801并且运行Immortalwrt系统的随身WiFi

### 功能

在检测WiFi热点开启后，自动切换usb至host模式，并且监听usb设备的连接状态，实现自动导出usb设备。

### BUG

usb设备在被usbip客户端attach后，会出现无限bind的情况，原因是设备被attach后，/sys/bus/usb/devices目录下的x-x.x:1.0目录会再次出现
该bug会影响性能，但不影响功能
