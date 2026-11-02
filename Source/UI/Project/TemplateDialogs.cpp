#include "TemplateDialogs.h"
#include "../../App/AppContext.h"
#include "../../Model/TemplateOps.h"

namespace cdaw::TemplateDialogs
{
    static void status (const juce::String& message)
    {
        if (auto& post = AppContext::get().postStatus)
            post (message);
    }

    void askNameAndSave (juce::Component* parent, const juce::String& title, const juce::String& defaultName,
                         const juce::File& folder, const char* suffix,
                         std::function<bool (const juce::File&)> write,
                         std::function<void (const juce::File&)> done)
    {
        auto* aw = new juce::AlertWindow (title, "Name:", juce::MessageBoxIconType::NoIcon, parent);
        aw->addTextEditor ("name", defaultName);
        aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, folder, suffix, write, done, parent, title] (int result)
        {
            if (result != 1)
                return;

            auto name = juce::File::createLegalFileName (aw->getTextEditorContents ("name").trim());
            if (name.isEmpty())
                return;

            auto file = folder.getChildFile (name + suffix);

            auto doWrite = [file, write, done]
            {
                auto ok = write (file);
                if (done)
                    done (ok ? file : juce::File());
            };

            if (! file.existsAsFile())
            {
                doWrite();
                return;
            }

            auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, title,
                                                                         "\"" + name + "\" already exists. Replace it?",
                                                                         "Replace", "Cancel", parent);
            juce::AlertWindow::showAsync (options, [doWrite] (int r) { if (r == 1) doWrite(); });
        }), true);
    }

    void saveTrackPreset (juce::Component* parent, te::Track& track)
    {
        te::Track::Ptr t = &track;
        askNameAndSave (parent, "Save Track Preset", track.getName(), TemplateOps::getTrackPresetsFolder(), TemplateOps::trackPresetSuffix,
                        [t] (const juce::File& f) { return TemplateOps::saveTrackPreset (*t, f); },
                        [] (const juce::File& f)
                        {
                            status (f != juce::File() ? "Saved track preset \"" + f.getFileNameWithoutExtension() + "\" (Insert Track Preset to use it)"
                                                      : juce::String ("Could not save the track preset"));
                        });
    }

    juce::PopupMenu createInsertTrackPresetMenu (te::Edit& edit, te::Track* after, te::SelectionManager* sm)
    {
        juce::PopupMenu m;
        te::Track::Ptr afterPtr = after;
        auto presets = TemplateOps::listFiles (TemplateOps::getTrackPresetsFolder(), TemplateOps::trackPresetSuffix);

        if (presets.isEmpty())
            m.addItem ("(none yet: right-click a track > Save as Track Preset)", false, false, [] {});

        for (auto& f : presets)
        {
            m.addItem (f.getFileNameWithoutExtension(), [&edit, f, afterPtr, sm]
            {
                edit.getUndoManager().beginNewTransaction ("Insert track preset");
                auto track = TemplateOps::insertTrackPreset (edit, f, afterPtr.get(), sm);
                status (track != nullptr ? "Inserted \"" + f.getFileNameWithoutExtension() + "\""
                                         : "Could not read " + f.getFileName());
            });
        }

        m.addSeparator();
        m.addItem ("Open Track Presets Folder", [] { TemplateOps::getTrackPresetsFolder().startAsProcess(); });
        return m;
    }
}
