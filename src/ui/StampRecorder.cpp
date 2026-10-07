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
constexpr int kRetryMs = 5000;

} // namespace

StampRecorder::StampRecorder(QObject *parent) : QObject(parent)
{
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, [this] { stampIfDue(true); });
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
        ++m_generation; // an answer still on its way is not wanted
        m_inFlight = false;
    }
    m_sessionStamped = false;
}

void StampRecorder::attach(TsfDocument *doc)
{
    m_doc = doc;
    ++m_generation;
    m_inFlight = false;
    m_idle.stop();
    m_chain.reset();
    m_raw = 0;
    m_packetHashes.clear();
    if (m_doc)
        for (; m_raw < m_doc->records.size(); ++m_raw)
            m_chain.push(m_doc->records[m_raw]);
    m_sessionStamped = false;
    m_pending = {};
    m_error.clear();
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

void StampRecorder::captureChanged(bool on)
{
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
    m_pending.delayMs = quint32(m_sinceKept.elapsed());
    // The packets the encoders gave so far (chain v2, re/stamps.md).
    const int mediaFrom = stampedMediaEnd(), mediaTo = m_clip ? int(m_clip->packets.size()) : 0;
    for (int i = int(m_packetHashes.size() / 32); i < mediaTo; ++i)
        m_packetHashes += Stamps::packetHash(m_clip->packets[i]);
    m_pending.mediaEnd = mediaTo;
    m_pending.imprint = m_chain.hashV2(previous, last, n, m_pending.delayMs,
                                       QByteArrayView(m_packetHashes).sliced(qsizetype(mediaFrom) * 32,
                                                                             qsizetype(mediaTo - mediaFrom) * 32),
                                       mediaTo - mediaFrom);
    m_pending.service = (m_service + m_pending.tries) % kServiceCount;
    m_inFlight = true;
    const quint64 generation = m_generation;
    m_send(m_pending.imprint, m_pending.service, [this, generation](const QByteArray &token, const QString &error) {
        received(generation, token, error);
    });
}

void StampRecorder::received(quint64 generation, const QByteArray &token, const QString &error)
{
    if (generation != m_generation || !m_doc)
        return;
    m_inFlight = false;
    const std::optional<TimeStamp::Info> info = error.isEmpty() ? TimeStamp::info(token) : std::nullopt;
    if (!info || info->imprint != m_pending.imprint) {
        const QString line = QUrl(QLatin1String(kServices[m_pending.service])).host() + QStringLiteral(": ")
                             + (error.isEmpty() ? tr("ответ не к этому запросу") : error);
        m_pending.errors = m_pending.errors.isEmpty() ? line : m_pending.errors + QStringLiteral("; ") + line;
        if (++m_pending.tries < kServiceCount) {
            send(); // the next authority, at once
            return;
        }
        m_error = m_pending.errors;
        m_pending.tries = 0;
        m_pending.errors.clear();
        emit changed();
        m_idle.start(kRetryMs); // the records wait; the next try covers them with its own delay
        return;
    }
    m_service = m_pending.service;
    m_pending.tries = 0;
    m_pending.errors.clear();
    m_error.clear();
    Stamps::addCertificates(m_doc->stampCertificates, TimeStamp::certificates(token));
    Stamp stamp;
    stamp.end = m_pending.end;
    stamp.delayMs = m_pending.delayMs;
    stamp.token = TimeStamp::withoutCertificates(token);
    stamp.version = 2;
    stamp.mediaEnd = m_pending.mediaEnd;
    m_doc->stamps.append(stamp);
    m_sessionStamped = true;
    emit changed();
    // Typed on while it was on its way: the idle stamp covers the rest if nothing else does.
    if (m_chain.size() > m_pending.end && !m_idle.isActive())
        m_idle.start(Stamps::kIdleMs);
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
    connect(reply, &QNetworkReply::finished, this, [reply, imprint, nonce, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done({}, reply->errorString());
            return;
        }
        const TimeStamp::Response r = TimeStamp::parseResponse(reply->readAll());
        const std::optional<TimeStamp::Info> info = r.granted ? TimeStamp::info(r.token) : std::nullopt;
        if (!r.granted)
            done({}, r.error);
        else if (!info || info->imprint != imprint || info->nonce != nonce)
            done({}, tr("ответ не к этому запросу"));
        else
            done(r.token, {});
    });
}
