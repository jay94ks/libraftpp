#include <raftpp/uv/uv_stream.hpp>

int main() {
    raftpp::uv_stream stream;
    return static_cast<bool>(stream);
}
