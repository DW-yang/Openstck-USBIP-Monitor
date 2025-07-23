// USB-IP_Monitor.cpp: 定义应用程序的入口点。
//

#include "USB-IP_Monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <dirent.h>

#define LOCK_FILE "/var/run/usbip_monitor.lock"

char* devices[64];			// 字符串长度为256
char* devices_to_bind[4];	// 字符串长度为16
char* binded_devices[4];	// 字符串长度为16

// 进程Daemon化
void init_daemon() {
	pid_t pid;
	pid = fork();

	if (pid < 0) {
		syslog(LOG_ERR, "Daemon fork error");
		exit(EXIT_FAILURE);
	}
	else if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	setsid();	// 设置会话id
	chdir("/");	// 切换工作目录
	umask(0);	// 设置掩码

	for (int i = 0; i < getdtablesize(); i++) {
		close(i);	// 关闭所有fd
	}
	// 重定向fd
	open("/dev/null", O_RDWR);	// stdin
	open("/dev/null", O_RDWR);	// stdout
	open("/dev/null", O_RDWR);	// stderr
}

// 文件锁检测
void lock_detect() {
	int fd = open(LOCK_FILE, O_RDWR|O_CREAT, 0666);
	if (fd < 0) {
		syslog(LOG_ERR, "Could not open lock file");
		exit(EXIT_FAILURE);
	}
	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
		syslog(LOG_WARNING, "Lock failed, may be another daemon is running, exiting");
		exit(EXIT_SUCCESS);
	}
}

// 初始化数组
void init_devices() {
	for (int i = 0; i < 64; i++) {
		devices[i] = (char*)malloc(256);
		memset(devices[i], 0, 256);
	}
}

// 初始化数组
void init_devices_to_bind() {
	for (int i = 0; i < 4; i++) {
		devices_to_bind[i] = (char*)malloc(16);
		memset(devices_to_bind[i], 0, 16);
	}
}

// 初始化数组
void init_binded_devices() {
	for (int i = 0; i < 4; i++) {
		binded_devices[i] = (char*)malloc(16);
		memset(binded_devices[i], 0, 16);
	}
}

//检测usbip与usbipd
void usbip_detect() {
	FILE* fp;
	char output[64] = "";

	fp = popen("/sbin/uci show wireless.wifinet0.mode", "r");
	if (fp == NULL) {
		syslog(LOG_ERR, "Failed to run uci command");
		exit(EXIT_FAILURE);
	}
	if (fgets(output, sizeof(output), fp)) {
		output[strcspn(output, "\n")] = '\0';
	}
	pclose(fp);

	if (strcmp(output, "wireless.wifinet0.mode='ap'") == 0) {
		// 切换USB至host模式
		if (WEXITSTATUS(system("echo host > /sys/devices/platform/soc@0/78d9000.usb/ci_hdrc.0/role")) != 0) {
			syslog(LOG_ERR, "Failed to switch usb mode to host");
			exit(EXIT_FAILURE);
		}
		// 等待切换完成
		sleep(1);
		// 尝试启动usbipd
		if (WEXITSTATUS(system("usbipd -D")) != 0) {
			syslog(LOG_ERR, "usbipd failed to start");
			exit(EXIT_FAILURE);
		}
		// 尝试启动usbip
		if (WEXITSTATUS(system("usbip list -l")) != 0) {
			syslog(LOG_ERR, "usbip failed to start");
			exit(EXIT_FAILURE);
		}
	}
	else {
		// 切换USB至gadget模式
		syslog(LOG_ERR, "WiFi AP mode not enable, switch usb to gadget mode");
		if (WEXITSTATUS(system("echo gadget > /sys/devices/platform/soc@0/78d9000.usb/ci_hdrc.0/role")) != 0) {
			syslog(LOG_ERR, "Failed to switch usb mode to gadget");
			exit(EXIT_FAILURE);
		}
	}
}

