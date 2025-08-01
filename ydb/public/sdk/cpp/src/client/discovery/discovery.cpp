#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/discovery/discovery.h>

#include <ydb/public/sdk/cpp/src/client/common_client/impl/client.h>

namespace NYdb::inline Dev {
namespace NDiscovery {

TListEndpointsResult::TListEndpointsResult(TStatus&& status, const Ydb::Discovery::ListEndpointsResult& proto)
    : TStatus(std::move(status))
{
    const auto& endpoints = proto.endpoints();
    Info_.reserve(proto.endpoints().size());
    for (const auto& endpointInfo : endpoints) {
        TEndpointInfo& info = Info_.emplace_back();
        info.Address = endpointInfo.address();
        info.Port = endpointInfo.port();
        info.Location = endpointInfo.location();
        info.NodeId = endpointInfo.node_id();
        info.LoadFactor = endpointInfo.load_factor();
        info.Ssl = endpointInfo.ssl();
        info.Services.reserve(endpointInfo.service().size());
        for (const auto& s : endpointInfo.service()) {
            info.Services.emplace_back(s);
        }
        info.IPv4Addrs.reserve(endpointInfo.ip_v4().size());
        for (const auto& addr : endpointInfo.ip_v4()) {
            info.IPv4Addrs.emplace_back(addr);
        }
        info.IPv6Addrs.reserve(endpointInfo.ip_v6().size());
        for (const auto& addr : endpointInfo.ip_v6()) {
            info.IPv6Addrs.emplace_back(addr);
        }
        info.SslTargetNameOverride = endpointInfo.ssl_target_name_override();
    }
}

const std::vector<TEndpointInfo>& TListEndpointsResult::GetEndpointsInfo() const {
    return Info_;
}

TWhoAmIResult::TWhoAmIResult(TStatus&& status, const Ydb::Discovery::WhoAmIResult& proto)
    : TStatus(std::move(status))
{
    UserName_ = proto.user();
    const auto& groups = proto.groups();
    Groups_.reserve(groups.size());
    for (const auto& group : groups) {
        Groups_.emplace_back(group);
    }
}

const std::string& TWhoAmIResult::GetUserName() const {
    return UserName_;
}

const std::vector<std::string>& TWhoAmIResult::GetGroups() const {
    return Groups_;
}

TNodeLocation::TNodeLocation(const Ydb::Discovery::NodeLocation& location)
    : DataCenterNum(location.has_data_center_num() ? std::make_optional(location.data_center_num()) : std::nullopt)
    , RoomNum(location.has_room_num() ? std::make_optional(location.room_num()) : std::nullopt)
    , RackNum(location.has_rack_num() ? std::make_optional(location.rack_num()) : std::nullopt)
    , BodyNum(location.has_body_num() ? std::make_optional(location.body_num()) : std::nullopt)
    , Body(location.has_body() ? std::make_optional(location.body()) : std::nullopt)
    , BridgePileName(location.has_bridge_pile_name() ? std::make_optional(location.bridge_pile_name()) : std::nullopt)
    , DataCenter(location.has_data_center() ? std::make_optional(location.data_center()) : std::nullopt)
    , Module(location.has_module() ? std::make_optional(location.module()) : std::nullopt)
    , Rack(location.has_rack() ? std::make_optional(location.rack()) : std::nullopt)
    , Unit(location.has_unit() ? std::make_optional(location.unit()) : std::nullopt)
{}

TNodeInfo::TNodeInfo(const Ydb::Discovery::NodeInfo& info)
    : NodeId(info.node_id())
    , Host(info.host())
    , Port(info.port())
    , ResolveHost(info.resolve_host())
    , Address(info.address())
    , Location(info.location())
    , Expire(info.expire())
{}

TNodeRegistrationResult::TNodeRegistrationResult(TStatus&& status, const Ydb::Discovery::NodeRegistrationResult& proto)
    : TStatus(std::move(status))
    , NodeId_(proto.node_id())
    , DomainPath_(proto.domain_path())
    , Expire_(proto.expire())
    , ScopeTableId_(proto.has_scope_tablet_id() ? std::make_optional(proto.scope_tablet_id()) : std::nullopt)
    , ScopePathId_(proto.has_scope_path_id() ? std::make_optional(proto.scope_path_id()) : std::nullopt)
    , NodeName_(proto.has_node_name() ? std::make_optional(proto.node_name()) : std::nullopt)
{
    const auto& nodes = proto.nodes();
    Nodes_.reserve(nodes.size());
    for (const auto& node : nodes) {
        Nodes_.emplace_back(node);
    }
}

uint32_t TNodeRegistrationResult::GetNodeId() const {
    return NodeId_;
}

const std::string& TNodeRegistrationResult::GetDomainPath() const {
    return DomainPath_;
}

uint64_t TNodeRegistrationResult::GetExpire() const {
    return Expire_;
}

uint64_t TNodeRegistrationResult::GetScopeTabletId() const {
    return ScopeTableId_.value();
}

bool TNodeRegistrationResult::HasScopeTabletId() const {
    return ScopeTableId_.has_value();
}

uint64_t TNodeRegistrationResult::GetScopePathId() const {
    return ScopePathId_.value();
}

bool TNodeRegistrationResult::HasScopePathId() const {
    return ScopePathId_.has_value();
}

bool TNodeRegistrationResult::HasNodeName() const {
    return NodeName_.has_value();
}

const std::string& TNodeRegistrationResult::GetNodeName() const {
    return NodeName_.value();
}

const std::vector<TNodeInfo>& TNodeRegistrationResult::GetNodes() const {
    return Nodes_;
}

class TDiscoveryClient::TImpl : public TClientImplCommon<TDiscoveryClient::TImpl> {
public:
    TImpl(std::shared_ptr<TGRpcConnectionsImpl>&& connections, const TCommonClientSettings& settings)
        : TClientImplCommon(std::move(connections), settings)
    { }

