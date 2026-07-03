#include "BspFilePanelSource.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Command.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/FileSystem.hpp"

namespace {

const float kTile = 128.0f;

// Source BSP v21 on-disk lump structs; layout must match the file
// byte-for-byte.
#pragma pack(push, 1)
struct Plane {
  Vector normal;
  float dist;
  int type;
};
struct Edge {
  unsigned short v[2];
};
struct Face {
  unsigned short planenum;
  unsigned char side;
  unsigned char onNode;
  int firstedge;
  short numedges;
  short texinfo;
  short dispinfo;
  short surfaceFogVolumeID;
  unsigned char styles[4];
  int lightofs;
  float area;
  int lightmapMins[2];
  int lightmapSize[2];
  int origFace;
  unsigned short numPrims;
  unsigned short firstPrimID;
  unsigned int smoothingGroups;
};
struct TexInfo {
  float textureVecs[2][4];
  float lightmapVecs[2][4];
  int flags;
  int texdata;
};
struct TexData {
  Vector reflectivity;
  int nameStringTableID;
  int width, height;
  int viewWidth, viewHeight;
};
struct Model {
  Vector mins, maxs;
  Vector origin;
  int headnode;
  int firstface, numfaces;
};
#pragma pack(pop)

enum {
  LUMP_ENTITIES = 0,
  LUMP_PLANES = 1,
  LUMP_TEXDATA = 2,
  LUMP_VERTEXES = 3,
  LUMP_TEXINFO = 6,
  LUMP_FACES = 7,
  LUMP_EDGES = 12,
  LUMP_SURFEDGES = 13,
  LUMP_MODELS = 14,
  LUMP_TEXDATA_STRING_DATA = 43,
  LUMP_TEXDATA_STRING_TABLE = 44,
};

const int kSurfNoPortal = 0x0020;

struct BspFile {
  std::vector<Plane> planes;
  std::vector<Vector> verts;
  std::vector<Edge> edges;
  std::vector<int> surfedges;
  std::vector<Face> faces;
  std::vector<TexInfo> texinfos;
  std::vector<TexData> texdatas;
  std::vector<int> stringTable;
  std::vector<char> stringData;
  std::vector<Model> models;
  std::vector<char> entText;
  bool ok = false;
};

template <typename T>
void ReadLump(std::ifstream& f, int ofs, int len, std::vector<T>& out) {
  out.resize(len / sizeof(T));
  if (out.empty()) return;
  f.seekg(ofs, std::ios::beg);
  f.read((char*)out.data(), out.size() * sizeof(T));
}

BspFile LoadBsp(const std::string& path) {
  BspFile bsp;
  std::ifstream f(path, std::ios::binary);
  if (!f) return bsp;

  int ident = 0, version = 0;
  f.read((char*)&ident, 4);
  f.read((char*)&version, 4);
  if (ident != 0x50534256) return bsp;  // 'VBSP'

  struct Lump {
    int ofs, len, ver, fourCC;
  };
  auto lump = [&](int idx) {
    Lump l{};
    f.seekg(8 + idx * 16, std::ios::beg);
    f.read((char*)&l, sizeof(l));
    return l;
  };

  Lump l;
  l = lump(LUMP_PLANES);
  ReadLump(f, l.ofs, l.len, bsp.planes);
  l = lump(LUMP_VERTEXES);
  ReadLump(f, l.ofs, l.len, bsp.verts);
  l = lump(LUMP_EDGES);
  ReadLump(f, l.ofs, l.len, bsp.edges);
  l = lump(LUMP_SURFEDGES);
  ReadLump(f, l.ofs, l.len, bsp.surfedges);
  l = lump(LUMP_FACES);
  ReadLump(f, l.ofs, l.len, bsp.faces);
  l = lump(LUMP_TEXINFO);
  ReadLump(f, l.ofs, l.len, bsp.texinfos);
  l = lump(LUMP_TEXDATA);
  ReadLump(f, l.ofs, l.len, bsp.texdatas);
  l = lump(LUMP_TEXDATA_STRING_TABLE);
  ReadLump(f, l.ofs, l.len, bsp.stringTable);
  l = lump(LUMP_TEXDATA_STRING_DATA);
  ReadLump(f, l.ofs, l.len, bsp.stringData);
  l = lump(LUMP_MODELS);
  ReadLump(f, l.ofs, l.len, bsp.models);
  l = lump(LUMP_ENTITIES);
  ReadLump(f, l.ofs, l.len, bsp.entText);

  bsp.ok = !bsp.faces.empty() && !bsp.planes.empty();
  return bsp;
}

bool LoadByMap(const std::string& map, BspFile* out) {
  if (!fileSystem || map.empty()) return false;
  std::string path =
      fileSystem->FindFileSomewhere("maps/" + map + ".bsp").value_or("");
  if (path.empty()) return false;
  *out = LoadBsp(path);
  return out->ok;
}

struct FaceGeo {
  Vector normal;
  float dist;
  std::vector<Vector> verts;
  std::string material;
  int flags;
};

bool ExtractFace(const BspFile& bsp, const Face& f, FaceGeo* out) {
  if (f.planenum >= bsp.planes.size()) return false;
  out->normal = bsp.planes[f.planenum].normal;
  out->dist = bsp.planes[f.planenum].dist;

  out->verts.clear();
  for (int i = 0; i < f.numedges; ++i) {
    int seIdx = f.firstedge + i;
    if (seIdx < 0 || seIdx >= (int)bsp.surfedges.size()) return false;
    int se = bsp.surfedges[seIdx];
    int eIdx = se >= 0 ? se : -se;
    if (eIdx < 0 || eIdx >= (int)bsp.edges.size()) return false;
    int vi = se >= 0 ? bsp.edges[eIdx].v[0] : bsp.edges[eIdx].v[1];
    if (vi < 0 || vi >= (int)bsp.verts.size()) return false;
    out->verts.push_back(bsp.verts[vi]);
  }

  out->flags = 0;
  out->material.clear();
  if (f.texinfo >= 0 && f.texinfo < (int)bsp.texinfos.size()) {
    const TexInfo& ti = bsp.texinfos[f.texinfo];
    out->flags = ti.flags;
    if (ti.texdata >= 0 && ti.texdata < (int)bsp.texdatas.size()) {
      int nameId = bsp.texdatas[ti.texdata].nameStringTableID;
      if (nameId >= 0 && nameId < (int)bsp.stringTable.size()) {
        int off = bsp.stringTable[nameId];
        if (off >= 0 && off < (int)bsp.stringData.size())
          out->material = &bsp.stringData[off];
      }
    }
  }
  return true;
}

bool IsWhiteTile(const std::string& mat) {
  std::string m = mat;
  for (auto& c : m) c = (char)std::tolower((unsigned char)c);
  return m.find("white") != std::string::npos &&
         m.find("tile") != std::string::npos;
}

bool IsPortalable(const FaceGeo& g) {
  return !(g.flags & kSurfNoPortal) && IsWhiteTile(g.material);
}

// In-plane basis: an arbitrary but deterministic (u, v) spanning the plane.
void PlaneAxes(Vector n, Vector* u, Vector* v) {
  Vector up = (n.z < 0.9f && n.z > -0.9f) ? Vector{0, 0, 1} : Vector{1, 0, 0};
  *u = n.Cross(up).Normalize();
  *v = n.Cross(*u).Normalize();
}

Vector PlanePoint(float uc, float vc, const Vector& u, const Vector& v,
                  float dist, const Vector& n) {
  return Vector{uc * u.x + vc * v.x + dist * n.x,
                uc * u.y + vc * v.y + dist * n.y,
                uc * u.z + vc * v.z + dist * n.z};
}

// The entity lump is plain text: { "key" "value" ... } blocks. Collect
// targetname + brush-model index for every entity whose model is "*N".
struct BrushEntRef {
  std::string targetname;
  int model;
};

std::vector<BrushEntRef> ParseBrushEnts(const std::vector<char>& text) {
  std::vector<BrushEntRef> out;
  size_t i = 0, n = text.size();
  auto quoted = [&](std::string* s) {
    while (i < n && text[i] != '"' && text[i] != '}') ++i;
    if (i >= n || text[i] == '}') return false;
    size_t start = ++i;
    while (i < n && text[i] != '"') ++i;
    if (i >= n) return false;
    s->assign(&text[start], i - start);
    ++i;
    return true;
  };
  while (i < n) {
    if (text[i++] != '{') continue;
    std::string targetname, model, key, val;
    while (quoted(&key) && quoted(&val)) {
      if (key == "targetname")
        targetname = val;
      else if (key == "model")
        model = val;
    }
    if (model.size() > 1 && model[0] == '*')
      out.push_back({targetname, std::atoi(model.c_str() + 1)});
  }
  return out;
}

// Every portalable face on a brush-entity model, as an entity-local rest rect.
// One face is one panel -- no clustering. Faces reference the shared plane
// array; side != 0 means the face looks opposite the plane normal.
std::vector<DynamicPanelRest> ExtractDynamicRests(const BspFile& bsp) {
  std::vector<DynamicPanelRest> out;
  for (const auto& e : ParseBrushEnts(bsp.entText)) {
    if (e.model <= 0 || e.model >= (int)bsp.models.size()) continue;
    const Model& m = bsp.models[e.model];
    for (int fi = m.firstface;
         fi >= 0 && fi < m.firstface + m.numfaces && fi < (int)bsp.faces.size();
         ++fi) {
      FaceGeo g;
      if (!ExtractFace(bsp, bsp.faces[fi], &g) || !IsPortalable(g)) continue;
      // Outward normal from the winding (faces wind clockwise seen from the
      // front); the shared-plane normal + side bit misorients some
      // brush-model faces.
      Vector nw{0, 0, 0};
      for (size_t k = 0; k < g.verts.size(); ++k)
        nw = nw + g.verts[k].Cross(g.verts[(k + 1) % g.verts.size()]);
      if (nw.Length() < 1e-3f) continue;
      g.normal = (nw * -1.0f).Normalize();

      Vector u, v;
      PlaneAxes(g.normal, &u, &v);
      float umin = 1e30f, umax = -1e30f, vmin = 1e30f, vmax = -1e30f;
      for (const auto& p : g.verts) {
        umin = std::min(umin, p.Dot(u));
        umax = std::max(umax, p.Dot(u));
        vmin = std::min(vmin, p.Dot(v));
        vmax = std::max(vmax, p.Dot(v));
      }
      float dist = g.verts.empty() ? 0.0f : g.verts[0].Dot(g.normal);

      DynamicPanelRest r;
      r.targetname = e.targetname;
      r.normal = g.normal;
      r.corners[0] = PlanePoint(umin, vmin, u, v, dist, g.normal);
      r.corners[1] = PlanePoint(umax, vmin, u, v, dist, g.normal);
      r.corners[2] = PlanePoint(umax, vmax, u, v, dist, g.normal);
      r.corners[3] = PlanePoint(umin, vmax, u, v, dist, g.normal);
      out.push_back(std::move(r));
    }
  }
  // Deterministic order; stable so a multi-face entity keeps its face order.
  std::stable_sort(out.begin(), out.end(),
                   [](const DynamicPanelRest& a, const DynamicPanelRest& b) {
                     return a.targetname < b.targetname;
                   });
  return out;
}

using Cell = std::pair<int, int>;

std::vector<std::vector<Cell>> ConnectedComponents(
    const std::set<Cell>& cells) {
  std::set<Cell> seen;
  std::vector<std::vector<Cell>> comps;
  for (const auto& start : cells) {
    if (seen.count(start)) continue;
    std::vector<Cell> blob;
    std::vector<Cell> stack{start};
    while (!stack.empty()) {
      Cell c = stack.back();
      stack.pop_back();
      if (seen.count(c)) continue;
      seen.insert(c);
      blob.push_back(c);
      for (Cell nb : {Cell{c.first + 1, c.second}, Cell{c.first - 1, c.second},
                      Cell{c.first, c.second + 1}, Cell{c.first, c.second - 1}})
        if (cells.count(nb) && !seen.count(nb)) stack.push_back(nb);
    }
    comps.push_back(std::move(blob));
  }
  return comps;
}

// Quantize portalable white-tile faces to 128u cells on their plane, connect
// adjacent cells into panels, and number them deterministically (plane key then
// min cell).
std::vector<PanelDesc> ClusterPanels(const BspFile& bsp) {
  struct Group {
    Vector normal;
    float dist;
    std::vector<FaceGeo> faces;
  };
  std::map<std::string, Group> byPlane;
  for (const auto& f : bsp.faces) {
    FaceGeo g;
    if (!ExtractFace(bsp, f, &g) || !IsPortalable(g)) continue;
    char key[64];
    std::snprintf(key, sizeof(key), "%.2f,%.2f,%.2f,%ld", g.normal.x,
                  g.normal.y, g.normal.z, std::lround(g.dist));
    Group& grp = byPlane[key];
    grp.normal = g.normal;
    grp.dist = g.dist;
    grp.faces.push_back(std::move(g));
  }

  struct Raw {
    float nx, ny, nz;
    long distR;
    int mincu, mincv;
    PanelDesc desc;
  };
  std::vector<Raw> raw;
  for (auto& kv : byPlane) {
    Group& grp = kv.second;
    Vector u, vv;
    PlaneAxes(grp.normal, &u, &vv);

    std::set<Cell> cells;
    for (const auto& fg : grp.faces) {
      float umin = 1e30f, umax = -1e30f, vmin = 1e30f, vmax = -1e30f;
      for (const auto& p : fg.verts) {
        float du = p.Dot(u), dv = p.Dot(vv);
        umin = std::min(umin, du);
        umax = std::max(umax, du);
        vmin = std::min(vmin, dv);
        vmax = std::max(vmax, dv);
      }
      int cu0 = (int)std::floor(umin / kTile);
      int cu1 = (int)std::floor((umax - 1e-3f) / kTile);
      int cv0 = (int)std::floor(vmin / kTile);
      int cv1 = (int)std::floor((vmax - 1e-3f) / kTile);
      for (int cu = cu0; cu <= cu1; ++cu)
        for (int cv = cv0; cv <= cv1; ++cv) cells.insert({cu, cv});
    }

    for (const auto& blob : ConnectedComponents(cells)) {
      int mincu = blob[0].first, maxcu = blob[0].first;
      int mincv = blob[0].second, maxcv = blob[0].second;
      for (const Cell& c : blob) {
        mincu = std::min(mincu, c.first);
        maxcu = std::max(maxcu, c.first);
        mincv = std::min(mincv, c.second);
        maxcv = std::max(maxcv, c.second);
      }
      float umin = mincu * kTile, umax = (maxcu + 1) * kTile;
      float vmin = mincv * kTile, vmax = (maxcv + 1) * kTile;
      Vector center = PlanePoint((umin + umax) / 2, (vmin + vmax) / 2, u, vv,
                                 grp.dist, grp.normal);
      // PeTI puzzlemaker origin-instance geometry clusters at (0,0,0).
      if (std::fabs(center.x) < 1.0f && std::fabs(center.y) < 1.0f &&
          std::fabs(center.z) < 1.0f)
        continue;

      Vector corners[4] = {
          PlanePoint(umin, vmin, u, vv, grp.dist, grp.normal),
          PlanePoint(umax, vmin, u, vv, grp.dist, grp.normal),
          PlanePoint(umax, vmax, u, vv, grp.dist, grp.normal),
          PlanePoint(umin, vmax, u, vv, grp.dist, grp.normal),
      };
      Vector mins = corners[0], maxs = corners[0];
      for (const Vector& c : corners) {
        mins.x = std::min(mins.x, c.x);
        mins.y = std::min(mins.y, c.y);
        mins.z = std::min(mins.z, c.z);
        maxs.x = std::max(maxs.x, c.x);
        maxs.y = std::max(maxs.y, c.y);
        maxs.z = std::max(maxs.z, c.z);
      }

      Raw r;
      r.nx = std::round(grp.normal.x * 100) / 100;
      r.ny = std::round(grp.normal.y * 100) / 100;
      r.nz = std::round(grp.normal.z * 100) / 100;
      r.distR = std::lround(grp.dist);
      r.mincu = mincu;
      r.mincv = mincv;
      r.desc = PanelDesc{
          0,    grp.normal, center,
          mins, maxs,       {corners[0], corners[1], corners[2], corners[3]},
          0};
      raw.push_back(r);
    }
  }

  std::sort(raw.begin(), raw.end(), [](const Raw& a, const Raw& b) {
    if (a.nx != b.nx) return a.nx < b.nx;
    if (a.ny != b.ny) return a.ny < b.ny;
    if (a.nz != b.nz) return a.nz < b.nz;
    if (a.distR != b.distR) return a.distR < b.distR;
    if (a.mincu != b.mincu) return a.mincu < b.mincu;
    return a.mincv < b.mincv;
  });

  std::vector<PanelDesc> panels;
  for (size_t i = 0; i < raw.size(); ++i) {
    raw[i].desc.mark = (int)i + 1;
    panels.push_back(raw[i].desc);
  }
  return panels;
}

}  // namespace

