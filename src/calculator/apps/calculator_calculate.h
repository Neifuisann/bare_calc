#pragma once
#include "calculator/runtime/calculator_job.h"
#include "calculator/runtime/calculator_session.h"
#include "input/expression_editor.h"
#include "display/panel_icons.h"
#include <cstdint>
#include <vector>
#ifdef CALCULATOR_REFERENCE_TEST
#include "calculator/runtime/calculator_app.h"
#endif

// The expression app: entry, result, replay, CATALOG/TOOLS/FORMAT/SETTINGS,
// the VARIABLE grid with Store/Define/CALC fields. Calculate and Complex run on
// it with their own capabilities (Complex: the typed evaluator, i entry, its
// CATALOG category, no CALC, the i/angle icon) rather than a copy per app.
// One instance, state file-local in calculator_calculate.cpp. CalculatorApp owns Home, the session, the job service and persistence and
// routes keys, results and frames here.
namespace Calculator {
namespace CalculateApp {
// Source-only normalization: Math errors return to end, other typed errors
// retain a valid source stop or normalize hidden/UTF-8 offsets through the document.
size_t errorReturnCursor(const std::string& source,Error error,size_t position);
#ifdef CALCULATOR_REFERENCE_TEST
CalculatorApp::Snapshot snapshot();
#endif
// Home committed `app`: the entry screen with every menu closed. The draft
// and visible result survive reopening the same app.
void enter(AppId app);
// Reuse the existing VARIABLE/Store, Function/Define and SETTINGS surfaces
// from an app controller. Entry is the return boundary, never executed.
void openShared(AppId app, ExpressionInput::Key menuKey, const ResultSummary* value = nullptr);
bool sharedActive();
// Insertions made by a delegated VARIABLE/Function picker, returned to the
// caller's cell editor when sharedActive() becomes false.
const std::string& sharedInput();
void openHomeSettings();
// True once after OK on Settings > System Setup. The Settings list stays up;
// the host draws the device's System Setup page over it and hands back.
bool takeSystemSetupRequest();
// Leaving for Home or suspending: menus, field menus and parked field drafts
// are dropped; the entry draft stays.
void pause();
// Keypad discontinuity: both editors require review and lose SHIFT.
void interrupt();
void press(ExpressionInput::Key key);
// A job this controller started finished and is still current.
void adopt(const Job& job, Result& result);
// The visible screen owns a live caret (the loop drives the blink).
bool caretVisible();
std::vector<uint8_t> render(bool caretOn);
PanelIcons::State icons();
}  // namespace CalculateApp
}  // namespace Calculator