    TAsyncListEndpointsResult ListEndpoints(const TListEndpointsSettings& settings) {
        Ydb::Discovery::ListEndpointsRequest request;
        request.set_database(TStringType{DbDriverState_->Database});

        auto promise = NThreading::NewPromise<TListEndpointsResult>();

        auto extractor = [promise]
            (google::protobuf::Any* any, TPlainStatus status) mutable {
                Ydb::Discovery::ListEndpointsResult result;
                if (any) {
                    any->UnpackTo(&result);
                }
                TListEndpointsResult val{TStatus(std::move(status)), result};
                promise.SetValue(std::move(val));
            };

        Connections_->RunDeferred<Ydb::Discovery::V1::DiscoveryService, Ydb::Discovery::ListEndpointsRequest, Ydb::Discovery::ListEndpointsResponse>(
            std::move(request),
            extractor,
            &Ydb::Discovery::V1::DiscoveryService::Stub::AsyncListEndpoints,
            DbDriverState_,
            INITIAL_DEFERRED_CALL_DELAY,
            TRpcRequestSettings::Make(settings));

        return promise.GetFuture();
    }

    TAsyncWhoAmIResult WhoAmI(const TWhoAmISettings& settings) {
        Ydb::Discovery::WhoAmIRequest request;
        if (settings.WithGroups_) {
            request.set_include_groups(true);
        }

        auto promise = NThreading::NewPromise<TWhoAmIResult>();

        auto extractor = [promise]
        (google::protobuf::Any* any, TPlainStatus status) mutable {
            Ydb::Discovery::WhoAmIResult result;
            if (any) {
                any->UnpackTo(&result);
            }
            TWhoAmIResult val{ TStatus(std::move(status)), result };
            promise.SetValue(std::move(val));
        };

        Connections_->RunDeferred<Ydb::Discovery::V1::DiscoveryService, Ydb::Discovery::WhoAmIRequest, Ydb::Discovery::WhoAmIResponse>(
            std::move(request),
            extractor,
            &Ydb::Discovery::V1::DiscoveryService::Stub::AsyncWhoAmI,
            DbDriverState_,
            INITIAL_DEFERRED_CALL_DELAY,
            TRpcRequestSettings::Make(settings));

        return promise.GetFuture();
    }

