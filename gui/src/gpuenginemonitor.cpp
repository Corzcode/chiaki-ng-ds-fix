// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "gpuenginemonitor.h"
#include "sessionlog.h"

#include <QByteArray>
#include <QFile>
#include <QDateTime>
#include <QTextStream>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <pdh.h>
#include <tlhelp32.h>
// mingw's pdh.h omits these PI status codes used to drive the two-pass sizing.
#ifndef PDH_MORE_DATA
#define PDH_MORE_DATA ((PDH_STATUS)0x800007D2L)
#endif
#ifndef PDH_NO_DATA
#define PDH_NO_DATA ((PDH_STATUS)0x800007D5L)
#endif
#endif

namespace {

// Sample cadence: low enough to be free, fast enough to catch ~0.5 s spikes.
constexpr int kIntervalMs = 500;
// If a matched engine exceeds this while the stream is active, we care.
constexpr double kSpikeThreshold = 60.0;
// A "sudden" rise: now.utilized >= threshold while the mean of the previous
// two samples sat well below it.
constexpr double kBaselineMeanThreshold = 40.0;
// Cooldown between flushes to avoid flooding the log on a persistent spike.
constexpr qint64 kFlushCooldownMs = 3000;

constexpr char kLogName[] = "gpu_monitor.log";

qint64 monoMs()
{
	return static_cast<qint64>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch())
			.count());
}

// Instance name from PDH, e.g.:
//   pid_1234,luid_00000000_0000B48D,engtype_VideoDecode
// Returns the pid (or 0 if unparseable).
quint32 instancePid(const QString &name)
{
	const int p = name.indexOf(QStringLiteral("pid_"));
	if (p < 0)
		return 0;
	quint32 v = 0;
	for (int i = p + 4; i < name.size() && name[i].isDigit(); i++)
		v = v * 10 + static_cast<quint32>(name[i].digitValue());
	return v;
}

QString instanceEngineType(const QString &name)
{
	const int p = name.lastIndexOf(QStringLiteral("engtype_"));
	if (p < 0)
		return QString();
	return name.mid(p + 8);
}

} // namespace

GpuEngineMonitor::GpuEngineMonitor(QObject *parent)
	: QObject(parent)
{
	ring_.resize(kRingCapacity);

#ifdef _WIN32
	PDH_HQUERY q = nullptr;
	PDH_STATUS st = PdhOpenQueryW(nullptr, 0, &q);
	if (st != ERROR_SUCCESS) {
		qWarning() << "GpuEngineMonitor: PdhOpenQueryW failed" << QString::number(st, 16);
		return;
	}
	PDH_HCOUNTER c = nullptr;
	st = PdhAddEnglishCounterW(q,
			L"\\GPU Engine(*)\\Utilization Percentage", 0, &c);
	if (st != ERROR_SUCCESS) {
		qWarning() << "GpuEngineMonitor: PdhAddEnglishCounterW failed" << QString::number(st, 16);
		PdhCloseQuery(q);
		return;
	}
	// Prime the query so the first tick() returns real data instead of stale 0s.
	PdhCollectQueryData(q);
	pdh_query_ = q;
	pdh_counter_ = c;
	pdh_ok_ = true;
#endif

	timer_ = new QTimer(this);
	timer_->setInterval(kIntervalMs);
	timer_->setTimerType(Qt::CoarseTimer);
	connect(timer_, &QTimer::timeout, this, &GpuEngineMonitor::tick);
}

GpuEngineMonitor::~GpuEngineMonitor()
{
#ifdef _WIN32
	if (pdh_query_) {
		PdhCloseQuery(static_cast<PDH_HQUERY>(pdh_query_));
		pdh_query_ = nullptr;
	}
#endif
}

void GpuEngineMonitor::setPipelineSnapshot(std::function<void(const EngineSample &, QString &)> fn)
{
	pipeline_snapshot_ = std::move(fn);
}

void GpuEngineMonitor::setStreamActive(bool active)
{
	// Switching stream state resets the sample history so the baseline is not
	// polluted by the previous session (or a long idle period).
	if (stream_active_ != active) {
		stream_active_ = active;
		ring_size_ = 0;
		ring_head_ = 0;
		last_flush_ms_ = 0;
		if (active)
			refreshAcceptedPids();
	}
	// QTimer has no setEnabled(): drive it via start()/stop() directly.
	const bool run = stream_active_ && enabled_;
	if (run)
		timer_->start();
	else
		timer_->stop();
}

void GpuEngineMonitor::setEnabled(bool enabled)
{
	enabled_ = enabled;
	const bool run = stream_active_ && enabled_;
	if (run)
		timer_->start();
	else
		timer_->stop();
}

