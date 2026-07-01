#pragma once

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace PicoATE::Core {

using PlanId = QString;
using SequenceId = QString;
using NodeId = QString;
using EdgeId = QString;
using UutId = QString;
using FrameId = QString;
using ActivationId = QString;
using AttemptId = QString;
using ResourceId = QString;
using ResourceLeaseId = QString;
using ResourceRequestId = QString;
using BarrierInstanceId = QString;
using CleanupRegionId = QString;
using LoopId = QString;

enum class ExecNodeKind {
    Noop,
    Wait,
    Action,
    Barrier,
    Cleanup,
    Loop,
    TestItem,
    Limit,
    Statement,
    SequenceCall
};

enum class EdgeKind {
    Dependency,
    Control,
    Finally,
    BarrierJoin
};

enum class EdgeTrigger {
    OnSuccess,
    OnFail,
    OnError,
    OnTimeout,
    OnCancelled,
    OnSkipped,
    OnStopRequested,
    OnAbortRequested,
    Always,
    Finally
};

enum class NodeOutcome {
    Unknown,
    Passed,
    Failed,
    Error,
    Timeout,
    Cancelled,
    Skipped
};

enum class ResourceMode {
    SharedRead,
    SharedWrite,
    Exclusive,
    Counted,
    OrderedExclusive
};

enum class CleanupReason {
    NormalCompletion,
    StepFailed,
    ModuleError,
    Timeout,
    UserStop,
    UserAbort,
    HostCrash,
    UnknownUnsafeState
};

enum class ErrorAction {
    Continue,
    StopUut,
    Retry,
    RunCleanup,
    Abort
};

struct ResourceRequirement {
    ResourceId resourceId;
    ResourceMode mode = ResourceMode::Exclusive;
    int count = 1;
    int priority = 0;
    int acquireTimeoutMs = 30000;
};

struct RetryPolicy {
    int maxAttempts = 1;
    int delayMs = 0;
    QString retryWhenExpression;
};

struct TimeoutPolicy {
    int timeoutMs = 0;
};

struct NodeErrorPolicy {
    ErrorAction onFail = ErrorAction::StopUut;
    ErrorAction onError = ErrorAction::StopUut;
    ErrorAction onTimeout = ErrorAction::StopUut;
    CleanupRegionId cleanupRegionId;
    bool stopUutOnFailure = true;
};

struct ExecNode {
    NodeId id;
    QString localId;
    QString key;
    QString displayName;
    ExecNodeKind kind = ExecNodeKind::Noop;
    QVariantMap payload;
    QVector<ResourceRequirement> resources;
    TimeoutPolicy timeout;
    RetryPolicy retry;
    NodeErrorPolicy errorPolicy;
    bool alwaysRun = false;
    bool checkpointBefore = false;
    bool checkpointAfter = false;
    bool resultRecording = true;
    QStringList tags;
};

struct ExecEdge {
    EdgeId id;
    NodeId from;
    NodeId to;
    EdgeKind kind = EdgeKind::Dependency;
    EdgeTrigger trigger = EdgeTrigger::OnSuccess;
    QString condition;
    int priority = 0;
};

struct CleanupRegion {
    CleanupRegionId id;
    QVector<NodeId> entryNodes;
    QVector<NodeId> exitNodes;
    QVector<CleanupReason> triggers;
    bool bestEffort = true;
};

struct ForLoopSpec {
    QString variableName = "i";
    int from = 0;
    int to = 0;
    int step = 1;
};

struct LoopRegion {
    LoopId id;
    NodeId controllerNodeId;
    QVector<NodeId> bodyNodes;
    QVector<NodeId> childNodeIds;
    QVector<NodeId> entryNodes;
    QVector<NodeId> exitNodes;
    ForLoopSpec forLoop;
};

struct TestItemRegion {
    NodeId controllerNodeId;
    QVector<NodeId> childNodeIds;
};

struct ExecutionPlan {
    PlanId id;
    SequenceId sequenceId;
    QString sequenceVersion;
    QHash<NodeId, ExecNode> nodes;
    QVector<ExecEdge> edges;
    QVector<CleanupRegion> cleanupRegions;
    QVector<LoopRegion> loopRegions;
    QVector<TestItemRegion> testItemRegions;
    NodeId entryNodeId;
    NodeId exitNodeId;

    bool addNode(const ExecNode& node);
    void addEdge(const ExecEdge& edge);

    const ExecNode* node(const NodeId& nodeId) const;
    QVector<ExecEdge> incomingEdges(const NodeId& nodeId) const;
    QVector<ExecEdge> outgoingEdges(const NodeId& nodeId) const;
    std::optional<CleanupRegion> cleanupRegion(const CleanupRegionId& id) const;
    std::optional<LoopRegion> loopRegionForController(const NodeId& nodeId) const;
    std::optional<LoopRegion> loopRegionForBodyNode(const NodeId& nodeId) const;
    std::optional<TestItemRegion> testItemRegionForController(const NodeId& nodeId) const;
    std::optional<TestItemRegion> testItemRegionForChild(const NodeId& nodeId) const;
    std::optional<NodeId> structuralParentOf(const NodeId& nodeId) const;
    bool isInsideTestItem(const NodeId& nodeId) const;
};

bool isTerminalOutcome(NodeOutcome outcome);
bool triggerMatchesOutcome(EdgeTrigger trigger, NodeOutcome outcome);
QString nodeOutcomeName(NodeOutcome outcome);

} // namespace PicoATE::Core
