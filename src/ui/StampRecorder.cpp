#include "StampRecorder.h"

#include "core/TimeStamp.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>


namespace {

// RFC 3161 over plain HTTP: the answer is signed, the request is a hash. Companies of different countries, so that
// one of them going away or out of reach leaves the others (re/stamps.md).
const char *const kServices[] = {
    "http://timestamp.digicert.com",
    "http://timestamp.sectigo.com",
    "http://timestamp.globalsign.com/tsa/r6advanced1",
    "http://time.certum.pl",
    "http://tsa.swisssign.net",
    "http://timestamp.acs.microsoft.com",
};
constexpr int kServiceCount = int(std::size(kServices));
constexpr int kTimeoutMs = 5000; // then the next authority
// A cosign asked for after its stamp came this much later at most: its time against the records' would be off by that.
constexpr int kCosignLateMs = 1000;
constexpr int kRetryMs = 5000;
constexpr int kAfterStopMs = 1000; // the frames still on their way when recording stops

} // namespace

StampRecorder::StampRecorder(QObject *parent) : QObject(parent)
{
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, [this] { stampIfDue(true); });
    m_videoDue.setSingleShot(true);
    connect(&m_videoDue, &QTimer::timeout, this, [this] { stampIfDue(true); });
    m_send = [this](const QByteArray &imprint, int service, Done done) {
        sendOverNetwork(imprint, service, std::move(done));
    };
}

QStringList StampRecorder::services()
{
    QStringList out;
    for (const char *s : kServices)
        out.append(QLatin1String(s));
    return out;
}

void StampRecorder::setEnabled(bool on)
{
    m_enabled = on;
    if (!on) {
        m_idle.stop();
        m_videoDue.stop();
        ++m_generation; // an answer still on its way is not wanted
        m_inFlight = false;
        flushDone(false);
    }
    m_sessionStamped = false;
}

void StampRecorder::attach(TsfDocument *doc, bool another)
{
    m_doc = doc;
    ++m_generation;
    m_inFlight = false;
    m_idle.stop();
    m_videoDue.stop();
    flushDone(false);
    m_chain.reset();
    m_raw = 0;
    m_packetHashes.clear();
    if (m_doc)
        for (; m_raw < m_doc->records.size(); ++m_raw)
            m_chain.push(m_doc->records[m_raw]);
    m_sessionStamped = false;
    m_pending = {};
    m_error.clear();
    m_cover = {};
    const int packets = m_clip ? int(m_clip->packets.size()) : 0;
    if (another) {
        m_awaited.clear(); // the same document edited keeps them: a cosign finds its stamp by the imprint
        m_sinceKept.invalidate(); // its records were not typed now
        m_ownRecords = m_chain.size();
        m_ownPackets = packets;
    } else { // the same document edited (records dropped) or put back
        m_ownRecords = std::min(m_ownRecords, m_chain.size());
        m_ownPackets = std::min(m_ownPackets, packets);
    }
    if (m_doc && !m_doc->stamps.isEmpty() && stampedEnd() == m_chain.size())
        m_cover = {m_raw, stampedMediaEnd(), m_doc->stamps.size()};
}

void StampRecorder::recordsAdded()
{
    if (!m_doc)
        return;
    if (m_raw > m_doc->records.size()) { // replaced without attach(): start over
        attach(m_doc);
        return;
    }
    bool kept = false;
    for (; m_raw < m_doc->records.size(); ++m_raw)
        kept |= m_chain.push(m_doc->records[m_raw]);
    if (!kept || !m_enabled)
        return;
    m_sinceKept.start();
    m_idle.start(Stamps::kIdleMs);
    stampIfDue(false);
}

void StampRecorder::packetsAdded()
{
    if (!m_enabled || !m_doc)
        return;
    if (!m_capturing) // the last frames the encoders give after recording stopped: stamped as soon as they are in
        m_videoDue.start(kAfterStopMs);
    else if (!m_videoDue.isActive())
        m_videoDue.start(Stamps::kIntervalMs);
}

