// JadeightRunner — 跨平台 Jadeight 字节码启动器（单 exe）
// 行为：
//   1) 自动寻找 Jadeight 虚拟机（环境变量 JADEIGHT_VM 优先，其次常见路径，最后 PATH）
//   2) 运行当前目录 byteCode/ 下的所有 .bc（可用 JADEIGHT_BC_DIR 覆盖目录）
// 平台适配：宏 __APPLE__ / __linux__ / _WIN32
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

#if defined(_WIN32)
#define PATH_SEP '\\'
#define PATH_SEP_S "\\"
#define EXE_SUFFIX ".exe"
#else
#define PATH_SEP '/'
#define PATH_SEP_S "/"
#define EXE_SUFFIX ""
#endif

static std::string dirOf(const std::string& p) {
    fs::path pp(p);
    return pp.has_parent_path() ? pp.parent_path().string() : std::string(".");
}

static bool isExecutable(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return false;
    auto st = fs::status(p, ec);
    if (!fs::is_regular_file(st)) return false;
#if defined(_WIN32)
    return true;
#else
    auto perm = st.permissions();
    return (perm & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none;
#endif
}

// 在 PATH 中查找可执行文件
static std::string findInPath(const std::string& name) {
    const char* p = std::getenv("PATH");
    if (!p || !*p) return "";
#if defined(_WIN32)
    const char* sep = ";";
#else
    const char* sep = ":";
#endif
    std::string paths(p);
    size_t start = 0;
    while (true) {
        size_t pos = paths.find(sep, start);
        std::string d = paths.substr(start, pos == std::string::npos ? paths.size() - start : pos - start);
        if (!d.empty()) {
            std::string full = d + PATH_SEP_S + name;
            if (isExecutable(full)) return full;
        }
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return "";
}

// 自动寻找 Jadeight 虚拟机（按优先级）
static std::string findVM(const std::string& exeDir, const std::string& cwd) {
    std::vector<std::string> cands;
    // 1) 环境变量显式指定
    if (const char* e = std::getenv("JADEIGHT_VM")) if (*e) cands.push_back(e);
    // 2) 程序同目录 / 当前目录
    std::string vm = "jadeight_vm" EXE_SUFFIX;
    cands.push_back(exeDir + PATH_SEP_S + vm);
    cands.push_back(cwd + PATH_SEP_S + vm);
    // 3) 兄弟工程（本机已知可运行 .bc 的 VM 运行器）
    cands.push_back(exeDir + PATH_SEP_S + ".." + PATH_SEP_S + "JadeightCompiler" + PATH_SEP_S + "build" + PATH_SEP_S + "j8run" EXE_SUFFIX);
    cands.push_back(exeDir + PATH_SEP_S + ".." + PATH_SEP_S + "Jadeight2" + PATH_SEP_S + "cmake-build-debug" + PATH_SEP_S + "Jadeight2" EXE_SUFFIX);
#if defined(__APPLE__) || defined(__linux__)
    // 4) 用户目录 / 系统目录
    if (const char* h = std::getenv("HOME")) if (*h) cands.push_back(std::string(h) + PATH_SEP_S + vm);
    cands.push_back("/usr/local/bin/" + vm);
    cands.push_back("/usr/bin/" + vm);
#endif
    for (auto& c : cands) if (isExecutable(c)) return c;
    // 5) PATH
    for (const char* n : {"jadeight_vm" EXE_SUFFIX, "j8run" EXE_SUFFIX, "Jadeight2" EXE_SUFFIX}) {
        std::string p = findInPath(n);
        if (!p.empty()) return p;
    }
    return "";
}

static std::string quote(const std::string& s) { return "\"" + s + "\""; }

static int runOne(const std::string& vm, const std::string& bc) {
    return std::system((quote(vm) + " " + quote(bc)).c_str());
}

int main(int argc, char** argv) {
    (void)argc;
    std::string exeDir = dirOf(fs::absolute(argv[0]).string());
    std::string cwd = fs::current_path().string();
    std::string bcDir = cwd + PATH_SEP_S + "byteCode";
    if (const char* e = std::getenv("JADEIGHT_BC_DIR")) if (*e) bcDir = e;

    std::string vm = findVM(exeDir, cwd);
    if (vm.empty()) {
        fprintf(stderr, "错误：找不到 Jadeight 虚拟机。\n"
                        "  可用 JADEIGHT_VM 环境变量指定，或把虚拟机复制为 ./jadeight_vm\n");
        return 2;
    }
    printf("Jadeight VM: %s\n", vm.c_str());

    std::error_code ec;
    if (!fs::is_directory(bcDir, ec)) {
        fprintf(stderr, "错误：找不到字节码目录：%s\n", bcDir.c_str());
        return 3;
    }

    std::vector<fs::path> files;
    for (auto& it : fs::directory_iterator(bcDir, ec)) {
        if (it.path().extension() == ".bc") files.push_back(it.path());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        printf("byteCode 目录为空：%s\n", bcDir.c_str());
        return 0;
    }

    int failed = 0;
    for (auto& f : files) {
        printf("\n===== 运行 %s =====\n", f.filename().string().c_str());
        int rc = runOne(vm, f.string());
        if (rc != 0) {
            fprintf(stderr, "!! %s 失败 (exit %d)\n", f.filename().string().c_str(), rc);
            failed = 1;
        }
    }
    return failed;
}
