#include "DesktopParsers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

#include <cstring>

namespace Desktop {

XkbNames namesFromIds(const QStringList &ids, const QString &options)
{
    QStringList layouts, variants;
    for (const QString &id : ids) {
        const qsizetype plus = id.indexOf(u'+');
        layouts << id.left(plus);
        variants << (plus < 0 ? QString() : id.mid(plus + 1));
    }
    XkbNames n;
    n.layout = layouts.join(u',');
    // An empty list of variants rather than ",,": xkbcommon takes both, the first is easier to read.
    if (std::any_of(variants.begin(), variants.end(), [](const QString &v) { return !v.isEmpty(); }))
        n.variant = variants.join(u',');
    n.options = options;
    return n;
}

QString withoutGroupSwitching(const QString &options)
{
    QStringList kept;
    for (const QString &o : options.split(u',', Qt::SkipEmptyParts))
        if (!o.trimmed().startsWith(QLatin1String("grp:")))
            kept << o.trimmed();
    return kept.join(u',');
}

// --- sway ---

namespace I3 {

constexpr char kMagic[] = "i3-ipc";
constexpr int kHeader = 6 + 4 + 4;

QByteArray message(quint32 type, const QByteArray &payload)
{
    // Lengths and types are in the byte order of the machine.
    QByteArray m(kMagic, 6);
    const quint32 header[2] = {quint32(payload.size()), type};
    m.append(reinterpret_cast<const char *>(header), sizeof(header));
    return m + payload;
}

std::optional<std::pair<quint32, QByteArray>> take(QByteArray &buffer)
{
    if (buffer.size() < kHeader)
        return std::nullopt;
    if (!buffer.startsWith(kMagic)) { // out of step: nothing sensible can follow
        buffer.clear();
        return std::nullopt;
    }
    quint32 header[2];
    std::memcpy(header, buffer.constData() + 6, sizeof(header));
    if (buffer.size() < kHeader + qsizetype(header[0]))
        return std::nullopt;
    std::pair<quint32, QByteArray> m{header[1], buffer.mid(kHeader, header[0])};
    buffer.remove(0, kHeader + header[0]);
    return m;
}

} // namespace I3

namespace {

std::optional<SwayKeyboard> swayKeyboard(const QJsonObject &input)
{
    if (input.value(QLatin1String("type")).toString() != QLatin1String("keyboard"))
        return std::nullopt;
    const QJsonArray names = input.value(QLatin1String("xkb_layout_names")).toArray();
    if (names.isEmpty())
        return std::nullopt;
    SwayKeyboard k;
    k.identifier = input.value(QLatin1String("identifier")).toString();
    for (const QJsonValue &n : names)
        k.layoutNames << n.toString();
    k.active = input.value(QLatin1String("xkb_active_layout_index")).toInt(-1);
    return k;
}

ActiveWindow window(const QJsonObject &node)
{
    return {quint64(node.value(QLatin1String("id")).toInteger()), node.value(QLatin1String("name")).toString()};
}

std::optional<ActiveWindow> focused(const QJsonObject &node)
{
    if (node.value(QLatin1String("focused")).toBool())
        return window(node);
    for (const char *key : {"nodes", "floating_nodes"})
        for (const QJsonValue &child : node.value(QLatin1String(key)).toArray())
            if (auto w = focused(child.toObject()))
                return w;
    return std::nullopt;
}

} // namespace

QList<SwayKeyboard> parseSwayInputs(const QByteArray &json)
{
    QList<SwayKeyboard> keyboards;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).array())
        if (auto k = swayKeyboard(v.toObject()))
            keyboards << *k;
    return keyboards;
}

std::optional<SwayKeyboard> parseSwayInputEvent(const QByteArray &json)
{
    const QJsonObject e = QJsonDocument::fromJson(json).object();
    const QString change = e.value(QLatin1String("change")).toString();
    if (change != QLatin1String("xkb_layout") && change != QLatin1String("xkb_keymap") && change != QLatin1String("added"))
        return std::nullopt;
    return swayKeyboard(e.value(QLatin1String("input")).toObject());
}

std::optional<ActiveWindow> parseSwayWindowEvent(const QByteArray &json)
{
    const QJsonObject e = QJsonDocument::fromJson(json).object();
    const QString change = e.value(QLatin1String("change")).toString();
    const QJsonObject container = e.value(QLatin1String("container")).toObject();
    if (change == QLatin1String("focus") || (change == QLatin1String("title") && container.value(QLatin1String("focused")).toBool()))
        return window(container);
    return std::nullopt;
}

