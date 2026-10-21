#include "MixerView.h"
#include "InsertSlots.h"
#include "SpectrumView.h"
#include "../Arrange/TrackHeader.h"
#include "../../Engine/MasterAnalyser.h"
#include "../Common/LevelMeter.h"
#include "../LookAndFeel/Palette.h"
#include "../../Model/MixerOps.h"
#include "../../Model/TrackOps.h"

namespace cdaw
{
    ChannelStrip::ChannelStrip (EditViewState& vs, te::Track::Ptr t) : viewState (vs), track (t)
    {
        init();
    }

    ChannelStrip::ChannelStrip (EditViewState& vs) : viewState (vs)
    {
        init();
    }

    te::VolumeAndPanPlugin* ChannelStrip::getVolumePlugin() const
    {
        if (track == nullptr)
            return viewState.edit.getMasterVolumePlugin().get();

        return TrackHeader::getVolumePlugin (*track);
    }

    void ChannelStrip::init()
    {
        setOpaque (true);   // paint() fills the whole component
        meter = std::make_unique<LevelMeter>();
        addAndMakeVisible (*meter);

        nameLabel.setJustificationType (juce::Justification::centred);
        nameLabel.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        nameLabel.setText (track != nullptr ? track->getName() : juce::String ("Master"), juce::dontSendNotification);
        if (track != nullptr)
        {
            nameLabel.setEditable (false, true, false);
            nameLabel.setTooltip ("Double-click to rename. Right-click the strip for track options.");
            nameLabel.onTextChange = [this]
            {
                viewState.edit.getUndoManager().beginNewTransaction ("Rename track");
                track->setName (nameLabel.getText());
            };
        }
        else
        {
            nameLabel.setInterceptsMouseClicks (false, false);
        }

        addAndMakeVisible (nameLabel);

        auto* at = dynamic_cast<te::AudioTrack*> (track.get());

        if (track == nullptr)
            inserts = std::make_unique<InsertSlots> (viewState.edit.getMasterPluginList(), true);
        else if (track->canContainPlugins())
            inserts = std::make_unique<InsertSlots> (track, true);

        if (track != nullptr && at != nullptr && TrackOps::getKind (*track) != TrackKind::fx)
            sends = std::make_unique<SendSlots> (track, true);

        // Inserts and sends sit in fixed-height sections that scroll when the list is longer.
        auto relayout = [this] { if (onLayoutChanged) onLayoutChanged(); else resized(); };

        for (auto* view : { &insertsView, &sendsView })
        {
            view->setScrollBarsShown (true, false);
            view->setScrollBarThickness (6);
            addChildComponent (view);
        }

        if (inserts != nullptr)
        {
            inserts->onIdealHeightChanged = relayout;
            insertsView.setViewedComponent (inserts.get(), false);
            insertsView.setVisible (true);
        }

        if (sends != nullptr)
        {
            sends->onIdealHeightChanged = relayout;
            sendsView.setViewedComponent (sends.get(), false);
            sendsView.setVisible (true);
        }

        if (track == nullptr)
        {
            // The master has no input, so its input row holds the analyser switch.
            spectrumButton.setClickingTogglesState (false);
            spectrumButton.setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            spectrumButton.setTooltip ("Show / hide the spectrum analyser of the master output");
            spectrumButton.onClick = [this] { if (onToggleSpectrum) onToggleSpectrum(); };
            addAndMakeVisible (spectrumButton);
        }

        if (track != nullptr)
        {

            if (at != nullptr && TrackOps::getKind (*track) != TrackKind::fx)
            {
                inputButton.setTooltip ("Input");
                inputButton.onClick = [this, at]
                {
                    juce::PopupMenu m;
                    auto current = TrackOps::getAssignedInput (*at);
                    m.addItem ("No input", true, current == nullptr, [at] { TrackOps::assignInput (*at, nullptr); });

                    for (bool midi : { false, true })
                    {
                        m.addSectionHeader (midi ? "MIDI inputs" : "Audio inputs");
                        for (auto& c : TrackOps::getInputChoices (at->edit, midi))
                        {
                            auto* inst = c.instance;
                            m.addItem (c.name, true, inst == current, [at, inst]
                            {
                                at->edit.getUndoManager().beginNewTransaction ("Set input");
                                TrackOps::assignInput (*at, inst);
                            });
                        }
                    }

                    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&inputButton));
                };
                addAndMakeVisible (inputButton);
            }

