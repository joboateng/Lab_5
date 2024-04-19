#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "SM.h"
#include "time.h"
#include "queue.h"

#define DEBUG 0							
#define VERBOSE 0						
#define TUNING 0						


const int maxChildProcessCount = 100; 
const long maxWaitInterval = 500; 

int childProcessCount = 0; 
int dispatchedProcessCount = 0; 
int totalChildProcessCount = 0; 
int signalIntercepted = 0; 
int childrenDispatching = 0; 
int ossSeconds; 
int ossUSeconds; 
int quantum = 100000; 
char timeVal[30]; 
long timeStarted = 0; 
long timeToStop = 0; 
int lastSignalPid = 0;
int signalRetries = 0;
int resourceRequests = 0;
int resourcesGranted = 0;
int resourcesQueued = 0;
int resourceReleases = 0;
int pcbMap[MAX_PROCESS_CONTROL_BLOCKS]; 

long long totalTurnaroundTime; 
long long totalWaitTime;
int totalProcesses;
long long totalCpuIdleTime;


FILE *logFile;

SmStruct shmMsg;
SmStruct *p_shmMsg;
sem_t *sem;

pid_t childpids[5000];

void signal_handler(int signalIntercepted);
void increment_clock(int offset);
void increment_clock_values(int seconds, int uSeconds, int offset);
void kill_detach_destroy_exit(int status);
void printAllocatedResourceMap();

int pcbMapNextAvailableIndex();
void pcbAssign(int pcbMap[], int index, int pid);
void pcbDelete(int pcbMap[], int index);
int pcbFindIndex(int pid);
void pcbUpdateStats(int pcbIndex);
void pcbAssignQueue(int priQueues[3][MAX_PROCESS_CONTROL_BLOCKS],
		int priQueueQuantums[], int pcbIndex);
void pcbUpdateTotalStats(int pcbIndex);
void pcbDisplayTotalStats();
void checkResourceRequestQueue();
int countAllocatedResourcesFromPcbs(int resource);

int findAvailableResource(int resource);
void enqueueResourceRequest(int pid, int resource);
void checkForDeadlocks();
void killProcess(int pid);


