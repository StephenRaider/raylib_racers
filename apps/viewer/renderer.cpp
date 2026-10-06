#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <random>

#include "raymath.h"
#include "rlgl.h"

using rr::Vec2;

namespace {

// ---------------------------------------------------------------- shaders

const char* kLitVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
void main() {
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* kLitFS = R"(#version 330
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 ambientSky;
uniform vec3 ambientGround;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogDensity;
uniform float specStrength;
uniform mat4 lightVP;
uniform sampler2D shadowMap;
uniform int shadowMapResolution;
out vec4 finalColor;

float shadowFactor(vec3 n, vec3 l) {
    // normal offset avoids shadow acne on surfaces at grazing angles to the sun
    vec4 lp = lightVP * vec4(fragPosition + n * 0.12, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 0.0;
    // depth range of the sun camera is ~800 m: 0.00006 ~ 5 cm
    float bias = max(0.0003 * (1.0 - dot(n, l)), 0.00006);
    vec2 texel = vec2(1.0 / float(shadowMapResolution));
    float s = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            s += (p.z - bias > texture(shadowMap, p.xy + vec2(x, y) * texel).r) ? 1.0 : 0.0;
    // fade out towards the edge of the shadow map
    vec2 edge = min(p.xy, 1.0 - p.xy);
    float fade = clamp(min(edge.x, edge.y) * 12.0, 0.0, 1.0);
    return s / 9.0 * fade;
}

void main() {
    vec4 tex = texture(texture0, fragTexCoord);
    vec3 base = tex.rgb * colDiffuse.rgb * fragColor.rgb;
    vec3 n = normalize(fragNormal);
    vec3 l = -normalize(lightDir);
    float ndl = max(dot(n, l), 0.0);
    float sh = ndl > 0.0 ? shadowFactor(n, l) : 0.0;
    vec3 hemi = mix(ambientGround, ambientSky, n.y * 0.5 + 0.5);
    vec3 v = normalize(viewPos - fragPosition);
    vec3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), 48.0) * specStrength * ndl;
    vec3 col = base * (hemi + lightColor * ndl * (1.0 - sh)) + lightColor * spec * (1.0 - sh);
    float dist = length(viewPos - fragPosition);
    float fog = 1.0 - exp(-pow(dist * fogDensity, 2.0));
    col = mix(col, fogColor, clamp(fog, 0.0, 1.0));
    finalColor = vec4(col, tex.a * colDiffuse.a * fragColor.a);
}
)";

const char* kDepthVS = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";

const char* kDepthFS = R"(#version 330
out vec4 finalColor;
void main() { finalColor = vec4(1.0); }
)";

// ---------------------------------------------------------------- helpers

const Vector3 kLightDir = Vector3Normalize({0.45f, -0.75f, 0.35f});
const Color kSkyTop = {78, 128, 200, 255};
const Color kSkyHorizon = {190, 210, 232, 255};

Vector3 W(Vec2 p, float h = 0) { return {p.x, h, -p.y}; }
Vector3 Wdir(Vec2 d) { return {d.x, 0, -d.y}; }

uint32_t hash3(int x, int y, int seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float hashf(int x, int y, int seed) { return (hash3(x, y, seed) & 0xffffff) / 16777215.0f; }

// Tileable value noise on a period x period lattice.
float valueNoise(float x, float y, int period, int seed) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    float fx = x - x0, fy = y - y0;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    auto at = [&](int xi, int yi) {
        xi = ((xi % period) + period) % period;
        yi = ((yi % period) + period) % period;
        return hashf(xi, yi, seed);
    };
    float a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

float fbm(float u, float v, int freq, int octaves, int seed) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * valueNoise(u * freq, v * freq, freq, seed + o * 17);
        norm += amp;
        amp *= 0.5f;
        freq *= 2;
    }
    return sum / norm;
}

Texture2D makeTexture(int size, const std::function<Color(int, int)>& f) {
    Color* px = (Color*)MemAlloc(size * size * sizeof(Color));
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) px[y * size + x] = f(x, y);
    Image img{px, size, size, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_ANISOTROPIC_8X);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

unsigned char c8(float v) { return (unsigned char)std::clamp(v, 0.0f, 255.0f); }

struct MeshBuilder {
    std::vector<float> pos, nrm, uv;
    std::vector<unsigned char> col;

    void vert(Vector3 p, Vector3 n, Vector2 t, Color c) {
        pos.insert(pos.end(), {p.x, p.y, p.z});
        nrm.insert(nrm.end(), {n.x, n.y, n.z});
        uv.insert(uv.end(), {t.x, t.y});
        col.insert(col.end(), {c.r, c.g, c.b, c.a});
    }
    // Quad a-b-c-d; the winding is fixed up so the front face points along n.
    void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 n, Vector2 ta, Vector2 tb, Vector2 tc, Vector2 td,
              Color col) {
        Vector3 cr = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
        if (Vector3DotProduct(cr, n) < 0) {
            std::swap(b, d);
            std::swap(tb, td);
        }
        vert(a, n, ta, col); vert(b, n, tb, col); vert(c, n, tc, col);
        vert(a, n, ta, col); vert(c, n, tc, col); vert(d, n, td, col);
    }
    Mesh build() {
        Mesh m{};
        m.vertexCount = (int)pos.size() / 3;
        m.triangleCount = m.vertexCount / 3;
        m.vertices = (float*)MemAlloc(pos.size() * sizeof(float));
        m.normals = (float*)MemAlloc(nrm.size() * sizeof(float));
        m.texcoords = (float*)MemAlloc(uv.size() * sizeof(float));
        m.colors = (unsigned char*)MemAlloc(col.size());
        std::memcpy(m.vertices, pos.data(), pos.size() * sizeof(float));
        std::memcpy(m.normals, nrm.data(), nrm.size() * sizeof(float));
        std::memcpy(m.texcoords, uv.data(), uv.size() * sizeof(float));
        std::memcpy(m.colors, col.data(), col.size());
        UploadMesh(&m, false);
        return m;
    }
};

