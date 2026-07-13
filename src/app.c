#include "pingmote/app.h"

#include "pingmote/animation.h"

#include <raylib.h>

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum {
    WINDOW_WIDTH = 280,
    WINDOW_HEIGHT = 220,
    TARGET_FPS = 60
};

typedef struct AppState {
    AnimationController animation;
    bool running;
    bool dragging;
    Vector2 drag_anchor;
} AppState;

static bool point_in_circle(Vector2 point, Vector2 center, float radius)
{
    const float x = point.x - center.x;
    const float y = point.y - center.y;
    return (x * x) + (y * y) <= radius * radius;
}

static AnimationState state_from_keyboard(void)
{
    if (IsKeyPressed(KEY_ONE)) {
        return ANIMATION_IDLE;
    }
    if (IsKeyPressed(KEY_TWO)) {
        return ANIMATION_TALKING;
    }
    if (IsKeyPressed(KEY_THREE)) {
        return ANIMATION_LISTENING;
    }
    if (IsKeyPressed(KEY_FOUR)) {
        return ANIMATION_THINKING;
    }
    if (IsKeyPressed(KEY_FIVE)) {
        return ANIMATION_SLEEPING;
    }
    if (IsKeyPressed(KEY_SIX)) {
        return ANIMATION_BOUNCE;
    }
    return ANIMATION_STATE_COUNT;
}

static float animation_offset(const AnimationController *animation)
{
    if (animation->reduced_motion) {
        return 0.0F;
    }

    const float time = animation->elapsed_seconds;
    switch (animation->state) {
        case ANIMATION_IDLE:
            return sinf(time * 2.4F) * 3.0F;
        case ANIMATION_TALKING:
            return sinf(time * 16.0F) * 2.0F;
        case ANIMATION_LISTENING:
            return sinf(time * 4.0F) * 1.5F;
        case ANIMATION_THINKING:
            return sinf(time * 3.0F) * 2.0F;
        case ANIMATION_SLEEPING:
            return sinf(time * 1.4F) * 1.0F;
        case ANIMATION_BOUNCE:
            return -fabsf(sinf(time * 8.6F)) * 24.0F;
        case ANIMATION_STATE_COUNT:
            break;
    }
    return 0.0F;
}

static Color state_color(AnimationState state)
{
    switch (state) {
        case ANIMATION_TALKING:
            return (Color){255, 190, 92, 255};
        case ANIMATION_LISTENING:
            return (Color){113, 213, 187, 255};
        case ANIMATION_THINKING:
            return (Color){177, 148, 255, 255};
        case ANIMATION_SLEEPING:
            return (Color){115, 143, 190, 255};
        case ANIMATION_IDLE:
        case ANIMATION_BOUNCE:
        case ANIMATION_STATE_COUNT:
            return (Color){255, 219, 138, 255};
    }
    return (Color){255, 219, 138, 255};
}

static void update_dragging(AppState *app, Vector2 mouse)
{
    const Rectangle drag_region = {12.0F, 10.0F, 220.0F, 32.0F};

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, drag_region)) {
        app->dragging = true;
        app->drag_anchor = mouse;
    }

    if (app->dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        const Vector2 position = GetWindowPosition();
        const Vector2 delta = {
            mouse.x - app->drag_anchor.x,
            mouse.y - app->drag_anchor.y
        };
        SetWindowPosition((int)(position.x + delta.x), (int)(position.y + delta.y));
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        app->dragging = false;
    }
}

static void update_app(AppState *app)
{
    const Vector2 mouse = GetMousePosition();
    const Vector2 creature_center = {140.0F, 124.0F};
    const Vector2 close_center = {250.0F, 26.0F};
    const AnimationState requested_state = state_from_keyboard();

    update_dragging(app, mouse);

    if (requested_state != ANIMATION_STATE_COUNT) {
        (void)animation_set_state(&app->animation, requested_state);
    }

    if (IsKeyPressed(KEY_M)) {
        animation_set_reduced_motion(&app->animation, !app->animation.reduced_motion);
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
        && point_in_circle(mouse, creature_center, 55.0F)) {
        (void)animation_set_state(&app->animation, ANIMATION_BOUNCE);
    }

    if (IsKeyPressed(KEY_Q)
        || (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && point_in_circle(mouse, close_center, 14.0F))) {
        app->running = false;
    }

    animation_update(&app->animation, GetFrameTime());
}