int main(int argc, char *argv[]) {

	int opt;
        pid_t childpid;
        int maxConcSlaveProcesses = 18;
        int maxOssTimeLimitSeconds = 10000;
        char logFileName[50];
        strncpy(logFileName, "log.out", sizeof(logFileName));
        int totalRunSeconds = 2;
        int goClock = 0;

        time_t t;
        srand(getpid());
        int interval = (rand() % maxWaitInterval);
        int nextChildTimeSeconds;
        int nextChildTimeUSeconds;


	
	while ((opt = getopt(argc, argv, "hl:q:s:t:")) != -1) {
		switch (opt) {
		case 'l': 
			strncpy(logFileName, optarg, sizeof(logFileName));
			if (DEBUG)
				printf("opt l detected: %s\n", logFileName);
			break;
		case 'q': 
			quantum = atoi(optarg);
			if (DEBUG)
				printf("opt q detected: %d\n", quantum);
			break;
		case 's': 
			maxConcSlaveProcesses = atoi(optarg);
			if (DEBUG)
				printf("opt s detected: %d\n", maxConcSlaveProcesses);
			break;
		case 't': 
			totalRunSeconds = atoi(optarg);
			if (DEBUG)
				printf("opt t detected: %d\n", totalRunSeconds);
			break;
		case 'h': 
			if (DEBUG)
				printf("opt h detected\n");
			fprintf(stderr, "Usage: ./%s <arguments>\n", argv[0]);
			break;
		default:
			break;
		}
	}

	if (argc < 1 || opt == 'h') { fprintf(stderr, "Usage: %s command arg1 arg2 ...\n", argv[0]);
		exit(1);
	}

	
	logFile = fopen(logFileName, "w+");

	if (logFile == NULL) {
		perror("Cannot open log file");
		exit(1);
	}

	
	getTime(timeVal);
	if (DEBUG)
		printf("\n\nOSS  %s: create shared memory\n", timeVal);

	
	int shmid;
	if ((shmid = shmget(SHM_MSG_KEY, SHMSIZE, IPC_CREAT | 0660)) == -1) {
		fprintf(stderr, "sharedMemory: shmget error code: %d", errno);
		perror("sharedMemory: Creating shared memory segment failed\n");
		exit(1);
	}
	p_shmMsg = &shmMsg;
	p_shmMsg = shmat(shmid, NULL, 0);

	p_shmMsg->ossSeconds = 0;
	p_shmMsg->ossUSeconds = 0;

	
	for (int i = 0; i < MAX_PROCESS_CONTROL_BLOCKS; i++)
		pcbDelete(pcbMap, i);

	for (int i = 0; i < MAX_RESOURCE_REQUEST_COUNT; i++) {
		p_shmMsg->resourceRequestQueue[i][0] = 0;
		p_shmMsg->resourceRequestQueue[i][1] = 0;
	}

	for (int i = 0; i < MAX_RESOURCE_COUNT; i++) {
		p_shmMsg->resourcesGrantedCount[i] = 0;
	}

	
	sem = open_semaphore(1);

	
	signal(SIGINT, signal_handler);

	getTime(timeVal);
	if (DEBUG && VERBOSE) printf("OSS  %s: entering main loop\n", timeVal);

	struct timespec timeperiod;
	timeperiod.tv_sec = 0;
	timeperiod.tv_nsec = 5 * 10000;

	
	while (1) {

		
		nanosleep(&timeperiod, NULL);

		
		if (signalIntercepted) { 
			printf("\nmaster: //////////// oss terminating children due to a signal! //////////// \n\n");
			printf("Master: parent terminated due to a signal!\n\n");

			kill_detach_destroy_exit(130);
		}

		
		if (totalChildProcessCount >= maxChildProcessCount 
		|| ossSeconds >= maxOssTimeLimitSeconds || 
				(timeToStop != 0 && timeToStop < getUnixTime())) { 

			char typeOfLimit[50];
			strncpy(typeOfLimit, "", 50);
			if (totalChildProcessCount >= maxChildProcessCount)
				strncpy(typeOfLimit, "because of process limit", 50);
			if (ossSeconds > maxOssTimeLimitSeconds)
				strncpy(typeOfLimit, "because of OSS time limit", 50);
			if (timeToStop != 0 && timeToStop < getUnixTime())
				strncpy(typeOfLimit, "because of real time limit (2s)", 50);

			getTime(timeVal);
			printf(
					"\nOSS  %s: Halting %s.\nTotal Processes Spawned: %d\nTotal Processes Reporting Time: %d\nOSS Seconds(sec): %d.%09d\nStop Time(unix):    %ld\nCurrent Time(unix): %ld\n",
					timeVal, typeOfLimit, totalChildProcessCount,
					totalChildProcessCount, ossSeconds, ossUSeconds, timeToStop,
					getUnixTime());

			fprintf(logFile,
					"\nOSS  %s: Halting %s.\nTotal Processes Spawned: %d\nTotal Processes Reporting Time: %d\nOSS Seconds(sec): %d.%09d\nStop Time(unix):    %ld\nCurrent Time(unix): %ld\n",
					timeVal, typeOfLimit, totalChildProcessCount,
					totalChildProcessCount, ossSeconds, ossUSeconds, timeToStop,
					getUnixTime());

			pcbDisplayTotalStats();

			kill_detach_destroy_exit(0);
		}

		
		if (childpid != 0 && goClock) {
			if (timeToStop == 0) {
				
				struct timespec timeperiod;
				timeperiod.tv_sec = 0;
				timeperiod.tv_nsec = 50 * 1000 * 1000;
				nanosleep(&timeperiod, NULL);

				timeStarted = getUnixTime();
				timeToStop = timeStarted + (1000 * totalRunSeconds);
				getTime(timeVal);
				if (TUNING) printf("OSS  %s: OSS starting clock.  Real start time: %ld  Real stop time: %ld\n",
							timeVal, timeStarted, timeToStop);
			}

			increment_clock(quantum);

			
			if (dispatchedProcessCount < 1)
				totalCpuIdleTime += (long) quantum;
		}

		getTime(timeVal);
		if (0 && DEBUG && VERBOSE) printf("OSS  %s: CHILD PROCESS COUNT: %d\nMAX CONC PROCESS COUNT: %d\n",
					timeVal, childProcessCount, maxConcSlaveProcesses);

		
		if (childProcessCount >= maxConcSlaveProcesses) {
			goClock = 1; 

			
			if (p_shmMsg->userPid == 0) { 

				if (ossUSeconds > 0 && ossUSeconds % (quantum * 500) == 0) 
					checkResourceRequestQueue(); 

				if (ossUSeconds > 0 && ossUSeconds % (quantum * 1000) == 0) 
					checkForDeadlocks();

				continue; 
			}

		      
			int userPid = p_shmMsg->userPid;

			getTime(timeVal);
			if (DEBUG && VERBOSE)
				if (p_shmMsg->userGrantedResource == 0) 
					printf("OSS  %s: OSS has detected child %d has sent a signal (userHalt:%d, requestOrRelease:%d, userResource:%d, userGrantedResource:%d) at my time %d.%09d\n",
							timeVal, p_shmMsg->userPid, p_shmMsg->userHaltSignal, p_shmMsg->userRequestOrRelease, p_shmMsg->userResource, p_shmMsg->userGrantedResource, ossSeconds, ossUSeconds);

			int pcbIndex = pcbFindIndex(p_shmMsg->userPid); 

			getTime(timeVal); 
			if (p_shmMsg->userHaltSignal == 1) { 
				if (DEBUG) printf("OSS  %s: Child %d is terminating at my time %d.%09d\n\n", timeVal, p_shmMsg->userPid, ossSeconds, ossUSeconds);

				
				pcbUpdateStats(pcbIndex);
				pcbUpdateTotalStats(pcbIndex);
				pcbDelete(pcbMap, pcbIndex);
				dispatchedProcessCount--; 
				childProcessCount--; 

				
				p_shmMsg->userPid = 0;
				p_shmMsg->userHaltSignal = 0;
				p_shmMsg->userHaltTime = 0;
				p_shmMsg->userRequestOrRelease = 0;
				p_shmMsg->userResource = 0;

			} else if (p_shmMsg->userRequestOrRelease != 0) {
				if (p_shmMsg->userRequestOrRelease == 1) { 
					if (DEBUG) printf("OSS  %s: Child %d is requesting a resource %d at my time %d.%09d\n",
							timeVal, p_shmMsg->userPid, p_shmMsg->userResource, ossSeconds, ossUSeconds);

					fprintf(logFile, "OSS  %s: Child %d is requesting a resource %d at my time %d.%09d\n",
							timeVal, p_shmMsg->userPid, p_shmMsg->userResource, ossSeconds, ossUSeconds);

					resourceRequests++;

					int resourceValue = findAvailableResource(p_shmMsg->userResource);

					
					if (resourceValue && countAllocatedResourcesFromPcbs(p_shmMsg->userResource) < MAX_RESOURCE_QTY) {
						p_shmMsg->userGrantedResource = resourceValue;
						p_shmMsg->userPid = userPid;
						resourcesGranted++; 

						if (resourcesGranted % 20 == 0)
							printAllocatedResourceMap();

						getTime(timeVal);
						if (DEBUG) printf("OSS  %s: Child %d has been granted resource %d at my time %d.%09d\n",
								timeVal, p_shmMsg->userPid, p_shmMsg->userGrantedResource, ossSeconds, ossUSeconds);

						fprintf(logFile, "OSS  %s: Child %d has been granted resource %d at my time %d.%09d\n",
								timeVal, p_shmMsg->userPid, p_shmMsg->userGrantedResource, ossSeconds, ossUSeconds);

					} else { 
						enqueueResourceRequest(p_shmMsg->userPid, p_shmMsg->userResource);
						resourcesQueued++;

						if (DEBUG) printf("OSS  %s: Child %d resource request for %d has been queued at my time %d.%09d\n",
								timeVal, p_shmMsg->userPid, p_shmMsg->userResource, ossSeconds, ossUSeconds);

						fprintf(logFile, "OSS  %s: Child %d resource request for %d has been queued at my time %d.%09d\n",
								timeVal, p_shmMsg->userPid, p_shmMsg->userResource, ossSeconds, ossUSeconds);

						p_shmMsg->userGrantedResource = 0;
						p_shmMsg->userPid = 0;
					}

				    
					p_shmMsg->userHaltSignal = 0;
					p_shmMsg->userHaltTime = 0;
					p_shmMsg->userRequestOrRelease = 0;
					p_shmMsg->userResource = 0;


				} else if (p_shmMsg->userRequestOrRelease == 2) { 
					if (DEBUG) printf("OSS  %s: Child %d is releasing a resource %d at my time %d.%09d\n",
							timeVal, p_shmMsg->userPid, p_shmMsg->userResource, ossSeconds, ossUSeconds);

					fprintf(logFile, "OSS  %s: Child %d is releasing a resource %d at my time %d.%09d\n",
							timeVal, p_shmMsg->userPid, p_shmMsg->userResource, ossSeconds, ossUSeconds);

					resourceReleases++;
					p_shmMsg->userPid = 0;
					p_shmMsg->userHaltSignal = 0;
					p_shmMsg->userHaltTime = 0;
					p_shmMsg->userRequestOrRelease = 0;
					p_shmMsg->userResource = 0;
				}
			} else if (p_shmMsg->userGrantedResource == 0) {
				lastSignalPid = p_shmMsg->userPid;
				signalRetries++;
				if (signalRetries > 4) { 
					signalRetries = 0;
					p_shmMsg->userPid = 0;
					p_shmMsg->userHaltSignal = 0;
					p_shmMsg->userHaltTime = 0;
					p_shmMsg->userRequestOrRelease = 0;
					p_shmMsg->userResource = 0;
					p_shmMsg->userGrantedResource = 0;

					getTime(timeVal);
					if (DEBUG) printf("OSS  %s: message from child %d (no status determined) has been reset at my time %d.%09d\n",
							timeVal, p_shmMsg->userPid, ossSeconds, ossUSeconds);
				}
			}

//			}

		}

		getTime(timeVal);
		if (DEBUG && VERBOSE)
			printf("OSS  %s: Process %d CHILD PROCESS COUNT: %d\n", timeVal,
					getpid(), childProcessCount);

		if (childProcessCount < maxConcSlaveProcesses) {

			if (goClock && nextChildTimeSeconds >= ossSeconds
					&& nextChildTimeUSeconds > ossUSeconds) { 
				continue;
			}

			int assignedPcb = pcbMapNextAvailableIndex(pcbMap); 
			if (assignedPcb == -1) 
				continue;

			getTime(timeVal);
			if (DEBUG && VERBOSE)
				printf(
						"OSS  %s: Child (fork #%d from parent) has been assigned pcb index: %d\n",
						timeVal, totalChildProcessCount, assignedPcb);

			char iStr[1]; 
			sprintf(iStr, " %d", totalChildProcessCount);

			char assignedPcbStr[2]; 
			sprintf(assignedPcbStr, " %d", assignedPcb);

			childpid = fork(); 

			
			if (childpid == -1) {
				perror("master: Failed to fork");
				kill_detach_destroy_exit(1);
				return 1;
			}

			
			if (childpid == 0) {

				getTime(timeVal);
				if (DEBUG)
					printf(
							"OSS  %s: Child %d (fork #%d from parent) will attempt to execl user\n",
							timeVal, getpid(), totalChildProcessCount);

				int status = execl("./user", iStr, assignedPcbStr, NULL);

				getTime(timeVal);
				if (status)
					printf(
							"OSS  %s: Child (fork #%d from parent) has failed to execl user error: %d\n",
							timeVal, totalChildProcessCount, errno);

				perror("OSS: Child failed to execl() the command");
				return 1;
			}

			
			if (childpid != 0) {

				pcbAssign(pcbMap, assignedPcb, childpid); 

				childpids[totalChildProcessCount] = childpid; 
				childProcessCount++; 
				totalChildProcessCount++; 
				increment_clock_values(nextChildTimeSeconds,
						nextChildTimeUSeconds, (rand() % maxWaitInterval));

				getTime(timeVal);
				if (DEBUG && VERBOSE)
					printf("OSS  %s: Process %d CHILD PROCESS COUNT: %d\n",
							timeVal, getpid(), childProcessCount);

				getTime(timeVal);

			}

		}

	} 

	fclose(logFile);

	kill_detach_destroy_exit(0);

	return 0;
}

