#include "wasteClassifier.h"
#include <string.h>
#include <ctype.h>

static bool caseInsensitiveEquals(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static void trimWhitespace(const char* str, char* buffer, size_t maxLen)
{
    if (!str || !buffer || maxLen == 0) {
        if (buffer && maxLen > 0) buffer[0] = '\0';
        return;
    }
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        len--;
    }
    if (len >= maxLen) {
        len = maxLen - 1;
    }
    memcpy(buffer, str, len);
    buffer[len] = '\0';
}

int tagToAngle(const char* tag)
{
    if (!tag) return -1;
    char cleanTag[32];
    trimWhitespace(tag, cleanTag, sizeof(cleanTag));

    if (caseInsensitiveEquals(cleanTag, "METAL")) {
        return ANGLE_METAL;
    }
    if (caseInsensitiveEquals(cleanTag, "PLASTICS") || caseInsensitiveEquals(cleanTag, "PLASTIC")) {
        return ANGLE_PLASTICS;
    }
    if (caseInsensitiveEquals(cleanTag, "PAPER") ||
        caseInsensitiveEquals(cleanTag, "PAPER_CARDBOARD") ||
        caseInsensitiveEquals(cleanTag, "CARDBOARD")) {
        return ANGLE_PAPER;
    }
    return -1;
}