#include <assert.h>
#include <stdbool.h>

#include "dfu_config_command.h"

int main(void)
{
    bool enter_dfu = false;

    assert(dfu_config_command_parse("SYNC=1234567890123456\r\n"
                                    "ENTER_DFU=1\r\n",
                                    &enter_dfu) == DFU_CONFIG_COMMAND_ENTER);
    assert(enter_dfu);
    enter_dfu = true;
    assert(dfu_config_command_parse("ENTER_DFU=0\r\n",
                                    &enter_dfu) == DFU_CONFIG_COMMAND_NONE);
    assert(!enter_dfu);
    assert(dfu_config_command_parse("ENTER_DFU=1\r\nENTER_DFU=1\r\n",
                                    &enter_dfu) == DFU_CONFIG_COMMAND_INVALID);
    assert(dfu_config_command_parse("ENTER_DFU =1\r\n",
                                    &enter_dfu) == DFU_CONFIG_COMMAND_INVALID);
    assert(dfu_config_command_parse("ENTER_DFU=true\r\n",
                                    &enter_dfu) == DFU_CONFIG_COMMAND_INVALID);
    assert(dfu_config_command_parse("ENTER_DFU=2\r\n",
                                    &enter_dfu) == DFU_CONFIG_COMMAND_INVALID);
    assert(dfu_config_command_parse("DEVICE_MODE=WIRED\r\n",
                                    &enter_dfu) == DFU_CONFIG_COMMAND_NONE);
    return 0;
}