bool StampRecorder::covered() const
{
    return m_doc && m_cover.records == m_doc->records.size() && m_cover.packets == (m_clip ? m_clip->packets.size() : 0)
           && m_cover.stamps == m_doc->stamps.size();
}

void StampRecorder::flush()
{
    recordsAdded();
    if (covered()) {
        emit flushed(true);
        return;
    }
    // No key since the document came: its records are not this recording's to stamp now.
    if (!m_enabled || !m_doc || !m_sinceKept.isValid()) {
        emit flushed(false);
        return;
    }
    m_flushing = true;
    m_flushSent = !m_inFlight;
    if (!m_inFlight)
        send();
}

void StampRecorder::flushDone(bool ok)
{
    if (!m_flushing)
        return;
    m_flushing = false;
    m_flushImprint.clear();
    emit flushed(ok);
}

void StampRecorder::captureChanged(bool on)
{
    m_capturing = on;
    recordsAdded(); // the releases of the held keys when it goes off
    if (!on)
        stampIfDue(true);
    m_sessionStamped = false;
}

int StampRecorder::stampedEnd() const
{
    return m_doc && !m_doc->stamps.isEmpty() ? m_doc->stamps.last().end : 0;
}

int StampRecorder::stampedMediaEnd() const
{
    // Stamps v1 (an older recording typed on) do not cover packets.
    return m_doc && !m_doc->stamps.isEmpty() && m_doc->stamps.last().version >= 2 ? m_doc->stamps.last().mediaEnd : 0;
}

void StampRecorder::stampIfDue(bool now)
{
    if (!m_enabled || !m_doc || m_inFlight || !m_sinceKept.isValid())
        return;
    const int n = m_chain.size(), last = stampedEnd();
    // The video after the last key is stamped when recording stops or goes idle.
    const bool morePackets = m_clip && m_clip->packets.size() > stampedMediaEnd();
    if (n <= last && !(now && morePackets))
        return;
    // A session starts with a stamp at its first key; then one per interval of typing, and one after it.
    if (now || !m_sessionStamped || last == 0
        || m_chain.timeUs(n - 1) - m_chain.timeUs(last - 1) >= qint64(Stamps::kIntervalMs) * 1000)
        send();
}

void StampRecorder::send()
{
    // The delay and so the imprint are taken when the request leaves: a try at another authority after a failed
    // one says how late it is.
    const int last = stampedEnd(), n = m_chain.size();
    QByteArray previous;
    if (!m_doc->stamps.isEmpty())
        if (const std::optional<TimeStamp::Info> info = TimeStamp::info(m_doc->stamps.last().token))
            previous = info->imprint;
    m_pending.end = n;
    m_pending.raw = m_raw;
    m_pending.delayMs = quint32(m_sinceKept.elapsed());
    // The packets the encoders gave so far (chain v3, re/stamps.md).
    const int mediaFrom = stampedMediaEnd(), mediaTo = m_clip ? int(m_clip->packets.size()) : 0;
    for (int i = int(m_packetHashes.size() / 32); i < mediaTo; ++i)
        m_packetHashes += Stamps::packetHash(m_clip->packets[i]);
    m_pending.mediaEnd = mediaTo;
    // The key of the records' salts (chain v3): a block may then show some of them, and the others stay hashes.
    m_pending.key.resize(Stamps::kKeySize);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(m_pending.key.data()), Stamps::kKeySize / 4);
    m_pending.imprint = m_chain.hashV3(previous, last, n, m_pending.delayMs, m_pending.key,
                                       QByteArrayView(m_packetHashes).sliced(qsizetype(mediaFrom) * 32,
                                                                             qsizetype(mediaTo - mediaFrom) * 32),
                                       mediaTo - mediaFrom);
    // Two authorities at once, the same imprint: the one that answers first gives the stamp, the other its cosign.
    QList<int> pair = m_pending.tried;
    const int a = nextService(pair, m_service);
    pair.append(a);
    const int b = m_second >= 0 && !pair.contains(m_second) ? m_second : nextService(pair, a);
    m_pending.round = ++m_round;
    m_pending.pair[0] = a;
    m_pending.pair[1] = b;
    m_pending.requests = b >= 0 ? 2 : 1;
    m_pending.failed = 0;
    m_pending.sent.start();
    m_inFlight = true;
    // An answer may come from within the call, and a flush send the next stamp: what goes out is of this one.
    const QByteArray imprint = m_pending.imprint;
    const quint64 round = m_pending.round;
    request(imprint, a, round);
    if (b >= 0)
        request(imprint, b, round);
}

