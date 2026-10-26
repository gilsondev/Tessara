#include "todoist_widget.h"

namespace {

const tessera::WidgetManifest kManifest = {
    "org.tessera.todoist",
    "Tarefas",
    120,
    100,
    false,
    tessera::HomeTile::Todoist,
    0xE44332,
    false,
};

void (*g_show_in)(void *ui_parent) = nullptr;

class TodoistWidget final : public tessera::TsWidget {
public:
    const tessera::WidgetManifest &manifest() const override { return kManifest; }

    void mount(tessera::WidgetContext &context) override {
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

const char *tessera::todoist_widget_id() { return kManifest.id; }

tessera::TsWidget *tessera::create_todoist_widget() { return new TodoistWidget(); }

void tessera::todoist_widget_bind_show(void (*show_in)(void *ui_parent)) { g_show_in = show_in; }
