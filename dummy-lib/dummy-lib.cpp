#include "dummy-lib.h"

void callMeBack(std::function<void ()> cb)
{
    cb();
}
