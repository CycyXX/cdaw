#include "InsertSlots.h"
#include "../LookAndFeel/Palette.h"
#include "../Plugins/PluginMenus.h"
#include "../../Model/MixerOps.h"
#include "../../Model/TrackOps.h"

namespace cdaw
{
    //==============================================================================
    struct InsertSlots::Row : public juce::Component
    {
        Row (te::Plugin::Ptr p, bool c) : plugin (p), compact (c)
        {
            power.setClickingTogglesState (false);
            power.setToggleState (plugin->isEnabled(), juce::dontSendNotification);
            power.setColour (juce::TextButton::buttonOnColourId, Palette::accentDim);
            power.setButtonText (juce::String::charToString ((juce::juce_wchar) 0x25CF));
            power.setTooltip ("Bypass on/off");
            power.onClick = [this]
            {
                plugin->edit.getUndoManager().beginNewTransaction ("Bypass plugin");
                plugin->setEnabled (! plugin->isEnabled());
            };
            addAndMakeVisible (power);

            name.setButtonText (plugin->getName() + (plugin->isMissing() ? " (missing)" : ""));
            name.setTooltip (plugin->getName() + " - click to open, right-click for options");
            name.onClick = [this] { showWindow(); };
            name.setColour (juce::TextButton::textColourOffId, plugin->isEnabled() ? Palette::text : Palette::textDisabled);
            name.addMouseListener (this, false);
            addAndMakeVisible (name);

            if (! compact)
            {
                remove.setButtonText ("x");
                remove.setTooltip ("Remove");
                remove.onClick = [this]
                {
                    plugin->edit.getUndoManager().beginNewTransaction ("Remove plugin");
                    plugin->deleteFromParent();
                };
                addAndMakeVisible (remove);
            }
        }

        void showWindow()
        {
            if (plugin->isMissing())
            {
                plugin->edit.engine.getUIBehaviour().showWarningMessage (plugin->getName() + " is not installed or failed to load. Its settings are kept.");
                return;
            }

            plugin->showWindowExplicitly();
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
                return;

            auto p = plugin;
            auto* track = p->getOwnerTrack();
            auto* owningList = p->getOwnerList();
            juce::PopupMenu m;
            m.addItem ("Open Editor", [this] { showWindow(); });
            m.addItem (p->isEnabled() ? "Bypass" : "Enable", [p]
            {
                p->edit.getUndoManager().beginNewTransaction ("Bypass plugin");
                p->setEnabled (! p->isEnabled());
            });

            juce::ignoreUnused (track);

            if (owningList != nullptr)
            {
                juce::Array<te::Plugin*> inserts;
                for (auto other : owningList->getPlugins())
                    if (MixerOps::isInsertPlugin (*other))
                        inserts.add (other);

                auto idx = inserts.indexOf (p.get());
                te::Track::Ptr keepTrack = track;

                auto moveTo = [p, owningList, keepTrack] (te::Plugin* neighbour, bool after)
                {
                    if (neighbour == nullptr) return;
                    p->edit.getUndoManager().beginNewTransaction ("Move plugin");
                    auto keep = p;
                    p->removeFromParent();
                    auto target = owningList->indexOf (neighbour) + (after ? 1 : 0);
                    owningList->insertPlugin (keep, target, nullptr);
                };

                m.addItem ("Move Up", idx > 0, false, [moveTo, inserts, idx] { moveTo (inserts[idx - 1], false); });
                m.addItem ("Move Down", idx < inserts.size() - 1, false, [moveTo, inserts, idx] { moveTo (inserts[idx + 1], true); });
            }

            if (MixerOps::canSidechain (*p))
            {
                m.addSeparator();
                m.addSubMenu ("Sidechain from", MixerOps::createSidechainMenu (*p));
            }

            if (auto ext = dynamic_cast<te::ExternalPlugin*> (p.get()))
            {
                auto latency = ext->getLatencySeconds() * 1000.0;
                m.addSeparator();
                m.addItem ("Latency: " + juce::String (latency, 1) + " ms", false, false, [] {});
                m.addItem ("Vendor: " + ext->getVendor(), false, false, [] {});
            }

            m.addSeparator();
            m.addItem ("Remove", [p]
            {
                p->edit.getUndoManager().beginNewTransaction ("Remove plugin");
                p->deleteFromParent();
            });
            m.showMenuAsync ({});
        }

