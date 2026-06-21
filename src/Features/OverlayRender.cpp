#include "OverlayRender.hpp"
#include "Modules/Client.hpp"
#include "Modules/Engine.hpp"
#include "Modules/MaterialSystem.hpp"
#include "Modules/Surface.hpp"
#include "Modules/Scheme.hpp"
#include "Event.hpp"
#include "Features/Session.hpp"
#include "Features/Timer/PauseTimer.hpp"
#include "Utils/FontAtlas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

#define FONT_HPAD 48
#define FONT_VPAD 24

RenderCallback RenderCallback::none = {
	[](ViewSetup *vs, Color &col_out, bool &nodepth_out) {
		col_out = Color{0,0,0,0};
		nodepth_out = false;
	},
};

RenderCallback RenderCallback::constant(Color col, bool nodepth) {
	return {
		[=](ViewSetup *vs, Color &col_out, bool &nodepth_out) {
			col_out = col;
		 	nodepth_out = nodepth;
		},
	};
}

RenderCallback RenderCallback::prox_fade(float min, float max, Color target, Vector point, RenderCallback base) {
	return {
		[=](ViewSetup *vs, Color &col_out, bool &nodepth_out) {
			base.cbk(vs, col_out, nodepth_out);
			float dist = (vs->origin - point).Length();
			if (dist < min) {
				col_out = target;
			} else if (dist < max) {
				float ratio = (dist - min) / (max - min);
				float r = (float)col_out.r * ratio + (float)target.r * (1-ratio);
				float g = (float)col_out.g * ratio + (float)target.g * (1-ratio);
				float b = (float)col_out.b * ratio + (float)target.b * (1-ratio);
				float a = (float)col_out.a * ratio + (float)target.a * (1-ratio);
				col_out = { (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a };
			}
		},
	};
}

RenderCallback RenderCallback::shade(Vector point, RenderCallback base) {
	return {
		[=](ViewSetup *vs, Color &col_out, bool &nodepth_out) {
			base.cbk(vs, col_out, nodepth_out);

			Color light = engine->GetLightAtPoint(point);
			float r = (float)light.r / 255.0;
			float g = (float)light.g / 255.0;
			float b = (float)light.b / 255.0;

			// Scale the numbers in a way that seems reasonable
			r *= 15.0f;
			g *= 15.0f;
			b *= 15.0f;
			if (r > 1.0f) r = 1.0f;
			if (g > 1.0f) g = 1.0f;
			if (b > 1.0f) b = 1.0f;
			r = sqrt(r);
			g = sqrt(g);
			b = sqrt(b);

			// Bring all components close to the max to make the shading subtle
			float max = fmaxf(r, fmaxf(g, b));
			r += (max - r) * 0.3f;
			g += (max - g) * 0.3f;
			b += (max - b) * 0.3f;

			// Make sure it's at least slightly lit
			if (r < 0.2f) r = 0.2f;
			if (g < 0.2f) g = 0.2f;
			if (b < 0.2f) b = 0.2f;

			// Tint!
			col_out.r *= r;
			col_out.g *= g;
			col_out.b *= b;
		},
	};
}

struct OverlayText {
	Vector pos;
	OverlayRender::TextAlign align;
	std::string text;
	Color col;
	float x_height; // in units
	bool visibility_scale; // should we scale the text size to make it more visible from afar?
	bool no_depth;
	Color bg_col;
	bool clamp_to_screen; // keep on-screen + on-top (see addText doc)
	std::vector<Vector> alts; // extra declutter candidate spots (see addText)
	float nudge_x = 0.0f;     // per-frame screen-space declutter offset, in NDC
	float nudge_y = 0.0f;
};

static std::vector<OverlayText> g_text;

struct OverlayMesh {
	RenderCallback solid;
	RenderCallback wireframe;
	Vector pos;
	int num_points_in_pos;
	std::vector<Vector> tri_verts;
	std::vector<Vector> line_verts;
};

static std::vector<OverlayMesh> g_meshes;
static size_t g_num_meshes;

// Dispatched just before RENDER
ON_EVENT(FRAME) {
	// Garbage collection - remove any unused slots
	g_meshes.resize(g_num_meshes);

	// Clear the vertex arrays for each mesh that we'll keep around
	for (size_t i = 0; i < g_num_meshes; ++i) {
		auto &m = g_meshes[i];
		m.tri_verts.clear();
		m.line_verts.clear();
		m.pos = {0,0,0};
		m.num_points_in_pos = 0;
	}

	g_num_meshes = 0;

	g_text.clear();
}

MeshId OverlayRender::createMesh(RenderCallback solid, RenderCallback wireframe) {
	MeshId id = g_num_meshes;
	if (g_num_meshes == g_meshes.size()) g_meshes.push_back({});
	g_num_meshes += 1;
	g_meshes[id].solid = solid;
	g_meshes[id].wireframe = wireframe;
	return id;
}

void OverlayRender::addTriangle(MeshId &mesh, Vector a, Vector b, Vector c, bool cull_back) {
	if (g_meshes[mesh].num_points_in_pos >= 8192) {
		mesh = OverlayRender::createMesh(g_meshes[mesh].solid, g_meshes[mesh].wireframe);
	}
	auto &vs = g_meshes[mesh].tri_verts;
	vs.insert(vs.end(), { a, b, c });
	if (!cull_back) vs.insert(vs.end(), { a, c, b });
	g_meshes[mesh].pos += (a + b + c) / 3.0;
	g_meshes[mesh].num_points_in_pos += 1;
}

void OverlayRender::addLine(MeshId &mesh, Vector a, Vector b) {
	if (g_meshes[mesh].num_points_in_pos >= 8192) {
		mesh = OverlayRender::createMesh(g_meshes[mesh].solid, g_meshes[mesh].wireframe);
	}
	auto &vs = g_meshes[mesh].line_verts;
	vs.insert(vs.end(), { a, b });
	g_meshes[mesh].pos += (a + b) / 2.0;
	g_meshes[mesh].num_points_in_pos += 1;
}

void OverlayRender::addQuad(MeshId &mesh, Vector a, Vector b, Vector c, Vector d, bool cull_back) {
	OverlayRender::addTriangle(mesh, a, b, c, cull_back);
	OverlayRender::addTriangle(mesh, a, c, d, cull_back);
}

void OverlayRender::addBoxMesh(Vector origin, Vector mins, Vector maxs, QAngle ang, RenderCallback solid, RenderCallback wireframe) {
	auto rot = Math::AngleMatrix(ang);

	Vector verts[8];
	for (int i = 0; i < 8; ++i) {
		Vector v;
		v.x = (i & 1) ? maxs[0] : mins[0];
		v.y = (i & 2) ? maxs[1] : mins[1];
		v.z = (i & 4) ? maxs[2] : mins[2];
		verts[i] = origin + rot * v;
	}

	MeshId solid_mesh = OverlayRender::createMesh(solid, RenderCallback::none);
	for (auto i : std::array<std::array<int, 4>, 6>{
		std::array<int, 4>{ 2, 6, 4, 0 },
		std::array<int, 4>{ 7, 3, 1, 5 },
		std::array<int, 4>{ 4, 5, 1, 0 },
		std::array<int, 4>{ 3, 7, 6, 2 },
		std::array<int, 4>{ 1, 3, 2, 0 },
		std::array<int, 4>{ 6, 7, 5, 4 },
	}) {
		OverlayRender::addQuad(solid_mesh, verts[i[0]], verts[i[1]], verts[i[2]], verts[i[3]], true);
	}

	MeshId wf_mesh = OverlayRender::createMesh(RenderCallback::none, wireframe);
	for (auto i : std::array<std::array<int, 2>, 12>{
		std::array<int, 2>{ 0, 1 },
		std::array<int, 2>{ 0, 2 },
		std::array<int, 2>{ 0, 4 },
		std::array<int, 2>{ 1, 3 },
		std::array<int, 2>{ 1, 5 },
		std::array<int, 2>{ 2, 6 },
		std::array<int, 2>{ 2, 3 },
		std::array<int, 2>{ 3, 7 },
		std::array<int, 2>{ 4, 5 },
		std::array<int, 2>{ 4, 6 },
		std::array<int, 2>{ 5, 7 },
		std::array<int, 2>{ 6, 7 },
	}) {
		OverlayRender::addLine(wf_mesh, verts[i[0]], verts[i[1]]);
	}
}

void OverlayRender::addText(Vector pos, const std::string &text, float x_height, bool visibility_scale, bool no_depth, OverlayRender::TextAlign align, Color col, Color bg_col, bool clamp_to_screen, std::vector<Vector> alts) {
	g_text.push_back({pos, align, text, col, x_height, visibility_scale, no_depth, bg_col, clamp_to_screen, std::move(alts)});
}

static IMaterial *createMaterial(KeyValues *kv, const char *name) {
	return materialSystem->CreateMaterial(materialSystem->materials->ThisPtr(), name, kv);
}

static void destroyMaterial(IMaterial *mat) {
	if (!mat) return;

	auto material = reinterpret_cast<CMaterial_QueueFriendly*>(mat)->m_pRealTimeVersion;

	auto DecrementReferenceCount = Memory::VMT<void (__rescall *)(IMaterialInternal *thisptr)>(material, Offsets::DecrementReferenceCount);
	DecrementReferenceCount(material);

	materialSystem->RemoveMaterial(materialSystem->materials->ThisPtr(), material);
	mat = nullptr;
}

static IMaterial *g_mat_solid_opaque,     *g_mat_solid_opaque_noz,     *g_mat_solid_alpha,     *g_mat_solid_alpha_noz;
static IMaterial *g_mat_wireframe_opaque, *g_mat_wireframe_opaque_noz, *g_mat_wireframe_alpha, *g_mat_wireframe_alpha_noz;
static IMaterial *g_mat_font, *g_mat_font_noz;
static ITexture *g_tex_font_atlas;

void OverlayRender::initMaterials() {
	KeyValues *kv;

	kv = new KeyValues("unlitgeneric");
	kv->SetInt("$vertexcolor", 1);
	g_mat_solid_opaque = createMaterial(kv, "_SAR_UnlitSolidOpaque");

	kv = new KeyValues("unlitgeneric");
	kv->SetInt("$vertexcolor", 1);
	kv->SetInt("$ignorez", 1);
	g_mat_solid_opaque_noz = createMaterial(kv, "_SAR_UnlitSolidOpaqueNoDepth");

	kv = new KeyValues("unlitgeneric");
	kv->SetInt("$vertexcolor", 1);
	kv->SetInt("$vertexalpha", 1);
	g_mat_solid_alpha = createMaterial(kv, "_SAR_UnlitSolidAlpha");

	kv = new KeyValues("unlitgeneric");
	kv->SetInt("$vertexcolor", 1);
	kv->SetInt("$vertexalpha", 1);
	kv->SetInt("$ignorez", 1);
	g_mat_solid_alpha_noz = createMaterial(kv, "_SAR_UnlitSolidAlphaNoDepth");

	kv = new KeyValues("wireframe");
	kv->SetInt("$vertexcolor", 1);
	g_mat_wireframe_opaque = createMaterial(kv, "_SAR_UnlitWireframeOpaque");

	kv = new KeyValues("wireframe");
	kv->SetInt("$vertexcolor", 1);
	kv->SetInt("$ignorez", 1);
	g_mat_wireframe_opaque_noz = createMaterial(kv, "_SAR_UnlitWireframeOpaqueNoDepth");

	kv = new KeyValues("wireframe");
	kv->SetInt("$vertexcolor", 1);
	kv->SetInt("$vertexalpha", 1);
	g_mat_wireframe_alpha = createMaterial(kv, "_SAR_UnlitWireframeAlpha");

	kv = new KeyValues("wireframe");
	kv->SetInt("$vertexcolor", 1);
	kv->SetInt("$vertexalpha", 1);
	kv->SetInt("$ignorez", 1);
	g_mat_wireframe_alpha_noz = createMaterial(kv, "_SAR_UnlitWireframeAlphaNoDepth");

	g_tex_font_atlas = materialSystem->CreateTexture("_SAR_FontAtlasTex", FONT_ATLAS_WIDTH, FONT_ATLAS_HEIGHT, FONT_ATLAS_DATA);

	kv = new KeyValues("unlitgeneric");
	kv->SetInt("$vertexcolor", 1);
	kv->SetInt("$vertexalpha", 1);
	kv->SetInt("$translucent", 1);
	kv->SetString("$basetexture", "_SAR_FontAtlasTex");
	g_mat_font = createMaterial(kv, "_SAR_FontAtlas");

	kv = new KeyValues("unlitgeneric");
	kv->SetInt("$vertexcolor", 1);
	kv->SetInt("$vertexalpha", 1);
	kv->SetInt("$translucent", 1);
	kv->SetInt("$ignorez", 1);
	kv->SetString("$basetexture", "_SAR_FontAtlasTex");
	g_mat_font_noz = createMaterial(kv, "_SAR_FontAtlasNoDepth");
}

ON_EVENT(SAR_UNLOAD) {
	destroyMaterial(g_mat_solid_opaque);
	destroyMaterial(g_mat_solid_opaque_noz);
	destroyMaterial(g_mat_solid_alpha);
	destroyMaterial(g_mat_solid_alpha_noz);
	destroyMaterial(g_mat_wireframe_alpha);
	destroyMaterial(g_mat_wireframe_alpha_noz);
	destroyMaterial(g_mat_wireframe_opaque);
	destroyMaterial(g_mat_wireframe_opaque_noz);
	destroyMaterial(g_mat_font);
	destroyMaterial(g_mat_font_noz);
	materialSystem->DestroyTexture(g_tex_font_atlas);
}

static void drawVerts(IMaterial *mat, bool lines, Vector *verts, int nverts, Color col) {
	int prims = lines ? nverts / 2 : nverts / 3;
	MeshBuilder mb(mat, lines ? PrimitiveType::LINES : PrimitiveType::TRIANGLES, prims);

	for (int i = 0; i < nverts; ++i) {
		mb.Position(verts[i]);
		mb.Color(col);
		mb.AdvanceVertex();
	}

	mb.Draw();
}

static void drawMesh(ViewSetup *setup, OverlayMesh &m, bool translucent) {
	Color solid_color, wf_color;
	bool solid_nodepth, wf_nodepth;
	m.solid.cbk(setup, solid_color, solid_nodepth);
	m.wireframe.cbk(setup, wf_color, wf_nodepth);

	if (solid_color.a != 0 && (translucent ^ (solid_color.a == 255))) {
		IMaterial *mat = solid_nodepth ?
			(translucent ? g_mat_solid_alpha_noz : g_mat_solid_opaque_noz) :
			(translucent ? g_mat_solid_alpha     : g_mat_solid_opaque);

		// Tris
		drawVerts(mat, false, m.tri_verts.data(), m.tri_verts.size(), solid_color);
	}

	if (wf_color.a != 0 && (translucent ^ (wf_color.a == 255))) {
		IMaterial *mat = wf_nodepth ?
			(translucent ? g_mat_wireframe_alpha_noz : g_mat_wireframe_opaque_noz) :
			(translucent ? g_mat_wireframe_alpha     : g_mat_wireframe_opaque);

		// Lines
		drawVerts(mat, true, m.line_verts.data(), m.line_verts.size(), wf_color);

		// Tris
		drawVerts(mat, true, m.tri_verts.data(), m.tri_verts.size(), wf_color);
	}
}

static Matrix createTextRotationMatrix(Vector text_pos, ViewSetup *setup) {
	(void)text_pos; // not used for now

	auto rot = Math::AngleMatrix({-setup->angles.x, fmodf(setup->angles.y + 180.0, 360.0), 0});

	return rot;
}

static float drawTextLine(const char *str, Vector top_center, Color text_color, float scale, Matrix rot, bool no_depth) {
	int width = 0;
	int min_height = 0;
	int max_height = 0;
	for (const char *ptr = str; *ptr; ++ptr) {
		auto info = FONT_ATLAS_INFO[*ptr];
		width += info.advance;
		if (info.origin_y > max_height) max_height = info.origin_y;
		if (info.origin_y - info.height < min_height) min_height = info.origin_y - info.height;
	}

	Vector center_baseline = top_center + rot * Vector{ 0, 0, -(float)max_height } * scale;

	{
		Vector base = center_baseline + rot * Vector{ 0.0, -(float)width * scale * 0.5f, 0.0 };

		MeshBuilder text(no_depth ? g_mat_font_noz : g_mat_font, PrimitiveType::QUADS, strlen(str));
		for (const char *ptr = str; *ptr; ++ptr) {
			auto info = FONT_ATLAS_INFO[*ptr];

			Vector bl = base + rot * Vector{ 0.0, -(float)info.origin_x, (float)(info.origin_y - info.height) } * scale;
			Vector tl = base + rot * Vector{ 0.0, -(float)info.origin_x, (float)info.origin_y } * scale;
			Vector br = base + rot * Vector{ 0.0, (float)(-info.origin_x + info.width), (float)(info.origin_y - info.height) } * scale;
			Vector tr = base + rot * Vector{ 0.0, (float)(-info.origin_x + info.width), (float)info.origin_y } * scale;

			float tex[4] = {
				(float)info.x / FONT_ATLAS_WIDTH,
				(float)(info.y + info.height) / FONT_ATLAS_HEIGHT,
				(float)(info.x + info.width) / FONT_ATLAS_WIDTH,
				(float)info.y / FONT_ATLAS_HEIGHT,
			};

			text.Position(bl); text.Color(text_color); text.TexCoord(0, tex[0], tex[1]); text.AdvanceVertex();
			text.Position(tl); text.Color(text_color); text.TexCoord(0, tex[0], tex[3]); text.AdvanceVertex();
			text.Position(tr); text.Color(text_color); text.TexCoord(0, tex[2], tex[3]); text.AdvanceVertex();
			text.Position(br); text.Color(text_color); text.TexCoord(0, tex[2], tex[1]); text.AdvanceVertex();

			base += rot * Vector{ 0.0, (float)info.advance, 0.0 } * scale;
		}
		text.Draw();
	}

	return (max_height - min_height + FONT_VPAD) * scale;
}

// Mark labels (clamp_to_screen) are projected into the view, kept inside a
// screen-edge inset, decluttered, and placed at this fixed distance so they read
// at a constant on-screen size and always draw on top.
static const float kClampDist = 96.0f;
// Pulled in from the very edge so a label's glyphs (which extend up/sideways from
// the anchor) don't clip against the screen border.
static const float kClampMargin = 0.84f;

// Project a world point to normalized device coords [-1,1]. False if at/behind
// the near plane.
static bool projectNDC(ViewSetup *setup, const Vector &p, float &nx, float &ny) {
	Vector fwd, right, up;
	Math::AngleVectors(setup->angles, &fwd, &right, &up);
	Vector d = p - setup->origin;
	float along = d.x * fwd.x + d.y * fwd.y + d.z * fwd.z;
	if (along <= setup->zNear) return false;
	int sw = 0, sh = 0;
	engine->GetScreenSize(nullptr, sw, sh);
	float aspect = sh > 0 ? (float)sw / (float)sh : 16.0f / 9.0f;
	float tanX = tanf(setup->fov * (float)(M_PI / 180.0) * 0.5f);
	if (tanX <= 1e-4f) return false; // degenerate fov -> skip instead of NaN
	float tanY = aspect > 0 ? tanX / aspect : tanX;
	nx = (d.x * right.x + d.y * right.y + d.z * right.z) / (along * tanX);
	ny = (d.x * up.x + d.y * up.y + d.z * up.z) / (along * tanY);
	return true;
}

static void clampToMargin(float &nx, float &ny) {
	nx = nx < -kClampMargin ? -kClampMargin : (nx > kClampMargin ? kClampMargin : nx);
	ny = ny < -kClampMargin ? -kClampMargin : (ny > kClampMargin ? kClampMargin : ny);
}

// Inverse of projectNDC at a fixed distance in front of the camera.
static Vector unprojectNDC(ViewSetup *setup, float nx, float ny, float dist) {
	Vector fwd, right, up;
	Math::AngleVectors(setup->angles, &fwd, &right, &up);
	int sw = 0, sh = 0;
	engine->GetScreenSize(nullptr, sw, sh);
	float aspect = sh > 0 ? (float)sw / (float)sh : 16.0f / 9.0f;
	float tanX = tanf(setup->fov * (float)(M_PI / 180.0) * 0.5f);
	float tanY = aspect > 0 ? tanX / aspect : tanX;
	return setup->origin + fwd * dist + right * (nx * tanX * dist) + up * (ny * tanY * dist);
}

static void drawText(ViewSetup *setup, OverlayText &t) {
	// Effective position + depth. clamp_to_screen projects the anchor into the
	// view, keeps it inside a screen-edge inset, applies the declutter nudge from
	// layoutClampLabels, and places it at a fixed distance drawn on top -- so the
	// label can't fall off-screen, get sliced by a wall, or stack on a neighbour.
	Vector pos = t.pos;
	bool no_depth = t.no_depth;
	if (t.clamp_to_screen) {
		float nx, ny;
		if (!projectNDC(setup, t.pos, nx, ny)) return; // behind the camera
		clampToMargin(nx, ny);
		nx += t.nudge_x;
		ny += t.nudge_y;
		clampToMargin(nx, ny);
		pos = unprojectNDC(setup, nx, ny, kClampDist);
		no_depth = true;
	}

	std::vector<std::string> lines;
	int height = FONT_VPAD;
	int last_base_delta = 0;
	int max_width = 0;

	{
		std::string all = t.text;
		while (!all.empty()) {
			size_t pos = all.find("\n");
			std::string line;
			if (pos != std::string::npos) {
				line = all.substr(0, pos);
				all.erase(0, pos + 1);
			} else {
				line = all;
				all.clear();
			}

			int width = 0;
			int min_height = 0;
			int max_height = 0;
			for (char c : line) {
				auto info = FONT_ATLAS_INFO[c];
				width += info.advance;
				if (info.origin_y > max_height) max_height = info.origin_y;
				if (info.origin_y - info.height < min_height) min_height = info.origin_y - info.height;
			}

			lines.push_back(line);
			if (width > max_width) max_width = width;
			height += max_height - min_height + FONT_VPAD;
			last_base_delta = -min_height + FONT_VPAD;
		}
	}

	float scale = t.x_height / (float)FONT_ATLAS_INFO['x'].height;
	// clamp_to_screen labels sit at a fixed distance; keep them a constant size so
	// the declutter footprint stays accurate and edge labels don't grow.
	if (t.visibility_scale && !t.clamp_to_screen) {
		float dist = (setup->origin - pos).Length();
		if (dist > 100) {
			// this seems to work fairly well just from briefly messing around
			scale *= sqrt((dist - 60) / 40);
		}
	}

	Matrix rotation = createTextRotationMatrix(t.pos, setup);

	// the top-center of the top line of text, including padding
	Vector top_center = pos;

	switch (t.align) {
	case OverlayRender::TextAlign::BOTTOM:
		top_center += rotation * Vector{ 0, 0, (float)height } * scale;
		break;
	case OverlayRender::TextAlign::CENTER:
		top_center += rotation * Vector{ 0, 0, (float)height * 0.5f } * scale;
		break;
	case OverlayRender::TextAlign::TOP:
		break;
	case OverlayRender::TextAlign::BASELINE:
		top_center += rotation * Vector{ 0, 0, (float)(height - last_base_delta)} * scale;
		break;
	}

	// draw background
	{

		Vector bl = top_center + rotation * Vector{ 0.0, -(float)max_width * 0.5f - FONT_HPAD, -(float)height } * scale;
		Vector tl = top_center + rotation * Vector{ 0.0, -(float)max_width * 0.5f - FONT_HPAD, 0 } * scale;
		Vector br = top_center + rotation * Vector{ 0.0, (float)max_width * 0.5f + FONT_HPAD, -(float)height } * scale;
		Vector tr = top_center + rotation * Vector{ 0.0, (float)max_width * 0.5f + FONT_HPAD, 0 } * scale;

		MeshBuilder bg(no_depth ? g_mat_solid_alpha_noz : g_mat_solid_alpha, PrimitiveType::QUADS, 1);
		bg.Position(bl); bg.Color(t.bg_col); bg.AdvanceVertex();
		bg.Position(tl); bg.Color(t.bg_col); bg.AdvanceVertex();
		bg.Position(tr); bg.Color(t.bg_col); bg.AdvanceVertex();
		bg.Position(br); bg.Color(t.bg_col); bg.AdvanceVertex();
		bg.Draw();
	}

	top_center -= rotation * Vector{ 0, 0, FONT_VPAD } * scale;

	for (auto &line : lines) {
		float draw_height = drawTextLine(line.c_str(), top_center, t.col, scale, rotation, no_depth);
		top_center -= rotation * Vector{ 0, 0, draw_height };
	}
}

void OverlayRender::drawOpaques(void *viewrender) {
	// CRendering3dView inherits CViewSetup! this is handy
	auto setup = ViewSetupCreate((CViewSetup *)((uintptr_t)viewrender + 8));

	for (size_t i = 0; i < g_num_meshes; ++i) {
		drawMesh(setup, g_meshes[i], false);
	}
}

// De-overlap clamp_to_screen labels by greedy placement. Project each into the
// view, place them top-to-bottom, and drop any label that lands on an already-
// placed one straight down to a free row. Placed labels never move, so a cluster
// opens cleanly downward -- unlike symmetric pairwise nudging, which seizes up
// (a label pushed equally from both sides nets zero movement). Writes the per-
// label NDC nudge consumed by drawText.
static void layoutClampLabels(ViewSetup *setup) {
	struct L {
		OverlayText *t;
		float tx, ty;       // primary (box-top) candidate ndc; also the nudge base
		float ax[4], ay[4]; // alternate candidate ndc positions
		int na;             // number of valid alternates
		float x, y;         // placed ndc
		float w, h;         // ndc footprint (full width / height of the label)
	};
	std::vector<L> ls;
	int sw = 0, sh = 0;
	engine->GetScreenSize(nullptr, sw, sh);
	float aspect = sh > 0 ? (float)sw / (float)sh : 16.0f / 9.0f;
	float tanX = tanf(setup->fov * (float)(M_PI / 180.0) * 0.5f);
	float tanY = aspect > 0 ? tanX / aspect : tanX;
	for (auto &t : g_text) {
		t.nudge_x = 0.0f;
		t.nudge_y = 0.0f;
		if (!t.clamp_to_screen) continue;
		float tx, ty;
		if (!projectNDC(setup, t.pos, tx, ty)) continue;
		clampToMargin(tx, ty);
		// Footprint tracks the actual label size at the fixed clamp distance, so
		// the spacing stays correct if the label height or fov changes.
		float h = (t.x_height * 1.7f) / (kClampDist * tanY);
		float w = (t.x_height * 0.7f * (float)t.text.size()) / (kClampDist * tanX);
		L l{ &t, tx, ty, {}, {}, 0, tx, ty, w, h };
		// Project the caller's alternate spots (the box's other sides) that land
		// in view; the placement below prefers them over stacking.
		for (auto &a : t.alts) {
			if (l.na >= 4) break;
			float nx, ny;
			if (projectNDC(setup, a, nx, ny)) {
				clampToMargin(nx, ny);
				l.ax[l.na] = nx;
				l.ay[l.na] = ny;
				++l.na;
			}
		}
		ls.push_back(l);
	}
	std::sort(ls.begin(), ls.end(), [](const L &a, const L &b) {
		return a.ty != b.ty ? a.ty > b.ty : a.tx < b.tx;
	});
	const float gap = 0.012f; // extra ndc breathing room between rows
	auto collides = [&](size_t i, float x, float y) {
		for (size_t j = 0; j < i; ++j) {
			float needX = (ls[i].w + ls[j].w) * 0.5f;
			float needY = (ls[i].h + ls[j].h) * 0.5f + gap;
			if (fabsf(x - ls[j].x) < needX && fabsf(y - ls[j].y) < needY) return true;
		}
		return false;
	};
	for (size_t i = 0; i < ls.size(); ++i) {
		if (!collides(i, ls[i].tx, ls[i].ty)) {
			ls[i].x = ls[i].tx; // preferred spot (box top) is free
			ls[i].y = ls[i].ty;
			continue;
		}
		bool placed = false;
		for (int k = 0; k < ls[i].na; ++k) {
			if (!collides(i, ls[i].ax[k], ls[i].ay[k])) {
				ls[i].x = ls[i].ax[k]; // a box side is free
				ls[i].y = ls[i].ay[k];
				placed = true;
				break;
			}
		}
		if (!placed) {
			ls[i].x = ls[i].tx; // every candidate taken: stack down from the top
			ls[i].y = ls[i].ty;
			for (int guard = 0; guard < 256 && collides(i, ls[i].x, ls[i].y); ++guard)
				ls[i].y -= ls[i].h + gap;
		}
	}
	for (auto &l : ls) {
		l.t->nudge_x = l.x - l.tx;
		l.t->nudge_y = l.y - l.ty;
	}
}

void OverlayRender::drawTranslucents(void *viewrender, bool secondaryPass) {
	// CRendering3dView inherits CViewSetup! this is handy
	auto setup = ViewSetupCreate((CViewSetup *)((uintptr_t)viewrender + 8));

	// The clamp/declutter layout is for the main view only; skip it (and the
	// labels themselves, below) in skybox/shadow passes so they don't stray.
	if (!secondaryPass) layoutClampLabels(setup);

	// Order meshes!
	struct MeshCompare {
		bool operator()(std::pair<bool, void *> a, std::pair<bool, void *> b) const {
			float dist_a;
			if (a.first) {
				OverlayText *t = (OverlayText *)a.second;
				dist_a = (t->pos - this->origin).SquaredLength();
			} else {
				OverlayMesh *m = (OverlayMesh *)a.second;
				int npa = m->num_points_in_pos == 0 ? 1 : m->num_points_in_pos;
				dist_a = (m->pos / npa - this->origin).SquaredLength();
			}

			float dist_b;
			if (b.first) {
				OverlayText *t = (OverlayText *)b.second;
				dist_b = (t->pos - this->origin).SquaredLength();
			} else {
				OverlayMesh *m = (OverlayMesh *)b.second;
				int npb = m->num_points_in_pos == 0 ? 1 : m->num_points_in_pos;
				dist_b = (m->pos / npb - this->origin).SquaredLength();
			}

			if (dist_a == dist_b) return a.second < b.second; // Define *some* kind of ordering for meshes in the same place
			return dist_a > dist_b;
		}
		Vector origin;
	};
	std::set<std::pair<bool, void *>, MeshCompare> meshes(MeshCompare{setup->origin});
	for (size_t i = 0; i < g_num_meshes; ++i) {
		meshes.insert({ false, &g_meshes[i] });
	}
	for (auto &text : g_text) {
		meshes.insert({ true, &text });
	}

	for (auto mesh : meshes) {
		if (mesh.first) {
			OverlayText *t = (OverlayText *)mesh.second;
			// On-top clamp labels belong to the main view only; a secondary pass
			// would project them through its own camera and stray onto the screen.
			if (secondaryPass && t->clamp_to_screen) continue;
			drawText(setup, *t);
		} else {
			drawMesh(setup, *(OverlayMesh *)mesh.second, true);
		}
	}
}
