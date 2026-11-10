#include "../../runtime/core/net/websocket.hpp"
#include <cstdint>
#include <cstddef>
#include <sys/socket.h>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 64 * 1024) return 0;
    int sv[2]{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return 0;
    (void)::send(sv[0], data, size, 0);
    ::shutdown(sv[0], SHUT_WR);
    auto res = cfire::websocket::decode_frame(sv[1]);
    (void)res;
    ::close(sv[0]);
    ::close(sv[1]);
    return 0;
}
