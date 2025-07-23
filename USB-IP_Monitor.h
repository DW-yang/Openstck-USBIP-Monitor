// USB-IP_Monitor.h: 标准系统包含文件的包含文件
// 或项目特定的包含文件。

#pragma once

#include <iostream>

void init_daemon();
void lock_detect();
void usbip_detect();
void usb_device_detect();
void usbip_bind_devices();
void append(char* list[4], char* item);
void del(char* list[4], char* item);
void init_devices();
void init_devices_to_bind();
void init_binded_devices();