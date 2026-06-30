#include "BspFilePanelSource.hpp"

#include <cctype>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "Command.hpp"
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"
#include "Modules/FileSystem.hpp"

namespace {

// Source BSP v21 on-disk layout (subset). Field order matches public/bspfile.h;
// the struct sizes were cross-checked against a v21 map's lump lengths
// (face 56, texinfo 72, texdata 32, plane 20, edge 4).
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
#pragma pack(pop)

enum {
  LUMP_PLANES = 1,
  LUMP_TEXDATA = 2,
  LUMP_VERTEXES = 3,
  LUMP_TEXINFO = 6,
  LUMP_FACES = 7,
  LUMP_EDGES = 12,
  LUMP_SURFEDGES = 13,
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

}  // namespace

std::vector<PanelDesc> BspFilePanelSource::EnumeratePanels(
    const std::string& mapName) {
  (void)mapName;
  return {};  // clustering lands in a later step
}

CON_COMMAND(sar_harness_bsp_geo_dump,
            "sar_harness_bsp_geo_dump - parse the current map's .bsp and print "
            "the geometry lump counts plus the portalable white-tile faces "
            "(material/plane/centroid). Read-only.\n") {
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

  int n = 0, shown = 0;
  for (const auto& f : bsp.faces) {
    FaceGeo g;
    if (!ExtractFace(bsp, f, &g) || !IsPortalable(g)) continue;
    ++n;
    if (shown < 24 && !g.verts.empty()) {
      Vector c{0, 0, 0};
      for (const auto& v : g.verts) {
        c.x += v.x;
        c.y += v.y;
        c.z += v.z;
      }
      float inv = 1.0f / g.verts.size();
      console->Msg(
          "    %-28s n %.0f %.0f %.0f  d %.0f  verts %d  c %.0f %.0f %.0f\n",
          g.material.c_str(), g.normal.x, g.normal.y, g.normal.z, g.dist,
          (int)g.verts.size(), c.x * inv, c.y * inv, c.z * inv);
      ++shown;
    }
  }
  console->Print("bsp geo dump: %d portalable white-tile faces (showed %d).\n",
                 n, shown);
}
