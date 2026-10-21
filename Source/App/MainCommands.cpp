// Command definitions and handlers for MainComponent (split out to keep files readable).

#include "MainComponent.h"
#include "AppContext.h"
#include "../Commands/CommandIDs.h"
#include "../Engine/AudioDeviceSetup.h"
#include "../Model/ClipOps.h"
#include "../Model/ProjectSession.h"
#include "../Model/TrackOps.h"
#include "../Model/TempoOps.h"
#include "../UI/Arrange/ArrangeView.h"
#include "../UI/Common/StatusBar.h"
#include "../UI/Plugins/PluginMenus.h"
#include "../UI/LowerZone.h"
#include "../UI/Export/ExportDialog.h"
#include "../UI/Settings/PreferencesDialog.h"

namespace cdaw
{
    void MainComponent::getAllCommands (juce::Array<juce::CommandID>& c)
    {
        const juce::CommandID ids[] = {
            CommandIDs::newProject, CommandIDs::openProject, CommandIDs::saveProject, CommandIDs::saveProjectAs,
            CommandIDs::revealProjectFolder, CommandIDs::showAudioSettings, CommandIDs::showPreferences, CommandIDs::importAudio,
            CommandIDs::importMidi, CommandIDs::exportMixdown, CommandIDs::exportStems, CommandIDs::exportMidi,
            juce::StandardApplicationCommandIDs::quit,

            CommandIDs::undo, CommandIDs::redo, CommandIDs::deleteSelection, CommandIDs::duplicate,
            CommandIDs::selectAll, CommandIDs::deselectAll, CommandIDs::splitAtCursor, CommandIDs::muteClips,
            CommandIDs::deleteRangeCloseGap, CommandIDs::nudgeLeft, CommandIDs::nudgeRight,
            CommandIDs::toggleSnap, CommandIDs::gridFiner, CommandIDs::gridCoarser,

            CommandIDs::playStop, CommandIDs::stop, CommandIDs::record, CommandIDs::abortRecording,
            CommandIDs::returnToStart, CommandIDs::gotoEnd, CommandIDs::toggleLoop, CommandIDs::togglePunch,
            CommandIDs::toggleClick, CommandIDs::toggleCountIn, CommandIDs::toggleFollowPlayhead,
            CommandIDs::setLoopToSelection, CommandIDs::addMarker, CommandIDs::nextMarker, CommandIDs::prevMarker,

            CommandIDs::zoomIn, CommandIDs::zoomOut, CommandIDs::zoomToFit,
            CommandIDs::zoomInVertical, CommandIDs::zoomOutVertical,

            CommandIDs::addAudioTrack, CommandIDs::addInstrumentTrack, CommandIDs::addFxTrack,
            CommandIDs::addGroupTrack, CommandIDs::addFolderTrack, CommandIDs::deleteTrack, CommandIDs::duplicateTrack,
            CommandIDs::moveTrackUp, CommandIDs::moveTrackDown,
            CommandIDs::toggleArm, CommandIDs::toggleMute, CommandIDs::toggleSolo,

            CommandIDs::toolSelect, CommandIDs::toolRange, CommandIDs::toolSplit,
            CommandIDs::toolDraw, CommandIDs::toolErase, CommandIDs::toolMute,

            CommandIDs::openPluginFromFile, CommandIDs::showPluginManager, CommandIDs::toggleInspector,
            CommandIDs::toggleEditor, CommandIDs::toggleMixer, CommandIDs::toggleAutomation
        };

        c.addArray (ids, juce::numElementsInArray (ids));
    }

