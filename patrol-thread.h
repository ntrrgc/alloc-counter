#include <thread>
#include <mutex>
#include <condition_variable>
using namespace std;

class PatrolThread {
public:
    static void spawn();
    static PatrolThread* instance() { return s_instance; }
    static uint32_t LEAK_TIME();

    void tearDown();

private:
    PatrolThread()
        : m_thread([this]() {
            this->monitorMain();
        })
    {}
    ~PatrolThread();
    static PatrolThread* s_instance;

    thread m_thread;
    mutex m_mutex;
    condition_variable m_cv;
    bool m_should_tear_down = false;

    void monitorMain();
};