// A barrier segment from a to b (ground level): inner face along inN, a top,
// and an outer face offset by thick (track-plane vector).
void addWall(MeshBuilder& mb, Vector3 a, Vector3 b, Vector3 inN, Vector3 outN, Vec2 thick, float hgt, Color face) {
    Vector3 t = {thick.x, 0, -thick.y};
    Vector3 up = {0, 1, 0};
    auto H = [&](Vector3 v, float h) { return Vector3{v.x, v.y + h, v.z}; };
    Vector3 a2 = Vector3Add(a, t), b2 = Vector3Add(b, t);
    mb.quad(a, b, H(b, hgt), H(a, hgt), inN, {0, 0}, {0, 0}, {0, 0}, {0, 0}, face);
    mb.quad(H(a, hgt), H(b, hgt), H(b2, hgt), H(a2, hgt), up, {0, 0}, {0, 0}, {0, 0}, {0, 0}, Color{150, 150, 155, 255});
    mb.quad(a2, b2, H(b2, hgt), H(a2, hgt), outN, {0, 0}, {0, 0}, {0, 0}, {0, 0}, Color{175, 175, 180, 255});
}

Model modelFrom(Mesh mesh, Texture2D tex) {
    Model m = LoadModelFromMesh(mesh);
    m.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    return m;
}

Matrix boxTransform(Vector3 center, Vector3 size, float yaw) {
    return MatrixMultiply(MatrixMultiply(MatrixScale(size.x, size.y, size.z), MatrixRotateY(yaw)),
                          MatrixTranslate(center.x, center.y, center.z));
}

}  // namespace

Color teamColor(int i) {
    static const Color c[] = {{220, 40, 40, 255},  {30, 110, 230, 255}, {245, 190, 20, 255}, {30, 170, 90, 255},
                              {240, 120, 20, 255}, {150, 60, 200, 255}, {20, 190, 200, 255}, {235, 235, 240, 255}};
    return c[i % 8];
}

Color teamAccent(int i) {
    static const Color c[] = {{245, 245, 245, 255}, {250, 210, 40, 255}, {30, 30, 35, 255},  {240, 240, 240, 255},
                              {30, 30, 35, 255},    {250, 250, 250, 255}, {30, 30, 35, 255}, {200, 30, 30, 255}};
    return c[i % 8];
}

// ---------------------------------------------------------------- setup

