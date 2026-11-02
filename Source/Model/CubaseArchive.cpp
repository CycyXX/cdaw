#include "CubaseArchive.h"
#include <unordered_map>

namespace cdaw::Cubase
{
    //==============================================================================
    juce::uint8 Reader::u8()
    {
        if (! has (1)) { fail(); return 0; }
        return data[pos++];
    }

    juce::uint16 Reader::u16()
    {
        if (! has (2)) { fail(); return 0; }
        auto v = (juce::uint16) ((data[pos] << 8) | data[pos + 1]);
        pos += 2;
        return v;
    }

    juce::uint32 Reader::u32()
    {
        if (! has (4)) { fail(); return 0; }
        auto v = juce::ByteOrder::bigEndianInt (data + pos);
        pos += 4;
        return v;
    }

    juce::int64 Reader::i64()
    {
        if (! has (8)) { fail(); return 0; }
        auto v = (juce::int64) juce::ByteOrder::bigEndianInt64 (data + pos);
        pos += 8;
        return v;
    }

    float Reader::f32()
    {
        auto bits = u32();
        float f;
        std::memcpy (&f, &bits, sizeof (f));
        return f;
    }

    double Reader::f64()
    {
        auto bits = (juce::uint64) i64();
        double d;
        std::memcpy (&d, &bits, sizeof (d));
        return d;
    }

    float Reader::f32le()
    {
        if (! has (4)) { fail(); return 0.0f; }
        auto bits = juce::ByteOrder::littleEndianInt (data + pos);
        pos += 4;
        float f;
        std::memcpy (&f, &bits, sizeof (f));
        return f;
    }

    juce::String Reader::string()
    {
        auto n = (size_t) u32();
        if (! has (n)) { fail(); return {}; }

        auto* p = data + pos;
        pos += n;

        if (n >= 4 && p[n - 4] == 0 && p[n - 3] == 0xef && p[n - 2] == 0xbb && p[n - 1] == 0xbf)
            return juce::String::fromUTF8 (reinterpret_cast<const char*> (p), (int) n - 4);

        while (n > 0 && p[n - 1] == 0)
            --n;

        // 8-bit text (ASCII in practice; Latin-1 otherwise).
        juce::String s;
        s.preallocateBytes (n + 1);
        for (size_t i = 0; i < n; ++i)
            s += (juce::juce_wchar) p[i];
        return s;
    }

    juce::MemoryBlock Reader::bytes (size_t n)
    {
        if (! has (n)) { fail(); return {}; }
        juce::MemoryBlock m (data + pos, n);
        pos += n;
        return m;
    }

    //==============================================================================
    const Node* Node::child (const juce::String& className) const
    {
        for (auto& c : children)
            if (c->cls == className)
                return c.get();
        return nullptr;
    }

    std::vector<const Node*> Node::childrenOf (const juce::String& className) const
    {
        std::vector<const Node*> result;
        for (auto& c : children)
            if (c->cls == className)
                result.push_back (c.get());
        return result;
    }

    const Node* Node::find (const juce::String& className) const
    {
        for (auto& c : children)
        {
            if (c->cls == className)
                return c.get();
            if (auto* d = c->find (className))
                return d;
        }
        return nullptr;
    }

    //==============================================================================
    /** Objects whose data is parsed some other way: scanning inside them would only find look-alikes
        (attribute trees, MIDI event streams). */
    static bool isOpaque (const juce::String& cls)
    {
        return cls == "MTrack" || cls == "MAudioTrack" || cls == "MInstrumentTrack" || cls == "MMidiTrack"
            || cls == "FAttributes" || cls == "PAttributes" || cls == "StMedia::PAttributes"
            || cls == "PChordTrackParam" || cls == "UWindowLayout";
    }

    struct Archive::Parser
    {
        Parser (Archive& a, size_t chunkStart) : archive (a), b (a.bytes()), origin (chunkStart) {}

        Archive& archive;
        const juce::uint8* b;
        size_t origin;
        std::unordered_map<size_t, std::pair<juce::String, int>> classes;   // tag offset (from origin) -> class
        int depth = 0;

        juce::uint32 u32At (size_t p) const     { return juce::ByteOrder::bigEndianInt (b + p); }

