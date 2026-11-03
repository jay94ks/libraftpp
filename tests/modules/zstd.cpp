#include <raftpp/zstd/zstd.hpp>
#include "codec_contract.hpp"
int main() {
    return check_codec(raftpp::zstd_codec{}) ? 0 : 1;
}
