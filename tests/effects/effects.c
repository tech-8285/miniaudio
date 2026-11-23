#define MA_NO_DEVICE_IO
#include "../common/common.c"

#include "effects_delay.c"

int main(int argc, char** argv)
{
    ma_register_test("Delay", test_entry__delay);

    return ma_run_tests(argc, argv);
}
