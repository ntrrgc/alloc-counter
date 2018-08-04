#include <stdlib.h>
#include <iostream>
#include <vector>
#include <unistd.h>
#include "dummy-lib.h"
using namespace std;

struct LongLived {
    int stepsRemaining = 10;
};

int main() {
    vector<LongLived*> longLivedObjects;

    while (true) {
        callMeBack([&longLivedObjects]() {
            // Leak
            int* volatile dummy = new int;
            (void)dummy;

            // Cycle long lived objects simulation
            longLivedObjects.push_back(new LongLived);
            for (auto it = longLivedObjects.begin(); it != longLivedObjects.end(); ) {
                LongLived* longLivedObject = *it;
                if (--longLivedObject->stepsRemaining == 0) {
                    delete longLivedObject;
                    it = longLivedObjects.erase(it);
                } else {
                    ++it;
                }
            }
        });
        sleep(1);
    }
    return 0;
}