int StampRecorder::nextService(const QList<int> &tried, int from) const
{
    for (int i = 0; i < kServiceCount; ++i)
        if (const int s = (from + i) % kServiceCount; !tried.contains(s))
            return s;
    return -1;
}

void StampRecorder::request(const QByteArray &imprint, int service, quint64 round)
{
    const quint64 generation = m_generation;
    m_send(imprint, service, [this, generation, round, imprint, service](const QByteArray &token, const QString &error) {
        received(generation, round, imprint, service, token, error);
    });
}

void StampRecorder::received(quint64 generation, quint64 round, const QByteArray &imprint, int service,
                             const QByteArray &token, const QString &error)
{
    const std::optional<TimeStamp::Info> info = error.isEmpty() ? TimeStamp::info(token) : std::nullopt;
    const bool ok = info && info->imprint == imprint;
    // The cosign of a stamp already taken.
    for (qsizetype i = 0; i < m_awaited.size(); ++i)
        if (m_awaited[i].imprint == imprint && m_awaited[i].service == service) {
            cosignAnswered(i, ok ? token : QByteArray());
            return;
        }
    if (generation != m_generation || !m_doc || round != m_pending.round || !m_inFlight)
        return;
    if (!ok) {
        const QString line = QUrl(QLatin1String(kServices[service])).host() + QStringLiteral(": ")
                             + (error.isEmpty() ? tr("ответ не к этому запросу") : error);
        m_pending.errors = m_pending.errors.isEmpty() ? line : m_pending.errors + QStringLiteral("; ") + line;
        m_pending.tried.append(service);
        if (++m_pending.failed < m_pending.requests)
            return; // the other one may answer yet
        if (m_pending.tried.size() < kServiceCount) {
            send(); // the next two, at once
            return;
        }
        m_inFlight = false;
        m_error = m_pending.errors;
        m_pending.tried.clear();
        m_pending.errors.clear();
        emit changed();
        flushDone(false);
        m_idle.start(kRetryMs); // the records wait; the next try covers them with its own delay
        return;
    }
    m_inFlight = false;
    m_service = service;
    Stamps::addCertificates(m_doc->stampCertificates, TimeStamp::certificates(token));
    Stamp stamp;
    stamp.end = m_pending.end;
    stamp.delayMs = m_pending.delayMs;
    stamp.token = TimeStamp::withoutCertificates(token);
    stamp.version = Stamps::kVersion;
    stamp.mediaEnd = m_pending.mediaEnd;
    stamp.key = m_pending.key;
    m_doc->stamps.append(stamp);
    m_cover = {m_pending.raw, m_pending.mediaEnd, m_doc->stamps.size()};
    m_sessionStamped = true;
    // Its cosign: the other of the pair, on its way; or, when that one failed, another authority if still in time.
    Awaited cosign{imprint, -1, m_pending.tried, m_pending.sent};
    cosign.tried.append(service);
    const int other = m_pending.pair[0] == service ? m_pending.pair[1] : m_pending.pair[0];
    m_pending.tried.clear();
    m_pending.errors.clear();
    m_error.clear();
    if (other >= 0 && !cosign.tried.contains(other)) {
        cosign.service = other;
        cosign.tried.append(other);
        m_awaited.append(cosign);
    } else {
        askCosign(cosign);
    }
    emit changed();
    if (m_flushing) {
        if (m_flushSent) {
            if (awaits(imprint))
                m_flushImprint = imprint;
            else
                flushDone(true);
        } else { // the request on its way was older than the flush
            m_flushSent = true;
            send();
            return;
        }
    }
    // Typed on while it was on its way: the idle stamp covers the rest if nothing else does.
    if (m_chain.size() > m_pending.end && !m_idle.isActive())
        m_idle.start(Stamps::kIdleMs);
}

