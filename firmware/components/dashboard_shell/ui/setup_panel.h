#pragma once

// Full-screen first-run instructions. Covers the task list until the home network is configured.
namespace setup_panel {

void show(const char *title, const char *headline, const char *name, const char *hint, const char *url);
void hide();
// Home screen while Wi-Fi is up and the Todoist token is still missing. Leaves the top bar visible.
void show_token(const char *url);
void hide_token();
bool token_visible();
bool visible();
void relayout();

}  // namespace setup_panel
