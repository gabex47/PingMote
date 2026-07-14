#include "pingmote/app.h"

#include "pingmote/animation.h"
#include "pingmote/assistant.h"
#include "pingmote/audio.h"
#include "pingmote/resources.h"
#include "pingmote/sprite.h"

#include <raylib.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WINDOW_WIDTH = 360,
    WINDOW_HEIGHT = 370,
    TARGET_FPS = 60,
    MAIN_FOCUS_COUNT = 4,
    SETTINGS_FOCUS_COUNT = 5
};

typedef enum MainFocus {
    MAIN_FOCUS_MESSAGE = 0,
    MAIN_FOCUS_MICROPHONE,
    MAIN_FOCUS_SEND,
    MAIN_FOCUS_SETTINGS
} MainFocus;

typedef enum SettingsFocus {
    SETTINGS_FOCUS_GROQ = 0,
    SETTINGS_FOCUS_SUPABASE_URL,
    SETTINGS_FOCUS_SUPABASE_KEY,
    SETTINGS_FOCUS_SAVE,
    SETTINGS_FOCUS_CANCEL
} SettingsFocus;

typedef struct TextField {
    char *text;
    size_t capacity;
    bool secret;
    bool touched;
} TextField;

typedef struct AppState {
    AnimationController animation;
    AssistantService assistant;
    AudioManager audio;
    SpriteSystem sprites;
    bool running;
    bool dragging;
    bool settings_open;
    bool assistant_ready;
    bool listening;
    bool listen_start_pending;
    bool push_to_talk_active;
    bool push_to_talk_mouse;
    bool response_pending;
    bool secure_storage_available;
    bool has_groq_key;
    bool has_supabase_url;
    bool has_supabase_key;
    Vector2 drag_anchor;
    MainFocus main_focus;
    SettingsFocus settings_focus;
    char message[PINGMOTE_MESSAGE_CAPACITY];
    char groq_key[PINGMOTE_API_KEY_CAPACITY];
    char supabase_url[PINGMOTE_SUPABASE_URL_CAPACITY];
    char supabase_key[PINGMOTE_API_KEY_CAPACITY];
    bool groq_touched;
    bool supabase_url_touched;
    bool supabase_key_touched;
    char bubble[PINGMOTE_REPLY_CAPACITY];
    float bubble_seconds;
    char notice[PINGMOTE_ERROR_CAPACITY];
    float notice_seconds;
} AppState;

static const Color COLOR_PANEL = {24, 25, 30, 244};
static const Color COLOR_PANEL_RAISED = {38, 40, 47, 255};
static const Color COLOR_TEXT = {242, 242, 244, 255};
static const Color COLOR_MUTED = {166, 169, 179, 255};
static const Color COLOR_ACCENT = {255, 212, 59, 255};
static const Color COLOR_SUCCESS = {113, 213, 187, 255};
static const Color COLOR_ERROR = {235, 142, 142, 255};

static bool point_in_circle(Vector2 point, Vector2 center, float radius)
{
    const float x = point.x - center.x;
    const float y = point.y - center.y;
    return (x * x) + (y * y) <= radius * radius;
}

static bool activate_rectangle(Rectangle bounds)
{
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
        && CheckCollisionPointRec(GetMousePosition(), bounds);
}

static void set_bubble(AppState *app, const char *message, float seconds)
{
    (void)snprintf(app->bubble, sizeof(app->bubble), "%s", message == NULL ? "" : message);
    app->bubble_seconds = seconds;
}

static void set_notice(AppState *app, const char *message, float seconds)
{
    (void)snprintf(app->notice, sizeof(app->notice), "%s", message == NULL ? "" : message);
    app->notice_seconds = seconds;
}

static void wipe_bytes(void *memory, size_t size)
{
    volatile unsigned char *cursor = memory;
    for (size_t index = 0U; index < size; ++index) {
        cursor[index] = 0U;
    }
}

