#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// CamBotTimeline — Timeline Container
// ═══════════════════════════════════════════════════════════════════════════════

#include "timeline/track.h"
#include <QMap>
#include <QVariant>
#include <memory>

namespace timeline {

class Timeline {
public:
    void addTrack(std::shared_ptr<ITimelineTrack> track) {
        if (track) {
            m_tracks[track->trackId()] = track;
        }
    }

    std::shared_ptr<ITimelineTrack> track(const QString& trackId) const {
        return m_tracks.value(trackId, nullptr);
    }
    
    QList<std::shared_ptr<ITimelineTrack>> allTracks() const {
        return m_tracks.values();
    }

    // Samples the entire system state at time t
    QMap<QString, QVariant> getFrame(double t) const {
        QMap<QString, QVariant> frame;
        for (auto it = m_tracks.begin(); it != m_tracks.end(); ++it) {
            frame[it.key()] = it.value()->sampleAt(t);
        }
        return frame;
    }

    bool validateLimits(QStringList& outErrors) const {
        bool ok = true;
        for (auto it = m_tracks.begin(); it != m_tracks.end(); ++it) {
            QString err;
            if (!it.value()->validateLimits(err)) {
                outErrors.append(QString("[%1] %2").arg(it.key(), err));
                ok = false;
            }
        }
        return ok;
    }

private:
    QMap<QString, std::shared_ptr<ITimelineTrack>> m_tracks;
};

} // namespace timeline