bool Renderer::init(const rr::Track& track, unsigned seed, std::string* err) {
    rlSetClipPlanes(0.5, 5000.0);

    lit_ = LoadShaderFromMemory(kLitVS, kLitFS);
    depth_ = LoadShaderFromMemory(kDepthVS, kDepthFS);
    if (lit_.id == 0 || depth_.id == 0 || lit_.id == rlGetShaderIdDefault()) {
        if (err) *err = "shader compilation failed (OpenGL 3.3 required)";
        return false;
    }
    lit_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(lit_, "matModel");
    lit_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(lit_, "matNormal");
    locLightVP_ = GetShaderLocation(lit_, "lightVP");
    locShadowMap_ = GetShaderLocation(lit_, "shadowMap");
    locViewPos_ = GetShaderLocation(lit_, "viewPos");
    locSpec_ = GetShaderLocation(lit_, "specStrength");
    auto set3 = [&](const char* name, Vector3 v) { SetShaderValue(lit_, GetShaderLocation(lit_, name), &v, SHADER_UNIFORM_VEC3); };
    set3("lightDir", kLightDir);
    set3("lightColor", {1.05f, 1.0f, 0.92f});
    set3("ambientSky", {0.42f, 0.50f, 0.62f});
    set3("ambientGround", {0.24f, 0.24f, 0.20f});
    set3("fogColor", {kSkyHorizon.r / 255.0f, kSkyHorizon.g / 255.0f, kSkyHorizon.b / 255.0f});
    float spec = 0.15f;
    locFog_ = GetShaderLocation(lit_, "fogDensity");
    SetShaderValue(lit_, locSpec_, &spec, SHADER_UNIFORM_FLOAT);
    int res = shadowRes_;
    SetShaderValue(lit_, GetShaderLocation(lit_, "shadowMapResolution"), &res, SHADER_UNIFORM_INT);

    // Depth-only render target for the sun's shadow map.
    shadowMap_.id = rlLoadFramebuffer();
    shadowMap_.texture.width = shadowRes_;
    shadowMap_.texture.height = shadowRes_;
    rlEnableFramebuffer(shadowMap_.id);
    shadowMap_.depth.id = rlLoadTextureDepth(shadowRes_, shadowRes_, false);
    shadowMap_.depth.width = shadowRes_;
    shadowMap_.depth.height = shadowRes_;
    shadowMap_.depth.format = 19;
    shadowMap_.depth.mipmaps = 1;
    rlFramebufferAttach(shadowMap_.id, shadowMap_.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    bool ok = rlFramebufferComplete(shadowMap_.id);
    rlDisableFramebuffer();
    if (!ok) {
        if (err) *err = "could not create the shadow-map framebuffer";
        return false;
    }

    // Procedural textures (tileable).
    texAsphalt_ = makeTexture(512, [](int x, int y) {
        float u = x / 512.0f, v = y / 512.0f;
        float g = 66 + 22 * fbm(u, v, 16, 4, 3) + 8 * (hashf(x, y, 7) - 0.5f);
        if (hashf(x, y, 11) > 0.995f) g += 18;  // aggregate specks
        return Color{c8(g), c8(g), c8(g + 4), 255};
    });
    texGrass_ = makeTexture(512, [](int x, int y) {
        float u = x / 512.0f, v = y / 512.0f;
        float big = fbm(u, v, 4, 4, 21), fine = hashf(x, y, 5);
        float k = 0.72f + 0.45f * big + 0.12f * (fine - 0.5f);
        return Color{c8(66 * k + 22 * big), c8(104 * k), c8(48 * k), 255};
    });
    {
        Image chk = GenImageChecked(64, 64, 8, 8, RAYWHITE, Color{25, 25, 25, 255});
        texChecker_ = LoadTextureFromImage(chk);
        UnloadImage(chk);
        SetTextureFilter(texChecker_, TEXTURE_FILTER_POINT);
        SetTextureWrap(texChecker_, TEXTURE_WRAP_REPEAT);
    }
    texWhite_ = Texture2D{rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};

    mdlCube_ = LoadModelFromMesh(GenMeshCube(1, 1, 1));
    mdlWheel_ = LoadModelFromMesh(GenMeshCylinder(1, 1, 20));
    mdlSphere_ = LoadModelFromMesh(GenMeshSphere(1, 12, 16));
    mdlCone_ = LoadModelFromMesh(GenMeshCone(1, 1, 10));
    mdlTrunk_ = LoadModelFromMesh(GenMeshCylinder(1, 1, 8));

    buildTrack(track);
    buildScenery(track, seed);
    return true;
}

void Renderer::shutdown() {
    // Shared textures are owned here, not by the models.
    for (Model* m : {&mdlAsphalt_, &mdlMarkings_, &mdlWalls_, &mdlGround_, &mdlStart_, &mdlCube_, &mdlWheel_,
                     &mdlSphere_, &mdlCone_, &mdlTrunk_}) {
        if (m->meshCount == 0) continue;
        m->materials[0].maps[MATERIAL_MAP_DIFFUSE].texture.id = rlGetTextureIdDefault();
        m->materials[0].shader = Shader{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
        UnloadModel(*m);
    }
    for (Texture2D* t : {&texAsphalt_, &texGrass_, &texChecker_})
        if (t->id) UnloadTexture(*t);
    UnloadShader(lit_);
    UnloadShader(depth_);
    if (shadowMap_.id) {
        rlUnloadTexture(shadowMap_.depth.id);
        rlUnloadFramebuffer(shadowMap_.id);
    }
}

void Renderer::buildTrack(const rr::Track& tr) {
    const int n = tr.size();
    const Vector3 up = {0, 1, 0};

    // Kerbs where the track bends, widened a little either side.
    std::vector<char> kerb(n, 0);
    for (int i = 0; i < n; ++i)
        if (std::fabs(tr.at(i).curvature) > 1.0f / 220.0f)
            for (int k = -12; k <= 12; ++k) kerb[tr.wrap(i + k)] = 1;

    MeshBuilder asphalt, marks, walls, ground, start;
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < n; ++i) {
        const auto& a = tr.at(i);
        const auto& b = tr.at(i + 1);
        float sa = a.s, sb = (i + 1 == n) ? tr.length() : b.s;
        minX = std::min(minX, a.p.x); maxX = std::max(maxX, a.p.x);
        minY = std::min(minY, a.p.y); maxY = std::max(maxY, a.p.y);

        auto edge = [&](const rr::TrackSample& s, float lat, float h) { return W(s.p + s.n * lat, h); };

        // tarmac: u across (one tile per 8 m), v along
        float ua = a.halfWidth / 4.0f, ub = b.halfWidth / 4.0f;
        asphalt.quad(edge(a, -a.halfWidth, 0), edge(b, -b.halfWidth, 0), edge(b, b.halfWidth, 0), edge(a, a.halfWidth, 0),
                     up, {-ua, sa / 8}, {-ub, sb / 8}, {ub, sb / 8}, {ua, sa / 8}, WHITE);

        for (float side : {1.0f, -1.0f}) {
            // painted edge line
            Color white = {235, 235, 235, 255};
            marks.quad(edge(a, side * (a.halfWidth - 0.45f), 0.015f), edge(b, side * (b.halfWidth - 0.45f), 0.015f),
                       edge(b, side * (b.halfWidth - 0.2f), 0.015f), edge(a, side * (a.halfWidth - 0.2f), 0.015f), up,
                       {0, 0}, {0, 0}, {0, 0}, {0, 0}, white);
            // kerb stripes
            if (kerb[i]) {
                bool red = ((int)std::floor(sa / 2.5f)) % 2 == 0;
                Color c = red ? Color{200, 35, 35, 255} : Color{235, 235, 235, 255};
                marks.quad(edge(a, side * a.halfWidth, 0.03f), edge(b, side * b.halfWidth, 0.03f),
                           edge(b, side * (b.halfWidth + 1.2f), 0.07f), edge(a, side * (a.halfWidth + 1.2f), 0.07f), up,
                           {0, 0}, {0, 0}, {0, 0}, {0, 0}, c);
            }
            // barrier: inner face, top, outer face (pushed out round the pit area)
            int sd = side > 0 ? 1 : -1;
            const float extra = tr.barrierOffset(0.5f * (sa + sb), sd, 0.0f);  // beyond the tarmac edge
            const float ob = b.halfWidth + extra;
            float wa = side * (a.halfWidth + extra), wb = side * ob;
            float th = side * 0.4f, hgt = 1.0f;
            Vector3 inN = Wdir(a.n * -side), outN = Wdir(a.n * side);
            bool panel = ((int)std::floor(sa / 8.0f)) % 2 == 0;
            Color face = panel ? Color{232, 232, 236, 255} : Color{40, 85, 175, 255};
            addWall(walls, edge(a, wa, 0), edge(b, wb, 0), inN, outN, a.n * th, hgt, face);
            // Where the barrier steps out for the pit area, close the gap across.
            const float nextS = sb + 0.5f * tr.spacing();  // middle of the next segment
            float next = b.halfWidth + tr.barrierOffset(nextS, sd, 0.0f);
            if (std::fabs(next - ob) > 0.5f) {
                Vector3 fwdN = Wdir(b.t * (next > ob ? -1.0f : 1.0f));
                addWall(walls, edge(b, side * std::min(ob, next), 0), edge(b, side * std::max(ob, next), 0), fwdN,
                        Vector3Negate(fwdN), b.t * 0.4f * (next > ob ? -1.0f : 1.0f), hgt, face);
            }
        }

        // Pit area: paved apron out to the barrier, the pit wall along the lane,
        // the fast-lane line and the speed-limit lines.
        if (tr.hasPit() && tr.inPitArea(sa) && tr.inPitArea(sb)) {
            const float side = (float)tr.pit().side;
            const Color apron = {178, 178, 184, 255};
            float ia = a.halfWidth + 1.2f, ib = b.halfWidth + 1.2f;
            float oa = a.halfWidth + rr::Track::kPitBarrier, ob = b.halfWidth + rr::Track::kPitBarrier;
            asphalt.quad(edge(a, side * ia, 0.005f), edge(b, side * ib, 0.005f), edge(b, side * ob, 0.005f),
                         edge(a, side * oa, 0.005f), up, {ia / 4, sa / 8}, {ib / 4, sb / 8}, {ob / 4, sb / 8},
                         {oa / 4, sa / 8}, apron);
            if (tr.inPitLane(sa) && tr.inPitLane(sb)) {
                float da = side * (a.halfWidth + rr::Track::kDividerIn), db = side * (b.halfWidth + rr::Track::kDividerIn);
                bool red = ((int)std::floor(sa / 4.0f)) % 2 == 0;
                Color c = red ? Color{205, 40, 40, 255} : Color{238, 238, 240, 255};
                float thick = rr::Track::kDividerOut - rr::Track::kDividerIn;
                addWall(walls, edge(a, da, 0), edge(b, db, 0), Wdir(a.n * -side), Wdir(a.n * side), a.n * (side * thick),
                        1.1f, c);
                // line between the fast lane and the boxes
                float la = side * (a.halfWidth + 7.0f), lb = side * (b.halfWidth + 7.0f);
                marks.quad(edge(a, la - side * 0.1f, 0.02f), edge(b, lb - side * 0.1f, 0.02f), edge(b, lb + side * 0.1f, 0.02f),
                           edge(a, la + side * 0.1f, 0.02f), up, {0, 0}, {0, 0}, {0, 0}, {0, 0}, Color{235, 235, 235, 255});
            }
        }
    }

    // Pit speed-limit lines across the lane.
    if (tr.hasPit()) {
        const auto& p = tr.pit();
        for (float s : {p.lane_start_s, p.lane_end_s}) {
            const auto& sm = tr.at(tr.indexAt(s));
            float lo = sm.halfWidth + rr::Track::kDividerOut, hi = sm.halfWidth + rr::Track::kPitBarrier;
            Vec2 f = sm.t * 0.3f;
            auto P = [&](float along, float lat) { return W(sm.p + f * along + sm.n * (p.side * lat), 0.025f); };
            marks.quad(P(-1, lo), P(1, lo), P(1, hi), P(-1, hi), up, {0, 0}, {0, 0}, {0, 0}, {0, 0}, Color{235, 235, 235, 255});
        }
    }

    // start / finish line, 2 m deep, 0.5 m squares
    {
        const auto& s0 = tr.at(0);
        float hw = s0.halfWidth;
        Vec2 f = s0.t, l = s0.n;
        auto P = [&](float along, float lat) { return W(s0.p + f * along + l * lat, 0.02f); };
        float u = 2 * hw / 0.5f / 8.0f, v = 2.0f / 0.5f / 8.0f;
        start.quad(P(-1, -hw), P(1, -hw), P(1, hw), P(-1, hw), up, {0, 0}, {0, v}, {u, v}, {u, 0}, WHITE);
    }

    // ground
    trackCenter_ = {(minX + maxX) / 2, 0, -(minY + maxY) / 2};
    trackExtent_ = std::max(maxX - minX, maxY - minY);
    float half = trackExtent_ * 0.5f + 1500.0f;
    float tile = 14.0f;
    Vector3 c = trackCenter_;
    ground.quad({c.x - half, -0.08f, c.z - half}, {c.x + half, -0.08f, c.z - half}, {c.x + half, -0.08f, c.z + half},
                {c.x - half, -0.08f, c.z + half}, up, {0, 0}, {2 * half / tile, 0}, {2 * half / tile, 2 * half / tile},
                {0, 2 * half / tile}, WHITE);

    mdlAsphalt_ = modelFrom(asphalt.build(), texAsphalt_);
    mdlMarkings_ = modelFrom(marks.build(), texWhite_);
    mdlWalls_ = modelFrom(walls.build(), texWhite_);
    mdlGround_ = modelFrom(ground.build(), texGrass_);
    mdlStart_ = modelFrom(start.build(), texChecker_);
}

void Renderer::buildScenery(const rr::Track& tr, unsigned seed) {
    // Gantry over the start line.
    const auto& s0 = tr.at(0);
    float yaw0 = std::atan2(s0.t.y, s0.t.x);
    float postOff = s0.halfWidth + tr.runoff() + 1.2f;
    Color gantry = {55, 58, 66, 255};
    for (float side : {1.0f, -1.0f})
        boxes_.push_back({W(s0.p + s0.n * (side * postOff), 3.75f), {0.6f, 7.5f, 0.6f}, gantry});
    boxes_.push_back({W(s0.p, 7.2f), {0.9f, 1.4f, 2 * postOff + 0.6f}, gantry});
    // (boxes_ store size in the track frame: x along the track, z across; yaw applied when drawn)

    // Grandstand on the outside (right) of the longest straight-ish stretch after the line.
    float bestLen = 0, bestS = 0;
    for (float s = 0; s < tr.length(); s += 10) {
        float len = 0;
        while (len < 400 && std::fabs(tr.at(tr.indexAt(s + len)).curvature) < 1.0f / 800.0f) len += 5;
        if (len > bestLen) { bestLen = len; bestS = s; }
    }
    if (bestLen >= 120) {
        float standLen = std::min(220.0f, bestLen - 40);
        float mid = bestS + bestLen * 0.5f;
        const auto& sm = tr.at(tr.indexAt(mid));
        float base = sm.halfWidth + tr.runoff() + 3.0f;
        for (int k = 0; k < 7; ++k) {
            float h = 0.9f * (k + 1);
            Color seat = (k % 2) ? Color{40, 62, 130, 255} : Color{190, 192, 198, 255};
            boxes_.push_back({W(sm.p - sm.n * (base + 1.6f * k + 0.8f), h * 0.5f), {standLen, h, 1.6f}, seat});
        }
        boxes_.push_back({W(sm.p - sm.n * (base + 6.0f), 11.0f), {standLen + 6, 0.4f, 14.0f}, {225, 225, 228, 255}});
        for (int k = 0; k <= 4; ++k) {
            float along = -standLen * 0.5f + standLen * k / 4.0f;
            boxes_.push_back({W(sm.p + sm.t * along - sm.n * (base + 12.5f), 5.5f), {0.5f, 11.0f, 0.5f}, gantry});
        }
        // remember the stand's heading for drawing (all stand boxes share it)
    }

    // Trees scattered away from the track.
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> U(0, 1);
    float half = trackExtent_ * 0.5f + 250.0f;
    int attempts = 0;
    while (trees_.size() < 420 && attempts++ < 6000) {
        Vec2 p = {trackCenter_.x + (U(rng) * 2 - 1) * half, -trackCenter_.z + (U(rng) * 2 - 1) * half};
        rr::TrackLoc loc = tr.locateGlobal(p);
        if (std::fabs(loc.lateral) < loc.halfWidth + tr.runoff() + 9.0f) continue;
        // keep clear of the grandstand roof
        bool clash = false;
        for (const auto& b : boxes_)
            if (Vector2Distance({b.center.x, b.center.z}, {p.x, -p.y}) < b.size.x * 0.5f + 10.0f && b.size.x > 50) clash = true;
        if (clash) continue;
        trees_.push_back({W(p), 0.8f + 0.7f * U(rng), U(rng)});
    }

    // TV camera spots on the outside of the bends every ~250 m.
    for (float s = 0; s < tr.length(); s += 250) {
        const auto& sm = tr.at(tr.indexAt(s));
        float side = sm.curvature > 0 ? -1.0f : 1.0f;
        tvSpots_.push_back(W(sm.p + sm.n * (side * (sm.halfWidth + tr.runoff() + 8.0f)), 6.0f));
    }
    standYaw_ = 0;
    if (bestLen >= 120) {
        const auto& sm = tr.at(tr.indexAt(bestS + bestLen * 0.5f));
        standYaw_ = std::atan2(sm.t.y, sm.t.x);
    }
    gantryYaw_ = yaw0;
    numGantryBoxes_ = 3;
}

// ---------------------------------------------------------------- camera

void Renderer::updateCamera(const rr::Race& race, int focus, CamMode mode, float dt) {
    const rr::Car& c = race.cars()[focus];
    Vector3 p = W(c.state.pos);
    Vector3 fwd = Wdir(rr::fromAngle(c.state.yaw));
    Vec2 vel = c.state.velWorld();
    float speed = rr::length(vel);
    // Follow the direction of travel when moving, so spins do not whip the camera round.
    Vector3 dir = speed > 3 ? Vector3Normalize(Vector3Lerp(fwd, Wdir(rr::normalize(vel)), 0.6f)) : fwd;
    camera.up = {0, 1, 0};
    camera.projection = CAMERA_PERSPECTIVE;
    if (focus != lastFocus_) chaseInit_ = false;
    lastFocus_ = focus;

    switch (mode) {
        case CAM_CHASE: {
            Vector3 wantPos = Vector3Add(Vector3Subtract(p, Vector3Scale(dir, 8.5f)), {0, 2.7f, 0});
            Vector3 wantTarget = Vector3Add(Vector3Add(p, Vector3Scale(dir, 4.0f)), {0, 0.9f, 0});
            if (!chaseInit_) {
                chasePos_ = wantPos;
                chaseTarget_ = wantTarget;
                chaseInit_ = true;
            }
            float k = 1 - std::exp(-dt * 6.0f), kt = 1 - std::exp(-dt * 14.0f);
            chasePos_ = Vector3Lerp(chasePos_, wantPos, k);
            chaseTarget_ = Vector3Lerp(chaseTarget_, wantTarget, kt);
            // never fall too far behind at high speed
            Vector3 off = Vector3Subtract(chasePos_, p);
            float d = Vector3Length(off);
            if (d > 13.0f) chasePos_ = Vector3Add(p, Vector3Scale(off, 13.0f / d));
            camera.position = chasePos_;
            camera.target = chaseTarget_;
            camera.fovy = 55.0f + std::min(14.0f, speed * 0.18f);
            break;
        }
        case CAM_TV: {
            Vector3 best = tvSpots_.empty() ? Vector3Add(p, {20, 8, 20}) : tvSpots_[0];
            float bd = 1e30f;
            for (auto& s : tvSpots_) {
                float d = Vector3Distance(s, p);
                if (d < bd) { bd = d; best = s; }
            }
            camera.position = best;
            camera.target = Vector3Add(p, {0, 0.6f, 0});
            float dist = Vector3Distance(best, p);
            camera.fovy = std::clamp(2.0f * std::atan(11.0f / dist) * RAD2DEG, 6.0f, 60.0f);
            break;
        }
        case CAM_ORBIT: {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                Vector2 md = GetMouseDelta();
                orbitYaw_ -= md.x * 0.006f;
                orbitPitch_ = std::clamp(orbitPitch_ + md.y * 0.006f, 0.05f, 1.45f);
            }
            orbitDist_ = std::clamp(orbitDist_ * (1.0f - GetMouseWheelMove() * 0.1f), 5.0f, 250.0f);
            Vector3 off = {std::cos(orbitPitch_) * std::cos(orbitYaw_), std::sin(orbitPitch_),
                           std::cos(orbitPitch_) * std::sin(orbitYaw_)};
            camera.target = Vector3Add(p, {0, 0.6f, 0});
            camera.position = Vector3Add(camera.target, Vector3Scale(off, orbitDist_));
            camera.fovy = 50.0f;
            break;
        }
        default: {
            camera.target = trackCenter_;
            camera.position = Vector3Add(trackCenter_, {0, trackExtent_ * 0.95f, trackExtent_ * 0.55f});
            camera.fovy = 50.0f;
            break;
        }
    }
}