        /** A class name: u32 length (2..82), [A-Za-z_][A-Za-z0-9_:]*, NUL. */
        bool readName (size_t p, size_t end, juce::String& name, size_t& after) const
        {
            if (p + 4 > end)
                return false;

            auto n = (size_t) u32At (p);
            if (n < 2 || n > 82 || p + 4 + n > end || b[p + 4 + n - 1] != 0)
                return false;

            auto* s = b + p + 4;
            for (size_t i = 0; i + 1 < n; ++i)
            {
                auto c = s[i];
                const bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
                const bool ok = alpha || (i > 0 && ((c >= '0' && c <= '9') || c == ':'));
                if (! ok)
                    return false;
            }

            name = juce::String (reinterpret_cast<const char*> (s), n - 1);
            after = p + 4 + n;
            return true;
        }

        std::unique_ptr<Node> object (size_t o, size_t end, Node* parent)
        {
            size_t p = o;
            int bases = 0;

            while (p + 4 <= end && u32At (p) == 0xfffffffe)
            {
                juce::String name;
                size_t after;
                if (! readName (p + 4, end, name, after) || after + 2 > end)
                    return {};
                p = after + 2;
                ++bases;
            }

            if (p + 4 > end)
                return {};

            auto node = std::make_unique<Node>();
            node->start = o;
            node->parent = parent;
            const auto tag = u32At (p);

            if (tag == 0xffffffff)
            {
                size_t after;
                if (! readName (p + 4, end, node->cls, after) || after + 6 > end)
                    return {};

                node->version = (b[after] << 8) | b[after + 1];
                node->lenPos = after + 2;
                node->data = after + 6;
                node->end = node->data + u32At (node->lenPos);
                if (node->end > end || node->end < node->data)
                    return {};

                classes[p - origin] = { node->cls, node->version };
            }
            else if ((tag & 0x80000000u) != 0 && bases == 0)
            {
                auto it = classes.find ((size_t) (tag & 0x7fffffffu));
                if (it == classes.end() || p + 8 > end)
                    return {};

                node->cls = it->second.first;
                node->version = it->second.second;
                node->lenPos = p + 4;
                node->data = p + 8;
                node->end = node->data + u32At (node->lenPos);
                if (node->end > end || node->end < node->data)
                    return {};
            }
            else
            {
                return {};
            }

            archive.objectsByLenPos[node->lenPos] = node.get();

            if (node->cls == "MMidiPart")
                midiPartClasses (*node);
            else if (! isOpaque (node->cls) && depth < 200)
            {
                ++depth;
                walk (*node, node->data, node->end);
                --depth;
            }

            return node;
        }

        /** A MIDI part declares its event classes inline (once per file); register them, skip the events. */
        void midiPartClasses (Node& part)
        {
            Reader r (b, part.data, part.end);
            r.string();
            r.skip (12);
            r.u32();                    // event count
            auto numClasses = r.u8();

            for (int i = 0; i < numClasses && r.ok; ++i)
            {
                auto c = object (r.pos, part.end, &part);
                if (c == nullptr)
                    break;
                r.pos = c->end;
                part.children.push_back (std::move (c));
            }
        }

        bool couldStartObject (size_t i, size_t end) const
        {
            if (i + 4 > end)
                return false;

            if (b[i] == 0xff && b[i + 1] == 0xff && b[i + 2] == 0xff && (b[i + 3] == 0xfe || b[i + 3] == 0xff))
                return true;

            return (b[i] & 0x80) != 0 && classes.count ((size_t) (u32At (i) & 0x7fffffffu)) > 0;
        }

        void walk (Node& parent, size_t from, size_t end)
        {
            for (size_t i = from; i + 4 <= end;)
            {
                if (couldStartObject (i, end))
                {
                    if (auto c = object (i, end, &parent))
                    {
                        i = c->end;
                        parent.children.push_back (std::move (c));
                        continue;
                    }
                }
                ++i;
            }
        }
    };

    //==============================================================================
    juce::Result Archive::load (const juce::File& f)
    {
        juce::MemoryBlock data;
        if (! f.loadFileAsData (data))
            return juce::Result::fail ("Can't read " + f.getFullPathName());

        return loadFromMemory (std::move (data));
    }

