#include "world/NavMesh.h"
#include "core/Log.h"
#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourCrowd.h>
#include <DetourCommon.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace sk {

namespace {
struct QuietCtx : rcContext { QuietCtx() : rcContext(false) {} };
inline void toDt(glm::vec3 v, float* o) { o[0] = v.x; o[1] = v.y; o[2] = v.z; }
inline glm::vec3 fromDt(const float* v) { return {v[0], v[1], v[2]}; }
}

NavMesh::~NavMesh() {
    if (crowd_) dtFreeCrowd(crowd_);
    if (query_) dtFreeNavMeshQuery(query_);
    if (nav_) dtFreeNavMesh(nav_);
    delete filter_;
}

bool NavMesh::build(const World& world, const std::vector<NavSlowZone>& slowZones, const NavBuildSettings& s) {
    auto t0 = std::chrono::steady_clock::now();
    settings_ = s;
    // ---- geometry: ground quad + every collider
    std::vector<float> verts; std::vector<int> tris; std::vector<unsigned char> triArea;
    auto addTris = [&](size_t firstTri, bool walkable) { for (size_t i = firstTri; i < tris.size() / 3; ++i) triArea.push_back(walkable ? RC_WALKABLE_AREA : RC_NULL_AREA); };
    if (world.hasGround) {
        float e = world.groundExtent, y = world.groundY;
        float gv[12] = {-e, y, -e, e, y, -e, e, y, e, -e, y, e};
        verts.insert(verts.end(), gv, gv + 12);
        int gt[6] = {0, 2, 1, 0, 3, 2};     // CCW seen from above
        tris.insert(tris.end(), gt, gt + 6); addTris(0, true);
    }
    for (const Collider& c : world.colliders()) { size_t first = tris.size() / 3; appendBoxTriangles(c, verts, tris); addTris(first, c.walkable); }
    if (verts.empty()) return false;
    const int nverts = (int)verts.size() / 3, ntris = (int)tris.size() / 3;

    float bmin[3] = {1e30f, 1e30f, 1e30f}, bmax[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < nverts; ++i) for (int k = 0; k < 3; ++k) { bmin[k] = std::min(bmin[k], verts[i * 3 + k]); bmax[k] = std::max(bmax[k], verts[i * 3 + k]); }

    rcConfig cfg{};
    cfg.cs = s.cellSize; cfg.ch = s.cellHeight;
    cfg.walkableSlopeAngle = s.agentMaxSlope;
    cfg.walkableHeight = (int)std::ceil(s.agentHeight / cfg.ch);
    cfg.walkableClimb = (int)std::floor(s.agentMaxClimb / cfg.ch);
    cfg.walkableRadius = (int)std::ceil(s.agentRadius / cfg.cs);
    cfg.maxEdgeLen = (int)(s.edgeMaxLen / cfg.cs);
    cfg.maxSimplificationError = s.edgeMaxError;
    cfg.minRegionArea = (int)rcSqr(s.regionMinSize);
    cfg.mergeRegionArea = (int)rcSqr(s.regionMergeSize);
    cfg.maxVertsPerPoly = s.vertsPerPoly;
    cfg.detailSampleDist = s.detailSampleDist < 0.9f ? 0.f : cfg.cs * s.detailSampleDist;
    cfg.detailSampleMaxError = cfg.ch * s.detailSampleMaxError;
    rcVcopy(cfg.bmin, bmin); rcVcopy(cfg.bmax, bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    QuietCtx ctx;
    rcHeightfield* hf = rcAllocHeightfield();
    if (!hf || !rcCreateHeightfield(&ctx, *hf, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) { SK_ERR("nav: heightfield"); return false; }
    // Colliders mark their own area: slope-walkable triangles keep RC_WALKABLE_AREA only if the collider is walkable.
    std::vector<unsigned char> areas((size_t)ntris, RC_NULL_AREA);
    rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts.data(), nverts, tris.data(), ntris, areas.data());
    for (int i = 0; i < ntris; ++i) if (triArea[i] == RC_NULL_AREA) areas[i] = RC_NULL_AREA;     // non walkable colliders still block
    rcRasterizeTriangles(&ctx, verts.data(), nverts, tris.data(), areas.data(), ntris, *hf, cfg.walkableClimb);
    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *hf);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *hf);

    rcCompactHeightfield* chf = rcAllocCompactHeightfield();
    bool ok = chf && rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf, *chf);
    rcFreeHeightField(hf);
    if (!ok) { SK_ERR("nav: compact heightfield"); rcFreeCompactHeightfield(chf); return false; }
    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf)) { rcFreeCompactHeightfield(chf); return false; }
    for (const NavSlowZone& z : slowZones) {      // rcMarkConvexPolyArea: Detour area cost makes the crowd route around these
        float pv[12] = {z.center.x - z.half.x, 0, z.center.z - z.half.y, z.center.x + z.half.x, 0, z.center.z - z.half.y,
                        z.center.x + z.half.x, 0, z.center.z + z.half.y, z.center.x - z.half.x, 0, z.center.z + z.half.y};
        rcMarkConvexPolyArea(&ctx, pv, 4, bmin[1] - 1.f, bmax[1] + 1.f, NAV_AREA_SLOW, *chf);
    }
    if (!rcBuildDistanceField(&ctx, *chf) || !rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea)) { SK_ERR("nav: regions"); rcFreeCompactHeightfield(chf); return false; }
    rcContourSet* cset = rcAllocContourSet();
    if (!rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset)) { SK_ERR("nav: contours"); rcFreeCompactHeightfield(chf); rcFreeContourSet(cset); return false; }
    rcPolyMesh* pmesh = rcAllocPolyMesh();
    if (!rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh)) { SK_ERR("nav: polymesh"); rcFreeCompactHeightfield(chf); rcFreeContourSet(cset); rcFreePolyMesh(pmesh); return false; }
    rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
    if (!rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh)) { SK_ERR("nav: detail mesh"); }
    rcFreeCompactHeightfield(chf); rcFreeContourSet(cset);

    for (int i = 0; i < pmesh->npolys; ++i) {
        if (pmesh->areas[i] == RC_WALKABLE_AREA) pmesh->areas[i] = NAV_AREA_GROUND;
        pmesh->flags[i] = pmesh->areas[i] == NAV_AREA_SLOW ? (NAV_FLAG_WALK | NAV_FLAG_SLOW) : NAV_FLAG_WALK;
    }
    dtNavMeshCreateParams p{};
    p.verts = pmesh->verts; p.vertCount = pmesh->nverts; p.polys = pmesh->polys; p.polyAreas = pmesh->areas; p.polyFlags = pmesh->flags; p.polyCount = pmesh->npolys; p.nvp = pmesh->nvp;
    p.detailMeshes = dmesh->meshes; p.detailVerts = dmesh->verts; p.detailVertsCount = dmesh->nverts; p.detailTris = dmesh->tris; p.detailTriCount = dmesh->ntris;
    p.walkableHeight = s.agentHeight; p.walkableRadius = s.agentRadius; p.walkableClimb = s.agentMaxClimb;
    rcVcopy(p.bmin, pmesh->bmin); rcVcopy(p.bmax, pmesh->bmax); p.cs = cfg.cs; p.ch = cfg.ch; p.buildBvTree = true;
    polyCount_ = pmesh->npolys; vertCount_ = pmesh->nverts; detailTris_ = dmesh->ntris;
    unsigned char* data = nullptr; int dataSize = 0;
    bool created = dtCreateNavMeshData(&p, &data, &dataSize);
    rcFreePolyMesh(pmesh); rcFreePolyMeshDetail(dmesh);
    if (!created) { SK_ERR("nav: dtCreateNavMeshData failed"); return false; }
    nav_ = dtAllocNavMesh();
    if (!nav_ || dtStatusFailed(nav_->init(data, dataSize, DT_TILE_FREE_DATA))) { SK_ERR("nav: dtNavMesh init"); dtFree(data); return false; }
    query_ = dtAllocNavMeshQuery();
    if (dtStatusFailed(query_->init(nav_, 2048))) { SK_ERR("nav: query init"); return false; }
    filter_ = new dtQueryFilter();
    filter_->setIncludeFlags(NAV_FLAG_WALK | NAV_FLAG_SLOW); filter_->setExcludeFlags(0);
    filter_->setAreaCost(NAV_AREA_GROUND, 1.f); filter_->setAreaCost(NAV_AREA_SLOW, 6.f);
    buildMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    SK_LOG("navmesh: %d polys, %d verts, %d detail tris, built in %.0f ms (cell %.2f, agent r %.2f h %.2f climb %.2f)", polyCount_, vertCount_, detailTris_, buildMs_, s.cellSize, s.agentRadius, s.agentHeight, s.agentMaxClimb);
    return true;
}

