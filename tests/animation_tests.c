#include "pingmote/animation.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    AnimationController animation;
    animation_init(&animation);

    assert(animation.state == ANIMATION_IDLE);
    assert(strcmp(animation_state_name(animation.state), "idle") == 0);
    assert(!animation.reduced_motion);

    assert(animation_set_state(&animation, ANIMATION_TALKING));
    animation_update(&animation, 0.25F);
    assert(animation.state == ANIMATION_TALKING);
    assert(animation.elapsed_seconds == 0.25F);

    assert(animation_set_state(&animation, ANIMATION_BOUNCE));
    assert(animation.return_state == ANIMATION_TALKING);
    animation_update(&animation, 0.56F);
    assert(animation.state == ANIMATION_TALKING);
    assert(animation.elapsed_seconds == 0.0F);

    animation_set_reduced_motion(&animation, true);
    assert(animation.reduced_motion);
    assert(!animation_set_state(&animation, ANIMATION_STATE_COUNT));
    assert(strcmp(animation_state_name(ANIMATION_STATE_COUNT), "unknown") == 0);

    return 0;
}
