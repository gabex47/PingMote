#include "pingmote/reply.h"

#include <ctype.h>

enum {
    REPLY_WORD_LIMIT = 20
};

static bool is_markdown_character(unsigned char character)
{
    return character == (unsigned char)'*'
        || character == (unsigned char)'_'
        || character == (unsigned char)'`'
        || character == (unsigned char)'#'
        || character == (unsigned char)'>'
        || character == (unsigned char)'~'
        || character == (unsigned char)'|';
}

bool reply_sanitize(const char *input, char *output, size_t output_capacity)
{
    if (input == NULL || output == NULL || output_capacity == 0U) {
        return false;
    }

    size_t output_length = 0U;
    int word_count = 0;
    bool in_word = false;
    bool previous_space = true;

    for (size_t index = 0U; input[index] != '\0'; ++index) {
        const unsigned char character = (unsigned char)input[index];

        if (character >= 0x80U || is_markdown_character(character)) {
            continue;
        }

        const bool is_space = isspace((int)character) != 0;
        if (is_space) {
            if (!previous_space && output_length + 1U < output_capacity) {
                output[output_length++] = ' ';
            }
            previous_space = true;
            in_word = false;
            continue;
        }

        if (iscntrl((int)character) != 0) {
            continue;
        }

        if (!in_word) {
            if (word_count >= REPLY_WORD_LIMIT) {
                break;
            }
            word_count += 1;
            in_word = true;
        }

        if (output_length + 1U >= output_capacity) {
            return false;
        }

        output[output_length++] = (char)tolower((int)character);
        previous_space = false;
    }

    while (output_length > 0U && output[output_length - 1U] == ' ') {
        output_length -= 1U;
    }
    output[output_length] = '\0';
    return output_length > 0U;
}

const char *reply_offline_fallback(uint64_t seed)
{
    static const char *const messages[] = {
        "wifi vanished",
        "brain offline",
        "im thinking really hard",
        "cant reach the internet"
    };
    const size_t count = sizeof(messages) / sizeof(messages[0]);
    return messages[seed % count];
}