// remove newline characters from palinValues
void trim_newline(char *string) {
	string[strcspn(string, "\r\n")] = 0;
}

// handle the ^C interrupt
void signal_handler(int signal) {
	if (DEBUG) printf("\nmaster: //////////// Encountered signal! //////////// \n\n");
	signalIntercepted = 1;
}

void increment_clock(int offset) {
	const int oneBillion = 1000000000;

	ossUSeconds += offset;

	if (ossUSeconds >= oneBillion) {
		ossSeconds++;
		ossUSeconds -= oneBillion;
	}

	if (0 && DEBUG && VERBOSE) printf("master: updating oss clock to %d.%09d\n", ossSeconds, ossUSeconds);
	p_shmMsg->ossSeconds = ossSeconds;
	p_shmMsg->ossUSeconds = ossUSeconds;

}

void increment_clock_values(int seconds, int uSeconds, int offset) {
	const int oneBillion = 1000000000;

	int localOssSeconds = ossSeconds;
	int localOssUSeconds = ossUSeconds;

	localOssUSeconds += offset;

	if (localOssUSeconds >= oneBillion) {
		localOssSeconds++;
		localOssUSeconds -= oneBillion;
	}

	seconds = localOssSeconds;
	uSeconds = localOssUSeconds;

	if (0 && DEBUG && VERBOSE) printf("master: updating clock values by %d ms to %d.%09d\n", offset, ossSeconds, ossUSeconds);
}

