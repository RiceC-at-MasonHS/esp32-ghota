#pragma once

// Shared clock logic for every hardware variant. Call every loop().
// System time is synced by GithubReleaseOTA (configTzTime) once Wi-Fi is up.
void clock_tick();
