// JadeightRunner — 跨平台 Jadeight 字节码启动器（单 exe）
//
// 行为：
//   1) 自动寻找 Jadeight 虚拟机（JADEIGHT_VM 优先 → 常见路径 → PATH）
//   2) 运行 byteCode/ 下的 .bc（默认全部；可指定文件名只运行部分）
//   3) 外部函数清单 lib/externs.txt（或 --externs / JADEIGHT_EXTERNS）自动传给 VM；
//      --lib 可预加载动态库（透传给 VM，可多次）
//
// 用法：
//   JadeightRunner [选项] [文件.bc ...]
//     不带文件参数：运行 byteCode/ 下全部 .bc
//     带文件参数：只运行指定的（相对路径按 byteCode/ 解析，绝对路径直接用）
//   选项：
//     --vm PATH          指定 VM 可执行文件（默认自动寻找）
//     --externs PATH     外部函数清单（默认 lib/externs.txt；JADEIGHT_EXTERNS 优先）
//     --lib PATH         预加载动态库（可多次，透传给 VM 的 --lib）
//     --list             只列出 byteCode/ 下的 .bc，不运行
//     --stop-on-error    遇到失败立即停止（默认跑完所有并汇总）
//     --quiet            不打印每条运行的横幅
//     -h, --help         帮助
//
// 平台适配：宏 __APPLE__ / __linux__ / _WIN32

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <sys/wait.h>
#include <unistd.h>

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
    // 3) 兄弟工程（本机已知可运行 .bc 的 VM 运行器，覆盖常见构建产物位置）
    cands.push_back(exeDir + PATH_SEP_S + ".." + PATH_SEP_S + "JadeightCompiler" + PATH_SEP_S + "build" + PATH_SEP_S + "j8run" EXE_SUFFIX);
    cands.push_back(exeDir + PATH_SEP_S + ".." + PATH_SEP_S + "Jadeight2" + PATH_SEP_S + "cmake-build-debug" + PATH_SEP_S + "Jadeight2" EXE_SUFFIX);
    cands.push_back(exeDir + PATH_SEP_S + ".." + PATH_SEP_S + "Jadeight2" + PATH_SEP_S + "build" + PATH_SEP_S + "Jadeight2" EXE_SUFFIX);
    cands.push_back(exeDir + PATH_SEP_S + ".." + PATH_SEP_S + "Jadeight2" + PATH_SEP_S + "cmake-build-release" + PATH_SEP_S + "Jadeight2" EXE_SUFFIX);
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

// 在 POSIX 下用 fork+execvp 直接运行（避免 system 的 shell 拼接/引号问题）；
// 返回子进程退出码（-1 表示启动失败）。
static int runOne(const std::vector<std::string>& args) {
#if defined(_WIN32)
    std::string cmd;
    for (auto& a : args) cmd += "\"" + a + "\" ";
    return std::system(cmd.c_str());
#else
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        std::vector<char*> argv;
        for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        fprintf(stderr, "无法执行 VM：%s\n", argv[0]);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); return -1; }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "VM 被信号 %d 终止\n", WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    return -1;
#endif
}

static void printHelp(const char* prog) {
    printf(
        "JadeightRunner — Jadeight 字节码启动器\n"
        "\n"
        "用法: %s [选项] [文件.bc ...]\n"
        "  不带文件参数：运行 byteCode/ 下全部 .bc\n"
        "  带文件参数：只运行指定的（相对路径按 byteCode/ 解析）\n"
        "\n"
        "选项:\n"
        "  --vm PATH         指定 VM 可执行文件（默认自动寻找，JADEIGHT_VM 优先）\n"
        "  --externs PATH    外部函数清单（默认 lib/externs.txt）\n"
        "  --lib PATH        预加载动态库，可多次（透传给 VM 的 --lib）\n"
        "  --list            只列出 byteCode/ 下的 .bc，不运行\n"
        "  --stop-on-error   遇到失败立即停止（默认跑完所有并汇总）\n"
        "  --quiet           不打印每条运行的横幅\n"
        "  -h, --help        显示本帮助\n"
        "\n"
        "环境变量: JADEIGHT_VM / JADEIGHT_BC_DIR / JADEIGHT_LIB_DIR / JADEIGHT_EXTERNS\n",
        prog);
}