static void draw_creature(const AnimationController *animation)
{
    const float offset_y = animation_offset(animation);
    const Vector2 center = {140.0F, 124.0F + offset_y};
    const Color body = state_color(animation->state);

    DrawEllipse((int)center.x + 3, (int)center.y + 51, 46.0F, 9.0F, Fade(BLACK, 0.28F));
    DrawCircleV(center, 52.0F, body);
    DrawCircleLinesV(center, 52.0F, Fade(WHITE, 0.22F));

    if (animation->state == ANIMATION_SLEEPING) {
        DrawLineEx((Vector2){121.0F, center.y - 5.0F}, (Vector2){130.0F, center.y - 5.0F}, 3.0F, (Color){47, 42, 50, 255});
        DrawLineEx((Vector2){150.0F, center.y - 5.0F}, (Vector2){159.0F, center.y - 5.0F}, 3.0F, (Color){47, 42, 50, 255});
    } else {
        DrawCircleV((Vector2){126.0F, center.y - 6.0F}, 4.0F, (Color){47, 42, 50, 255});
        DrawCircleV((Vector2){154.0F, center.y - 6.0F}, 4.0F, (Color){47, 42, 50, 255});
    }

    if (animation->state == ANIMATION_TALKING) {
        DrawCircleV((Vector2){140.0F, center.y + 14.0F}, 7.0F, (Color){47, 42, 50, 255});
    } else {
        DrawLineEx((Vector2){134.0F, center.y + 14.0F}, (Vector2){146.0F, center.y + 14.0F}, 3.0F, (Color){47, 42, 50, 255});
    }
}

static void draw_app(const AppState *app)
{
    const Color panel = (Color){25, 26, 31, 236};
    const Color secondary_text = (Color){171, 174, 185, 255};
    const char *const state_name = animation_state_name(app->animation.state);

    ClearBackground(BLANK);
    DrawRectangleRounded((Rectangle){4.0F, 4.0F, 272.0F, 212.0F}, 0.14F, 12, Fade(BLACK, 0.22F));
    DrawRectangleRounded((Rectangle){2.0F, 2.0F, 272.0F, 210.0F}, 0.14F, 12, panel);
    DrawRectangleRoundedLinesEx((Rectangle){2.0F, 2.0F, 272.0F, 210.0F}, 0.14F, 12, 1.0F, Fade(WHITE, 0.12F));

    DrawCircle(18, 26, 3.0F, (Color){113, 213, 187, 255});
    DrawText("mote", 28, 17, 18, RAYWHITE);
    DrawCircle(250, 26, 13.0F, Fade(WHITE, 0.06F));
    DrawLine(246, 22, 254, 30, secondary_text);
    DrawLine(254, 22, 246, 30, secondary_text);

    draw_creature(&app->animation);

    const int state_width = MeasureText(state_name, 16);
    DrawText(state_name, (WINDOW_WIDTH - state_width) / 2, 188, 16, secondary_text);
}

int app_run(void)
{
    SetConfigFlags(
        FLAG_WINDOW_TRANSPARENT
        | FLAG_WINDOW_UNDECORATED
        | FLAG_WINDOW_TOPMOST
        | FLAG_WINDOW_HIGHDPI
        | FLAG_MSAA_4X_HINT
    );
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "PingMote");
    if (!IsWindowReady()) {
        return EXIT_FAILURE;
    }

    SetExitKey(KEY_NULL);
    SetTargetFPS(TARGET_FPS);

    AppState app = {0};
    animation_init(&app.animation);
    app.running = true;

    const char *const reduced_motion = getenv("PINGMOTE_REDUCED_MOTION");
    animation_set_reduced_motion(
        &app.animation,
        reduced_motion != NULL && strcmp(reduced_motion, "1") == 0
    );

    while (app.running && !WindowShouldClose()) {
        update_app(&app);
        BeginDrawing();
        draw_app(&app);
        EndDrawing();
    }

    CloseWindow();
    return EXIT_SUCCESS;
}
