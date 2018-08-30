#include <stdlib.h>
#include <unistd.h>
#include "dummy-lib.h"

int main() {
    while (true) {
        sleep(1);
        callMeBack([]() {
            int* volatile dummy = new int;
            (void)dummy;
        });
    }
    return 0;
}
