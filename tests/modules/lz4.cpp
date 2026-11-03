#include <raftpp/lz4/lz4.hpp>
#include "codec_contract.hpp"
int main() {
    return check_codec(raftpp::lz4_codec{}) ? 0 : 1;
}