// Bilinear over the four corners, so an off-center point lands on the surface
// even for a tilted panel.
Vector ResolvePanelPoint(const PanelDesc& p, float u, float v) {
  auto lerp = [](const Vector& a, const Vector& b, float t) {
    return Vector{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t};
  };
  return lerp(lerp(p.corners[0], p.corners[1], u),
              lerp(p.corners[3], p.corners[2], u), v);
}

std::vector<PanelDesc> BspFilePanelSource::EnumeratePanels(
    const std::string& mapName) {
  BspFile bsp;
  if (!LoadByMap(mapName, &bsp)) return {};
  return ClusterPanels(bsp);
}

CON_COMMAND(sar_harness_bsp_geo_dump,
            "sar_harness_bsp_geo_dump - parse the current map's .bsp and print "
            "the geometry lump counts, the portalable white-tile face count, "
            "and the clustered panels. Read-only.\n") {
  if (!engine) {
    console->Print("bsp geo dump: no engine.\n");
    return;
  }
  BspFile bsp;
  if (!LoadByMap(engine->GetCurrentMapName(), &bsp)) {
    console->Print("bsp geo dump: couldn't load the current map's .bsp.\n");
    return;
  }
  console->Print(
      "bsp geo: %d planes  %d verts  %d edges  %d surfedges  %d faces  "
      "%d texinfos  %d texdatas\n",
      (int)bsp.planes.size(), (int)bsp.verts.size(), (int)bsp.edges.size(),
      (int)bsp.surfedges.size(), (int)bsp.faces.size(),
      (int)bsp.texinfos.size(), (int)bsp.texdatas.size());

  int n = 0;
  for (const auto& f : bsp.faces) {
    FaceGeo g;
    if (ExtractFace(bsp, f, &g) && IsPortalable(g)) ++n;
  }
  console->Print("    %d portalable white-tile faces\n", n);

  auto panels = ClusterPanels(bsp);
  for (const auto& p : panels)
    console->Msg("    S%d  center %.0f %.0f %.0f  normal %.0f %.0f %.0f\n",
                 p.mark, p.center.x, p.center.y, p.center.z, p.planeNormal.x,
                 p.planeNormal.y, p.planeNormal.z);
  console->Print("bsp geo dump: %d panels.\n", (int)panels.size());

  auto rests = ExtractDynamicRests(bsp);
  for (const auto& r : rests)
    console->Msg(
        "    dyn \"%s\"  normal %.2f %.2f %.2f  local (%.1f %.1f %.1f)..(%.1f "
        "%.1f %.1f)\n",
        r.targetname.c_str(), r.normal.x, r.normal.y, r.normal.z,
        r.corners[0].x, r.corners[0].y, r.corners[0].z, r.corners[2].x,
        r.corners[2].y, r.corners[2].z);
  console->Print("    %d dynamic (brush-entity) panel faces\n",
                 (int)rests.size());
}
