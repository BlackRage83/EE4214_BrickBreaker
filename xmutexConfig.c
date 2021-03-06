#ifndef XMUTEXCONFIG_C
#define XMUTEXCONFIG_C

#include "xmutexConfig.h"

int initXMutex() {
	XStatus Status;
	int ret;

	// configure the HW Mutex
	XMutex_Config* ConfigPtr;
	ConfigPtr = XMutex_LookupConfig(MUTEX_DEVICE_ID);
	if (ConfigPtr == (XMutex_Config *)NULL){
		print("B1-- ERROR  init HW mutex...\r\n");
	}

	// initialize the HW Mutex
	Status = XMutex_CfgInitialize(&Mutex, ConfigPtr, ConfigPtr->BaseAddress);

	if (Status != XST_SUCCESS){
		print("B1-- ERROR  init HW mutex...\r\n");
	}

	return Status;
}

void safePrint(const char *ptr) {
    XMutex_Lock(&Mutex, MUTEX_NUM);
    print(ptr);
    XMutex_Unlock(&Mutex, MUTEX_NUM);
}

#endif
