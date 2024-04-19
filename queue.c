#include "queue.h"

int pop(int queue[]) {
    int returnValue = -1;
    if (queue[0] == -1) {
        return -1;
    } else {
        returnValue = queue[0];
        for (int i = 0; i < MAX_PROCESS_CONTROL_BLOCKS - 1; i++) {
            queue[i] = queue[i + 1];
        }
        queue[MAX_PROCESS_CONTROL_BLOCKS - 1] = -1;
    }
    return returnValue;
}

int peek(int queue[]) {
    return queue[0];
} // Added missing closing brace here

void push_back(int queue[], int pushValue) {
    for (int i = 0; i < MAX_PROCESS_CONTROL_BLOCKS; i++) {
        if (queue[i] < 0) {
            queue[i] = pushValue;
            break;
        }
    }
}

void initialize(int queue[]) {
    for (int i = 0; i < MAX_PROCESS_CONTROL_BLOCKS; i++) {
        queue[i] = -1;
    }
}

void printQueue(int queue[]) {
    for (int i = 0; i < MAX_PROCESS_CONTROL_BLOCKS; i++) {
        printf("%d,", queue[i]);
    }
    printf("\n");
}