void GpuEngineMonitor::refreshAcceptedPids()
{
	accepted_pids_.clear();
#ifdef _WIN32
	const quint32 self = static_cast<quint32>(GetCurrentProcessId());
	accepted_pids_.insert(self);
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return;
	PROCESSENTRY32W pe;
	pe.dwSize = sizeof(pe);
	if (Process32FirstW(snap, &pe)) {
		do {
			if (pe.th32ParentProcessID == self)
				accepted_pids_.insert(pe.th32ProcessID);
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);
#endif
}

bool GpuEngineMonitor::collectUtil(EngineSample &out)
{
#ifdef _WIN32
	if (!pdh_ok_)
		return false;

	PDH_STATUS st = PdhCollectQueryData(static_cast<PDH_HQUERY>(pdh_query_));
	if (st != ERROR_SUCCESS && st != PDH_NO_DATA)
		return false;

	DWORD buf_size = 0, item_count = 0;
	st = PdhGetFormattedCounterArrayW(static_cast<PDH_HCOUNTER>(pdh_counter_), PDH_FMT_DOUBLE, &buf_size, &item_count, nullptr);
	if (st != PDH_MORE_DATA || item_count == 0)
		return false;

	QVector<PDH_FMT_COUNTERVALUE_ITEM_W> items(item_count);
	st = PdhGetFormattedCounterArrayW(static_cast<PDH_HCOUNTER>(pdh_counter_), PDH_FMT_DOUBLE, &buf_size, &item_count, items.data());
	if (st != ERROR_SUCCESS || item_count == 0)
		return false;

	double max_util = 0.0, max_vdec = 0.0, max_venc = 0.0, max_3d = 0.0, max_copy = 0.0;
	for (DWORD i = 0; i < item_count; i++) {
		if (items[i].FmtValue.CStatus != ERROR_SUCCESS)
			continue;
		const QString name = QString::fromWCharArray(items[i].szName);
		if (!accepted_pids_.contains(instancePid(name)))
			continue;
		const double u = items[i].FmtValue.doubleValue;
		const QString eng = instanceEngineType(name);
		if (u > max_util)
			max_util = u;
		if (eng.contains(QStringLiteral("VideoDecode"), Qt::CaseInsensitive)) {
			if (u > max_vdec)
				max_vdec = u;
		} else if (eng.contains(QStringLiteral("VideoEncode"), Qt::CaseInsensitive)) {
			if (u > max_venc)
				max_venc = u;
		} else if (eng == QStringLiteral("3D") || eng == QStringLiteral("Compute") ||
				eng == QStringLiteral("Cuda")) {
			if (u > max_3d)
				max_3d = u;
		} else if (eng == QStringLiteral("Copy")) {
			if (u > max_copy)
				max_copy = u;
		}
	}

	out = EngineSample{};
	out.utilized = max_util;
	out.video_decode = max_vdec;
	out.video_encode = max_venc;
	out.compute_3d = max_3d;
	out.copy = max_copy;
	out.ts_ms = monoMs();
	return true;
#else
	Q_UNUSED(out);
	return false;
#endif
}

void GpuEngineMonitor::tick()
{
	if (!stream_active_ || !enabled_)
		return;

	const qint64 now = monoMs();
	// Refresh the accepted PIDs periodically in case a decoder helper spawns late.
	if (now - last_pids_refresh_ms_ > 5000) {
		last_pids_refresh_ms_ = now;
		refreshAcceptedPids();
	}

	EngineSample s;
	if (!collectUtil(s))
		return;
	s.ts_ms = now;

	// Push into ring (overwrite oldest).
	ring_[ring_head_] = s;
	ring_head_ = (ring_head_ + 1) % kRingCapacity;
	if (ring_size_ < kRingCapacity)
		ring_size_++;

	// Baseline = mean util of the up-to-two immediately-prior samples.
	double prev_mean = 0.0;
	int n = qMin(2, ring_size_ - 1);
	for (int k = 1; k <= n; k++) {
		const int idx = (ring_head_ - 1 - k + kRingCapacity) % kRingCapacity;
		prev_mean += ring_[idx].utilized;
	}
	if (n > 0)
		prev_mean /= n;

	const bool sudden_rise = s.utilized >= kSpikeThreshold && prev_mean <= kBaselineMeanThreshold;
	const bool sustained = s.utilized >= (kSpikeThreshold + 15.0) && ring_size_ >= 3 &&
		ring_[ (ring_head_ - 2 + kRingCapacity) % kRingCapacity ].utilized >= kSpikeThreshold &&
		ring_[ (ring_head_ - 3 + kRingCapacity) % kRingCapacity ].utilized >= kSpikeThreshold;

	if ((sudden_rise || sustained) && (now - last_flush_ms_) >= kFlushCooldownMs) {
		last_flush_ms_ = now;
		QString pipeline_ctx;
		if (pipeline_snapshot_)
			pipeline_snapshot_(s, pipeline_ctx);
		flushSnapshot(s, pipeline_ctx, prev_mean);
	}
}

void GpuEngineMonitor::flushSnapshot(const EngineSample &now, const QString &pipeline_ctx, double baseline)
{
	const QString dir = GetLogBaseDir();
	if (dir.isEmpty())
		return;
	QFile f(QStringLiteral("%1/%2").arg(dir, QString::fromLatin1(kLogName)));
	if (!f.open(QIODevice::Append | QIODevice::Text))
		return;

	QTextStream out(&f);
	const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz"));
	out << "\n===== GPU Engine spike " << ts << " baseline=" << baseline
	    << " utilized=" << now.utilized << " =====\n";
	out << "engine trend (older -> latest, every 500ms):\n";
	// Dump the trailing ~24 samples (12 s) before the spike.
	const int start = qMax(0, ring_size_ - 24);
	for (int i = start; i < ring_size_; i++) {
		const int idx = (ring_head_ - (ring_size_ - i) + kRingCapacity) % kRingCapacity;
		const EngineSample &s = ring_[idx];
		out << "  util=" << QString::number(s.utilized, 'f', 1)
		    << " vdec=" << QString::number(s.video_decode, 'f', 1)
		    << " venc=" << QString::number(s.video_encode, 'f', 1)
		    << " 3d=" << QString::number(s.compute_3d, 'f', 1)
		    << " copy=" << QString::number(s.copy, 'f', 1) << "\n";
	}
	if (!pipeline_ctx.isEmpty())
		out << "pipeline: " << pipeline_ctx << "\n";
	out.flush();
}