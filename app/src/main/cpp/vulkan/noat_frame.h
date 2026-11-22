#ifndef NOAT_FRAME_H
#define NOAT_FRAME_H

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NoatFrame NoatFrame;

NoatFrame *noat_frame_create(VkDevice device, uint32_t queue_family);
void noat_frame_destroy(NoatFrame *frame);

VkCommandBuffer noat_frame_get_command_buffer(const NoatFrame *frame);
VkSemaphore noat_frame_get_image_available_semaphore(const NoatFrame *frame);
VkSemaphore noat_frame_get_render_finished_semaphore(const NoatFrame *frame);
VkFence noat_frame_get_in_flight_fence(const NoatFrame *frame);

void noat_frame_begin_command_buffer(NoatFrame *frame);
void noat_frame_end_command_buffer(NoatFrame *frame);

#ifdef __cplusplus
}
#endif

#endif /* NOAT_FRAME_H */