#pragma once

#include "../PCH.h"
#include <map>

namespace cdaw::Cubase
{
    /** Low-level reader for Cubase / Nuendo project files (.cpr), reverse engineered from Cubase 10.5
        projects. Everything is big-endian.

        File:   "RIFF" u32 "NUND", then pairs of
                "ROOT" u32 { string name, string className }   e.g. "Arrangement1" / "PArrangement"
                "ARCH" u32 { object archive }

        Archive: a stream of objects. An object slot starts with a u32 tag:
                FFFFFFFE string name u16 version          base class of the object that follows (repeatable)
                FFFFFFFF string name u16 version u32 len  a new class and its first object (len bytes of data)
                8000xxxx u32 len                          another object of the class whose FFFFFFFF tag sits
                                                          at offset xxxx of the archive
                00000000                                  null
                anything else                             reference to an earlier object: the offset of its
                                                          length field

        Strings: u32 length, bytes. UTF-8 strings end with NUL + a UTF-8 BOM (EF BB BF) inside the length;
        others are plain 8-bit with a trailing NUL.

        An object's data mixes plain fields with nested objects. The Archive finds nested objects by
        scanning (a class reference must point at a known class definition, a new class needs a valid
        name, version and length that fit its parent), which works without knowing every class layout. */

    //==============================================================================
    /** Bounds-checked big-endian reader. Reading past the end sets `ok` to false and returns zeros. */
    struct Reader
    {
        Reader (const juce::uint8* d, size_t start, size_t endPos) : data (d), pos (start), end (endPos) {}

        const juce::uint8* data;
        size_t pos, end;
        bool ok = true;

        bool has (size_t n) const       { return ok && pos + n <= end && pos + n >= pos; }
        size_t remaining() const        { return pos < end ? end - pos : 0; }
        void skip (size_t n)            { if (has (n)) pos += n; else fail(); }
        void fail()                     { ok = false; pos = end; }

        juce::uint8 u8();
        juce::uint16 u16();
        juce::uint32 u32();
        juce::int64 i64();
        float f32();
        double f64();
        float f32le();

        /** u32 length + bytes; see the class comment for the encoding. */
        juce::String string();
        juce::MemoryBlock bytes (size_t n);
    };

    //==============================================================================
    struct Node
    {
        juce::String cls;
        int version = 0;
        size_t start = 0;       // first byte of the slot (a base-class marker, a class tag or a reference tag)
        size_t lenPos = 0;      // where its length field sits: what references to this object point at
        size_t data = 0, end = 0;
        Node* parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;

        const Node* child (const juce::String& className) const;
        std::vector<const Node*> childrenOf (const juce::String& className) const;
        /** First descendant (depth-first) of that class. */
        const Node* find (const juce::String& className) const;
        /** Byte position where the plain fields end: the first child object (or the end). */
        size_t fieldsEnd() const        { return children.empty() ? end : children.front()->start; }
    };

    //==============================================================================
    class Archive
    {
    public:
        juce::Result load (const juce::File&);
        juce::Result loadFromMemory (juce::MemoryBlock);

        /** The top-level object list of a ROOT chunk ("Arrangement1", "Devices", ...); null if absent. */
        const Node* root (const juce::String& name) const;
        juce::StringArray rootNames() const;

        const juce::uint8* bytes() const            { return static_cast<const juce::uint8*> (file.getData()); }
        size_t size() const                         { return file.getSize(); }
        Reader readerFor (const Node& n) const      { return { bytes(), n.data, n.end }; }

        /** Reads an object slot inside `parent`'s data at r.pos: the nested child that starts there,
            the object a reference points at, or nullptr (null, or something unreadable). */
        const Node* readSlot (Reader& r, const Node& parent) const;

        /** Indented outline of the object tree, for reverse engineering. */
        juce::String dump (const Node&, int maxDepth = 6) const;

    private:
        struct Chunk { juce::String name, className; std::unique_ptr<Node> root; size_t origin = 0; };

        juce::MemoryBlock file;
        std::vector<Chunk> chunks;
        std::map<size_t, const Node*> objectsByLenPos;      // absolute position -> object

        struct Parser;
    };

    //==============================================================================
    /** The typed attribute trees used for mixer channels, devices and settings:
            entries = u32 count, then { string key, u16 type, value }
            type 1 int64 · 4 double · 8 string · 2 container, followed by a u16 kind:
                 6 struct (entries) · 5 array of structs · 7 blob · 4 string array · 2 int64 array
                 3 double array · 20 object (class name, u64 id, entries) · 21 reference (u64 id)
                 201 array of typed containers */
    struct Attr
    {
        enum class Type { none, integer, real, string, structure, array, blob, strings, integers, reals, object, reference };

        Type type = Type::none;
        juce::int64 integer = 0;
        double real = 0.0;
        juce::String text;                                      // string, or an object's class name
        juce::MemoryBlock blob;
        std::vector<std::pair<juce::String, Attr>> fields;      // structure / object
        std::vector<Attr> items;                                // array / strings / integers / reals

        /** Field lookup; a path "Volume/Value" walks nested structures. */
        const Attr* get (const juce::String& path) const;
        juce::int64 getInt (const juce::String& path, juce::int64 fallback = 0) const;
        double getReal (const juce::String& path, double fallback = 0.0) const;
        juce::String getString (const juce::String& path) const;
        const juce::MemoryBlock* getBlob (const juce::String& path) const;
        const std::vector<Attr>* getArray (const juce::String& path) const;

        bool isStructure() const    { return type == Type::structure || type == Type::object; }
    };

    /** Reads `entries` at r.pos into a structure. With `tolerant`, a value it can't read ends the
        list (keeping what came before) instead of failing; r.ok tells whether everything was read. */
    Attr readAttributes (Reader& r, bool tolerant);

    /** A mixer channel object (MTrack / MAudioTrack / MInstrumentTrack data):
        u16, string "VST Multitrack", u32 kind, u32, then the attribute entries. */
    struct ChannelData
    {
        juce::String typeName;
        int kind = 0;           // 1 audio, 4 group, 5 FX, 8 instrument, 0 none
        Attr attrs;
        bool complete = false;
    };

    std::optional<ChannelData> readChannel (const Archive&, const Node&);
}