    void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
    {
        auto* s = getSession();
        const bool hasSession = s != nullptr;
        auto* sm = hasSession ? &s->getSelectionManager() : nullptr;
        auto* vs = hasSession ? &s->getViewState() : nullptr;
        const bool hasClips = sm != nullptr && ! ClipOps::getSelectedClips (*sm).isEmpty();
        const bool hasTrack = arrangeView != nullptr && arrangeView->getFirstSelectedTrack() != nullptr;
        const bool hasRange = vs != nullptr && vs->hasRange();

        using KP = juce::KeyPress;
        const auto ctrl = juce::ModifierKeys::commandModifier;
        const auto shift = juce::ModifierKeys::shiftModifier;
        const auto alt = juce::ModifierKeys::altModifier;

        auto set = [&] (const char* name, const char* desc, const char* cat, bool active = true)
        {
            info.setInfo (name, desc, cat, 0);
            info.setActive (active);
        };

        switch (id)
        {
            // File
            case CommandIDs::newProject:          set ("New Project...", "Create a new project", CommandCategories::file); info.addDefaultKeypress ('n', ctrl); break;
            case CommandIDs::openProject:         set ("Open Project...", "Open a project", CommandCategories::file); info.addDefaultKeypress ('o', ctrl); break;
            case CommandIDs::saveProject:         set ("Save", "Save the project", CommandCategories::file, hasSession); info.addDefaultKeypress ('s', ctrl); break;
            case CommandIDs::saveProjectAs:       set ("Save As...", "Save under a new name", CommandCategories::file, hasSession); info.addDefaultKeypress ('s', ctrl | shift); break;
            case CommandIDs::revealProjectFolder: set ("Show Project Folder", "Open the project folder in Explorer", CommandCategories::file, hasSession); break;
            case CommandIDs::showAudioSettings:   set ("Audio Settings...", "Audio device, sample rate and buffer size", CommandCategories::file); break;
            case CommandIDs::showPreferences:     set ("Preferences...", "Audio, MIDI, recording, plugins and shortcuts", CommandCategories::file); info.addDefaultKeypress (',', ctrl); break;
            case CommandIDs::importAudio:         set ("Import Audio...", "Import audio files at the playhead", CommandCategories::file, hasSession); info.addDefaultKeypress ('i', ctrl); break;
            case CommandIDs::importMidi:          set ("Import MIDI File...", "Import a MIDI file at the playhead", CommandCategories::file, hasSession); info.addDefaultKeypress ('i', ctrl | shift); break;
            case CommandIDs::exportMixdown:       set ("Export Mixdown...", "Render the mix to an audio file", CommandCategories::file, hasSession); info.addDefaultKeypress ('e', ctrl | shift); break;
            case CommandIDs::exportStems:         set ("Export Stems...", "Render each track to its own file", CommandCategories::file, hasSession); break;
            case CommandIDs::exportMidi:          set ("Export MIDI File...", "Write selected (or all) MIDI clips to a .mid file", CommandCategories::file, hasSession); break;
            case juce::StandardApplicationCommandIDs::quit:
                set ("Exit", "Quit CDAW", CommandCategories::file);
                info.addDefaultKeypress (KP::F4Key, alt);
                break;

            // Edit
            case CommandIDs::undo:
                set ("Undo", "Undo", CommandCategories::edit, hasSession && s->getEdit().getUndoManager().canUndo());
                info.addDefaultKeypress ('z', ctrl);
                break;
            case CommandIDs::redo:
                set ("Redo", "Redo", CommandCategories::edit, hasSession && s->getEdit().getUndoManager().canRedo());
                info.addDefaultKeypress ('z', ctrl | shift);
                info.addDefaultKeypress ('y', ctrl);
                break;
            case CommandIDs::deleteSelection:
                set ("Delete", "Delete selected clips, or the selected range", CommandCategories::edit, hasClips || hasRange);
                info.addDefaultKeypress (KP::deleteKey, 0);
                info.addDefaultKeypress (KP::backspaceKey, 0);
                break;
            case CommandIDs::duplicate:           set ("Duplicate", "Duplicate selected clips", CommandCategories::edit, hasClips); info.addDefaultKeypress ('d', ctrl); break;
            case CommandIDs::selectAll:           set ("Select All", "Select all clips", CommandCategories::edit, hasSession); info.addDefaultKeypress ('a', ctrl); break;
            case CommandIDs::deselectAll:         set ("Deselect All", "Clear the selection", CommandCategories::edit, hasSession); info.addDefaultKeypress (KP::escapeKey, 0); break;
            case CommandIDs::splitAtCursor:       set ("Split at Playhead", "Split clips at the playhead", CommandCategories::edit, hasSession); info.addDefaultKeypress ('x', alt); break;
            case CommandIDs::muteClips:           set ("Mute / Unmute Clips", "Toggle mute on selected clips", CommandCategories::edit, hasClips); info.addDefaultKeypress ('m', shift); break;
            case CommandIDs::deleteRangeCloseGap: set ("Delete Range and Close Gap", "Remove the selected range on selected tracks and pull later material back", CommandCategories::edit, hasRange); info.addDefaultKeypress (KP::deleteKey, ctrl | shift); break;
            case CommandIDs::nudgeLeft:           set ("Nudge Left", "Move selected clips left by one grid step", CommandCategories::edit, hasClips); info.addDefaultKeypress (KP::leftKey, ctrl); break;
            case CommandIDs::nudgeRight:          set ("Nudge Right", "Move selected clips right by one grid step", CommandCategories::edit, hasClips); info.addDefaultKeypress (KP::rightKey, ctrl); break;
            case CommandIDs::toggleSnap:
                set ("Snap to Grid", "Toggle snapping", CommandCategories::edit, hasSession);
                info.setTicked (vs != nullptr && vs->snapEnabled.get());
                info.addDefaultKeypress ('j', 0);
                break;
            case CommandIDs::gridFiner:           set ("Finer Grid", "Smaller grid step", CommandCategories::edit, hasSession); info.addDefaultKeypress (']', 0); break;
            case CommandIDs::gridCoarser:         set ("Coarser Grid", "Larger grid step", CommandCategories::edit, hasSession); info.addDefaultKeypress ('[', 0); break;

            // Transport
            case CommandIDs::playStop:            set ("Play / Stop", "Start or stop playback", CommandCategories::transport, hasSession); info.addDefaultKeypress (KP::spaceKey, 0); break;
            case CommandIDs::stop:                set ("Stop", "Stop (press again to return to start)", CommandCategories::transport, hasSession); info.addDefaultKeypress (KP::numberPad0, 0); break;
            case CommandIDs::record:
                set ("Record", "Start / stop recording on armed tracks", CommandCategories::transport, hasSession);
                info.addDefaultKeypress (KP::numberPadMultiply, 0);
                info.addDefaultKeypress ('r', ctrl);
                break;
            case CommandIDs::abortRecording:      set ("Abort Recording", "Stop recording and discard what was recorded", CommandCategories::transport, hasSession && s->getTransport().isRecording()); info.addDefaultKeypress ('r', ctrl | shift); break;
            case CommandIDs::returnToStart:       set ("Return to Start", "Move the playhead to the start", CommandCategories::transport, hasSession); info.addDefaultKeypress (KP::homeKey, 0); break;
            case CommandIDs::gotoEnd:             set ("Go to End", "Move the playhead to the end", CommandCategories::transport, hasSession); info.addDefaultKeypress (KP::endKey, 0); break;
            case CommandIDs::toggleLoop:
                set ("Loop", "Toggle loop (cycle) playback", CommandCategories::transport, hasSession);
                info.setTicked (hasSession && s->getTransport().looping.get());
                info.addDefaultKeypress (KP::numberPadDivide, 0);
                info.addDefaultKeypress ('l', 0);
                break;
            case CommandIDs::togglePunch:
                set ("Punch In/Out", "Record only inside the loop range", CommandCategories::transport, hasSession);
                info.setTicked (hasSession && s->getEdit().recordingPunchInOut.get());
                info.addDefaultKeypress ('p', 0);
                break;
            case CommandIDs::toggleClick:
                set ("Metronome", "Toggle the click", CommandCategories::transport, hasSession);
                info.setTicked (hasSession && s->getEdit().clickTrackEnabled.get());
                info.addDefaultKeypress ('c', 0);
                break;
            case CommandIDs::toggleCountIn:
                set ("Count-in", "One bar count-in before recording", CommandCategories::transport, hasSession);
                info.setTicked (hasSession && s->getEdit().getCountInMode() != te::Edit::CountIn::none);
                break;
            case CommandIDs::toggleFollowPlayhead:
                set ("Follow Playhead", "Scroll the view during playback", CommandCategories::transport, hasSession);
                info.setTicked (vs != nullptr && vs->followPlayhead.get());
                info.addDefaultKeypress ('f', 0);
                break;
            case CommandIDs::addMarker:           set ("Add Marker", "Add a marker at the playhead", CommandCategories::transport, hasSession); info.addDefaultKeypress ('m', ctrl); break;
            case CommandIDs::nextMarker:          set ("Next Marker", "Jump to the next marker", CommandCategories::transport, hasSession); info.addDefaultKeypress (KP::rightKey, alt); break;
            case CommandIDs::prevMarker:          set ("Previous Marker", "Jump to the previous marker", CommandCategories::transport, hasSession); info.addDefaultKeypress (KP::leftKey, alt); break;
            case CommandIDs::setLoopToSelection:  set ("Set Loop to Selection", "Loop the selected range or clips", CommandCategories::transport, hasClips || hasRange); info.addDefaultKeypress ('p', shift); break;

            // View
            case CommandIDs::zoomIn:              set ("Zoom In", "Zoom in horizontally", CommandCategories::view, hasSession); info.addDefaultKeypress ('h', 0); break;
            case CommandIDs::zoomOut:             set ("Zoom Out", "Zoom out horizontally", CommandCategories::view, hasSession); info.addDefaultKeypress ('g', 0); break;
            case CommandIDs::zoomToFit:           set ("Zoom to Fit", "Show the whole project", CommandCategories::view, hasSession); info.addDefaultKeypress ('f', shift); break;
            case CommandIDs::zoomInVertical:      set ("Taller Tracks", "Increase track heights", CommandCategories::view, hasSession); info.addDefaultKeypress ('h', shift); break;
            case CommandIDs::zoomOutVertical:     set ("Shorter Tracks", "Decrease track heights", CommandCategories::view, hasSession); info.addDefaultKeypress ('g', shift); break;

            // Track
            case CommandIDs::addAudioTrack:       set ("Add Audio Track", "Add an audio track", CommandCategories::track, hasSession); info.addDefaultKeypress ('t', ctrl); break;
            case CommandIDs::addInstrumentTrack:  set ("Add Instrument Track...", "Add a track with an instrument plugin", CommandCategories::track, hasSession); info.addDefaultKeypress ('t', ctrl | shift); break;
            case CommandIDs::addFxTrack:          set ("Add FX Track", "Add an effect return channel fed by sends", CommandCategories::track, hasSession); break;
            case CommandIDs::addGroupTrack:       set ("Add Group Track", "Add a group (submix) track", CommandCategories::track, hasSession); break;
            case CommandIDs::addFolderTrack:      set ("Add Folder Track", "Add a folder to organise tracks", CommandCategories::track, hasSession); break;
            case CommandIDs::deleteTrack:         set ("Delete Track", "Delete the selected tracks", CommandCategories::track, hasTrack); info.addDefaultKeypress (KP::deleteKey, shift); break;
            case CommandIDs::duplicateTrack:      set ("Duplicate Track", "Duplicate the selected track", CommandCategories::track, hasTrack); info.addDefaultKeypress ('d', ctrl | shift); break;
            case CommandIDs::moveTrackUp:         set ("Move Track Up", "Move the selected track up", CommandCategories::track, hasTrack); info.addDefaultKeypress (KP::upKey, ctrl | alt); break;
            case CommandIDs::moveTrackDown:       set ("Move Track Down", "Move the selected track down", CommandCategories::track, hasTrack); info.addDefaultKeypress (KP::downKey, ctrl | alt); break;
            case CommandIDs::toggleArm:           set ("Record Enable Track", "Toggle record enable on selected tracks", CommandCategories::track, hasTrack); info.addDefaultKeypress ('r', 0); break;
            case CommandIDs::toggleMute:          set ("Mute Track", "Toggle mute on selected tracks", CommandCategories::track, hasTrack); info.addDefaultKeypress ('m', 0); break;
            case CommandIDs::toggleSolo:          set ("Solo Track", "Toggle solo on selected tracks", CommandCategories::track, hasTrack); info.addDefaultKeypress ('s', 0); break;

            // Tools
            case CommandIDs::toolSelect: set ("Select Tool", "Select and move", CommandCategories::tools, hasSession); info.addDefaultKeypress ('1', 0); info.setTicked (vs && vs->getTool() == Tool::select); break;
            case CommandIDs::toolRange:  set ("Range Tool", "Select a time range", CommandCategories::tools, hasSession); info.addDefaultKeypress ('2', 0); info.setTicked (vs && vs->getTool() == Tool::range); break;
            case CommandIDs::toolSplit:  set ("Split Tool", "Click to split clips", CommandCategories::tools, hasSession); info.addDefaultKeypress ('3', 0); info.setTicked (vs && vs->getTool() == Tool::split); break;
            case CommandIDs::toolDraw:   set ("Draw Tool", "Draw MIDI clips", CommandCategories::tools, hasSession); info.addDefaultKeypress ('4', 0); info.setTicked (vs && vs->getTool() == Tool::draw); break;
            case CommandIDs::toolErase:  set ("Erase Tool", "Click to delete clips", CommandCategories::tools, hasSession); info.addDefaultKeypress ('5', 0); info.setTicked (vs && vs->getTool() == Tool::erase); break;
            case CommandIDs::toolMute:   set ("Mute Tool", "Click to mute clips", CommandCategories::tools, hasSession); info.addDefaultKeypress ('6', 0); info.setTicked (vs && vs->getTool() == Tool::mute); break;

            case CommandIDs::toggleEditor:
                set ("MIDI Editor", "Show / hide the MIDI editor", CommandCategories::view, hasSession);
                info.setTicked (showLowerZone && lowerZone != nullptr && lowerZone->getPage() == LowerZone::Page::editor);
                info.addDefaultKeypress ('e', ctrl);
                break;
            case CommandIDs::toggleMixer:
                set ("Mixer", "Show / hide the mixer", CommandCategories::view, hasSession);
                info.setTicked (showLowerZone && lowerZone != nullptr && lowerZone->getPage() == LowerZone::Page::mixer);
                info.addDefaultKeypress (KP::F3Key, 0);
                break;
            case CommandIDs::toggleAutomation:
                set ("Show / Hide Automation", "Toggle automation lanes on the selected tracks", CommandCategories::view, hasTrack);
                info.addDefaultKeypress ('a', 0);
                break;
            case CommandIDs::toggleInspector:
                set ("Inspector", "Show / hide the track inspector", CommandCategories::view, hasSession);
                info.setTicked (showInspector);
                info.addDefaultKeypress ('i', alt);
                break;

            // Plugins
            case CommandIDs::showPluginManager: set ("Plugin Manager...", "Scan for plugins and manage the plugin list", CommandCategories::plugins); break;
            case CommandIDs::openPluginFromFile: set ("Open Plugin From File...", "Load a VST3 directly onto the selected track", CommandCategories::plugins, hasSession); break;

            default: break;
        }
    }

