#pragma once

#include <optional>
#include <string>

#include "brscan/types.h"

// A clean-room table mapping a Brother scan-button config command's `P=`
// paper token (see daemon/button_config.h's ButtonConfig::paper and
// docs/BUTTON.md's "Config command" section) to the brscan::Area the
// printer actually scans for that paper size, at a given dpi.
//
// This is a self-contained data/lookup unit only: it does not decide when
// to apply a paper size. daemon/button_plan.h's PlanButtonScan calls this
// with either the button config's own P= token (Touch-Panel-ON) or the
// explicit `<dest>.paper` config key (Touch-Panel-OFF) -- see
// docs/BUTTON.md's "Touch-Panel precedence" section for that precedence
// rule, and daemon/config.h's `<dest>.paper` key for the config side of
// this.
namespace brscan::scand {

// Returns the scan area for `paper_token` (an exact, case-sensitive match
// against one of the 9 tokens below -- Brother always sends upper case) at
// `dpi`, or std::nullopt if the token is unknown or `dpi` is not positive.
// The caller falls back to some other area (e.g. the printer's own
// ESC I offer) when this returns nullopt.
// The ADF sensor width, in pixels at 300 dpi, that the captured table's
// centered rows (LETTER/LEGAL/A4/LEDGER) were centered within: the
// MFC-J6920DW's 3472 (A3 fills it). Other models have narrower sensors --
// the MFC-J5720DW offers 2527 -- so the daemon's `sensor_width` config key
// re-centers those rows via RecenterAreaForSensor.
inline constexpr int kCapturedSensorWidthAt300 = 3472;

// Re-frames a captured-table `area` (at `dpi`) for a scanner whose sensor is
// `sensor_width_at_300` px wide at 300 dpi instead of the captured 3472:
//   - a CENTERED row (captured x0 > 0, i.e. LETTER/LEGAL/A4/LEDGER) keeps its
//     width and is re-centered in the new sensor, x0 = (sensor - width) / 2;
//   - a corner-registered row (captured x0 == 0: A3, A5, EXECUTIVE, PHOTO,
//     BCARD) is left at x0 = 0;
//   - either way x1 is clamped to the sensor edge, so the request can never
//     run past the sensor (which on the J5720DW wrapped the row).
// Returns `area` unchanged when sensor_width_at_300 == kCapturedSensorWidthAt300
// (the J6920DW: byte-for-byte the captured request) or is not positive. Pure.
brscan::Area RecenterAreaForSensor(const brscan::Area& area, int dpi,
                                   int sensor_width_at_300);

std::optional<brscan::Area> AreaForPaper(const std::string& paper_token,
                                          int dpi);

// True if `paper_token` names one of the 9 known paper sizes below (exact,
// case-sensitive match), false for anything else -- including B4, which
// Brother's protocol supports elsewhere but the LCD panel this project
// targets never offers as a Scan-button paper choice.
bool IsKnownPaper(const std::string& paper_token);

}  // namespace brscan::scand
