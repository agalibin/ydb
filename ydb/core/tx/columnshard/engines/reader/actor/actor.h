#pragma once
#include <ydb/core/formats/arrow/converter.h>
#include <ydb/core/kqp/compute_actor/kqp_compute_events.h>
#include <ydb/core/tx/columnshard/blobs_action/abstract/storages_manager.h>
#include <ydb/core/tx/columnshard/columnshard_private_events.h>
#include <ydb/core/tx/columnshard/counters/scan.h>
#include <ydb/core/tx/columnshard/engines/reader/abstract/abstract.h>
#include <ydb/core/tx/columnshard/engines/reader/abstract/read_context.h>
#include <ydb/core/tx/columnshard/engines/reader/abstract/read_metadata.h>
#include <ydb/core/tx/conveyor_composite/usage/config.h>
#include <ydb/core/tx/tracing/usage/tracing.h>

#include <ydb/library/actors/core/actor_bootstrapped.h>
#include <ydb/library/actors/core/log.h>
#include <ydb/library/chunks_limiter/chunks_limiter.h>

namespace NKikimr::NOlap::NReader {

class TColumnShardScan: public TActorBootstrapped<TColumnShardScan>,
                        NArrow::IRowWriter,
                        NColumnShard::TMonitoringObjectsCounter<TColumnShardScan> {
private:
    TActorId ResourceSubscribeActorId;
    TActorId ReadCoordinatorActorId;
    const std::shared_ptr<IStoragesManager> StoragesManager;
    const std::shared_ptr<NDataAccessorControl::IDataAccessorsManager> DataAccessorsManager;
    std::optional<TMonotonic> StartInstant;
    std::optional<TMonotonic> FinishInstant;

public:
    virtual void PassAway() override;

    TColumnShardScan(const TActorId& columnShardActorId, const TActorId& scanComputeActorId,
        const std::shared_ptr<IStoragesManager>& storagesManager,
        const std::shared_ptr<NDataAccessorControl::IDataAccessorsManager>& dataAccessorsManager,
        const TComputeShardingPolicy& computeShardingPolicy, ui32 scanId, ui64 txId, ui32 scanGen, ui64 requestCookie, ui64 tabletId,
        TDuration timeout, const TReadMetadataBase::TConstPtr& readMetadataRange, NKikimrDataEvents::EDataFormat dataFormat,
        const NColumnShard::TScanCounters& scanCountersPool, const NConveyorComposite::TCPULimitsConfig& cpuLimits
    );

    void Bootstrap(const TActorContext& ctx);

private:
    STATEFN(StateScan) {
        auto g = Stats->MakeGuard("processing", IS_INFO_LOG_ENABLED(NKikimrServices::TX_COLUMNSHARD_SCAN));
        TLogContextGuard gLogging(NActors::TLogContextBuilder::Build(NKikimrServices::TX_COLUMNSHARD_SCAN) ("SelfId", SelfId())("TabletId",
                    TabletId)("ScanId", ScanId)("TxId", TxId)("ScanGen", ScanGen)("task_identifier", ReadMetadataRange->GetScanIdentifier()));
        switch (ev->GetTypeRewrite()) {
            hFunc(NKqp::TEvKqpCompute::TEvScanDataAck, HandleScan);
            hFunc(NKqp::TEvKqpCompute::TEvScanPing, HandleScan);
            hFunc(NKqp::TEvKqp::TEvAbortExecution, HandleScan);
            hFunc(NActors::TEvents::TEvPoison, HandleScan);
            hFunc(TEvents::TEvUndelivered, HandleScan);
            hFunc(TEvents::TEvWakeup, HandleScan);
            hFunc(NColumnShard::TEvPrivate::TEvTaskProcessedResult, HandleScan);
            default:
                AFL_VERIFY(false)("unexpected_event", ev->GetTypeName());
        }
    }

    void HandleScan(NColumnShard::TEvPrivate::TEvTaskProcessedResult::TPtr& ev);

    void HandleScan(NKqp::TEvKqpCompute::TEvScanDataAck::TPtr& ev);

    void HandleScan(NKqp::TEvKqpCompute::TEvScanPing::TPtr& ev);

    // Returns true if it was able to produce new batch
    bool ProduceResults() noexcept;

    void ContinueProcessing();

    void HandleScan(NKqp::TEvKqp::TEvAbortExecution::TPtr& ev) noexcept;
    void HandleScan(NActors::TEvents::TEvPoison::TPtr& ev) noexcept;

    void HandleScan(TEvents::TEvUndelivered::TPtr& ev);

    void HandleScan(TEvents::TEvWakeup::TPtr& /*ev*/);

private:
    void CheckHanging(const bool logging = false) const;

    void MakeResult(size_t reserveRows = 0);

    void AddRow(const TConstArrayRef<TCell>& row) override;

    TOwnedCellVec ConvertLastKey(const std::shared_ptr<arrow::RecordBatch>& lastReadKey);

    class TScanStatsOwner: public NKqp::TEvKqpCompute::IShardScanStats {
    private:
        YDB_READONLY_DEF(TReadStats, Stats);

    public:
        TScanStatsOwner(const TReadStats& stats)
            : Stats(stats) {
        }

        virtual THashMap<TString, ui64> GetMetrics() const override {
            THashMap<TString, ui64> result;
            result["compacted_bytes"] = Stats.CompactedPortionsBytes;
            result["inserted_bytes"] = Stats.InsertedPortionsBytes;
            result["committed_bytes"] = Stats.CommittedPortionsBytes;
            return result;
        }
    };

    bool SendResult(bool pageFault, bool lastBatch);

    void SendScanError(const TString& reason);

    void Finish(const NColumnShard::TScanCounters::EStatusFinish status);

    void ReportStats();

    void ScheduleWakeup(const TMonotonic deadline);

    TMonotonic GetScanDeadline() const;

    TMonotonic GetComputeDeadline() const;
    std::optional<TMonotonic> GetComputeDeadlineOptional() const;

private:
    const TActorId ColumnShardActorId;
    const TActorId ReadBlobsActorId;
    const TActorId ScanComputeActorId;
    std::optional<TMonotonic> AckReceivedInstant;
    TActorId ScanActorId;
    TActorId BlobCacheActorId;
    const ui32 ScanId;
    const ui64 TxId;
    const ui32 ScanGen;
    const ui64 RequestCookie;
    const NKikimrDataEvents::EDataFormat DataFormat;
    const ui64 TabletId;
    const NConveyorComposite::TCPULimitsConfig CPULimits;

    TReadMetadataBase::TConstPtr ReadMetadataRange;
    std::unique_ptr<TScanIteratorBase> ScanIterator;

    std::vector<std::pair<TString, NScheme::TTypeInfo>> KeyYqlSchema;
    const TSerializedTableRange TableRange;
    const TSmallVec<bool> SkipNullKeys;
    const TDuration Timeout;
    NColumnShard::TConcreteScanCounters ScanCountersPool;

    TMaybe<TString> AbortReason;

    TChunksLimiter ChunksLimiter;
    THolder<NKqp::TEvKqpCompute::TEvScanData> Result;
    std::shared_ptr<IScanCursor> CurrentLastReadKey;
    bool Finished = false;
    ui32 BuildResultCounter = 0;
    std::optional<TMonotonic> LastResultInstant;

    class TBlobStats {
    private:
        ui64 PartsCount = 0;
        ui64 Bytes = 0;
        TDuration ReadingDurationSum;
        TDuration ReadingDurationMax;
        NMonitoring::THistogramPtr BlobDurationsCounter;
        NMonitoring::THistogramPtr ByteDurationsCounter;

    public:
        TBlobStats(const NMonitoring::THistogramPtr blobDurationsCounter, const NMonitoring::THistogramPtr byteDurationsCounter)
            : BlobDurationsCounter(blobDurationsCounter)
            , ByteDurationsCounter(byteDurationsCounter) {
        }
        void Received(const TBlobRange& br, const TDuration d) {
            ReadingDurationSum += d;
            ReadingDurationMax = Max(ReadingDurationMax, d);
            ++PartsCount;
            Bytes += br.Size;
            BlobDurationsCounter->Collect(d.MilliSeconds());
            ByteDurationsCounter->Collect((i64)d.MilliSeconds(), br.Size);
        }
        TString DebugString() const {
            TStringBuilder sb;
            if (PartsCount) {
                sb << "p_count=" << PartsCount << ";";
                sb << "bytes=" << Bytes << ";";
                sb << "d_avg=" << ReadingDurationSum / PartsCount << ";";
                sb << "d_max=" << ReadingDurationMax << ";";
            } else {
                sb << "NO_BLOBS;";
            }
            return sb;
        }
    };

    NTracing::TTraceClientGuard Stats;
    const TComputeShardingPolicy ComputeShardingPolicy;
    ui64 Rows = 0;
    ui64 BytesSum = 0;
    ui64 RowsSum = 0;
    ui64 PacksSum = 0;
    ui64 Bytes = 0;
    ui32 PageFaults = 0;
    TInstant StartWaitTime;
    TDuration WaitTime;
    TInstant LastSend = TInstant::Now();
};

}   // namespace NKikimr::NOlap::NReader
