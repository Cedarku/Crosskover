#include "HardcoverSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "HardcoverAuthActivity.h"
#include "HardcoverCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int MENU_ITEMS = 2;
const StrId menuNames[MENU_ITEMS] = {StrId::STR_HARDCOVER_API_TOKEN, StrId::STR_HARDCOVER_LINK_ACCOUNT};
}  // namespace

void HardcoverSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void HardcoverSettingsActivity::onExit() { Activity::onExit(); }

void HardcoverSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finishAfterBackPress();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    requestUpdate();
  });
}

void HardcoverSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    // API token, copied from https://hardcover.app/account/api
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_HARDCOVER_API_TOKEN),
                                                HARDCOVER_STORE.getApiToken(), 512, InputType::Password),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            HARDCOVER_STORE.setApiToken(kb.text);
            HARDCOVER_STORE.saveToFile();
          }
        });
  } else if (selectedIndex == 1) {
    // Link account (validates the token against the Hardcover API)
    if (!HARDCOVER_STORE.hasToken()) return;
    startActivityForResult(std::make_unique<HardcoverAuthActivity>(renderer, mappedInput), [](const ActivityResult&) {});
  }
}

void HardcoverSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_HARDCOVER_SYNC));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(MENU_ITEMS),
      static_cast<int>(selectedIndex), [](int index) { return std::string(I18N.get(menuNames[index])); }, nullptr,
      nullptr,
      [this](int index) {
        if (index == 0) {
          return HARDCOVER_STORE.getApiToken().empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
        } else if (index == 1) {
          if (!HARDCOVER_STORE.hasToken()) return std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]";
          return HARDCOVER_STORE.hasIdentity() ? HARDCOVER_STORE.getUsername() : std::string(tr(STR_NOT_SET));
        }
        return std::string(tr(STR_NOT_SET));
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