        void paint (juce::Graphics& g) override
        {
            if (! compact && latencyMs > 0.05)
            {
                g.setColour (Palette::textDim);
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (juce::String (latencyMs, 1) + "ms", latencyArea, juce::Justification::centredRight);
            }
        }

        // "SC" badge on the name when the plugin is keyed from another track.
        void paintOverChildren (juce::Graphics& g) override
        {
            if (sidechainName.isEmpty())
                return;

            auto badge = name.getBounds().removeFromRight (20).reduced (2, 4).toFloat();
            g.setColour (Palette::automation);
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (Palette::windowBackground);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText ("SC", badge, juce::Justification::centred, false);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            power.setBounds (r.removeFromLeft (r.getHeight()));
            r.removeFromLeft (2);

            if (! compact)
            {
                remove.setBounds (r.removeFromRight (r.getHeight()));
                latencyArea = r.removeFromRight (40);
            }

            name.setBounds (r);
        }

        /** Bypass state and name, after they change. */
        void syncState()
        {
            power.setToggleState (plugin->isEnabled(), juce::dontSendNotification);
            name.setButtonText (plugin->getName() + (plugin->isMissing() ? " (missing)" : ""));
            name.setColour (juce::TextButton::textColourOffId, plugin->isEnabled() ? Palette::text : Palette::textDisabled);
        }

        void refresh()
        {
            auto newLatency = plugin->getLatencySeconds() * 1000.0;
            if (std::abs (newLatency - latencyMs) > 0.01)
            {
                latencyMs = newLatency;
                repaint();
            }

            auto* source = MixerOps::getSidechainSource (*plugin);
            auto newSidechain = source != nullptr ? source->getName() : juce::String();

            if (newSidechain != sidechainName)
            {
                sidechainName = newSidechain;
                name.setTooltip (plugin->getName() + (sidechainName.isNotEmpty() ? "\nSidechain from: " + sidechainName : juce::String())
                                 + "\nClick to open, right-click for options");
                repaint();
            }
        }

        te::Plugin::Ptr plugin;
        bool compact;
        juce::TextButton power, name, remove;
        juce::Rectangle<int> latencyArea;
        double latencyMs = 0.0;
        juce::String sidechainName;
    };

    InsertSlots::InsertSlots (te::Track::Ptr t, bool c) : InsertSlots (t->pluginList, c)
    {
        track = t;
    }

    InsertSlots::InsertSlots (te::PluginList& l, bool c) : track (l.getOwnerTrack()), list (l), compact (c)
    {
        addButton.setTooltip ("Add an insert effect (or instrument)");
        addButton.onClick = [this] { PluginMenus::showInsertMenu (list, -1, &addButton); };
        addAndMakeVisible (addButton);

        list.state.addListener (this);
        rebuild();
        startTimerHz (2);
    }

    InsertSlots::~InsertSlots()
    {
        list.state.removeListener (this);
    }

    void InsertSlots::rebuild()
    {
        rows.clear();

        for (auto p : list.getPlugins())
            if (MixerOps::isInsertPlugin (*p))
                addAndMakeVisible (rows.add (new Row (p, compact)));

        resized();

        if (onIdealHeightChanged)
            onIdealHeightChanged();
        else if (auto* parent = getParentComponent())
            parent->resized();
    }

    void InsertSlots::handleAsyncUpdate()
    {
        if (compareAndReset (needsRebuild))
        {
            needsRowUpdate = false;
            rebuild();
        }
        else if (compareAndReset (needsRowUpdate))
        {
            for (auto r : rows)
                r->syncState();
        }
    }

    void InsertSlots::timerCallback()
    {
        if (! isShowing())
            return;

        for (auto r : rows)
            r->refresh();
    }

    int InsertSlots::getIdealHeight() const
    {
        return (rows.size() + 1) * rowPitch;
    }

    void InsertSlots::resized()
    {
        auto r = getLocalBounds();

        for (auto row : rows)
        {
            row->setBounds (r.removeFromTop (rowHeight));
            r.removeFromTop (2);
        }

        addButton.setBounds (r.removeFromTop (rowHeight));
    }

    void InsertSlots::paint (juce::Graphics&) {}