// ---------------------------------------------------------------- drawing

void Renderer::drawModel(Model& m, Matrix transform, Color tint) {
    m.materials[0].shader = *current_;
    m.transform = transform;
    DrawModel(m, {0, 0, 0}, 1.0f, tint);
}

void Renderer::drawBox(Vector3 center, Vector3 size, Color color, Matrix parent) {
    Matrix local = MatrixMultiply(MatrixScale(size.x, size.y, size.z), MatrixTranslate(center.x, center.y, center.z));
    drawModel(mdlCube_, MatrixMultiply(local, parent), color);
}

void Renderer::drawCar(const rr::Car& car, int index) {
    const auto& st = car.state;
    Matrix M = MatrixMultiply(MatrixRotateY(st.yaw), MatrixTranslate(st.pos.x, 0, -st.pos.y));
    Color body = teamColor(index), accent = teamAccent(index);
    Color carbon = {32, 32, 36, 255};
    float spec = 0.6f;
    if (current_ == &lit_) SetShaderValue(lit_, locSpec_, &spec, SHADER_UNIFORM_FLOAT);

    // Local frame: x forward, y up, z to the right.
    drawBox({0.0f, 0.22f, 0}, {4.3f, 0.14f, 1.45f}, carbon, M);          // floor
    drawBox({0.25f, 0.42f, 0}, {2.9f, 0.34f, 0.78f}, body, M);            // tub
    drawBox({1.95f, 0.34f, 0}, {1.3f, 0.2f, 0.42f}, body, M);             // nose
    for (float z : {-0.62f, 0.62f}) drawBox({-0.15f, 0.4f, z}, {1.7f, 0.32f, 0.5f}, body, M);  // sidepods
    drawBox({-0.95f, 0.62f, 0}, {1.5f, 0.36f, 0.52f}, body, M);           // engine cover
    drawBox({-0.15f, 0.88f, 0}, {0.36f, 0.3f, 0.3f}, accent, M);          // airbox
    drawBox({2.42f, 0.13f, 0}, {0.5f, 0.06f, 1.9f}, accent, M);           // front wing
    for (float z : {-0.93f, 0.93f}) drawBox({2.42f, 0.22f, z}, {0.55f, 0.22f, 0.04f}, carbon, M);
    drawBox({-2.15f, 0.92f, 0}, {0.45f, 0.06f, 1.35f}, accent, M);        // rear wing
    drawBox({-2.15f, 0.78f, 0}, {0.3f, 0.04f, 1.25f}, body, M);
    for (float z : {-0.67f, 0.67f}) drawBox({-2.15f, 0.72f, z}, {0.55f, 0.48f, 0.04f}, carbon, M);
    drawBox({-1.95f, 0.62f, 0}, {0.12f, 0.4f, 0.08f}, carbon, M);         // wing pylon
    // helmet
    Matrix helmet = MatrixMultiply(MatrixScale(0.17f, 0.17f, 0.17f), MatrixTranslate(0.35f, 0.78f, 0));
    drawModel(mdlSphere_, MatrixMultiply(helmet, M), accent);

    // wheels: cylinder along +y, centred, turned onto the z axle, rolled, steered
    const float r = car.phys.wheelRadius;
    struct W { float x, z, width; bool front; };
    const W wheels[] = {{car.phys.cgToFront, -0.86f, 0.36f, true}, {car.phys.cgToFront, 0.86f, 0.36f, true},
                        {-car.phys.cgToRear, -0.84f, 0.44f, false}, {-car.phys.cgToRear, 0.84f, 0.44f, false}};
    for (const W& w : wheels) {
        Matrix m = MatrixMultiply(MatrixScale(r, w.width, r), MatrixTranslate(0, -w.width * 0.5f, 0));
        m = MatrixMultiply(m, MatrixRotateX(PI / 2));
        m = MatrixMultiply(m, MatrixRotateZ(-st.wheelRot));
        if (w.front) m = MatrixMultiply(m, MatrixRotateY(st.steerAngle));
        m = MatrixMultiply(m, MatrixTranslate(w.x, r, w.z));
        drawModel(mdlWheel_, MatrixMultiply(m, M), Color{22, 22, 24, 255});
        // rim
        Matrix rim = MatrixMultiply(MatrixScale(r * 0.62f, w.width + 0.02f, r * 0.62f),
                                    MatrixTranslate(0, -(w.width + 0.02f) * 0.5f, 0));
        rim = MatrixMultiply(rim, MatrixRotateX(PI / 2));
        if (w.front) rim = MatrixMultiply(rim, MatrixRotateY(st.steerAngle));
        rim = MatrixMultiply(rim, MatrixTranslate(w.x, r, w.z));
        drawModel(mdlWheel_, MatrixMultiply(rim, M), Color{150, 150, 158, 255});
    }
    spec = 0.15f;
    if (current_ == &lit_) SetShaderValue(lit_, locSpec_, &spec, SHADER_UNIFORM_FLOAT);
}

