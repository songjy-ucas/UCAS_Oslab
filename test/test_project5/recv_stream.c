#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define CHUNK_SIZE 1024

/* 
 * Fletcher-16 Checksum Algorithm
 * Used to verify data integrity against the sender.
 */
static uint16_t fletcher16_step(uint16_t current_sum, const uint8_t *data, int len)
{
    uint16_t sum1 = current_sum & 0xFF;
    uint16_t sum2 = (current_sum >> 8) & 0xFF;

    for (int i = 0; i < len; ++i) {
        sum1 = (sum1 + data[i]) % 255;
        sum2 = (sum2 + sum1) % 255;
    }
    return (sum2 << 8) | sum1;
}

int main(int argc, char *argv[])
{
    char buffer[CHUNK_SIZE];
    int nbytes;

    // Reset the reliable layer to ensure we start fresh
    sys_net_reset();

    int total_bytes = 0;
    uint16_t checksum = 0;
    int iteration = 0;

    int print_location = 1; // Line number to print output

    // Clear screen area
    sys_move_cursor(0, print_location);
    printf("[RECV STREAM] Starting Reliable Data Reception...\n");

    while (1) {
        // 1. Request data from the kernel
        // Note: nbytes is Input (max capacity) and Output (received size)
        nbytes = CHUNK_SIZE; 

        // This syscall blocks until data arrives or an error occurs
        sys_net_recv_stream(buffer, &nbytes);

        if (nbytes > 0) {
            // 2. Update Statistics
            total_bytes += nbytes;

            // 3. Update Checksum
            checksum = fletcher16_step(checksum, (uint8_t *)buffer, nbytes);

            // 4. Print Status (every ~10KB to avoid spamming I/O)
            if (iteration++ % 10 == 0) {
                sys_move_cursor(0, print_location + 1);
                // Clear the line with spaces
                printf("                                                               \n"); 
                printf("[RECV STREAM] Total: %d bytes | Checksum: 0x%04x | Last Chunk: %d\n", 
                       total_bytes, checksum, nbytes);

                // Print a snippet of the data (optional)
                sys_move_cursor(0, print_location + 2);
                printf("Data: ");
                for(int i=0; i < (nbytes > 10 ? 10 : nbytes); i++) {
                    // Print as char if printable, else hex
                    char c = buffer[i];
                    if (c >= 32 && c <= 126) printf("%c", c);
                    else printf(".");
                }
                printf("...   \n");
            }
        } else {
            // Should theoretically block, but if it returns 0 unexpectedly:
            // sys_yield(); 
        }
    }

    return 0;
}