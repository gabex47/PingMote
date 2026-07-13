#include "pingmote/animation.h"

#include <stddef.h>

static bool animation_state_is_valid(AnimationState state)
{
    return state >= ANIMATION_IDLE && state < ANIMATION_STATE_COUNT;
}

void animation_init(AnimationController *controller)
{
    if (controller == NULL) {
        return;
    }

    controller->state = ANIMATION_IDLE;
    controller->return_state = ANIMATION_IDLE;
    controller->elapsed_seconds = 0.0F;
    controller->reduced_motion = false;
}

bool animation_set_state(AnimationController *controller, AnimationState state)
{
    if (controller == NULL || !animation_state_is_valid(state)) {
        return false;
    }

    if (state == ANIMATION_BOUNCE && controller->state != ANIMATION_BOUNCE) {
        controller->return_state = controller->state;
    }

    controller->state = state;
    controller->elapsed_seconds = 0.0F;
    return true;
}

void animation_update(AnimationController *controller, float delta_seconds)
{
    if (controller == NULL || delta_seconds <= 0.0F) {
        return;
    }

    controller->elapsed_seconds += delta_seconds;

    if (controller->state == ANIMATION_BOUNCE && controller->elapsed_seconds >= 0.55F) {
        const AnimationState next_state = animation_state_is_valid(controller->return_state)
            ? controller->return_state
            : ANIMATION_IDLE;
        (void)animation_set_state(controller, next_state);
    }
}

void animation_set_reduced_motion(AnimationController *controller, bool enabled)
{
    if (controller == NULL) {
        return;
    }

    controller->reduced_motion = enabled;
}

const char *animation_state_name(AnimationState state)
{
    static const char *const names[ANIMATION_STATE_COUNT] = {
        "idle",
        "talking",
        "listening",
        "thinking",
        "sleeping",
        "bounce"
    };

    if (!animation_state_is_valid(state)) {
        return "unknown";
    }

    return names[state];
}
