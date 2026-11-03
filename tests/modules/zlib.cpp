#include <raftpp/zlib/zlib.hpp>
#include "codec_contract.hpp"
int main() {
    return check_codec(raftpp::zlib_codec{}) ? 0 : 1;
}
