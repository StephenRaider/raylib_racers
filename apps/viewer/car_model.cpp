#include "car_model.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "raymath.h"
#include "rlgl.h"

namespace {

// Team order: index i uses kLiveries[i % 7]; teamColor() in renderer.cpp matches it.
const char* kLiveries[] = {"rosso", "blue_pink", "papaya", "racing_green", "midnight", "silver_teal", "white_navy"};

std::string readFile(const std::string& path) {
    std::string s;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
        std::fclose(f);
    }
    return s;
}

// Reads wheels.<key>.hub = [x, y, z] from car.json. Small on purpose: the file is ours.
bool readHub(const std::string& json, const char* key, float out[3]) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return false;
    p = json.find("\"hub\"", p);
    if (p == std::string::npos) return false;
    p = json.find('[', p);
    if (p == std::string::npos) return false;
    const char* c = json.c_str() + p + 1;
    for (int i = 0; i < 3; ++i) {
        char* end;
        out[i] = std::strtof(c, &end);
        if (end == c) return false;
        c = end;
        while (*c == ',' || *c == ' ' || *c == '\n' || *c == '\r' || *c == '\t') ++c;
    }
    return true;
}

}  // namespace

bool CarModel::load(const std::string& assetsDir, std::string* err) {
    const std::string dir = assetsDir + "/cars/f1_gearari";
    for (const char* f : {"body.glb", "wheel_front.glb", "wheel_rear.glb", "car.json"})
        if (!std::filesystem::exists(dir + "/" + f)) {
            if (err) *err = "missing " + dir + "/" + f;
            return false;
        }

    const std::string json = readFile(dir + "/car.json");
    const char* keys[4] = {"FL", "FR", "RL", "RR"};
    for (int i = 0; i < 4; ++i) {
        float h[3];
        if (!readHub(json, keys[i], h)) {
            if (err) *err = std::string("car.json has no hub for ") + keys[i];
            return false;
        }
        hubs_[i] = {h[0], h[1], h[2], i < 2};
    }
    wheelRadius_ = hubs_[0].y;

    body_ = LoadModel((dir + "/body.glb").c_str());
    wheelFront_ = LoadModel((dir + "/wheel_front.glb").c_str());
    wheelRear_ = LoadModel((dir + "/wheel_rear.glb").c_str());
    if (body_.meshCount == 0 || wheelFront_.meshCount == 0 || wheelRear_.meshCount == 0) {
        if (err) *err = "could not load the F1 car models from " + dir;
        unload();
        return false;
    }

    // raylib drops glTF material names; the livery is the material with the 2048-wide texture.
    for (int i = 0; i < body_.materialCount; ++i)
        if (body_.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture.width == 2048) liveryMaterial_ = i;
    if (liveryMaterial_ >= 0) {
        defaultLivery_ = body_.materials[liveryMaterial_].maps[MATERIAL_MAP_DIFFUSE].texture;
        for (const char* name : kLiveries) {
            std::string path = dir + "/liveries/" + name + ".png";
            if (!std::filesystem::exists(path)) continue;
            Texture2D t = LoadTexture(path.c_str());
            if (t.id == 0) continue;
            GenTextureMipmaps(&t);
            SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
            liveries_.push_back(t);
        }
    }
    for (Model* m : {&body_, &wheelFront_, &wheelRear_})
        for (int i = 0; i < m->materialCount; ++i) {
            Texture2D& t = m->materials[i].maps[MATERIAL_MAP_DIFFUSE].texture;
            if (t.id != rlGetTextureIdDefault() && t.mipmaps <= 1) {
                GenTextureMipmaps(&t);
                SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
            }
        }
    loaded_ = true;
    return true;
}

void CarModel::unload() {
    const Shader def{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
    if (liveryMaterial_ >= 0) body_.materials[liveryMaterial_].maps[MATERIAL_MAP_DIFFUSE].texture = defaultLivery_;
    for (Model* m : {&body_, &wheelFront_, &wheelRear_}) {
        if (m->meshCount == 0) continue;
        for (int i = 0; i < m->materialCount; ++i) m->materials[i].shader = def;
        UnloadModel(*m);
        *m = Model{};
    }
    for (Texture2D& t : liveries_) UnloadTexture(t);
    liveries_.clear();
    liveryMaterial_ = -1;
    loaded_ = false;
}

void CarModel::drawPart(Model& m, Matrix transform, Shader shader) {
    for (int i = 0; i < m.materialCount; ++i) m.materials[i].shader = shader;
    m.transform = transform;
    DrawModel(m, {0, 0, 0}, 1.0f, WHITE);
}

void CarModel::draw(const Pose& pose, Shader shader) {
    // Model frame (+Z forward, +X left) to the sim's local frame (+X forward, +Z right), wheelbase centre
    // moved ahead of the CG.
    const Matrix toSim = MatrixMultiply(MatrixRotateY(PI / 2), MatrixTranslate(pose.centreOffset, 0, 0));
    const Matrix base = MatrixMultiply(toSim, pose.world);

    if (liveryMaterial_ >= 0 && !liveries_.empty())
        body_.materials[liveryMaterial_].maps[MATERIAL_MAP_DIFFUSE].texture =
            liveries_[((pose.livery % (int)liveries_.size()) + (int)liveries_.size()) % (int)liveries_.size()];
    drawPart(body_, base, shader);

    for (const Hub& h : hubs_) {
        const bool right = h.x < 0;  // +X is the car's left
        Matrix m = right ? MatrixRotateY(PI) : MatrixIdentity();
        m = MatrixMultiply(m, MatrixRotateX(pose.wheelRot));
        if (h.front) m = MatrixMultiply(m, MatrixRotateY(pose.steer));
        m = MatrixMultiply(m, MatrixTranslate(h.x, h.y, h.z));
        drawPart(h.front ? wheelFront_ : wheelRear_, MatrixMultiply(m, base), shader);
    }
}