void Renderer::drawScene(const rr::Race& race, bool shadowPass) {
    const Matrix I = MatrixIdentity();
    if (!shadowPass) {
        drawModel(mdlGround_, I, WHITE);
        drawModel(mdlAsphalt_, I, WHITE);
        drawModel(mdlMarkings_, I, WHITE);
        drawModel(mdlStart_, I, WHITE);
    }
    drawModel(mdlWalls_, I, WHITE);

    for (size_t i = 0; i < boxes_.size(); ++i) {
        const Box& b = boxes_[i];
        float yaw = (int)i < numGantryBoxes_ ? gantryYaw_ : standYaw_;
        drawModel(mdlCube_, boxTransform(b.center, b.size, yaw), b.color);
    }

    for (const Tree& t : trees_) {
        float s = t.scale;
        Color trunk = {92, 66, 45, 255};
        Color leaf = {(unsigned char)(38 + 25 * t.tint), (unsigned char)(88 + 30 * t.tint), (unsigned char)(42 + 10 * t.tint), 255};
        drawModel(mdlTrunk_, MatrixMultiply(MatrixScale(0.28f * s, 2.4f * s, 0.28f * s), MatrixTranslate(t.pos.x, 0, t.pos.z)), trunk);
        drawModel(mdlCone_, MatrixMultiply(MatrixScale(2.4f * s, 5.0f * s, 2.4f * s), MatrixTranslate(t.pos.x, 1.6f * s, t.pos.z)), leaf);
        drawModel(mdlCone_, MatrixMultiply(MatrixScale(1.7f * s, 3.8f * s, 1.7f * s), MatrixTranslate(t.pos.x, 3.9f * s, t.pos.z)), leaf);
    }

    const rr::Track& tr = race.track();
    if (tr.hasPit()) {
        const RRPitInfo& p = tr.pit();
        // Painted boxes in team colours, and the garages behind the pit barrier.
        if (!shadowPass)
            for (size_t i = 0; i < race.cars().size(); ++i) {
                float bs = race.cars()[i].pitBoxS;
                const auto& sm = tr.at(tr.indexAt(bs));
                Vec2 c = tr.pointAt(bs, p.side * (sm.halfWidth + rr::Track::kBoxCentre));
                float yaw = std::atan2(sm.t.y, sm.t.x);
                drawModel(mdlCube_, boxTransform(W(c, 0.012f), {6.5f, 0.02f, 3.6f}, yaw), Color{235, 235, 235, 255});
                drawModel(mdlCube_, boxTransform(W(c, 0.02f), {6.1f, 0.02f, 3.2f}, yaw), Fade(teamColor((int)i), 1.0f));
            }
        float len = std::fmod(p.lane_end_s - p.lane_start_s + tr.length(), tr.length());
        float mid = p.lane_start_s + 0.5f * len;
        const auto& sm = tr.at(tr.indexAt(mid));
        float yaw = std::atan2(sm.t.y, sm.t.x);
        float back = sm.halfWidth + rr::Track::kPitBarrier + 0.6f;
        drawModel(mdlCube_, boxTransform(W(tr.pointAt(mid, p.side * (back + 4.5f)), 2.5f), {len - 20, 5.0f, 9.0f}, yaw),
                  Color{200, 202, 208, 255});
        drawModel(mdlCube_, boxTransform(W(tr.pointAt(mid, p.side * (back + 4.5f)), 5.2f), {len - 16, 0.4f, 10.0f}, yaw),
                  Color{40, 85, 175, 255});
    }

    for (size_t i = 0; i < race.cars().size(); ++i) drawCar(race.cars()[i], (int)i);
}