bool NavMesh::nearestPoint(glm::vec3 p, glm::vec3& out, glm::vec3 ext) const {
    if (!query_) return false;
    float c[3], e[3], o[3]; toDt(p, c); toDt(ext, e); dtPolyRef ref = 0;
    if (dtStatusFailed(query_->findNearestPoly(c, e, filter_, &ref, o)) || !ref) return false;
    out = fromDt(o); return true;
}

bool NavMesh::findPath(glm::vec3 a, glm::vec3 b, std::vector<glm::vec3>& straight) const {
    straight.clear(); if (!query_) return false;
    float pa[3], pb[3], e[3] = {2.f, 3.f, 2.f}, na[3], nb[3]; toDt(a, pa); toDt(b, pb);
    dtPolyRef ra = 0, rb = 0;
    query_->findNearestPoly(pa, e, filter_, &ra, na); query_->findNearestPoly(pb, e, filter_, &rb, nb);
    if (!ra || !rb) return false;
    dtPolyRef path[256]; int n = 0;
    query_->findPath(ra, rb, na, nb, filter_, path, &n, 256);
    if (n <= 0) return false;
    float sp[256 * 3]; unsigned char fl[256]; dtPolyRef pr[256]; int ns = 0;
    query_->findStraightPath(na, nb, path, n, sp, fl, pr, &ns, 256);
    for (int i = 0; i < ns; ++i) straight.push_back(fromDt(&sp[i * 3]));
    return ns > 0;
}

