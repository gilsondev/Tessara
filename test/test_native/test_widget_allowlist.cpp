#include <unity.h>

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

#ifndef TESSARA_PROJECT_DIR
#error "TESSARA_PROJECT_DIR must be set (platformio.ini native build_flags)"
#endif

const char *kForbidden[] = {
    "Arduino.h",   "WiFi.h",     "Preferences.h", "boards/",
    "esp_heap",    "driver/gpio", "esp_lcd",
};

bool ends_with(const std::string &path, const char *suffix) {
    size_t n = std::strlen(suffix);
    return path.size() >= n && path.compare(path.size() - n, n, suffix) == 0;
}

bool is_source(const std::string &path) {
    return ends_with(path, ".cpp") || ends_with(path, ".h") || ends_with(path, ".hpp") ||
           ends_with(path, ".c");
}

void collect(const std::string &dir, std::vector<std::string> &out) {
    DIR *d = opendir(dir.c_str());
    if (!d) return;
    while (dirent *ent = readdir(d)) {
        if (ent->d_name[0] == '.') continue;
        std::string path = dir + "/" + ent->d_name;
        struct stat st = {};
        if (stat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            collect(path, out);
        } else if (S_ISREG(st.st_mode) && is_source(path)) {
            out.push_back(path);
        }
    }
    closedir(d);
}

bool line_has_forbidden(const char *line, const char **which) {
    for (const char *needle : kForbidden) {
        if (std::strstr(line, needle)) {
            if (which) *which = needle;
            return true;
        }
    }
    return false;
}

std::string scan_tree(const std::string &root) {
    std::vector<std::string> files;
    collect(root, files);
    for (const std::string &path : files) {
        FILE *f = std::fopen(path.c_str(), "r");
        if (!f) continue;
        char buf[512];
        int lineno = 0;
        while (std::fgets(buf, sizeof(buf), f)) {
            ++lineno;
            const char *hit = nullptr;
            if (line_has_forbidden(buf, &hit)) {
                std::fclose(f);
                char msg[768];
                std::snprintf(msg, sizeof(msg), "%s:%d: forbidden '%s'", path.c_str(), lineno, hit);
                return msg;
            }
        }
        std::fclose(f);
    }
    return {};
}

}  // namespace

void test_widgets_forbid_platform_includes(void) {
    std::string root = std::string(TESSARA_PROJECT_DIR) + "/firmware/widgets";
    struct stat st = {};
    TEST_ASSERT_EQUAL_MESSAGE(0, stat(root.c_str(), &st), root.c_str());
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
    std::string err = scan_tree(root);
    TEST_ASSERT_TRUE_MESSAGE(err.empty(), err.c_str());
}