    void MainComponent::deleteSelection()
    {
        auto* s = getSession();
        if (s == nullptr)
            return;

        auto& edit = s->getEdit();
        auto& sm = s->getSelectionManager();
        auto clips = ClipOps::getSelectedClips (sm);

        if (! clips.isEmpty())
        {
            edit.getUndoManager().beginNewTransaction ("Delete clips");
            sm.deselectAll();
            ClipOps::deleteClips (clips);
        }
        else if (s->getViewState().hasRange())
        {
            edit.getUndoManager().beginNewTransaction ("Delete range");
            te::deleteRegionOfTracks (edit, s->getViewState().getRange(), true, te::CloseGap::no, &sm);
        }
    }

    bool MainComponent::perform (const InvocationInfo& info)
    {
        auto& ctx = AppContext::get();
        auto* s = getSession();

        // Commands that don't need a project
        switch (info.commandID)
        {
            case CommandIDs::newProject:        newProjectDialog(); return true;
            case CommandIDs::openProject:       openProjectDialog(); return true;
            case CommandIDs::showAudioSettings: PreferencesDialog::show (0); return true;
            case CommandIDs::showPreferences:   PreferencesDialog::show (0); return true;
            case CommandIDs::showPluginManager: PluginMenus::showPluginManager (ctx.engine()); return true;
            case juce::StandardApplicationCommandIDs::quit:
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
                return true;
            default: break;
        }

        if (s == nullptr)
            return false;

        auto& edit = s->getEdit();
        auto& transport = s->getTransport();
        auto& sm = s->getSelectionManager();
        auto& vs = s->getViewState();
        auto& um = edit.getUndoManager();

        auto selectedTracks = [&sm]
        {
            juce::Array<te::Track*> tracks;
            for (auto obj : sm.getSelectedObjects())
                if (auto t = dynamic_cast<te::Track*> (obj))
                    tracks.add (t);
            return tracks;
        };

        auto insertAfter = [this]() -> te::Track* { return arrangeView != nullptr ? arrangeView->getFirstSelectedTrack() : nullptr; };

        switch (info.commandID)
        {
            case CommandIDs::saveProject:         saveProject(); return true;
            case CommandIDs::saveProjectAs:       saveProjectAsDialog(); return true;
            case CommandIDs::revealProjectFolder: s->getEditFile().revealToUser(); return true;
            case CommandIDs::importAudio:         importAudioDialog(); return true;
            case CommandIDs::importMidi:          importMidiDialog(); return true;
            case CommandIDs::exportMixdown:       transport.stop (false, false); ExportDialog::show (*s, false); return true;
            case CommandIDs::exportStems:         transport.stop (false, false); ExportDialog::show (*s, true); return true;
            case CommandIDs::exportMidi:          exportMidiDialog(); return true;

            case CommandIDs::undo:                edit.undo(); return true;
            case CommandIDs::redo:                edit.redo(); return true;
            case CommandIDs::deleteSelection:     deleteSelection(); return true;

            case CommandIDs::duplicate:
            {
                um.beginNewTransaction ("Duplicate");
                auto copies = ClipOps::duplicateClips (edit, ClipOps::getSelectedClips (sm));
                sm.deselectAll();
                for (auto c : copies)
                    sm.addToSelection (c);
                return true;
            }

            case CommandIDs::selectAll:
                sm.deselectAll();
                for (auto ct : te::getClipTracks (edit))
                    for (auto c : ct->getClips())
                        sm.addToSelection (c);
                return true;

            case CommandIDs::deselectAll:
                sm.deselectAll();
                vs.clearRange();
                return true;

            case CommandIDs::splitAtCursor:
                ClipOps::splitAt (edit, sm, transport.getPosition());
                return true;

            case CommandIDs::muteClips:
            {
                auto clips = ClipOps::getSelectedClips (sm);
                const bool allMuted = std::all_of (clips.begin(), clips.end(), [] (te::Clip* c) { return c->isMuted(); });
                um.beginNewTransaction ("Mute clips");
                ClipOps::setMuted (clips, ! allMuted);
                return true;
            }

            case CommandIDs::deleteRangeCloseGap:
                um.beginNewTransaction ("Delete range and close gap");
                te::deleteRegionOfTracks (edit, vs.getRange(), true, te::CloseGap::yes, &sm);
                vs.clearRange();
                return true;

            case CommandIDs::nudgeLeft:
            case CommandIDs::nudgeRight:
            {
                auto clips = ClipOps::getSelectedClips (sm);
                if (clips.isEmpty())
                    return true;

                auto step = vs.getGridDuration (clips.getFirst()->getPosition().getStart());
                um.beginNewTransaction ("Nudge");
                ClipOps::moveClips (edit, clips, info.commandID == CommandIDs::nudgeLeft ? te::TimeDuration() - step : step, 0, false);
                return true;
            }

            case CommandIDs::toggleSnap:   vs.snapEnabled = ! vs.snapEnabled.get(); return true;
            case CommandIDs::gridFiner:    vs.gridIndex = juce::jlimit (0, EditViewState::getGridDivisions().size() - 1, vs.gridIndex.get() + 1); return true;
            case CommandIDs::gridCoarser:  vs.gridIndex = juce::jlimit (0, EditViewState::getGridDivisions().size() - 1, vs.gridIndex.get() - 1); return true;

            // Transport
            case CommandIDs::playStop:
                if (transport.isPlaying() || transport.isRecording())  transport.stop (false, false);
                else                                                   transport.play (false);
                return true;

            case CommandIDs::stop:
                if (transport.isPlaying() || transport.isRecording())  transport.stop (false, false);
                else                                                   transport.setPosition (te::TimePosition());
                return true;

            case CommandIDs::record:
                if (transport.isRecording())
                {
                    transport.stop (false, false);
                }
                else
                {
                    transport.record (false, false);

                    if (! transport.isRecording())
                        statusBar->showMessage ("Nothing to record: record-enable a track with an input first (R).");
                }
                return true;

            case CommandIDs::abortRecording:
                transport.stop (true, false);
                statusBar->showMessage ("Recording discarded");
                return true;

            case CommandIDs::returnToStart:  transport.setPosition (te::TimePosition()); if (arrangeView) arrangeView->scrollToTime ({}); return true;
            case CommandIDs::gotoEnd:        transport.setPosition (te::TimePosition() + edit.getLength()); if (arrangeView) arrangeView->scrollToTime (te::TimePosition() + edit.getLength()); return true;
            case CommandIDs::addMarker:
                um.beginNewTransaction ("Add marker");
                TempoOps::addMarker (edit, transport.getPosition());
                return true;
            case CommandIDs::nextMarker:
            case CommandIDs::prevMarker:
            {
                auto t = TempoOps::getNextMarkerTime (edit, transport.getPosition(), info.commandID == CommandIDs::nextMarker);
                transport.setPosition (t);
                if (arrangeView) arrangeView->scrollToTime (t);
                return true;
            }
            case CommandIDs::toggleLoop:     transport.looping = ! transport.looping.get(); return true;
            case CommandIDs::togglePunch:    edit.recordingPunchInOut = ! edit.recordingPunchInOut.get(); return true;
            case CommandIDs::toggleClick:    edit.clickTrackEnabled = ! edit.clickTrackEnabled.get(); return true;
            case CommandIDs::toggleCountIn:
                edit.setCountInMode (edit.getCountInMode() != te::Edit::CountIn::none ? te::Edit::CountIn::none
                                     : ctx.settings.getInt (SettingKeys::countInBars, 1) == 2 ? te::Edit::CountIn::twoBar
                                                                                              : te::Edit::CountIn::oneBar);
                return true;
            case CommandIDs::toggleFollowPlayhead: vs.followPlayhead = ! vs.followPlayhead.get(); return true;

            case CommandIDs::setLoopToSelection:
            {
                auto range = vs.getRange();

                if (range.isEmpty())
                {
                    te::SelectableList clips;
                    for (auto c : ClipOps::getSelectedClips (sm))
                        clips.add (c);
                    range = te::getTimeRangeForSelectedItems (clips);
                }

                if (! range.isEmpty())
                {
                    transport.setLoopRange (range);
                    transport.looping = true;
                }
                return true;
            }

            // View
            case CommandIDs::zoomIn:          if (arrangeView) arrangeView->zoomBy (1.4); return true;
            case CommandIDs::zoomOut:         if (arrangeView) arrangeView->zoomBy (1.0 / 1.4); return true;
            case CommandIDs::zoomToFit:       if (arrangeView) arrangeView->zoomToFit(); return true;
            case CommandIDs::zoomInVertical:  if (arrangeView) arrangeView->zoomTrackHeights (1.25); return true;
            case CommandIDs::zoomOutVertical: if (arrangeView) arrangeView->zoomTrackHeights (0.8); return true;

            // Track
            case CommandIDs::addAudioTrack:
                um.beginNewTransaction ("Add audio track");
                TrackOps::addAudioTrack (edit, &sm, insertAfter());
                return true;
            case CommandIDs::addInstrumentTrack:
                addInstrumentTrackMenu();
                return true;
            case CommandIDs::addFxTrack:
                um.beginNewTransaction ("Add FX track");
                TrackOps::addFxTrack (edit, &sm, insertAfter());
                return true;
            case CommandIDs::addGroupTrack:
                um.beginNewTransaction ("Add group track");
                TrackOps::addFolderTrack (edit, &sm, true, insertAfter());
                return true;
            case CommandIDs::addFolderTrack:
                um.beginNewTransaction ("Add folder track");
                TrackOps::addFolderTrack (edit, &sm, false, insertAfter());
                return true;
            case CommandIDs::deleteTrack:
            {
                auto tracks = selectedTracks();
                um.beginNewTransaction ("Delete track");
                sm.deselectAll();
                TrackOps::deleteTracks (edit, tracks);
                return true;
            }
            case CommandIDs::duplicateTrack:
                if (auto t = insertAfter())
                {
                    um.beginNewTransaction ("Duplicate track");
                    TrackOps::duplicateTrack (edit, *t);
                }
                return true;
            case CommandIDs::moveTrackUp:
            case CommandIDs::moveTrackDown:
                if (auto t = insertAfter())
                {
                    um.beginNewTransaction ("Move track");
                    TrackOps::moveTrack (edit, *t, info.commandID == CommandIDs::moveTrackUp ? -1 : 1);
                }
                return true;
            case CommandIDs::toggleArm:
                for (auto t : selectedTracks())
                    if (auto at = dynamic_cast<te::AudioTrack*> (t))
                        TrackOps::setArmed (*at, ! TrackOps::isArmed (*at));
                return true;
            case CommandIDs::toggleMute:
                um.beginNewTransaction ("Mute");
                for (auto t : selectedTracks())
                    t->setMute (! t->isMuted (false));
                return true;
            case CommandIDs::toggleSolo:
                um.beginNewTransaction ("Solo");
                for (auto t : selectedTracks())
                    t->setSolo (! t->isSolo (false));
                return true;

            // Tools
            case CommandIDs::toolSelect: vs.tool = (int) Tool::select; return true;
            case CommandIDs::toolRange:  vs.tool = (int) Tool::range; return true;
            case CommandIDs::toolSplit:  vs.tool = (int) Tool::split; return true;
            case CommandIDs::toolDraw:   vs.tool = (int) Tool::draw; return true;
            case CommandIDs::toolErase:  vs.tool = (int) Tool::erase; return true;
            case CommandIDs::toolMute:   vs.tool = (int) Tool::mute; return true;

            case CommandIDs::openPluginFromFile: openPluginFromFileDialog(); return true;
            case CommandIDs::toggleAutomation:
                if (arrangeView != nullptr)
                {
                    auto tracks = selectedTracks();
                    if (tracks.isEmpty())
                        if (auto t = arrangeView->getFirstSelectedTrack())
                            tracks.add (t);
                    arrangeView->toggleAutomationLanes (tracks);
                }
                return true;
            case CommandIDs::toggleInspector:    showInspector = ! showInspector; resized(); return true;
            case CommandIDs::toggleEditor:
            case CommandIDs::toggleMixer:
            {
                auto page = info.commandID == CommandIDs::toggleMixer ? LowerZone::Page::mixer : LowerZone::Page::editor;
                const bool isShowingThis = showLowerZone && lowerZone != nullptr && lowerZone->getPage() == page;

                // Ctrl+E with a MIDI clip selected opens that clip.
                if (! isShowingThis && page == LowerZone::Page::editor && lowerZone != nullptr)
                    for (auto c : ClipOps::getSelectedClips (sm))
                        if (auto mc = dynamic_cast<te::MidiClip*> (c)) { lowerZone->editClip (*mc); break; }

                setLowerZoneVisible (! isShowingThis, (int) page);
                return true;
            }

            default:
                return false;
        }
    }
}
