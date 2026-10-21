#include "AppContext.h"
#include "MainWindow.h"
#include "../Model/ProjectSession.h"

namespace cdaw
{
    AppContext* AppContext::instance = nullptr;

    AppContext::AppContext()
    {
        jassert (instance == nullptr);
        instance = this;

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        auto& ui = engineHost.getUI();
        ui.commandManager = &commandManager;
        ui.getEdit = [this]() -> te::Edit* { return session != nullptr && session->isValid() ? &session->getEdit() : nullptr; };
        ui.getSelectionManager = [this]() -> te::SelectionManager* { return session != nullptr ? &session->getSelectionManager() : nullptr; };
        ui.showMessage = [this] (const juce::String& m) { if (postStatus) postStatus (m); };
    }

    AppContext::~AppContext()
    {
        mainWindow.reset();
        session.reset();
        settings.saveIfNeeded();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        instance = nullptr;
    }

    AppContext& AppContext::get()
    {
        jassert (instance != nullptr);
        return *instance;
    }

    bool AppContext::exists()
    {
        return instance != nullptr;
    }
}
