#pragma once

// Task ids the user already checked off. They stay here until Todoist accepts the close,
// including across a reboot while the internet is down.
namespace pending_closes {

constexpr int kMax = 32;

// Each id is a 19-character C string. Returns how many were stored.
int load(char ids[][20], int cap);
void save(const char ids[][20], int count);

}  // namespace pending_closes
