#include "stack-trace.h"
#include "dummy-lib.h"
#include <vector>
#include <algorithm>
#include <unordered_map>
using namespace std;

std::unordered_map<StackTrace, int> myMap;

int main() {
    std::vector<int> v = {{1, 2, 3, 4, 5, 6, 7}};
    sort(v.begin(), v.end(), [](int a, int b) {
        callMeBack([]() {
            StackTrace t(0);
            auto kvIter = myMap.find(t);
            if (kvIter == myMap.end()) {
                kvIter = myMap.insert(std::pair<StackTrace, int>(t, 0)).first;
            }
            kvIter->second++;
        });
        return a - b;
    });
    cerr << "Found " << myMap.size() << " stacks." << endl;
    for (auto& kv : myMap) {
        cerr << kv.second << " calls at:" << endl;
        cerr << kv.first << endl;
    }
    return 0;
}
