// tsstat: prints the main statistics of a .tsf file the way the original shows them.
// Used by the differential test bench (re/scripts/diffstand.py).
//
//   tsstat [--split MS] [--only-text] [--by-pauses] [--sel START LEN] [--text | --runs] file.tsf
//
// --text  the text as the RichEdit shows it (paragraphs separated by '\n');
// --runs  styled runs of that text: "start<TAB>length<TAB>style" (TextStyle bits).

#include "core/MainStats.h"
#include "core/TsfFile.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    RecalcOptions opt;
    opt.onlyText = false; // options come from the flags only
    int selStart = 0, selLen = 0;
    bool printText = false, printRuns = false;
    QString file;
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
    const MainStats s = Stats::compute(m, b, e, opt.splitMs, opt.byPauses);
    const QStringList names = Stats::rowNames();
    const QStringList values = Stats::format(s, QLocale(QLocale::Russian));
    for (int i = 0; i < names.size(); ++i)
        out << names[i] << "\t" << values[i] << "\n";
    return 0;
}
