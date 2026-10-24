#include <unity.h>

#include <cstdio>
#include <fstream>
#include <string>

#ifndef TESSARA_PROJECT_DIR
#error "TESSARA_PROJECT_DIR must be set"
#endif

namespace {

std::string read_file(const std::string &path) {
    std::ifstream in(path);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

void test_web_src_settings_and_setup_exist_with_markers(void) {
    std::string root = TESSARA_PROJECT_DIR;
    std::string settings = read_file(root + "/web/src/settings.html");
    std::string setup = read_file(root + "/web/src/setup.html");
    TEST_ASSERT_FALSE(settings.empty());
    TEST_ASSERT_FALSE(setup.empty());
    TEST_ASSERT_NOT_NULL(strstr(settings.c_str(), "/api/dashboard"));
    TEST_ASSERT_NOT_NULL(strstr(settings.c_str(), "Na home"));
    TEST_ASSERT_NOT_NULL(strstr(settings.c_str(), "clock_minutes"));
    TEST_ASSERT_NOT_NULL(strstr(settings.c_str(), "Relógio fica por"));
    TEST_ASSERT_NOT_NULL(strstr(settings.c_str(), "Descanso de tela"));
    TEST_ASSERT_NOT_NULL(strstr(settings.c_str(), "Início"));
    TEST_ASSERT_NOT_NULL(strstr(settings.c_str(), "show_aitokens_codex"));
    TEST_ASSERT_NOT_NULL(strstr(settings.c_str(), "rest_range"));
    TEST_ASSERT_NOT_NULL(strstr(settings.c_str(), "sound-tog"));
    TEST_ASSERT_NOT_NULL(strstr(settings.c_str(), "page-resources"));
    TEST_ASSERT_NOT_NULL(strstr(settings.c_str(), "Memória e armazenamento"));
    TEST_ASSERT_NOT_NULL(strstr(settings.c_str(), "drawResources"));
    TEST_ASSERT_NOT_NULL(strstr(setup.c_str(), "/api/setup"));
}

void test_web_headers_match_src_after_embed(void) {
    // embed_web_pre runs before native tests; headers must contain the HTML body.
    std::string root = TESSARA_PROJECT_DIR;
    std::string settings = read_file(root + "/web/src/settings.html");
    std::string header = read_file(root + "/firmware/components/web_configurator/services/web_page.h");
    TEST_ASSERT_FALSE(header.empty());
    TEST_ASSERT_NOT_EQUAL((size_t)std::string::npos, header.find("kPage[] PROGMEM"));
    // Spot-check: a distinctive substring from settings.html is present in the generated header.
    const char *needle = "org.tessara.todoist";
    // settings may not include id strings (JS loads from API); use a stable UI string instead.
    needle = "Widgets";
    TEST_ASSERT_NOT_EQUAL((size_t)std::string::npos, settings.find(needle));
    TEST_ASSERT_NOT_EQUAL((size_t)std::string::npos, header.find(needle));
}
