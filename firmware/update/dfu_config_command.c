#include "dfu_config_command.h"

#include <stddef.h>
#include <string.h>

dfu_config_command_result_t dfu_config_command_parse(const char *text,
                                                      bool *enter_dfu)
{
    const char *line = text;
    bool found = false;

    if ((text == NULL) || (enter_dfu == NULL)) {
        return DFU_CONFIG_COMMAND_INVALID;
    }
    *enter_dfu = false;
    while (*line != '\0') {
        const char *end = line;
        size_t length;

        while ((*end != '\0') && (*end != '\r') && (*end != '\n')) {
            ++end;
        }
        length = (size_t)(end - line);
        if (length >= 9U && strncmp(line, "ENTER_DFU", 9U) == 0) {
            if (found || (length != 11U) ||
                (line[9] != '=') ||
                ((line[10] != '0') && (line[10] != '1'))) {
                return DFU_CONFIG_COMMAND_INVALID;
            }
            found = true;
            *enter_dfu = line[10] == '1';
        }
        line = end;
        while ((*line == '\r') || (*line == '\n')) {
            ++line;
        }
    }
    return *enter_dfu ? DFU_CONFIG_COMMAND_ENTER :
                        DFU_CONFIG_COMMAND_NONE;
}