    TAsyncNodeRegistrationResult NodeRegistration(const TNodeRegistrationSettings& settings) {
        Ydb::Discovery::NodeRegistrationRequest request;
        request.set_host(TStringType{settings.Host_});
        request.set_port(settings.Port_);
        request.set_resolve_host(TStringType{settings.ResolveHost_});
        request.set_address(TStringType{settings.Address_});
        request.set_domain_path(TStringType{settings.DomainPath_});
        request.set_fixed_node_id(settings.FixedNodeId_);
        if (!settings.Path_.empty()) {
            request.set_path(TStringType{settings.Path_});
        }

        auto requestLocation = request.mutable_location();
        const auto& location = settings.Location_;

        if (location.BridgePileName) {
            requestLocation->set_bridge_pile_name(TStringType{location.BridgePileName.value()});
        }
        if (location.DataCenter) {
            requestLocation->set_data_center(TStringType{location.DataCenter.value()});
        }
        if (location.Module) {
            requestLocation->set_module(TStringType{location.Module.value()});
        }
        if (location.Rack) {
            requestLocation->set_rack(TStringType{location.Rack.value()});
        }
        if (location.Unit) {
            requestLocation->set_unit(TStringType{location.Unit.value()});
        }

        if (location.DataCenterNum) {
            requestLocation->set_data_center_num(location.DataCenterNum.value());
        }
        if (location.RoomNum) {
            requestLocation->set_room_num(location.RoomNum.value());
        }
        if (location.RackNum) {
            requestLocation->set_rack_num(location.RackNum.value());
        }
        if (location.BodyNum) {
            requestLocation->set_body_num(location.BodyNum.value());
        }
        if (location.Body) {
            requestLocation->set_body(location.Body.value());
        }

        auto promise = NThreading::NewPromise<TNodeRegistrationResult>();

        auto extractor = [promise] (google::protobuf::Any* any, TPlainStatus status) mutable {
            Ydb::Discovery::NodeRegistrationResult result;
            if (any) {
                any->UnpackTo(&result);
            }
            TNodeRegistrationResult val{TStatus(std::move(status)), result};
            promise.SetValue(std::move(val));
        };

        Connections_->RunDeferred<Ydb::Discovery::V1::DiscoveryService, Ydb::Discovery::NodeRegistrationRequest, Ydb::Discovery::NodeRegistrationResponse>(
            std::move(request),
            extractor,
            &Ydb::Discovery::V1::DiscoveryService::Stub::AsyncNodeRegistration,
            DbDriverState_,
            INITIAL_DEFERRED_CALL_DELAY,
            TRpcRequestSettings::Make(settings));

        return promise.GetFuture();
    }
};

TDiscoveryClient::TDiscoveryClient(const TDriver& driver, const TCommonClientSettings& settings)
    : Impl_(new TImpl(CreateInternalInterface(driver), settings))
{ }

TAsyncListEndpointsResult TDiscoveryClient::ListEndpoints(const TListEndpointsSettings& settings) {
    return Impl_->ListEndpoints(settings);
}

TAsyncWhoAmIResult TDiscoveryClient::WhoAmI(const TWhoAmISettings& settings) {
    return Impl_->WhoAmI(settings);
}

TAsyncNodeRegistrationResult TDiscoveryClient::NodeRegistration(const TNodeRegistrationSettings& settings) {
    return Impl_->NodeRegistration(settings);
}

} // namespace NDiscovery
} // namespace NYdb