void kill_detach_destroy_exit(int status) {
      
	for (int p = 0; p < totalChildProcessCount; p++) {
		if (DEBUG) printf("Master: //////////// oss terminating child process %d //////////// \n",
					(int) childpids[p]);
		kill(childpids[p], SIGTERM);
	}

      
	shmdt(p_shmMsg);
	shmctl(SHM_MSG_KEY, IPC_RMID, NULL);

      
	sem_unlink(SEM_NAME);
	close_semaphore(sem);
	sem_destroy(sem);

	if (status == 0)
		printf("Master: Parent terminated normally \n\n");

	exit(status);
}

int pcbMapNextAvailableIndex(int pcbMap[]) {
	for (int i = 0; i < MAX_PROCESS_CONTROL_BLOCKS; i++) {
		if (!pcbMap[i])
			return i;
	}
	return -1;
}
void pcbAssign(int pcbMap[], int index, int pid) {
	pcbMap[index] = 1;
	p_shmMsg->pcb[index].pid = pid;

	getTime(timeVal);
	if (DEBUG) printf("OSS  %s: Assigning child %d pcb at %d.%09d\n", timeVal, p_shmMsg->pcb[index].pid, ossSeconds, ossUSeconds);
}

void pcbDelete(int pcbMap[], int index) {
	getTime(timeVal);
	if (DEBUG)
		printf("OSS  %s: Deleting child %d pcb at %d.%09d\n", timeVal, p_shmMsg->pcb[index].pid, ossSeconds, ossUSeconds);

	pcbMap[index] = 0;
	p_shmMsg->pcb[index].lastBurstLength = 0;
	p_shmMsg->pcb[index].pid = 0;
	p_shmMsg->pcb[index].processPriority = 0;
	p_shmMsg->pcb[index].totalCpuTime = 0;
	p_shmMsg->pcb[index].totalTimeInSystem = 0;
}

