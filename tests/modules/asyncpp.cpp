#include <raftpp/asyncpp/asyncpp.hpp>
#include <deque>

class queue final : public asyncpp::dispatcher {
public:
    std::deque<std::function<void()>> pending;
    void push(std::function<void()> callback) override {
        pending.push_back(std::move(callback));
    }
    void run() {
        while (!pending.empty()) {
            auto callback = std::move(pending.front());
            pending.pop_front();
            callback();
        }
    }
};
int main() {
    auto native = std::make_shared<queue>();
    std::weak_ptr<queue> lifetime = native;
    int calls = 0;
    {
        raftpp::asyncpp_executor executor{native};
        raftpp::executor& base = executor;
        if (base.post({}) != raftpp::errc::invalid)
            return 1;
        if (base.post([&] { calls = calls * 10 + 1; }) ||
            base.post([&] { calls = calls * 10 + 2; }) || calls)
            return 1;
    }
    native.reset();
    auto retained = lifetime.lock();
    if (!retained)
        return 1;
    retained->run();
    retained.reset();
    return calls != 12 || !lifetime.expired();
}
