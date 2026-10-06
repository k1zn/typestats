// tsstat: prints the main statistics of a .tsf file the way the original shows them.
// Used by the differential test bench (re/scripts/diffstand.py).
//
//   tsstat [--split MS] [--only-text] [--by-pauses] [--sel START LEN] [--text | --runs] file.tsf
//   tsstat ... --extra KIND [--avg] [--sort MODE] [--desc] [--pattern P] [--only S] [--any S] [--exclude S] file.tsf
//
// --text  the text as the RichEdit shows it (paragraphs separated by '\n');
// --runs  styled runs of that text: "start<TAB>length<TAB>style" (TextStyle bits);
// --extra the list of the extra statistics window (Form3) for kind 0..6: "speed<TAB>text[<TAB>count]".
//
//   tsstat --to-journal out.tsj file.tsf    the records of the file as a journal (re/journal.md)
//   tsstat --verify file.tsf                the time stamps of the recording (re/stamps.md); exit code 0 - all
//                                           the records confirmed, 1 - not all, 2 - stamps that do not check

#include "core/ExtraStats.h"
#include "core/FingerZones.h"
#include "core/Journal.h"
#include "core/MainStats.h"
#include "core/Stamps.h"
#include "core/TsfFile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimeZone>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    RecalcOptions opt;
    opt.onlyText = false; // options come from the flags only
    int selStart = 0, selLen = 0;
    bool printText = false, printRuns = false;
    int extraKind = -1, sortMode = 0;
    bool averages = false, descending = false;
    QString pattern;
    ExtraStats::CharFilter filter;
    QString file, journalOut;
    bool verify = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args[i];
        if (a == QLatin1String("--split") && i + 1 < args.size())
            opt.splitMs = args[++i].toInt();
        else if (a == QLatin1String("--only-text"))
            opt.onlyText = true;
        else if (a == QLatin1String("--by-pauses"))
            opt.byPauses = true;
        else if (a == QLatin1String("--sel") && i + 2 < args.size()) {
            selStart = args[++i].toInt();
            selLen = args[++i].toInt();
        } else if (a == QLatin1String("--text"))
            printText = true;
        else if (a == QLatin1String("--runs"))
            printRuns = true;
        else if (a == QLatin1String("--extra") && i + 1 < args.size())
            extraKind = args[++i].toInt();
        else if (a == QLatin1String("--verify"))
            verify = true;
        else if (a == QLatin1String("--to-journal") && i + 1 < args.size())
            journalOut = args[++i];
        else if (a == QLatin1String("--avg"))
            averages = true;
        else if (a == QLatin1String("--sort") && i + 1 < args.size())
            sortMode = args[++i].toInt();
        else if (a == QLatin1String("--desc"))
            descending = true;
        else if (a == QLatin1String("--pattern") && i + 1 < args.size())
            pattern = args[++i];
        else if (a == QLatin1String("--only") && i + 1 < args.size()) {
            filter.onlyOn = true;
            filter.only = args[++i];
        } else if (a == QLatin1String("--any") && i + 1 < args.size()) {
            filter.anyOn = true;
            filter.any = args[++i];
        } else if (a == QLatin1String("--exclude") && i + 1 < args.size()) {
            filter.excludeOn = true;
            filter.exclude = args[++i];
        }
        else
            file = a;
    }
    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);
    TsfDocument doc;
    if (file.isEmpty() || Tsf::read(file, doc) == Tsf::ReadError::CannotOpen) {
        QTextStream(stderr) << "usage: tsstat [--split MS] [--only-text] [--by-pauses] [--sel START LEN] [--text] file.tsf\n";
        return 1;
    }
    if (!journalOut.isEmpty()) {
        QFile f(journalOut);
        if (!f.open(QIODevice::WriteOnly))
            return 1;
        for (const KeyRecord &r : doc.records)
            f.write(Journal::encode(r));
        return 0;
    }
    if (verify) {
        const Stamps::Report r = Stamps::verify(Recalc::normalized(doc.records), doc.stamps, doc.stampCertificates);
        static const char *const status[] = {"none", "confirmed", "partial", "broken"};
        const auto utc = [](qint64 ms) {
            return ms ? QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::utc()).toString(Qt::ISODate) : QString();
        };
        const std::pair<const char *, QString> rows[] = {
            {"status", QLatin1String(status[int(r.status)])},
            {"stamps", QString::number(r.stamps)},
            {"bad", QString::number(r.bad)},
            {"voided", QString::number(r.voided)},
            {"records", QString::number(r.records)},
            {"stamped", QString::number(r.stamped)},
            {"confirmed", QString::number(r.confirmed)},
            {"drift_ms", QString::number(r.driftMs)},
            {"first", utc(r.firstMs)},
            {"last", utc(r.lastMs)},
            {"injected", QString::number(r.injected)},
            {"authorities", r.authorities.join(QStringLiteral(", "))},
        };
        for (const auto &[name, value] : rows)
            out << name << "\t" << value << "\n";
        return r.status == Stamps::Report::Status::Confirmed ? 0 : r.status == Stamps::Report::Status::Broken ? 2 : 1;
    }
    opt.keyNames = doc.platform;
    const TextModel m = Recalc::run(doc.records, opt);
    if (printText) {
        out << m.text << "\n";
        return 0;
    }
    if (printRuns) {
        for (const TextRun &r : m.runs)
            out << r.start << "\t" << r.length << "\t" << r.style << "\n";
        return 0;
    }
    const auto [b, e] = Stats::range(m, selStart, selLen, opt.byPauses);
    if (extraKind >= 0) {
        const QLocale loc(QLocale::Russian);
        const auto occ = ExtraStats::collect(m, fingerSeries(m, FingerZones::standard()), b, e,
                                             ExtraStats::Kind(extraKind), pattern, filter);
        for (const ExtraStats::Row &r : ExtraStats::rows(occ, averages, sortMode, descending)) {
            out << ExtraStats::formatSpeed(r.speed, loc) << "\t" << r.text;
            if (averages)
                out << "\t" << r.value;
            out << "\n";
        }
        return 0;
    }
    const MainStats s = Stats::compute(m, b, e, opt.splitMs, opt.byPauses);
    const QStringList names = Stats::rowNames();
    const QStringList values = Stats::format(s, QLocale(QLocale::Russian));
    for (int i = 0; i < names.size(); ++i)
        out << names[i] << "\t" << values[i] << "\n";
    return 0;
}
