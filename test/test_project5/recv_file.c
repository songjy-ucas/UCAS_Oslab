#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define CHUNK_SIZE 1400 // Slightly less than MTU

/* 
 * Fletcher-16 Checksum Algorithm
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

    // File transfer state
    uint32_t file_size = 0;
    uint32_t total_received_data = 0;
    int header_parsed = 0;
    uint16_t checksum = 0;
    int print_location = 1;

    sys_move_cursor(0, print_location);
    printf("[RECV FILE] Waiting for file transmission...\n");

    while (1) {
        nbytes = CHUNK_SIZE;
        // Block until data arrives
        sys_net_recv_stream(buffer, &nbytes);

        if (nbytes <= 0) continue;

        int data_offset = 0;

        // --- Step 1: Parse File Size from the very first chunk ---
        if (!header_parsed) {
            if (nbytes >= 4) {
                // Guidebook says: "process according to local byte order"
                // Since x86 (Host) and RISC-V (Board) are both Little Endian, just copy.
                file_size = *(uint32_t *)buffer - 4;
                
                header_parsed = 1;
                data_offset = 4; // Skip the 4-byte size header
                
                sys_move_cursor(0, print_location + 1);
                printf("[RECV FILE] Detected File Size: %d bytes\n", file_size);
            } else {
                // Edge case: first packet is too small (unlikely)
                continue; 
            }
        }

        // --- Step 2: Process File Content ---
        int content_len = nbytes - data_offset;
        
        // Safety check: Don't read more than the file size
        if (total_received_data + content_len > file_size) {
            content_len = file_size - total_received_data;
        }

        if (content_len > 0) {
            // Calculate checksum on the actual file content
            checksum = fletcher16_step(checksum, (uint8_t *)(buffer + data_offset), content_len);
            total_received_data += content_len;
        }

        // --- Step 3: Progress Update ---
        if (total_received_data % 10240 == 0 || total_received_data == file_size || 1) {
            sys_move_cursor(0, print_location + 2);
            printf("Progress: %d / %d bytes | Current Checksum: 0x%04x", 
                   total_received_data, file_size, checksum);
        }

        // --- Step 4: Finish ---
        if (header_parsed && total_received_data >= file_size) {
            sys_move_cursor(0, print_location + 3);
            printf("\n[RECV FILE] Transfer Complete!\n");
            printf("[RECV FILE] Final Checksum: 0x%04x\n", checksum);
            break; // Exit loop
        }
    }

    return 0;
}