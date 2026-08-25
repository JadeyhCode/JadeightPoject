#include <cstring>
#include <string>
extern "C" {
int64_t cpp_find(const char* hay, const char* needle) {
    std::string h(hay ? hay : "");
    size_t p = h.find(needle ? needle : "");
    return p == std::string::npos ? -1 : (int64_t)p;
}
}
