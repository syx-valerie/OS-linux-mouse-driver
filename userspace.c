#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <termios.h>
#include <sys/select.h>
#include <errno.h>

void movement_tracker_menu(void);
void set_raw_mode(int enable);
void click_counter_menu(void);

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
                click_counter_menu();
                break;

            case 2:
                movement_tracker_menu();
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

// Functionality: Mouse left-click counter, includes viewing click count, resetting counter, stopping and resuming count
void click_counter_menu() {
    int fd = open("/dev/usb_mouse_clicks", O_RDWR);

    if (fd < 0) {
        perror("Failed to open /dev/usb_mouse_clicks");
        return;
    }

    int choice;

    while (1) {
        printf("\n-- Click Counter --\n");
        printf("1. View Click Count\n");
        printf("2. Reset Counter\n");
        printf("3. Stop Counting\n");
        printf("4. Resume Counting\n");
        printf("5. Back to Main Menu\n");
        printf("Enter choice: ");

        if (scanf("%d", &choice) != 1) {
            printf("Invalid input.\n");
            while (getchar() != '\n'); // Clear input buffer
            continue;
        }

        switch (choice) {
            case 1: {
                char buffer[128] = {0};
                int len = read(fd, buffer, sizeof(buffer) - 1);

                if (len > 0) {
                    buffer[len] = '\0';
                    printf("[Click] %s\n", buffer);

                } else if (len == 0) {
                    printf("[Click] No clicks detected yet.\n");

                } else {
                    perror("Read failed");
                }

                break;

            }
            case 2:
                write(fd, "reset", 5);
                printf("Counter reset.\n");

                break;

            case 3:
                write(fd, "stop", 4);
                printf("Counting stopped.\n");

                break;

            case 4:
                write(fd, "start", 5);
                printf("Counting resumed.\n");

                break;

            case 5:
                close(fd);
                return;

            default:
                printf("Invalid choice.\n");

        }

    }

}

// Helper function to set terminal to raw mode to allow non-blocking and unbuffered keyboard input
void set_raw_mode(int enable) {
    static struct termios original_termios, new_termios;

    if (enable) {
        // Obtain current terminal settings
        tcgetattr(STDIN_FILENO, &original_termios);
        new_termios = original_termios;

        // Disable canonical mode (no line buffering) and echo (no echoing of input)
        new_termios.c_lflag &= ~(ICANON | ECHO);

        // Apply new terminal settings
        tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

        // Set standard input to non-blocking mode
        fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);  // Restore original terminal settings
        fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) & ~O_NONBLOCK);  // Unset non-blocking mode
    }
}

// Functionality: Track mouse movements in terms of X-Y coordinates, includes starting / stopping tracking and resetting position
void movement_tracker_menu() {
    // Open custom character device file for mouse movements
    int file_descriptor = open("/dev/usb_mouse_movements", O_RDWR | O_NONBLOCK);

    // Error handling in event file cannot be opened
    if (file_descriptor < 0) {
        perror("Failed to open /dev/usb_mouse_movements");
        return;
    }

    int tracking_enabled = 0;  // Flag for tracking mode (0: not tracking, 1: tracking enabled)
    signed char buffer[64];    // Buffer to store mouse movement data

    setvbuf(stdout, NULL, _IONBF, 0);  // Disable buffering for stdout

    while(1) {
        // Display movement tracker sub-menu
        printf("\n-- Movement Tracker --\n");
        printf("1. Start Tracking\n");
        printf("2. Reset Position\n");
        printf("3. Back to Main Menu\n");
        printf("Enter choice: ");

        int choice;
        // Handling of user input for meny selection with re-prompt on invalid input
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input.\n");
            while (getchar() != '\n'); // Clear input buffer
            continue;
        }

        switch (choice) {
            case 1:  // Start tracking mouse movements, sends "start" command to driver.c
                write(file_descriptor, "start", 5);
                set_raw_mode(1);  // Enable raw input mode
                printf("Started tracking mouse movements... (Press 'q' to stop tracking)\n");
                tracking_enabled = 1;

                while (tracking_enabled) {
                    fd_set fds;
                    FD_ZERO(&fds);
                    FD_SET(STDIN_FILENO, &fds);     // Monitor input for 'q' / 'Q' key press
                    FD_SET(file_descriptor, &fds);  // Monitor device file for mouse movement data
                    int max_fd = (file_descriptor > STDIN_FILENO ? file_descriptor : STDIN_FILENO) + 1;  

                    struct timeval timeout = {1, 0};  // 1 second timeout for select()

                    int ret = select(max_fd, &fds, NULL, NULL, &timeout);

                    if (ret < 0) {
                        perror("select error");
                        break;
                    } 

                    // Check for user input to stop tracking
                    if (FD_ISSET(STDIN_FILENO, &fds)) {
                        char ch;
                        if (read(STDIN_FILENO, &ch, 1) > 0 && (ch == 'q' || ch == 'Q')) {
                            tracking_enabled = 0;
                            write(file_descriptor, "stop", 4);  // Send "stop" command to driver.c
                            break;
                        }
                    } 

                    // Check for mouse movement data
                    if (FD_ISSET(file_descriptor, &fds)) {
                        ssize_t bytes_read = read(file_descriptor, buffer, sizeof(buffer) - 1);
                        if (bytes_read > 0) {
                            buffer[bytes_read] = '\0';  // Null-terminate buffer
                            printf("[Movement] %s", buffer);
                            usleep(100000);             // Sleep for 100ms to avoid flooding the output (delay)
                        } else if (bytes_read == 0){
                            usleep(200000);             // No data available, wait for 200ms
                        } else {
                            // Error handling for read()
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                usleep(200000);         // No data available, wait for 200ms
                            } else {
                            perror("Read error");
                            tracking_enabled = 0;
                            }
                        }
                    }
                }
            
                set_raw_mode(0);  // Reset terminal settings to original after tracking stops
                printf("Stopped tracking mouse movements.\n");
                break;

            case 2:  // Reset mouse position by sending "reset" command to driver.c
                write(file_descriptor, "reset", 5);
                printf("Position has been resetted.\n");
                break;

            case 3:  // Exit movement tracker sub-menu and return to main menu
                (file_descriptor);
                printf("Returning to main menu...\n");
                return;
            
            default:
                printf("Invalid choice.\n");
            }
        }

    close(file_descriptor);  // Close device file before exiting
}