#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cdaw
{
    /** Every user-invokable action. IDs start at 0x21000 to stay clear of JUCE's StandardApplicationCommandIDs (0x1001+). Handled by AppCommands through juce::ApplicationCommandManager. */
    namespace CommandIDs
    {
        enum : juce::CommandID
        {
            // File
            newProject = 0x21000,
            openProject,
            saveProject,
            saveProjectAs,
            closeProject,
            importAudio,
            importMidi,
            exportMixdown,
            exportStems,
            exportMidi,
            showPreferences,
            showAudioSettings,
            revealProjectFolder,

            // Edit
            undo = 0x22000,
            redo,
            cut,
            copy,
            paste,
            deleteSelection,
            duplicate,
            selectAll,
            deselectAll,
            splitAtCursor,
            glueClips,
            muteClips,
            deleteRangeCloseGap,
            insertSilence,
            nudgeLeft,
            nudgeRight,
            quantize,
            normaliseClips,
            reverseClips,
            renameSelection,
            toggleSnap,
            gridFiner,
            gridCoarser,
            colourSelection,
            bounceInPlace,

            // Transport
            playStop = 0x23000,
            play,
            stop,
            record,
            returnToStart,
            gotoEnd,
            rewind,
            fastForward,
            toggleLoop,
            togglePunch,
            toggleClick,
            toggleCountIn,
            toggleFollowPlayhead,
            setLoopToSelection,
            addMarker,
            nextMarker,
            prevMarker,
            abortRecording,
            toggleRetroRecord,

            // View
            zoomIn = 0x24000,
            zoomOut,
            zoomToFit,
            zoomInVertical,
            zoomOutVertical,
            toggleMixer,
            toggleEditor,
            toggleBrowser,
            toggleInspector,
            toggleTakeLanes,
            toggleAutomation,

            // Track
            addAudioTrack = 0x25000,
            addInstrumentTrack,
            addFxTrack,
            addGroupTrack,
            addFolderTrack,
            deleteTrack,
            duplicateTrack,
            renameTrack,
            toggleArm,
            toggleMute,
            toggleSolo,
            toggleMonitor,
            freezeTrack,
            unfreezeTrack,
            moveTrackUp,
            moveTrackDown,
            setupMultiOut,

            // Plugins
            scanPlugins = 0x26000,
            showPluginManager,
            openPluginFromFile,

            // Tools
            toolSelect = 0x27000,
            toolRange,
            toolSplit,
            toolDraw,
            toolErase,
            toolMute,
            toolGlue,

            // Takes and comping
            flattenTakes = 0x28000,
            deleteUnusedTakes,
            unpackTakes,
            nextTake,
            prevTake
        };
    }

    namespace CommandCategories
    {
        inline constexpr const char* file      = "File";
        inline constexpr const char* edit      = "Edit";
        inline constexpr const char* transport = "Transport";
        inline constexpr const char* view      = "View";
        inline constexpr const char* track     = "Track";
        inline constexpr const char* plugins   = "Plugins";
        inline constexpr const char* tools     = "Tools";
        inline constexpr const char* takes     = "Takes";
    }
}
