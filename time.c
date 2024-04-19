#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#define DEBUG 0

void getTime(char* buffer) {
    int millisec;
    struct tm* tm_info;
    struct timeval tv;

    gettimeofday(&tv, NULL);

    millisec = (int)(tv.tv_usec / 1000.0); // Round to nearest millisec

    if (millisec >= 1000) { // Allow for rounding up to nearest second
        millisec -= 1000;
        tv.tv_sec++;
    }
    char mils[4]; // Changed size to 4 to accommodate 3 digits plus null terminator
    snprintf(mils, sizeof(mils), "%03d", millisec); // Use snprintf for safety

    tm_info = localtime(&tv.tv_sec);

    if (strftime(buffer, 27, "%Y-%m-%d %H:%M:%S.", tm_info) == 0) {
        // Error handling for strftime failure
        fprintf(stderr, "Error formatting time.\n");
        buffer[0] = '\0'; // Empty string in case of error
        return;
    }
    strcat(buffer, mils);
    if (DEBUG) fprintf(stderr, "Timestamp: %s\n", buffer); // Print to stderr if DEBUG is set
}

long getUnixTime() {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == -1) {
        // Error handling for gettimeofday failure
        fprintf(stderr, "Error getting UNIX time.\n");
        return -1;
    }
    return (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000;
}