    juce::Result Archive::loadFromMemory (juce::MemoryBlock data)
    {
        file = std::move (data);
        chunks.clear();
        objectsByLenPos.clear();

        auto* b = bytes();
        const auto n = size();

        if (n < 12 || std::memcmp (b, "RIFF", 4) != 0 || std::memcmp (b + 8, "NUND", 4) != 0)
            return juce::Result::fail ("Not a Cubase / Nuendo project (no RIFF/NUND header)");

        Reader r (b, 12, n);
        juce::String rootName, rootClass;

        while (r.remaining() >= 8)
        {
            auto tag = juce::String (reinterpret_cast<const char*> (b + r.pos), 4);
            r.skip (4);
            auto len = (size_t) r.u32();
            const auto start = r.pos;

            if (! r.has (len))
                return juce::Result::fail ("The project file is truncated (chunk " + tag + ")");

            if (tag == "ROOT")
            {
                Reader s (b, start, start + len);
                rootName = s.string();
                rootClass = s.string();
            }
            else if (tag == "ARCH")
            {
                Chunk c;
                c.name = rootName;
                c.className = rootClass;
                c.origin = start;
                c.root = std::make_unique<Node>();
                c.root->cls = "<" + rootName + ">";
                c.root->start = c.root->data = start;
                c.root->end = start + len;

                Parser parser (*this, start);
                parser.walk (*c.root, start, start + len);
                chunks.push_back (std::move (c));
            }

            r.pos = start + len;
        }

        if (root ("Arrangement1") == nullptr)
            return juce::Result::fail ("The project has no arrangement");

        return juce::Result::ok();
    }

    const Node* Archive::root (const juce::String& name) const
    {
        for (auto& c : chunks)
            if (c.name == name)
                return c.root.get();
        return nullptr;
    }

    juce::StringArray Archive::rootNames() const
    {
        juce::StringArray names;
        for (auto& c : chunks)
            names.add (c.name);
        return names;
    }

    const Node* Archive::readSlot (Reader& r, const Node& parent) const
    {
        const auto at = r.pos;
        const auto tag = r.u32();
        if (! r.ok || tag == 0)
            return nullptr;

        if (tag >= 0xfffffffe || (tag & 0x80000000u) != 0)
        {
            for (auto& c : parent.children)
            {
                if (c->start == at)
                {
                    r.pos = c->end;
                    return c.get();
                }
            }

            r.fail();
            return nullptr;
        }

        // A reference: offset of the object's length field within its chunk.
        for (auto& c : chunks)
        {
            if (at >= c.root->data && at < c.root->end)
            {
                auto it = objectsByLenPos.find (c.origin + tag);
                return it != objectsByLenPos.end() ? it->second : nullptr;
            }
        }

        return nullptr;
    }

    juce::String Archive::dump (const Node& n, int maxDepth) const
    {
        juce::String s;

        std::function<void (const Node&, int)> visit = [&] (const Node& node, int depth)
        {
            for (auto& c : node.children)
            {
                s << juce::String::repeatedString ("  ", depth) << c->cls << " v" << c->version
                  << " @" << (juce::int64) c->start << " len=" << (juce::int64) (c->end - c->data) << "\n";

                if (depth + 1 < maxDepth)
                    visit (*c, depth + 1);
            }
        };

        visit (n, 0);
        return s;
    }

    //==============================================================================
    const Attr* Attr::get (const juce::String& path) const
    {
        const Attr* a = this;

        for (auto& key : juce::StringArray::fromTokens (path, "/", ""))
        {
            const Attr* next = nullptr;
            for (auto& [k, v] : a->fields)
            {
                if (k == key)
                {
                    next = &v;
                    break;
                }
            }

            if (next == nullptr)
                return nullptr;
            a = next;
        }

        return a;
    }

    juce::int64 Attr::getInt (const juce::String& path, juce::int64 fallback) const
    {
        auto* a = get (path);
        return a != nullptr && a->type == Type::integer ? a->integer : fallback;
    }

    double Attr::getReal (const juce::String& path, double fallback) const
    {
        auto* a = get (path);
        if (a == nullptr)                   return fallback;
        if (a->type == Type::real)          return a->real;
        if (a->type == Type::integer)       return (double) a->integer;
        return fallback;
    }

    juce::String Attr::getString (const juce::String& path) const
    {
        auto* a = get (path);
        return a != nullptr && a->type == Type::string ? a->text : juce::String();
    }

    const juce::MemoryBlock* Attr::getBlob (const juce::String& path) const
    {
        auto* a = get (path);
        return a != nullptr && a->type == Type::blob ? &a->blob : nullptr;
    }

