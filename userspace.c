#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

void movement_tracker();

int main() {
    int user_choice;

    while (1) {
        printf("----- USB Mouse Driver Menu -----\n");
        printf("1. Click Counter\n");
        printf("2. Movement Tracker\n");
        printf("3. Exit\n");
        printf("Please enter your choice: ");

        // Invalid user input handling
        if (scanf("%d", &user_choice) != 1) {
            printf("Invalid input. Please enter a number between 1-3!\n");
            while (getchar() != '\n');
            continue;
        }

        switch (user_choice) {
            case 1:
                break;
            
            case 2:
                // movement_tracker();
                break;

            case 3:
                printf("Exiting USB Mouse Driver Menu...\n");
                exit(0);
               
            default:
            printf("Invalid input. Please enter a number between 1-3!\n");   
        }
    }
    return 0;
}

// Functionality 1: Track mouse movements in terms of X-Y coordinates
// void movement_tracker() {
//     int file_descriptor;
//     signed char buffer[4];
//     int x = 0, y = 0, scroll_total = 0;

//     // Open USB mouse device file for read only operation
//     file_descriptor = open("/dev/usbmouse", O_RDONLY);
//     if (file_descriptor == -1) {
//         perror("Error opening USB mouse device file");
//         return;
//     }

//     printf("Tracking mouse movements... (Press Ctrl + C to stop tracking)\n");

//     while (1) {
//         ssize_t bytes_read = read(file_descriptor, buffer, sizeof(buffer));
//         if (bytes_read == 4) {
//             signed char dx = buffer[1];
//             signed char dy = buffer[2];
//             signed char scroll = buffer[3];

//             x += dx;
//             y += dy;
//             scroll_total += scroll;

//             printf("Your mouse moved by: %d in X and %d in Y.| End position (x, y): %d, %d | "
//                 "Total scroll (positive for up & negative for down): %d\n", dx, dy, x, y, scroll_total);
//         } else if (bytes_read == -1) {
//             perror("Error reading from USB mouse device");
//             break;
//         } else {
//             printf("Unexpected data size read: %zd bytes. Expected 4 bytes.\n", bytes_read);
//             break;
//         }
//     }
//     close(file_descriptor);
//     printf("Mouse movement tracking stopped...\n");
// }