bool NavMesh::randomPointAround(glm::vec3 center, float radius, glm::vec3& out) const {
    if (!query_) return false;
    float c[3], e[3] = {2.f, 3.f, 2.f}, n[3], o[3]; toDt(center, c); dtPolyRef ref = 0;
    query_->findNearestPoly(c, e, filter_, &ref, n); if (!ref) return false;
    dtPolyRef r2 = 0;
    auto rnd = []() { static unsigned s = 1234567u; s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.f; };
    if (dtStatusFailed(query_->findRandomPointAroundCircle(ref, n, radius, filter_, rnd, &r2, o))) return false;
    out = fromDt(o); return true;
}

bool NavMesh::walkable(glm::vec3 a, glm::vec3 b, float* frac) const {
    if (!query_) return true;
    float pa[3], pb[3], e[3] = {1.f, 3.f, 1.f}, na[3]; toDt(a, pa); toDt(b, pb);
    dtPolyRef ra = 0; query_->findNearestPoly(pa, e, filter_, &ra, na); if (!ra) return false;
    dtRaycastHit hit{}; dtPolyRef path[32]; hit.path = path; hit.maxPath = 32;
    if (dtStatusFailed(query_->raycast(ra, na, pb, filter_, 0, &hit))) return false;
    if (frac) *frac = hit.t > 1.f ? 1.f : hit.t;
    return hit.t > 1.f;      // FLT_MAX when the segment reaches the end
}

float NavMesh::distanceToWall(glm::vec3 p, glm::vec3* wallPoint) const {
    if (!query_) return 1e9f;
    float c[3], e[3] = {1.f, 3.f, 1.f}, n[3]; toDt(p, c); dtPolyRef ref = 0;
    query_->findNearestPoly(c, e, filter_, &ref, n); if (!ref) return 1e9f;
    float d = 0, hp[3], hn[3];
    if (dtStatusFailed(query_->findDistanceToWall(ref, n, 5.f, filter_, &d, hp, hn))) return 1e9f;
    if (wallPoint) *wallPoint = fromDt(hp);
    return d;
}