// 检测所有usb设备
void usb_device_detect() {
	DIR* dir = opendir("/sys/bus/usb/devices");
	if (!dir) {
		syslog(LOG_ERR, "Failed to open /sys/bus/usb/devices");
		exit(EXIT_FAILURE);
	}
	dirent* direntp;
	int i = 0;
	errno = 0;
	while ((direntp = readdir(dir)) && i < 64) {
		if (direntp->d_name[0] == '\0') continue;
		strncpy(devices[i], direntp->d_name, 255);
		devices[i][255] = '\0';
		i++;
	}
	if (errno != 0) {
		syslog(LOG_ERR, "Readdir error");
		exit(EXIT_FAILURE);
	}
	closedir(dir);
}

// bind usb设备
void usbip_bind_devices() {
	// 筛选可绑定usb设备
	for (int i = 0; i < 4; i++) {	// 清空devices_to_bind
		strcpy(devices_to_bind[i], "");
	}
	for (int i = 0; i < 64; i++) {
		if (devices[i] == NULL || devices[i][0] == '\0') continue;
		if (strncmp(devices[i], "3-1.", 4) == 0 && strlen(devices[i]) >= 5) {
			char temp[256];
			strncpy(temp, devices[i], sizeof(temp) - 1);
			temp[5] = '\0';
			append(devices_to_bind, temp);
		}
	}
	
	// 检测未绑定的设备
	for (int i = 0; i < 64; i++) {
		if (devices[i] == NULL || devices[i][0] == '\0') continue;
		if (strncmp(devices[i], "3-1.", 4) == 0 && strlen(devices[i]) >= 5) {
			if (strncmp(devices[i] + 5, ":1.0", 4) == 0) {
				char temp[256];
				strncpy(temp, devices[i], sizeof(temp) - 1);
				temp[5] = '\0';
				del(binded_devices, temp);
			}
		}
	}

	int flag;
	for (int i = 0; i < 4; i++) {
		// 检查待绑定的设备是否已经绑定
		if (devices_to_bind[i] == NULL || devices_to_bind[i][0] == '\0') continue;
		flag = 0;
		for (int j = 0; j < 4; j++) {
			if (binded_devices[i] == NULL || binded_devices[i][0] == '\0') continue;
			if (strcmp(devices_to_bind[i], binded_devices[j]) == 0) {
				flag = 1;
				break;
			}
		}
		if (flag) continue;	// 已绑定则跳过绑定

		// 绑定USB设备
		syslog(LOG_NOTICE, "Attempting to bind %s ...", devices_to_bind[i]);
		char command[256];
		sprintf(command, "usbip bind -b %s", devices_to_bind[i]);
		int rc = system(command);
		if (WEXITSTATUS(rc) != 0) {
			syslog(LOG_WARNING, "Failed to bind device %s, returned %d", devices_to_bind[i], WEXITSTATUS(rc));
			continue;
		}
		else {
			syslog(LOG_NOTICE, "usbip bind -b %s returned %d", devices_to_bind[i], WEXITSTATUS(rc));
			append(binded_devices, devices_to_bind[i]);
		}
	}
}

// 向数组添加内容
void append(char* list[4], char* item) {
	// 检测是否存在相同item
	for (int i = 0; i < 4; i++) {
		if (strcmp(list[i], item) == 0) {
			return;
		}
	}
	// 检测是否存在空位
	for (int i = 0; i < 4; i++) {
		if (strcmp(list[i], "") == 0) {
			strcpy(list[i], item);
			return;
		}
	}
}

// 删除数组内容
void del(char* list[4], char* item) {
	// 检测是否存在相同item
	for (int i = 0; i < 4; i++) {
		if (strcmp(list[i], item) == 0) {
			strcpy(list[i], "");
		}
	}
}

int main()
{
	init_daemon();
	lock_detect();
	init_devices();
	init_devices_to_bind();
	init_binded_devices();
	usbip_detect();
	while (1) {
		usb_device_detect();
		usbip_bind_devices();
		sleep(3);
	}
	return 0;
}