int pcbFindIndex(int pid) {
	for (int i = 0; i < MAX_PROCESS_CONTROL_BLOCKS; i++) {
		if (p_shmMsg->pcb[i].pid == pid) {
			if (DEBUG && VERBOSE) printf("OSS  %s: found pcbIndex: %d\n", timeVal, i);
			return i;
		}
	}
	return -1;
}

void pcbUpdateStats(int pcbIndex) {
	p_shmMsg->pcb[pcbIndex].lastBurstLength = p_shmMsg->userHaltTime;
	p_shmMsg->pcb[pcbIndex].totalCpuTime += p_shmMsg->userHaltTime;
	p_shmMsg->pcb[pcbIndex].totalTimeInSystem = 0;

}



void pcbUpdateTotalStats(int pcbIndex) {
	totalProcesses++;

    
	long long totalSeconds = abs(p_shmMsg->pcb[pcbIndex].endUserSeconds - p_shmMsg->pcb[pcbIndex].startUserSeconds);
	long long totalUSeconds = abs(p_shmMsg->pcb[pcbIndex].endUserUSeconds - p_shmMsg->pcb[pcbIndex].startUserUSeconds);
	totalTurnaroundTime += ((totalSeconds * 1000 * 1000 * 1000) + totalUSeconds);

	
	totalWaitTime += (totalTurnaroundTime - p_shmMsg->pcb[pcbIndex].totalCpuTime);
}