// ------------------------------------------------------------------------------------------------ crowd
void NavMesh::initCrowd(int maxAgents, float maxRadius) {
    if (!nav_) return;
    if (crowd_) dtFreeCrowd(crowd_);
    crowd_ = dtAllocCrowd();
    crowd_->init(maxAgents, maxRadius, nav_);
    dtObstacleAvoidanceParams oa{};
    std::memcpy(&oa, crowd_->getObstacleAvoidanceParams(0), sizeof(oa));
    oa.velBias = 0.4f; oa.weightDesVel = 2.f; oa.weightCurVel = 0.75f; oa.weightSide = 0.75f; oa.weightToi = 2.5f;
    oa.horizTime = 2.5f; oa.gridSize = 33; oa.adaptiveDivs = 7; oa.adaptiveRings = 2; oa.adaptiveDepth = 5;
    crowd_->setObstacleAvoidanceParams(0, &oa);
    crowd_->getEditableFilter(0)->setAreaCost(NAV_AREA_SLOW, 6.f);
}

int NavMesh::addAgent(glm::vec3 pos, float radius, float height, float maxSpeed, float maxAccel) {
    if (!crowd_) return -1;
    dtCrowdAgentParams ap{};
    ap.radius = radius; ap.height = height; ap.maxAcceleration = maxAccel; ap.maxSpeed = maxSpeed;
    ap.collisionQueryRange = radius * 10.f; ap.pathOptimizationRange = radius * 30.f; ap.separationWeight = 2.f;
    ap.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION;
    ap.obstacleAvoidanceType = 0; ap.queryFilterType = 0;
    float p[3]; toDt(pos, p);
    return crowd_->addAgent(p, &ap);
}
void NavMesh::removeAgent(int id) { if (crowd_ && id >= 0) crowd_->removeAgent(id); }

void NavMesh::setTarget(int id, glm::vec3 t) {
    if (!crowd_ || !query_ || id < 0) return;
    float c[3], e[3] = {2.f, 3.f, 2.f}, n[3]; toDt(t, c); dtPolyRef ref = 0;
    query_->findNearestPoly(c, e, crowd_->getFilter(0), &ref, n);
    if (ref) crowd_->requestMoveTarget(id, ref, n);
}
void NavMesh::clearTarget(int id) { if (crowd_ && id >= 0) crowd_->resetMoveTarget(id); }

void NavMesh::syncAgent(int id, glm::vec3 pos, glm::vec3 vel) {
    if (!crowd_ || id < 0) return;
    dtCrowdAgent* ag = crowd_->getEditableAgent(id); if (!ag || !ag->active) return;
    toDt(pos, ag->npos); toDt(vel, ag->vel);
}
void NavMesh::setMaxSpeed(int id, float v) {
    if (!crowd_ || id < 0) return;
    const dtCrowdAgent* ag = crowd_->getAgent(id); if (!ag) return;
    dtCrowdAgentParams p = ag->params; p.maxSpeed = v; crowd_->updateAgentParameters(id, &p);
}
void NavMesh::updateCrowd(float dt) { if (crowd_) crowd_->update(dt, nullptr); }

CrowdAgentInfo NavMesh::agent(int id) const {
    CrowdAgentInfo i;
    if (!crowd_ || id < 0) return i;
    const dtCrowdAgent* ag = crowd_->getAgent(id); if (!ag || !ag->active) return i;
    i.active = true; i.pos = fromDt(ag->npos); i.vel = fromDt(ag->vel); i.desiredVel = fromDt(ag->dvel); i.newVel = fromDt(ag->nvel);
    i.state = ag->state; i.targetState = ag->targetState; i.hasTarget = ag->targetState != DT_CROWDAGENT_TARGET_NONE; i.target = fromDt(ag->targetPos);
    for (int k = 0; k < ag->ncorners; ++k) i.corners.push_back(fromDt(&ag->cornerVerts[k * 3]));
    return i;
}

void NavMesh::debugEdges(std::vector<Edge>& out, float lift) const {
    if (!nav_) return;
    for (int t = 0; t < nav_->getMaxTiles(); ++t) {
        const dtNavMesh* cn = nav_; const dtMeshTile* tile = cn->getTile(t);
        if (!tile || !tile->header) continue;
        for (int i = 0; i < tile->header->polyCount; ++i) {
            const dtPoly& p = tile->polys[i];
            for (int j = 0; j < p.vertCount; ++j) {
                const float* a = &tile->verts[p.verts[j] * 3]; const float* b = &tile->verts[p.verts[(j + 1) % p.vertCount] * 3];
                out.push_back({glm::vec3(a[0], a[1] + lift, a[2]), glm::vec3(b[0], b[1] + lift, b[2]), p.getArea() == NAV_AREA_SLOW});
            }
        }
    }
}

}  // namespace sk
