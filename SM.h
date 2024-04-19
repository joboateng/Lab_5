#ifndef SM_H_
#define SM_H_

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>

#define SHM_MSG_KEY 98753
#define SHMSIZE sizeof(SmStruct)
#define SEM_NAME "cyb01b_p5"

// Constants for resource management
#define MAX_PROCESS_CONTROL_BLOCKS 18
#define MAX_RESOURCE_DESCRIPTORS 20
#define MAX_RESOURCE_COUNT 20
#define MAX_RESOURCE_QTY 10
#define MAX_RESOURCE_REQUEST_COUNT 1000
#define RESOURCE_DESCRIPTOR_MOD 5
#define MAX_RESOURCES 20

// Structure definitions

typedef struct {
    int resourceId;
} SmResourceDescriptorInstance;

typedef struct {
    int request;
    int allocation;
    int release;
    int sharable;
    SmResourceDescriptorInstance resInstances[MAX_RESOURCE_COUNT];
} SmResourceDescriptor;

typedef struct {
    int startUserSeconds;
    int startUserUSeconds;
    int endUserSeconds;
    int endUserUSeconds;
    int totalCpuTime;
    int totalTimeInSystem;
    int lastBurstLength;
    int processPriority;
    int pid;
    int resources[100];
} SmProcessControlBlock;

typedef struct {
    int ossSeconds;
    int ossUSeconds;
    int userPid;
    int userHaltSignal; 
    int userHaltTime;
    int userResource;
    int userRequestOrRelease; 
    int userGrantedResource;
    SmProcessControlBlock pcb[MAX_PROCESS_CONTROL_BLOCKS];
    SmResourceDescriptor resDesc[MAX_RESOURCE_DESCRIPTORS];
    int resourcesGrantedCount[MAX_RESOURCE_COUNT];
    int resourceRequestQueue[MAX_RESOURCE_REQUEST_COUNT][2]; 
} SmStruct;

// Function prototypes

sem_t* open_semaphore(int createSemaphore);

void close_semaphore(sem_t *sem);

#endif /* SHAREDMEMORY_H_ */

