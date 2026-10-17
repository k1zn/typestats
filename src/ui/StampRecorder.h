#pragma once

#include "core/Stamps.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <functional>

class QNetworkAccessManager;

// Takes the time stamps of a recording while it is typed (re/stamps.md): at the first key of a session, at the
// first key Stamps::kIntervalMs after the last stamped record, Stamps::kIdleMs after the last key, when recording
// stops, Stamps::kIntervalMs after video the stamps do not hold yet, and before saving (flush). Only hashes leave the computer; the stamps and the authorities' certificates go into the
// document. Each imprint goes to two authorities at once: the first answer is the stamp, the second its cosign
// (re/stamps.md, "Две службы").
class StampRecorder : public QObject
{
    Q_OBJECT
public:
    // Sends an imprint to authority number `service` of services(); `done` gets the token as it came (or an
    // error). When both of a pair fail, the next two are tried with a new request, the delay of which says how late
    // it is.
    using Done = std::function<void(const QByteArray &token, const QString &error)>;
    using Send = std::function<void(const QByteArray &imprint, int service, Done done)>;

    explicit StampRecorder(QObject *parent = nullptr);

    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }
    // The document: its records are followed, stamps and certificates are added to it. Call again after the
    // records were replaced (an edit; `another` - another document): the chain is built anew, a request in flight
    // dropped.
    void attach(TsfDocument *doc, bool another = false);
    // The webcam's clip of the document: its packets go under the stamps with the records (chain v3).
    void setClip(const MediaClip *clip) { m_clip = clip; m_packetHashes.clear(); }
    // Records were appended to the document (typing).
    void recordsAdded();
    // Packets were added to the clip: video with no keys is stamped too, an interval after it began.
    void packetsAdded();
    // Recording was switched on or off: the next key starts a new session; off - what is left is stamped now.
    void captureChanged(bool on);

    // What the last stamp holds: the raw records of the document, the packets of the clip and the stamps up to it.
    struct Cover
    {
        qsizetype records = 0;
        int packets = 0;
        qsizetype stamps = 0;
    };
    // Before saving: a stamp of everything there is now (after the request on its way, if any); flushed() tells
    // when it came with its cosign, or the second authority gave up (true, cover() holds it), or could not be had
    // (false: nothing to stamp with, the authorities did not answer). It may come at once, from within the call.
    void flush();
    Cover cover() const { return m_cover; }

    bool busy() const { return m_inFlight; }
    bool cosignsAwaited() const { return !m_awaited.isEmpty(); }
    // The first normalized record that a cosign on its way will confirm a second time (-1: none on its way).
    int cosignAwaitedFrom() const;
    // Records (normalized) and packets from these on came while this document was open here: while recording, those
    // after the last stamp wait for the next one; those of a file that came so do not.
    int ownRecordsFrom() const { return m_ownRecords; }
    int ownPacketsFrom() const { return m_ownPackets; }
    QString lastError() const { return m_error; }
    // The authorities, in the order they are tried.
    static QStringList services();

    void setSend(Send send) { m_send = std::move(send); }

signals:
    void changed(); // a stamp was added, or a request failed
    void flushed(bool ok);

private:
    void stampIfDue(bool now);
    bool covered() const;
    void flushDone(bool ok);
    void send();
    void request(const QByteArray &imprint, int service, quint64 round);
    void received(quint64 generation, quint64 round, const QByteArray &imprint, int service, const QByteArray &token,
                  const QString &error);
    struct Awaited
    {
        QByteArray imprint; // of the stamp it is for
        int service = -1;   // asked now
        QList<int> tried;   // the authorities that had it (or the stamp)
        QElapsedTimer sent; // since the stamp's request left
    };
    bool askCosign(Awaited a);
    bool awaits(const QByteArray &imprint) const;
    void cosignAnswered(qsizetype i, const QByteArray &token);
    int nextService(const QList<int> &tried, int from) const;
    int stampedEnd() const;
    int stampedMediaEnd() const;
    void sendOverNetwork(const QByteArray &imprint, int service, Done done);

    TsfDocument *m_doc = nullptr;
    const MediaClip *m_clip = nullptr;
    QByteArray m_packetHashes;      // SHA-256 of the clip's packets so far, 32 bytes each
    Stamps::Chain m_chain;
    qsizetype m_raw = 0;            // records of the document the chain has seen
    int m_ownRecords = 0, m_ownPackets = 0;
    bool m_enabled = false;
    bool m_sessionStamped = false;  // this session of recording has a stamp
    bool m_inFlight = false;
    quint64 m_generation = 0;       // a request of an older document (or before an edit) is dropped
    struct Pending
    {
        int end = 0;
        int mediaEnd = 0;
        qsizetype raw = 0; // the document's records it holds
        quint32 delayMs = 0;
        QByteArray imprint;
        QByteArray key;    // of the salts (chain v3)
        quint64 round = 0; // of the pair asked now
        int pair[2] = {-1, -1};
        int requests = 0, failed = 0; // of the pair
        QElapsedTimer sent;
        QList<int> tried;  // authorities that failed this time
        QString errors;
    } m_pending;
    quint64 m_round = 0;
    QList<Awaited> m_awaited;       // cosigns on their way
    QByteArray m_flushImprint;      // the flush waits for the cosign of this stamp
    QElapsedTimer m_sinceKept;      // since the last record the normalization kept
    QTimer m_idle;
    QTimer m_videoDue;              // video the stamps do not hold yet
    Cover m_cover;
    bool m_flushing = false, m_flushSent = false;
    bool m_capturing = false;
    QString m_error;
    Send m_send;
    QNetworkAccessManager *m_net = nullptr;
    int m_service = 0;              // the authority that answered first last time
    int m_second = -1;              // and the one that cosigned last
};