bool StampRecorder::awaits(const QByteArray &imprint) const
{
    for (const Awaited &a : m_awaited)
        if (a.imprint == imprint)
            return true;
    return false;
}

bool StampRecorder::askCosign(Awaited a)
{
    if (a.sent.elapsed() > kCosignLateMs)
        return false;
    const int s = nextService(a.tried, m_second >= 0 ? m_second : m_service);
    if (s < 0)
        return false;
    a.service = s;
    a.tried.append(s);
    m_awaited.append(a);
    request(a.imprint, s, 0);
    return true;
}

void StampRecorder::cosignAnswered(qsizetype i, const QByteArray &token)
{
    const Awaited a = m_awaited.takeAt(i);
    if (token.isEmpty()) {
        if (askCosign(a))
            return;
    } else if (m_doc) {
        // Its stamp, wherever it is now (an edit keeps it, maybe voided).
        for (qsizetype k = m_doc->stamps.size(); k-- > 0;) {
            Stamp &s = m_doc->stamps[k];
            const std::optional<TimeStamp::Info> info = TimeStamp::info(s.token);
            if (!info || info->imprint != a.imprint)
                continue;
            if (s.cosign.isEmpty()) {
                Stamps::addCertificates(m_doc->stampCertificates, TimeStamp::certificates(token));
                s.cosign = TimeStamp::withoutCertificates(token);
                m_second = a.service;
                emit changed();
            }
            break;
        }
    }
    if (!m_flushImprint.isEmpty() && m_flushImprint == a.imprint && !awaits(a.imprint))
        flushDone(true);
}

int StampRecorder::cosignAwaitedFrom() const
{
    int from = -1;
    if (!m_doc)
        return from;
    for (const Awaited &a : m_awaited)
        for (qsizetype k = m_doc->stamps.size(); k-- > 0;) {
            const std::optional<TimeStamp::Info> info = TimeStamp::info(m_doc->stamps[k].token);
            if (!info || info->imprint != a.imprint)
                continue;
            const int f = k ? m_doc->stamps[k - 1].end : 0;
            from = from < 0 ? f : std::min(from, f);
            break;
        }
    return from;
}

void StampRecorder::sendOverNetwork(const QByteArray &imprint, int service, Done done)
{
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    quint64 value = QRandomGenerator::system()->generate64() >> 1; // positive as a DER INTEGER
    const QByteArray body = TimeStamp::request(imprint, value, true);
    QByteArray nonce;
    do { // as TimeStamp::Info gives it: big-endian, no leading zeros
        nonce.prepend(char(value & 0xFF));
        value >>= 8;
    } while (value);
    QNetworkRequest req{QUrl(QLatin1String(kServices[service]))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/timestamp-query"));
    req.setTransferTimeout(kTimeoutMs);
    QNetworkReply *reply = m_net->post(req, body);
    const QList<QByteArray> pool = m_doc ? m_doc->stampCertificates : QList<QByteArray>();
    connect(reply, &QNetworkReply::finished, this, [reply, imprint, nonce, pool, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done({}, reply->errorString());
            return;
        }
        const TimeStamp::Response r = TimeStamp::parseResponse(reply->readAll());
        TimeStamp::Info info;
        // Plain HTTP: the answer counts only if it checks as the file will.
        const bool checks = r.granted && TimeStamp::verify(r.token, pool, &info) == TimeStamp::Check::Ok && info.sha256;
        if (!r.granted)
            done({}, r.error);
        else if (!checks)
            done({}, tr("ответ не проверяется"));
        else if (info.imprint != imprint || info.nonce != nonce)
            done({}, tr("ответ не к этому запросу"));
        else
            done(r.token, {});
    });
}
