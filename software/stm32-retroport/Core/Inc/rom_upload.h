#ifndef ROM_UPLOAD_H
#define ROM_UPLOAD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ROM_UPLOAD_PHASE_OFF = 0,
    ROM_UPLOAD_PHASE_WAITING,
    ROM_UPLOAD_PHASE_ERASING,
    ROM_UPLOAD_PHASE_WRITING,
    ROM_UPLOAD_PHASE_VERIFYING,
    ROM_UPLOAD_PHASE_DONE,
    ROM_UPLOAD_PHASE_ERROR,
} RomUploadPhase;

typedef struct {
    RomUploadPhase phase;
    uint32_t done;
    uint32_t total;
    uint32_t jedec_id;
    const char *message;
} RomUploadStatus;

void RomUpload_Start(void);
void RomUpload_Stop(void);
void RomUpload_Task(void);
void RomUpload_GetStatus(RomUploadStatus *status);
void RomUpload_UsbReceive(const uint8_t *data, uint32_t size);
void RomUpload_UsbTransmitComplete(void);

#ifdef __cplusplus
}
#endif

#endif