    const std::vector<Attr>* Attr::getArray (const juce::String& path) const
    {
        auto* a = get (path);
        return a != nullptr && a->type == Type::array ? &a->items : nullptr;
    }

    //==============================================================================
    static bool readEntries (Reader& r, Attr& into, bool tolerant, int depth);

    static bool readValue (Reader& r, Attr& v, int type, int depth)
    {
        if (depth > 64)
            return false;

        switch (type)
        {
            case 1:  v.type = Attr::Type::integer;  v.integer = r.i64();   return r.ok;
            case 4:  v.type = Attr::Type::real;     v.real = r.f64();      return r.ok;
            case 8:  v.type = Attr::Type::string;   v.text = r.string();   return r.ok;
            case 2:  break;
            default: return false;
        }

        const auto kind = r.u16();

        switch (kind)
        {
            case 6:
                v.type = Attr::Type::structure;
                return readEntries (r, v, false, depth + 1);

            case 5:
            {
                v.type = Attr::Type::array;
                auto n = r.u32();
                if (n > r.remaining())
                    return false;
                for (juce::uint32 i = 0; i < n && r.ok; ++i)
                {
                    Attr item;
                    item.type = Attr::Type::structure;
                    if (! readEntries (r, item, false, depth + 1))
                        return false;
                    v.items.push_back (std::move (item));
                }
                return r.ok;
            }

            case 7:
                v.type = Attr::Type::blob;
                v.blob = r.bytes (r.u32());
                return r.ok;

            case 4:
            {
                v.type = Attr::Type::strings;
                auto n = r.u32();
                if (n > r.remaining())
                    return false;
                for (juce::uint32 i = 0; i < n && r.ok; ++i)
                {
                    Attr s;
                    s.type = Attr::Type::string;
                    s.text = r.string();
                    v.items.push_back (std::move (s));
                }
                return r.ok;
            }

            case 2:
            case 3:
            {
                v.type = kind == 2 ? Attr::Type::integers : Attr::Type::reals;
                auto n = r.u32();
                if ((size_t) n * 8 > r.remaining())
                    return false;
                for (juce::uint32 i = 0; i < n; ++i)
                {
                    Attr x;
                    if (kind == 2) { x.type = Attr::Type::integer; x.integer = r.i64(); }
                    else           { x.type = Attr::Type::real;    x.real = r.f64(); }
                    v.items.push_back (std::move (x));
                }
                return r.ok;
            }

            case 20:
                v.type = Attr::Type::object;
                v.text = r.string();
                r.skip (8);             // the object's identity (a pointer)
                return readEntries (r, v, false, depth + 1);

            case 21:
                v.type = Attr::Type::reference;
                v.integer = r.i64();
                return r.ok;

            case 201:
            {
                v.type = Attr::Type::array;
                auto n = r.u32();
                if (n > r.remaining())
                    return false;
                for (juce::uint32 i = 0; i < n && r.ok; ++i)
                {
                    Attr item;
                    if (! readValue (r, item, 2, depth + 1))
                        return false;
                    v.items.push_back (std::move (item));
                }
                return r.ok;
            }

            default:
                return false;
        }
    }

    static bool readEntries (Reader& r, Attr& into, bool tolerant, int depth)
    {
        auto n = r.u32();
        if (! r.ok || n > r.remaining())
            return false;

        for (juce::uint32 i = 0; i < n; ++i)
        {
            const auto entryStart = r.pos;
            auto key = r.string();
            auto type = r.u16();
            Attr value;

            if (! r.ok || ! readValue (r, value, type, depth))
            {
                if (tolerant)
                {
                    r.pos = entryStart;
                    r.ok = false;
                    return true;        // keep what was read so far
                }
                return false;
            }

            into.fields.emplace_back (key, std::move (value));
        }

        return r.ok;
    }

    Attr readAttributes (Reader& r, bool tolerant)
    {
        Attr a;
        a.type = Attr::Type::structure;

        if (! readEntries (r, a, tolerant, 0))
            r.ok = false;

        return a;
    }

    std::optional<ChannelData> readChannel (const Archive& archive, const Node& node)
    {
        auto r = archive.readerFor (node);
        ChannelData c;
        r.u16();
        c.typeName = r.string();
        c.kind = (int) r.u32();
        r.u32();

        if (! r.ok)
            return {};

        c.attrs = readAttributes (r, true);
        c.complete = r.ok;
        return c;
    }
}
