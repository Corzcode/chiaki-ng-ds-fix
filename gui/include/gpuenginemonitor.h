// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <QVector>
#include <QSet>
#include <cstdint>
#include <functional>

// Monitors this-process GPU Video-Engine utilization via PDH
// ("\GPU Engine(*)\Utilization Percentage") and correlates it against the
// present pipeline's telemetry. Runs on the GUI thread at a low cadence
// (default 500 ms, always-on). It is diagnostic plumbing: when a sustained or
// rapid engine-utilization spike is detected while a stream is active, it
// flushes a bounded ring buffer (recent engine samples + a pipeline-context
// line supplied by the caller) to "<logdir>/gpu_monitor.log".
//
// This targets the intermittent "vc0 suddenly 90%, then back to 30%" symptom.
// By capturing BOTH the engine utilization trend and the pipeline snapshot at
// the moment of the spike, we can tell whether the spike correlates with decode,
// FSR/compute rendering, present-loop pacing, queue resets, or swapchain churn.
class GpuEngineMonitor : public QObject
{
	Q_OBJECT

	public:
		struct EngineSample
		{
			double utilized = 0.0; // max util across matched engines this tick (%)
			double video_decode = 0.0;
			double video_encode = 0.0;
			double compute_3d = 0.0; // 3D + Compute + CUDA
		double copy = 0.0;
		qint64 ts_ms = 0; // monotonic ms at sample time
		// Raw top-N PDH instances at sample time ("<instance>=<util>"),
		// so VC0/VC1-style engines stay distinguishable after aggregation.
		QString top_instances;
	};

		explicit GpuEngineMonitor(QObject *parent = nullptr);
		~GpuEngineMonitor() override;

		// Called once per sample to let the caller append a one-line pipeline
		// context snapshot (queue depth, latency, fps, packet loss, ...). It is
		// only invoked on the GUI thread, inside tick().
		void setPipelineSnapshot(std::function<void(const EngineSample &, QString &)> fn);

		void setStreamActive(bool active);
		void setEnabled(bool enabled);

	private:
		void tick();
		bool collectUtil(EngineSample &out);
		void refreshAcceptedPids();
		void flushSnapshot(const EngineSample &now, const QString &pipeline_ctx, double baseline);
		static QString formatSample(const EngineSample &s);

		std::function<void(const EngineSample &, QString &)> pipeline_snapshot_;
		QTimer *timer_ = nullptr;
		bool enabled_ = true;
		bool stream_active_ = false;

		// Bounded ring of recent samples (capacity = 64 -> ~32 s of history at 500 ms).
		static constexpr int kRingCapacity = 64;
		QVector<EngineSample> ring_;
		int ring_head_ = 0;
		int ring_size_ = 0;
		qint64 last_flush_ms_ = 0;
		qint64 last_pids_refresh_ms_ = 0;

		// Accepted PIDs (current process + descendants) whose GPU engine
		// instances the monitor attributes to this client.
		QSet<quint32> accepted_pids_;

#ifdef _WIN32
		bool pdh_ok_ = false;
		void *pdh_query_ = nullptr;   // PDH_HQUERY
		void *pdh_counter_ = nullptr; // PDH_HCOUNTER
#endif
};