            outputButton.setTooltip ("Output routing");
            outputButton.onClick = [this] { MixerOps::createOutputMenu (*track).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputButton)); };
            addAndMakeVisible (outputButton);

            muteButton.setColour (juce::TextButton::buttonOnColourId, Palette::mute);
            soloButton.setColour (juce::TextButton::buttonOnColourId, Palette::solo);
            muteButton.onClick = [this] { viewState.edit.getUndoManager().beginNewTransaction ("Mute"); track->setMute (! track->isMuted (false)); };
            soloButton.onClick = [this] { viewState.edit.getUndoManager().beginNewTransaction ("Solo"); track->setSolo (! track->isSolo (false)); };
            addAndMakeVisible (muteButton);
            addAndMakeVisible (soloButton);

            if (at != nullptr && TrackOps::getKind (*track) != TrackKind::fx)
            {
                armButton.setColour (juce::TextButton::buttonOnColourId, Palette::record);
                monitorButton.setColour (juce::TextButton::buttonOnColourId, Palette::monitor);
                armButton.onClick = [at] { TrackOps::setArmed (*at, ! TrackOps::isArmed (*at)); };
                monitorButton.onClick = [at]
                {
                    using MM = te::InputDevice::MonitorMode;
                    auto m = TrackOps::getMonitorMode (*at);
                    TrackOps::setMonitorMode (*at, m == MM::automatic ? MM::on : m == MM::on ? MM::off : MM::automatic);
                };
                addAndMakeVisible (armButton);
                addAndMakeVisible (monitorButton);
            }
        }

        if (auto vp = getVolumePlugin())
        {
            te::Plugin::Ptr keep = vp;
            fader.setSliderStyle (juce::Slider::LinearVertical);
            fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            fader.setRange (0.0, 1.0);
            fader.setDoubleClickReturnValue (true, te::decibelsToVolumeFaderPosition (0.0f));
            fader.onDragStart = [keep, vp] { vp->volParam->parameterChangeGestureBegin(); };
            fader.onDragEnd = [keep, vp] { vp->volParam->parameterChangeGestureEnd(); };
            fader.onValueChange = [this, vp] { if (! updating) vp->setSliderPos ((float) fader.getValue()); };
            addAndMakeVisible (fader);

            pan.onDragStart = [vp] { vp->panParam->parameterChangeGestureBegin(); };
            pan.onDragEnd = [vp] { vp->panParam->parameterChangeGestureEnd(); };
            pan.onValueChange = [this, vp] { if (! updating) vp->setPan ((float) pan.getValue()); };
            addAndMakeVisible (pan);

            dbLabel.setJustificationType (juce::Justification::centred);
            dbLabel.setFont (juce::FontOptions (11.0f));
            dbLabel.setColour (juce::Label::textColourId, Palette::textDim);
            addAndMakeVisible (dbLabel);
        }

        if (track != nullptr)
        {
            if (auto mp = TrackHeader::getMeterPlugin (*track))
                meter->attach (&mp->measurer, mp);
        }

        viewState.selectionManager.addChangeListener (this);
        refreshState();
        startTimerHz (10);
    }

    ChannelStrip::~ChannelStrip()
    {
        viewState.selectionManager.removeChangeListener (this);
    }

    void ChannelStrip::refreshState()
    {
        // Master meter comes from the playback context, which can be recreated.
        if (track == nullptr)
            if (auto epc = viewState.edit.getCurrentPlaybackContext())
                meter->attach (&epc->masterLevels);

        if (auto vp = getVolumePlugin())
        {
            juce::ScopedValueSetter<bool> svs (updating, true);
            if (! fader.isMouseButtonDown()) fader.setValue (vp->getSliderPos(), juce::dontSendNotification);
            if (! pan.isMouseButtonDown())   pan.setValue (vp->getPan(), juce::dontSendNotification);

            auto db = vp->getVolumeDb();
            dbLabel.setText (db <= -96.0f ? juce::String ("-inf") : juce::String (db, 1), juce::dontSendNotification);
        }

        if (track != nullptr)
        {
            if (nameLabel.getText() != track->getName())
                nameLabel.setText (track->getName(), juce::dontSendNotification);

            muteButton.setToggleState (track->isMuted (false), juce::dontSendNotification);
            soloButton.setToggleState (track->isSolo (false), juce::dontSendNotification);
            outputButton.setButtonText (MixerOps::describeOutput (*track));

            if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
            {
                // One input lookup per tick (each walks the edit's input devices).
                auto in = TrackOps::getAssignedInput (*at);

                if (inputButton.isVisible())
                    inputButton.setButtonText (in != nullptr ? in->getInputDevice().getAliasOrName() : juce::String ("No input"));

                armButton.setToggleState (in != nullptr && in->isRecordingEnabled (at->itemID), juce::dontSendNotification);
                using MM = te::InputDevice::MonitorMode;
                auto m = in != nullptr ? in->getInputDevice().getMonitorMode() : MM::automatic;
                monitorButton.setButtonText (m == MM::automatic ? "A" : "I");
                monitorButton.setToggleState (m == MM::on || (m == MM::automatic && in != nullptr && in->isLivePlayEnabled (*at)), juce::dontSendNotification);
            }
        }
    }

    void ChannelStrip::paint (juce::Graphics& g)
    {
        const bool selected = track != nullptr && viewState.selectionManager.isSelected (track.get());
        g.fillAll (selected ? Palette::panelRaised : Palette::panelBackground);

        auto colour = track != nullptr ? track->getColour() : Palette::accent;
        g.setColour (colour);
        g.fillRect (0, 0, getWidth(), 4);

        g.setColour (Palette::windowBackground);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

        if (track != nullptr)
        {
            g.setColour (Palette::textDim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (TrackOps::getKindName (TrackOps::getKind (*track)), getLocalBounds().removeFromBottom (14), juce::Justification::centred);
        }

        drawFaderScale (g);
    }

    void ChannelStrip::drawFaderScale (juce::Graphics& g)
    {
        // dB marks left of the fader groove, placed with the fader's own value-to-position mapping
        // so they line up with the cap at any panel height.
        if (! fader.isVisible() || fader.getHeight() < 60)
            return;

        const auto bounds = fader.getBounds();
        const float grooveX = (float) bounds.getCentreX() - 2.0f;
        const float labelRight = grooveX - 17.0f;        // clear of the fader cap (28 px wide)
        const float tickLeft = labelRight + 2.0f;
        float lastY = -100.0f;

        g.setFont (juce::FontOptions (9.5f));

        for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            auto pos = te::decibelsToVolumeFaderPosition (db);
            if (pos < 0.0f || pos > 1.0f)
                continue;

            auto y = (float) bounds.getY() + (float) fader.getPositionOfValue (pos);

            // Leave out marks that would crowd the previous one on short faders.
            if (y - lastY < 11.0f)
                continue;

            lastY = y;
            const bool unity = db == 0.0f;

            g.setColour (unity ? Palette::text.withAlpha (0.75f) : Palette::textDim.withAlpha (0.55f));
            g.drawHorizontalLine ((int) std::round (y), tickLeft, grooveX - 3.0f);

            g.setColour (unity ? Palette::text : Palette::textDim);
            auto label = db > 0.0f ? "+" + juce::String ((int) db) : juce::String ((int) db);
            g.drawText (label, juce::Rectangle<float> (labelRight - 26.0f, y - 6.0f, 26.0f, 12.0f), juce::Justification::centredRight, false);
        }

        // Bottom of the fader: silence.
        auto bottom = (float) bounds.getY() + (float) fader.getPositionOfValue (0.0);
        if (bottom - lastY >= 11.0f)
        {
            g.setColour (Palette::textDim.withAlpha (0.55f));
            g.drawHorizontalLine ((int) std::round (bottom), tickLeft, grooveX - 3.0f);
            g.setColour (Palette::textDim);
            g.drawText (juce::String::fromUTF8 ("-\xe2\x88\x9e"), juce::Rectangle<float> (labelRight - 26.0f, bottom - 6.0f, 26.0f, 12.0f),
                        juce::Justification::centredRight, false);
        }
    }

    // Strip layout, top to bottom. Everything except the fader has a fixed height, so a taller
    // mixer panel goes entirely into fader travel.
    namespace StripLayout
    {
        constexpr int marginTop = 8, marginBottom = 2, marginX = 4;
        constexpr int nameH = 20, ioRowH = 20, gap = 2, sectionGap = 6;
        constexpr int panH = 30, dbH = 16, buttonsH = 20, kindH = 14;

        constexpr int headerHeight()    { return marginTop + nameH + gap + ioRowH + gap + ioRowH + sectionGap; }
        constexpr int footerHeight()    { return dbH + gap + buttonsH + kindH + marginBottom; }
    }

    int ChannelStrip::getFixedHeight (int insRows, int sndRows)
    {
        using namespace StripLayout;
        auto sections = insRows * InsertSlots::rowPitch + (insRows > 0 ? sectionGap : 0)
                      + sndRows * InsertSlots::rowPitch + (sndRows > 0 ? sectionGap : 0);
        return headerHeight() + sections + panH + footerHeight();
    }

    int ChannelStrip::getWantedInsertRows() const
    {
        return inserts != nullptr ? inserts->getIdealHeight() / InsertSlots::rowPitch : 0;
    }

    int ChannelStrip::getWantedSendRows() const
    {
        return sends != nullptr ? sends->getIdealHeight() / InsertSlots::rowPitch : 0;
    }

    void ChannelStrip::setSectionRows (int ins, int snd)
    {
        if (ins != insertRows || snd != sendRows)
        {
            insertRows = ins;
            sendRows = snd;
            resized();
        }
    }

    void ChannelStrip::resized()
    {
        using namespace StripLayout;
        auto r = getLocalBounds().withTrimmedTop (marginTop).withTrimmedBottom (marginBottom).reduced (marginX, 0);

        nameLabel.setBounds (r.removeFromTop (nameH));
        r.removeFromTop (gap);

        // Input and output keep their rows even when hidden, so every strip lines up.
        inputButton.setBounds (r.getX(), r.getY(), r.getWidth(), ioRowH);
        spectrumButton.setBounds (r.removeFromTop (ioRowH));
        r.removeFromTop (gap);
        outputButton.setBounds (r.removeFromTop (ioRowH));
        r.removeFromTop (sectionGap);

        auto placeSection = [&r] (juce::Viewport& view, juce::Component* content, int idealH, int rows)
        {
            if (rows <= 0)
                return;

            auto area = r.removeFromTop (rows * InsertSlots::rowPitch);
            r.removeFromTop (sectionGap);

            if (content == nullptr)
                return;

            view.setBounds (area);
            const bool scrolls = idealH > area.getHeight();
            content->setSize (area.getWidth() - (scrolls ? view.getScrollBarThickness() + 1 : 0), idealH);
        };

        placeSection (insertsView, inserts.get(), inserts != nullptr ? inserts->getIdealHeight() : 0, insertRows);
        placeSection (sendsView, sends.get(), sends != nullptr ? sends->getIdealHeight() : 0, sendRows);

        // Footer: dB readout, one row of M S R A, track kind.
        r.removeFromBottom (kindH);
        auto buttons = r.removeFromBottom (buttonsH);
        r.removeFromBottom (gap);
        dbLabel.setBounds (r.removeFromBottom (dbH));

        juce::Array<juce::Component*> visibleButtons;
        for (auto* b : { &muteButton, &soloButton, &armButton, &monitorButton })
            if (b->isVisible())
                visibleButtons.add (b);

        if (! visibleButtons.isEmpty())
        {
            auto w = buttons.getWidth() / visibleButtons.size();
            for (auto* b : visibleButtons)
                b->setBounds (buttons.removeFromLeft (w).reduced (1, 0));
        }

        // Pan, then the fader and meter take all the remaining height.
        pan.setBounds (r.removeFromTop (panH).withSizeKeepingCentre (PanKnob::widthForHeight (panH), panH));
        meter->setBounds (r.removeFromRight (12).reduced (0, 4));
        fader.setBounds (r);
    }

    void ChannelStrip::mouseDown (const juce::MouseEvent& e)
    {
        if (track == nullptr)
            return;

        viewState.selectionManager.selectOnly (track.get());

        if (e.mods.isPopupMenu() && onTrackMenu)
            onTrackMenu (*track);
    }

    //==============================================================================
    MixerView::MixerView (EditViewState& vs) : viewState (vs)
    {
        setOpaque (true);   // paint() fills the whole component
        viewport.setViewedComponent (&stripHolder, false);
        viewport.setScrollBarsShown (false, true);
        addAndMakeVisible (viewport);

        master = std::make_unique<ChannelStrip> (viewState);
        master->onLayoutChanged = [this] { layoutStrips(); };
        master->onToggleSpectrum = [this] { viewState.state.setProperty (ViewIDs::showSpectrum, ! isSpectrumShown(), nullptr); };
        addAndMakeVisible (*master);
        syncSpectrum();

        viewState.edit.state.addListener (this);
        rebuild();
    }

    MixerView::~MixerView()
    {
        viewState.edit.state.removeListener (this);
    }

    void MixerView::rebuild()
    {
        strips.clear();

        for (auto t : TrackOps::getArrangeTracks (viewState.edit))
        {
            // Plain folders have no audio path; groups do.
            if (auto ft = dynamic_cast<te::FolderTrack*> (t); ft != nullptr && ! ft->isSubmixFolder())
                continue;

            auto* strip = strips.add (new ChannelStrip (viewState, t));
            strip->onTrackMenu = [this] (te::Track& tr) { if (onTrackMenu) onTrackMenu (tr); };
            strip->onLayoutChanged = [this] { layoutStrips(); };
            stripHolder.addAndMakeVisible (strip);
        }

        resized();
    }

    void MixerView::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::windowBackground);
    }

    void MixerView::resized()
    {
        layoutStrips();
    }

    bool MixerView::isSpectrumShown() const
    {
        return viewState.analyser != nullptr && (bool) viewState.state.getProperty (ViewIDs::showSpectrum, false);
    }

    void MixerView::syncSpectrum()
    {
        const bool shown = isSpectrumShown();

        if (shown && spectrum == nullptr)
        {
            spectrum = std::make_unique<SpectrumView> (viewState.analyser->getFifo());
            addAndMakeVisible (*spectrum);
        }
        else if (! shown)
        {
            spectrum.reset();
        }

        master->setSpectrumShown (shown);
        layoutStrips();
    }

    void MixerView::layoutStrips()
    {
        auto r = getLocalBounds();

        // The analyser sits to the right of the master strip.
        if (spectrum != nullptr)
            spectrum->setBounds (r.removeFromRight (juce::jlimit (260, 560, getWidth() / 3)).reduced (4, 4));

        auto masterArea = r.removeFromRight (ChannelStrip::width + 10);
        r.removeFromRight (4);
        viewport.setBounds (r);

        const bool needsScrollBar = strips.size() * ChannelStrip::width > r.getWidth();
        auto h = juce::jmax (100, r.getHeight() - (needsScrollBar ? viewport.getScrollBarThickness() : 0));
        stripHolder.setSize (juce::jmax (r.getWidth(), strips.size() * ChannelStrip::width), h);

        // Every strip reserves the same insert and send rows, so the pan knobs and faders line up.
        // Enough rows for the longest list; when the panel is short, rows give way (the lists then
        // scroll) before the fader drops below its minimum.
        int insRows = master->getWantedInsertRows(), sndRows = 0;

        for (auto s : strips)
        {
            insRows = juce::jmax (insRows, s->getWantedInsertRows());
            sndRows = juce::jmax (sndRows, s->getWantedSendRows());
        }

        insRows = juce::jlimit (1, 8, insRows);
        sndRows = juce::jmin (6, sndRows);

        while (ChannelStrip::getFixedHeight (insRows, sndRows) + ChannelStrip::minFaderHeight > h)
        {
            if (insRows > 1 && insRows >= sndRows)  --insRows;
            else if (sndRows > 1)                    --sndRows;
            else                                     break;
        }

        int x = 0;
        for (auto s : strips)
        {
            s->setSectionRows (insRows, sndRows);
            s->setBounds (x, 0, ChannelStrip::width, h);
            x += ChannelStrip::width;
        }

        master->setSectionRows (insRows, sndRows);
        master->setBounds (masterArea.withHeight (h).withSizeKeepingCentre (ChannelStrip::width, h).withY (masterArea.getY()));
    }
}
