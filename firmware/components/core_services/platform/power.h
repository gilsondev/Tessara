#pragma once

// Battery sense on the JC3248W535. USB power without a cell is normal; the icon stays hidden then.
namespace power {

bool battery_present();
// 0–100 when a pack is present, otherwise −1.
int battery_percent();

}  // namespace power
