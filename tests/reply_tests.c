#include "pingmote/reply.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    char output[256];

    assert(reply_sanitize("**HELLO** boss", output, sizeof(output)));
    assert(strcmp(output, "hello boss") == 0);

    assert(reply_sanitize("yo \xF0\x9F\xAB\xA1 ok", output, sizeof(output)));
    assert(strcmp(output, "yo ok") == 0);

    assert(reply_sanitize(
        "one two three four five six seven eight nine ten eleven twelve thirteen fourteen "
        "fifteen sixteen seventeen eighteen nineteen twenty twentyone",
        output,
        sizeof(output)
    ));
    assert(strcmp(
        output,
        "one two three four five six seven eight nine ten eleven twelve thirteen fourteen "
        "fifteen sixteen seventeen eighteen nineteen twenty"
    ) == 0);

    assert(!reply_sanitize("```***", output, sizeof(output)));
    assert(strcmp(reply_offline_fallback(0U), "wifi vanished") == 0);
    assert(strcmp(reply_offline_fallback(4U), "wifi vanished") == 0);

    return 0;
}
