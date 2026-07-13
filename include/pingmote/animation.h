#ifndef PINGMOTE_ANIMATION_H
#define PINGMOTE_ANIMATION_H

#include <stdbool.h>

typedef enum AnimationState {
    ANIMATION_IDLE = 0,
    ANIMATION_TALKING,
    ANIMATION_LISTENING,
    ANIMATION_THINKING,
    ANIMATION_SLEEPING,
    ANIMATION_BOUNCE,
    ANIMATION_STATE_COUNT
} AnimationState;

typedef struct AnimationController {
    AnimationState state;
    AnimationState return_state;
    float elapsed_seconds;
    bool reduced_motion;
} AnimationController;

void animation_init(AnimationController *controller);
bool animation_set_state(AnimationController *controller, AnimationState state);
void animation_update(AnimationController *controller, float delta_seconds);
void animation_set_reduced_motion(AnimationController *controller, bool enabled);
const char *animation_state_name(AnimationState state);

#endif
