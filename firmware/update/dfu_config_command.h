#ifndef DFU_CONFIG_COMMAND_H
#define DFU_CONFIG_COMMAND_H

#include <stdbool.h>

typedef enum {
    DFU_CONFIG_COMMAND_NONE = 0,
    DFU_CONFIG_COMMAND_ENTER,
    DFU_CONFIG_COMMAND_INVALID
} dfu_config_command_result_t;

dfu_config_command_result_t dfu_config_command_parse(const char *text,
                                                      bool *enter_dfu);

#endif