std::optional<ActiveWindow> parseSwayTree(const QByteArray &json)
{
    return focused(QJsonDocument::fromJson(json).object());
}

// --- Hyprland ---

QList<HyprKeyboard> parseHyprDevices(const QByteArray &json)
{
    QList<HyprKeyboard> keyboards;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).object().value(QLatin1String("keyboards")).toArray()) {
        const QJsonObject o = v.toObject();
        HyprKeyboard k;
        k.name = o.value(QLatin1String("name")).toString();
        k.names.model = o.value(QLatin1String("model")).toString();
        k.names.layout = o.value(QLatin1String("layout")).toString();
        k.names.variant = o.value(QLatin1String("variant")).toString();
        k.names.options = o.value(QLatin1String("options")).toString();
        k.activeKeymap = o.value(QLatin1String("active_keymap")).toString();
        k.main = o.value(QLatin1String("main")).toBool();
        keyboards << k;
    }
    return keyboards;
}

std::optional<ActiveWindow> parseHyprActiveWindow(const QByteArray &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (!o.contains(QLatin1String("address")))
        return std::nullopt;
    bool ok = false;
    const quint64 id = o.value(QLatin1String("address")).toString().toULongLong(&ok, 16); // "0x55e0..."
    return ActiveWindow{ok ? id : 0, o.value(QLatin1String("title")).toString()};
}

std::optional<std::pair<QString, QString>> parseHyprEvent(const QByteArray &line)
{
    const qsizetype sep = line.indexOf(">>");
    if (sep <= 0)
        return std::nullopt;
    return std::pair{QString::fromUtf8(line.left(sep)), QString::fromUtf8(line.mid(sep + 2)).trimmed()};
}

std::pair<QString, QString> splitHyprPair(const QString &data)
{
    const qsizetype comma = data.indexOf(u',');
    if (comma < 0)
        return {data, {}};
    return {data.left(comma), data.mid(comma + 1)};
}

// --- GNOME ---

QList<std::pair<QString, QString>> parseGnomeSources(const QString &text)
{
    static const QRegularExpression pair(QStringLiteral(R"(\(\s*'([^']*)'\s*,\s*'([^']*)'\s*\))"));
    QList<std::pair<QString, QString>> sources;
    for (auto it = pair.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        sources.append({m.captured(1), m.captured(2)});
    }
    return sources;
}

QStringList parseGVariantStrings(const QString &text)
{
    static const QRegularExpression string(QStringLiteral(R"('([^']*)')"));
    QStringList strings;
    for (auto it = string.globalMatch(text); it.hasNext();)
        strings << it.next().captured(1);
    return strings;
}

std::optional<std::pair<QString, QString>> parseGsettingsMonitorLine(const QString &line)
{
    const qsizetype sep = line.indexOf(QLatin1String(": "));
    if (sep <= 0)
        return std::nullopt;
    return std::pair{line.left(sep).trimmed(), line.mid(sep + 2).trimmed()};
}

// --- KDE and /etc/default/keyboard ---

XkbNames parseKxkbrc(const QString &text)
{
    XkbNames n;
    bool inLayout = false, use = true;
    for (QString line : text.split(u'\n')) {
        line = line.trimmed();
        if (line.startsWith(u'[')) {
            inLayout = line == QLatin1String("[Layout]");
            continue;
        }
        const qsizetype eq = line.indexOf(u'=');
        if (!inLayout || eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed(), value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("LayoutList"))
            n.layout = value;
        else if (key == QLatin1String("VariantList"))
            n.variant = value;
        else if (key == QLatin1String("Options"))
            n.options = value;
        else if (key == QLatin1String("Model"))
            n.model = value;
        else if (key == QLatin1String("Use"))
            use = value != QLatin1String("false");
    }
    return use ? n : XkbNames{};
}

XkbNames parseDefaultKeyboard(const QString &text)
{
    XkbNames n;
    for (QString line : text.split(u'\n')) {
        line = line.trimmed();
        const qsizetype eq = line.indexOf(u'=');
        if (line.startsWith(u'#') || eq <= 0)
            continue;
        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2 && (value.front() == u'"' || value.front() == u'\'') && value.back() == value.front())
            value = value.mid(1, value.size() - 2);
        const QString key = line.left(eq).trimmed();
        if (key == QLatin1String("XKBMODEL"))
            n.model = value;
        else if (key == QLatin1String("XKBLAYOUT"))
            n.layout = value;
        else if (key == QLatin1String("XKBVARIANT"))
            n.variant = value;
        else if (key == QLatin1String("XKBOPTIONS"))
            n.options = value;
    }
    return n;
}

}
