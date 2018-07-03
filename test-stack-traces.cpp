#include "stack-trace.h"
#include <vector>
#include <algorithm>
using namespace std;

int main() {
    std::vector<int> v = {{1, 2}};
    sort(v.begin(), v.end(), [](int a, int b) {
        return a - b;
    });
    return 0;
}