void pcbDisplayTotalStats() {
	if (DEBUG) printf("Total Turnaround Time(usec): %lli\nTotal Wait Time(usec): %lli\nTotal Processes: %d\nCPU Idle Time(usec): %lli\n",
				totalTurnaroundTime,
				totalWaitTime, totalProcesses, totalCpuIdleTime);
	printf("Average Turnaround Time(usec): %lli\nAverage Wait Time(usec): %lli\nCPU Idle Time(usec): %lli\n",
			totalTurnaroundTime / totalProcesses,
			totalWaitTime / totalProcesses, totalCpuIdleTime);
	printf("Resources Requested: %d\nResources Granted: %d\nResource Requests Queued: %d\nResources Released: %d\n\n",
				resourceRequests, resourcesGranted, resourcesQueued, resourceReleases);
	fprintf(logFile,"Average Turnaround Time(usec): %lli\nAverage Wait Time(usec): %lli\nCPU Idle Time(usec): %lli\n\n",
			totalTurnaroundTime / totalProcesses,
			totalWaitTime / totalProcesses, totalCpuIdleTime);
}

int findAvailableResource(int resource) {
	int resourceIndex = resource - 1;
	int value = p_shmMsg->resourcesGrantedCount[resourceIndex];

	if (p_shmMsg->resourcesGrantedCount[resourceIndex] < MAX_RESOURCE_QTY) {
		p_shmMsg->resourcesGrantedCount[resourceIndex]++;
		getTime(timeVal);
		if (DEBUG) printf("OSS  %s: found resource %d: value: %d at %d.%09d\n", timeVal, resource, (resource * 100) + value, ossSeconds, ossUSeconds);
		int returnValue = (resource * 100) + value;
		if (returnValue > 0)
			return returnValue;
	}
	return 0;
}