void Renderer::draw(const rr::Race& race, int focus, const ViewOptions& opt) {
    const rr::Car& fc = race.cars()[focus];

    // --- shadow pass: an orthographic sun camera centred between the car and the view target
    Vector3 centre = Vector3Lerp(W(fc.state.pos), camera.target, 0.5f);
    const float orthoSize = 180.0f;
    const float texel = orthoSize / shadowRes_;
    centre.x = std::floor(centre.x / texel) * texel;
    centre.z = std::floor(centre.z / texel) * texel;
    centre.y = 0;
    Camera3D sun{};
    sun.target = centre;
    sun.position = Vector3Subtract(centre, Vector3Scale(kLightDir, 400.0f));
    sun.up = {0, 1, 0};
    sun.fovy = orthoSize;
    sun.projection = CAMERA_ORTHOGRAPHIC;

    Matrix lightView, lightProj;
    rlSetClipPlanes(1.0, 800.0);  // tight depth range keeps the shadow bias small
    BeginTextureMode(shadowMap_);
    ClearBackground(WHITE);
    BeginMode3D(sun);
    lightView = rlGetMatrixModelview();
    lightProj = rlGetMatrixProjection();
    current_ = &depth_;
    drawScene(race, true);
    EndMode3D();
    EndTextureMode();
    rlSetClipPlanes(0.5, 5000.0);
    lightVP_ = MatrixMultiply(lightView, lightProj);

    // --- main pass (thin the fog for the high overview camera)
    float camHeight = std::max(1.0f, camera.position.y);
    float fogDensity = 0.0016f * std::clamp(40.0f / camHeight, 0.08f, 1.0f);
    SetShaderValue(lit_, locFog_, &fogDensity, SHADER_UNIFORM_FLOAT);
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), kSkyTop, kSkyHorizon);
    BeginMode3D(camera);
    SetShaderValueMatrix(lit_, locLightVP_, lightVP_);
    SetShaderValue(lit_, locViewPos_, &camera.position, SHADER_UNIFORM_VEC3);
    rlEnableShader(lit_.id);
    int slot = 10;
    rlActiveTextureSlot(slot);
    rlEnableTexture(shadowMap_.depth.id);
    rlSetUniform(locShadowMap_, &slot, SHADER_UNIFORM_INT, 1);
    rlActiveTextureSlot(0);
    current_ = &lit_;
    drawScene(race, false);

    // --- debug overlays (unlit)
    if (opt.showPaths) {
        static std::vector<float> xy(2 * 8192);
        for (size_t i = 0; i < race.cars().size(); ++i) {
            const rr::Car& c = race.cars()[i];
            auto fn = c.module ? c.module->api()->debug_path : nullptr;
            if (!fn || !c.robot) continue;
            int count = fn(c.robot, xy.data(), (int)xy.size() / 2);
            Color col = Fade(teamColor((int)i), 0.85f);
            float h = 0.06f + 0.01f * i;
            for (int k = 0; k + 1 < count; ++k)
                DrawLine3D({xy[2 * k], h, -xy[2 * k + 1]}, {xy[2 * k + 2], h, -xy[2 * k + 3]}, col);
        }
    }
    if (opt.showSensors) {
        const auto& s = fc.sensors;
        Vector3 o = W(fc.state.pos, 0.5f);
        for (int k = 0; k < RR_NUM_TRACK_SENSORS; ++k) {
            if (s.track[k] < 0) continue;
            float ang = fc.state.yaw + fc.robotCfg.track_sensor_angles[k] * DEG2RAD;
            Vec2 d = rr::fromAngle(ang) * s.track[k];
            Vector3 e = W(fc.state.pos + d, 0.5f);
            DrawLine3D(o, e, Color{255, 230, 60, 200});
            DrawSphere(e, 0.25f, Color{255, 120, 40, 255});
        }
    }
    EndMode3D();
}
