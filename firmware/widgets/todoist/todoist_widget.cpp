#include "todoist_widget.h"

namespace {

const tessara::WidgetManifest kManifest = {
    "org.tessara.todoist",
    "Tarefas",
    120,
    100,
    false,
    tessara::HomeTile::Todoist,
    0xE44332,
};

void (*g_show_in)(void *ui_parent) = nullptr;

class TodoistWidget final : public tessara::TsWidget {
public:
    const tessara::WidgetManifest &manifest() const override { return kManifest; }

    void mount(tessara::WidgetContext &context) override {
        generation_ = context.generation();
        if (context.logger()) context.logger()->info("todoist.mount");
        if (g_show_in) g_show_in(context.ui_parent());
        mounted_ = true;
    }

    void on_resize(uint16_t, uint16_t) override {}

    void unmount() override {
        mounted_ = false;
        generation_ = 0;
    }

private:
    bool mounted_ = false;
    uint32_t generation_ = 0;
};

}  // namespace

const char *tessara::todoist_widget_id() { return kManifest.id; }

tessara::TsWidget *tessara::create_todoist_widget() { return new TodoistWidget(); }

void tessara::todoist_widget_bind_show(void (*show_in)(void *ui_parent)) { g_show_in = show_in; }