void enqueueResourceRequest(int pid, int resource) {
	for (int i = 0; i < MAX_RESOURCE_REQUEST_COUNT; i++) {
		if (p_shmMsg->resourceRequestQueue[i][0] == 0) { 						
			p_shmMsg->resourceRequestQueue[i][0] = pid;							
			p_shmMsg->resourceRequestQueue[i][1] = resource;
		}
	}
}

void checkResourceRequestQueue() {
	getTime(timeVal);
	if (DEBUG) printf("OSS  %s: reviewing resource queue for resource availability at my time %d.%09d\n",
			timeVal, ossSeconds, ossUSeconds);

	fprintf(logFile, "OSS  %s: reviewing resource queue for resource availability at my time %d.%09d\n",
			timeVal, ossSeconds, ossUSeconds);

	for (int i = 0; i < MAX_RESOURCE_REQUEST_COUNT; i++) { 		
		if (p_shmMsg->resourceRequestQueue[i][0] == 0) {						
			printf("queue check made it to %d\n", i);
			break;
		}

		int resource = p_shmMsg->resourceRequestQueue[i][1]; 	
		if (p_shmMsg->resourcesGrantedCount[resource - 1] < MAX_RESOURCE_QTY && countAllocatedResourcesFromPcbs(resource) < MAX_RESOURCE_QTY) { 
			int index = p_shmMsg->resourcesGrantedCount[resource - 1]++;
			p_shmMsg->userGrantedResource = (p_shmMsg->resourceRequestQueue[i][1] * 100) + index;
			p_shmMsg->userPid = p_shmMsg->resourceRequestQueue[i][0];

			getTime(timeVal);
			if (DEBUG) printf("OSS  %s: a review of the resource queue has granted child %d resource %d at my time %d.%09d\n",
					timeVal, p_shmMsg->userPid, p_shmMsg->userGrantedResource, ossSeconds, ossUSeconds);

			fprintf(logFile, "OSS  %s: a review of the resource queue has granted child %d resource %d at my time %d.%09d\n",
					timeVal, p_shmMsg->userPid, p_shmMsg->userGrantedResource, ossSeconds, ossUSeconds);

			for (int j = i; j < MAX_RESOURCE_REQUEST_COUNT - 1; j++) { 
				p_shmMsg->resourceRequestQueue[j][0] = p_shmMsg->resourceRequestQueue[j + 1][0];
				p_shmMsg->resourceRequestQueue[j][1] = p_shmMsg->resourceRequestQueue[j + 1][1];
				 if (p_shmMsg->resourceRequestQueue[j + 1][0] == 0) 
					 break;
			}
		}
	}
}

void printAllocatedResourceMap () {
	printf("\n\n        RESOUCE ALLOCATION GRAPH\n");
	printf("  pid  R1 R2 R3 R4 R5 R6 R7 R8 R9 R10\n");
	for (int i = 0; i < MAX_PROCESS_CONTROL_BLOCKS; i++) {
		if (p_shmMsg->pcb[i].pid != 0) {
			printf("%6d ", p_shmMsg->pcb[i].pid);
			for (int j = 0; j < MAX_RESOURCE_COUNT; j++) {
				int resourceType = (int) (p_shmMsg->pcb[i].resources[j] / 100);
				printf("%2d ", resourceType);
			}
		}
		printf("\n");
	}
}

int countAllocatedResourcesFromPcbs(int resource) {
	int total = 0;
	for (int i = 0; i < MAX_PROCESS_CONTROL_BLOCKS; i++) {
		int resourceType = (int) (p_shmMsg->pcb[i].resources[resource - 1] / 100);
		if (resourceType == resource)
			total++;
	}
	return total;
}