    //==============================================================================
    struct SendSlots::Row : public juce::Component
    {
        Row (te::AuxSendPlugin* s, bool c) : send (s), compact (c)
        {
            auto busName = send->edit.getAuxBusName (send->busNumber.get());
            name.setText (busName.isNotEmpty() ? busName : "Bus " + juce::String (send->busNumber.get() + 1), juce::dontSendNotification);
            name.setFont (juce::FontOptions (12.0f));
            name.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (name);

            level.setSliderStyle (juce::Slider::LinearBar);
            level.setRange (-60.0, 6.0, 0.1);
            level.setSkewFactorFromMidPoint (-12.0);
            level.setValue (send->getGainDb(), juce::dontSendNotification);
            level.setTextValueSuffix (" dB");
            level.setDoubleClickReturnValue (true, 0.0);
            level.setColour (juce::Slider::trackColourId, Palette::accentDim);
            level.onDragStart = [this] { send->edit.getUndoManager().beginNewTransaction ("Send level"); };
            level.onValueChange = [this] { send->setGainDb ((float) level.getValue()); };
            addAndMakeVisible (level);

            prePost.setButtonText (MixerOps::isPostFader (*send) ? "Post" : "Pre");
            prePost.setTooltip ("Pre-fader or post-fader send");
            prePost.onClick = [this]
            {
                send->edit.getUndoManager().beginNewTransaction ("Send pre/post");
                MixerOps::setPostFader (*send, ! MixerOps::isPostFader (*send));
            };
            addAndMakeVisible (prePost);

            remove.setButtonText ("x");
            remove.onClick = [this]
            {
                send->edit.getUndoManager().beginNewTransaction ("Remove send");
                send->deleteFromParent();
            };
            addAndMakeVisible (remove);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            remove.setBounds (r.removeFromRight (r.getHeight()));
            prePost.setBounds (r.removeFromRight (compact ? 30 : 38));
            r.removeFromRight (2);

            if (compact)
            {
                level.setBounds (r);
                name.setVisible (false);
                level.setTooltip (name.getText());
            }
            else
            {
                name.setBounds (r.removeFromLeft (r.getWidth() / 2));
                level.setBounds (r);
            }
        }

        te::AuxSendPlugin* send;                 // declared before keepAlive: members initialise in this order
        te::Plugin::Ptr keepAlive { send };
        bool compact;
        juce::Label name;
        juce::Slider level;
        juce::TextButton prePost, remove;
    };

    SendSlots::SendSlots (te::Track::Ptr t, bool c) : track (t), compact (c)
    {
        addButton.setTooltip ("Add a send to an FX track");
        addButton.onClick = [this]
        {
            juce::PopupMenu m;
            auto& edit = track->edit;
            te::Track::Ptr tr = track;

            for (auto& bus : MixerOps::getFxBuses (edit))
            {
                if (bus.returnTrack == track.get())
                    continue;

                auto num = bus.number;
                m.addItem (bus.name, [tr, num]
                {
                    tr->edit.getUndoManager().beginNewTransaction ("Add send");
                    MixerOps::addSend (*tr, num, true);
                });
            }

            m.addSeparator();
            m.addItem ("New FX track + send", [tr]
            {
                auto& e = tr->edit;
                e.getUndoManager().beginNewTransaction ("Add FX track and send");
                if (auto fx = TrackOps::addFxTrack (e, nullptr))
                    for (auto& bus : MixerOps::getFxBuses (e))
                        if (bus.returnTrack == fx.get())
                            MixerOps::addSend (*tr, bus.number, true);
            });

            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton));
        };
        addAndMakeVisible (addButton);

        track->state.addListener (this);
        rebuild();
    }

    SendSlots::~SendSlots()
    {
        track->state.removeListener (this);
    }

    void SendSlots::rebuild()
    {
        rows.clear();

        for (auto s : MixerOps::getSends (*track))
            addAndMakeVisible (rows.add (new Row (s, compact)));

        resized();

        if (onIdealHeightChanged)
            onIdealHeightChanged();
        else if (auto* parent = getParentComponent())
            parent->resized();
    }

    int SendSlots::getIdealHeight() const
    {
        return (rows.size() + 1) * InsertSlots::rowPitch;
    }

    void SendSlots::resized()
    {
        auto r = getLocalBounds();

        for (auto row : rows)
        {
            row->setBounds (r.removeFromTop (InsertSlots::rowHeight));
            r.removeFromTop (2);
        }

        addButton.setBounds (r.removeFromTop (InsertSlots::rowHeight));
    }
}
