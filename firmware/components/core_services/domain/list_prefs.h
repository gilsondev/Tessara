#pragma once

#include "domain/model.h"

// Which catalog lists appear on the home, and in what order.
// With nothing saved, every list is shown in the Todoist order.
namespace list_prefs {

struct Choice {
    char name[28];
    char query[220];
    bool shown;
};

// Editor order: saved rows first, then any new catalog list (shown).
int arrange(const ViewItem *catalog, int count, Choice *out, int cap);
// The subset the home draws.
int visible(const ViewItem *catalog, int count, ViewItem *out, int cap);
// Persists the editor order. Matching the catalog with everything shown clears the preference.
// False when the result would hide every list.
bool save(const ViewItem *catalog, int count, const char names[][28], const bool *shown, int names_n);

}  // namespace list_prefs
