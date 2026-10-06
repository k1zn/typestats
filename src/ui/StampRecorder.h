#pragma once

#include "core/Stamps.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <functional>

class QNetworkAccessManager;

// Takes the time stamps of a recording while it is typed (re/stamps.md): at the first key of a session, at the
// first key Stamps::kIntervalMs after the last stamped record, Stamps::kIdleMs after the last key, and when
// recording stops. Only hashes leave the computer; the stamps and the authorities' certificates go into the
// document.
class StampRecorder : public QObject
{
    Q_OBJECT
public:
    // Sends an imprint to authority number `service` of services(); `done` gets the token as it came (or an
    // error). The next authority is tried with a new request, the delay of which says how late it is.
    using Done = std::function<void(const QByteArray &token, const QString &error)>;
    using Send = std::function<void(const QByteArray &imprint, int service, Done done)>;

    explicit StampRecorder(QObject *parent = nullptr);

    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }
    // The document: its records are followed, stamps and certificates are added to it. Call again after the
    // records were replaced (another document, an edit): the chain is built anew, a request in flight dropped.
    void attach(TsfDocument *doc);
    // Records were appended to the document (typing).
    void recordsAdded();
    // Recording was switched on or off: the next key starts a new session; off - what is left is stamped now.
    void captureChanged(bool on);

    bool busy() const { return m_inFlight; }
    QString lastError() const { return m_error; }
    // The authorities, in the order they are tried.
    static QStringList services();

    void setSend(Send send) { m_send = std::move(send); }

signals:
    void changed(); // a stamp was added, or a request failed

private:
    void stampIfDue(bool now);
    void send();
    void received(quint64 generation, const QByteArray &token, const QString &error);
    int stampedEnd() const;
    void sendOverNetwork(const QByteArray &imprint, int service, Done done);

    TsfDocument *m_doc = nullptr;
    Stamps::Chain m_chain;
    qsizetype m_raw = 0;            // records of the document the chain has seen
    bool m_enabled = false;
    bool m_sessionStamped = false;  // this session of recording has a stamp
    bool m_inFlight = false;
    quint64 m_generation = 0;       // a request of an older document (or before an edit) is dropped
    struct Pending
    {
        int end = 0;
        quint32 delayMs = 0;
        QByteArray imprint;
        int service = 0;
        int tries = 0;     // authorities that failed this time
        QString errors;
    } m_pending;
    QElapsedTimer m_sinceKept;      // since the last record the normalization kept
    QTimer m_idle;
    QString m_error;
    Send m_send;
    QNetworkAccessManager *m_net = nullptr;
    int m_service = 0;              // the authority that answered last
};