static void clear_settings_fields(AppState *app)
{
    wipe_bytes(app->groq_key, sizeof(app->groq_key));
    wipe_bytes(app->supabase_url, sizeof(app->supabase_url));
    wipe_bytes(app->supabase_key, sizeof(app->supabase_key));
    app->groq_touched = false;
    app->supabase_url_touched = false;
    app->supabase_key_touched = false;
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

static void update_dragging(AppState *app, Vector2 mouse)
{
    const Rectangle drag_region = {12.0F, 8.0F, 270.0F, 38.0F};
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
        && CheckCollisionPointRec(mouse, drag_region)) {
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

static size_t utf8_previous(const char *text, size_t length)
{
    if (length == 0U) {
        return 0U;
    }
    size_t index = length - 1U;
    while (index > 0U && ((unsigned char)text[index] & 0xC0U) == 0x80U) {
        index -= 1U;
    }
    return index;
}

static void append_bytes(TextField *field, const char *bytes, size_t length)
{
    const size_t current = strlen(field->text);
    if (length == 0U || current + length >= field->capacity) {
        return;
    }
    (void)memcpy(field->text + current, bytes, length);
    field->text[current + length] = '\0';
    field->touched = true;
}

static void paste_text(TextField *field)
{
    const char *const clipboard = GetClipboardText();
    if (clipboard == NULL) {
        return;
    }
    for (size_t index = 0U; clipboard[index] != '\0'; ++index) {
        const unsigned char byte = (unsigned char)clipboard[index];
        if (byte == '\r' || byte == '\n' || (byte < 0x20U && byte != '\t')) {
            continue;
        }
        append_bytes(field, &clipboard[index], 1U);
    }
}

static void update_text_field(TextField *field)
{
    if ((IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)
            || IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
        && IsKeyPressed(KEY_V)) {
        paste_text(field);
    }

    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        const size_t length = strlen(field->text);
        field->touched = true;
        if (length > 0U) {
            field->text[utf8_previous(field->text, length)] = '\0';
        }
    }

    int codepoint = GetCharPressed();
    while (codepoint > 0) {
        if (codepoint >= 0x20 && codepoint != 0x7F) {
            int byte_count = 0;
            const char *const bytes = CodepointToUTF8(codepoint, &byte_count);
            if (bytes != NULL && byte_count > 0) {
                append_bytes(field, bytes, (size_t)byte_count);
            }
        }
        codepoint = GetCharPressed();
    }
}

static void open_settings(AppState *app)
{
    app->settings_open = true;
    app->settings_focus = SETTINGS_FOCUS_GROQ;
    clear_settings_fields(app);
}

static void stop_listening(AppState *app)
{
    app->push_to_talk_active = false;
    if (!app->listening) {
        return;
    }
    app->listening = false;
    app->response_pending = true;
    if (!assistant_stop_listening(&app->assistant)) {
        app->response_pending = false;
        set_notice(app, "could not stop microphone", 5.0F);
        (void)animation_set_state(&app->animation, ANIMATION_IDLE);
        return;
    }
    set_bubble(app, "turning noises into words...", 30.0F);
    (void)animation_set_state(&app->animation, ANIMATION_THINKING);
}

static void start_listening(AppState *app, bool from_mouse)
{
    if (app->listening || app->listen_start_pending || app->response_pending
        || assistant_is_busy(&app->assistant)) {
        return;
    }
    if (!assistant_start_listening(&app->assistant)) {
        set_notice(app, "microphone is busy", 4.0F);
        return;
    }
    app->listen_start_pending = true;
    app->push_to_talk_active = true;
    app->push_to_talk_mouse = from_mouse;
    set_bubble(app, "opening an ear...", 10.0F);
    (void)animation_set_state(&app->animation, ANIMATION_THINKING);
}

static void submit_message(AppState *app)
{
    if (app->message[0] == '\0' || assistant_is_busy(&app->assistant)
        || app->listening || app->listen_start_pending || app->response_pending) {
        return;
    }
    if (!assistant_submit_chat(&app->assistant, app->message)) {
        set_notice(app, "mote is busy. try again.", 4.0F);
        return;
    }
    app->message[0] = '\0';
    app->response_pending = true;
    set_bubble(app, "hmm...", 20.0F);
    (void)animation_set_state(&app->animation, ANIMATION_THINKING);
}

static void save_settings(AppState *app)
{
    if (!app->secure_storage_available) {
        set_notice(app, "Keychain unavailable. credentials were not saved.", 6.0F);
        return;
    }
    const AssistantSettingsUpdate update = {
        .groq_api_key = "",
        .supabase_url = "",
        .supabase_key = "",
        .replace_groq_api_key = app->groq_touched,
        .replace_supabase_url = app->supabase_url_touched,
        .replace_supabase_key = app->supabase_key_touched
    };
    AssistantSettingsUpdate editable = update;
    (void)snprintf(editable.groq_api_key, sizeof(editable.groq_api_key), "%s", app->groq_key);
    (void)snprintf(editable.supabase_url, sizeof(editable.supabase_url), "%s", app->supabase_url);
    (void)snprintf(editable.supabase_key, sizeof(editable.supabase_key), "%s", app->supabase_key);
    const bool queued = assistant_save_settings(&app->assistant, &editable);
    wipe_bytes(&editable, sizeof(editable));
    if (!queued) {
        set_notice(app, "could not queue settings save", 5.0F);
    } else {
        set_notice(app, "saving securely...", 10.0F);
    }
}

static void update_settings(AppState *app)
{
    const Rectangle groq_bounds = {30.0F, 101.0F, 300.0F, 39.0F};
    const Rectangle url_bounds = {30.0F, 171.0F, 300.0F, 39.0F};
    const Rectangle key_bounds = {30.0F, 241.0F, 300.0F, 39.0F};
    const Rectangle save_bounds = {184.0F, 306.0F, 146.0F, 38.0F};
    const Rectangle cancel_bounds = {30.0F, 306.0F, 140.0F, 38.0F};

    if (activate_rectangle(groq_bounds)) {
        app->settings_focus = SETTINGS_FOCUS_GROQ;
    } else if (activate_rectangle(url_bounds)) {
        app->settings_focus = SETTINGS_FOCUS_SUPABASE_URL;
    } else if (activate_rectangle(key_bounds)) {
        app->settings_focus = SETTINGS_FOCUS_SUPABASE_KEY;
    }

    if (IsKeyPressed(KEY_TAB)) {
        const int direction = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? -1 : 1;
        int next = ((int)app->settings_focus + direction + SETTINGS_FOCUS_COUNT)
            % SETTINGS_FOCUS_COUNT;
        app->settings_focus = (SettingsFocus)next;
    }

    TextField field = {0};
    if (app->settings_focus == SETTINGS_FOCUS_GROQ) {
        field = (TextField){app->groq_key, sizeof(app->groq_key), true, app->groq_touched};
        update_text_field(&field);
        app->groq_touched = field.touched;
    } else if (app->settings_focus == SETTINGS_FOCUS_SUPABASE_URL) {
        field = (TextField){app->supabase_url, sizeof(app->supabase_url), false, app->supabase_url_touched};
        update_text_field(&field);
        app->supabase_url_touched = field.touched;
    } else if (app->settings_focus == SETTINGS_FOCUS_SUPABASE_KEY) {
        field = (TextField){app->supabase_key, sizeof(app->supabase_key), true, app->supabase_key_touched};
        update_text_field(&field);
        app->supabase_key_touched = field.touched;
    }

    if (activate_rectangle(cancel_bounds)
        || IsKeyPressed(KEY_ESCAPE)
        || (app->settings_focus == SETTINGS_FOCUS_CANCEL && IsKeyPressed(KEY_ENTER))) {
        clear_settings_fields(app);
        app->settings_open = false;
    } else if (activate_rectangle(save_bounds)
        || (app->settings_focus == SETTINGS_FOCUS_SAVE && IsKeyPressed(KEY_ENTER))) {
        save_settings(app);
    }
}

static void process_assistant_events(AppState *app)
{
    AssistantEvent event;
    while (assistant_poll_event(&app->assistant, &event)) {
        switch (event.type) {
            case ASSISTANT_EVENT_READY:
                app->assistant_ready = true;
                app->secure_storage_available = event.secure_storage_available;
                app->has_groq_key = event.has_groq_key;
                app->has_supabase_url = event.has_supabase_url;
                app->has_supabase_key = event.has_supabase_key;
                if (!event.secure_storage_available) {
                    set_notice(app, "secure storage unavailable", 6.0F);
                } else if (!event.has_groq_key) {
                    set_bubble(app, "gimme a groq key", 7.0F);
                }
                break;
            case ASSISTANT_EVENT_REPLY:
                app->response_pending = false;
                set_bubble(app, event.text, 8.0F);
                break;
            case ASSISTANT_EVENT_AUDIO_READY: {
                char error[PINGMOTE_ERROR_CAPACITY];
                if (play_audio(
                        &app->audio,
                        event.audio_path,
                        error,
                        sizeof(error))) {
                    (void)animation_set_state(&app->animation, ANIMATION_TALKING);
                } else {
                    set_notice(app, error, 5.0F);
                    (void)animation_set_state(&app->animation, ANIMATION_IDLE);
                }
                break;
            }
            case ASSISTANT_EVENT_LISTENING_STARTED:
                app->listen_start_pending = false;
                app->listening = true;
                if (!app->push_to_talk_active) {
                    stop_listening(app);
                } else {
                    set_bubble(app, "listening...", 35.0F);
                    (void)animation_set_state(&app->animation, ANIMATION_LISTENING);
                }
                break;
            case ASSISTANT_EVENT_SPEECH_PREPARING:
                app->listening = false;
                app->listen_start_pending = false;
                app->response_pending = true;
                set_bubble(app, event.detail, 30.0F);
                (void)animation_set_state(&app->animation, ANIMATION_THINKING);
                break;
            case ASSISTANT_EVENT_TRANSCRIPTION:
                set_notice(app, event.text, 4.0F);
                break;
            case ASSISTANT_EVENT_SETTINGS_SAVED:
                app->has_groq_key = event.has_groq_key;
                app->has_supabase_url = event.has_supabase_url;
                app->has_supabase_key = event.has_supabase_key;
                clear_settings_fields(app);
                app->settings_open = false;
                set_bubble(app, "locked in boss", 5.0F);
                set_notice(app, "settings saved in Keychain", 4.0F);
                break;
            case ASSISTANT_EVENT_ERROR:
                app->listening = false;
                app->listen_start_pending = false;
                app->push_to_talk_active = false;
                app->response_pending = false;
                set_notice(app, event.detail, 6.0F);
                if (app->animation.state == ANIMATION_THINKING) {
                    (void)animation_set_state(&app->animation, ANIMATION_IDLE);
                }
                break;
        }
    }
}

static void audio_finished(void *user_data, const char *path, bool completed)
{
    (void)path;
    (void)completed;
    AppState *const app = user_data;
    if (app != NULL) {
        (void)animation_set_state(&app->animation, ANIMATION_IDLE);
    }
}

static void update_main(AppState *app)
{
    const Rectangle message_bounds = {18.0F, 302.0F, 194.0F, 44.0F};
    const Rectangle microphone_bounds = {220.0F, 302.0F, 42.0F, 44.0F};
    const Rectangle send_bounds = {270.0F, 302.0F, 72.0F, 44.0F};
    const Rectangle settings_bounds = {286.0F, 10.0F, 28.0F, 32.0F};
    const Vector2 mouse = GetMousePosition();
    const Vector2 creature_center = {180.0F, 205.0F};

    if (activate_rectangle(message_bounds)) {
        app->main_focus = MAIN_FOCUS_MESSAGE;
    } else if (activate_rectangle(microphone_bounds)) {
        app->main_focus = MAIN_FOCUS_MICROPHONE;
        start_listening(app, true);
    } else if (activate_rectangle(send_bounds)) {
        app->main_focus = MAIN_FOCUS_SEND;
        submit_message(app);
    } else if (activate_rectangle(settings_bounds)) {
        app->main_focus = MAIN_FOCUS_SETTINGS;
        open_settings(app);
    }

    if (IsKeyPressed(KEY_TAB)) {
        const int direction = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? -1 : 1;
        const int next = ((int)app->main_focus + direction + MAIN_FOCUS_COUNT) % MAIN_FOCUS_COUNT;
        app->main_focus = (MainFocus)next;
    }

    if (app->main_focus == MAIN_FOCUS_MESSAGE) {
        TextField message = {app->message, sizeof(app->message), false, false};
        update_text_field(&message);
        if (IsKeyPressed(KEY_ENTER)) {
            submit_message(app);
        }
    } else if (app->main_focus == MAIN_FOCUS_MICROPHONE && IsKeyPressed(KEY_ENTER)) {
        start_listening(app, false);
    } else if (app->main_focus == MAIN_FOCUS_SEND && IsKeyPressed(KEY_ENTER)) {
        submit_message(app);
    } else if (app->main_focus == MAIN_FOCUS_SETTINGS && IsKeyPressed(KEY_ENTER)) {
        open_settings(app);
    }

    if (app->push_to_talk_active
        && ((app->push_to_talk_mouse && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            || (!app->push_to_talk_mouse && IsKeyReleased(KEY_ENTER)))) {
        stop_listening(app);
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
        && point_in_circle(mouse, creature_center, 58.0F)) {
        (void)animation_set_state(&app->animation, ANIMATION_BOUNCE);
    }
}

static void update_app(AppState *app)
{
    const Vector2 mouse = GetMousePosition();
    const Vector2 close_center = {335.0F, 26.0F};
    const float delta = GetFrameTime();
    update_dragging(app, mouse);
    process_assistant_events(app);

    if (app->settings_open) {
        update_settings(app);
    } else {
        update_main(app);
    }

    if (IsKeyPressed(KEY_Q)
        && (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)
            || IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
        app->running = false;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
        && point_in_circle(mouse, close_center, 14.0F)) {
        app->running = false;
    }

    if (app->bubble_seconds > 0.0F) {
        app->bubble_seconds -= delta;
    }
    if (app->notice_seconds > 0.0F) {
        app->notice_seconds -= delta;
    }
    if (app->animation.state == ANIMATION_THINKING
        && !assistant_is_busy(&app->assistant)
        && !app->listen_start_pending
        && !app->response_pending) {
        (void)animation_set_state(&app->animation, ANIMATION_IDLE);
    }
    animation_update(&app->animation, delta);
    audio_update(&app->audio);
}

static void draw_button(Rectangle bounds, const char *label, bool focused, bool enabled)
{
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    Color fill = COLOR_PANEL_RAISED;
    if (!enabled) {
        fill = Fade(COLOR_PANEL_RAISED, 0.45F);
    } else if (hovered || focused) {
        fill = (Color){58, 60, 69, 255};
    }
    DrawRectangleRounded(bounds, 0.28F, 8, fill);
    DrawRectangleRoundedLinesEx(
        bounds,
        0.28F,
        8,
        focused ? 2.0F : 1.0F,
        focused ? COLOR_ACCENT : Fade(WHITE, 0.12F)
    );
    const int width = MeasureText(label, 16);
    DrawText(
        label,
        (int)(bounds.x + (bounds.width - (float)width) * 0.5F),
        (int)(bounds.y + 13.0F),
        16,
        enabled ? COLOR_TEXT : COLOR_MUTED
    );
}

static void make_field_display(
    const TextField *field,
    const char *placeholder,
    char *display,
    size_t capacity
)
{
    if (field->text[0] == '\0') {
        (void)snprintf(display, capacity, "%s", placeholder);
        return;
    }
    if (!field->secret) {
        (void)snprintf(display, capacity, "%s", field->text);
        return;
    }

    const size_t length = strlen(field->text);
    const size_t mask_length = length < capacity - 1U ? length : capacity - 1U;
    (void)memset(display, '*', mask_length);
    display[mask_length] = '\0';
}

static void draw_text_field(
    Rectangle bounds,
    const TextField *field,
    const char *placeholder,
    bool focused
)
{
    DrawRectangleRounded(bounds, 0.2F, 8, (Color){16, 17, 21, 255});
    DrawRectangleRoundedLinesEx(
        bounds,
        0.2F,
        8,
        focused ? 2.0F : 1.0F,
        focused ? COLOR_ACCENT : Fade(WHITE, 0.13F)
    );
    char display[512];
    make_field_display(field, placeholder, display, sizeof(display));
    const bool placeholder_visible = field->text[0] == '\0';
    const int full_width = MeasureText(display, 16);
    const int available = (int)bounds.width - 22;
    int x = (int)bounds.x + 11;
    if (full_width > available) {
        x -= full_width - available;
    }
    BeginScissorMode((int)bounds.x + 8, (int)bounds.y, (int)bounds.width - 16, (int)bounds.height);
    DrawText(
        display,
        x,
        (int)bounds.y + 12,
        16,
        placeholder_visible ? COLOR_MUTED : COLOR_TEXT
    );
    if (focused && ((int)(GetTime() * 2.0) % 2) == 0) {
        const int cursor_x = x + full_width + 1;
        DrawLine(cursor_x, (int)bounds.y + 10, cursor_x, (int)bounds.y + 29, COLOR_ACCENT);
    }
    EndScissorMode();
}

static void draw_bubble(const AppState *app)
{
    if (app->bubble[0] == '\0' || app->bubble_seconds <= 0.0F) {
        return;
    }
    const float alpha = app->bubble_seconds < 1.5F ? app->bubble_seconds / 1.5F : 1.0F;
    const Rectangle bounds = {26.0F, 55.0F, 308.0F, 69.0F};
    DrawRectangleRounded(bounds, 0.22F, 10, Fade((Color){248, 247, 242, 255}, alpha));
    DrawTriangle(
        (Vector2){163.0F, 123.0F},
        (Vector2){181.0F, 139.0F},
        (Vector2){195.0F, 123.0F},
        Fade((Color){248, 247, 242, 255}, alpha)
    );
    const int font_size = strlen(app->bubble) > 38U ? 15 : 17;
    const int width = MeasureText(app->bubble, font_size);
    DrawText(
        app->bubble,
        width <= 284 ? (WINDOW_WIDTH - width) / 2 : 38,
        80,
        font_size,
        Fade((Color){28, 29, 34, 255}, alpha)
    );
}

static void draw_creature(const AppState *app)
{
    const float offset_y = animation_offset(&app->animation);
    const Vector2 center = {180.0F, 205.0F + offset_y};
    DrawEllipse((int)center.x + 3, (int)center.y + 53, 47.0F, 9.0F, Fade(BLACK, 0.28F));
    if (sprite_system_has_state(&app->sprites, app->animation.state)) {
        sprite_system_draw(
            &app->sprites,
            app->animation.state,
            center,
            108.0F,
            app->animation.elapsed_seconds,
            WHITE
        );
    } else {
        DrawText("sprite unavailable", 118, 198, 14, COLOR_ERROR);
    }
}

static void draw_header(void)
{
    DrawCircle(18, 26, 3.0F, COLOR_SUCCESS);
    DrawText("mote", 28, 17, 18, COLOR_TEXT);
    DrawText("settings", 286, 19, 12, COLOR_MUTED);
    DrawCircle(335, 26, 13.0F, Fade(WHITE, 0.06F));
    DrawLine(331, 22, 339, 30, COLOR_MUTED);
    DrawLine(339, 22, 331, 30, COLOR_MUTED);
}

static void draw_main(const AppState *app)
{
    const Rectangle message_bounds = {18.0F, 302.0F, 194.0F, 44.0F};
    const Rectangle microphone_bounds = {220.0F, 302.0F, 42.0F, 44.0F};
    const Rectangle send_bounds = {270.0F, 302.0F, 72.0F, 44.0F};
    const TextField message = {
        (char *)app->message,
        sizeof(app->message),
        false,
        false
    };
    draw_bubble(app);
    draw_creature(app);
    draw_text_field(
        message_bounds,
        &message,
        app->assistant_ready ? "say something..." : "waking mote...",
        app->main_focus == MAIN_FOCUS_MESSAGE
    );
    draw_button(
        microphone_bounds,
        app->listening ? "live" : "mic",
        app->main_focus == MAIN_FOCUS_MICROPHONE || app->listening,
        !app->response_pending && !app->listen_start_pending
            && (!assistant_is_busy(&app->assistant) || app->listening)
    );
    draw_button(
        send_bounds,
        assistant_is_busy(&app->assistant) ? "wait" : "send",
        app->main_focus == MAIN_FOCUS_SEND,
        app->message[0] != '\0' && !assistant_is_busy(&app->assistant)
            && !app->listening && !app->response_pending
    );
}

static void draw_settings(const AppState *app)
{
    DrawRectangleRounded((Rectangle){14.0F, 51.0F, 332.0F, 306.0F}, 0.09F, 10, (Color){31, 32, 38, 255});
    DrawText("settings", 30, 65, 22, COLOR_TEXT);
    DrawText("stored in macOS Keychain", 121, 70, 13, COLOR_MUTED);

    DrawText("groq api key", 30, 84, 13, COLOR_MUTED);
    const TextField groq = {(char *)app->groq_key, sizeof(app->groq_key), true, app->groq_touched};
    draw_text_field(
        (Rectangle){30.0F, 101.0F, 300.0F, 39.0F},
        &groq,
        app->has_groq_key ? "saved - type replaces, backspace clears" : "gsk_...",
        app->settings_focus == SETTINGS_FOCUS_GROQ
    );

    DrawText("supabase url (optional)", 30, 154, 13, COLOR_MUTED);
    const TextField url = {(char *)app->supabase_url, sizeof(app->supabase_url), false, app->supabase_url_touched};
    draw_text_field(
        (Rectangle){30.0F, 171.0F, 300.0F, 39.0F},
        &url,
        app->has_supabase_url ? "saved - type replaces, backspace clears" : "https://project.supabase.co",
        app->settings_focus == SETTINGS_FOCUS_SUPABASE_URL
    );

    DrawText("supabase key (optional)", 30, 224, 13, COLOR_MUTED);
    const TextField key = {(char *)app->supabase_key, sizeof(app->supabase_key), true, app->supabase_key_touched};
    draw_text_field(
        (Rectangle){30.0F, 241.0F, 300.0F, 39.0F},
        &key,
        app->has_supabase_key ? "saved - type replaces, backspace clears" : "public client key",
        app->settings_focus == SETTINGS_FOCUS_SUPABASE_KEY
    );

    draw_button(
        (Rectangle){30.0F, 306.0F, 140.0F, 38.0F},
        "cancel",
        app->settings_focus == SETTINGS_FOCUS_CANCEL,
        true
    );
    draw_button(
        (Rectangle){184.0F, 306.0F, 146.0F, 38.0F},
        "save securely",
        app->settings_focus == SETTINGS_FOCUS_SAVE,
        app->secure_storage_available && !assistant_is_busy(&app->assistant)
    );
}

static void draw_app(const AppState *app)
{
    ClearBackground(BLANK);
    DrawRectangleRounded((Rectangle){4.0F, 4.0F, 352.0F, 362.0F}, 0.09F, 12, Fade(BLACK, 0.22F));
    DrawRectangleRounded((Rectangle){2.0F, 2.0F, 352.0F, 360.0F}, 0.09F, 12, COLOR_PANEL);
    DrawRectangleRoundedLinesEx((Rectangle){2.0F, 2.0F, 352.0F, 360.0F}, 0.09F, 12, 1.0F, Fade(WHITE, 0.12F));
    draw_header();
    if (app->settings_open) {
        draw_settings(app);
    } else {
        draw_main(app);
    }

    if (app->notice[0] != '\0' && app->notice_seconds > 0.0F) {
        const float alpha = app->notice_seconds < 1.0F ? app->notice_seconds : 1.0F;
        const int width = MeasureText(app->notice, 13);
        DrawRectangleRounded(
            (Rectangle){18.0F, 349.0F, 324.0F, 17.0F},
            0.3F,
            6,
            Fade((Color){55, 39, 43, 255}, alpha)
        );
        DrawText(
            app->notice,
            width < 318 ? (WINDOW_WIDTH - width) / 2 : 22,
            351,
            13,
            Fade(COLOR_ERROR, alpha)
        );
    }
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
    SetWindowMinSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    SetExitKey(KEY_NULL);
    SetTargetFPS(TARGET_FPS);

    AppState app = {0};
    animation_init(&app.animation);
    app.running = true;
    app.main_focus = MAIN_FOCUS_MESSAGE;

    char error[PINGMOTE_ERROR_CAPACITY];
    char sprite_directory[PINGMOTE_PATH_CAPACITY];
    if (!resource_find_sprite_directory(
            PINGMOTE_SOURCE_ASSET_DIR,
            sprite_directory,
            sizeof(sprite_directory))) {
        (void)snprintf(
            sprite_directory,
            sizeof(sprite_directory),
            "%s",
            PINGMOTE_SOURCE_ASSET_DIR
        );
    }
    if (!sprite_system_init(
            &app.sprites,
            sprite_directory,
            error,
            sizeof(error))) {
        TraceLog(LOG_WARNING, "SPRITE: %s", error);
    }
    if (!audio_init(&app.audio, error, sizeof(error))) {
        TraceLog(LOG_WARNING, "AUDIO: %s", error);
        set_notice(&app, "audio is unavailable", 6.0F);
    } else {
        audio_set_playback_callback(&app.audio, audio_finished, &app);
    }
    if (!assistant_init(&app.assistant, error, sizeof(error))) {
        TraceLog(LOG_WARNING, "ASSISTANT: %s", error);
        set_notice(&app, "assistant worker could not start", 8.0F);
    }

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

    assistant_shutdown(&app.assistant);
    clear_settings_fields(&app);
    sprite_system_cleanup(&app.sprites);
    cleanup_audio(&app.audio);
    CloseWindow();
    return EXIT_SUCCESS;
}