int main(int argc, char** argv) {
    std::string exeDir = dirOf(fs::absolute(argv[0]).string());
    std::string cwd = fs::current_path().string();
    std::string bcDir = cwd + PATH_SEP_S + "byteCode";
    std::string libDir = cwd + PATH_SEP_S + "lib";
    if (const char* e = std::getenv("JADEIGHT_BC_DIR")) if (*e) bcDir = e;
    if (const char* e = std::getenv("JADEIGHT_LIB_DIR")) if (*e) libDir = e;

    // ---- 解析参数 ----
    std::string vmOverride, externsOverride;
    std::vector<std::string> libs, fileArgs;
    bool listOnly = false, stopOnError = false, quiet = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { printHelp(argv[0]); return 0; }
        else if (a == "--list") listOnly = true;
        else if (a == "--stop-on-error") stopOnError = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--vm") { if (++i >= argc) { fprintf(stderr, "--vm 需要参数\n"); return 2; } vmOverride = argv[i]; }
        else if (a == "--externs") { if (++i >= argc) { fprintf(stderr, "--externs 需要参数\n"); return 2; } externsOverride = argv[i]; }
        else if (a == "--lib") { if (++i >= argc) { fprintf(stderr, "--lib 需要参数\n"); return 2; } libs.push_back(argv[i]); }
        else if (!a.empty() && a[0] == '-') { fprintf(stderr, "未知选项: %s（-h 查看帮助）\n", a.c_str()); return 2; }
        else fileArgs.push_back(a);
    }

    // ---- 定位 VM ----
    std::string vm = vmOverride.empty() ? findVM(exeDir, cwd) : vmOverride;
    if (vm.empty()) {
        fprintf(stderr,
                "错误：找不到 Jadeight 虚拟机。\n"
                "  可用 JADEIGHT_VM 环境变量或 --vm 指定；或先构建：\n"
                "    cd ../JadeightCompiler && cmake --build build --target j8run -j4\n"
                "  也可把虚拟机复制为 ./jadeight_vm\n");
        return 2;
    }
    {   // 规范化路径显示（去掉 ./.. 等）
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(vm, ec);
        if (!ec) vm = canon.string();
    }
    if (!quiet) printf("Jadeight VM: %s\n", vm.c_str());

    // ---- 外部函数清单 ----
    std::string externs = externsOverride;
    if (externs.empty()) {
        if (const char* e = std::getenv("JADEIGHT_EXTERNS")) if (*e) externs = e;
    }
    if (externs.empty()) {
        std::string f = libDir + PATH_SEP_S + "externs.txt";
        std::error_code ec;
        if (fs::is_regular_file(f, ec)) externs = f;
    }
    if (!externs.empty() && !quiet) printf("外部函数清单: %s\n", externs.c_str());

    // ---- 收集 .bc 文件 ----
    std::vector<fs::path> files;
    if (fileArgs.empty()) {
        std::error_code ec;
        if (!fs::is_directory(bcDir, ec)) {
            fprintf(stderr, "错误：找不到字节码目录：%s\n", bcDir.c_str());
            return 3;
        }
        for (auto& it : fs::directory_iterator(bcDir, ec)) {
            if (it.path().extension() == ".bc") files.push_back(it.path());
        }
        std::sort(files.begin(), files.end());
    } else {
        for (auto& a : fileArgs) {
            fs::path p(a);
            if (p.is_relative()) p = fs::path(bcDir) / p;
            std::error_code ec;
            if (!fs::is_regular_file(p, ec)) {
                fprintf(stderr, "错误：找不到字节码文件：%s\n", p.string().c_str());
                return 3;
            }
            files.push_back(p);
        }
    }
    if (files.empty()) {
        if (listOnly) { printf("byteCode 目录为空：%s\n", bcDir.c_str()); return 0; }
        printf("byteCode 目录为空：%s\n", bcDir.c_str());
        return 0;
    }

    // ---- 只列出 ----
    if (listOnly) {
        printf("byteCode/（%zu 个）:\n", files.size());
        for (auto& f : files) printf("  %s\n", f.filename().string().c_str());
        return 0;
    }

    // ---- 运行 ----
    int failed = 0;
    for (auto& f : files) {
        if (!quiet) printf("\n===== 运行 %s =====\n", f.filename().string().c_str());
        std::vector<std::string> args;
        args.push_back(vm);
        args.push_back(f.string());
        if (!externs.empty()) { args.push_back("--externs"); args.push_back(externs); }
        for (auto& l : libs) { args.push_back("--lib"); args.push_back(l); }
        int rc = runOne(args);
        if (rc != 0) {
            fprintf(stderr, "!! %s 失败 (exit %d)\n", f.filename().string().c_str(), rc);
            failed = 1;
            if (stopOnError) break;
        }
    }
    if (!quiet && failed) fprintf(stderr, "\n共 %zu 个程序，%s\n", files.size(), failed ? "存在失败" : "全部成功");
    return failed;
}