void checkForDeadlocks() {
	getTime(timeVal);
	printf("\n\nOSS  %s: running deadlock detection at my time %d.%09d\n\n", timeVal, ossSeconds, ossUSeconds);

	int dlResourcesAllocated[MAX_RESOURCE_COUNT];
	int dlResourcesAvailable[MAX_RESOURCE_COUNT];
	int dlResourcesRequested[MAX_RESOURCE_COUNT];
	int dlResourcePids[MAX_PROCESS_CONTROL_BLOCKS];
	int dlResourceRequestCounts[MAX_PROCESS_CONTROL_BLOCKS][MAX_RESOURCE_COUNT];
	int dlProcessCanComplete[MAX_PROCESS_CONTROL_BLOCKS];
	int dlResourceInContention[MAX_RESOURCE_COUNT];
	int dlProcessesList[MAX_PROCESS_CONTROL_BLOCKS];
	int dlCount = 0;

      
	for (int i = 0; i < MAX_RESOURCE_COUNT; i++) {
		dlResourcesAllocated[i] = p_shmMsg->resourcesGrantedCount[i];
		dlResourcesAvailable[i] = MAX_RESOURCE_QTY - p_shmMsg->resourcesGrantedCount[i];
		dlResourcesRequested[i] = 0;
		dlResourcePids[i] = 0;
		for (int j = 0; j < MAX_PROCESS_CONTROL_BLOCKS; j++) {

			dlResourceRequestCounts[j][i] = 0;
		}
		dlProcessCanComplete[i] = 1;
		dlResourceInContention[i] = 0;
		dlProcessesList[i] = 0;
	}

	
	for (int i = 0; i < MAX_RESOURCE_REQUEST_COUNT; i++) {
		int resourceType = p_shmMsg->resourceRequestQueue[i][1];
		if (resourceType < 1)
			break;
		dlResourcesRequested[resourceType]++;
	}

	
	for (int i = 0; i < MAX_RESOURCE_REQUEST_COUNT; i++) {
		if (p_shmMsg->resourceRequestQueue[i][0] == 0) {
			break;
		}

		int pidExists = -1;
		int pidListEnds = -1;

	      
		for (int j = 0; j < MAX_PROCESS_CONTROL_BLOCKS; j++) {
			if (dlResourcePids[j] == p_shmMsg->resourceRequestQueue[i][0]) {
				pidExists = j;
			} else if (dlResourcePids[j] == 0) {
				pidListEnds = j;
				break;
			}
		}

		if (pidExists > -1) {
			dlResourceRequestCounts[pidExists][p_shmMsg->resourceRequestQueue[i][1] - 1]++;
		} else { 
			dlResourcePids[pidListEnds] = p_shmMsg->resourceRequestQueue[i][0];
			dlResourceRequestCounts[pidListEnds][p_shmMsg->resourceRequestQueue[i][1] - 1]++;
		}
	}

	for (int i = 0; i < MAX_PROCESS_CONTROL_BLOCKS; i++) {
		for (int j = 0; j < MAX_RESOURCE_COUNT; j++) {
			if (dlResourcesAvailable[j] < dlResourceRequestCounts[i][j]) {
				dlProcessCanComplete[i] = 0;
				dlResourceInContention[j] = 1;
			}
		}
	}

	for (int i = 0, j = 0; i < MAX_PROCESS_CONTROL_BLOCKS; i++) {
		if (!dlProcessCanComplete[i]) {
			dlCount++;
			dlProcessesList[j++] = dlResourcePids[i];
			getTime(timeVal);
			printf("OSS  %s: process %d is deadlocked at  %d.%09d\n", timeVal, dlResourcePids[i], ossSeconds, ossUSeconds);
		}
	}

	for (int i = 0; i < dlCount; i++) {
		if (dlProcessesList[i] != 0) {
			killProcess(dlProcessesList[i]);
		} else {
			break;
		}
	}
}

void killProcess(int pid) {
	int pcbIndex = pcbFindIndex(pid);

	kill(pid, SIGTERM);
	pcbDelete(pcbMap, pcbIndex);

	dispatchedProcessCount--; 
	childProcessCount--; 

	getTime(timeVal);
	printf("OSS  %s: process %d has been terminated due to a deadlock at  %d.%09d\n", timeVal, pid, ossSeconds, ossUSeconds);
}

